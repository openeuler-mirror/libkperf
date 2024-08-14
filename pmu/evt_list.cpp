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
 * Description: implementations for managing and interacting with performance events in the KUNPENG_PMU namespace
 ******************************************************************************/
#include <cstdio>
#include <unordered_set>
#include <fstream>
#include "cpu_map.h"
#include "pmu_event.h"
#include "pcerrc.h"
#include "log.h"
#include "common.h"
#include "evt_list.h"

using namespace std;

int KUNPENG_PMU::EvtList::CollectorDoTask(PerfEvtPtr collector, int task)
{
    switch (task) {
        case START:
            return collector->Start();
        case PAUSE:
            return collector->Pause();
        case DISABLE:
            return collector->Disable();
        case ENABLE:
            return collector->Enable();
        case RESET:
            return collector->Reset();
        case CLOSE: {
            auto ret = collector->Close();
            if (ret == SUCCESS) {
                fdList.erase(collector->GetFd());
            }
            return ret;
        }
        case INIT:
            return collector->Init();
        default:
            return UNKNOWN_ERROR;
    }
}

int KUNPENG_PMU::EvtList::CollectorXYArrayDoTask(std::vector<std::vector<PerfEvtPtr>>& xyArray, int task)
{
    std::unique_lock<std::mutex> lock(mutex);
    for (auto row: xyArray) {
        for (auto evt: row) {
            auto err = CollectorDoTask(evt, task);
            if (err != SUCCESS) {
                return err;
            }
        }
    }
    this->prevStat = this->evtStat;
    this->evtStat = task;
    return SUCCESS;
}

int KUNPENG_PMU::EvtList::Init()
{
    // Init process map.
    for (auto& proc: pidList) {
        if (proc->tid > 0) {
            procMap[proc->tid] = proc;
        }
    }
    bool hasHappenedErr = false;
    for (unsigned int row = 0; row < numCpu; row++) {
        std::vector<PerfEvtPtr> evtVec{};
        for (unsigned int col = 0; col < numPid; col++) {
            PerfEvtPtr perfEvt =
                    this->MapPmuAttr(this->cpuList[row]->coreId, this->pidList[col]->tid, this->pmuEvt.get());
            if (perfEvt == nullptr) {
                continue;
            }
            perfEvt->SetSymbolMode(symMode);
            auto err = perfEvt->Init();
            if (err != SUCCESS) {
                // The SPE and SAMPLING modes are not changed.
                if (!perfEvt->IsMainPid()) {
                    hasHappenedErr = true;
                    continue;
                }
                return err;
            }
            fdList.insert(perfEvt->GetFd());
            evtVec.emplace_back(perfEvt);
        }
        this->xyCounterArray.emplace_back(evtVec);
        // if an exception occurs due to exited threads, clear the exited fds.
        if (hasHappenedErr) {
            this->ClearExitFd();
        }
    }
    return SUCCESS;
}

int KUNPENG_PMU::EvtList::Start()
{
    return CollectorXYArrayDoTask(this->xyCounterArray, START);
}

int KUNPENG_PMU::EvtList::Enable()
{
    return CollectorXYArrayDoTask(this->xyCounterArray, ENABLE);
}

int KUNPENG_PMU::EvtList::Stop()
{
    return CollectorXYArrayDoTask(this->xyCounterArray, STOP);
}

int KUNPENG_PMU::EvtList::Close()
{
    auto ret = CollectorXYArrayDoTask(this->xyCounterArray, CLOSE);
    if (ret != SUCCESS) {
        return ret;
    }

    procMap.clear();
    return SUCCESS;
}

int KUNPENG_PMU::EvtList::Reset()
{
    return CollectorXYArrayDoTask(this->xyCounterArray, RESET);
}

void KUNPENG_PMU::EvtList::FillFields(
        const size_t& start, const size_t& end, CpuTopology* cpuTopo, ProcTopology* procTopo, vector<PmuData>& data)
{
    for (auto i = start; i < end; ++i) {
        data[i].cpuTopo = cpuTopo;
        data[i].evt = this->pmuEvt->name.c_str();
        if (data[i].comm == nullptr) {
            data[i].comm = procTopo->comm;
        }
        data[i].ts = this->ts;
    }
}

int KUNPENG_PMU::EvtList::Read(vector<PmuData>& data, std::vector<PerfSampleIps>& sampleIps,
                               std::vector<PmuDataExt*>& extPool)
{
    for (unsigned int row = 0; row < numCpu; row++) {
        for (unsigned int col = 0; col < numPid; col++) {
            int err = this->xyCounterArray[row][col]->BeginRead();
            if (err != SUCCESS) {
                return err;
            }
        }
    }

    struct PmuEvtData* head = nullptr;
    for (unsigned int row = 0; row < numCpu; row++) {
        auto cpuTopo = this->cpuList[row].get();
        for (unsigned int col = 0; col < numPid; col++) {
            auto cnt = data.size();
            int err = this->xyCounterArray[row][col]->Read(data, sampleIps, extPool);
            if (err != SUCCESS) {
                return err;
            }
            if (data.size() - cnt) {
                DBG_PRINT("evt: %s pid: %d cpu: %d samples num: %d\n", pmuEvt->name.c_str(), pidList[col]->pid,
                          cpuTopo->coreId, data.size() - cnt);
            }
            // Fill event name and cpu topology.
            FillFields(cnt, data.size(), cpuTopo, pidList[col].get(), data);
        }
    }

    for (unsigned int row = 0; row < numCpu; row++) {
        for (unsigned int col = 0; col < numPid; col++) {
            int err = this->xyCounterArray[row][col]->EndRead();
            if (err != SUCCESS) {
                return err;
            }
        }
    }

    this->ClearExitFd();
    return SUCCESS;
}

int KUNPENG_PMU::EvtList::Pause()
{
    return CollectorXYArrayDoTask(this->xyCounterArray, PAUSE);
}

std::shared_ptr<KUNPENG_PMU::PerfEvt> KUNPENG_PMU::EvtList::MapPmuAttr(int cpu, int pid, PmuEvt* pmuEvent)
{
    switch (pmuEvent->collectType) {
        case (COUNTING):
            return std::make_shared<KUNPENG_PMU::PerfCounter>(cpu, pid, pmuEvent, procMap);
        case (SAMPLING):
            return std::make_shared<KUNPENG_PMU::PerfSampler>(cpu, pid, pmuEvent, procMap);
        case (SPE_SAMPLING):
            return std::make_shared<KUNPENG_PMU::PerfSpe>(cpu, pid, pmuEvent, procMap);
        default:
            return nullptr;
    };
}

void KUNPENG_PMU::EvtList::AddNewProcess(pid_t pid)
{
    if (pid <= 0 || evtStat == CLOSE || evtStat == STOP) {
        return;
    }
    ProcTopology* topology = GetProcTopology(pid);
    if (topology == nullptr) {
        return;
    }
    std::unique_lock<std::mutex> lock(mutex);
    for (unsigned int row = 0; row < numCpu; row++) {
        this->pidList.emplace_back(shared_ptr<ProcTopology>(topology, FreeProcTopo));
        procMap[pid] = this->pidList.back();
        PerfEvtPtr perfEvt = this->MapPmuAttr(this->cpuList[row]->coreId, this->pidList.back()->tid,
                                              this->pmuEvt.get());
        if (perfEvt == nullptr) {
            return;
        }
        perfEvt->SetSymbolMode(symMode);
        auto err = perfEvt->Init();
        if (err != SUCCESS) {
            return;
        }
        fdList.insert(perfEvt->GetFd());
        numPid++;
        this->xyCounterArray[row].emplace_back(perfEvt);
        /**
         * If the current status is enable, start, read, other existing perfEvt may have been enabled and is counting,
         * so the new perfEvt must also be added to enable. If the current status is read, the status of all perfEvt
         * may be disable. At this time No need to collect counts.
         */
        if (evtStat == ENABLE || evtStat == START) {
            perfEvt->Enable();
        }
        if (evtStat == READ && prevStat != DISABLE) {
            perfEvt->Enable();
        }
    }
}

void KUNPENG_PMU::EvtList::ClearExitFd()
{
    if (this->pidList.size() == 1 && this->pidList[0]->tid == -1) {
        return;
    }

    if (this->pmuEvt->collectType != COUNTING) {
        return;
    }

    std::set<pid_t> exitPidVet;
    for (const auto& it: this->pidList) {
        std::string path = "/proc/" + std::to_string(it->tid);
        if (!ExistPath(path)) {
            exitPidVet.insert(it->tid);
        }
    }
    // erase the exit perfVet
    for (int row = 0; row < numCpu; row++) {
        auto& perfVet = xyCounterArray[row];
        for (auto it = perfVet.begin(); it != perfVet.end();) {
            int pid = it->get()->GetPid();
            if (exitPidVet.find(pid) != exitPidVet.end()) {
                it = perfVet.erase(it);
                this->fdList.erase(it->get()->GetFd());
                continue;
            }
            ++it;
        }
    }

    for (const auto& exitPid: exitPidVet) {
        for (auto it = this->pidList.begin(); it != this->pidList.end();) {
            if (it->get()->tid == exitPid) {
                it = this->pidList.erase(it);
                continue;
            }
            ++it;
        }
        procMap.erase(exitPid);
        numPid--;
    }
}