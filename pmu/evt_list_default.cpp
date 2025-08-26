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
#include "pcerr.h"
#include "log.h"
#include "common.h"
#include "evt_list_default.h"

using namespace std;

int KUNPENG_PMU::EvtListDefault::CollectorXYArrayDoTask(std::vector<std::vector<PerfEvtPtr>>& xyArray, int task)
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

void KUNPENG_PMU::EvtListDefault::AdaptErrInfo(int err, PerfEvtPtr perfEvt) 
{
    switch (err) {
        case LIBPERF_ERR_INVALID_EVENT:
            if (branchSampleFilter != KPERF_NO_BRANCH_SAMPLE) {
                pcerr::SetCustomErr(err,
                    "Invalid event:" + perfEvt->GetEvtName() +
                        ", PMU Hardware or event type doesn't support branch stack sampling");
            } else {
                pcerr::SetCustomErr(
                    err, "Invalid event:" + perfEvt->GetEvtName() + ", " + std::string{strerror(errno)});
            }
            break;
        case LIBPERF_ERR_NO_PERMISSION:
            pcerr::SetCustomErr(LIBPERF_ERR_NO_PERMISSION,
                "Current user does not have the permission to collect the event."
                "Switch to the root user and run the 'echo -1 > /proc/sys/kernel/perf_event_paranoid'");
            break;
        case LIBPERF_ERR_FAIL_MMAP:
            if (errno == ENOMEM) {
                pcerr::SetCustomErr(err,
                    "The number of mmap reaches the upper limit.Execute `echo {NUM} > /proc/sys/vm/max_map_count` to "
                    "set a bigger limit");
            } else {
                pcerr::SetCustomErr(err, std::string{strerror(errno)});
            }
            break;
        case LIBPERF_ERR_COUNTER_INDEX_IS_ZERO:
            pcerr::SetCustomErr(err, "There are too many open events. No registers are available.");
            break;
        case UNKNOWN_ERROR:
            pcerr::SetCustomErr(err, std::string{strerror(errno)});
            break;
        default:
            break;
    }
}

int KUNPENG_PMU::EvtListDefault::Init(const bool groupEnable, const std::shared_ptr<EvtList> evtLeader)
{
    // Init process map.
    for (auto& proc: pidList) {
        if (proc->tid >= 0) {
            procMap[proc->tid] = proc;
        }
    }
    bool hasHappenedErr = false;
    for (unsigned int row = 0; row < numCpu; row++) {
        int resetOutPutFd = -1;
        std::vector<PerfEvtPtr> evtVec{};
        for (unsigned int col = 0; col < numPid; col++) {
            PerfEvtPtr perfEvt =
                    this->MapPmuAttr(this->cpuList[row]->coreId, this->pidList[col]->tid, this->pmuEvt.get());
            if (perfEvt == nullptr) {
                continue;
            }
            if (col > 0 && !evtVec.empty()) {
                resetOutPutFd = evtVec[0]->GetFd();
            }
            perfEvt->SetSymbolMode(symMode);
            perfEvt->SetBranchSampleFilter(branchSampleFilter);
            auto evtleaderDefault = std::dynamic_pointer_cast<EvtListDefault>(evtLeader);
            int groupFd = groupEnable && evtleaderDefault ? evtleaderDefault->xyCounterArray[row][col]->GetFd():-1;
            int err = perfEvt->Init(groupEnable, groupFd, resetOutPutFd);
            if (err == LIBPERF_ERR_NO_PERMISSION && !this->pmuEvt->excludeKernel && !this->pmuEvt->excludeUser && GetParanoidVal() > 1) {
                perfEvt->SetNeedTryExcludeKernel(true);
                err = perfEvt->Init(groupEnable, groupFd, resetOutPutFd);
            }
            if (err != SUCCESS) {
                hasHappenedErr = true;
                if (!perfEvt->IsMainPid()) {
                    if (err == LIBPERF_ERR_NO_PROC) {
                        noProcList.emplace(this->pidList[col]->tid);
                    }
                    continue;
                }
                
                this->AdaptErrInfo(err, perfEvt);
                return err;
            }
            fdList.insert(perfEvt->GetFd());
            evtVec.emplace_back(perfEvt);
        }
        this->xyCounterArray.emplace_back(evtVec);
    }
    // if an exception occurs due to exited threads, clear the exited fds.
    if (hasHappenedErr) {
        this->ClearExitFd();
    }
    return SUCCESS;
}

int KUNPENG_PMU::EvtListDefault::Start()
{
    return CollectorXYArrayDoTask(this->xyCounterArray, START);
}

int KUNPENG_PMU::EvtListDefault::Enable()
{
    return CollectorXYArrayDoTask(this->xyCounterArray, ENABLE);
}

int KUNPENG_PMU::EvtListDefault::Stop()
{
    return CollectorXYArrayDoTask(this->xyCounterArray, STOP);
}

int KUNPENG_PMU::EvtListDefault::Close()
{
    auto ret = CollectorXYArrayDoTask(this->xyCounterArray, CLOSE);
    if (ret != SUCCESS) {
        return ret;
    }

    procMap.clear();
    return SUCCESS;
}

int KUNPENG_PMU::EvtListDefault::Reset()
{
    return CollectorXYArrayDoTask(this->xyCounterArray, RESET);
}

void KUNPENG_PMU::EvtListDefault::FillFields(
        size_t start, size_t end, CpuTopology* cpuTopo, ProcTopology* procTopo, vector<PmuData>& data)
{
    for (auto i = start; i < end; ++i) {
        data[i].cpuTopo = cpuTopo;
        if (groupInfo && pmuEvt->collectType == COUNTING && i - start > 0) {
            // For group events, PmuData are all read by event leader,
            // and then some PmuData elements should be related to group members.
            std::shared_ptr<EvtListDefault> child = std::dynamic_pointer_cast<EvtListDefault>(groupInfo->evtGroupChildList[i-start-1]);
            data[i].evt = child->pmuEvt->name.c_str();
        } else {
            // For no group events or group leader.
            data[i].evt = this->pmuEvt->name.c_str();
        }
        data[i].groupId = this->groupId;
        if (data[i].comm == nullptr) {
            data[i].comm = procTopo->comm;
        }
        if (data[i].ts == 0) {
            data[i].ts = this->ts;
        }
    }
}

int KUNPENG_PMU::EvtListDefault::Read(EventData &eventData)
{

    std::unique_lock<std::mutex> lg(mutex);

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
            auto cnt = eventData.data.size();
            int err = this->xyCounterArray[row][col]->Read(eventData);
            if (err != SUCCESS) {
                return err;
            }
            if (eventData.data.size() - cnt) {
                DBG_PRINT("evt: %s pid: %d cpu: %d samples num: %d\n", pmuEvt->name.c_str(), pidList[col]->pid,
                          cpuTopo->coreId, eventData.data.size() - cnt);
            }
            // Fill event name and cpu topology.
            FillFields(cnt, eventData.data.size(), cpuTopo, pidList[col].get(), eventData.data);
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

int KUNPENG_PMU::EvtListDefault::Pause()
{
    return CollectorXYArrayDoTask(this->xyCounterArray, PAUSE);
}

std::shared_ptr<KUNPENG_PMU::PerfEvt> KUNPENG_PMU::EvtListDefault::MapPmuAttr(int cpu, int pid, PmuEvt* pmuEvent)
{
    switch (pmuEvent->collectType) {
        case (COUNTING):
            return std::make_shared<KUNPENG_PMU::PerfCounterDefault>(cpu, pid, pmuEvent, procMap);
        case (SAMPLING):
            return std::make_shared<KUNPENG_PMU::PerfSampler>(cpu, pid, pmuEvent, procMap);
        case (SPE_SAMPLING):
            return std::make_shared<KUNPENG_PMU::PerfSpe>(cpu, pid, pmuEvent, procMap);
        default:
            return nullptr;
    };
}

void KUNPENG_PMU::EvtListDefault::AddNewProcess(pid_t pid, const bool groupEnable, const std::shared_ptr<EvtList> evtLeader)
{
    if (pid <= 0 || evtStat == CLOSE || evtStat == STOP) {
        return;
    }
    ProcTopology* topology = GetProcTopology(pid);
    if (topology == nullptr) {
        return;
    }
    std::unique_lock<std::mutex> lock(mutex);
    this->pidList.emplace_back(shared_ptr<ProcTopology>(topology, FreeProcTopo));
    bool hasInitErr = false;
    std::map<int, PerfEvtPtr> perfEvtMap;
    for (unsigned int row = 0; row < numCpu; row++) {
        PerfEvtPtr perfEvt = this->MapPmuAttr(this->cpuList[row]->coreId, this->pidList.back()->tid,
                                              this->pmuEvt.get());
        if (perfEvt == nullptr) {
            hasInitErr = true;
            break;
        }
        perfEvt->SetSymbolMode(symMode);
        perfEvt->SetBranchSampleFilter(branchSampleFilter);
        int err = 0;
        if (groupEnable) {
            int sz = this->pidList.size();
            std::shared_ptr<EvtListDefault> evtLeaderDefault = std::dynamic_pointer_cast<EvtListDefault>(evtLeader);
            auto groupFd = evtLeaderDefault?evtLeaderDefault->xyCounterArray[row][sz - 1]->GetFd():-1;
            err = perfEvt->Init(groupEnable, groupFd, -1);
        } else {
            err = perfEvt->Init(groupEnable, -1, -1);
        }
        if (err != SUCCESS) {
            hasInitErr = true;
            break;
        }
        perfEvtMap.emplace(row, perfEvt);
    }

    if (!hasInitErr) {
        procMap[pid] = this->pidList.back();
        numPid++;
        for (unsigned int row = 0; row < numCpu; row++) {
            auto perfEvt = perfEvtMap[row];
            fdList.insert(perfEvt->GetFd());
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
    } else {
        for (const auto& evtPtr : perfEvtMap) {
            close(evtPtr.second->GetFd());
        }
        this->pidList.erase(this->pidList.end() - 1);
    }
}

void KUNPENG_PMU::EvtListDefault::ClearExitFd()
{
    if (this->pidList.size() == 1 && this->pidList[0]->tid == -1) {
        return;
    }

    for (const auto& it: this->pidList) {
        if (it->isMain) {
            continue;
        }
        std::string path = "/proc/" + std::to_string(it->tid);
        if (!ExistPath(path)) {
            noProcList.insert(it->tid);
        }
    }

    if (noProcList.empty()) {
        return;
    }
    // erase the exit perfVet
    for (int row = 0; row < numCpu; row++) {
        auto& perfVet = xyCounterArray[row];
        for (auto it = perfVet.begin(); it != perfVet.end();) {
            int pid = it->get()->GetPid();
            if (noProcList.find(pid) != noProcList.end()) {
                int fd = it->get()->GetFd();
                this->fdList.erase(this->fdList.find(fd));
                close(fd);
                it = perfVet.erase(it);
                continue;
            }
            ++it;
        }
    }

    for (const auto& exitPid: noProcList) {
        for (auto it = this->pidList.begin(); it != this->pidList.end();) {
            if (it->get()->tid == exitPid) {
                this->unUsedPidList.push_back(it.operator*());
                it = this->pidList.erase(it);
                continue;
            }
            ++it;
        }
        procMap.erase(exitPid);
        numPid--;
    }

    noProcList.clear();
}

void KUNPENG_PMU::EvtListDefault::SetGroupInfo(const EventGroupInfo &grpInfo)
{
    this->groupInfo = unique_ptr<EventGroupInfo>(new EventGroupInfo(grpInfo));
}