/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Li
 * Create: 2024-04-03
 * Description: Provide a complete set of symbolic analysis tools, perform operations such as
 * module records, address analysis and stack conversion.
 ******************************************************************************/
#ifndef USER_SYMBOL_H
#define USER_SYMBOL_H
#include <sys/stat.h>
#include <sys/mman.h>
#include <mutex>
#include <unordered_map>
#include <map>
#include <memory>
#include <vector>
#include <iostream>
#include <string>
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include <linux/types.h>
#include "safe_handler.h"
#include "linked_list.h"
#ifndef ELF_LLVM
#include <elf++.hh>
#endif
#include "symbol.h"

using namespace llvm;
using namespace symbolize;

namespace KUNPENG_SYM {

    enum class RecordModuleType { RECORD_ALL = 0, RECORD_NO_DWARF = 1 };

    struct ModuleMap {
        unsigned long start;
        unsigned long end;
        std::string moduleName;
        std::string mntPoint;
        bool isExecFile = false;
        RecordModuleType moduleType;
        bool isFile = true;
    };

#ifndef ELF_LLVM
    struct ElfMap {
        unsigned long start;
        unsigned long end;
        std::string symbolName;
    } __attribute__((aligned(8)));

    using ELF_SYM = elf::sym;
    using ELF = elf::elf;

    class ParserElf
    {
    public:
        explicit ParserElf(const ELF &elf) : elf(elf){};
        ELF_SYM *FindSymbol(unsigned long addr);
        void Emplace(unsigned long addr, const ELF_SYM &elfSym);
    private:
        ELF elf;
        std::map<unsigned long, ELF_SYM> symTab;
    };
#endif

    struct ElfHdr {
        char elfFormat[4];
        char elfClass;
        char elfData;
        unsigned char elfVersion;
    };

    struct ElfNoteHeader {
        int nameSize;
        int descSize;
        int type;
    };

    class MyElf {
    public:
        MyElf(const std::string& filePath) : filePath(filePath){};
        ~MyElf() {
            if (base) {
                munmap(base, lim);
            }
        };
        int LoadMmap();
        const void* Load(off_t offset, size_t size);
        int ElfGetBuildId(char** buildId);
        bool IsExecFile();
    private:
        template<typename Ehdr, typename Phdr, typename Shdr>
        int ElfParser(char** buildId);
        template<typename Ehdr>
        bool CheckIsExecFile();
        int CheckElfHeader();
        ElfHdr* elfHdr = nullptr;
        void* base;
        size_t lim;
        std::string filePath;
    };

    static LLVMSymbolizer Symbolizer;

    using SYMBOL_MAP = std::unordered_map<pid_t, std::unordered_map<__u64, struct Symbol *>>;
    using SYMBOL_UNMAP = std::vector<Symbol*>;
    using STACK_MAP = std::unordered_map<pid_t, std::unordered_map<std::string, struct Stack*>>;
    using MODULE_MAP = std::unordered_map<pid_t, std::vector<std::shared_ptr<ModuleMap>>>;
#ifndef ELF_LLVM
    using ELF_MAP = std::unordered_map<std::string, ParserElf>;
#endif

    class SymbolUtils final {
    public:
        SymbolUtils() = default;
        ~SymbolUtils() = default;
        static void FreeSymbol(struct Symbol* symbol);
        static bool IsFile(const char* fileName);
        static unsigned long SymStoul(const std::string& addrStr);
        static bool IsNumber(const std::string& str);
        static void FreeStackAsm(struct StackAsm** stackAsm);
        static void StrCpy(char* dst, int dstLen, const char* src);
    };
    class SymbolResolve {
    public:
        static SymbolResolve* GetInstance()
        {
            // Double-checked locking for thread safety
            if (instance == nullptr) {
                std::lock_guard<std::mutex> lock(mutex);
                if (instance == nullptr) {
                    instance = new SymbolResolve();
                }
            }
            return instance;
        }

        int RecordModule(int pid, RecordModuleType recordModuleType);
        void FreeModule(int pid);
        int RecordKernel();
        int UpdateModule(int pid, RecordModuleType recordModuleType);
        int UpdateModule(int pid, const char* moduleName, unsigned long startAddr, RecordModuleType recordModuleType);
        void Clear();
        std::shared_ptr<ModuleMap> AddrToModule(std::vector<std::shared_ptr<ModuleMap>>& processModule, unsigned long addr);
        struct Stack* StackToHash(int pid, unsigned long* stack, int nr);
        struct Symbol* MapAddr(int pid, unsigned long addr);
        struct StackAsm* MapAsmCode(const char* moduleName, unsigned long startAddr, unsigned long endAddr);
        struct Symbol* MapCodeAddr(const char* moduleName, unsigned long startAddr);
        int GetBuildId(const char *moduleName, char **buildId);
        int GetAsmCodeByAddr(const char* moduleName, unsigned long startAddr, unsigned long endAddr, char** asmCode);
#ifndef ELF_LLVM
        int RecordElf(const char* fileName);
#endif
    private:
#ifndef ELF_LLVM
        void SearchElfInfo(ParserElf &myElf, unsigned long addr, struct Symbol* symbol, unsigned long *offset);
#endif
        char* GetCharFromStr(const std::string& str);
        struct Symbol* MapKernelAddr(unsigned long addr);
        struct Symbol* MapUserAddr(int pid, unsigned long addr);
        struct StackAsm* MapAsmCodeStack(const std::string& moduleName, unsigned long startAddr, unsigned long endAddr);
        std::vector<std::shared_ptr<ModuleMap>> FindDiffMaps(const std::vector<std::shared_ptr<ModuleMap>>& oldMaps,
                                                             const std::vector<std::shared_ptr<ModuleMap>>& newMaps) const;

        std::map<std::string, char*> strToCharMap;
        SYMBOL_MAP symbolMap{};
        SYMBOL_UNMAP symbolUnmap{};
        STACK_MAP stackMap{};
        MODULE_MAP moduleMap{};
        std::vector<std::shared_ptr<Symbol>> ksymArray;
        SymbolResolve()
        {}

        SymbolResolve(const SymbolResolve&) = delete;
        SymbolResolve& operator=(const SymbolResolve&) = delete;

        ~SymbolResolve()
        {}
#ifndef ELF_LLVM
        ELF_MAP elfMap{};
#endif
        SafeHandler<int> moduleSafeHandler;
        SafeHandler<std::string> dwarfSafeHandler;
        SafeHandler<std::string> elfSafeHandler;
        SafeHandler<int> symSafeHandler;
        SafeHandler<std::string> dwarfLoadHandler;
        static std::mutex kernelMutex;
        static SymbolResolve* instance;
        static std::mutex mutex;
    };
}  // namespace KUNPENG_SYM
#endif