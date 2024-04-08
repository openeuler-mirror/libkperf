/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * gala-gopher licensed under the Mulan PSL v2.
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

    struct DwarfMap {
        unsigned int lineNum;
        unsigned int fileIndex;
    } __attribute__((aligned(8)));

    enum class RecordModuleType { RECORD_ALL = 0, RECORD_NO_DWARF = 1 };

    using SYMBOL_MAP = std::unordered_map<pid_t, std::unordered_map<__u64, struct Symbol*>>;
    using STACK_MAP = std::unordered_map<pid_t, std::unordered_map<size_t, struct Stack*>>;
    using MODULE_MAP = std::unordered_map<pid_t, std::vector<std::shared_ptr<ModuleMap>>>;
    using DWARF_DATA_MAP = std::map<unsigned long, DwarfMap>;
    using DWARF_MAP = std::unordered_map<std::string, DWARF_DATA_MAP>;
    using DWARF_VET_MAP = std::unordered_map<std::string, std::vector<unsigned long>>;
    using ELF_MAP = std::unordered_map<std::string, std::vector<ElfMap>>;

    template <typename Key>
    class SymbolVet : public std::vector<Key> {
    public:
        unsigned int InsertKeyForIndex(const Key& key)
        {
            std::lock_guard<std::mutex> guard(keyMutex);
            if (keyMap.find(key) != keyMap.end()) {
                return keyMap.at(key);
            }
            this->push_back(key);
            keyMap[key] = this->size() - 1;
            return this->size() - 1;
        }

        Key& GetKeyByIndex(const unsigned int index)
        {
            std::lock_guard<std::mutex> guard(keyMutex);
            if (index < this->size()) {
                return this->at(index);
            }
            Key key = {};
            return key;
        }

    private:
        std::unordered_map<Key, unsigned int> keyMap;
        std::mutex keyMutex;
    };

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
        int UpdateModule(int pid);
        int UpdateModule(int pid, const char* moduleName, unsigned long startAddr);
        void Clear();
        std::shared_ptr<ModuleMap> AddrToModule(std::vector<std::shared_ptr<ModuleMap>>& processModule, unsigned long addr);
        struct Stack* StackToHash(int pid, unsigned long* stack, int nr);
        struct Symbol* MapAddr(int pid, unsigned long addr);
        struct StackAsm* MapAsmCode(const char* moduleName, unsigned long startAddr, unsigned long endAddr);
        struct Symbol* MapCodeAddr(const char* moduleName, unsigned long startAddr);

    private:
        void SearchElfInfo(
                std::vector<ElfMap>& elfVec, unsigned long addr, struct Symbol* symbol, unsigned long* offset);
        void SearchDwarfInfo(
                std::vector<unsigned long>& addrVet, DWARF_DATA_MAP& dwalfVec, unsigned long addr, struct Symbol* symbol);
        struct Symbol* MapKernelAddr(unsigned long addr);
        struct Symbol* MapUserAddr(int pid, unsigned long addr);
        struct Symbol* MapUserCodeAddr(const std::string& moduleName, unsigned long addr);
        struct Symbol* MapCodeElfAddr(const std::string& moduleName, unsigned long addr);
        struct StackAsm* MapAsmCodeStack(const std::string& moduleName, unsigned long startAddr, unsigned long endAddr);
        std::vector<std::shared_ptr<ModuleMap>> FindDiffMaps(const std::vector<std::shared_ptr<ModuleMap>>& oldMaps,
                                                             const std::vector<std::shared_ptr<ModuleMap>>& newMaps) const;

        SYMBOL_MAP symbolMap{};
        STACK_MAP stackMap{};
        MODULE_MAP moduleMap{};
        DWARF_MAP dwarfMap{};
        DWARF_VET_MAP dwarfVetMap{};
        ELF_MAP elfMap{};
        bool isCleared = false;
        std::vector<std::shared_ptr<Symbol>> ksymArray;
        SymbolVet<std::string> dwarfFileArray;
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
        static std::mutex kernelMutex;
        static SymbolResolve* instance;
        static std::mutex mutex;
    };
}  // namespace KUNPENG_SYM
#endif