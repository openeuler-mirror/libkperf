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
#include <iostream>
#include <cstdio>
#include <unordered_set>
#include <fstream>
#include <linux/perf_event.h>
#include "cpu_map.h"
#include "linked_list.h"
#include "pmu_event.h"
#include "pcerrc.h"
#include "log.h"
#include "evt_list.h"

using namespace std;

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
    for (auto row : xyArray) {
        for (auto evt : row) {
            auto err = CollectorDoTask(evt, task);
            if (err != SUCCESS) {
                return err;
            }
        }
    }
    return SUCCESS;
}

int KUNPENG_PMU::EvtList::Init()
{
    // Init process map.
    for (auto &proc : pidList) {
        if (proc->tid > 0) {
            procMap[proc->tid] = proc;
        }
    }

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
                return err;
            }
            fdList.insert(perfEvt->GetFd());
            evtVec.emplace_back(perfEvt);
        }
        this->xyCounterArray.emplace_back(evtVec);
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
        const size_t &start, const size_t &end, CpuTopology *cpuTopo, ProcTopology *procTopo, vector<PmuData> &data)
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

int KUNPENG_PMU::EvtList::Read(vector<PmuData> &data, std::vector<PerfSampleIps> &sampleIps)
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
            int err = this->xyCounterArray[row][col]->Read(data, sampleIps);
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
