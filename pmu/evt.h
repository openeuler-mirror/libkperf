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
 * Description: declaration of class PerfEvt and related functions for managing performance events in
 * the KUNPENG_PMU namespace
 ******************************************************************************/
#ifndef PMU_EVT_H
#define PMU_EVT_H
#include <memory>
#include <vector>
#include <unordered_map>
#include <linux/types.h>
#include <linux/perf_event.h>
#include "common.h"
#include "symbol.h"
#include "pmu_event.h"

namespace KUNPENG_PMU {
class PerfEvt {
public:
    using ProcPtr = std::shared_ptr<ProcTopology>;
    using ProcMap = std::unordered_map<pid_t, ProcPtr>;

    PerfEvt(int cpu, int pid, struct PmuEvt* evt, ProcMap& procMap) : cpu(cpu), pid(pid), evt(evt), procMap(procMap) {}
    ~PerfEvt() {}
    virtual int Start();
    virtual int Pause();
    virtual int Disable();
    virtual int Enable();
    virtual int Reset();
    virtual int Close();
    virtual int BeginRead();
    virtual int EndRead();

    virtual int Init(const bool groupEnable, const int groupFd) = 0;

    virtual int Read(std::vector<PmuData> &data, std::vector<PerfSampleIps> &sampleIps, std::vector<PmuDataExt*> &extPool) = 0;

    virtual int MapPerfAttr(const bool groupEnable, const int groupFd) = 0;

    void SetSymbolMode(const SymbolMode &symMode)
    {
        this->symMode = symMode;
    }

    int GetFd() const
    {
        return fd;
    }

    int GetPid() const {
        return pid;
    }

    virtual bool IsMainPid() const
    {
        if (procMap.find(pid) != procMap.end()) {
            return this->procMap[pid]->isMain;
        }
        return true;
    }

    std::string GetEvtName() {
        if (this->evt) {
            return this->evt->name;
        }
        return "";
    }

protected:
    int fd;
    int cpu;
    pid_t pid;
    struct PmuEvt* evt;
    ProcMap &procMap;
    SymbolMode symMode = NO_SYMBOL_RESOLVE;
};
int PerfEventOpen(struct perf_event_attr* attr, pid_t pid, int cpu, int groupFd, unsigned long flags);
__u64 ReadOnce(__u64 *head);
}   // namespace KUNPENG_PMU
#endif
