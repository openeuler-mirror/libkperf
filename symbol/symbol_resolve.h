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
#include <mutex>
#include <unordered_map>
#include <map>
#include <memory>
#include <vector>
#include <iostream>
#include <string>
#include <dwarf++.hh>
#include <elf++.hh>
#include <linux/types.h>
#include "safe_handler.h"
#include "linked_list.h"
#include "symbol.h"

namespace KUNPENG_SYM {
    struct ModuleMap {
        unsigned long start;
        unsigned long end;
        std::string moduleName;
    } __attribute__((aligned(8)));

    struct ElfMap {
        unsigned long start;
        unsigned long end;
        std::string symbolName;
    } __attribute__((aligned(8)));

    struct DwarfEntry {
        unsigned int lineNum = 0;
        std::string fileName = {};
        bool find = false;
    };

    enum class RecordModuleType { RECORD_ALL = 0, RECORD_NO_DWARF = 1 };

    using ELF_SYM = elf::sym;
    using ELF = elf::elf;
    using DWARF = dwarf::dwarf;
    using DWARF_TABEL = dwarf::line_table;
    using DWARF_ENTRY = dwarf::line_table::entry;
    using DWARF_CU = dwarf::compilation_unit;

    class RangeL {
    public:
        RangeL() = default;

        void FindLine(unsigned long addr, struct DwarfEntry &entry);

        void ReadRange(const DWARF_CU &cu);

        dwarf::line_table::iterator FindAddress(unsigned long addr);

        bool IsInLineTable(unsigned long addr) const
        {
            auto it = rangeMap.upper_bound(addr);
            if (it == rangeMap.cbegin()) {
                return false;
            }
            it--;
            const unsigned long *highAddr = &it->second;
            return addr <= *highAddr;
        }

    private:
        std::map<unsigned long, unsigned long> rangeMap;
        bool loadLineTable = false;
        DWARF_TABEL lineTab;
        DWARF_CU cu;
    };

    class MyElf
    {
    public:
        explicit MyElf(const ELF &elf) : elf(elf){};
        ELF_SYM *FindSymbol(unsigned long addr);
        void Emplace(unsigned long addr, const ELF_SYM &elfSym);
        bool IsExecFile();

    private:
        ELF elf;
        std::map<unsigned long, ELF_SYM> symTab;
    };

    class MyDwarf
    {
    public:
        MyDwarf(const DWARF &dw, const std::string &moduleName) : dw(dw), moduleName(moduleName){};
        void FindLine(unsigned long addr, struct DwarfEntry &dwarfEntry);
        void LoadDwarf(unsigned long addr, struct DwarfEntry &dwarfEntry);

        bool IsLoad() const 
        {
            return hasLoad;
        }

        std::string GetModule() const
        {
            return this->moduleName;
        }

    private:
        DWARF dw;
        std::string moduleName;
        volatile bool hasLoad = false;
        volatile int loadNum = 0;
        std::vector<RangeL> rangeList;
        std::vector<dwarf::compilation_unit> cuList;
    };

    using SYMBOL_MAP = std::unordered_map<pid_t, std::unordered_map<__u64, struct Symbol *>>;
    using SYMBOL_UNMAP = std::vector<Symbol*>;
    using STACK_MAP = std::unordered_map<pid_t, std::unordered_map<std::string, struct Stack*>>;
    using MODULE_MAP = std::unordered_map<pid_t, std::vector<std::shared_ptr<ModuleMap>>>;
    using DWARF_MAP = std::unordered_map<std::string, MyDwarf>;
    using ELF_MAP = std::unordered_map<std::string, MyElf>;

    class SymbolUtils final {
    public:
        SymbolUtils() = default;
        ~SymbolUtils() = default;
        static void FreeSymbol(struct Symbol* symbol);
        static bool IsFile(const char* fileName);
        static unsigned long SymStoul(const std::string& addrStr);
        static std::string RealPath(const std::string& filePath);
        static bool IsValidPath(const std::string& filePath);
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
        int RecordElf(const char* fileName);
        int RecordDwarf(const char* fileName);
        int UpdateModule(int pid, RecordModuleType recordModuleType);
        int UpdateModule(int pid, const char* moduleName, unsigned long startAddr, RecordModuleType recordModuleType);
        void Clear();
        std::shared_ptr<ModuleMap> AddrToModule(std::vector<std::shared_ptr<ModuleMap>>& processModule, unsigned long addr);
        struct Stack* StackToHash(int pid, unsigned long* stack, int nr);
        struct Symbol* MapAddr(int pid, unsigned long addr);
        struct StackAsm* MapAsmCode(const char* moduleName, unsigned long startAddr, unsigned long endAddr);
        struct Symbol* MapCodeAddr(const char* moduleName, unsigned long startAddr);
        int GetBuildId(const char *moduleName, char **buildId);

    private:
        void SearchElfInfo(MyElf &myElf, unsigned long addr, struct Symbol *symbol, unsigned long *offset);
        void SearchDwarfInfo(MyDwarf &myDwarf, unsigned long addr, struct Symbol *symbol);
        struct Symbol* MapKernelAddr(unsigned long addr);
        struct Symbol* MapUserAddr(int pid, unsigned long addr);
        struct Symbol* MapUserCodeAddr(const std::string& moduleName, unsigned long addr);
        struct Symbol* MapCodeElfAddr(const std::string& moduleName, unsigned long addr);
        struct StackAsm* MapAsmCodeStack(const std::string& moduleName, unsigned long startAddr, unsigned long endAddr);
        std::vector<std::shared_ptr<ModuleMap>> FindDiffMaps(const std::vector<std::shared_ptr<ModuleMap>>& oldMaps,
                                                             const std::vector<std::shared_ptr<ModuleMap>>& newMaps) const;

        SYMBOL_MAP symbolMap{};
        SYMBOL_UNMAP symbolUnmap{};
        STACK_MAP stackMap{};
        MODULE_MAP moduleMap{};
        DWARF_MAP dwarfMap{};
        ELF_MAP elfMap{};
        bool isCleared = false;
        std::vector<std::shared_ptr<Symbol>> ksymArray;
        SymbolResolve()
        {}

        SymbolResolve(const SymbolResolve&) = delete;
        SymbolResolve& operator=(const SymbolResolve&) = delete;

        ~SymbolResolve()
        {}
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