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
 * Author: Mr.Ye
 * Create: 2024-04-03
 * Description: event type definitions, declarations, prototypes
 ******************************************************************************/
#ifndef PFM_EVENT_H
#define PFM_EVENT_H
#include <string>
#include <unordered_map>
#include <linux/types.h>
#include "cpu_map.h"
#include "pmu.h"

namespace KUNPENG_PMU {
    enum PMU_TYPE {
        RAW_TYPE,
        CORE_TYPE,
        UNCORE_TYPE,
        UNCORE_RAW_TYPE,
        TRACE_TYPE,
    };

    struct UncoreConfig {
        __u64 type;
        __u64 config;
        __u64 config1;
        __u64 config2;
        enum PMU_TYPE pmuType;
        int cpuMask;
        const std::string eventName;
        const std::string desc;
    };

    struct CoreConfig {
        __u64 type;
        __u64 config;
        const std::string eventName;
        const std::string desc;
    };
    using UNCORE_EVT_MAP =
            std::unordered_map<int, const std::unordered_map<std::string, KUNPENG_PMU::UncoreConfig>&>;
    using CORE_EVT_MAP =
            std::unordered_map<int, const std::unordered_map<std::string, KUNPENG_PMU::CoreConfig>&>;
}  // namespace KUNPENG_PMU
#endif
