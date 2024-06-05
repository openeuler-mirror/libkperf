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
#include "pmu.h"

using namespace pcerr;
using namespace KUNPENG_PMU;
using namespace std;

#define MAX_CPU_NUM sysconf(_SC_NPROCESSORS_ONLN)

static unordered_map<unsigned, bool> runningStatus;
static SafeHandler<unsigned> pdMutex;
static pair<unsigned, const char**> uncoreEventPair;

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
            string errMsg = "Invalid cpu id: " + to_string(cpuList[i]);
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
            string errMsg = "Invalid pid: " + to_string(pidList[i]);
            New(LIBPERF_ERR_INVALID_PIDLIST, errMsg);
            return LIBPERF_ERR_INVALID_PIDLIST;
        }
    }
    return SUCCESS;
}

static int CheckEvtList(unsigned numEvt, char** evtList)
{
    if (numEvt > 0 && evtList == nullptr) {
        New(LIBPERF_ERR_INVALID_EVTLIST);
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
    if (collectType < 0 || collectType >= MAX_TASK_TYPE) {
        New(LIBPERF_ERR_INVALID_TASK_TYPE);
        return LIBPERF_ERR_INVALID_TASK_TYPE;
    }
    if ((collectType == SAMPLING || collectType == COUNTING) && attr->evtList == nullptr) {
        New(LIBPERF_ERR_INVALID_EVTLIST);
        return LIBPERF_ERR_INVALID_EVTLIST;
    }
    if (InvalidSampleRate(collectType, attr)) {
        New(LIBPERF_ERR_INVALID_SAMPLE_RATE);
        return LIBPERF_ERR_INVALID_SAMPLE_RATE;
    }

    return SUCCESS;
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

static unsigned GenerateSplitList(unordered_map<string, char*>& eventSplitMap, vector<char*> &newEvtlist)
{
    // append child event
    for (auto& event : eventSplitMap) {
        newEvtlist.push_back(const_cast<char*>(event.first.c_str()));
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
    try {
        auto err = CheckAttr(collectType, attr);
        if (err != SUCCESS) {
            return -1;
        }
        unordered_map<string, char*> eventSplitMap;
        if (!SplitUncoreEvent(attr, eventSplitMap)) {
            return -1;
        }
        auto previousEventList = make_pair(attr->numEvt, attr->evtList);
        vector<char*> newEvtlist;
        attr->numEvt = GenerateSplitList(eventSplitMap, newEvtlist);
        attr->evtList = newEvtlist.data();

        auto pTaskAttr = AssignPmuTaskParam(collectType, attr);
        if (pTaskAttr == nullptr) {
            return -1;
        }
        unique_ptr<PmuTaskAttr, void (*)(PmuTaskAttr*)> taskAttr(pTaskAttr, PmuTaskAttrFree);

        auto pd = KUNPENG_PMU::PmuList::GetInstance()->NewPd();
        if (pd == -1) {
            New(LIBPERF_ERR_NO_AVAIL_PD);
            return -1;
        }

        KUNPENG_PMU::PmuList::GetInstance()->SetSymbolMode(pd, attr->symbolMode);
        err = KUNPENG_PMU::PmuList::GetInstance()->Register(pd, taskAttr.get());
        if (err != SUCCESS) {
            PmuList::GetInstance()->Close(pd);
            pd = -1;
        }
        // store eventList provided by user and the mapping relationship between the user eventList and the split
        // eventList into buff
        KUNPENG_PMU::PmuList::GetInstance()->StoreSplitData(pd, previousEventList, eventSplitMap);
        New(err);
        return pd;
    } catch (std::bad_alloc&) {
        New(COMMON_ERR_NOMEM);
        return -1;
    } catch (exception& ex) {
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
    if (pmuEvt->cpumask >= 0) {
        taskParam->numCpu = 1;
        taskParam->cpuList = new int[1];
        taskParam->cpuList[0] = pmuEvt->cpumask;
    } else if (attr->cpuList == nullptr && attr->pidList != nullptr && pmuEvt->collectType == COUNTING) {
        // For counting with pid list for system wide, open fd with cpu -1 and specific pid.
        taskParam->numCpu = 1;
        taskParam->cpuList = new int[taskParam->numCpu];
        taskParam->cpuList[0] = -1;
    } else if (attr->cpuList == nullptr) {
        // For null cpulist, open fd with cpu 0,1,2...max_cpu
        taskParam->numCpu = MAX_CPU_NUM;
        taskParam->cpuList = new int[taskParam->numCpu];
        for (int i = 0; i < taskParam->numCpu; i++) {
            taskParam->cpuList[i] = i;
        }
    } else {
        taskParam->numCpu = attr->numCpu;
        taskParam->cpuList = new int[attr->numCpu];
        for (int i = 0; i < attr->numCpu; i++) {
            taskParam->cpuList[i] = attr->cpuList[i];
        }
    }
}

static struct PmuTaskAttr* AssignTaskParam(PmuTaskType collectType, PmuAttr *attr, const char* name)
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
        pmuEvt = GetPmuEvent(name, collectType);
        if (pmuEvt == nullptr) {
            New(LIBPERF_ERR_INVALID_EVENT, "Invalid event: " + string(name));
            return nullptr;
        }
    }
    /**
     * Assign cpus to collect
     */
    PrepareCpuList(attr, taskParam.get(), pmuEvt);

    taskParam->pmuEvt = shared_ptr<PmuEvt>(pmuEvt, PmuEvtFree);
    taskParam->pmuEvt->useFreq = attr->useFreq;
    taskParam->pmuEvt->period = attr->period;
    taskParam->pmuEvt->excludeKernel = attr->excludeKernel;
    taskParam->pmuEvt->excludeUser = attr->excludeUser;
    taskParam->pmuEvt->callStack = attr->callStack;
    return taskParam.release();
}

struct PmuTaskAttr* AssignPmuTaskParam(enum PmuTaskType collectType, struct PmuAttr *attr)
{
    struct PmuTaskAttr* taskParam = nullptr;
    if (collectType == SPE_SAMPLING) {
        // evtList is nullptr, cannot loop over evtList.
        taskParam = AssignTaskParam(collectType, attr, nullptr);
        return taskParam;
    }
    for (int i = 0; i < attr->numEvt; i++) {
        struct PmuTaskAttr* current = AssignTaskParam(collectType, attr, attr->evtList[i]);
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