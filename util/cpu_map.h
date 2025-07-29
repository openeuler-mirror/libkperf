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
 * Author: Mr.Wang
 * Create: 2024-04-03
 * Description: Get CPU topology and chip type.
 ******************************************************************************/
#ifndef CPU_MAP_H
#define CPU_MAP_H
#include <numa.h>
#include <set>
#include "pmu.h"

#define MAX_CPU_NUM sysconf(_SC_NPROCESSORS_CONF)

enum CHIP_TYPE {
    UNDEFINED_TYPE = 0,
    HIPA = 1,
    HIPB = 2,
    HIPC = 3,
    HIPF = 4,
    HIPE = 5,
    HIPX86 = 6,
};

struct CpuTopology* GetCpuTopology(int coreId);
CHIP_TYPE GetCpuType();
std::set<int> GetOnLineCpuIds();
unsigned* GetCoreList(int start);
int GetNumaCore(unsigned nodeId, unsigned** coreList);
#endif       
