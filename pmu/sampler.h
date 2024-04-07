/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * gala-gopher licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Gan
 * Create: 2024-04-03
 * Description: definition of class PerfSampler for sampling and processing performance data in
 * the KUNPENG_PMU namespace
 ******************************************************************************/
#ifndef PMU_SAMPLE_H
#define PMU_SAMPLE_H

#include <memory>
#include <stdexcept>
#include <vector>
#include <climits>
#include <unordered_map>
#include <linux/types.h>
#include <linux/perf_event.h>
#include "pmu_event.h"
#include "evt.h"
#include "symbol.h"

namespace KUNPENG_PMU {
    struct MmapParam {
        int prot;
        __u64 mask;
    };

    class PerfSampler : public PerfEvt {
    public:
        using PerfEvt::PerfEvt;
        ~PerfSampler()
        {}

        int Init() override;
        int Read(std::vector<PmuData> &data, std::vector<PerfSampleIps> &sampleIps) override;
        bool Close() override;

        int MapPerfAttr() override;

    private:
        int ReadInit();
        int Mmap();
        union PerfEvent *SampleReadEvent();
        void RawSampleProcess(struct PmuData *sampleHead, PerfSampleIps *ips, union KUNPENG_PMU::PerfEvent *event);
        void MmapSampleProcess();
        void Mmap2SampleProcess();
        void ForkSampleProcess();
        void ReadRingBuffer(std::vector<PmuData> &data, std::vector<PerfSampleIps> &sampleIps);
        void FillComm(const size_t &start, const size_t &end, std::vector<PmuData> &data);
        void UpdatePidInfo(const pid_t &pid, const int &tid);

        static int pages;
        std::shared_ptr<PerfMmap> sampleMmap = std::make_shared<PerfMmap>();
    };
}  // namespace KUNPENG_PMU
#endif
