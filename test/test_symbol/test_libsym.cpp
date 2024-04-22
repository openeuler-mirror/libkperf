/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * libkperf is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Li
 * Create: 2024-04-03
 * Description: Provide ut tests for symbol.so .
 ******************************************************************************/
#include <gtest/gtest.h>
#include <vector>
#include <map>
#include <iostream>
#include <linux/types.h>
#include <link.h>
#include <thread>
#include <stdio.h>
#include "pcerrc.h"
#include "symbol_resolve.h"
#include "linked_list.h"
#include "symbol.h"
/*******************************************
 * @brief  DT testcases for libsym.so APIs *
 * @return                                 *
 *******************************************/
/**
 * Successfully record kernel symbol info
 */
using namespace KUNPENG_SYM;

static pid_t demoPid;
class TestLibSym : public testing::Test {
public:
    static std::string GetExePath()
    {
        char *buffer;
        buffer = getcwd(NULL, 256);
        std::string exeDir = std::string{buffer};
        std::string exePath = exeDir + "/case/libsym_case";
        return exePath;
    }

    static void SetUpTestCase()
    {
        std::string exePath = GetExePath();
        std::cout << "exe path:" << exePath << std::endl;
        demoPid = fork();
        if (demoPid == 0) {
            char *const *dummy = nullptr;
            execvp(exePath.c_str(), dummy);
            _exit(errno);
        }
        std::cout << "exe pid: " << demoPid << std::endl;
    }

    static void TearDownTestCase()
    {
        if (kill(demoPid, SIGTERM) != 0) {
            std::cout << "kill pid " << std::to_string(demoPid) << "failed" << std::endl;
        }
        demoPid = -1;
    }

    void TearDown()
    {
        std::cout << "TearDown" << std::endl;
    }
};

std::string& Trim(std::string &item, const std::string &delims)
{
    auto index = item.find_last_not_of(delims);
    if (index != std::string::npos) {
        item.erase(++index);
    }
    index = item.find_first_not_of(delims);
    if (index != std::string::npos) {
        item.erase(0, index);
    }
    return item;
}

void StrSplit(char *&line, std::vector<std::string> &vets, char split)
{
    if (strlen(line) == 0)
        return;
    char *p = line;
    bool hasdata = false;
    while (*p != split && *p) {
        hasdata = true;
        p++;
    }

    if (hasdata) {
        std::string ret = std::string{line, p};
        vets.push_back(ret);
    }

    while (*p == split) {
        p++;
    }
    line = p;
    StrSplit(line, vets, split);
}

struct FuncSym {
    uintptr_t low;
    uintptr_t high;
    std::string name;
    FuncSym(uintptr_t l, uintptr_t h, std::string name) : low(l), high(h), name(name)
    {}
};

std::string GetPthreadSoPath(int pid)
{
    std::string cmd = "cat /proc/" + std::to_string(pid) + "/maps | grep libpthread";
    FILE *pipe = popen(cmd.c_str(), "r");
    int bufferLen = 128;
    char buffer[bufferLen];
    char moduleName[bufferLen];
    while (!feof(pipe)) {
        if (fgets(buffer, bufferLen, pipe) != nullptr) {
            if (!sscanf(buffer, "%*s %*s %*s %*s %*s %s", moduleName)) {
                memset(buffer, 0, bufferLen);
                continue;
            }
            memset(buffer, 0, bufferLen);
            break;
        }
    }
    pclose(pipe);
    return std::string{moduleName};
}

std::vector<FuncSym> Readelf(const std::string &fileName)
{
    std::string cmd = "readelf -s " + fileName;
    FILE *pipe = popen(cmd.c_str(), "r");
    int bufferLen = 128;
    char buffer[bufferLen];
    std::vector<FuncSym> symbols;
    while (!feof(pipe)) {
        if (fgets(buffer, bufferLen, pipe) != nullptr) {
            if (buffer[0] == '\0') {
                continue;
            }
            std::string data = std::string{buffer};
            if (data.empty()) {
                continue;
            }
            if (data.find(":") == std::string::npos) {
                memset(buffer, 0, bufferLen);
                continue;
            }
            if (data.find("Symbol table") != std::string::npos || data.find("Num:") != std::string::npos) {
                memset(buffer, 0, bufferLen);
                continue;
            }
            std::vector<std::string> vets;
            char *resultStr = buffer;
            StrSplit(resultStr, vets, ' ');
            std::string start = "0x" + vets[1];
            std::string offset = vets[2];
            std::string func = vets[7];
            unsigned long long int startInt = std::stoull(start.c_str(), nullptr, 16);
            unsigned long long int offsetInt = std::stoull(offset.c_str(), nullptr, 10);
            unsigned long long int endInt = startInt + offsetInt;
            if (startInt == 0) {
                continue;
            }
            symbols.emplace_back(startInt, endInt, func);
        }
        memset(buffer, 0, bufferLen);
    }
    pclose(pipe);
    pipe = nullptr;
    return symbols;
}

std::unordered_map<unsigned long, int> GetReadelfData(const std::string &fname)
{
    clock_t start, end;
    std::string cmd = "readelf -wl " + fname;
    start = clock();
    FILE *pipe = popen(cmd.c_str(), "r");
    int bufferLen = 128;
    char buffer[bufferLen];
    std::unordered_map<unsigned long, int> lineMap;
    bool line = false;
    std::string address = "";
    std::string entryStr = "1";
    std::string lineStr = "1";
    while (!feof(pipe)) {

        if (fgets(buffer, bufferLen, pipe) != nullptr) {
            char *resultStr = buffer;
            std::string data = std::string{buffer};
            if (data.find("Line Number Statements:") != std::string::npos) {
                line = true;
                memset(buffer, 0, bufferLen);
                continue;
            }
            if (line) {
                std::vector<std::string> vets;
                StrSplit(resultStr, vets, ' ');
                if (data.find("set Address") != std::string::npos) {
                    address = vets[7];
                };

                if (data.find("entry") != std::string::npos) {
                    entryStr = vets[6];
                }

                if (data.find("Advance Line by") != std::string::npos) {
                    lineStr = vets[6];
                }

                if (data.find("advance Address by") != std::string::npos) {
                    address = vets[9];
                    lineStr = vets[15];
                }
                unsigned long addr = SymbolUtils::SymStoul(address);
                int line = std::stoi(lineStr);
                lineMap.insert({addr, line});
            }
            memset(buffer, 0, bufferLen);
        }
    }
    pclose(pipe);
    return lineMap;
}

void CoutStack(struct Stack *result)
{
    while (result != nullptr) {
        if (result->symbol != nullptr) {
            Symbol *data = result->symbol;
            std::cout << std::hex << data->addr << " " << data->symbolName << "+0x" << data->offset << " "
                      << data->codeMapAddr << " (" << data->module << ")"
                      << " (" << std::dec << data->fileName << ":" << data->lineNum << ")" << std::endl;
        } else {
            std::cout << Perror() << std::endl;
        }
        result = result->next;
    }
}

TEST(symbol, record_kernel_success)
{
    SymResolverInit();
    int ret = SymResolverRecordKernel();
    EXPECT_TRUE(ret == 0);
}

/** For root users, we suppose to have any access of the current running process */
TEST(symbol, record_user_module_root_error)
{
    SymResolverInit();
    int ret = SymResolverRecordModule(1);
    EXPECT_TRUE(ret == LIBSYM_ERR_OPEN_FILE_FAILED);
}

/** For pid < 1, we can't get data */
TEST(symbol, record_user_module_pid_error)
{
    SymResolverInit();
    int ret = SymResolverRecordModule(-1);
    EXPECT_TRUE(ret == LIBSYM_ERR_PARAM_PID_INVALID);
}

/** For regular users, we only have access to process owned by current user */
TEST(symbol, record_user_module_regular_success)
{
    SymResolverInit();
    int pid = getpid();
    int ret = SymResolverRecordModule(pid);
    EXPECT_TRUE(ret == 0);
}

/** For regular users, we only have access to process owned by current user */
TEST(symbol, record_elf_success)
{
    SymResolverInit();
    std::string exePath = TestLibSym::GetExePath();
    int ret = SymResolverRecordElf(exePath.c_str());
    EXPECT_TRUE(ret == 0);
}

/** For regular users, we only have access to process owned by current user */
TEST(symbol, record_elf_failed)
{
    SymResolverInit();
    std::string fileName = "test" + std::to_string(std::time(nullptr));
    int ret = SymResolverRecordElf(fileName.c_str());
    EXPECT_TRUE(ret == LIBSYM_ERR_FILE_NOT_RGE);
}

/** For regular users, we only have access to process owned by current user */
TEST(symbol, record_dwarf_success)
{
    SymResolverInit();
    std::string exePath = TestLibSym::GetExePath();
    int ret = SymResolverRecordDwarf(exePath.c_str());
    EXPECT_TRUE(ret == 0);
}

/** For regular users, we only have access to process owned by current user */
TEST(symbol, record_dwarf_failed)
{
    SymResolverInit();
    std::string fileName = "test" + std::to_string(std::time(nullptr));
    int ret = SymResolverRecordDwarf(fileName.c_str());
    EXPECT_TRUE(ret == LIBSYM_ERR_FILE_NOT_RGE);
}

/** For regular users, we only have access to process owned by current user */
TEST(symbol, record_dwarf_failed2)
{
    SymResolverInit();
    std::string fileName = "/home";
    int ret = SymResolverRecordDwarf(fileName.c_str());
    EXPECT_TRUE(ret == LIBSYM_ERR_FILE_NOT_RGE);
}

/** For regular users, we only have access to process owned by current user */
TEST_F(TestLibSym, sym_map_addr_suc)
{
    SymResolverInit();
    int pid = demoPid;

    /** make sure the pid exists */
    int ret = SymResolverRecordModule(pid);
    EXPECT_TRUE(ret == 0);

    ret = SymResolverRecordModule(pid);
    EXPECT_TRUE(ret == 0);

    /** record kernel symbol first */
    ret = SymResolverRecordKernel();
    EXPECT_TRUE(ret == 0);

    std::unordered_map<unsigned long, int> lineMap = GetReadelfData(TestLibSym::GetExePath());
    auto lineObj = lineMap.begin();

    /** resolve symbol based on the recorded pid */
    auto data = SymResolverMapAddr(pid, lineObj->first);
    EXPECT_TRUE(data != nullptr);

    std::cout << std::hex << data->addr << " " << data->symbolName << "+0x" << data->offset << " (" << data->module
              << ")" << data->lineNum << std::endl;

    EXPECT_TRUE(data->addr == lineObj->first);
    EXPECT_TRUE(data->lineNum == lineObj->second);
}

TEST(symbol, sym_map_addr_suc_failed)
{
    SymResolverInit();
    int pid = -1;
    auto data = SymResolverMapAddr(pid, 0x4008e8);
    EXPECT_TRUE(data == nullptr);
}

TEST_F(TestLibSym, update_module_suc)
{
    int pid = demoPid;
    SymResolverInit();
    /** record kernel symbol first */
    int ret = SymResolverRecordKernel();
    EXPECT_TRUE(ret == 0);
    std::unordered_map<unsigned long, int> lineMap = GetReadelfData(TestLibSym::GetExePath());
    auto lineObj = lineMap.begin();
    ret = SymResolverUpdateModule(pid, TestLibSym::GetExePath().c_str(), lineObj->first);
    EXPECT_TRUE(ret == 0);

    unsigned long stack[] = {lineObj->first};

    unsigned long *p = stack;

    Stack *result = StackToHash(pid, p, 1);

    EXPECT_TRUE(Perrorno() == 0);

    EXPECT_TRUE(result->symbol->addr == lineObj->first);
    EXPECT_TRUE(result->symbol->lineNum == lineObj->second);

    CoutStack(result);
}

TEST(symbol, update_module_failed_module_open_error)
{
    int pid = getpid();
    std::string fileName = "test" + std::to_string(std::time(nullptr));
    int ret = SymResolverUpdateModule(pid, fileName.c_str(), 0x00000000004008c4);
    EXPECT_TRUE(ret == LIBSYM_ERR_FILE_NOT_RGE);
}

TEST(symbol, update_module_failed_pid_error)
{
    int pid = -1;
    std::string fileName = "test" + std::to_string(std::time(nullptr));
    int ret = SymResolverUpdateModule(pid, fileName.c_str(), 0x00000000004008c4);
    EXPECT_TRUE(ret == LIBSYM_ERR_PARAM_PID_INVALID);
}

void StackToHashThread()
{
    int pid = demoPid;
    SymResolverInit();
    /** make sure the pid exists */
    int ret = SymResolverRecordModule(pid);
    EXPECT_TRUE(ret == 0);

    std::unordered_map<unsigned long, int> lineMap = GetReadelfData(TestLibSym::GetExePath());
    auto lineObj = lineMap.begin();

    /** record kernel symbol first */
    ret = SymResolverRecordKernel();
    std::cout << Perror() << std::endl;
    EXPECT_TRUE(ret == 0);

    unsigned long stack[] = {lineObj->first};

    unsigned long *p = stack;

    Stack *result = StackToHash(pid, p, 1);

    EXPECT_TRUE(Perrorno() == 0);

    Stack *result_second = StackToHash(pid, p, 1);

    EXPECT_TRUE(Perrorno() == 0);

    EXPECT_TRUE(result->symbol->addr == lineObj->first);
    EXPECT_TRUE(result->symbol->lineNum == lineObj->second);

    CoutStack(result);
}

TEST_F(TestLibSym, stack_to_hash)
{
    std::thread a(StackToHashThread);
    std::thread b(StackToHashThread);
    StackToHashThread();
    a.join();
    b.join();
}

void CoutAsmCode(struct StackAsm *stackAsm)
{
    struct StackAsm *head = stackAsm;
    while (stackAsm != nullptr) {
        if (stackAsm->funcName) {
            std::cout << "fuc" << std::endl;
            std::cout << std::hex << stackAsm->funcStartAddr << ":" << stackAsm->funcName << ":"
                      << stackAsm->functFileOffset << std::endl;
            stackAsm = stackAsm->next;
            continue;
        }
        std::cout << "code" << std::endl;
        std::cout << std::hex << stackAsm->asmCode->fileName << ":" << stackAsm->asmCode->lineNum
                  << stackAsm->asmCode->addr << ":" << stackAsm->asmCode->code << std::endl;
        stackAsm = stackAsm->next;
    }
}

TEST_F(TestLibSym, map_asm_code)
{
    std::string fileName = TestLibSym::GetExePath();
    std::unordered_map<unsigned long, int> lineMap = GetReadelfData(TestLibSym::GetExePath());

    auto lineObj = lineMap.begin();
    Symbol *symbol = SymResolverMapCodeAddr(fileName.c_str(), lineObj->first);

    struct StackAsm *stackAsm =
        SymResolverAsmCode(fileName.c_str(), symbol->codeMapAddr - symbol->offset, symbol->codeMapEndAddr);
    EXPECT_TRUE(stackAsm != nullptr);
    CoutAsmCode(stackAsm);
    SymResolverDestroy();
}

TEST_F(TestLibSym, map_asm_code_so)
{
    std::string fileName = GetPthreadSoPath(demoPid);
    std::vector<FuncSym> funcVets = Readelf(fileName);
    FuncSym select = {0, 0, ""};
    for (auto symFunc : funcVets) {
        if (strstr(symFunc.name.c_str(), "pthread_mutexattr_setrobu") != nullptr) {
            select = symFunc;
            break;
        }
    }
    Symbol *symbol = SymResolverMapCodeAddr(fileName.c_str(), select.low);
    struct StackAsm *stackAsm =
        SymResolverAsmCode(fileName.c_str(), symbol->codeMapAddr - symbol->offset, symbol->codeMapEndAddr);
    EXPECT_TRUE(stackAsm != nullptr);
    CoutAsmCode(stackAsm);
    SymResolverDestroy();
}

TEST_F(TestLibSym, get_src_code)
{
    std::string fileName = TestLibSym::GetExePath();
    std::unordered_map<unsigned long, int> lineMap = GetReadelfData(TestLibSym::GetExePath());
    auto lineObj = lineMap.begin();
    Symbol *symbol = SymResolverMapCodeAddr(fileName.c_str(), lineObj->first);
    EXPECT_TRUE(symbol->addr == lineObj->first);
    EXPECT_TRUE(symbol->lineNum == lineObj->second);
}

TEST_F(TestLibSym, get_src_code_so)
{
    std::string fileName = GetPthreadSoPath(demoPid);
    std::vector<FuncSym> funcVets = Readelf(fileName);
    FuncSym select = {0, 0, ""};
    for (auto symFunc : funcVets) {
        if (strstr(symFunc.name.c_str(), "pthread_mutexattr_setrobu") != nullptr) {
            select = symFunc;
            break;
        }
    }
    Symbol *symbol = SymResolverMapCodeAddr(fileName.c_str(), select.low);
    EXPECT_TRUE(symbol != nullptr);
}

TEST(symbol, get_asm_code_failed)
{
    std::string fileName = "test" + std::to_string(std::time(nullptr));
    unsigned long startAddr = 0x40081;
    unsigned long endAddr = 0x40082;
    struct StackAsm *stackAsm = SymResolverAsmCode(fileName.c_str(), startAddr, endAddr);
    EXPECT_TRUE(stackAsm == nullptr);
}

void ClearSymbol(){
    SymResolverDestroy();
}

TEST(symbol, test_clear)
{
    std::thread a(ClearSymbol);
    std::thread b(ClearSymbol);
    ClearSymbol();
    a.join();
    b.join();
}

void InsertSafeDataAndDoRead(std::unordered_map<int, int>& map, SafeHandler<int>& lock)
{
    for (int i = 0; i < 2000; i++) {
        while (true) {
            lock.tryLock(1);
            if(map.find(i) != map.end()){
                lock.releaseLock(1);
                break;;
            }
            usleep(500);
            lock.releaseLock(1);
        }
        lock.tryLock(1);
        ASSERT_TRUE(map.at(i) == i);
        lock.releaseLock(1);
    }
}

void InsertSafeDataAndDoInsert(std::unordered_map<int, int>& map, SafeHandler<int>& lock)
{
    for (int i = 0; i < 1000; i++) {
        lock.tryLock(1);
        map.insert({i, i});
        lock.releaseLock(1);
    }
}

void InsertSafeDataAndDoInsert2(std::unordered_map<int, int>& map, SafeHandler<int>& lock)
{
    for (int i = 1000; i < 2000; i++) {
        lock.tryLock(1);
        map.insert({i, i});
        lock.releaseLock(1);
    }
}


TEST(symbol, test_safe_handler){
    SafeHandler<int> lock;
    std::unordered_map<int, int> map;
    std::thread a(InsertSafeDataAndDoInsert, std::ref(map), std::ref(lock));
    std::thread b(InsertSafeDataAndDoInsert2, std::ref(map), std::ref(lock));
    std::thread c(InsertSafeDataAndDoRead, std::ref(map), std::ref(lock));
    std::thread d(InsertSafeDataAndDoRead, std::ref(map), std::ref(lock));
    a.join();
    b.join();
    c.join();
    d.join();
}