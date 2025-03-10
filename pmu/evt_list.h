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
 * Description: declaration of class EvtList with functions for managing and interacting with a list
 * of performance events in the KUNPENG_PMU namespace
 ******************************************************************************/
#ifndef PMU_EVTLIST_H
#define PMU_EVTLIST_H
#include <iostream>
#include <unordered_map>
#include <vector>
#include <set>
#include <linux/types.h>
#include <mutex>
#include "cpu_map.h"
#include "perf_counter.h"
#include "pmu.h"
#include "process_map.h"
#include "sampler.h"
#include "spe_sampler.h"

namespace KUNPENG_PMU {
enum PmuTask {
    START = 0,
    PAUSE = 1,
    DISABLE = 2,
    ENABLE = 3,
    RESET = 4,
    OPEN = 5,
    CLOSE = 6,
    INIT = 7,
    READ = 8,
    STOP = 9,
};

enum class UncoreState {
    InitState = 0b01,
    OnlyUncore = 0b11,
    HasUncore = 0b10,
    OnlyOther = 0b01,
};

class EvtList {
public:
    using ProcPtr = std::shared_ptr<ProcTopology>;
    using CpuPtr = std::shared_ptr<CpuTopology>;
    EvtList(const SymbolMode &symbolMode, std::vector<CpuPtr> &cpuList, std::vector<ProcPtr> &pidList,
            std::shared_ptr<PmuEvt> pmuEvt, const int group_id)
        : symMode(symbolMode), cpuList(cpuList), pidList(pidList), pmuEvt(pmuEvt), group_id(group_id)
    {
        this->numCpu = this->cpuList.size();
        this->numPid = this->pidList.size();
        this->prevStat = OPEN;
        this->evtStat = OPEN;
    }
    int Init(const bool groupEnable, const std::shared_ptr<EvtList> evtLeader);
    int Pause();
    int Close();
    int Start();
    int Enable();
    int Stop();
    int Reset();
    int Read(std::vector<PmuData>& pmuData, std::vector<PerfSampleIps>& sampleIps, std::vector<PmuDataExt*>& extPool);

    void SetTimeStamp(const int64_t& timestamp)
    {
        this->ts = timestamp;
    }

    void SetBranchSampleFilter(const unsigned long& branchSampleFilter)
    {
        this->branchSampleFilter = branchSampleFilter;
    }

    std::set<int> GetFdList() const
    {
        return fdList;
    }

    int GetEvtType() const
    {
        return pmuEvt->collectType;
    }

    int GetPmuType() const
    {
        return pmuEvt->pmuType;
    }

    int GetGroupId() const
    {
        return group_id;
    }

    void AddNewProcess(pid_t pid, const bool groupEnable, const std::shared_ptr<EvtList> evtLeader);
    void ClearExitFd();
private:
    using PerfEvtPtr = std::shared_ptr<KUNPENG_PMU::PerfEvt>;

    void PredictRemainMemoryIsEnough();
    int CollectorDoTask(PerfEvtPtr collector, int task);
    int CollectorXYArrayDoTask(std::vector<std::vector<PerfEvtPtr>>& xyArray, int task);
    void FillFields(const size_t& start, const size_t& end, CpuTopology* cpuTopo, ProcTopology* procTopo,
                    std::vector<PmuData>& pmuData);

    std::vector<CpuPtr> cpuList;
    std::vector<ProcPtr> pidList;
    std::shared_ptr<PmuEvt> pmuEvt;
    int group_id; // event group id
    std::vector<std::vector<std::shared_ptr<PerfEvt>>> xyCounterArray;
    std::shared_ptr<PerfEvt> MapPmuAttr(int cpu, int pid, PmuEvt* pmuEvent);
    unsigned int numCpu = 0;
    unsigned int numPid = 0;
    std::set<int> fdList;
    int64_t ts = 0;
    std::unordered_map<pid_t, ProcPtr> procMap;
    SymbolMode symMode = NO_SYMBOL_RESOLVE;
    unsigned long branchSampleFilter = KPERF_NO_BRANCH_SAMPLE;
    int prevStat;
    int evtStat;
    std::mutex mutex;
    bool isMemoryEnough = true;
};

struct EventGroupInfo {
    // store event group leader info
    std::shared_ptr<EvtList> evtLeader;
    // store event group child events info
    std::vector<std::shared_ptr<EvtList>> evtGroupChildList;
    // store event group child events state flag info
    /* event group child state explain:
        * Enumeration variable uncoreState has four state, Initialization is the InitState;
        * scan the event List, if found the uncore event, the uncoreState is configured with the high bit set to 1;
        * if find the other event, the uncoreState is config the low bit set to 0.
    */
    enum class UncoreState uncoreState;  
};

// store event group id and event group info
using groupMapPtr = std::shared_ptr<std::unordered_map<int, EventGroupInfo>>;

}   // namespace KUNPENG_PMU
#endif
