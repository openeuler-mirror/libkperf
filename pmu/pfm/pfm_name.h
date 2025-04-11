/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
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
namespace COMMON {
extern const char* BRANCH_MISSES;
extern const char* BUS_CYCLES;
extern const char* CACHE_MISSES;
extern const char* CACHE_REFERENCES;
extern const char* REF_CYCLES;
extern const char* BRANCHES;
extern const char* BRANCH_INSTRUCTIONS;
extern const char* CPU_CYCLES;
extern const char* CYCLES;
extern const char* INSTRUCTIONS;
extern const char* STALLED_CYCLES_BACKEND;
extern const char* STALLED_CYCLES_FRONTEND;
extern const char* L1_DCACHE_LOAD_MISSES;
extern const char* L1_DCACHE_LOADS;
extern const char* L1_DCACHE_STORE_MISSES;
extern const char* L1_DCACHE_STORES;
extern const char* IDLE_CYCLES_BACKEND;
extern const char* L1_ICACHE_LOAD_MISSES;
extern const char* IDLE_CYCLES_FRONTEND;
extern const char* L1_ICACHE_LOADS;
extern const char* LLC_LOAD_MISSES;
extern const char* LLC_LOADS;
extern const char* LLC_STORE_MISSES;
extern const char* LLC_STORES;
extern const char* BRANCH_LOAD_MISSES;
extern const char* BRANCH_LOADS;
extern const char* DTLB_LOAD_MISSES;
extern const char* DTLB_LOADS;
extern const char* ITLB_LOAD_MISSES;
extern const char* ITLB_LOADS;
extern const char* NODE_LOAD_MISSES;
extern const char* NODE_LOADS;
extern const char* NODE_STORE_MISSES;
extern const char* NODE_STORES;
// Software event
extern const char* ALIGNMENT_FAULTS;
extern const char* BPF_OUTPUT;
extern const char* CONTEXT_SWITCHES;
extern const char* CS;
extern const char* CPU_CLOCK;
extern const char* CPU_MIGRATIONS;
extern const char* MIGRATIONS;
extern const char* DUMMY;
extern const char* EMULATION_FAULTS;
extern const char* MAJOR_FAULTS;
extern const char* MINOR_FAULTS;
extern const char* PAGE_FAULTS;
extern const char* FAULTS;
extern const char* TASK_CLOCK;
}
namespace HIP_A {
namespace CORE {
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
}  // namespace KUNPENG_PMU
#endif