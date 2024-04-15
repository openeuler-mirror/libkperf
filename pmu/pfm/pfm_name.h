/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * gala-gopher licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Ye
 * Create: 2024-04-03
 * Description: core event name declination
 ******************************************************************************/
#ifndef PFM_NAME_H
#define PFM_NAME_H
#include <string>
#include <vector>
#include <map>
#include <linux/types.h>

namespace KUNPENG_PMU {
namespace HIP_A {
namespace CORE {
extern const char* BRANCH_MISSES;
extern const char* BUS_CYCLES;
extern const char* CACHE_MISSES;
extern const char* CACHE_REFERENCES;
extern const char* CPU_CYCLES;
extern const char* CYCLES;
extern const char* INSTRUCTIONS;
extern const char* STALLED_CYCLES_BACKEND;
extern const char* STALLED_CYCLES_FRONTEND;
extern const char* L1_DCACHE_LOAD_MISSES;
extern const char* IDLE_CYCLES_BACKEND;
extern const char* L1_ICACHE_LOAD_MISSES;
extern const char* IDLE_CYCLES_FRONTEND;
extern const char* L1_ICACHE_LOADS;
extern const char* LLC_LOAD_MISSES;
extern const char* LLC_LOADS;
extern const char* BRANCH_LOAD_MISSES;
extern const char* BRANCH_LOADS;
extern const char* DTLB_LOAD_MISSES;
extern const char* DTLB_LOADS;
extern const char* ITLB_LOAD_MISSES;
extern const char* ITLB_LOADS;
extern const char* L1D_CACHE_RD;
extern const char* L1D_CACHE_WR;
extern const char* L1D_CACHE_REFILL_RD;
extern const char* L1D_CACHE_REFILL_WR;
extern const char* L1D_CACHE_WB_VICTIM;
extern const char* L1D_CACHE_WB_CLEAN;
extern const char* L1D_CACHE_INVAL;
extern const char* L1D_TLB_REFILL_RD;
extern const char* L1D_TLB_REFILL_WR;
extern const char* L1D_TLB_RD;
extern const char* L1D_TLB_WR;
extern const char* L2D_CACHE_RD;
extern const char* L2D_CACHE_WR;
extern const char* L2D_CACHE_REFILL_RD;
extern const char* L2D_CACHE_REFILL_WR;
extern const char* L2D_CACHE_WB_VICTIM;
extern const char* L2D_CACHE_WB_CLEAN;
extern const char* L2D_CACHE_INVAL;
extern const char* L1I_CACHE_PRF;
extern const char* L1I_CACHE_PRF_REFILL;
extern const char* IQ_IS_EMPTY;
extern const char* IF_IS_STALL;
extern const char* FETCH_BUBBLE;
extern const char* PRF_REQ;
extern const char* HIT_ON_PRF;
extern const char* EXE_STALL_CYCLE;
extern const char* MEM_STALL_ANYLOAD;
extern const char* MEM_STALL_L1MISS;
extern const char* MEM_STALL_L2MISS;
}  // namespace CORE

}  // namespace HIP_A

namespace HIP_B {
namespace CORE {
extern const char* BRANCH_MISSES;
extern const char* CACHE_MISSES;
extern const char* CACHE_REFERENCES;
extern const char* CPU_CYCLES;
extern const char* CYCLES;
extern const char* INSTRUCTIONS;
extern const char* STALLED_CYCLES_BACKEND;
extern const char* STALLED_CYCLES_FRONTEND;
extern const char* L1_DCACHE_LOAD_MISSES;
extern const char* IDLE_CYCLES_BACKEND;
extern const char* L1_ICACHE_LOAD_MISSES;
extern const char* IDLE_CYCLES_FRONTEND;
extern const char* L1_ICACHE_LOADS;
extern const char* LLC_LOAD_MISSES;
extern const char* LLC_LOADS;
extern const char* BRANCH_LOAD_MISSES;
extern const char* BRANCH_LOADS;
extern const char* DTLB_LOAD_MISSES;
extern const char* DTLB_LOADS;
extern const char* ITLB_LOAD_MISSES;
extern const char* ITLB_LOADS;
}  // namespace CORE

}  // namespace HIP_B

}  // namespace KUNPENG_PMU
#endif