/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Lei
 * Create: 2025-01-17
 * Description: Pmu trace event analysis module.
 * Current capability: Analyze the time consumed by system invoking function
 ******************************************************************************/
#include <iostream>
#include <vector>
#include "pmu_list.h"
#include "pmu_analysis.h"
#include "pcerr.h"
#include "pmu.h"

using namespace pcerr;
using namespace KUNPENG_PMU;
using namespace std;

static std::mutex SysCallListMtx;
static vector<const char*> SysCallFuncList;

const char** PmuSysCallFuncList(unsigned *numFuncs)
{
    lock_guard<mutex> lg(SysCallListMtx);
    SetWarn(SUCCESS);
    try {
        if (!SysCallFuncList.empty()) {
            *numFuncs = SysCallFuncList.size();
            return SysCallFuncList.data();
        }
        enum PmuEventType eventType = TRACE_EVENT;
        unsigned int numTraceEvents = 0;
        const char **traceEventList = PmuEventList(eventType, &numTraceEvents);
        if (traceEventList == nullptr) {
            return nullptr;
        }
        for (int i = 0; i < numTraceEvents; ++i) {
            if (const char *pos = strstr(traceEventList[i], SYSCALL_FUNC_ENTER_PREFIX)) {
                size_t funcNameLen = strlen(traceEventList[i]) - strlen(SYSCALL_FUNC_ENTER_PREFIX) + 1;
                char *syscallFunName = new char[funcNameLen];
                strcpy(syscallFunName, pos + strlen(SYSCALL_FUNC_ENTER_PREFIX));
                SysCallFuncList.emplace_back(syscallFunName);
            }
        }
    } catch (...) {
        New(LIBPERF_ERR_QUERY_SYSCALL_LIST_FAILED, "Query system call function list failed!");
        return nullptr;
    }
    New(SUCCESS);
    *numFuncs = SysCallFuncList.size();
    return SysCallFuncList.data();
}

void PmuSysCallFuncListFree()
{
    lock_guard<mutex> lg(SysCallListMtx);
    for (auto &funcName : SysCallFuncList) {
        if (funcName != nullptr) {
            delete[] funcName;
        }
    }
    SysCallFuncList.clear();
    New(SUCCESS);
}

static int CheckSysCallName(const char **funList, unsigned numFuns)
{
    const char **sysCallFuns = nullptr;
    unsigned numSysCall = 0;
    if (SysCallFuncList.empty()) {
        sysCallFuns = PmuSysCallFuncList(&numSysCall);
    } else {
        sysCallFuns = SysCallFuncList.data();
        numSysCall = SysCallFuncList.size();
    }
    for (int i = 0; i < numFuns; ++i) {
        bool isValid = false;
        for (int j = 0; j < numSysCall; ++j) {
            if (sysCallFuns[j] != nullptr && funList[i] != nullptr && strcmp(sysCallFuns[j], funList[i]) == 0) {
                isValid = true;
                break;
            }
        }
        if (!isValid) {
            New(LIBPERF_ERR_INVALID_SYSCALL_FUN, "the system call function name error!");
            return LIBPERF_ERR_INVALID_SYSCALL_FUN;
        }
    }
    return SUCCESS;
}

static int CheckTraceAttr(enum PmuTraceType traceType, struct PmuTraceAttr *traceAttr)
{
    if (traceType != TRACE_SYS_CALL) {
        New(LIBPERF_ERR_INVALID_TRACE_TYPE, "traceType config error");
        return LIBPERF_ERR_INVALID_TRACE_TYPE;
    }
    if (traceAttr->funcs == nullptr) {
        return PmuAnalysis::GetInstance()->GenerateSysCallTable();
    }
    auto err = CheckSysCallName(traceAttr->funcs, traceAttr->numFuncs);
    if (err != SUCCESS) {
        return err;
    }

    return SUCCESS;
}

static void EraseTraceAttrEvtList(char **evtList, unsigned numEvt)
{
    for (size_t i = 0; i < numEvt; ++i) {
        delete[] evtList[i];
    }
    delete[] evtList;
}

static bool PdValid(const int pd)
{
    return PmuAnalysis::GetInstance()->IsPdAlive(pd);
}

static char **GeneratePmuAttrEvtList(const char **sysCallFuncs, const unsigned numFuncs, unsigned int &numEvt)
{
    SetWarn(SUCCESS);
    try {
        if (sysCallFuncs == nullptr) {
            numEvt = 2;
            char **syscallEvts = new char* [numEvt];
            size_t exitLen = strlen(EXIT_RAW_SYSCALL) + 1;
            syscallEvts[0] = new char[exitLen];
            strcpy(syscallEvts[0], EXIT_RAW_SYSCALL);
            size_t enterLen = strlen(ENTER_RAW_SYSCALL) + 1;
            syscallEvts[1] = new char[enterLen];
            strcpy(syscallEvts[1], ENTER_RAW_SYSCALL);
            New(SUCCESS);
            return syscallEvts;
        } else {
            numEvt = 0;
            char **syscallEvts = new char* [numFuncs * 2];
            for (size_t i = 0; i < numFuncs; ++i) {
                size_t exitLen = strlen(SYSCALL_FUNC_EXIT_PREFIX) + strlen(sysCallFuncs[i]) + 1;
                syscallEvts[numEvt] = new char[exitLen];
                strcpy(syscallEvts[numEvt], SYSCALL_FUNC_EXIT_PREFIX);
                strcat(syscallEvts[numEvt], sysCallFuncs[i]);
                numEvt++;

                size_t enterLen = strlen(SYSCALL_FUNC_ENTER_PREFIX) + strlen(sysCallFuncs[i]) + 1;
                syscallEvts[numEvt] = new char[enterLen];
                strcpy(syscallEvts[numEvt], SYSCALL_FUNC_ENTER_PREFIX);
                strcat(syscallEvts[numEvt], sysCallFuncs[i]);
                numEvt++;
            }
            New(SUCCESS);
            return syscallEvts;
        }
    } catch (std::bad_alloc&) {
        New(COMMON_ERR_NOMEM);
        return nullptr;
    } catch (std::exception& ex) {
        New(UNKNOWN_ERROR, ex.what());
        return nullptr;
    }
}

int PmuTraceOpen(enum PmuTraceType traceType, struct PmuTraceAttr *traceAttr)
{
    SetWarn(SUCCESS);
    auto err = CheckTraceAttr(traceType, traceAttr);
    if (err != SUCCESS) {
        return -1;
    }
    PmuAttr attr = {0};
    attr.evtList = GeneratePmuAttrEvtList(traceAttr->funcs, traceAttr->numFuncs, attr.numEvt);
    attr.pidList = traceAttr->pidList;
    attr.numPid = traceAttr->numPid;
    attr.cpuList = traceAttr->cpuList;
    attr.numCpu = traceAttr->numCpu;
    attr.period = 1; // configured to sample once when an event occurs

    int pd = PmuOpen(SAMPLING, &attr);
    if (pd == -1) {
        EraseTraceAttrEvtList(attr.evtList, attr.numEvt);
        return -1;
    }

    err = KUNPENG_PMU::PmuAnalysis::GetInstance()->Register(pd, traceAttr);
    EraseTraceAttrEvtList(attr.evtList, attr.numEvt);
    if (err != SUCCESS) {
        PmuAnalysis::GetInstance()->Close(pd);
        return -1;
    }

    return pd;
}

int PmuTraceEnable(int pd)
{
    return PmuEnable(pd);
}

int PmuTraceDisable(int pd)
{
    return PmuDisable(pd);
}

int PmuTraceRead(int pd, struct PmuTraceData **pmuTraceData)
{
    PmuData *pmuData = nullptr;
    unsigned len = PmuRead(pd, &pmuData);
    if (len == -1) {
        return -1;
    }
    if (len == 0) {
        *pmuTraceData = nullptr;
        return 0;
    }
    if (!PdValid(pd)) {
        New(LIBPERF_ERR_INVALID_PD);
        return -1;
    }
    bool isAllCollect =
        (strcmp(pmuData[0].evt, ENTER_RAW_SYSCALL) == 0) || (strcmp(pmuData[0].evt, EXIT_RAW_SYSCALL) == 0);
    std::vector<PmuTraceData>& traceData = isAllCollect ? PmuAnalysis::GetInstance()->AnalyzeRawTraceData(pd, pmuData, len)
                                                        : PmuAnalysis::GetInstance()->AnalyzeTraceData(pd, pmuData, len);
    New(SUCCESS);
    if (!traceData.empty()) {
        *pmuTraceData = traceData.data();
        return traceData.size();
    } else {
        *pmuTraceData = nullptr;
        return 0;
    }
}

void PmuTraceClose(int pd)
{
    SetWarn(SUCCESS);
    try {
        KUNPENG_PMU::PmuAnalysis::GetInstance()->Close(pd);
        PmuSysCallFuncListFree();
        New(SUCCESS);
    } catch (std::bad_alloc&) {
        New(COMMON_ERR_NOMEM);
    } catch (std::exception& ex) {
        New(UNKNOWN_ERROR, ex.what());
    }
}

void PmuTraceDataFree(struct PmuTraceData *pmuTraceData)
{
    SetWarn(SUCCESS);
    KUNPENG_PMU::PmuAnalysis::GetInstance()->FreeTraceData(pmuTraceData);
    New(SUCCESS);
}
