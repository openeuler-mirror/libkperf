//===- SymbolizableObjectFile.cpp -----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of SymbolizableObjectFile class.
//
//===----------------------------------------------------------------------===//

#include "SymbolizableObjectFile.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolSize.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/BinaryFormat/ELF.h"

#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"

#include <sstream>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace llvm;
using namespace object;
using namespace symbolize;

static DILineInfoSpecifier
getDILineInfoSpecifier(FunctionNameKind FNKind) {
  return DILineInfoSpecifier(
      DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath, FNKind);
}

ErrorOr<std::unique_ptr<SymbolizableObjectFile>>
SymbolizableObjectFile::create(object::ObjectFile *Obj,
                               std::unique_ptr<DIContext> DICtx) {
  std::unique_ptr<SymbolizableObjectFile> res(
      new SymbolizableObjectFile(Obj, std::move(DICtx)));
  std::unique_ptr<DataExtractor> OpdExtractor;
  uint64_t OpdAddress = 0;
  std::vector<std::pair<SymbolRef, uint64_t>> Symbols =
      computeSymbolSizes(*Obj);
  for (auto &P : Symbols)
    res->addSymbol(P.first, P.second, OpdExtractor.get(), OpdAddress);

  // If this is a COFF object and we didn't find any symbols, try the export
  // table.
  if (Symbols.empty()) {
    if (auto *CoffObj = dyn_cast<COFFObjectFile>(Obj))
      if (auto EC = res->addCoffExportSymbols(CoffObj))
        return EC;
  }
  res->parsePLTSection(Obj);
  return std::move(res);
}

SymbolizableObjectFile::SymbolizableObjectFile(ObjectFile *Obj,
                                               std::unique_ptr<DIContext> DICtx)
    : Module(Obj), DebugInfoContext(std::move(DICtx)) {}

namespace {

struct OffsetNamePair {
  uint32_t Offset;
  StringRef Name;

  bool operator<(const OffsetNamePair &R) const {
    return Offset < R.Offset;
  }
};

} // end anonymous namespace

std::error_code SymbolizableObjectFile::addCoffExportSymbols(
    const COFFObjectFile *CoffObj) {
  // Get all export names and offsets.
  std::vector<OffsetNamePair> ExportSyms;
  for (const ExportDirectoryEntryRef &Ref : CoffObj->export_directories()) {
    StringRef Name;
    uint32_t Offset;
    if (auto EC = Ref.getSymbolName(Name))
      return EC;
    if (auto EC = Ref.getExportRVA(Offset))
      return EC;
    ExportSyms.push_back(OffsetNamePair{Offset, Name});
  }
  if (ExportSyms.empty())
    return std::error_code();

  // Sort by ascending offset.
  array_pod_sort(ExportSyms.begin(), ExportSyms.end());

  // Approximate the symbol sizes by assuming they run to the next symbol.
  // FIXME: This assumes all exports are functions.
  uint64_t ImageBase = CoffObj->getImageBase();
  for (auto I = ExportSyms.begin(), E = ExportSyms.end(); I != E; ++I) {
    OffsetNamePair &Export = *I;
    // FIXME: The last export has a one byte size now.
    uint32_t NextOffset = I != E ? I->Offset : Export.Offset + 1;
    uint64_t SymbolStart = ImageBase + Export.Offset;
    uint64_t SymbolSize = NextOffset - Export.Offset;
    SymbolDesc SD = {SymbolStart, SymbolSize};
    Functions.insert(std::make_pair(SD, Export.Name));
  }
  return std::error_code();
}

std::error_code SymbolizableObjectFile::addSymbol(const SymbolRef &Symbol,
                                                  uint64_t SymbolSize,
                                                  DataExtractor *OpdExtractor,
                                                  uint64_t OpdAddress) {
  Expected<SymbolRef::Type> SymbolTypeOrErr = Symbol.getType();
  if (!SymbolTypeOrErr)
    return errorToErrorCode(SymbolTypeOrErr.takeError());
  SymbolRef::Type SymbolType = *SymbolTypeOrErr;
  if (SymbolType != SymbolRef::ST_Function && SymbolType != SymbolRef::ST_Data)
    return std::error_code();
  Expected<uint64_t> SymbolAddressOrErr = Symbol.getAddress();
  if (!SymbolAddressOrErr)
    return errorToErrorCode(SymbolAddressOrErr.takeError());
  uint64_t SymbolAddress = *SymbolAddressOrErr;
  if (OpdExtractor) {
    // For big-endian PowerPC64 ELF, symbols in the .opd section refer to
    // function descriptors. The first word of the descriptor is a pointer to
    // the function's code.
    // For the purposes of symbolization, pretend the symbol's address is that
    // of the function's code, not the descriptor.
    uint64_t OpdOffset = SymbolAddress - OpdAddress;
    uint32_t OpdOffset32 = OpdOffset;
    if (OpdOffset == OpdOffset32 &&
        OpdExtractor->isValidOffsetForAddress(OpdOffset32))
      SymbolAddress = OpdExtractor->getAddress(&OpdOffset32);
  }
  Expected<StringRef> SymbolNameOrErr = Symbol.getName();
  if (!SymbolNameOrErr)
    return errorToErrorCode(SymbolNameOrErr.takeError());
  StringRef SymbolName = *SymbolNameOrErr;
  // Mach-O symbol table names have leading underscore, skip it.
  if (Module->isMachO() && !SymbolName.empty() && SymbolName[0] == '_')
    SymbolName = SymbolName.drop_front();
  // FIXME: If a function has alias, there are two entries in symbol table
  // with same address size. Make sure we choose the correct one.
  auto &M = SymbolType == SymbolRef::ST_Function ? Functions : Objects;
  SymbolDesc SD = { SymbolAddress, SymbolSize };
  M.insert(std::make_pair(SD, SymbolName));
  return std::error_code();
}

// Return true if this is a 32-bit x86 PE COFF module.
bool SymbolizableObjectFile::isWin32Module() const {
  auto *CoffObject = dyn_cast<COFFObjectFile>(Module);
  return CoffObject && CoffObject->getMachine() == COFF::IMAGE_FILE_MACHINE_I386;
}

uint64_t SymbolizableObjectFile::getModulePreferredBase() const {
  if (auto *CoffObject = dyn_cast<COFFObjectFile>(Module))
    return CoffObject->getImageBase();
  return 0;
}

bool SymbolizableObjectFile::getNameFromSymbolTable(SymbolRef::Type Type,
                                                    uint64_t Address,
                                                    std::string &Name,
                                                    uint64_t &Addr,
                                                    uint64_t &Size) const {
  const auto &SymbolMap = Type == SymbolRef::ST_Function ? Functions : Objects;
  if (SymbolMap.empty())
    return false;
  SymbolDesc SD = { Address, Address };
  auto SymbolIterator = SymbolMap.upper_bound(SD);
  if (SymbolIterator == SymbolMap.begin())
    return false;
  --SymbolIterator;
  if (SymbolIterator->first.Size != 0 &&
      SymbolIterator->first.Addr + SymbolIterator->first.Size <= Address)
    return false;
  if (SymbolIterator->first.Size == 0 && SymbolIterator->first.Addr == Address)
    return false;
  Name = SymbolIterator->second.str();
  Addr = SymbolIterator->first.Addr;
  Size = SymbolIterator->first.Size;
  return true;
}

bool SymbolizableObjectFile::shouldOverrideWithSymbolTable(
    FunctionNameKind FNKind, bool UseSymbolTable) const {
  // When DWARF is used with -gline-tables-only / -gmlt, the symbol table gives
  // better answers for linkage names than the DIContext. Otherwise, we are
  // probably using PEs and PDBs, and we shouldn't do the override. PE files
  // generally only contain the names of exported symbols.
  return FNKind == FunctionNameKind::LinkageName && UseSymbolTable &&
         isa<DWARFContext>(DebugInfoContext.get());
}

DILineInfo SymbolizableObjectFile::getPLTSymbol(uint64_t ModuleOffset) const {
    DILineInfo LineInfo;
    PLTEntry PLTEntry;
    if (isPLTAddr(ModuleOffset, &PLTEntry)) {
        LineInfo.FunctionName = PLTEntry.Name;
        LineInfo.FileName = ".plt";
        LineInfo.Line = 0;
        LineInfo.Column = 0;
        LineInfo.CodeEndAddr = PLTEntry.Addr + PLTEntry.Size;
        LineInfo.Offset = ModuleOffset - PLTEntry.Addr;
    }
    return LineInfo;
}

DILineInfo SymbolizableObjectFile::symbolizeCode(uint64_t ModuleOffset,
                                                 FunctionNameKind FNKind,
                                                 bool UseSymbolTable) const {
  DILineInfo LineInfo;
  PLTEntry PLTEntry;
  if (isPLTAddr(ModuleOffset, &PLTEntry)) {
      LineInfo.FunctionName = PLTEntry.Name;
      LineInfo.FileName = ".plt";
      LineInfo.Line = 0;
      LineInfo.Column = 0;
      LineInfo.CodeEndAddr = PLTEntry.Addr + PLTEntry.Size;
      LineInfo.Offset = ModuleOffset - PLTEntry.Addr;
      return LineInfo;
  }
  if (DebugInfoContext) {
    LineInfo = DebugInfoContext->getLineInfoForAddress(
        ModuleOffset, getDILineInfoSpecifier(FNKind));
  }
  // Override function name from symbol table if necessary.
  if (shouldOverrideWithSymbolTable(FNKind, UseSymbolTable)) {
    std::string FunctionName;
    uint64_t Start, Size;
    if (getNameFromSymbolTable(SymbolRef::ST_Function, ModuleOffset,
                               FunctionName, Start, Size)) {
      LineInfo.FunctionName = FunctionName;
      LineInfo.CodeEndAddr = Start + Size;
      LineInfo.Offset = ModuleOffset - Start;
    }
  }
  return LineInfo;
}

DIInliningInfo SymbolizableObjectFile::symbolizeInlinedCode(
    uint64_t ModuleOffset, FunctionNameKind FNKind, bool UseSymbolTable) const {
  DIInliningInfo InlinedContext;

  if (DebugInfoContext)
    InlinedContext = DebugInfoContext->getInliningInfoForAddress(
        ModuleOffset, getDILineInfoSpecifier(FNKind));
  // Make sure there is at least one frame in context.
  if (InlinedContext.getNumberOfFrames() == 0)
    InlinedContext.addFrame(DILineInfo());

  // Override the function name in lower frame with name from symbol table.
  if (shouldOverrideWithSymbolTable(FNKind, UseSymbolTable)) {
    std::string FunctionName;
    uint64_t Start, Size;
    if (getNameFromSymbolTable(SymbolRef::ST_Function, ModuleOffset,
                               FunctionName, Start, Size)) {
      InlinedContext.getMutableFrame(InlinedContext.getNumberOfFrames() - 1)
          ->FunctionName = FunctionName;
    }
  }

  return InlinedContext;
}

DIGlobal SymbolizableObjectFile::symbolizeData(uint64_t ModuleOffset) const {
  DIGlobal Res;
  getNameFromSymbolTable(SymbolRef::ST_Data, ModuleOffset, Res.Name, Res.Start,
                         Res.Size);
  return Res;
}


static uint64_t getPLTEntrySize(Triple::ArchType Arch) {
    switch (Arch) {
        case Triple::x86:
            return 16;
        case Triple::x86_64:
            return 16;
        case Triple::aarch64:
            return 16;
        case Triple::arm:
            return 12;
        default:
            return 16;
    }
}

static uint64_t getFirstEntrySize(Triple::ArchType Arch) {
    switch (Arch) {
        case Triple::x86:
            return 16;
        case Triple::x86_64:
            return 16;
        case Triple::aarch64:
            return 32;
        case Triple::arm:
            return 12;
        default:
            return 16;
    }
}

static bool parseDynSym(StringRef Contents, std::vector<std::string>& DynSymbols, const char* DynstrTable, size_t DynstrSize) {
    const size_t EntrySize = sizeof(ELF::Elf64_Sym);
    const size_t EntryNum = Contents.size() / EntrySize;
    if (Contents.size() % EntrySize != 0) {
        return false;
    }

    for (size_t I = 0; I < EntryNum; I++) {
        const char* EntryData = Contents.data() + I * EntrySize;
        uint32_t StName;
        memcpy(&StName, EntryData, sizeof(ELF::Elf64_Sym::st_name));
        std::string Name;
        if (StName < DynstrSize) {
            const char* NamePtr = DynstrTable + StName;
            Name = NamePtr;
        }
        DynSymbols.push_back(Name);
    }
    return true;
}

static bool parseRelaPLT(StringRef Contents, std::vector<uint32_t>& RelaEntries, bool HasGot, std::map<uint64_t, uint32_t>& GotToSymbol) {
    const size_t EntrySize = sizeof(ELF::Elf64_Rela);
    const size_t EntryNum = Contents.size() / EntrySize;
    for (size_t I = 0; I < EntryNum; I++) {
        const char* EntryData = Contents.data() + I * EntrySize;
        uint64_t RInfo;
        
        memcpy(&RInfo, EntryData + sizeof(ELF::Elf64_Rela::r_offset), sizeof(ELF::Elf64_Rela::r_info));
       
        const uint32_t SymIndex = (RInfo >> 32);
        RelaEntries.push_back(SymIndex);
        if (HasGot) {
           uint64_t ROffset;
           memcpy(&ROffset, EntryData, sizeof(ELF::Elf64_Rela::r_offset));
           GotToSymbol[ROffset] = SymIndex;
        }
    }
    return true;
}

uint64_t ExtractGoTAddress(uint8_t* Code, uint64_t Addr) {
    if (Code[0] == 0xff && Code[1] == 0x25) {
        int32_t offset;
        memcpy(&offset, &Code[2], 4);
        uint64_t rip = Addr + 6;
        return rip + offset;
    }
    return 0;
}

void SymbolizableObjectFile::parsePLTSection(const object::ObjectFile *Obj) {
    if (!Obj->isELF()) {
        return;
    }
    std::vector<std::string> DynSymbols;
    std::vector<uint32_t> RelaEntries;
    const char* DynstrTable = nullptr;
    size_t DynstrSize = 0;
    auto* ELFObj = dyn_cast<ELF64LEObjectFile>(Obj);
    uint64_t PLTStart, PLTSize;
    StringRef PLTContents, DynStrContents, DynsymContents, RelaPLTContents;
    bool HasGot = false; // if .plt.got section exists, need got_symbols.
    std::map<uint64_t, uint32_t> GotToSymbol;
    for (const auto& Section : ELFObj->sections()) {
        StringRef SecName;
        if (Section.getName(SecName)) {
            continue;
        }
        if (SecName.str() == ".plt") {
            if (Section.getContents(PLTContents)) {
                return;
            }
            PLTStart = Section.getAddress();
            PLTSize = Section.getSize();
        } else if (SecName.str() == ".dynstr") {
            if (Section.getContents(DynStrContents)) {
                return;
            }
            DynstrTable = DynStrContents.data();
            DynstrSize = DynStrContents.size();
        } else if (SecName.str() == ".rela.plt") {
            if (Section.getContents(RelaPLTContents)) {
                return;
            }
        } else if (SecName.str() == ".dynsym") {
            if (Section.getContents(DynsymContents)) {
                return;
            }
        } else if (SecName.str() == ".plt.got") {
            HasGot = true;
        }
    }

    if (!parseDynSym(DynsymContents, DynSymbols, DynstrTable, DynstrSize)) {
        return;
    }

    if (!parseRelaPLT(RelaPLTContents, RelaEntries, HasGot, GotToSymbol)) {
        return;
    }

    const uint64_t PLTEntrySize = getPLTEntrySize(Obj->getArch());
    const uint64_t FirstEntrySize = getFirstEntrySize(Obj->getArch());
    PLTSymbols[PLTStart] = {.Addr = PLTStart, .Name = "unkown@plt", .Size = FirstEntrySize};
    const uint64_t BaseAddr = PLTStart + FirstEntrySize;
    const size_t NumEntries = (PLTSize - FirstEntrySize) / PLTEntrySize;
    for (size_t I = 0; I < NumEntries; ++I) {
        const uint64_t Addr = BaseAddr + I * PLTEntrySize;
        if (Addr < PLTStart + PLTSize) {
            if (I >= RelaEntries.size()) {
                return;
            }
            uint32_t SymIndex = 0;
            if (HasGot) {
               const char* EntryData = PLTContents.data() + I * PLTEntrySize + FirstEntrySize;
               uint8_t* Ptr = (uint8_t*)EntryData;
               std::vector<uint8_t> Code;
               Code.assign(Ptr, Ptr + PLTEntrySize);
               uint64_t GotAddr = ExtractGoTAddress(Ptr, Addr);
               if (GotAddr > 0 && GotToSymbol.find(GotAddr) != GotToSymbol.end()) {
                  SymIndex = GotToSymbol[GotAddr];
               }
            } else {
               SymIndex = RelaEntries[I];
            }
            if (SymIndex >= DynSymbols.size()) {
                continue;
            }
            auto SymName = DynSymbols[SymIndex];
            if (SymName.empty()) {
                SymName = "unkown@plt";
            } else {
                SymName = SymName + "@plt";
            }
            PLTEntry Entry;
            Entry.Addr = Addr;
            Entry.Name = SymName;
            Entry.Size = PLTEntrySize;
            PLTSymbols[Addr] = Entry;
        }
    }
}

bool SymbolizableObjectFile::isPLTAddr(uint64_t Address, PLTEntry *Entry) const {
    auto It = PLTSymbols.lower_bound(Address);
    if (It != PLTSymbols.end()) {
        if (Address == It->first) {
            if (Entry) *Entry = It->second;
            return true;
        }
    }

    if (It != PLTSymbols.begin()) {
        --It;
        if (Address >= It->first && Address < It->first + It->second.Size) {
            if (Entry) *Entry = It->second;
            return true;
        }
    }

    return false;
}

typedef std::function<bool(llvm::object::SectionRef const &)> FilterPredicate;
typedef std::vector<std::tuple<uint64_t, StringRef, uint8_t>> SectionSymbolsTy;

class SectionFilterIterator {
public:
  SectionFilterIterator(FilterPredicate P,
                        llvm::object::section_iterator const &I,
                        llvm::object::section_iterator const &E)
      : Predicate(std::move(P)), Iterator(I), End(E) {
    ScanPredicate();
  }
  const llvm::object::SectionRef &operator*() const { return *Iterator; }
  SectionFilterIterator &operator++() {
    ++Iterator;
    ScanPredicate();
    return *this;
  }
  bool operator!=(SectionFilterIterator const &Other) const {
    return Iterator != Other.Iterator;
  }

private:
  void ScanPredicate() {
    while (Iterator != End && !Predicate(*Iterator)) {
      ++Iterator;
    }
  }
  FilterPredicate Predicate;
  llvm::object::section_iterator Iterator;
  llvm::object::section_iterator End;
};

class SectionFilter {
public:
  SectionFilter(FilterPredicate P, llvm::object::ObjectFile const &O)
      : Predicate(std::move(P)), Object(O) {}
  SectionFilterIterator begin() {
    return SectionFilterIterator(Predicate, Object.section_begin(),
                                 Object.section_end());
  }
  SectionFilterIterator end() {
    return SectionFilterIterator(Predicate, Object.section_end(),
                                 Object.section_end());
  }

private:
  FilterPredicate Predicate;
  llvm::object::ObjectFile const &Object;
};

SectionFilter ToolSectionFilter(llvm::object::ObjectFile const &O) {
  return SectionFilter(
      [](llvm::object::SectionRef const &S) {
          return true;},
      O);
}

static bool isArmElf(const ObjectFile *Obj) {
  return (Obj->isELF() &&
          (Obj->getArch() == Triple::aarch64 ||
           Obj->getArch() == Triple::aarch64_be ||
           Obj->getArch() == Triple::arm || Obj->getArch() == Triple::armeb ||
           Obj->getArch() == Triple::thumb ||
           Obj->getArch() == Triple::thumbeb));
}

class PrettyPrinter {
public:
  virtual ~PrettyPrinter() = default;
  virtual void printInst(MCInstPrinter &IP, const MCInst *MI,
                         ArrayRef<uint8_t> Bytes, uint64_t Address,
                         raw_ostream &OS, StringRef Annot,
                         MCSubtargetInfo const &STI) {
    OS << format("%8" PRIx64 ":", Address);
    OS << "\t";
    dumpBytes(Bytes, OS);
    if (MI)
      IP.printInst(MI, OS, "", STI);
    else
      OS << " <unknown>";
  }
};

static uint8_t getElfSymbolType(const ObjectFile *Obj, const SymbolRef &Sym) {
  if (auto *Elf32LEObj = dyn_cast<ELF32LEObjectFile>(Obj))
    return Elf32LEObj->getSymbol(Sym.getRawDataRefImpl())->getType();
  if (auto *Elf64LEObj = dyn_cast<ELF64LEObjectFile>(Obj))
    return Elf64LEObj->getSymbol(Sym.getRawDataRefImpl())->getType();
  if (auto *Elf32BEObj = dyn_cast<ELF32BEObjectFile>(Obj))
    return Elf32BEObj->getSymbol(Sym.getRawDataRefImpl())->getType();
  if (auto *Elf64BEObj = cast<ELF64BEObjectFile>(Obj))
    return Elf64BEObj->getSymbol(Sym.getRawDataRefImpl())->getType();
  llvm_unreachable("Unsupported binary format");
}

template <class ELFT> static void
addDynamicElfSymbols(const ELFObjectFile<ELFT> *Obj,
                     std::map<SectionRef, SectionSymbolsTy> &AllSymbols) {
  for (auto Symbol : Obj->getDynamicSymbolIterators()) {
    uint8_t SymbolType = Symbol.getELFType();
    if (SymbolType != ELF::STT_FUNC || Symbol.getSize() == 0)
      continue;

    Expected<uint64_t> AddressOrErr = Symbol.getAddress();
    if (!AddressOrErr)
      return;
    uint64_t Address = *AddressOrErr;

    Expected<StringRef> Name = Symbol.getName();
    if (!Name)
      return;
    if (Name->empty())
      continue;

    Expected<section_iterator> SectionOrErr = Symbol.getSection();
    if (!SectionOrErr)
      return;
    section_iterator SecI = *SectionOrErr;
    if (SecI == Obj->section_end())
      continue;

    AllSymbols[*SecI].emplace_back(Address, *Name, SymbolType);
  }
}

static void
addDynamicElfSymbols(const ObjectFile *Obj,
                     std::map<SectionRef, SectionSymbolsTy> &AllSymbols) {
  if (auto *Elf32LEObj = dyn_cast<ELF32LEObjectFile>(Obj))
    addDynamicElfSymbols(Elf32LEObj, AllSymbols);
  else if (auto *Elf64LEObj = dyn_cast<ELF64LEObjectFile>(Obj))
    addDynamicElfSymbols(Elf64LEObj, AllSymbols);
  else if (auto *Elf32BEObj = dyn_cast<ELF32BEObjectFile>(Obj))
    addDynamicElfSymbols(Elf32BEObj, AllSymbols);
  else if (auto *Elf64BEObj = cast<ELF64BEObjectFile>(Obj))
    addDynamicElfSymbols(Elf64BEObj, AllSymbols);
  else
    llvm_unreachable("Unsupported binary format");
}

class StringStreamWrapper : public llvm::raw_ostream {
public:
   explicit StringStreamWrapper(std::stringstream &stream):ss(stream){
      SetUnbuffered();
   }

   ~StringStreamWrapper() override {
      flush();
   }

   std::string str() const { return ss.str(); }
private:
   std::stringstream &ss;
   uint64_t pos = 0;

   void write_impl(const char *ptr, size_t size) override {
      ss.write(ptr, size);
      pos += size;
   }

   uint64_t current_pos() const override {
      return pos;
   }
};

const std::string AsmErrPrefix = "LLVM_ASM_RESOLVE_FAILED: ";

std::string SymbolizableObjectFile::getAsmCode(uint64_t StartAddr, uint64_t EndAddr) const {
   if (!Module) {
      return AsmErrPrefix + "can't find Module";
   }

   llvm::InitializeAllTargetInfos();
   llvm::InitializeAllTargetMCs();
   llvm::InitializeAllDisassemblers();

   std::stringstream Ss;
   StringStreamWrapper Wrapper(Ss);

   if (StartAddr > EndAddr)
      return AsmErrPrefix + "Start Address should be less than end address";
   llvm::Triple TheTriple = Module->makeTriple();
   auto TripleName = TheTriple.getTriple();
   std::string Error;
   const Target *TheTarget = TargetRegistry::lookupTarget("", TheTriple, Error);
   if (!TheTarget) {
      return AsmErrPrefix + "can't find target " + Error;
   }

   const SubtargetFeatures Features = Module->getFeatures();

   std::unique_ptr<const MCRegisterInfo> MRI(
      TheTarget->createMCRegInfo(TripleName));
   if (!MRI)
      return AsmErrPrefix + "no register info for target " + TripleName;
     // Set up disassembler.
   std::unique_ptr<const MCAsmInfo> AsmInfo(
      TheTarget->createMCAsmInfo(*MRI, TripleName));
   if (!AsmInfo)
      return AsmErrPrefix + "no assembly info for target " + TripleName;
   std::unique_ptr<const MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(TripleName, "", Features.getString()));
   if (!STI)
      return AsmErrPrefix + "no subtarget info for target " + TripleName;
   std::unique_ptr<const MCInstrInfo> MII(TheTarget->createMCInstrInfo());
   if (!MII)
      return AsmErrPrefix + "no instruction info for target " + TripleName;
   MCObjectFileInfo MOFI;
   MCContext Ctx(AsmInfo.get(), MRI.get(), &MOFI);
   // FIXME: for now initialize MCObjectFileInfo with default values
   MOFI.InitMCObjectFileInfo(Triple(TripleName), false, Ctx);

   std::unique_ptr<MCDisassembler> DisAsm(
     TheTarget->createMCDisassembler(*STI, Ctx));
   if (!DisAsm)
     return AsmErrPrefix + "no disassembler for target " + TripleName;

   std::unique_ptr<const MCInstrAnalysis> MIA(
       TheTarget->createMCInstrAnalysis(MII.get()));

  int AsmPrinterVariant = AsmInfo->getAssemblerDialect();
  std::unique_ptr<MCInstPrinter> IP(TheTarget->createMCInstPrinter(
      Triple(TripleName), AsmPrinterVariant, *AsmInfo, *MII, *MRI));
  if (!IP)
     return AsmErrPrefix +  "no instruction printer for target " + TripleName;
  IP->setPrintImmHex(false);

  PrettyPrinter PrettyPrinterInst;
  PrettyPrinter &PIP = PrettyPrinterInst;

  std::map<SectionRef, SmallVector<SectionRef, 1>> SectionRelocMap;
  for (const SectionRef &Section : ToolSectionFilter(*Module)) {
    section_iterator Sec2 = Section.getRelocatedSection();
    if (Sec2 != Module->section_end())
      SectionRelocMap[*Sec2].push_back(Section);
  }

  // Create a mapping from virtual address to symbol name.  This is used to
  // pretty print the symbols while disassembling.
  std::map<SectionRef, SectionSymbolsTy> AllSymbols;
  SectionSymbolsTy AbsoluteSymbols;
  for (const SymbolRef &Symbol : Module->symbols()) {
    Expected<uint64_t> AddressOrErr = Symbol.getAddress();
    if (!AddressOrErr)
       return AsmErrPrefix + "symbol get address failed";
    uint64_t Address = *AddressOrErr;

    Expected<StringRef> Name = Symbol.getName();
    if (!Name)
       return AsmErrPrefix + "symbol get name failed";
    if (Name->empty())
      continue;

    Expected<section_iterator> SectionOrErr = Symbol.getSection();
    if (!SectionOrErr)
       return AsmErrPrefix + "symbol get section failed";

    uint8_t SymbolType = ELF::STT_NOTYPE;
    if (Module->isELF())
      SymbolType = getElfSymbolType(Module, Symbol);

    section_iterator SecI = *SectionOrErr;
    if (SecI != Module->section_end())
      AllSymbols[*SecI].emplace_back(Address, *Name, SymbolType);
    else
      AbsoluteSymbols.emplace_back(Address, *Name, SymbolType);
  }

  if (AllSymbols.empty() && Module->isELF())
    addDynamicElfSymbols(Module, AllSymbols);

   // Create a mapping from virtual address to section.
  std::vector<std::pair<uint64_t, SectionRef>> SectionAddresses;
  for (SectionRef Sec : Module->sections())
    SectionAddresses.emplace_back(Sec.getAddress(), Sec);
  array_pod_sort(SectionAddresses.begin(), SectionAddresses.end());
  
  // Sort all the symbols, this allows us to use a simple binary search to find
  // a symbol near an address.
  for (std::pair<const SectionRef, SectionSymbolsTy> &SecSyms : AllSymbols)
    array_pod_sort(SecSyms.second.begin(), SecSyms.second.end());
  array_pod_sort(AbsoluteSymbols.begin(), AbsoluteSymbols.end());

  for (const SectionRef &Section : ToolSectionFilter(*Module)) {
    if ((!Section.isText() || Section.isVirtual()))
      continue;

    uint64_t SectionAddr = Section.getAddress();
    uint64_t SectSize = Section.getSize();
    if (!SectSize)
      continue;

    // Get the list of all the symbols in this section.
    SectionSymbolsTy &Symbols = AllSymbols[Section];
    std::vector<uint64_t> DataMappingSymsAddr;
    std::vector<uint64_t> TextMappingSymsAddr;
    if (isArmElf(Module)) {
      for (const auto &Symb : Symbols) {
        uint64_t Address = std::get<0>(Symb);
        StringRef Name = std::get<1>(Symb);
        if (Name.startswith("$d"))
          DataMappingSymsAddr.push_back(Address - SectionAddr);
        if (Name.startswith("$x"))
          TextMappingSymsAddr.push_back(Address - SectionAddr);
        if (Name.startswith("$a"))
          TextMappingSymsAddr.push_back(Address - SectionAddr);
        if (Name.startswith("$t"))
          TextMappingSymsAddr.push_back(Address - SectionAddr);
      }
    }

    llvm::sort(DataMappingSymsAddr.begin(), DataMappingSymsAddr.end());
    llvm::sort(TextMappingSymsAddr.begin(), TextMappingSymsAddr.end());

    StringRef SectionName;
    Section.getName(SectionName);

    // If the section has no symbol at the start, just insert a dummy one.
    if (Symbols.empty() || std::get<0>(Symbols[0]) != 0) {
      Symbols.insert(
          Symbols.begin(),
          std::make_tuple(SectionAddr, SectionName,
                          Section.isText() ? ELF::STT_FUNC : ELF::STT_OBJECT));
    }

    SmallString<40> Comments;
    raw_svector_ostream CommentStream(Comments);

    StringRef BytesStr;
    Section.getContents(BytesStr);
    ArrayRef<uint8_t> Bytes(reinterpret_cast<const uint8_t *>(BytesStr.data()),
                            BytesStr.size());

    uint64_t Size;
    uint64_t Index;
    // Disassemble symbol by symbol.
    for (unsigned Si = 0, Se = Symbols.size(); Si != Se; ++Si) {
      uint64_t Start = std::get<0>(Symbols[Si]) - SectionAddr;
      // The end is either the section end or the beginning of the next
      // symbol.
      uint64_t End =
          (Si == Se - 1) ? SectSize : std::get<0>(Symbols[Si + 1]) - SectionAddr;
      // Don't try to disassemble beyond the end of section contents.
      if (End > SectSize)
        End = SectSize;
      // If this symbol has the same address as the next symbol, then skip it.
      if (Start >= End)
        continue;

      // Check if we need to skip symbol
      // Skip if the symbol's data is not between StartAddress and StopAddress
      if (End + SectionAddr < StartAddr ||
          Start + SectionAddr > EndAddr) {
        continue;
      }

      // Stop disassembly at the stop address specified
      if (End + SectionAddr > EndAddr)
        End = EndAddr - SectionAddr;

      for (Index = Start; Index < End; Index += Size) {
        MCInst Inst;

        if (Index + SectionAddr < StartAddr ||
            Index + SectionAddr > EndAddr) {
          // skip byte by byte till StartAddress is reached
          Size = 1;
          continue;
        }
        // AArch64 ELF binaries can interleave data and text in the
        // same section. We rely on the markers introduced to
        // understand what we need to dump. If the data marker is within a
        // function, it is denoted as a word/short etc
        if (isArmElf(Module) && std::get<2>(Symbols[Si]) != ELF::STT_OBJECT) {
          uint64_t Stride = 0;

          auto DAI = std::lower_bound(DataMappingSymsAddr.begin(),
                                      DataMappingSymsAddr.end(), Index);
          if (DAI != DataMappingSymsAddr.end() && *DAI == Index) {
            // Switch to data.
            while (Index < End) {
              Wrapper << format("%8" PRIx64 ":", SectionAddr + Index);
              Wrapper << "\t";
              if (Index + 4 <= End) {
                Stride = 4;
                dumpBytes(Bytes.slice(Index, 4), Wrapper);
                Wrapper << "\t.word\t";
                uint32_t Data = 0;
                if (Module->isLittleEndian()) {
                  const auto Word =
                      reinterpret_cast<const support::ulittle32_t *>(
                          Bytes.data() + Index);
                  Data = *Word;
                } else {
                  const auto Word = reinterpret_cast<const support::ubig32_t *>(
                      Bytes.data() + Index);
                  Data = *Word;
                }
                Wrapper << "0x" << format("%08" PRIx32, Data);
              } else if (Index + 2 <= End) {
                Stride = 2;
                dumpBytes(Bytes.slice(Index, 2), Wrapper);
                Wrapper << "\t\t.short\t";
                uint16_t Data = 0;
                if (Module->isLittleEndian()) {
                  const auto Short =
                      reinterpret_cast<const support::ulittle16_t *>(
                          Bytes.data() + Index);
                  Data = *Short;
                } else {
                  const auto Short =
                      reinterpret_cast<const support::ubig16_t *>(Bytes.data() +
                                                                  Index);
                  Data = *Short;
                }
                Wrapper << "0x" << format("%04" PRIx16, Data);
              } else {
                Stride = 1;
                dumpBytes(Bytes.slice(Index, 1), Wrapper);
                Wrapper << "\t\t.byte\t";
                Wrapper << "0x" << format("%02" PRIx8, Bytes.slice(Index, 1)[0]);
              }
              Index += Stride;
              Wrapper << "\n";
              auto TAI = std::lower_bound(TextMappingSymsAddr.begin(),
                                          TextMappingSymsAddr.end(), Index);
              if (TAI != TextMappingSymsAddr.end() && *TAI == Index)
                break;
            }
          }
        }

        // If there is a data symbol inside an ELF text section and we are only
        // disassembling text (applicable all architectures),
        // we are in a situation where we must print the data and not
        // disassemble it.
        if (Module->isELF() && std::get<2>(Symbols[Si]) == ELF::STT_OBJECT && Section.isText()) {
          // print out data up to 8 bytes at a time in hex and ascii
          uint8_t AsciiData[9] = {'\0'};
          uint8_t Byte;
          int NumBytes = 0;

          for (Index = Start; Index < End; Index += 1) {
            if (((SectionAddr + Index) < StartAddr) ||
                ((SectionAddr + Index) > EndAddr))
              continue;
            if (NumBytes == 0) {
              Wrapper << format("%8" PRIx64 ":", SectionAddr + Index);
              Wrapper << "\t";
            }
            Byte = Bytes.slice(Index)[0];
            Wrapper << format(" %02x", Byte);
            AsciiData[NumBytes] = isPrint(Byte) ? Byte : '.';

            uint8_t IndentOffset = 0;
            NumBytes++;
            if (Index == End - 1 || NumBytes > 8) {
              // Indent the space for less than 8 bytes data.
              // 2 spaces for byte and one for space between bytes
              IndentOffset = 3 * (8 - NumBytes);
              for (int Excess = 8 - NumBytes; Excess < 8; Excess++)
                AsciiData[Excess] = '\0';
              NumBytes = 8;
            }
            if (NumBytes == 8) {
              AsciiData[8] = '\0';
              Wrapper << std::string(IndentOffset, ' ') << "         ";
              Wrapper << reinterpret_cast<char *>(AsciiData);
              Wrapper << '\n';
              NumBytes = 0;
            }
          }
        }
        if (Index >= End)
          break;

        // Disassemble a real instruction or a data when disassemble all is
        // provided
        bool Disassembled = DisAsm->getInstruction(Inst, Size, Bytes.slice(Index),
                                                   SectionAddr + Index, nulls(),
                                                   CommentStream);
        if (Size == 0)
          Size = 1;

        PIP.printInst(*IP, Disassembled ? &Inst : nullptr,
                      Bytes.slice(Index, Size), SectionAddr + Index, Wrapper, "",
                      *STI);
        Wrapper << CommentStream.str();
        Comments.clear();

        // Try to resolve the target of a call, tail call, etc. to a specific
        // symbol.
        if (MIA && (MIA->isCall(Inst) || MIA->isUnconditionalBranch(Inst) ||
                    MIA->isConditionalBranch(Inst))) {
          uint64_t Target;
          if (MIA->evaluateBranch(Inst, SectionAddr + Index, Size, Target)) {
            // In a relocatable object, the target's section must reside in
            // the same section as the call instruction or it is accessed
            // through a relocation.
            //
            // In a non-relocatable object, the target may be in any section.
            //
            // N.B. We don't walk the relocations in the relocatable case yet.
            auto *TargetSectionSymbols = &Symbols;
            if (!Module->isRelocatableObject()) {
              auto SectionAddress = std::upper_bound(
                  SectionAddresses.begin(), SectionAddresses.end(), Target,
                  [](uint64_t LHS,
                      const std::pair<uint64_t, SectionRef> &RHS) {
                    return LHS < RHS.first;
                  });
              if (SectionAddress != SectionAddresses.begin()) {
                --SectionAddress;
                TargetSectionSymbols = &AllSymbols[SectionAddress->second];
              } else {
                TargetSectionSymbols = &AbsoluteSymbols;
              }
            }

            // Find the first symbol in the section whose offset is less than
            // or equal to the target. If there isn't a section that contains
            // the target, find the nearest preceding absolute symbol.
            auto TargetSym = std::upper_bound(
                TargetSectionSymbols->begin(), TargetSectionSymbols->end(),
                Target, [](uint64_t LHS,
                           const std::tuple<uint64_t, StringRef, uint8_t> &RHS) {
                  return LHS < std::get<0>(RHS);
                });
            if (TargetSym == TargetSectionSymbols->begin()) {
              TargetSectionSymbols = &AbsoluteSymbols;
              TargetSym = std::upper_bound(
                  AbsoluteSymbols.begin(), AbsoluteSymbols.end(),
                  Target, [](uint64_t LHS,
                             const std::tuple<uint64_t, StringRef, uint8_t> &RHS) {
                            return LHS < std::get<0>(RHS);
                          });
            }
            if (TargetSym != TargetSectionSymbols->begin()) {
              --TargetSym;
              uint64_t TargetAddress = std::get<0>(*TargetSym);
              StringRef TargetName = std::get<1>(*TargetSym);
              Wrapper << " <" << TargetName;
              uint64_t Disp = Target - TargetAddress;
              if (Disp)
                Wrapper << "+0x" << Twine::utohexstr(Disp);
              Wrapper << '>';
            }
          }
        }
        Wrapper << "\n";
      }
    }
  }
  return Ss.str();
}