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
 * Description: declaration of class PerfCounterBpf that inherits from PerfCounter.
 ******************************************************************************/
#ifndef PMU_COUNTER_BPF_H
#define PMU_COUNTER_BPF_H

#include <memory>
#include <stdexcept>
#include <linux/types.h>
#include "evt.h"
#include "pmu_event.h"
#include "perf_counter.h"

#define AT_FDCWD -100

struct BpfEvent {
    int bpfFd = -1;
    int eventId = -1;
    std::set<int> cpus;
    std::set<int> pids;
};

namespace KUNPENG_PMU {
    class PerfCounterBpf : public PerfCounter {
    public:
        using PerfCounter::PerfCounter;
        ~PerfCounterBpf()
        {}
        int Init(const bool groupEnable, const int groupFd, const int resetOutputFd) override;
        int Read(EventData &eventData) override;
        int MapPerfAttr(const bool groupEnable, const int groupFd) override;
        int Enable() override;
        int Disable() override;
        int Reset() override;
        int Close() override;

        int BeginRead();
        int EndRead();
    private:
        int InitBpfObj();
        int InitBpfCgroupObj();
        int InitPidForEvent();
        int ReadBpfProcess(std::vector<PmuData> &data);
        int ReadBpfCgroup(std::vector<PmuData> &data);
        std::map<std::string, int> cgroupIdxMap; // key: cgroup name, value: sequential number
    };
}  // namespace KUNPENG_PMU
#endif
