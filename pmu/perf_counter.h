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
#include <stdint.h>
#include "evt.h"
#include "pmu_event.h"

#define REQUEST_USER_ACCESS 0x2

struct ReadFormat {
    __u64 value;
    __u64 timeEnabled;
    __u64 timeRunning;
    __u64 id;
};

namespace KUNPENG_PMU {
    static constexpr int COUNT_PAGE_SIZE = 4096;
    class PerfCounter : public PerfEvt {
    public:
        using PerfEvt::PerfEvt;
        ~PerfCounter()
        {}
        int Init(const bool groupEnable, const int groupFd, const int resetOutputFd) override;
        int Read(EventData &eventData) override;
        int MapPerfAttr(const bool groupEnable, const int groupFd) override;
        int Enable() override;
        int Disable() override;
        int Reset() override;
        int Close() override;

    private:
        enum class GroupStatus
        {
            NO_GROUP,
            GROUP_LEADER,
            GROUP_MEMBER
        };
        int Mmap();
        int MapPerfAttrUserAccess();
        int CountValueToData(const __u64 value, const __u64 timeEnabled,
                                const __u64 timeRunning, __u64 &accumCount, std::vector<PmuData> &data);
        int ReadSingleEvent(std::vector<PmuData> &data);
        int ReadGroupEvents(std::vector<PmuData> &data);

	    // Accumulated pmu count, time enabled and time running.
	    __u64 enabled = 0;
	    __u64 running = 0;
        // For group events, <accumCount> is the accum counts of all members.
        // For normal events, <accumCount> has only one element.
        std::vector<__u64> accumCount;
        int groupFd = 0;
        GroupStatus groupStatus = GroupStatus::NO_GROUP; 
        // reg index is stored in countMmap->base
        std::shared_ptr<PerfMmap> countMmap = nullptr;
        bool isCollect{false};
    };
}  // namespace KUNPENG_PMU
#endif
