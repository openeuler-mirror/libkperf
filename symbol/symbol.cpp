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
 * Description: Provide the tool function and data structure of the complete symbol analysis and stack analysis.
 ******************************************************************************/
#include <iostream>
#include <mutex>
#include "symbol_resolve.h"
#include "pcerr.h"
#include "symbol.h"

using namespace KUNPENG_SYM;
void SymResolverInit()
{
    SymbolResolve::GetInstance();
}

int SymResolverRecordKernel()
{
    try {
        return SymbolResolve::GetInstance()->RecordKernel();
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return COMMON_ERR_NOMEM;
    }
}

int SymResolverRecordModule(int pid)
{
    try {
        return SymbolResolve::GetInstance()->RecordModule(pid, RecordModuleType::RECORD_ALL);
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return COMMON_ERR_NOMEM;
    }
}

int SymResolverRecordModuleNoDwarf(int pid)
{
    try {
        return SymbolResolve::GetInstance()->RecordModule(pid, RecordModuleType::RECORD_NO_DWARF);
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return COMMON_ERR_NOMEM;
    }
}

int SymResolverRecordElf(const char* fileName)
{
    try {
        return SymbolResolve::GetInstance()->RecordElf(fileName);
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return COMMON_ERR_NOMEM;
    }
}

int SymResolverRecordDwarf(const char* fileName)
{
    try {
        return SymbolResolve::GetInstance()->RecordDwarf(fileName);
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return COMMON_ERR_NOMEM;
    }
}

void SymResolverDestroy()
{
    SymbolResolve::GetInstance()->Clear();
}

struct Stack* StackToHash(int pid, unsigned long* stack, int nr)
{
    try {
        return SymbolResolve::GetInstance()->StackToHash(pid, stack, nr);
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return nullptr;
    }
}

struct Symbol* SymResolverMapAddr(int pid, unsigned long addr)
{
    try {
        return SymbolResolve::GetInstance()->MapAddr(pid, addr);
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return nullptr;
    }
}

int SymResolverIncrUpdateModule(int pid)
{
    try {
        return SymbolResolve::GetInstance()->UpdateModule(pid, RecordModuleType::RECORD_ALL);
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return COMMON_ERR_NOMEM;
    }
}

int SymResolverIncrUpdateModuleNoDwarf(int pid)
{
    try {
        return SymbolResolve::GetInstance()->UpdateModule(pid, RecordModuleType::RECORD_NO_DWARF);
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return COMMON_ERR_NOMEM;
    }
}

int SymResolverUpdateModule(int pid, const char* moduleName, unsigned long startAddr)
{
    try {
        return SymbolResolve::GetInstance()->UpdateModule(pid, moduleName, startAddr, RecordModuleType::RECORD_ALL);
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return COMMON_ERR_NOMEM;
    }
}

int SymResolverUpdateModuleNoDwarf(int pid, const char* moduleName, unsigned long startAddr)
{
    try {
        return SymbolResolve::GetInstance()->UpdateModule(pid, moduleName, startAddr, RecordModuleType::RECORD_NO_DWARF);
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return COMMON_ERR_NOMEM;
    }
}

struct StackAsm* SymResolverAsmCode(const char* moduleName, unsigned long startAddr, unsigned long endAddr)
{
    try {
        return SymbolResolve::GetInstance()->MapAsmCode(moduleName, startAddr, endAddr);
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return nullptr;
    }
}

struct Symbol* SymResolverMapCodeAddr(const char* moduleName, unsigned long startAddr)
{
    try {
        return SymbolResolve::GetInstance()->MapCodeAddr(moduleName, startAddr);
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return nullptr;
    }
}

int SymGetBuildId(const char *moduleName, char **buildId)
{
    try {
        return SymbolResolve::GetInstance()->GetBuildId(moduleName, buildId);
    } catch (std::bad_alloc& err) {
        pcerr::New(COMMON_ERR_NOMEM);
        return -1;
    }
}

void FreeModuleData(int pid)
{
    return SymbolResolve::GetInstance()->FreeModule(pid);
}

void FreeSymbolPtr(struct Symbol* symbol)
{
    SymbolUtils::FreeSymbol(symbol);
}

void FreeAsmStack(struct StackAsm* stackAsm)
{
    SymbolUtils::FreeStackAsm(&stackAsm);
}
