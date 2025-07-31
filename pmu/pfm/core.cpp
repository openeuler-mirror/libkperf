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
 * Description: core event config
 ******************************************************************************/
#include <vector>
#include <unordered_map>
#include <fstream>
#include <cstring>
#include <dirent.h>
#include "pmu_event.h"
#include "core.h"
#include "common.h"

using namespace std;
using PMU_PAIR = std::pair<std::string, KUNPENG_PMU::CoreConfig>;
static CHIP_TYPE g_chipType = UNDEFINED_TYPE;
static string pmuDevice = "";

namespace SOFTWARE_EVENT {
    PMU_PAIR ALIGNMENT_FAULTS = {
            KUNPENG_PMU::COMMON::ALIGNMENT_FAULTS,
            {
                    PERF_TYPE_SOFTWARE,
                    PERF_COUNT_HW_STALLED_CYCLES_FRONTEND,
                    KUNPENG_PMU::COMMON::ALIGNMENT_FAULTS
            }
    };

    PMU_PAIR BPF_OUTPUT = {
            KUNPENG_PMU::COMMON::BPF_OUTPUT,
            {
                    PERF_TYPE_SOFTWARE,
                    0xa,
                    KUNPENG_PMU::COMMON::BPF_OUTPUT
            }
    };


    PMU_PAIR CONTEXT_SWITCHES = {
            KUNPENG_PMU::COMMON::CONTEXT_SWITCHES,
            {
                    PERF_TYPE_SOFTWARE,
                    PERF_COUNT_HW_CACHE_MISSES,
                    KUNPENG_PMU::COMMON::CONTEXT_SWITCHES
            }
    };

    PMU_PAIR CS = {
            KUNPENG_PMU::COMMON::CS,
            {
                    PERF_TYPE_SOFTWARE,
                    PERF_COUNT_HW_CACHE_MISSES,
                    KUNPENG_PMU::COMMON::CS
            }
    };

    PMU_PAIR CPU_CLOCK = {
            KUNPENG_PMU::COMMON::CPU_CLOCK,
            {
                    PERF_TYPE_SOFTWARE,
                    PERF_COUNT_HW_CPU_CYCLES,
                    KUNPENG_PMU::COMMON::CPU_CLOCK
            }
    };


    PMU_PAIR CPU_MIGRATIONS = {
            KUNPENG_PMU::COMMON::CPU_MIGRATIONS,
            {
                    PERF_TYPE_SOFTWARE,
                    PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
                    KUNPENG_PMU::COMMON::CPU_MIGRATIONS
            }
    };

    PMU_PAIR MIGRATIONS = {
            KUNPENG_PMU::COMMON::MIGRATIONS,
            {
                    PERF_TYPE_SOFTWARE,
                    PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
                    KUNPENG_PMU::COMMON::MIGRATIONS
            }
    };

    PMU_PAIR DUMMY = {
            KUNPENG_PMU::COMMON::DUMMY,
            {
                    PERF_TYPE_SOFTWARE,
                    PERF_COUNT_HW_REF_CPU_CYCLES,
                    KUNPENG_PMU::COMMON::DUMMY
            }
    };

    PMU_PAIR EMULATION_FAULTS = {
            KUNPENG_PMU::COMMON::EMULATION_FAULTS,
            {
                    PERF_TYPE_SOFTWARE,
                    PERF_COUNT_HW_STALLED_CYCLES_BACKEND,
                    KUNPENG_PMU::COMMON::EMULATION_FAULTS
            }
    };

    PMU_PAIR MAJOR_FAULTS = {
            KUNPENG_PMU::COMMON::MAJOR_FAULTS,
            {
                    PERF_TYPE_SOFTWARE,
                    PERF_COUNT_HW_BUS_CYCLES,
                    KUNPENG_PMU::COMMON::MAJOR_FAULTS
            }
    };

    PMU_PAIR MINOR_FAULTS = {
            KUNPENG_PMU::COMMON::MINOR_FAULTS,
            {
                    PERF_TYPE_SOFTWARE,
                    PERF_COUNT_HW_BRANCH_MISSES,
                    KUNPENG_PMU::COMMON::MINOR_FAULTS
            }
    };

    PMU_PAIR PAGE_FAULTS = {
            KUNPENG_PMU::COMMON::PAGE_FAULTS,
            {
                    PERF_TYPE_SOFTWARE,
                    PERF_COUNT_HW_CACHE_REFERENCES,
                    KUNPENG_PMU::COMMON::PAGE_FAULTS
            }
    };

    PMU_PAIR FAULTS = {
            KUNPENG_PMU::COMMON::FAULTS,
            {
                    PERF_TYPE_SOFTWARE,
                    PERF_COUNT_HW_CACHE_REFERENCES,
                    KUNPENG_PMU::COMMON::FAULTS
            }
    };


    PMU_PAIR TASK_CLOCK = {
            KUNPENG_PMU::COMMON::TASK_CLOCK,
            {
                    PERF_TYPE_SOFTWARE,
                    PERF_COUNT_HW_INSTRUCTIONS,
                    KUNPENG_PMU::COMMON::TASK_CLOCK
            }
    };
} // namespace software event

namespace HARDWARE_EVENT {
    PMU_PAIR BRANCH_MISSES = {
            KUNPENG_PMU::COMMON::BRANCH_MISSES,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_BRANCH_MISSES,
                    KUNPENG_PMU::COMMON::BRANCH_MISSES
            }
    };

    PMU_PAIR CACHE_MISSES = {
            KUNPENG_PMU::COMMON::CACHE_MISSES,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_CACHE_MISSES,
                    KUNPENG_PMU::COMMON::CACHE_MISSES
            }
    };

    PMU_PAIR CACHE_REFERENCES = {
            KUNPENG_PMU::COMMON::CACHE_REFERENCES,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_CACHE_REFERENCES,
                    KUNPENG_PMU::COMMON::CACHE_REFERENCES
            }
    };

    PMU_PAIR CPU_CYCLES = {
            KUNPENG_PMU::COMMON::CPU_CYCLES,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_CPU_CYCLES,
                    KUNPENG_PMU::COMMON::CPU_CYCLES
            }
    };

    PMU_PAIR CYCLES = {
            KUNPENG_PMU::COMMON::CYCLES,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_CPU_CYCLES,
                    KUNPENG_PMU::COMMON::CYCLES
            }
    };

    PMU_PAIR INSTRUCTIONS = {
            KUNPENG_PMU::COMMON::INSTRUCTIONS,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_INSTRUCTIONS,
                    KUNPENG_PMU::COMMON::INSTRUCTIONS
            }
    };

    PMU_PAIR STALLED_CYCLES_BACKEND = {
            KUNPENG_PMU::COMMON::STALLED_CYCLES_BACKEND,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_STALLED_CYCLES_BACKEND,
                    KUNPENG_PMU::COMMON::STALLED_CYCLES_BACKEND
            }
    };

    PMU_PAIR STALLED_CYCLES_FRONTED = {
            KUNPENG_PMU::COMMON::STALLED_CYCLES_FRONTEND,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_STALLED_CYCLES_FRONTEND,
                    KUNPENG_PMU::COMMON::STALLED_CYCLES_FRONTEND
            }
    };

    PMU_PAIR IDLE_CYCLES_BACKEND = {
            KUNPENG_PMU::COMMON::IDLE_CYCLES_BACKEND,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_STALLED_CYCLES_BACKEND,
                    KUNPENG_PMU::COMMON::IDLE_CYCLES_BACKEND
            }
    };

    PMU_PAIR IDLE_CYCLES_FRONTED = {
            KUNPENG_PMU::COMMON::IDLE_CYCLES_FRONTEND,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_STALLED_CYCLES_FRONTEND,
                    KUNPENG_PMU::COMMON::IDLE_CYCLES_FRONTEND
            }
    };

    PMU_PAIR BUS_CYCLES = {
            KUNPENG_PMU::COMMON::BUS_CYCLES,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_BUS_CYCLES,
                    KUNPENG_PMU::COMMON::BUS_CYCLES
            }
    };

    PMU_PAIR REF_CYCLES = {
            KUNPENG_PMU::COMMON::REF_CYCLES,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_REF_CPU_CYCLES,
                    KUNPENG_PMU::COMMON::REF_CYCLES
            }
    };

    PMU_PAIR BRANCHES = {
            KUNPENG_PMU::COMMON::BRANCHES,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
                    KUNPENG_PMU::COMMON::BRANCHES
            }
    };

    PMU_PAIR BRANCH_INSTRUCTIONS = {
            KUNPENG_PMU::COMMON::BRANCH_INSTRUCTIONS,
            {
                    PERF_TYPE_HARDWARE,
                    PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
                    KUNPENG_PMU::COMMON::BRANCH_INSTRUCTIONS
            }
    };
} // namespace hardware event

namespace HW_CACHE_EVENT {
    PMU_PAIR L1_DCACHE_LOAD_MISSES =  {
            KUNPENG_PMU::COMMON::L1_DCACHE_LOAD_MISSES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x10000,
                    KUNPENG_PMU::COMMON::L1_DCACHE_LOAD_MISSES
            }
    };

    PMU_PAIR L1_DCACHE_LOADS = {
            KUNPENG_PMU::COMMON::L1_DCACHE_LOADS,
            {
                    PERF_TYPE_HW_CACHE,
                    0x0,
                    KUNPENG_PMU::COMMON::L1_DCACHE_LOADS
            }
    };

    PMU_PAIR L1_ICACHE_LOAD_MISSES = {
            KUNPENG_PMU::COMMON::L1_ICACHE_LOAD_MISSES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x10001,
                    KUNPENG_PMU::COMMON::L1_ICACHE_LOAD_MISSES
            }
    };

    PMU_PAIR L1_ICACHE_LOADS =  {
            KUNPENG_PMU::COMMON::L1_ICACHE_LOADS,
            {
                    PERF_TYPE_HW_CACHE,
                    0x1,
                    KUNPENG_PMU::COMMON::L1_ICACHE_LOADS
            }
    };

    PMU_PAIR LLC_LOAD_MISSES = {
            KUNPENG_PMU::COMMON::LLC_LOAD_MISSES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x10002,
                    KUNPENG_PMU::COMMON::LLC_LOAD_MISSES
            }
    };

    PMU_PAIR LLC_LOADS =   {
            KUNPENG_PMU::COMMON::LLC_LOADS,
            {
                    PERF_TYPE_HW_CACHE,
                    0x2,
                    KUNPENG_PMU::COMMON::LLC_LOADS
            }
    };

    
    PMU_PAIR LLC_STORE_MISSES =  {
            KUNPENG_PMU::COMMON::LLC_STORE_MISSES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x10102,
                    KUNPENG_PMU::COMMON::LLC_STORE_MISSES
            }
    };

    PMU_PAIR LLC_STORES =  {
            KUNPENG_PMU::COMMON::LLC_STORES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x102,
                    KUNPENG_PMU::COMMON::LLC_STORES
            }
    };


    PMU_PAIR BRANCH_LOAD_MISSES =  {
            KUNPENG_PMU::COMMON::BRANCH_LOAD_MISSES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x10005,
                    KUNPENG_PMU::COMMON::BRANCH_LOAD_MISSES
            }
    };

    PMU_PAIR BRANCH_LOADS = {
            KUNPENG_PMU::COMMON::BRANCH_LOADS,
            {
                    PERF_TYPE_HW_CACHE,
                    0x5,
                    KUNPENG_PMU::COMMON::BRANCH_LOADS
            }
    };

    PMU_PAIR DTLB_LOAD_MISSES = {
            KUNPENG_PMU::COMMON::DTLB_LOAD_MISSES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x10003,
                    KUNPENG_PMU::COMMON::DTLB_LOAD_MISSES
            }
    };

    PMU_PAIR DTLB_LOADS = {
            KUNPENG_PMU::COMMON::DTLB_LOADS,
            {
                    PERF_TYPE_HW_CACHE,
                    0x3,
                    KUNPENG_PMU::COMMON::DTLB_LOADS
            }
    };

     PMU_PAIR DTLB_STORE_MISSES = {
            KUNPENG_PMU::COMMON::DTLB_STORE_MISSES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x10103,
                    KUNPENG_PMU::COMMON::DTLB_STORE_MISSES
            }
    };

    PMU_PAIR DTLB_STORES = {
            KUNPENG_PMU::COMMON::DTLB_STORES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x103,
                    KUNPENG_PMU::COMMON::DTLB_STORES
            }
    };

    PMU_PAIR ITLB_LOAD_MISSES = {
            KUNPENG_PMU::COMMON::ITLB_LOAD_MISSES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x10004,
                    KUNPENG_PMU::COMMON::ITLB_LOAD_MISSES
            }
    };

    PMU_PAIR ITLB_LOADS =  {
            KUNPENG_PMU::COMMON::ITLB_LOADS,
            {
                    PERF_TYPE_HW_CACHE,
                    0x4,
                    KUNPENG_PMU::COMMON::ITLB_LOADS
            }
    };

    PMU_PAIR NODE_LOAD_MISSES =  {
            KUNPENG_PMU::COMMON::NODE_LOAD_MISSES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x10006,
                    KUNPENG_PMU::COMMON::NODE_LOAD_MISSES
            }
    };

    PMU_PAIR NODE_LOADS =  {
            KUNPENG_PMU::COMMON::NODE_LOADS,
            {
                    PERF_TYPE_HW_CACHE,
                    0x6,
                    KUNPENG_PMU::COMMON::NODE_LOADS
            }
    };

    PMU_PAIR NODE_STORE_MISSES =  {
            KUNPENG_PMU::COMMON::NODE_STORE_MISSES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x10106,
                    KUNPENG_PMU::COMMON::NODE_STORE_MISSES
            }
    };

    PMU_PAIR NODE_STORES =  {
            KUNPENG_PMU::COMMON::NODE_STORES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x106,
                    KUNPENG_PMU::COMMON::NODE_STORES
            }
    };

    PMU_PAIR L1_DCACHE_STORE_MISSES = {
            KUNPENG_PMU::COMMON::L1_DCACHE_STORE_MISSES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x10100,
                    KUNPENG_PMU::COMMON::L1_DCACHE_STORE_MISSES
            }
    };

    PMU_PAIR L1_DCACHE_STORES = {
            KUNPENG_PMU::COMMON::L1_DCACHE_STORES,
            {
                    PERF_TYPE_HW_CACHE,
                    0x100,
                    KUNPENG_PMU::COMMON::L1_DCACHE_STORES
            }
    };
} // namespace hardware cache event

namespace RAW_EVENT {
    PMU_PAIR L1D_CACHE_RD = {
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_RD,
            {
                    PERF_TYPE_RAW,
                    0x40,
                    KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_RD
            }
    };

    PMU_PAIR L1D_CACHE_WR =  {
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WR,
            {
                    PERF_TYPE_RAW,
                    0x41,
                    KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WR
            }
    };

    PMU_PAIR L1D_CACHE_REFILL_RD = {
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_REFILL_RD,
            {
                    PERF_TYPE_RAW,
                    0x42,
                    KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_REFILL_RD
            }
    };

    PMU_PAIR L1D_CACHE_REFILL_WR =   {
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_REFILL_WR,
            {
                    PERF_TYPE_RAW,
                    0x43,
                    KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_REFILL_WR
            }
    };

    PMU_PAIR L1D_CACHE_WB_VICTIM = {
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WB_VICTIM,
            {
                    PERF_TYPE_RAW,
                    0x46,
                    KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WB_VICTIM
            }
    };

    PMU_PAIR L1D_CACHE_WB_CLEAN = {
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WB_CLEAN,
            {
                    PERF_TYPE_RAW,
                    0x47,
                    KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WB_CLEAN
            }
    };

    PMU_PAIR L1D_CACHE_INVAL = {
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_INVAL,
            {
                    PERF_TYPE_RAW,
                    0x48,
                    KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_INVAL
            }
    };

    PMU_PAIR L1D_TLB_REFILL_RD = {
            KUNPENG_PMU::HIP_A::CORE::L1D_TLB_REFILL_RD,
            {
                    PERF_TYPE_RAW,
                    0x4c,
                    KUNPENG_PMU::HIP_A::CORE::L1D_TLB_REFILL_RD
            }
    };

    PMU_PAIR L1D_TLB_REFILL_WR = {
            KUNPENG_PMU::HIP_A::CORE::L1D_TLB_REFILL_WR,
            {
                    PERF_TYPE_RAW,
                    0x4d,
                    KUNPENG_PMU::HIP_A::CORE::L1D_TLB_REFILL_WR
            }
    };

    PMU_PAIR L1D_TLB_RD =  {
            KUNPENG_PMU::HIP_A::CORE::L1D_TLB_RD,
            {
                    PERF_TYPE_RAW,
                    0x4e,
                    KUNPENG_PMU::HIP_A::CORE::L1D_TLB_RD
            }
    };

    PMU_PAIR L1D_TLB_WR = {
            KUNPENG_PMU::HIP_A::CORE::L1D_TLB_WR,
            {
                    PERF_TYPE_RAW,
                    0x4f,
                    KUNPENG_PMU::HIP_A::CORE::L1D_TLB_WR
            }
    };

    PMU_PAIR L2D_CACHE_RD = {
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_RD,
            {
                    PERF_TYPE_RAW,
                    0x50,
                    KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_RD
            }
    };

    PMU_PAIR L2D_CACHE_WR =  {
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WR,
            {
                    PERF_TYPE_RAW,
                    0x51,
                    KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WR
            }
    };

    PMU_PAIR L2D_CACHE_REFILL_RD = {
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_REFILL_RD,
            {
                    PERF_TYPE_RAW,
                    0x52,
                    KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_REFILL_RD
            }
    };

    PMU_PAIR L2D_CACHE_REFILL_WR = {
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_REFILL_WR,
            {
                    PERF_TYPE_RAW,
                    0x53,
                    KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_REFILL_WR
            }
    };

    PMU_PAIR L2D_CACHE_WB_VICTIM = {
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WB_VICTIM,
            {
                    PERF_TYPE_RAW,
                    0x56,
                    KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WB_VICTIM
            }
    };

    PMU_PAIR L2D_CACHE_WB_CLEAN = {
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WB_CLEAN,
            {
                    PERF_TYPE_RAW,
                    0x57,
                    KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WB_CLEAN
            }
    };

    PMU_PAIR L2D_CACHE_INVAL = {
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_INVAL,
            {
                    PERF_TYPE_RAW,
                    0x58,
                    KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_INVAL
            }
    };

    PMU_PAIR L1I_CACHE_PRF = {
            KUNPENG_PMU::HIP_A::CORE::L1I_CACHE_PRF,
            {
                    PERF_TYPE_RAW,
                    0x102e,
                    KUNPENG_PMU::HIP_A::CORE::L1I_CACHE_PRF
            }
    };

    PMU_PAIR L1I_CACHE_PRF_REFILL = {
            KUNPENG_PMU::HIP_A::CORE::L1I_CACHE_PRF_REFILL,
            {
                    PERF_TYPE_RAW,
                    0x102f,
                    KUNPENG_PMU::HIP_A::CORE::L1I_CACHE_PRF_REFILL
            }
    };

    PMU_PAIR IQ_IS_EMPTY = {
            KUNPENG_PMU::HIP_A::CORE::IQ_IS_EMPTY,
            {
                    PERF_TYPE_RAW,
                    0x1043,
                    KUNPENG_PMU::HIP_A::CORE::IQ_IS_EMPTY
            }
    };

    PMU_PAIR IF_IS_STALL =  {
            KUNPENG_PMU::HIP_A::CORE::IF_IS_STALL,
            {
                    PERF_TYPE_RAW,
                    0x1044,
                    KUNPENG_PMU::HIP_A::CORE::IF_IS_STALL
            }
    };

    PMU_PAIR FETCH_BUBBLE = {
            KUNPENG_PMU::HIP_A::CORE::FETCH_BUBBLE,
            {
                    PERF_TYPE_RAW,
                    0x2014,
                    KUNPENG_PMU::HIP_A::CORE::FETCH_BUBBLE
            }
    };

    PMU_PAIR PRF_REQ = {
            KUNPENG_PMU::HIP_A::CORE::PRF_REQ,
            {
                    PERF_TYPE_RAW,
                    0x6013,
                    KUNPENG_PMU::HIP_A::CORE::PRF_REQ
            }
    };

    PMU_PAIR HIT_ON_PRF = {
            KUNPENG_PMU::HIP_A::CORE::HIT_ON_PRF,
            {
                    PERF_TYPE_RAW,
                    0x6014,
                    KUNPENG_PMU::HIP_A::CORE::HIT_ON_PRF
            }
    };

    PMU_PAIR EXE_STALL_CYCLE = {
            KUNPENG_PMU::HIP_A::CORE::EXE_STALL_CYCLE,
            {
                    PERF_TYPE_RAW,
                    0x7001,
                    KUNPENG_PMU::HIP_A::CORE::EXE_STALL_CYCLE
            }
    };

    PMU_PAIR MEM_STALL_ANYLOAD =  {
            KUNPENG_PMU::HIP_A::CORE::MEM_STALL_ANYLOAD,
            {
                    PERF_TYPE_RAW,
                    0x7004,
                    KUNPENG_PMU::HIP_A::CORE::MEM_STALL_ANYLOAD
            }
    };

    PMU_PAIR MEM_STALL_L1MISS = {
            KUNPENG_PMU::HIP_A::CORE::MEM_STALL_L1MISS,
            {
                    PERF_TYPE_RAW,
                    0x7006,
                    KUNPENG_PMU::HIP_A::CORE::MEM_STALL_L1MISS
            }
    };

    PMU_PAIR MEM_STALL_L2MISS =  {
            KUNPENG_PMU::HIP_A::CORE::MEM_STALL_L2MISS,
            {
                    PERF_TYPE_RAW,
                    0x7007,
                    KUNPENG_PMU::HIP_A::CORE::MEM_STALL_L2MISS
            }
    };
}

const std::unordered_map<std::string, KUNPENG_PMU::CoreConfig> HIP_A_CORE_PMU_MAP{
        HARDWARE_EVENT::BRANCH_MISSES,
        HARDWARE_EVENT::BUS_CYCLES,
        HARDWARE_EVENT::CACHE_MISSES,
        HARDWARE_EVENT::CACHE_REFERENCES,
        HARDWARE_EVENT::CPU_CYCLES,
        HARDWARE_EVENT::CYCLES,
        HARDWARE_EVENT::INSTRUCTIONS,
        HARDWARE_EVENT::STALLED_CYCLES_BACKEND,
        HARDWARE_EVENT::STALLED_CYCLES_FRONTED,
        HARDWARE_EVENT::IDLE_CYCLES_BACKEND,
        HARDWARE_EVENT::IDLE_CYCLES_FRONTED,
        HW_CACHE_EVENT::L1_DCACHE_LOAD_MISSES,
        HW_CACHE_EVENT::L1_DCACHE_LOADS,
        HW_CACHE_EVENT::L1_DCACHE_STORE_MISSES,
        HW_CACHE_EVENT::L1_DCACHE_STORES,
        HW_CACHE_EVENT::L1_ICACHE_LOAD_MISSES,
        HW_CACHE_EVENT::L1_ICACHE_LOADS,
        HW_CACHE_EVENT::LLC_LOAD_MISSES,
        HW_CACHE_EVENT::LLC_LOADS,
        HW_CACHE_EVENT::BRANCH_LOAD_MISSES,
        HW_CACHE_EVENT::BRANCH_LOADS,
        HW_CACHE_EVENT::DTLB_LOAD_MISSES,
        HW_CACHE_EVENT::DTLB_LOADS,
        HW_CACHE_EVENT::ITLB_LOAD_MISSES,
        HW_CACHE_EVENT::ITLB_LOADS,
        RAW_EVENT::L1D_CACHE_RD,
        RAW_EVENT::L1D_CACHE_WR,
        RAW_EVENT::L1D_CACHE_REFILL_RD,
        RAW_EVENT::L1D_CACHE_REFILL_WR,
        RAW_EVENT::L1D_CACHE_WB_VICTIM,
        RAW_EVENT::L1D_CACHE_WB_CLEAN,
        RAW_EVENT::L1D_CACHE_INVAL,
        RAW_EVENT::L1D_TLB_REFILL_RD,
        RAW_EVENT::L1D_TLB_REFILL_WR,
        RAW_EVENT::L1D_TLB_RD,
        RAW_EVENT::L1D_TLB_WR,
        RAW_EVENT::L2D_CACHE_RD,
        RAW_EVENT::L2D_CACHE_WR,
        RAW_EVENT::L2D_CACHE_REFILL_RD,
        RAW_EVENT::L2D_CACHE_REFILL_WR,
        RAW_EVENT::L2D_CACHE_WB_VICTIM,
        RAW_EVENT::L2D_CACHE_WB_CLEAN,
        RAW_EVENT::L2D_CACHE_INVAL,
        RAW_EVENT::L1I_CACHE_PRF,
        RAW_EVENT::L1I_CACHE_PRF_REFILL,
        RAW_EVENT::IQ_IS_EMPTY,
        RAW_EVENT::IF_IS_STALL,
        RAW_EVENT::FETCH_BUBBLE,
        RAW_EVENT::PRF_REQ,
        RAW_EVENT::HIT_ON_PRF,
        RAW_EVENT::EXE_STALL_CYCLE,
        RAW_EVENT::MEM_STALL_ANYLOAD,
        RAW_EVENT::MEM_STALL_L1MISS,
        RAW_EVENT::MEM_STALL_L2MISS,
        SOFTWARE_EVENT::ALIGNMENT_FAULTS,
        SOFTWARE_EVENT::BPF_OUTPUT,
        SOFTWARE_EVENT::CONTEXT_SWITCHES,
        SOFTWARE_EVENT::CS,
        SOFTWARE_EVENT::CPU_CLOCK,
        SOFTWARE_EVENT::CPU_MIGRATIONS,
        SOFTWARE_EVENT::MIGRATIONS,
        SOFTWARE_EVENT::DUMMY,
        SOFTWARE_EVENT::EMULATION_FAULTS,
        SOFTWARE_EVENT::MAJOR_FAULTS,
        SOFTWARE_EVENT::MINOR_FAULTS,
        SOFTWARE_EVENT::PAGE_FAULTS,
        SOFTWARE_EVENT::FAULTS,
        SOFTWARE_EVENT::TASK_CLOCK,
};

const std::unordered_map<std::string, KUNPENG_PMU::CoreConfig> HIP_B_CORE_PMU_MAP{
        HARDWARE_EVENT::BRANCH_MISSES,
        HARDWARE_EVENT::CACHE_MISSES,
        HARDWARE_EVENT::CACHE_REFERENCES,
        HARDWARE_EVENT::CPU_CYCLES,
        HARDWARE_EVENT::CYCLES,
        HARDWARE_EVENT::INSTRUCTIONS,
        HARDWARE_EVENT::STALLED_CYCLES_BACKEND,
        HARDWARE_EVENT::STALLED_CYCLES_FRONTED,
        HARDWARE_EVENT::IDLE_CYCLES_BACKEND,
        HARDWARE_EVENT::IDLE_CYCLES_FRONTED,
        HW_CACHE_EVENT::L1_DCACHE_LOAD_MISSES,
        HW_CACHE_EVENT::L1_ICACHE_LOADS,
        HW_CACHE_EVENT::LLC_LOAD_MISSES,
        HW_CACHE_EVENT::LLC_LOADS,
        HW_CACHE_EVENT::BRANCH_LOAD_MISSES,
        HW_CACHE_EVENT::BRANCH_LOADS,
        HW_CACHE_EVENT::DTLB_LOAD_MISSES,
        HW_CACHE_EVENT::DTLB_LOADS,
        HW_CACHE_EVENT::ITLB_LOAD_MISSES,
        HW_CACHE_EVENT::ITLB_LOADS,
        SOFTWARE_EVENT::ALIGNMENT_FAULTS,
        SOFTWARE_EVENT::BPF_OUTPUT,
        SOFTWARE_EVENT::CONTEXT_SWITCHES,
        SOFTWARE_EVENT::CS,
        SOFTWARE_EVENT::CPU_CLOCK,
        SOFTWARE_EVENT::CPU_MIGRATIONS,
        SOFTWARE_EVENT::MIGRATIONS,
        SOFTWARE_EVENT::DUMMY,
        SOFTWARE_EVENT::EMULATION_FAULTS,
        SOFTWARE_EVENT::MAJOR_FAULTS,
        SOFTWARE_EVENT::MINOR_FAULTS,
        SOFTWARE_EVENT::PAGE_FAULTS,
        SOFTWARE_EVENT::FAULTS,
        SOFTWARE_EVENT::TASK_CLOCK,
};

const std::unordered_map<std::string, KUNPENG_PMU::CoreConfig> HIP_C_CORE_PMU_MAP{
        HARDWARE_EVENT::BRANCH_MISSES,
        HARDWARE_EVENT::CACHE_MISSES,
        HARDWARE_EVENT::CACHE_REFERENCES,
        HARDWARE_EVENT::CPU_CYCLES,
        HARDWARE_EVENT::CYCLES,
        HARDWARE_EVENT::INSTRUCTIONS,
        HARDWARE_EVENT::STALLED_CYCLES_BACKEND,
        HARDWARE_EVENT::STALLED_CYCLES_FRONTED,
        HARDWARE_EVENT::IDLE_CYCLES_BACKEND,
        HARDWARE_EVENT::IDLE_CYCLES_FRONTED,
        HW_CACHE_EVENT::L1_DCACHE_LOAD_MISSES,
        HW_CACHE_EVENT::L1_ICACHE_LOAD_MISSES,
        HW_CACHE_EVENT::L1_ICACHE_LOADS,
        HW_CACHE_EVENT::LLC_LOAD_MISSES,
        HW_CACHE_EVENT::LLC_LOADS,
        HW_CACHE_EVENT::BRANCH_LOAD_MISSES,
        HW_CACHE_EVENT::BRANCH_LOADS,
        HW_CACHE_EVENT::DTLB_LOAD_MISSES,
        HW_CACHE_EVENT::DTLB_LOADS,
        HW_CACHE_EVENT::ITLB_LOAD_MISSES,
        HW_CACHE_EVENT::ITLB_LOADS,
        SOFTWARE_EVENT::ALIGNMENT_FAULTS,
        SOFTWARE_EVENT::BPF_OUTPUT,
        SOFTWARE_EVENT::CONTEXT_SWITCHES,
        SOFTWARE_EVENT::CS,
        SOFTWARE_EVENT::CPU_CLOCK,
        SOFTWARE_EVENT::CPU_MIGRATIONS,
        SOFTWARE_EVENT::MIGRATIONS,
        SOFTWARE_EVENT::DUMMY,
        SOFTWARE_EVENT::EMULATION_FAULTS,
        SOFTWARE_EVENT::MAJOR_FAULTS,
        SOFTWARE_EVENT::MINOR_FAULTS,
        SOFTWARE_EVENT::PAGE_FAULTS,
        SOFTWARE_EVENT::FAULTS,
        SOFTWARE_EVENT::TASK_CLOCK,
};

const std::unordered_map<std::string, KUNPENG_PMU::CoreConfig> HIP_F_CORE_PMU_MAP{
        HARDWARE_EVENT::BRANCH_MISSES,
        HARDWARE_EVENT::CACHE_MISSES,
        HARDWARE_EVENT::CACHE_REFERENCES,
        HARDWARE_EVENT::CPU_CYCLES,
        HARDWARE_EVENT::CYCLES,
        HARDWARE_EVENT::INSTRUCTIONS,
        HARDWARE_EVENT::STALLED_CYCLES_BACKEND,
        HARDWARE_EVENT::STALLED_CYCLES_FRONTED,
        HARDWARE_EVENT::IDLE_CYCLES_BACKEND,
        HARDWARE_EVENT::IDLE_CYCLES_FRONTED,
        HW_CACHE_EVENT::L1_DCACHE_LOAD_MISSES,
        HW_CACHE_EVENT::L1_DCACHE_LOADS,
        HW_CACHE_EVENT::L1_ICACHE_LOAD_MISSES,
        HW_CACHE_EVENT::L1_ICACHE_LOADS,
        HW_CACHE_EVENT::LLC_LOAD_MISSES,
        HW_CACHE_EVENT::LLC_LOADS,
        HW_CACHE_EVENT::BRANCH_LOAD_MISSES,
        HW_CACHE_EVENT::BRANCH_LOADS,
        HW_CACHE_EVENT::DTLB_LOAD_MISSES,
        HW_CACHE_EVENT::DTLB_LOADS,
        HW_CACHE_EVENT::ITLB_LOAD_MISSES,
        HW_CACHE_EVENT::ITLB_LOADS,
        SOFTWARE_EVENT::ALIGNMENT_FAULTS,
        SOFTWARE_EVENT::BPF_OUTPUT,
        SOFTWARE_EVENT::CONTEXT_SWITCHES,
        SOFTWARE_EVENT::CS,
        SOFTWARE_EVENT::CPU_CLOCK,
        SOFTWARE_EVENT::CPU_MIGRATIONS,
        SOFTWARE_EVENT::MIGRATIONS,
        SOFTWARE_EVENT::DUMMY,
        SOFTWARE_EVENT::EMULATION_FAULTS,
        SOFTWARE_EVENT::MAJOR_FAULTS,
        SOFTWARE_EVENT::MINOR_FAULTS,
        SOFTWARE_EVENT::PAGE_FAULTS,
        SOFTWARE_EVENT::FAULTS,
        SOFTWARE_EVENT::TASK_CLOCK,
};

const std::unordered_map<std::string, KUNPENG_PMU::CoreConfig> HIP_E_CORE_PMU_MAP{
        HARDWARE_EVENT::BRANCH_MISSES,
        HARDWARE_EVENT::CACHE_MISSES,
        HARDWARE_EVENT::CACHE_REFERENCES,
        HARDWARE_EVENT::CPU_CYCLES,
        HARDWARE_EVENT::CYCLES,
        HARDWARE_EVENT::INSTRUCTIONS,
        HARDWARE_EVENT::STALLED_CYCLES_BACKEND,
        HARDWARE_EVENT::STALLED_CYCLES_FRONTED,
        HARDWARE_EVENT::IDLE_CYCLES_BACKEND,
        HARDWARE_EVENT::IDLE_CYCLES_FRONTED,
        HW_CACHE_EVENT::L1_DCACHE_LOAD_MISSES,
        HW_CACHE_EVENT::L1_DCACHE_LOADS,
        HW_CACHE_EVENT::L1_ICACHE_LOAD_MISSES,
        HW_CACHE_EVENT::L1_ICACHE_LOADS,
        HW_CACHE_EVENT::LLC_LOAD_MISSES,
        HW_CACHE_EVENT::LLC_LOADS,
        HW_CACHE_EVENT::BRANCH_LOAD_MISSES,
        HW_CACHE_EVENT::BRANCH_LOADS,
        HW_CACHE_EVENT::DTLB_LOAD_MISSES,
        HW_CACHE_EVENT::DTLB_LOADS,
        HW_CACHE_EVENT::ITLB_LOAD_MISSES,
        HW_CACHE_EVENT::ITLB_LOADS,
        SOFTWARE_EVENT::ALIGNMENT_FAULTS,
        SOFTWARE_EVENT::BPF_OUTPUT,
        SOFTWARE_EVENT::CONTEXT_SWITCHES,
        SOFTWARE_EVENT::CS,
        SOFTWARE_EVENT::CPU_CLOCK,
        SOFTWARE_EVENT::CPU_MIGRATIONS,
        SOFTWARE_EVENT::MIGRATIONS,
        SOFTWARE_EVENT::DUMMY,
        SOFTWARE_EVENT::EMULATION_FAULTS,
        SOFTWARE_EVENT::MAJOR_FAULTS,
        SOFTWARE_EVENT::MINOR_FAULTS,
        SOFTWARE_EVENT::PAGE_FAULTS,
        SOFTWARE_EVENT::FAULTS,
        SOFTWARE_EVENT::TASK_CLOCK,
};

const std::unordered_map<std::string, KUNPENG_PMU::CoreConfig> HIP_X86_CORE_PMU_MAP{
        HARDWARE_EVENT::BRANCH_MISSES,
        HARDWARE_EVENT::CACHE_MISSES,
        HARDWARE_EVENT::CACHE_REFERENCES,
        HARDWARE_EVENT::CPU_CYCLES,
        HARDWARE_EVENT::CYCLES,
        HARDWARE_EVENT::INSTRUCTIONS,
        HARDWARE_EVENT::BUS_CYCLES,
        HARDWARE_EVENT::REF_CYCLES,
        HARDWARE_EVENT::BRANCH_INSTRUCTIONS,
        HARDWARE_EVENT::BRANCHES,

        SOFTWARE_EVENT::ALIGNMENT_FAULTS,
        SOFTWARE_EVENT::BPF_OUTPUT,
        SOFTWARE_EVENT::CONTEXT_SWITCHES,
        SOFTWARE_EVENT::CS,
        SOFTWARE_EVENT::CPU_CLOCK,
        SOFTWARE_EVENT::CPU_MIGRATIONS,
        SOFTWARE_EVENT::MIGRATIONS,
        SOFTWARE_EVENT::DUMMY,
        SOFTWARE_EVENT::EMULATION_FAULTS,
        SOFTWARE_EVENT::MAJOR_FAULTS,
        SOFTWARE_EVENT::MINOR_FAULTS,
        SOFTWARE_EVENT::PAGE_FAULTS,
        SOFTWARE_EVENT::FAULTS,
        SOFTWARE_EVENT::TASK_CLOCK,

        HW_CACHE_EVENT::L1_DCACHE_LOAD_MISSES,
        HW_CACHE_EVENT::L1_DCACHE_LOADS,
        HW_CACHE_EVENT::L1_ICACHE_LOAD_MISSES,
        HW_CACHE_EVENT::L1_ICACHE_LOADS,
        HW_CACHE_EVENT::LLC_LOAD_MISSES,
        HW_CACHE_EVENT::LLC_LOADS,
        HW_CACHE_EVENT::LLC_STORE_MISSES,
        HW_CACHE_EVENT::LLC_STORES,
        HW_CACHE_EVENT::BRANCH_LOAD_MISSES,
        HW_CACHE_EVENT::BRANCH_LOADS,
        HW_CACHE_EVENT::DTLB_LOAD_MISSES,
        HW_CACHE_EVENT::DTLB_LOADS,
        HW_CACHE_EVENT::DTLB_STORE_MISSES,
        HW_CACHE_EVENT::DTLB_STORES,
        HW_CACHE_EVENT::ITLB_LOADS,
        HW_CACHE_EVENT::ITLB_LOAD_MISSES,
        HW_CACHE_EVENT::NODE_LOAD_MISSES,
        HW_CACHE_EVENT::NODE_LOADS,
        HW_CACHE_EVENT::NODE_STORE_MISSES,
        HW_CACHE_EVENT::NODE_STORES,
};

const KUNPENG_PMU::CORE_EVT_MAP KUNPENG_PMU::CORE_EVENT_MAP = {
    {CHIP_TYPE::HIPA, HIP_A_CORE_PMU_MAP},
    {CHIP_TYPE::HIPB, HIP_B_CORE_PMU_MAP},
    {CHIP_TYPE::HIPC, HIP_C_CORE_PMU_MAP},
    {CHIP_TYPE::HIPF, HIP_F_CORE_PMU_MAP},
    {CHIP_TYPE::HIPE, HIP_E_CORE_PMU_MAP},
    {CHIP_TYPE::HIPX86, HIP_X86_CORE_PMU_MAP},
};

static struct PmuEvt* ConstructPmuEvtFromCore(KUNPENG_PMU::CoreConfig config, int collectType)
{
    auto* pmuEvtPtr = new PmuEvt {0};
    pmuEvtPtr->config = config.config;
    pmuEvtPtr->name = config.eventName;
    pmuEvtPtr->type = config.type;
    pmuEvtPtr->pmuType = KUNPENG_PMU::CORE_TYPE;
    pmuEvtPtr->collectType = collectType;
    return pmuEvtPtr;
}

static int64_t GetKernelCoreEventConfig(const string &name)
{
    auto pmuDevicePath = GetPmuDevicePath();
    if (pmuDevicePath.empty()) {
        return -1;
    }
    string eventPath = pmuDevicePath + "/events/" + name;
    string realPath = GetRealPath(eventPath);
    if (!IsValidPath(realPath)) {
        return -1;
    }
    ifstream evtIn(realPath);
    if (!evtIn.is_open()) {
        return -1;
    }
    string configStr;
    evtIn >> configStr;
    auto findEq = configStr.find('=');
    if (findEq == string::npos) {
        return -1;
    }
    auto subStr = configStr.substr(findEq + 1, configStr.size() - findEq);
    return stoi(subStr, nullptr, 16);
}

static int64_t GetKernelCoreEventType()
{
    auto pmuDevicePath = GetPmuDevicePath();
    if (pmuDevicePath.empty()) {
        return -1;
    }
    string eventPath = pmuDevicePath + "/type";
    string realPath = GetRealPath(eventPath);
    if (!IsValidPath(realPath)) {
        return -1;
    }
    ifstream typeIn(realPath);
    if (!typeIn.is_open()) {
        return -1;
    }
    string typeStr;
    typeIn >> typeStr;

    return stoi(typeStr);
}

static struct PmuEvt* ConstructPmuEvtFromKernel(const char* pmuName, int collectType)
{
    int64_t config = GetKernelCoreEventConfig(pmuName);
    int64_t type = GetKernelCoreEventType();
    if (config == -1 || type == -1) {
        return nullptr;
    }
    auto* pmuEvtPtr = new PmuEvt {0};
    pmuEvtPtr->config = config;
    pmuEvtPtr->name = pmuName;
    pmuEvtPtr->type = type;
    pmuEvtPtr->pmuType = KUNPENG_PMU::CORE_TYPE;
    pmuEvtPtr->collectType = collectType;
    return pmuEvtPtr;
}

struct PmuEvt* GetCoreEvent(const char* pmuName, int collectType)
{
    g_chipType = GetCpuType();
    if (g_chipType == UNDEFINED_TYPE) {
        return nullptr;
    }
    auto coreMap = KUNPENG_PMU::CORE_EVENT_MAP.at(g_chipType);
    if (coreMap.find(pmuName) != coreMap.end()) {
        return ConstructPmuEvtFromCore(KUNPENG_PMU::CORE_EVENT_MAP.at(g_chipType).at(pmuName), collectType);
    }
        return ConstructPmuEvtFromKernel(pmuName, collectType);
}

std::string GetPmuDevicePath()
{
    if (!pmuDevice.empty()) {
        return pmuDevice;
    }

    DIR *dir = opendir(SYS_DEVICE_PATH.c_str());
    if (dir == nullptr) {
        return "";
    }
    struct dirent *dent;
    while (dent = readdir(dir)) {
#ifdef IS_X86
        // look for devices like /sys/bus/event_source/devices/cpu/events
        if (strcmp(dent->d_name, "cpu") == 0) {
            pmuDevice = SYS_DEVICE_PATH + dent->d_name;
            break;
        }
#else
        if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..") || !strcmp(dent->d_name, "cpu")) {
            continue;
        }

        // look for devices like /sys/bus/event_source/devices/armv8_pmuv3_0/cpus.
        // Refer to function <is_arm_pmu_core> in kernel.
        string armPmuPath = SYS_DEVICE_PATH + dent->d_name + "/cpus";
        if (ExistPath(armPmuPath)) {
            pmuDevice = SYS_DEVICE_PATH + dent->d_name;
            break;
        }
#endif
    }
    closedir(dir);
    return pmuDevice;
}
