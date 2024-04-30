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
#include "cpu_map.h"
#include "perf_counter.h"
#include "pmu.h"
#include "process_map.h"
#include "sampler.h"
#include "spe_sampler.h"

namespace KUNPENG_PMU {
class EvtList {
public:
    using ProcPtr = std::shared_ptr<ProcTopology>;
    using CpuPtr = std::shared_ptr<CpuTopology>;
    EvtList(const SymbolMode &symbolMode, std::vector<CpuPtr> &cpuList, std::vector<ProcPtr> &pidList, std::shared_ptr<PmuEvt> pmuEvt)
        : symMode(symbolMode), cpuList(cpuList), pidList(pidList), pmuEvt(pmuEvt)
    {
        this->numCpu = this->cpuList.size();
        this->numPid = this->pidList.size();
    }
    int Init();
    int Pause();
    int Close();
    int Start();
    int Enable();
    int Stop();
    int Reset();
    int Read(std::vector<PmuData> &pmuData, std::vector<PerfSampleIps> &sampleIps);

    void SetTimeStamp(const int64_t &timestamp)
    {
        this->ts = timestamp;
    }

    std::set<int> GetFdList() const
    {
        return fdList;
    }

    int GetEvtType() const
    {
        return pmuEvt->collectType;
    }

private:
    using PerfEvtPtr = std::shared_ptr<KUNPENG_PMU::PerfEvt>;

    int CollectorDoTask(PerfEvtPtr collector, int task);
    int CollectorXYArrayDoTask(int numCpu, int numPid, std::vector<std::vector<PerfEvtPtr>>& xyArray, int task);
    void FillFields(const size_t &start, const size_t &end, CpuTopology *cpuTopo, ProcTopology *procTopo,
        std::vector<PmuData> &pmuData);

    std::vector<CpuPtr> cpuList;
    std::vector<ProcPtr> pidList;
    std::shared_ptr<PmuEvt> pmuEvt;
    std::vector<std::vector<std::shared_ptr<PerfEvt>>> xyCounterArray;
    std::shared_ptr<PerfEvt> MapPmuAttr(int cpu, int pid, PmuEvt* pmuEvent);
    unsigned int numCpu = 0;
    unsigned int numPid = 0;
    std::set<int> fdList;
    int64_t ts = 0;
    std::unordered_map<pid_t, ProcPtr> procMap;
    SymbolMode symMode = NO_SYMBOL_RESOLVE;
};
}   // namespace KUNPENG_PMU
#endif
