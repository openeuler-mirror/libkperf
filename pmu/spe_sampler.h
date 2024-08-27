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
 * Description: defines a class PerfSpe for handling System Performance Events (SPE) data collection
 * and processing in the KUNPENG_PMU namespace
 ******************************************************************************/
#ifndef PMU_SPE_H
#define PMU_SPE_H

#include <memory>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <climits>
#include <unistd.h>
#include "process_map.h"
#include "pmu_event.h"
#include "evt.h"
#include "symbol.h"
#include "spe.h"
#include "arm_spe_decoder.h"

namespace KUNPENG_PMU {
    class PerfSpe : public PerfEvt {
    public:
        using PerfEvt::PerfEvt;
        ~PerfSpe()
        {}

        int Init(const bool groupFlag, const int groupFd) override;
        int Read(std::vector<PmuData> &data, std::vector<PerfSampleIps> &sampleIps, std::vector<PmuDataExt*> &extPool) override;
        int MapPerfAttr(const bool groupFlag, const int groupFd) override;
        bool Mmap();

        int Disable() override;
        int Enable() override;
        int Close() override;

        int BeginRead() override;
        int EndRead() override;

        bool IsMainPid() const override
        {
            return true;
        };
    private:
        bool SpeExist(int cpu) const;
        void InsertSpeRecords(const int &tid, const std::vector<SpeRecord *> &speRecords, std::vector<PmuData> &data,
                              std::vector<PerfSampleIps> &sampleIps, std::vector<PmuDataExt*> &extPool);
        void UpdatePidList(const Spe &spe);
    };
}  // namespace KUNPENG_PMU
#endif
