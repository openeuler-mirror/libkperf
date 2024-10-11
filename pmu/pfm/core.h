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
 * Description: core event map declaration
 ******************************************************************************/
#ifndef CORE_H
#define CORE_H
#include <unordered_map>
#include "pfm_event.h"
#include "pfm_name.h"

namespace KUNPENG_PMU {
    extern const CORE_EVT_MAP CORE_EVENT_MAP;
}

struct PmuEvt* GetCoreEvent(const char* pmuName, int collectType);

std::string GetPmuDevicePath();

#endif

