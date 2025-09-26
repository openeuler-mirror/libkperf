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
 * Author: Mr.Jin
 * Create: 2024-04-03
 * Description: implements functions for handling System Performance Events (SPE) data collection
 * and processing for each CPU in the KUNPENG_PMU namespace
 ******************************************************************************/
#include <iostream>
#include <string.h>
#include <linux/perf_event.h>
#include <errno.h>
#include <sys/mman.h>
#include "linked_list.h"
#include "spe.h"
#include "pcerrc.h"
#include "spe_sampler.h"
#include "pcerr.h"

using namespace std;

namespace KUNPENG_PMU {

    static map<int, Spe> speSet;

    bool PerfSpe::Mmap()
    {
        return true;
    }

    int PerfSpe::MapPerfAttr(const bool groupEnable, const int groupFd)
    {
        return SUCCESS;
    }

    int PerfSpe::Init(const bool groupEnable, const int groupFd, const int resetOutputFd)
    {
        auto findSpe = speSet.find(this->cpu);
        if (findSpe != speSet.end()) {
            this->fd = findSpe->second.GetSpeFd();
            return SUCCESS;
        }

        findSpe = speSet.emplace(this->cpu, Spe(this->cpu, procMap, symMode)).first;
        auto err = findSpe->second.Open(evt);
        if (err != SUCCESS) {
            speSet.erase(this->cpu);
            return err;
        }
        this->fd = findSpe->second.GetSpeFd();
        return SUCCESS;
    }

    int PerfSpe::Read(EventData &eventData)
    {
        auto findSpe = speSet.find(this->cpu);
        if (findSpe == speSet.end()) {
            return LIBPERF_ERR_SPE_UNAVAIL;
        }

        if (findSpe->second.HaveRead()) {
            // Do not repeat reading data from the same core.
            return SUCCESS;
        }
        if (findSpe->second.Read() != SUCCESS) {
            return Perrorno();
        }

        // Fill pmu data from SPE collector.
        auto cnt = eventData.data.size();
        if (pid == -1) {
            // Loop over all tids in records and resolve module symbol for all pids.
            UpdatePidList(findSpe->second);
            for (auto records : findSpe->second.GetAllRecords()) {
                if (records.first == -1 || records.first == 0) {
                    continue;
                }
                // Insert each spe record for each tid.
                InsertSpeRecords(records.first, records.second, eventData.data, eventData.sampleIps, eventData.extPool);
            }
        } else {
            // Loop over all tids.
            for (auto &proc : procMap) {
                // Get all spe records for tid.
                const auto &records = findSpe->second.GetPidRecords(proc.first);
                InsertSpeRecords(proc.second->tid, records, eventData.data, eventData.sampleIps, eventData.extPool);
            }
        }

        return SUCCESS;
    }

    void PerfSpe::InsertSpeRecords(
            const int &tid, const std::vector<SpeRecord *> &speRecords, vector<PmuData> &data, vector<PerfSampleIps> &sampleIps, std::vector<PmuDataExt*> &extPool)
    {
        ProcTopology *procTopo = nullptr;
        auto findProc = procMap.find(tid);
        if (findProc == procMap.end()) {
            return;
        }
        procTopo = findProc->second.get();
        // Use large memory malloc instead of small mallocs to improve performance.
        PmuDataExt *extPtrs = new PmuDataExt[speRecords.size()];
        extPool.push_back(extPtrs);
        for (size_t i = 0; i < speRecords.size(); ++i) {
            auto rec = speRecords[i];
            data.emplace_back(PmuData{0});
            auto &current = data.back();
            current.pid = static_cast<pid_t>(procTopo->pid);
            current.tid = static_cast<int>(rec->tid);
            current.cpu = this->cpu;
            current.ext = &extPtrs[i];
            current.ext->event = rec->event;
            current.ext->va = rec->va;
            current.ext->pa = rec->pa;
            current.ext->lat = rec->lat;
            current.ts = static_cast<int64_t>(rec->timestamp);
            current.comm = procTopo ? procTopo->comm : nullptr;
            // Assign pc, which will be parsed to Symbol in PmuRead.
            sampleIps.emplace_back(PerfSampleIps());
            auto &ips = sampleIps.back();
            ips.ips.push_back(rec->pc);
        }
    }

    void PerfSpe::UpdatePidList(const Spe &spe)
    {
        for (auto records : spe.GetAllRecords()) {
            auto tid = records.first;
            if (procMap.find(tid) == procMap.end()) {
                auto procTopo = GetProcTopology(tid);
                if (procTopo != nullptr) {
                    procMap[tid] = shared_ptr<ProcTopology>(procTopo, FreeProcTopo);
                }
            }
        }
    }

    int PerfSpe::BeginRead()
    {
        return Disable();
    }

    int PerfSpe::EndRead()
    {
        return Enable();
    }

    int PerfSpe::Enable()
    {
        auto findSpe = speSet.find(this->cpu);
        if (findSpe == speSet.end()) {
            return LIBPERF_ERR_NOT_OPENED;
        }

        return findSpe->second.Enable();
    }

    int PerfSpe::Disable()
    {
        auto findSpe = speSet.find(this->cpu);
        if (findSpe == speSet.end()) {
            return LIBPERF_ERR_NOT_OPENED;
        }

        return findSpe->second.Disable();
    }

    int PerfSpe::Close()
    {
        auto findSpe = speSet.find(this->cpu);
        if (findSpe == speSet.end()) {
            return SUCCESS;
        }

        findSpe->second.Close();
        speSet.erase(this->cpu);
        return SUCCESS;
    }


}  // namespace KUNPENG_PMU
