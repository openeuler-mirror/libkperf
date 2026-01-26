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
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdbool>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <climits>
#include <set>
#include <elf.h>
#include "pcerr.h"
#include "symbol_resolve.h"
#include "common.h"

using namespace KUNPENG_SYM;
constexpr __u64 MAX_LINE_LENGTH = 1024;
constexpr __u64 MODULE_NAME_LEN = 1024;
constexpr __u64 MAX_LINUX_FILE_NAME = 1024;
constexpr __u64 MAX_LINUX_SYMBOL_LEN = 8192;
constexpr __u64 MAX_LINUX_MODULE_LEN = 1024;
constexpr int ADDR_LEN = 16;
constexpr int MODE_LEN = 5;
constexpr int MAP_LEN = 128;
constexpr int OFFSET_LEN = 16;
constexpr int DEV_LEN = 16;
constexpr int INODE_LEN = 16;
constexpr __u64 KERNEL_START_ADDR = 0xffff000000000000;
constexpr int BINARY_HALF = 2;
constexpr int KERNEL_NAME_LEN = 8;
constexpr int SHA_BIT_SHIFT_LEN = 8;
constexpr int KERNEL_MODULE_LNE = 128;
constexpr int CODE_LINE_RANGE_LEN = 10;
constexpr int HEX_LEN = 16;
constexpr int TO_TAIL_LEN = 2;
constexpr unsigned long USER_MAX_ADDR = 0xffffffff;

const std::string HUGEPAGE = "/anon_hugepage";
const std::string DEV_ZERO = "/dev/zero";
const std::string ANON = "//anon";
const std::string STACK = "[stack";
const std::string SOCKET = "socket:";
const std::string VSYSCALL = "[vsyscall]";
const std::string HEAP = "[heap]";
const std::string VDSO = "[vdso]";
const std::string SYSV = "/sysv";
const std::string VVAR = "[vvar]";
const std::string R_XP = "r-xp";
const std::string SLASH = "/";
const char DASH = '-';
const char EXE_TYPE = 'x';
char* UNKNOWN = "UNKNOWN";
char* KERNEL = "[kernel]";

namespace {
    static inline bool CheckIfFile(const std::string& mapline)
    {
        const std::vector<std::string> patterns = {HUGEPAGE, DEV_ZERO, ANON, STACK, SOCKET, VSYSCALL, HEAP ,VDSO, SYSV, VVAR};
        for (const auto& pattern :patterns) {
            if (mapline.find(pattern) != std::string::npos) {
                return false;
            }
        }
        return  mapline.find(R_XP) != std::string::npos;
    }

    static inline char* InitChar(int len)
    {
        char* str = new char[len + 1];

        if (str == nullptr) {
            return nullptr;
        }
        memset(str, 0, len + 1);
        return str;
    }

    static inline Symbol* InitializeSymbol(unsigned long addr) {
        struct Symbol* symbol = new struct Symbol();
        symbol->module = UNKNOWN;
        symbol->symbolName = UNKNOWN;
        symbol->mangleName = UNKNOWN;
        symbol->fileName = UNKNOWN;
        symbol->mntPoint = nullptr;
        symbol->addr = addr;
        symbol->offset = 0;
        symbol->lineNum = 0;
        symbol->firstLine = 0;
        return symbol;
    }

    static void ReadProcPidMap(std::ifstream& file, std::vector<std::shared_ptr<ModuleMap>>& modVec)
    {
        char line[MAX_LINUX_SYMBOL_LEN];
        char mode[MODE_LEN];
        char offset[OFFSET_LEN];
        char dev[DEV_LEN];
        char inode[INODE_LEN];
        while (file.getline(line, MAX_LINUX_SYMBOL_LEN)) {
            if (!CheckIfFile(line)) {
                continue;
            }
            std::shared_ptr<ModuleMap> data = std::make_shared<ModuleMap>();
            char modNameChar[MAX_LINUX_MODULE_LEN];
            if (sscanf(line, "%lx-%lx %s %s %s %s %s",
                          &data->start, &data->end, mode, offset,dev,
                          inode, modNameChar) == EOF) {
                continue;
            }
            data->moduleName = modNameChar;
            modVec.emplace_back(data);
        }
    }

    static StackAsm* FirstLineMatch(const std::string& line)
    {
        if (line[line.size() - TO_TAIL_LEN] == ':') {
            struct StackAsm* stackAsm = CreateNode<struct StackAsm>();
            stackAsm->funcName = InitChar(MAX_LINE_LENGTH);
            stackAsm->asmCode = nullptr;
            stackAsm->next = nullptr;
            int ret = sscanf(line.c_str(), "%llx %s %*s %*s %llx", &stackAsm->funcStartAddr, stackAsm->funcName,
                               &stackAsm->functFileOffset);
            if (ret == EOF) {
                delete[] stackAsm->funcName;
                stackAsm->funcName = nullptr;
                delete stackAsm;
                stackAsm = nullptr;
                return nullptr;
            }
            return stackAsm;
        }
        return nullptr;
    }

    static bool MatchFileName(const std::string& line, std::string& fileName, unsigned int& lineNum)
    {
        char startStr[MAX_LINE_LENGTH + 1];
        int ret = sscanf(line.c_str(), "%s", startStr);
        if (ret < 0) {
            return false;
        }
        std::string preLine = startStr;
        if (preLine.find(":") + 1 >= preLine.size()) {
            return false;
        }
        std::string lineStr = preLine.substr(preLine.find(":") + 1, preLine.size());
        if (lineStr.empty()) {
            return false;
        }
        if (!SymbolUtils::IsNumber(lineStr)) {
            return false;
        }
        try {
            lineNum = std::atoi(lineStr.c_str());
        } catch (std::exception& err) {
            return false;
        }
        fileName = preLine.substr(0, preLine.find(":"));
        return true;
    }

    static AsmCode* MatchAsmCode(const std::string& line, const std::string& srcFileName, unsigned int lineNum)
    {
        std::string addrStr = line.substr(0, line.find(":"));
        struct AsmCode* asmCode = new AsmCode();
        asmCode->addr = SymbolUtils::SymStoul(addrStr);
        if (asmCode->addr == 0) {
            delete asmCode;
            return nullptr;
        }
        size_t tailLen = line.find("\n") != std::string::npos ? line.find("\n") : strlen(line.c_str());
        char startStr[MAX_LINE_LENGTH + 1];
        int ret = sscanf(line.c_str(), "%*s %s", startStr);
        if (ret == EOF) {
            delete asmCode;
            return nullptr;
        }
        std::string codeStr = line.substr(line.find(startStr) + strlen(startStr), tailLen);
        if (!codeStr.empty()) {
            codeStr.erase(0, codeStr.find_first_not_of(" "));
            codeStr.erase(0, codeStr.find_first_not_of("\t"));
        }
        asmCode->code = InitChar(MAX_LINE_LENGTH);
        strcpy(asmCode->code, codeStr.c_str());
        asmCode->fileName = InitChar(MAX_LINUX_FILE_NAME);
        strcpy(asmCode->fileName, srcFileName.c_str());
        asmCode->lineNum = lineNum;
        return asmCode;
    }

    static inline void FreeStackMap(STACK_MAP& stackMap)
    {
        for (auto& item : stackMap) {
            for (auto& data : item.second) {
                struct Stack* head = data.second;
                FreeList(&head);
            }
        }
    }

    static inline std::string HashStr(unsigned long* stack, int nr)
    {
        std::string str = {};
        for (int k = 0; k < nr; k++) {
            str += std::to_string(stack[k]);
        }
        return str;
    }

    static inline bool CheckColonSuffix(const std::string& str)
    {
        if (!strstr(str.c_str(), ":")) {
            return false;
        }
        size_t colonIndex = str.find(":");
        if (colonIndex + 1 >= str.size()) {
            return false;
        }
        std::string suffix = str.substr(colonIndex + 1, str.size());
        if (suffix.empty() || suffix.size() == 1) {
            return false;
        }
        return true;
    }

}  // namespace

void SymbolUtils::FreeStackAsm(struct StackAsm** stackAsm)
{
    struct StackAsm* current = *stackAsm;
    struct StackAsm* next;
    while (current != nullptr) {
        next = current->next;
        if (current->asmCode) {
            delete[] current->asmCode->code;
            delete[] current->asmCode->fileName;
            delete current->asmCode;
        }
        delete[] current->funcName;
        delete current;
        current = next;
    }
    *stackAsm = nullptr;
}

bool SymbolUtils::IsFile(const char* fileName)
{
    struct stat st {};
    // 获取文件属性
    if (stat(fileName, &st) != 0) {
        return false;
    }
    return (S_ISREG(st.st_mode)) ? true : false;
}

unsigned long SymbolUtils::SymStoul(const std::string& addrStr)
{
    try {
        return std::stoul(addrStr, nullptr, HEX_LEN);
    } catch (std::invalid_argument& invalidErr) {
        return 0;
    }
}

bool SymbolUtils::IsNumber(const std::string& str)
{
    for (int i = 0; i < str.length(); i++) {
        if (!isdigit(str[i])) {
            return false;
        }
    }
    return true;
}

void SymbolUtils::FreeSymbol(struct Symbol* symbol)
{
    if (symbol) {
        delete symbol;
        symbol = nullptr;
    }
}

void SymbolUtils::StrCpy(char* dst, int dstLen, const char* src)
{
    int size = strlen(src) > dstLen ? dstLen + 1 : strlen(src) + 1;
    memcpy(dst, src, size);
    dst[dstLen] = '\0';
}

int SymbolResolve::RecordModule(int pid, RecordModuleType recordModuleType)
{
    if (pid < 0) {
        pcerr::New(LIBSYM_ERR_PARAM_PID_INVALID, "libsym param process ID must be greater than 0");
        return LIBSYM_ERR_PARAM_PID_INVALID;
    }
    if (this->moduleMap.find(pid) != this->moduleMap.end()) {
        pcerr::New(0, "success");
        return 0;
    }
    std::string mapFile = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream file(mapFile);
    if (!file.is_open()) {
        pcerr::New(LIBSYM_ERR_OPEN_FILE_FAILED,
                   "libsym can't open file named " + mapFile + " because of " + std::string{strerror(errno)});
        return LIBSYM_ERR_OPEN_FILE_FAILED;
    }
    std::vector<std::shared_ptr<ModuleMap>> modVec;
    ReadProcPidMap(file, modVec);
    std::string mntPoint = GetMntPoint(pid);
    for (auto& item : modVec) {
        if (!mntPoint.empty()) {
            item->mntPoint = mntPoint;
        }
        item->moduleType = recordModuleType;
    }
    this->moduleMap.insert({pid, modVec});
    pcerr::New(0, "success");
    return 0;
}

int SymbolResolve::UpdateModule(int pid, RecordModuleType recordModuleType)
{
    if (pid < 0) {
        pcerr::New(LIBSYM_ERR_PARAM_PID_INVALID, "libsym param process ID must be greater than 0");
        return LIBSYM_ERR_PARAM_PID_INVALID;
    }
    moduleSafeHandler.tryLock(pid);
    if (this->moduleMap.find(pid) == this->moduleMap.end()) {
        // need to use RecordModule first!
        pcerr::New(SUCCESS);
        moduleSafeHandler.releaseLock(pid);
        return SUCCESS;
    }
    // Get memory maps of pid.
    std::string mapFile = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream file(mapFile);
    if (!file.is_open()) {
        pcerr::New(LIBSYM_ERR_OPEN_FILE_FAILED,
                   "libsym can't open file named " + mapFile + " because of " + std::string{strerror(errno)});
        moduleSafeHandler.releaseLock(pid);
        return LIBSYM_ERR_OPEN_FILE_FAILED;
    }
    std::vector<std::shared_ptr<ModuleMap>> newModVec;
    ReadProcPidMap(file, newModVec);
    std::string mntPoint = GetMntPoint(pid);
    // Find new dynamic modules.
    auto &oldModVec = moduleMap[pid];
    if (newModVec.size() <= oldModVec.size()) {
        pcerr::New(SUCCESS);
        moduleSafeHandler.releaseLock(pid);
        return SUCCESS;
    }
    auto diffModVec = FindDiffMaps(oldModVec, newModVec);
    // Load modules.
    for (auto& item : diffModVec) {
        if (!mntPoint.empty()) {
            item->mntPoint = mntPoint;
        }
    }
    for (auto& mod : diffModVec) {
        oldModVec.emplace_back(mod);
    }
    pcerr::New(SUCCESS);
    moduleSafeHandler.releaseLock(pid);
    return SUCCESS;
}

void SymbolResolve::FreeModule(int pid)
{
    if (pid < 0) {
        return;
    }
    moduleSafeHandler.tryLock(pid);
    auto it = this->moduleMap.find(pid);
    if (it != this->moduleMap.end()) {
        this->moduleMap.erase(it);
    }
    moduleSafeHandler.releaseLock(pid);
    return;
}

void SymbolResolve::Clear()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!this->instance) {
        return;
    }
    for (auto& item : this->symbolMap) {
        for(auto& data : item.second) {
            SymbolUtils::FreeSymbol(data.second);
        }
    }

    for (auto& item: symbolUnmap) {
        SymbolUtils::FreeSymbol(item);
    }
    /**
     * free the memory allocated for stack table
     */
    FreeStackMap(this->stackMap);
    /**
     * free the strdup data
     */
    for (auto& item : strToCharMap) {
        free(item.second);
    }
    delete this->instance;
    this->instance = nullptr;
}

std::shared_ptr<ModuleMap> SymbolResolve::AddrToModule(
        std::vector<std::shared_ptr<ModuleMap>>& processModule, unsigned long addr)
{
    ssize_t start = 0;
    ssize_t end = processModule.size() - 1;
    ssize_t mid;
    unsigned long symAddr;

    while (start < end) {
        mid = start + (end - start + 1) / BINARY_HALF;
        symAddr = processModule[mid]->start;
        if (symAddr <= addr) {
            start = mid;
        } else {
            end = mid - 1;
        }
    }

    if (start == end && processModule[start]->start <= addr) {
        return processModule[start];
    }

    pcerr::New(LIBSYM_ERR_MAP_ADDR_MODULE_FAILED, "libsym addr can't find module");
    return nullptr;
}

struct Stack* SymbolResolve::StackToHash(int pid, unsigned long* stack, int nr)
{
    if (this->stackMap.find(pid) == this->stackMap.end()) {
        this->stackMap[pid] = {};
    }
    std::string stackId = HashStr(stack, nr);
    if (this->stackMap.at(pid).find(stackId) != this->stackMap.at(pid).end()) {
        return this->stackMap.at(pid).at(stackId);
    }

    struct Stack* head = nullptr;
    for (int i = nr - 1; i >= 0; i--) {
        struct Stack* current = CreateNode<struct Stack>();
        auto symbol = this->MapAddr(pid, stack[i]);
        if (symbol != nullptr) {
            current->symbol = symbol;
        } else {
            current->symbol = InitializeSymbol(stack[i]);
            symbolUnmap.emplace_back(current->symbol);
        }
        AddDoubleLinkedTail<struct Stack>(&head, &current);
    }

    if (this->stackMap.at(pid).find(stackId) == this->stackMap.at(pid).end()) {
        this->stackMap.at(pid)[stackId] = head;
    }
    pcerr::New(0, "success");
    return head;
}

struct Symbol* SymbolResolve::MapKernelAddr(unsigned long addr)
{
    ssize_t start = 0;
    ssize_t end = this->ksymArray.size() - 1;
    ssize_t mid;
    __u64 symAddr;

    while (start < end) {
        mid = start + (end - start + 1) / BINARY_HALF;
        symAddr = this->ksymArray[mid]->addr;
        if (symAddr <= addr) {
            start = mid;
        } else {
            end = mid - 1;
        }
    }

    if (start == end && this->ksymArray[start]->addr <= addr) {
        return this->ksymArray[start].get();
    }
    pcerr::New(LIBSYM_ERR_MAP_KERNAL_ADDR_FAILED, "libsym cannot find the corresponding kernel address");
    return nullptr;
}

char* SymbolResolve::GetCharFromStr(const std::string& str) 
{
    if (strToCharMap.find(str) == strToCharMap.end()) {
        char* data = strdup(str.c_str());
        strToCharMap[str] = data;
    }
    return strToCharMap[str];
}

struct Symbol* SymbolResolve::MapUserAddr(int pid, unsigned long addr)
{
    if (this->moduleMap.find(pid) == this->moduleMap.end()) {
        pcerr::New(LIBSYM_ERR_NOT_FIND_PID, "The libsym process ID " + std::to_string(pid) + " cannot be found.");
        return nullptr;
    }

    std::shared_ptr<ModuleMap> module = this->AddrToModule(this->moduleMap.at(pid), addr);
    if (!module) {
        return nullptr;
    }
    /**
     * Try to search elf data first
     */
    symSafeHandler.tryLock(pid);
    if (this->symbolMap.find(pid) == this->symbolMap.end()) {
        this->symbolMap[pid] = {};
    }
    symSafeHandler.releaseLock(pid);
    auto it = this->symbolMap.at(pid).find(addr);
    if (it != this->symbolMap.at(pid).end()) {
        return it->second;
    }
    struct Symbol* symbol = InitializeSymbol(addr);
    symbol->module = GetCharFromStr(module->moduleName);
    unsigned long addrToSearch = addr;
    if (addrToSearch > 0xFFFFFF) {
        addrToSearch = addrToSearch - module->start;
    }

    std::string moduleName = module->moduleName;
    if (!module->mntPoint.empty()) {
        symbol->mntPoint = GetCharFromStr(module->mntPoint);
        moduleName = module->mntPoint + "/" + module->moduleName;
    }

    auto ResOrErr = Symbolizer.symbolizeCode(moduleName, addrToSearch);

    if (ResOrErr) {
        if (module->moduleType == RecordModuleType::RECORD_ALL) {
            if (ResOrErr->FileName != "<invalid>") {
                symbol->lineNum = ResOrErr->Line;
                symbol->fileName = GetCharFromStr(ResOrErr->FileName);
                symbol->firstLine = ResOrErr->StartLine;
            }
        }
      
        if (ResOrErr->FunctionName != "<invalid>") {
            symbol->symbolName = GetCharFromStr(ResOrErr->FunctionName);
            symbol->mangleName = GetCharFromStr(ResOrErr->MangleName);
            symbol->offset = ResOrErr->Offset;
            symbol->codeMapEndAddr = ResOrErr->CodeEndAddr;
        }
    }
    symbol->codeMapAddr = addrToSearch;
    this->symbolMap.at(pid).insert({addr, symbol});
    pcerr::New(0, "success");
    return symbol;
}

struct Symbol* SymbolResolve::MapAddr(int pid, unsigned long addr)
{
    struct Symbol* data = nullptr;
    if (addr > KERNEL_START_ADDR) {
        data = this->MapKernelAddr(addr);
        if (data == nullptr) {
            auto symbol = InitializeSymbol(addr);
            symbol->module = KERNEL;
            symbol->fileName = KERNEL;
            symbolUnmap.emplace_back(symbol);
            return symbol;
        }
        data->offset = addr - data->addr;
    } else {
        data = this->MapUserAddr(pid, addr);
    }
    return data;
}

int SymbolResolve::RecordKernel()
{
    if (!this->ksymArray.empty()) {
        pcerr::New(0, "success");
        return 0;
    }
    //  Prevent multiple threads from processing kernel data at the same time.
    std::lock_guard<std::mutex> guard(kernelMutex);

    FILE* kallsyms = fopen("/proc/kallsyms", "r");
    if (__glibc_unlikely(kallsyms == nullptr)) {
        pcerr::New(LIBSYM_ERR_KALLSYMS_INVALID,
                   "libsym failed to open /proc/kallsyms, found that file /proc/kallsyms " + std::string{strerror(errno)});
        return LIBSYM_ERR_KALLSYMS_INVALID;
    }

    char line[MAX_LINE_LENGTH];
    char mode;
    __u64 addr;
    char name[KERNEL_MODULE_LNE];

    while (fgets(line, sizeof(line), kallsyms)) {
        if (sscanf(line, "%llx %c %s%*[^\n]\n", &addr, &mode, name) == EOF) {
            continue;
        }
        std::shared_ptr<Symbol> data = std::make_shared<Symbol>();
        data->symbolName = GetCharFromStr(name);
        data->mangleName = GetCharFromStr(name);
        data->addr = addr;
        data->fileName = KERNEL;
        data->module = KERNEL;
        data->lineNum = 0;
        this->ksymArray.emplace_back(data);
    }

    fclose(kallsyms);
    pcerr::New(0, "success");
    return 0;
}

int SymbolResolve::UpdateModule(int pid, const char* moduleName, unsigned long startAddr, RecordModuleType recordModuleType)
{
    if (pid < 0) {
        pcerr::New(LIBSYM_ERR_PARAM_PID_INVALID, "libsym param process ID must be greater than 0");
        return LIBSYM_ERR_PARAM_PID_INVALID;
    }
    std::shared_ptr<ModuleMap> data = std::make_shared<ModuleMap>();
    std::string mntPoint = GetMntPoint(pid);
    data->moduleName = moduleName;
    data->start = startAddr;
    std::string recordModule = std::string{moduleName};
    if (!mntPoint.empty()) {
        data->mntPoint = mntPoint;
        recordModule = mntPoint + "/" + recordModule;
    }

    if (!SymbolUtils::IsFile(recordModule.c_str())) {
        pcerr::New(LIBSYM_ERR_FILE_NOT_RGE, "libsym detects that the input paramter fileName is not a file");
        return LIBSYM_ERR_FILE_NOT_RGE;
    }

    if (this->moduleMap.find(pid) == this->moduleMap.end()) {
        int ret = RecordModule(pid, recordModuleType);
        if (ret != 0) {
            return ret;
        }
    }
    auto modV = moduleMap[pid];
    bool findModule = false;
    for (auto item : modV) {
        if (item->moduleName.compare(moduleName) == 0) {
            findModule = true;
            break;
        }
    }
    if (!findModule) {
        this->moduleMap[pid].emplace_back(data);
    }
    pcerr::New(0, "success");
    return 0;
}

struct StackAsm* SymbolResolve::MapAsmCode(const char* moduleName, unsigned long startAddr, unsigned long endAddr)
{
    struct StackAsm* stackAsm = MapAsmCodeStack(moduleName, startAddr, endAddr);
    return stackAsm;
}

struct Symbol* SymbolResolve::MapCodeAddr(const char* moduleName, unsigned long startAddr)
{
    if (!SymbolUtils::IsFile(moduleName)) {
        pcerr::New(LIBSYM_ERR_FILE_NOT_RGE, "libsym detects that the input parameter fileName is not a file");
        return nullptr;
    }
    struct Symbol* symbol = InitializeSymbol(startAddr);
    symbol->module = GetCharFromStr(moduleName);
    
    auto ResOrErr = Symbolizer.symbolizeCode(moduleName, startAddr);

    if (ResOrErr) {
        if (ResOrErr->FileName != "<invalid>") {
            symbol->lineNum = ResOrErr->Line;
            symbol->firstLine = ResOrErr->StartLine;
            symbol->fileName = GetCharFromStr(ResOrErr->FileName);
        }
        if (ResOrErr->FunctionName != "<invalid>") {
            symbol->symbolName = GetCharFromStr(ResOrErr->FunctionName);
            symbol->mangleName = GetCharFromStr(ResOrErr->MangleName);
            symbol->offset = ResOrErr->Offset;
            symbol->codeMapEndAddr = ResOrErr->CodeEndAddr;
        }
    }
    symbol->codeMapAddr = startAddr;
    return symbol;
}

static const void* loadElf(off_t offset, size_t size, size_t lim, void* base) {
    if (offset + size > lim) {
        return nullptr;
    }
    return (const char*)base + offset;
}

int MyElf::LoadMmap() {
    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0) {
        pcerr::New(LIBSYM_ERR_OPEN_FILE_FAILED);
        return LIBSYM_ERR_OPEN_FILE_FAILED;
    }

    off_t end = lseek(fd, 0, SEEK_END);
    if (end == (off_t) - 1) {
        close(fd);
        pcerr::New(LIBSYM_ERR_READ_BUILDID);
        return LIBSYM_ERR_READ_BUILDID;
    }

    lim = end;
    base = mmap(nullptr, lim, PROT_READ, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        close(fd);
        pcerr::New(LIBSYM_ERR_READ_BUILDID);
        return LIBSYM_ERR_READ_BUILDID;
    }
    close(fd);
    return this->CheckElfHeader();
}

const void* MyElf::Load(off_t offset, size_t size) {
    return loadElf(offset, size, lim, base);
}

int MyElf::CheckElfHeader() {
    const void* elfLoad = Load(0, sizeof(ElfHdr));
    if (!elfLoad) {
        pcerr::New(LIBSYM_ERR_READ_BUILDID);
        return LIBSYM_ERR_READ_BUILDID;
    }

    elfHdr = (struct ElfHdr*) elfLoad;

    if (strncmp(elfHdr->elfFormat, "\x7f" "ELF", 4) != 0) {
        pcerr::New(LIBSYM_ERR_READ_BUILDID, "bad ELF format");
        return LIBSYM_ERR_READ_BUILDID;
    }

    if (elfHdr->elfVersion != 1) {
        pcerr::New(LIBSYM_ERR_READ_BUILDID, "bad ELF version");
        return LIBSYM_ERR_READ_BUILDID;
    }

    if (elfHdr->elfClass != ELFCLASS32 && elfHdr->elfClass != ELFCLASS64) {
        pcerr::New(LIBSYM_ERR_READ_BUILDID, "bad ELF class");
        return LIBSYM_ERR_READ_BUILDID;
    }

    if (elfHdr->elfData != 1 && elfHdr->elfData) {
        pcerr::New(LIBSYM_ERR_READ_BUILDID, "bad ELF data");
        return LIBSYM_ERR_READ_BUILDID;
    }
    return SUCCESS;
}

int MyElf::ElfGetBuildId(char** buildId)
{
    if (elfHdr->elfClass == ELFCLASS32) {
        return ElfParser<Elf32_Ehdr, Elf32_Phdr, Elf32_Shdr>(buildId);
    } else if(elfHdr->elfClass == ELFCLASS64) {
        return ElfParser<Elf64_Ehdr, Elf64_Phdr, Elf64_Shdr>(buildId);
    }
    return LIBSYM_ERR_READ_BUILDID;
}

template<typename Ehdr, typename Phdr, typename Shdr>
int MyElf::ElfParser(char** buildId) 
{
    static int BUILD_ID_TYPE = 3;
    Ehdr* ehdr = reinterpret_cast<Ehdr*>(base);
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        pcerr::New(LIBSYM_ERR_READ_BUILDID, "bad file type");
        return LIBSYM_ERR_READ_BUILDID;
    }

    Phdr* phdr = reinterpret_cast<Phdr*>(((uint8_t*)base) + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_NOTE) {
            continue;
        }
        const void* data = Load(phdr[i].p_offset, phdr[i].p_filesz);
        if (data == nullptr) {
            pcerr::New(LIBSYM_ERR_READ_BUILDID, "load elf data failed");
            return LIBSYM_ERR_READ_BUILDID;
        }
        auto header = (ElfNoteHeader*)data;
        auto ptr = (char*)header;
        ptr += sizeof(*header);
        char* name = ptr;
        ptr += header->nameSize;
        if (header->type == BUILD_ID_TYPE && 
            header->nameSize == sizeof("GNU") &&
            memcmp(name, "GNU", header->nameSize) == 0) {
            char buildIdStr[2*header->descSize + 1];
            memset(buildIdStr, 0, 2*header->descSize + 1);
            for (size_t j = 0; j < header->descSize; j++) {
                sprintf(buildIdStr + j*2, "%02x", ptr[j]);
            }
            buildIdStr[2 * header->descSize] = '\0';
            *buildId = InitChar(2*header->descSize);
            strcpy(*buildId, buildIdStr);
            return SUCCESS;
        }
    }
    pcerr::New(LIBSYM_ERR_READ_BUILDID, "can't find buildId");
    return LIBSYM_ERR_READ_BUILDID;
}

int SymbolResolve::GetBuildId(const char *moduleName, char **buildId)
{
    MyElf myElf(moduleName);
    int rt = myElf.LoadMmap();
    if (rt != 0) {
        return rt;
    }
    return myElf.ElfGetBuildId(buildId);
}

struct StackAsm* ReadAsmCodeFromPipe(FILE* pipe)
{
    struct StackAsm* head = nullptr;
    struct StackAsm* last = nullptr;
    int lineLen = MAX_LINE_LENGTH;
    char line[lineLen];
    std::string srcFileName = "unknown";
    unsigned int srcLineNum = 0;
    while (!feof(pipe)) {
        memset(line, 0, lineLen);
        if (!fgets(line, lineLen, pipe)) {
            break;
        }
        if (!line || (line && line[0] == '\0') || (line && line[0] == '\n')) {
            continue;
        }
        struct StackAsm* stackAsm = nullptr;
        if (strstr(line, "File Offset") != nullptr) {
            stackAsm = FirstLineMatch(line);
        }
        if (!stackAsm && head) {
            if (!CheckColonSuffix(line)) {
                continue;
            }
            if (MatchFileName(line, srcFileName, srcLineNum)) {
                continue;
            }
            AsmCode* asmCode = MatchAsmCode(line, srcFileName, srcLineNum);
            if (!asmCode) {
                continue;
            }
            struct StackAsm* current = CreateNode<struct StackAsm>();
            current->funcName = nullptr;
            current->asmCode = asmCode;
            last->next = current;
            last = current;
            continue;
        }
        if (!head && stackAsm) {
            AddTail<struct StackAsm>(&head, &stackAsm);
            last = head;
        }
        if (stackAsm && head) {
            last->next = stackAsm;
            last = stackAsm;
        }
    }
    pclose(pipe);
    pipe = nullptr;
    return head;
}

struct StackAsm* SymbolResolve::MapAsmCodeStack(
        const std::string& moduleName, unsigned long startAddr, unsigned long endAddr)
{
    char startAddrStr[ADDR_LEN];
    char endAddrStr[ADDR_LEN];

    if (!ExistPath(moduleName)) {
        pcerr::New(LIBSYM_ERR_FILE_INVALID, "file does not exist");
        return nullptr;
    }

    if (startAddr >= endAddr) {
        pcerr::New(LIBSYM_ERR_START_SMALLER_END, "libysm the end address must be greater than the start address");
        return nullptr;
    }
    if (snprintf(startAddrStr, ADDR_LEN, "0x%lx", startAddr) < 0) {
        pcerr::New(LIBSYM_ERR_SNPRINF_OPERATE_FAILED, "libsym fails to execute snprintf");
        return nullptr;
    }

    if (snprintf(endAddrStr, ADDR_LEN, "0x%lx", endAddr) < 0) {
        pcerr::New(LIBSYM_ERR_SNPRINF_OPERATE_FAILED, "libsym fails to execute snprintf");
        return nullptr;
    }

    std::string cmd = "objdump -Fld " + moduleName + " --start-address=" + std::string{startAddrStr} +
                      " --stop-address=" + std::string{endAddrStr};
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        pcerr::New(LIBSYM_ERR_CMD_OPERATE_FAILED,
                   "libsym fails to obtain the assembly instruction" + std::string{strerror(errno)});
        return nullptr;
    }
    struct StackAsm* head = ReadAsmCodeFromPipe(pipe);
    pcerr::New(0, "success");
    return head;
}

std::vector<std::shared_ptr<ModuleMap>> SymbolResolve::FindDiffMaps(
        const std::vector<std::shared_ptr<ModuleMap>>& oldMaps,
        const std::vector<std::shared_ptr<ModuleMap>>& newMaps) const
{
    std::vector<std::shared_ptr<ModuleMap>> diffMaps;
    std::set<unsigned long> oldStarts;
    for (const auto& oldMod : oldMaps) {
        oldStarts.insert(oldMod->start);
    }
    for (auto newMod : newMaps) {
        if (oldStarts.find(newMod->start) ==  oldStarts.end()) {
            diffMaps.emplace_back(newMod);
        }
    }
    return diffMaps;
}

SymbolResolve* SymbolResolve::instance = nullptr;
std::mutex SymbolResolve::mutex;
std::mutex SymbolResolve::kernelMutex;
