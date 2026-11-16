/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Description: Internal PMU helpers shared with in-tree tools.
 ******************************************************************************/
#ifndef PMU_INTERNAL_H
#define PMU_INTERNAL_H

#include <vector>

#include "pmu.h"

namespace KUNPENG_PMU {

std::vector<unsigned long> GetSampleIpsInternal(PmuData* pmuData);

}

#endif
