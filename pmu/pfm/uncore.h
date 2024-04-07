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
 * Author: Mr.Ye
 * Create: 2024-04-03
 * Description: uncore event map declaration
 ******************************************************************************/
#ifndef UNCORE_H
#define UNCORE_H
#include <unordered_map>
#include "pfm_event.h"
#include "pfm_name.h"

namespace KUNPENG_PMU {
    extern const KUNPENG_PMU::UNCORE_EVT_MAP L3C_EVENT_MAP;
    extern const KUNPENG_PMU::UNCORE_EVT_MAP HHA_EVENT_MAP;
    extern const KUNPENG_PMU::UNCORE_EVT_MAP DDRC_EVENT_MAP;
    extern const KUNPENG_PMU::UNCORE_EVT_MAP PCIE_EVENT_MAP;
}  // namespace KUNPENG_PMU

#endif