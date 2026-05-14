/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Xie Jingwei
 * Create: 2026-01-21
 * Description: Implementation of ELF parsing and probe point extraction
 ******************************************************************************/

#include "elf_scanner.h"

#ifdef UTRACE

#include "pcerr.h"
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <capstone/capstone.h>

std::unordered_map<std::string, std::string> ElfScanner::failedElf2Reason_;
std::unordered_map<std::string, std::vector<std::string>> ElfScanner::elf2FailedSymbols_;

MappedFile::MappedFile(const std::string &path) : fd_(-1), size_(0), base_(MAP_FAILED)
{
    fd_ = open(path.c_str(), O_RDONLY);
    if (fd_ == -1) {
        pcerr::New(LIBPERF_ERR_UTRACE_ELF_SCAN_FAILED, "Failed to open file");
        return;
    }
    struct stat st;
    if (fstat(fd_, &st) == -1) {
        close(fd_);
        fd_ = -1;
        pcerr::New(LIBPERF_ERR_UTRACE_ELF_SCAN_FAILED, "Failed to get file status");
        return;
    }
    size_ = st.st_size;
    base_ = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (base_ == MAP_FAILED) {
        close(fd_);
        fd_ = -1;
        base_ = nullptr;
        pcerr::New(LIBPERF_ERR_UTRACE_ELF_SCAN_FAILED, "Failed to mmap file");
    }
}

MappedFile::~MappedFile()
{
    if (base_ != MAP_FAILED) {
        munmap(base_, size_);
        if (fd_ != -1) {
            close(fd_);
        }
    }
}

void *MappedFile::GetBase() const
{
    return base_;
}

size_t MappedFile::GetSize() const
{
    return size_;
}

std::unordered_map<std::string, std::vector<ProbePoints>> ElfScanner::ResolveElfs(
    const std::unordered_map<std::string, std::vector<std::string>> &module2Symbols) 
{
    std::unordered_map<std::string, std::vector<ProbePoints>> module2ProbePoints;
    for (const auto &pair : module2Symbols) {
        const std::string &filePath = pair.first;
        const std::vector<std::string> &symbols = pair.second;

        std::vector<ProbePoints> probePoints = ResolveElf(filePath, symbols);
        if (!probePoints.empty()) {
            module2ProbePoints[filePath] = std::move(probePoints);
        }
    }
    return module2ProbePoints;
}

const Elf64_Ehdr *ElfScanner::VerifyElfHeader(const char *base, size_t fileSize)
{
    if (fileSize < sizeof(Elf64_Ehdr)) {
        pcerr::New(LIBPERF_ERR_UTRACE_ELF_SCAN_FAILED, "File too small, not a valid ELF");
        return nullptr;
    }
    const Elf64_Ehdr *ehdr = reinterpret_cast<const Elf64_Ehdr *>(base);
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        pcerr::New(LIBPERF_ERR_UTRACE_ELF_SCAN_FAILED, "Not a valid ELF file");
        return nullptr;
    }
    if (ehdr->e_machine != EM_AARCH64 && ehdr->e_machine != EM_X86_64) {
        pcerr::New(LIBPERF_ERR_UTRACE_ELF_SCAN_FAILED, "Only ARM64 and x86_64 ELF files are supported");
        return nullptr;
    }
    return ehdr;
}

uint64_t ElfScanner::CalLoadBaseAddr(const Elf64_Ehdr *ehdr, const Elf64_Phdr *phdr)
{
    uint64_t baseVirtualAddr = 0;
    bool found = false;
    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type == PT_LOAD) {
            if (!found) {
                baseVirtualAddr = phdr[i].p_vaddr;
                found = true;
            } else {
                baseVirtualAddr = std::min(baseVirtualAddr, phdr[i].p_vaddr);
            }
        }
    }
    return found ? baseVirtualAddr : 0;
}

std::unordered_map<std::string, ElfScanner::ElfSymEntry> ElfScanner::ExtractSymEntries(const Elf64_Ehdr *ehdr,
    const Elf64_Shdr *shdr, const char *base, const std::unordered_set<std::string> &targetSet)
{
    std::unordered_map<std::string, ElfScanner::ElfSymEntry> foundSymbols;
    for (int i = 0; i < ehdr->e_shnum; ++i) {
        if (shdr[i].sh_type == SHT_SYMTAB || shdr[i].sh_type == SHT_DYNSYM) {
            const Elf64_Shdr &symsh = shdr[i];
            const Elf64_Shdr &strsh = shdr[symsh.sh_link];
            const Elf64_Sym *symTable = reinterpret_cast<const Elf64_Sym *>(base + symsh.sh_offset);
            const char *strTable = base + strsh.sh_offset;
            size_t symCount = symsh.sh_size / symsh.sh_entsize;
            for (size_t j = 0; j < symCount; ++j) {
                const Elf64_Sym &s = symTable[j];
                const char *symName = strTable + s.st_name;
                if (targetSet.count(symName) && ELF64_ST_TYPE(s.st_info) == STT_FUNC) {
                    foundSymbols[symName] = {s.st_value, s.st_size, s.st_shndx};
                }
            }
        }
    }
    return foundSymbols;
}

uint64_t ElfScanner::GetSymFileOffset(const ElfScanner::ElfSymEntry &symEntry, const Elf64_Shdr *shdr)
{
    const Elf64_Shdr &sec = shdr[symEntry.shndx];
    return (symEntry.value - sec.sh_addr) + sec.sh_offset;
}

std::vector<uint64_t> ElfScanner::GetRetInstOffsets(const ElfScanner::ElfSymEntry &symEntry, const char *base,
    size_t fileSize, uint64_t baseVirtualAddr, uint64_t symbolFileOffset, uint16_t machineType)
{
    std::vector<uint64_t> retOffsets;

    if (symbolFileOffset >= fileSize) {
        return retOffsets;
    }
    size_t codeSize = std::min(symEntry.size, fileSize - symbolFileOffset);
    if (codeSize == 0) {
        return retOffsets;
    }

    csh handle;
    cs_err err;

    if (machineType == EM_AARCH64) {
        err = cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &handle);
    } else if (machineType == EM_X86_64) {
        err = cs_open(CS_ARCH_X86, CS_MODE_64, &handle);
    } else {
        return retOffsets;
    }

    if (err != CS_ERR_OK) {
        return retOffsets;
    }

    cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

    cs_insn *insn = nullptr;
    const uint8_t *codePtr = reinterpret_cast<const uint8_t *>(base + symbolFileOffset);

    // 执行反汇编（代码指针，长度，起始虚拟地址，解析指令数(0代表全部)，返回的指令数组）
    size_t count = cs_disasm(handle, codePtr, codeSize, symEntry.value, 0, &insn);
    if (count > 0) {
        for (size_t i = 0; i < count; i++) {
            bool isRet = false;

            if (machineType == EM_AARCH64) {
                if (insn[i].id == ARM64_INS_RET) {
                    isRet = true;
                }
            } else if (machineType == EM_X86_64) {
                // x86_64 包括近返回和远返回
                if (insn[i].id == X86_INS_RET || insn[i].id == X86_INS_RETF || insn[i].id == X86_INS_RETFQ) {
                    isRet = true;
                }
            }

            if (isRet) {
                retOffsets.push_back(insn[i].address - baseVirtualAddr);
            }
        }
        cs_free(insn,count);
    }

    cs_close(&handle);

    return retOffsets;
}

ProbePoints ElfScanner::ConstructProbePoints(const std::string &symbolName, const ElfScanner::ElfSymEntry &symEntry,
    const Elf64_Shdr *shdr, const char *base, size_t fileSize, uint64_t baseVirtualAddr, uint16_t machineType)
{
    ProbePoints info;
    info.symbolName = symbolName;
    info.entryOffset = symEntry.value - baseVirtualAddr;
    uint64_t fileOffset = GetSymFileOffset(symEntry, shdr);
    info.retOffsets = GetRetInstOffsets(symEntry, base, fileSize, baseVirtualAddr, fileOffset, machineType);
    return info;
}

std::vector<ProbePoints> ElfScanner::ResolveElf(
    const std::string &filePath, const std::vector<std::string> &symbolsToFind)
{
    pcerr::New(SUCCESS);
    std::vector<ProbePoints> probePoints;

    MappedFile mappedFile(filePath);
    if (Perrorno() != SUCCESS) {
        failedElf2Reason_[filePath] = Perror();
        return probePoints;
    }
    const char *base = static_cast<const char *>(mappedFile.GetBase());
    size_t fileSize = mappedFile.GetSize();

    const Elf64_Ehdr *ehdr = VerifyElfHeader(base, fileSize);
    if (ehdr == nullptr) {
        failedElf2Reason_[filePath] = Perror();
        return probePoints;
    }

    uint16_t machineType = ehdr->e_machine;

    const Elf64_Phdr *phdr = reinterpret_cast<const Elf64_Phdr *>(base + ehdr->e_phoff);
    const Elf64_Shdr *shdr = reinterpret_cast<const Elf64_Shdr *>(base + ehdr->e_shoff);

    uint64_t baseVirtualAddr = CalLoadBaseAddr(ehdr, phdr);

    std::unordered_set<std::string> targetSet(symbolsToFind.begin(), symbolsToFind.end());
    auto foundSymbols = ExtractSymEntries(ehdr, shdr, base, targetSet);

    for (const auto &symbolName : symbolsToFind) {
        auto it = foundSymbols.find(symbolName);
        if (it != foundSymbols.end()) {
            probePoints.push_back(ConstructProbePoints(symbolName, it->second, shdr, base, fileSize, baseVirtualAddr, machineType));
        } else {
            elf2FailedSymbols_[filePath].push_back(symbolName);
        }
    }
    return probePoints;
}

std::string ElfScanner::FormatFailures()
{
    if (failedElf2Reason_.empty() && elf2FailedSymbols_.empty()) {
        return "";
    }

    std::stringstream ss;

    if (!failedElf2Reason_.empty()) {
        ss << "ELF parsing failed for:";
        for (const auto &kv : failedElf2Reason_) {
            ss << " " << kv.first << " (" << kv.second << ");";
        }
    }

    if (!elf2FailedSymbols_.empty()) {
        if (ss.tellp() > 0) {
            ss << " ";
        }
        ss << "Symbols not found:";
        for (const auto &kv : elf2FailedSymbols_) {
            ss << " in" << kv.first << " [";
            for (size_t i = 0; i < kv.second.size(); ++i) {
                ss << kv.second[i] << (i == kv.second.size() - 1 ? "" : ", ");
            }
            ss << "];";
        }
    }

    failedElf2Reason_.clear();
    elf2FailedSymbols_.clear();

    std::string result = ss.str();
    if (!result.empty() && result.back() == ';') {
        result.pop_back();
    }
    return result;
}

#else

std::unordered_map<std::string, std::string> ElfScanner::failedElf2Reason_;
std::unordered_map<std::string, std::vector<std::string>> ElfScanner::elf2FailedSymbols_;

std::unordered_map<std::string, std::vector<ProbePoints>> ElfScanner::ResolveElfs(
    const std::unordered_map<std::string, std::vector<std::string>> &module2Symbols) 
{
    return std::unordered_map<std::string, std::vector<ProbePoints>>();
}

std::string ElfScanner::FormatFailures()
{
    return "";
}

#endif