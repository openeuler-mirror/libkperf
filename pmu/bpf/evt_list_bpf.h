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
 * Author: Wu
 * Create: 2025-08-10
 * Description: declaration of class EvtListBpf with functions for managing and interacting with a list
 * of performance events in the KUNPENG_PMU namespace
 ******************************************************************************/
#ifndef PMU_EVTLISTBPF_H
#define PMU_EVTLISTBPF_H
#include <iostream>
#include <unordered_map>
#include <vector>
#include <set>
#include <linux/types.h>
#include <mutex>
#include "cpu_map.h"
#include "perf_counter_bpf.h"
#include "perf_counter_default.h"
#include "pmu.h"
#include "process_map.h"
#include "sampler.h"
#include "spe_sampler.h"
#include "evt_list.h"

namespace KUNPENG_PMU {

class EvtListBpf : public EvtList {
public:
    EvtListBpf(const SymbolMode &symbolMode, std::vector<CpuPtr> &cpuList, std::vector<ProcPtr> &pidList,
            std::shared_ptr<PmuEvt> pmuEvt, const int groupId)
        : EvtList(symbolMode, cpuList, pidList, pmuEvt, groupId){}

    int Init(const bool groupEnable, const std::shared_ptr<EvtList> evtLeader);
    int Pause();
    int Close() override;
    int Start() override;
    int Enable() override;
    int Stop() override;
    int Reset() override;
    int Read(EventData &eventData) override;

    void SetGroupInfo(const EventGroupInfo &grpInfo) override {};
    void AddNewProcess(pid_t pid, const bool groupEnable, const std::shared_ptr<EvtList> evtLeader) override {};

private:
    std::vector<std::shared_ptr<PerfEvt>> cpuCounterArray;
    std::vector<std::shared_ptr<PerfEvt>> pidCounterArray;
    int CollectorTaskArrayDoTask(std::vector<PerfEvtPtr>& taskArray, int task);
    void FillFields(size_t start, size_t end, CpuTopology* cpuTopo, ProcTopology* procTopo, std::vector<PmuData>& pmuData);
};

}   // namespace KUNPENG_PMU
#endif
