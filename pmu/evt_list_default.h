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
 * Description: declaration of class EvtListDefault with functions for managing and interacting with a list
 * of performance events in the KUNPENG_PMU namespace
 ******************************************************************************/
#ifndef PMU_EVTLISTDEFAULT_H
#define PMU_EVTLISTDEFAULT_H
#include <iostream>
#include <unordered_map>
#include <vector>
#include <set>
#include <linux/types.h>
#include <mutex>
#include "cpu_map.h"
#include "perf_counter_default.h"
#include "pmu.h"
#include "process_map.h"
#include "sampler.h"
#include "spe_sampler.h"
#include "evt_list.h"

namespace KUNPENG_PMU {

class EvtListDefault : public EvtList {
public:
    EvtListDefault(const SymbolMode &symbolMode, std::vector<CpuPtr> &cpuList, std::vector<ProcPtr> &pidList,
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

    void SetGroupInfo(const EventGroupInfo &grpInfo) override;
    void AddNewProcess(pid_t pid, const bool groupEnable, const std::shared_ptr<EvtList> evtLeader) override;
    void ClearExitFd();
private:
    int CollectorXYArrayDoTask(std::vector<std::vector<PerfEvtPtr>>& xyArray, int task);
    void FillFields(size_t start, size_t end, CpuTopology* cpuTopo, ProcTopology* procTopo, std::vector<PmuData>& pmuData);
    void AdaptErrInfo(int err, PerfEvtPtr perfEvt);
    std::shared_ptr<PerfEvt> MapPmuAttr(int cpu, int pid, PmuEvt* pmuEvent);
    // Fixme: decouple group event with normal event, use different classes to implement Read and Init.
    std::unique_ptr<EventGroupInfo> groupInfo = nullptr;
};
}   // namespace KUNPENG_PMU
#endif
