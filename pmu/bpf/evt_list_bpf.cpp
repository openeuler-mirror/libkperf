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

        int err = perfEvt->Init(groupEnable, -1, -1);
        if (err != SUCCESS) {
            return err;
        }
        this->cpuCounterArray.emplace_back(perfEvt);
    }

    this->allPids.resize(pidList.size());
    for (size_t i = 0; i < pidList.size(); i++) {
        this->allPids[i] = pidList[i]->tid;
    }

    PerfEvtPtr perfEvt =std::make_shared<KUNPENG_PMU::PerfCounterBpf>(-1, -1, this->pmuEvt.get(), procMap);
    int err = std::dynamic_pointer_cast<KUNPENG_PMU::PerfCounterBpf>(perfEvt)->InitPidForEvent(allPids);
    if (err != SUCCESS) {
        return err;
    }

    this->pidCounterArray.emplace_back(perfEvt);
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
    auto perfEvt = this->pidCounterArray[0];
    int err = perfEvt->BeginRead();
    if (err != SUCCESS) {
        return err;
    }

    auto cpuTopo = this->cpuList[0].get();
    size_t oldSize = eventData.data.size();
    if (pmuEvt->cgroupName.empty()) {
        err = std::dynamic_pointer_cast<KUNPENG_PMU::PerfCounterBpf>(
                this->pidCounterArray[0])->ReadBpfProcess(this->allPids, eventData.data);
    } else {
        err = std::dynamic_pointer_cast<KUNPENG_PMU::PerfCounterBpf>(
                this->pidCounterArray[0])->ReadBpfCgroup(eventData.data);
    }
    if (err != SUCCESS) {
        return err;
    }

    const char* evtName = pmuEvt->name.c_str();
    uint64_t tsVal = this->ts;
    for (size_t i = oldSize; i < eventData.data.size(); i++) {
        auto& d = eventData.data[i];
        DBG_PRINT("evt: %s pid: %d cpu: %d\n", evtName, d.pid, d.cpu);

        d.cpuTopo = cpuTopo;
        d.evt = evtName;
        if (!d.comm) {
            auto it = procMap.find(d.tid);
            if (it != procMap.end()) {
                d.comm = it->second->comm;
            }
        }
        if (d.ts == 0) {
            d.ts = tsVal;
        }
    }

    err = perfEvt->EndRead();
    if (err != SUCCESS) {
        return err;
    }

    return SUCCESS;
}
