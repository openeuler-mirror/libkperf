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
 * Author: Mr.Gan
 * Create: 2024-04-03
 * Description: declaration of class PerfCounter that inherits from PerfEvt and provides implementations
 * for initializing, reading, and mapping performance counter attributes in the KUNPENG_PMU namespace
 ******************************************************************************/
#ifndef PMU_COUNTER_H
#define PMU_COUNTER_H

#include <memory>
#include <stdexcept>
#include <linux/types.h>
#include "evt.h"
#include "pmu_event.h"

struct ReadFormat {
    __u64 value;
    __u64 timeEnabled;
    __u64 timeRunning;
    __u64 id;
};

namespace KUNPENG_PMU {
    class PerfCounter : public PerfEvt {
    public:
        using PerfEvt::PerfEvt;
        ~PerfCounter()
        {}
        int Init() override;
        int Read(std::vector<PmuData> &data, std::vector<PerfSampleIps> &sampleIps, std::vector<PmuDataExt*> &extPool) override;
        int MapPerfAttr() override;

    private:
	// Accumulated pmu count, time enabled and time running.
	__u64 count = 0;
	__u64 enabled = 0;
	__u64 running = 0;
    };
}  // namespace KUNPENG_PMU
#endif
