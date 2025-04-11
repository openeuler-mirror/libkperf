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
 * Author: Mr.Zhang
 * Create: 2024-04-03
 * Description: implementations for managing performance monitoring tasks, collecting data,
 * and handling performance counters in the KUNPENG_PMU namespace
 ******************************************************************************/
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <linux/perf_event.h>
#include <cstring>
#include "common.h"
#include "pfm.h"
#include "pfm_event.h"
#include "pmu_event.h"
#include "pmu_list.h"
#include "linked_list.h"
#include "pcerr.h"
#include "safe_handler.h"
#include "pmu_metric.h"
#include "trace_pointer_parser.h"
#include "pmu.h"

using namespace pcerr;
using namespace KUNPENG_PMU;
using namespace std;

static unordered_map<unsigned, bool> runningStatus;
static SafeHandler<unsigned> pdMutex;
static pair<unsigned, const char**> uncoreEventPair;
static std::set<int> onLineCpuIds;

struct PmuTaskAttr* AssignPmuTaskParam(PmuTaskType collectType, struct PmuAttr *attr);

static int PmuCollectStart(const int pd)
{
    auto err = KUNPENG_PMU::PmuList::GetInstance()->Start(pd);
    if (err != SUCCESS) {
        New(err);
        return -1;
    }
    return SUCCESS;
}

static int PmuCollectPause(const int pd)
{
    auto err = KUNPENG_PMU::PmuList::GetInstance()->Pause(pd);
    if (err != SUCCESS) {
        New(err);
        return -1;
    }
    return SUCCESS;
}

static int CheckCpuList(unsigned numCpu, int* cpuList)
{
    const set<int>& onLineCpus = GetOnLineCpuIds();
    if (numCpu > MAX_CPU_NUM) {
        string errMsg = "Invalid numCpu: " + to_string(numCpu);
        New(LIBPERF_ERR_INVALID_CPULIST, errMsg);
        return LIBPERF_ERR_INVALID_CPULIST;
    }
    if (numCpu > 0 && cpuList == nullptr) {
        New(LIBPERF_ERR_INVALID_CPULIST);
        return LIBPERF_ERR_INVALID_CPULIST;
    }
    for (int i = 0; i < numCpu; i++) {
        if (cpuList[i] < 0 || cpuList[i] >= MAX_CPU_NUM) {
            string errMsg = "Invalid cpu id: " + to_string(cpuList[i]) + ", Please check cpu config parameter.";
            New(LIBPERF_ERR_INVALID_CPULIST, errMsg);
            return LIBPERF_ERR_INVALID_CPULIST;
        }
        if (!onLineCpus.count(cpuList[i])) {
            string errMsg = "OffLine cpu id: " + to_string(cpuList[i]);
            New(LIBPERF_ERR_INVALID_CPULIST, errMsg);
            return LIBPERF_ERR_INVALID_CPULIST;
        }
    }
    return SUCCESS;
}

static int CheckPidList(unsigned numPid, int* pidList)
{
    if (numPid > 0 && pidList == nullptr) {
        New(LIBPERF_ERR_INVALID_PIDLIST);
        return LIBPERF_ERR_INVALID_PIDLIST;
    }
    for (int i = 0; i < numPid; i++) {
        if (pidList[i] < 0) {
            string errMsg = "Invalid pid: " + to_string(pidList[i]) + ", Please check pid config parameter.";
            New(LIBPERF_ERR_INVALID_PIDLIST, errMsg);
            return LIBPERF_ERR_INVALID_PIDLIST;
        }
    }
    return SUCCESS;
}

static int CheckEvtList(unsigned numEvt, char** evtList)
{
    if (numEvt > 0 && evtList == nullptr) {
        New(LIBPERF_ERR_INVALID_EVTLIST, "Invalid event list: numEvt is greater than 0, but evtList is null.");
        return LIBPERF_ERR_INVALID_EVTLIST;
    }
    return SUCCESS;
}

static bool InvalidSampleRate(enum PmuTaskType collectType, struct PmuAttr *attr)
{
    // When sampling, sample frequency must be less than or equal to perf_event_max_sample_rate.
    if (collectType != SAMPLING) {
        return false;
    }
    if (!attr->useFreq) {
        return false;
    }
    const string sysSampleRate = "/proc/sys/kernel/perf_event_max_sample_rate";
    ifstream inSys(sysSampleRate);
    if (!inSys.is_open()) {
        // If perf_event_max_sample_rate cannot be read, do not check frequency 
        // and perf_event_open will check later.
        return false;
    }
    unsigned long maxRate = 0;
    inSys >> maxRate;

    return attr->freq > maxRate;
}

static int CheckBranchSampleFilter(const unsigned long& branchSampleFilter, enum PmuTaskType collectType)
{
    if (branchSampleFilter == KPERF_NO_BRANCH_SAMPLE) {
        return SUCCESS;
    }

    if (collectType != SAMPLING) {
        return LIBPERF_ERR_BRANCH_JUST_SUPPORT_SAMPLING;
    }

    unsigned long branchFilterTmp = 0;
    for (int i = 0; i <= 16; i++) {
        if (branchSampleFilter & (1U << i)) {
            branchFilterTmp |= 1U << i;
        }
    }

    if (branchSampleFilter != branchFilterTmp) {
        return LIBPERF_ERR_INVALID_BRANCH_SAMPLE_FILTER;
    }

    // if the filter type is kernel or user or hv, the filter type is not supported. In this case, add the filter type after KPERF_SAMPLE_BRANCH_ANY
    // needs to be added.attr.branchSampleFilter = KPERF_SAMPLE_BRANCH_KERNEL | KPERF_SAMPLE_BRANCH_ANY.
    if (branchSampleFilter <= (KPERF_SAMPLE_BRANCH_KERNEL | KPERF_SAMPLE_BRANCH_USER | KPERF_SAMPLE_BRANCH_HV)) {
        pcerr::SetCustomErr(LIBPERF_ERR_INVALID_BRANCH_SAMPLE_FILTER,
                            "invalid value for branchSampleFilter, must set at least one or more "
                            "bits values greater than or equal to KPERF_SAMPLE_BRANCH_ANY.");
        return LIBPERF_ERR_INVALID_BRANCH_SAMPLE_FILTER;
    }

    return SUCCESS;
}

static int CheckCollectTypeConfig(enum PmuTaskType collectType, struct PmuAttr *attr)
{
    if (collectType < 0 || collectType >= MAX_TASK_TYPE) {
        New(LIBPERF_ERR_INVALID_TASK_TYPE);
        return LIBPERF_ERR_INVALID_TASK_TYPE;
    }
#ifdef IS_X86
    if (collectType != COUNTING && collectType != SAMPLING) {
        New(LIBPERF_ERR_INVALID_TASK_TYPE, "The x86 architecture supports only the COUTING mode and SMAPLING mode");
        return LIBPERF_ERR_INVALID_TASK_TYPE;
    }
#endif
    if ((collectType == COUNTING) && attr->evtList == nullptr) {
        New(LIBPERF_ERR_INVALID_EVTLIST, "Counting mode requires a non-null event list.");
        return LIBPERF_ERR_INVALID_EVTLIST;
    }
    if (collectType != SAMPLING && attr->blockedSample == 1) {
        New(LIBPERF_ERR_INVALID_BLOCKED_SAMPLE, "blocked sample mode only support sampling mode!");
        return LIBPERF_ERR_INVALID_BLOCKED_SAMPLE;
    }
    if (collectType == SAMPLING) {
        if (attr->blockedSample == 0 && attr->evtList == nullptr) {
            New(LIBPERF_ERR_INVALID_EVTLIST, "In sampling mode without blocked sample, the event list cannot be null.");
            return LIBPERF_ERR_INVALID_EVTLIST;
        } else if (attr->blockedSample == 1) {
            if (attr->evtList != nullptr) {
                New(LIBPERF_ERR_NOT_SUPPORT_CONFIG_EVENT, "Blocked sampling mode does not support configuring events to sample!");
                return LIBPERF_ERR_NOT_SUPPORT_CONFIG_EVENT;
            }
            if (attr->evtAttr != nullptr) {
                New(LIBPERF_ERR_NOT_SUPPORT_GROUP_EVENT, "Blocked sampling mode does not support grouped events!");
                return LIBPERF_ERR_NOT_SUPPORT_GROUP_EVENT;
            }
            if (attr->pidList == nullptr) {
                New(LIBPERF_ERR_NOT_SUPPORT_SYSTEM_SAMPLE, "Blocked sampling mode does not support sample system!");
                return LIBPERF_ERR_NOT_SUPPORT_SYSTEM_SAMPLE;
            }
        }
    }
    if (collectType == SPE_SAMPLING && attr->evtAttr != nullptr) {
        New(LIBPERF_ERR_INVALID_GROUP_SPE);
        return LIBPERF_ERR_INVALID_GROUP_SPE;
    }
    return SUCCESS;
}

static int CheckAttr(enum PmuTaskType collectType, struct PmuAttr *attr)
{
    auto err = CheckCpuList(attr->numCpu, attr->cpuList);
    if (err != SUCCESS) {
        return err;
    }
    err = CheckPidList(attr->numPid, attr->pidList);
    if (err != SUCCESS) {
        return err;
    }
    err = CheckEvtList(attr->numEvt, attr->evtList);
    if (err != SUCCESS) {
        return err;
    }
    err = CheckCollectTypeConfig(collectType, attr);
    if (err != SUCCESS) {
        return err;
    }

    if (InvalidSampleRate(collectType, attr)) {
        New(LIBPERF_ERR_INVALID_SAMPLE_RATE);
        return LIBPERF_ERR_INVALID_SAMPLE_RATE;
    }

    err = CheckBranchSampleFilter(attr->branchSampleFilter, collectType);
    if (err != SUCCESS) {
        New(err);
        return err;
    }

    return SUCCESS;
}

static void CopyAttrData(PmuAttr* newAttr, PmuAttr* inputAttr, enum PmuTaskType collectType) 
{
    //Coping event data to prevent delete exceptions
    char **newEvtList = nullptr;
    if ((inputAttr->blockedSample == 1)) {
        newEvtList = new char* [2];
        char* cycles = "cycles";
        newEvtList[0] = new char[strlen(cycles) + 1];
        strcpy(newEvtList[0], cycles);
        char* cs = "context-switches";
        newEvtList[1] = new char[strlen(cs) + 1];
        strcpy(newEvtList[1], cs);
        inputAttr->numEvt = 2;
    } else if (inputAttr->numEvt > 0) {
        newEvtList = new char *[inputAttr->numEvt];
        for (int i = 0; i < inputAttr->numEvt; ++i) {
            newEvtList[i] = new char[strlen(inputAttr->evtList[i]) + 1];
            strcpy(newEvtList[i], inputAttr->evtList[i]);
        }
    }
    newAttr->evtList = newEvtList;
    newAttr->numEvt = inputAttr->numEvt;

    // If the event group ID is not enabled, set the group_id to -1. It indicates that the event is not grouped.
    if ((collectType == SAMPLING || collectType == COUNTING) && inputAttr->evtAttr == nullptr) {
        struct EvtAttr *evtAttr = new struct EvtAttr[newAttr->numEvt];
        // handle event group id. -1 means that it doesn't run event group feature.
        for (int i = 0; i < newAttr->numEvt; ++i) {
            evtAttr[i].group_id = -1;
        }
        newAttr->evtAttr = evtAttr;
    }
}

static bool FreeEvtAttr(struct PmuAttr *attr)
{
    if (attr->evtAttr == nullptr) {
        return SUCCESS;
    }
    bool flag = false;
    int notGroupId = -1;
    for (int i = 0; i < attr->numEvt; ++i) {
        if (attr->evtAttr[i].group_id != notGroupId ) {
            flag = true;
            break;
        }
    }

    // when the values of group_id are all -1, the applied memory is released.
    if (!flag) {
        delete[] attr->evtAttr;
        attr->evtAttr = nullptr;
    }

    return SUCCESS;
}

static void FreeEvtList(unsigned evtNum, char** evtList)
{
    if (!evtList) {
        return;
    }
    for (int i = 0; i < evtNum; i++) {
        if (evtList[i]) {
            delete[] evtList[i];
            evtList[i] = nullptr;
        }
    }
    delete[] evtList;
    evtList = nullptr;
}

static bool AppendChildEvents(char* evt, unordered_map<string, char*>& eventSplitMap)
{
    string strName(evt);
    auto findSlash = strName.find('/');
    string devName = strName.substr(0, findSlash);
    string evtName = strName.substr(devName.size() + 1, strName.size() - 1 - (devName.size() + 1));
    auto numEvt = uncoreEventPair.first;
    auto uncoreEventList = uncoreEventPair.second;
    if (uncoreEventList == nullptr) {
        New(LIBPERF_ERR_INVALID_EVENT, "Invalid uncore event list");
        return false;
    }
    bool invalidFlag = true;
    for (int i = 0; i < numEvt; ++i) {
        string uncoreEvent = uncoreEventList[i];
        auto findUncoreSlash = uncoreEvent.find('/');
        string uncoreDevName = uncoreEvent.substr(0, findUncoreSlash);
        string uncoreEvtName = uncoreEvent.substr(
                uncoreDevName.size() + 1, uncoreEvent.size() - 1 - (uncoreDevName.size() + 1));
        // Determine whether "hisi_sccl1_ddrc" is front part and "act_cmd" is the back part of
        // "hisi_sccl1_ddrc0/act_cmd/"
        if (strncmp(uncoreEvent.c_str(), devName.c_str(), devName.length()) == 0 && evtName == uncoreEvtName) {
            invalidFlag = false;
            eventSplitMap.emplace(uncoreEvent, evt);
        }
    }
    if (invalidFlag) {
        string err = "Invalid uncore event " + string(evt);
        New(LIBPERF_ERR_INVALID_EVENT, err);
        return false;
    }
    return true;
}

static bool SplitUncoreEvent(struct PmuAttr *attr, unordered_map<string, char*> &eventSplitMap)
{
    char** evtList = attr->evtList;
    unsigned size = attr->numEvt;
    int newSize = 0;
    unsigned numEvt;
    auto eventList = PmuEventList(UNCORE_EVENT, &numEvt);
    uncoreEventPair = make_pair(numEvt, eventList);
    for (int i = 0; i < size; ++i) {
        char* evt = evtList[i];
        char* slashPos = std::strchr(evt, '/');
        if (slashPos != nullptr && slashPos != evt) {
            char* prevChar = slashPos - 1;
            if (!std::isdigit(*prevChar)) {
                // 添加子事件
                if (!AppendChildEvents(evt, eventSplitMap)){
                    return false;
                }
                continue;
            }
        }
        eventSplitMap.emplace(evt, evt);
        newSize++;
    }
    return true;
}

static unsigned GenerateSplitList(unordered_map<string, char*>& eventSplitMap, vector<char*> &newEvtlist, const struct PmuAttr *attr, vector<struct EvtAttr> &newEvtAttrList)
{
    // according to the origin eventList, generate the new eventList and new eventAttrList
    for (int i = 0; i < attr->numEvt; ++i) {
        auto evt = attr->evtList[i];
        auto evtAttr = attr->evtAttr[i];

        // If the event is in the split list, it means that it is not a child event of the aggregate event
        // and direct add events to the new eventList and new eventAttrList
        if (eventSplitMap.find(evt) != eventSplitMap.end()) {
            newEvtlist.push_back(evt);
            newEvtAttrList.push_back(evtAttr);
        } else {
            // If the event is in the split list, it means that it is a child event of the aggregate event
            // and add the all child events of the aggregate event to the new eventList and new eventAttrList
            for (auto &aggregtaChildEvt : eventSplitMap) {
                if (strcmp(evt, aggregtaChildEvt.second) == 0) {
                    newEvtlist.push_back(const_cast<char*>(aggregtaChildEvt.first.c_str()));
                    newEvtAttrList.push_back(evtAttr);
                }
            }
        }
    }
    return newEvtlist.size();
}

static bool PdValid(const int &pd)
{
    return PmuList::GetInstance()->IsPdAlive(pd);
}

static void PmuTaskAttrFree(PmuTaskAttr *taskAttr)
{
    auto node = taskAttr;
    while (node) {
        delete[] node->pidList;
        delete[] node->cpuList;
        auto current = node;
        node = node->next;
        current->pmuEvt = nullptr;
        free(current);
    }
}

int PmuOpen(enum PmuTaskType collectType, struct PmuAttr *attr)
{
    SetWarn(SUCCESS);
    PmuAttr copiedAttr = *attr;
    pair<unsigned, char**> previousEventList = {0, nullptr};
    try {
        auto err = CheckAttr(collectType, attr);
        if (err != SUCCESS) {
            return -1;
        }
        CopyAttrData(&copiedAttr, attr, collectType);
        previousEventList = make_pair(copiedAttr.numEvt, copiedAttr.evtList);
        int pd = -1;
        unordered_map<string, char*> eventSplitMap;
        do {
            if (!SplitUncoreEvent(&copiedAttr, eventSplitMap)) {
                break;
            }
            vector<char *> newEvtlist;
            vector<struct EvtAttr> newEvtAttrList;
            auto numEvt = GenerateSplitList(eventSplitMap, newEvtlist, &copiedAttr, newEvtAttrList);
            FreeEvtAttr(&copiedAttr);
            copiedAttr.numEvt = numEvt;
            copiedAttr.evtList = newEvtlist.data();
            copiedAttr.evtAttr = newEvtAttrList.data();

            // Configure the attributes of the performance events to be monitored.
            auto pTaskAttr = AssignPmuTaskParam(collectType, &copiedAttr);
            if (pTaskAttr == nullptr) {
                break;
            }
            unique_ptr<PmuTaskAttr, void (*)(PmuTaskAttr *)> taskAttr(pTaskAttr, PmuTaskAttrFree);

            pd = PmuList::GetInstance()->NewPd();
            if (pd == -1) {
                New(LIBPERF_ERR_NO_AVAIL_PD);
                break;
            }

            PmuList::GetInstance()->SetSymbolMode(pd, attr->symbolMode);
            PmuList::GetInstance()->SetBranchSampleFilter(pd, attr->branchSampleFilter);
            err = PmuList::GetInstance()->Register(pd, taskAttr.get());
            if (err != SUCCESS) {
                PmuList::GetInstance()->Close(pd);
                pd = -1;
            }
            New(err);
        } while(false);

        if (pd == -1) {
            FreeEvtList(previousEventList.first, previousEventList.second);
            return -1;
        }
        // store eventList provided by user and the mapping relationship between the user eventList and the split
        // eventList into buff
        PmuList::GetInstance()->StoreSplitData(pd, previousEventList, eventSplitMap);
        return pd;
    } catch (std::bad_alloc&) {
        FreeEvtList(previousEventList.first, previousEventList.second);
        New(COMMON_ERR_NOMEM);
        return -1;
    } catch (exception& ex) {
        FreeEvtList(previousEventList.first, previousEventList.second);
        New(UNKNOWN_ERROR, ex.what());
        return -1;
    }
}

int PmuEnable(int pd)
{
    SetWarn(SUCCESS);
    return PmuCollectStart(pd);
}

int PmuDisable(int pd)
{
    SetWarn(SUCCESS);
    return PmuCollectPause(pd);
}

int PmuAppendData(struct PmuData *fromData, struct PmuData **toData)
{
    SetWarn(SUCCESS);
    int toLen = 0;
    PmuList::GetInstance()->AppendData(fromData, toData, toLen);
    return toLen;
}

static int DoCollectCounting(int pd, int milliseconds, unsigned collectInterval)
{
    constexpr int usecPerMilli = 1000;
    // Collect every <collectInterval> milliseconds,
    // and read data from ring buffer.
    int remained = milliseconds;
    bool unlimited = milliseconds == -1;
    PmuCollectStart(pd);
    while (remained > 0 || unlimited) {
        int interval = collectInterval;
        if (!unlimited && remained < collectInterval) {
            interval = remained;
        }
        usleep(usecPerMilli * interval);

        if (PmuList::GetInstance()->IsAllPidExit(pd)) {
            break;
        }

        pdMutex.tryLock(pd);
        if (!runningStatus[pd]) {
            pdMutex.releaseLock(pd);
            break;
        }
        pdMutex.releaseLock(pd);

        remained -= interval;
    }
    PmuCollectPause(pd);
    // Read data from ring buffer and store data to somewhere.
    auto err = PmuList::GetInstance()->ReadDataToBuffer(pd);
    if (err != SUCCESS) {
        New(err);
        return err;
    }
    return SUCCESS;
}

static int DoCollectNonCounting(int pd, int milliseconds, unsigned collectInterval)
{
    constexpr int usecPerMilli = 1000;
    // Collect every <collectInterval> milliseconds,
    // and read data from ring buffer.
    int remained = milliseconds;
    bool unlimited = milliseconds == -1;
    while (remained > 0 || unlimited) {
        int interval = collectInterval;
        if (!unlimited && remained < collectInterval) {
            interval = remained;
        }

        PmuCollectStart(pd);
        usleep(usecPerMilli * interval);
        PmuCollectPause(pd);

        // Read data from ring buffer and store data to somewhere.
        auto err = PmuList::GetInstance()->ReadDataToBuffer(pd);
        if (err != SUCCESS) {
            New(err);
            return err;
        }

        // Check if all processes exit.
        if (PmuList::GetInstance()->AllPmuDead(pd)) {
            break;
        }
        pdMutex.tryLock(pd);
        if (!runningStatus[pd]) {
            pdMutex.releaseLock(pd);
            break;
        }
        pdMutex.releaseLock(pd);

        remained -= interval;
    }
    return SUCCESS;
}

static int DoCollect(int pd, int milliseconds, unsigned interval)
{
    if (PmuList::GetInstance()->GetTaskType(pd) == COUNTING) {
        return DoCollectCounting(pd, milliseconds, interval);
    }
    return DoCollectNonCounting(pd, milliseconds, interval);
}

int PmuCollect(int pd, int milliseconds, unsigned interval)
{
    SetWarn(SUCCESS);
    int err = SUCCESS;
    string errMsg = "";
    try {
        if (!PdValid(pd)) {
            New(LIBPERF_ERR_INVALID_PD);
            return -1;
        }
        if (milliseconds != -1 && milliseconds < 0) {
            New(LIBPERF_ERR_INVALID_TIME);
            return -1;
        }
        if (interval < 100) {
            New(LIBPERF_ERR_INVALID_TIME);
            return -1;
        }

        pdMutex.tryLock(pd);
        runningStatus[pd] = true;
        pdMutex.releaseLock(pd);
        err = DoCollect(pd, milliseconds, interval);
    } catch (std::bad_alloc&) {
        err = COMMON_ERR_NOMEM;
    } catch (exception& ex) {
        err = UNKNOWN_ERROR;
        errMsg = ex.what();
    }
    pdMutex.tryLock(pd);
    runningStatus[pd] = false;
    pdMutex.releaseLock(pd);
    if (!errMsg.empty()) {
        New(err, errMsg);
    } else {
        New(err);
    }
    if (err != SUCCESS) {
        return -1;
    }
    return err;
}

static int InnerCollect(int *pds, unsigned len, size_t collectTime, bool &stop)
{
    for (unsigned i = 0; i < len; ++i) {
        PmuCollectStart(pds[i]);
    }
    usleep(collectTime);
    for (unsigned i = 0; i < len; ++i) {
        PmuCollectPause(pds[i]);
    }

    for (unsigned i = 0; i < len; ++i) {
        // Read data from ring buffer and store data to somewhere.
        auto err = PmuList::GetInstance()->ReadDataToBuffer(pds[i]);
        if (err != SUCCESS) {
            return err;
        }
    }

    // Check if all processes exit.
    bool allDead = true;
    for (unsigned i = 0; i < len; ++i) {
        auto taskType = PmuList::GetInstance()->GetTaskType(pds[i]);
        if (taskType == COUNTING) {
            allDead = false;
            break;
        }
        if (!PmuList::GetInstance()->AllPmuDead(pds[i])) {
            allDead = false;
            break;
        }
    }
    if (allDead) {
        stop = true;
        return SUCCESS;
    }

    // Check if all processes are stopped.
    bool allStopped = true;
    for (unsigned i = 0; i < len; ++i) {
        pdMutex.tryLock(pds[i]);
        if (runningStatus[pds[i]]) {
            allStopped = false;
            pdMutex.releaseLock(pds[i]);
            break;
        }
        pdMutex.releaseLock(pds[i]);
    }
    if (allStopped) {
        stop = true;
    }

    return SUCCESS;
}

int PmuCollectV(int *pds, unsigned len, int milliseconds)
{
    SetWarn(SUCCESS);
    constexpr int collectInterval = 100;
    constexpr int usecPerMilli = 1000;
    // Collect every <collectInterval> milliseconds,
    // and read data from ring buffer.
    int remained = milliseconds;
    bool unlimited = milliseconds == -1;
    for (int i = 0; i < len; ++i) {
        pdMutex.tryLock(pds[i]);
        runningStatus[pds[i]] = true;
        pdMutex.releaseLock(pds[i]);
    }
    while (remained > 0 || unlimited) {
        int interval = collectInterval;
        if (!unlimited && remained < collectInterval) {
            interval = remained;
        }
        bool stop = false;
        auto err = InnerCollect(pds, len, static_cast<size_t>(usecPerMilli * interval), stop);
        if (err != SUCCESS) {
            New(err);
            return err;
        }
        if (stop) {
            break;
        }
        remained -= interval;
    }
    return SUCCESS;
}

void PmuStop(int pd)
{
    SetWarn(SUCCESS);
    if (!PdValid(pd)) {
        New(LIBPERF_ERR_INVALID_PD);
        return;
    }

    pdMutex.tryLock(pd);
    runningStatus[pd] = false;
    pdMutex.releaseLock(pd);
    New(SUCCESS);
}

int PmuRead(int pd, struct PmuData** pmuData)
{
    SetWarn(SUCCESS);
    try {
        if (!PdValid(pd)) {
            New(LIBPERF_ERR_INVALID_PD);
            return -1;
        }

        New(SUCCESS);
        auto& retData = KUNPENG_PMU::PmuList::GetInstance()->Read(pd);
        if (!retData.empty()) {
            *pmuData = retData.data();
            return retData.size();
        } else {
            *pmuData = nullptr;
            return 0;
        }
    } catch (std::bad_alloc&) {
        New(COMMON_ERR_NOMEM);
        return -1;
    } catch (exception& ex) {
        New(UNKNOWN_ERROR, ex.what());
        return -1;
    }
}

void PmuClose(int pd)
{
    SetWarn(SUCCESS);
    if (!PdValid(pd)) {
        New(LIBPERF_ERR_INVALID_PD);
        return;
    }
    try {
        KUNPENG_PMU::PmuList::GetInstance()->Close(pd);
        PmuDeviceBdfListFree();
        New(SUCCESS);
    } catch (std::bad_alloc&) {
        New(COMMON_ERR_NOMEM);
    } catch (exception& ex) {
        New(UNKNOWN_ERROR, ex.what());
    }
}

static struct PmuEvt* GetPmuEvent(const char* pmuName, int collectType)
{
    return PfmGetPmuEvent(pmuName, collectType);
}

static void PrepareCpuList(PmuAttr *attr, PmuTaskAttr *taskParam, PmuEvt* pmuEvt)
{
    if (!pmuEvt->cpuMaskList.empty()) {
        taskParam->numCpu = pmuEvt->cpuMaskList.size();
        taskParam->cpuList = new int[pmuEvt->cpuMaskList.size()];
        for(int i = 0; i < pmuEvt->cpuMaskList.size(); i++) {
            taskParam->cpuList[i] = pmuEvt->cpuMaskList[i];
        }
    } else if (attr->cpuList == nullptr && attr->pidList != nullptr && pmuEvt->collectType == COUNTING) {
        // For counting with pid list for system wide, open fd with cpu -1 and specific pid.
        taskParam->numCpu = 1;
        taskParam->cpuList = new int[taskParam->numCpu];
        taskParam->cpuList[0] = -1;
    } else if (attr->cpuList == nullptr) {
        // For null cpulist, open fd with cpu 0,1,2...max_cpu
        const set<int> &onLineCpus = GetOnLineCpuIds();
        int cpuNum = onLineCpus.size();
        taskParam->numCpu = cpuNum;
        taskParam->cpuList = new int[cpuNum];
        int i = 0;
        for (const auto &cpuId : onLineCpus) {
            taskParam->cpuList[i] = cpuId;
            i++;
        }
    } else {
        taskParam->numCpu = attr->numCpu;
        taskParam->cpuList = new int[attr->numCpu];
        for (int i = 0; i < attr->numCpu; i++) {
            taskParam->cpuList[i] = attr->cpuList[i];
        }
    }
}

static struct PmuTaskAttr* AssignTaskParam(PmuTaskType collectType, PmuAttr *attr, const char* evtName, const int group_id)
{
    unique_ptr<PmuTaskAttr, void (*)(PmuTaskAttr*)> taskParam(CreateNode<struct PmuTaskAttr>(), PmuTaskAttrFree);
    /**
     * Assign pids to collect
     */
    taskParam->numPid = attr->numPid;
    taskParam->pidList = new int[attr->numPid];
    for (int i = 0; i < attr->numPid; i++) {
        taskParam->pidList[i] = attr->pidList[i];
    }
    PmuEvt* pmuEvt = nullptr;
    if (collectType == SPE_SAMPLING) {
        pmuEvt = PfmGetSpeEvent(attr->dataFilter, attr->evFilter, attr->minLatency, collectType);
        if (pmuEvt == nullptr) {
            New(LIBPERF_ERR_SPE_UNAVAIL);
            return nullptr;
        }
    } else {
        pmuEvt = GetPmuEvent(evtName, collectType);
        if (pmuEvt == nullptr) {
#ifdef IS_X86
            New(LIBPERF_ERR_INVALID_EVENT, "Invalid event: " + string(evtName) + ";x86 just supports core event and raw event");
#else
            New(LIBPERF_ERR_INVALID_EVENT, "Invalid event: " + string(evtName));
#endif
            return nullptr;
        }
    }
    /**
     * Assign cpus to collect
     */
    PrepareCpuList(attr, taskParam.get(), pmuEvt);

    taskParam->group_id = group_id;

    taskParam->pmuEvt = shared_ptr<PmuEvt>(pmuEvt, PmuEvtFree);
    taskParam->pmuEvt->useFreq = attr->useFreq;
    taskParam->pmuEvt->period = attr->period;
    taskParam->pmuEvt->excludeKernel = attr->excludeKernel;
    taskParam->pmuEvt->excludeUser = attr->excludeUser;
    taskParam->pmuEvt->callStack = attr->callStack;
    taskParam->pmuEvt->blockedSample = attr->blockedSample;
    taskParam->pmuEvt->includeNewFork = attr->includeNewFork;
    return taskParam.release();
}

struct PmuTaskAttr* AssignPmuTaskParam(enum PmuTaskType collectType, struct PmuAttr *attr)
{
    struct PmuTaskAttr* taskParam = nullptr;
    if (collectType == SPE_SAMPLING) {
        // evtList is nullptr, cannot loop over evtList.
        taskParam = AssignTaskParam(collectType, attr, nullptr, 0);
        return taskParam;
    }
    for (int i = 0; i < attr->numEvt; i++) {
        struct PmuTaskAttr* current = AssignTaskParam(collectType, attr, attr->evtList[i], attr->evtAttr[i].group_id);
        if (current == nullptr) {
            return nullptr;
        }
        AddTail(&taskParam, &current);
    }
    return taskParam;
}

void PmuDataFree(struct PmuData* pmuData)
{
    SetWarn(SUCCESS);
    PmuList::GetInstance()->FreeData(pmuData);
    New(SUCCESS);
}

static void DumpStack(ofstream &out, Stack *stack, int dumpDwf)
{
    if (stack->next) {
        out << endl;
    }
    while (stack) {
        if (stack->symbol) {
            auto symbol = stack->symbol;
            out << std::hex << symbol->addr << std::dec << " "
                << symbol->symbolName << " 0x" << std::hex
                << symbol->offset << std::dec << " "
                << symbol->module << " ";
            
            if (dumpDwf) {
                out << symbol->fileName << ":" << symbol->lineNum;
            }
            out << endl;
        }
        stack = stack->next;
    }
}

int PmuDumpData(struct PmuData *pmuData, unsigned len, char *filepath, int dumpDwf)
{
    SetWarn(SUCCESS);
    ofstream out(filepath, ios_base::app);
    if (!out.is_open()) {
        New(LIBPERF_ERR_PATH_INACCESSIBLE, "cannot access: " + string(filepath));
        return -1;
    }

    for (unsigned i = 0; i < len; ++i) {
        auto &data = pmuData[i];
        if (data.comm) {
            out << data.comm << " ";
        } else {
            out << "NULL ";
        }
        
        out << data.pid << " "
            << data.tid << " "
            << data.cpu << " "
            << data.period << " ";
        
        if (data.evt) {
            out << data.evt << " ";
        } else {
            out << "NULL ";
        }
        out << data.count << " ";
        
        if (data.ext) {
            out << std::hex << data.ext->va << " "
                << data.ext->pa << " "
                << data.ext->event << " " << std::dec;
        }
        
        if (data.stack) {
            DumpStack(out, data.stack, dumpDwf);
        }
        out << endl;
    }
    New(SUCCESS);
    return 0;
}

int PmuGetField(struct SampleRawData *rawData, const char *fieldName, void *value, uint32_t vSize) {
#ifdef IS_X86
    New(LIBPERF_ERR_INTERFACE_NOT_SUPPORT_X86);
    return LIBPERF_ERR_INTERFACE_NOT_SUPPORT_X86;
#else 
    if (rawData == nullptr) {
        New(LIBPERF_ERR_INVALID_FIELD_ARGS, "rawData cannot be nullptr.");
        return LIBPERF_ERR_INVALID_FIELD_ARGS;
    }
    return PointerPasser::ParsePointer(rawData->data, fieldName, value, vSize);
#endif
}

struct SampleRawField *PmuGetFieldExp(struct SampleRawData *rawData, const char *fieldName) {
#ifdef IS_X86
    New(LIBPERF_ERR_INTERFACE_NOT_SUPPORT_X86);
    return nullptr;
#else 
    if (rawData == nullptr) {
        New(LIBPERF_ERR_INVALID_FIELD_ARGS, "rawData cannot be nullptr.");
        return nullptr;
    }

    SampleRawField *rt = PointerPasser::GetSampleRawField(rawData->data, fieldName);
    if (rt) {
        New(SUCCESS);
    }
    return rt;
#endif
}

