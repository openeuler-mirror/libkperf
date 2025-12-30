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
            if (GetParanoidVal() == -1) {
                pcerr::SetCustomErr(LIBPERF_ERR_NO_PERMISSION,
                    "Current user does not have the permission to collect the event."
                    "Please Switch to the root user and try again");
            } else {
                pcerr::SetCustomErr(LIBPERF_ERR_NO_PERMISSION,
                    "Current user does not have the permission to collect the event."
                    "Switch to the root user and run the 'echo -1 > /proc/sys/kernel/perf_event_paranoid'");
            }
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
        case LIBPERF_ERR_OPEN_INVALID_FILE:
            pcerr::SetCustomErr(err, "The kernel cannot find the corresponding file or directory when loading the event: " +perfEvt->GetEvtName());
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
        procMap[proc->tid] = proc; 
    }
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
            int groupFd = -1;
            if (groupEnable && evtleaderDefault) {
                // if leader initErr should skip;
                if (evtleaderDefault->xyCounterArray[row][col]->GetInitErr()) {
                    continue;
                }
                groupFd = evtleaderDefault->xyCounterArray[row][col]->GetFd();
            }
            int err = perfEvt->Init(groupEnable, groupFd, resetOutPutFd);
            if (err == LIBPERF_ERR_NO_PERMISSION && !this->pmuEvt->excludeKernel && !this->pmuEvt->excludeUser && GetParanoidVal() > 1) {
                perfEvt->SetNeedTryExcludeKernel(true);
                err = perfEvt->Init(groupEnable, groupFd, resetOutPutFd);
            }
            if (err != SUCCESS) {
                if (!perfEvt->IsMainPid()) {
                    // child pid init err
                    perfEvt->SetInitErr(true);
                    evtVec.emplace_back(perfEvt);
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
            data[i].evt = groupInfo->evtGroupChildList[i-start-1].lock()->GetPmuEvtName();
        } else {
            // For no group events or group leader.
            data[i].evt = this->pmuEvt->name.c_str();
        }
        data[i].groupId = this->groupId;
        if (data[i].comm == nullptr) {
            // If process has a fork call, it will generate a new pid and add a new comm.
            if (data[i].pid > 0 && procMap.find(data[i].pid) != procMap.end()) {
                data[i].comm = procMap[data[i].pid]->comm;   
            } else {
                data[i].comm = procTopo->comm;
            }
        }
        if (data[i].ts == 0) {
            data[i].ts = this->ts;
        }
    }
}

int KUNPENG_PMU::EvtListDefault::Read(EventData &eventData)
{

    std::unique_lock<std::mutex> lg(mutex);

    for (auto rowList : this->xyCounterArray) {
        for (auto evt : rowList) {
            int err = evt->BeginRead();
            if (err != SUCCESS) {
                return err;
            }
        }
    }

    struct PmuEvtData* head = nullptr;
    for (unsigned int row = 0; row < numCpu; row++) {
        auto cpuTopo = this->cpuList[row].get();
        auto rowList = this->xyCounterArray[row];
        for (auto evt : rowList) {
            auto cnt = eventData.data.size();
            int err = evt->Read(eventData);
            if (err != SUCCESS) {
                return err;
            }
            if (eventData.data.size() - cnt) {
                DBG_PRINT("evt: %s pid: %d cpu: %d samples num: %d\n", pmuEvt->name.c_str(), evt->GetPid(),
                          cpuTopo->coreId, eventData.data.size() - cnt);
            }
            // Fill event name and cpu topology.
            FillFields(cnt, eventData.data.size(), cpuTopo, procMap[evt->GetPid()].get(), eventData.data);
        }
    }

    // Due to the enable_on_exec being enabled, before launching, pmuopen will record its own comm,which needs to be replaced.
    if (this->pmuEvt->enableOnExec) {
        for (auto &pmuData : eventData.data) {
            if (procMap.find(pmuData.pid) == procMap.end()) {
                continue;
            }
            auto proc = procMap[pmuData.pid];
            if (proc->execComm != nullptr && pmuData.ts >= proc->execTs && pmuData.comm != proc->execComm) {
                pmuData.comm = proc->execComm;
            }
        }
    }

    for (auto rowList : this->xyCounterArray) {
        for (auto evt : rowList) {
            int err = evt->EndRead();
            if (err != SUCCESS) {
                return err;
            }
        }
    }

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
    if (evtStat == CLOSE || evtStat == STOP) {
        return;
    }
    std::unique_lock<std::mutex> lock(mutex);
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
            std::shared_ptr<EvtListDefault> evtLeaderDefault = std::dynamic_pointer_cast<EvtListDefault>(evtLeader);
            auto groupFd = evtLeaderDefault?evtLeaderDefault->xyCounterArray[row].back()->GetFd():-1;
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
    }
}


void KUNPENG_PMU::EvtListDefault::RemoveInitErr()
{
    for (auto &perfVet : xyCounterArray) {
        for (auto it = perfVet.begin(); it != perfVet.end();) {
            if (it->get()->GetInitErr()) {
                it = perfVet.erase(it);
                continue;
            }
            ++it;
        }
    }
}

void KUNPENG_PMU::EvtListDefault::ClearExitFd(std::set<int> noProcList)
{
    std::unique_lock<std::mutex> lock(mutex);
    // erase the exit perfVet
    for (auto& perfVet : xyCounterArray) {
          for (auto it = perfVet.begin(); it != perfVet.end();) {
            int pid = it->get()->GetPid();
            if (noProcList.find(pid) != noProcList.end()) {
                int fd = it->get()->GetFd();
                if (this->fdList.find(fd) != this->fdList.end()) {
                    this->fdList.erase(fd);
                    close(fd);
                }
                it = perfVet.erase(it);
                continue;
            }
            ++it;
        }
    }

    for (const auto& exitPid: noProcList) {
        procMap.erase(exitPid);
        numPid--;
    }
}

void KUNPENG_PMU::EvtListDefault::SetGroupInfo(const EventGroupInfo &grpInfo)
{
    this->groupInfo = unique_ptr<EventGroupInfo>(new EventGroupInfo(grpInfo));
}