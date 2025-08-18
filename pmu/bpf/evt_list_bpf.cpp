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
 * Description: implementations for managing and interacting with performance events of EvtListBpf in the KUNPENG_PMU namespace
 ******************************************************************************/
#include <cstdio>
#include <unordered_set>
#include <fstream>
#include "cpu_map.h"
#include "pmu_event.h"
#include "pcerrc.h"
#include "pcerr.h"
#include "log.h"
#include "common.h"
#include "evt_list_bpf.h"

using namespace std;

int KUNPENG_PMU::EvtListBpf::Init(const bool groupEnable, const std::shared_ptr<EvtList> evtLeader)
{
    // Init process map.
    for (auto& proc: pidList) {
        if (proc->tid > 0) {
            procMap[proc->tid] = proc;
        }
    }

    for (unsigned int cpu = 0; cpu < numCpu; cpu++) {
        PerfEvtPtr perfEvt =
                std::make_shared<KUNPENG_PMU::PerfCounterBpf>(this->cpuList[cpu]->coreId, -1, this->pmuEvt.get(), procMap);
        if (perfEvt == nullptr) {
            continue;
        }

        int err = 0;
        err = perfEvt->Init(groupEnable, -1, -1);
        if (err != SUCCESS) {
            return err;
        }
        this->cpuCounterArray.emplace_back(perfEvt);
    }

    for (unsigned int pid = 0; pid < numPid; pid++) {
        PerfEvtPtr perfEvt =
                std::make_shared<KUNPENG_PMU::PerfCounterBpf>(-1, this->pidList[pid]->tid, this->pmuEvt.get(), procMap);
        if (perfEvt == nullptr) {
            continue;
        }

        perfEvt->Init(groupEnable, -1, -1);  // init pid, ignore the result of perf_event_open
        this->pidCounterArray.emplace_back(perfEvt);
    }
    return SUCCESS;
}

int KUNPENG_PMU::EvtListBpf::CollectorTaskArrayDoTask(std::vector<PerfEvtPtr>& taskArray, int task)
{
    std::unique_lock<std::mutex> lock(mutex);
    for (auto evt: taskArray) {
        auto err = CollectorDoTask(evt, task);
        if (err != SUCCESS) {
            return err;
        }
    }
    this->prevStat = this->evtStat;
    this->evtStat = task;
    return SUCCESS;
}

int KUNPENG_PMU::EvtListBpf::Start()
{
    return CollectorTaskArrayDoTask(this->cpuCounterArray, START);
}

int KUNPENG_PMU::EvtListBpf::Enable()
{
    return CollectorTaskArrayDoTask(this->cpuCounterArray, ENABLE);
}

int KUNPENG_PMU::EvtListBpf::Stop()
{
    return CollectorTaskArrayDoTask(this->cpuCounterArray, STOP);
}

int KUNPENG_PMU::EvtListBpf::Reset()
{
    return CollectorTaskArrayDoTask(this->cpuCounterArray, RESET);
}

int KUNPENG_PMU::EvtListBpf::Pause()
{
    return CollectorTaskArrayDoTask(this->cpuCounterArray, PAUSE);
}

int KUNPENG_PMU::EvtListBpf::Close()
{
    auto ret = CollectorTaskArrayDoTask(this->cpuCounterArray, CLOSE);
    if (ret != SUCCESS) {
        return ret;
    }

    procMap.clear();
    return SUCCESS;
}

int KUNPENG_PMU::EvtListBpf::Read(EventData &eventData)
{
    std::unique_lock<std::mutex> lg(mutex);

    for (unsigned int pid = 0; pid < numPid; pid++) {
        int err = this->pidCounterArray[pid]->BeginRead();
        if (err != SUCCESS) {
            return err;
        }
    }

    struct PmuEvtData* head = nullptr;
    int row = 0;
    auto cpuTopo = this->cpuList[row].get();
    for (unsigned int pid = 0; pid < numPid; pid++) {
        auto cnt = eventData.data.size();
        int err = this->pidCounterArray[pid]->Read(eventData);
        if (err != SUCCESS) {
            return err;
        }
        if (eventData.data.size() - cnt) {
            DBG_PRINT("evt: %s pid: %d cpu: %d samples num: %d\n", pmuEvt->name.c_str(), pidList[pid]->pid,
                cpuTopo->coreId, eventData.data.size() - cnt);
        }
        // Fill event name and cpu topology.
        FillFields(cnt, eventData.data.size(), cpuTopo, pidList[pid].get(), eventData.data);
    }

    for (unsigned int pid = 0; pid < numPid; pid++) {
        int err = this->pidCounterArray[pid]->EndRead();
        if (err != SUCCESS) {
            return err;
        }
    }

    return SUCCESS;
}

void KUNPENG_PMU::EvtListBpf::FillFields(
        size_t start, size_t end, CpuTopology* cpuTopo, ProcTopology* procTopo, vector<PmuData>& data)
{
    for (auto i = start; i < end; ++i) {
        data[i].cpuTopo = cpuTopo;
        data[i].evt = this->pmuEvt->name.c_str();
        if (data[i].comm == nullptr) {
            data[i].comm = procTopo->comm;
        }
        if (data[i].ts == 0) {
            data[i].ts = this->ts;
        }
    }
}