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
 * Description: core event config
 ******************************************************************************/
#include <vector>
#include <unordered_map>
#include <fstream>
#include "pmu_event.h"
#include "core.h"
#include "common.h"

using namespace std;
static CHIP_TYPE g_chipType = UNDEFINED_TYPE;

const std::unordered_map<std::string, KUNPENG_PMU::CoreConfig> HIP_A_CORE_PMU_MAP{
    {
        KUNPENG_PMU::COMMON::BRANCH_MISSES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_BRANCH_MISSES,
            KUNPENG_PMU::COMMON::BRANCH_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::BUS_CYCLES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_BUS_CYCLES,
            KUNPENG_PMU::COMMON::BUS_CYCLES
        }
    },
    {
        KUNPENG_PMU::COMMON::CACHE_MISSES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_CACHE_MISSES,
            KUNPENG_PMU::COMMON::CACHE_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::CACHE_REFERENCES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_CACHE_REFERENCES,
            KUNPENG_PMU::COMMON::CACHE_REFERENCES
        }
    },
    {
        KUNPENG_PMU::COMMON::CPU_CYCLES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_CPU_CYCLES,
            KUNPENG_PMU::COMMON::CPU_CYCLES
        }
    },
    {
        KUNPENG_PMU::COMMON::CYCLES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_CPU_CYCLES,
            KUNPENG_PMU::COMMON::CYCLES
        }
    },
    {
        KUNPENG_PMU::COMMON::INSTRUCTIONS,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_INSTRUCTIONS,
            KUNPENG_PMU::COMMON::INSTRUCTIONS
        }
    },
    {
        KUNPENG_PMU::COMMON::STALLED_CYCLES_BACKEND,
        {
            PERF_TYPE_HARDWARE,
            0x8,
            KUNPENG_PMU::COMMON::STALLED_CYCLES_BACKEND
        }
    },
    {
        KUNPENG_PMU::COMMON::STALLED_CYCLES_FRONTEND,
        {
            PERF_TYPE_HARDWARE,
            0x7,
            KUNPENG_PMU::COMMON::STALLED_CYCLES_FRONTEND
        }
    },
    {
        KUNPENG_PMU::COMMON::L1_DCACHE_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10000,
            KUNPENG_PMU::COMMON::L1_DCACHE_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::IDLE_CYCLES_BACKEND,
        {
            PERF_TYPE_HARDWARE,
            0x8,
            KUNPENG_PMU::COMMON::IDLE_CYCLES_BACKEND
        }
    },
    {
        KUNPENG_PMU::COMMON::L1_ICACHE_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10001,
            KUNPENG_PMU::COMMON::L1_ICACHE_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::IDLE_CYCLES_FRONTEND,
        {
            PERF_TYPE_HARDWARE,
            0x7,
            KUNPENG_PMU::COMMON::IDLE_CYCLES_FRONTEND
        }
    },
    {
        KUNPENG_PMU::COMMON::L1_ICACHE_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x1,
            KUNPENG_PMU::COMMON::L1_ICACHE_LOADS
        }
    },
    {
        KUNPENG_PMU::COMMON::LLC_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10002,
            KUNPENG_PMU::COMMON::LLC_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::LLC_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x2,
            KUNPENG_PMU::COMMON::LLC_LOADS
        }
    },
    {
        KUNPENG_PMU::COMMON::BRANCH_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10005,
            KUNPENG_PMU::COMMON::BRANCH_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::BRANCH_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x5,
            KUNPENG_PMU::COMMON::BRANCH_LOADS
        }
    },
    {
        KUNPENG_PMU::COMMON::DTLB_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10003,
            KUNPENG_PMU::COMMON::DTLB_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::DTLB_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x3,
            KUNPENG_PMU::COMMON::DTLB_LOADS
        }
    },
    {
        KUNPENG_PMU::COMMON::ITLB_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10004,
            KUNPENG_PMU::COMMON::ITLB_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::ITLB_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x4,
            KUNPENG_PMU::COMMON::ITLB_LOADS
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_RD,
        {
            PERF_TYPE_RAW,
            0x40,
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_RD
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WR,
        {
            PERF_TYPE_RAW,
            0x41,
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WR
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_REFILL_RD,
        {
            PERF_TYPE_RAW,
            0x42,
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_REFILL_RD
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_REFILL_WR,
        {
            PERF_TYPE_RAW,
            0x43,
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_REFILL_WR
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WB_VICTIM,
        {
            PERF_TYPE_RAW,
            0x46,
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WB_VICTIM
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WB_CLEAN,
        {
            PERF_TYPE_RAW,
            0x47,
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WB_CLEAN
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_INVAL,
        {
            PERF_TYPE_RAW,
            0x48,
            KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_INVAL
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L1D_TLB_REFILL_RD,
        {
            PERF_TYPE_RAW,
            0x4c,
            KUNPENG_PMU::HIP_A::CORE::L1D_TLB_REFILL_RD
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L1D_TLB_REFILL_WR,
        {
            PERF_TYPE_RAW,
            0x4d,
            KUNPENG_PMU::HIP_A::CORE::L1D_TLB_REFILL_WR
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L1D_TLB_RD,
        {
            PERF_TYPE_RAW,
            0x4e,
            KUNPENG_PMU::HIP_A::CORE::L1D_TLB_RD
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L1D_TLB_WR,
        {
            PERF_TYPE_RAW,
            0x4f,
            KUNPENG_PMU::HIP_A::CORE::L1D_TLB_WR
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_RD,
        {
            PERF_TYPE_RAW,
            0x50,
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_RD
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WR,
        {
            PERF_TYPE_RAW,
            0x51,
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WR
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_REFILL_RD,
        {
            PERF_TYPE_RAW,
            0x52,
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_REFILL_RD
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_REFILL_WR,
        {
            PERF_TYPE_RAW,
            0x53,
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_REFILL_WR
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WB_VICTIM,
        {
            PERF_TYPE_RAW,
            0x56,
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WB_VICTIM
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WB_CLEAN,
        {
            PERF_TYPE_RAW,
            0x57,
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WB_CLEAN
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_INVAL,
        {
            PERF_TYPE_RAW,
            0x58,
            KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_INVAL
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L1I_CACHE_PRF,
        {
            PERF_TYPE_RAW,
            0x102e,
            KUNPENG_PMU::HIP_A::CORE::L1I_CACHE_PRF
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::L1I_CACHE_PRF_REFILL,
        {
            PERF_TYPE_RAW,
            0x102f,
            KUNPENG_PMU::HIP_A::CORE::L1I_CACHE_PRF_REFILL
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::IQ_IS_EMPTY,
        {
            PERF_TYPE_RAW,
            0x1043,
            KUNPENG_PMU::HIP_A::CORE::IQ_IS_EMPTY
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::IF_IS_STALL,
        {
            PERF_TYPE_RAW,
            0x1044,
            KUNPENG_PMU::HIP_A::CORE::IF_IS_STALL
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::FETCH_BUBBLE,
        {
            PERF_TYPE_RAW,
            0x2014,
            KUNPENG_PMU::HIP_A::CORE::FETCH_BUBBLE
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::PRF_REQ,
        {
            PERF_TYPE_RAW,
            0x6013,
            KUNPENG_PMU::HIP_A::CORE::PRF_REQ
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::HIT_ON_PRF,
        {
            PERF_TYPE_RAW,
            0x6014,
            KUNPENG_PMU::HIP_A::CORE::HIT_ON_PRF
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::EXE_STALL_CYCLE,
        {
            PERF_TYPE_RAW,
            0x7001,
            KUNPENG_PMU::HIP_A::CORE::EXE_STALL_CYCLE
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::MEM_STALL_ANYLOAD,
        {
            PERF_TYPE_RAW,
            0x7004,
            KUNPENG_PMU::HIP_A::CORE::MEM_STALL_ANYLOAD
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::MEM_STALL_L1MISS,
        {
            PERF_TYPE_RAW,
            0x7006,
            KUNPENG_PMU::HIP_A::CORE::MEM_STALL_L1MISS
        }
    },
    {
        KUNPENG_PMU::HIP_A::CORE::MEM_STALL_L2MISS,
        {
            PERF_TYPE_RAW,
            0x7007,
            KUNPENG_PMU::HIP_A::CORE::MEM_STALL_L2MISS
        }
    },
};

const std::unordered_map<std::string, KUNPENG_PMU::CoreConfig> HIP_B_CORE_PMU_MAP{
    {
        KUNPENG_PMU::COMMON::BRANCH_MISSES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_BRANCH_MISSES,
            KUNPENG_PMU::COMMON::BRANCH_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::CACHE_MISSES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_CACHE_MISSES,
            KUNPENG_PMU::COMMON::CACHE_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::CACHE_REFERENCES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_CACHE_REFERENCES,
            KUNPENG_PMU::COMMON::CACHE_REFERENCES
        }
    },
    {
        KUNPENG_PMU::COMMON::CPU_CYCLES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_CPU_CYCLES,
            KUNPENG_PMU::COMMON::CPU_CYCLES
        }
    },
    {
        KUNPENG_PMU::COMMON::CYCLES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_CPU_CYCLES,
            KUNPENG_PMU::COMMON::CYCLES
        }
    },
    {
        KUNPENG_PMU::COMMON::INSTRUCTIONS,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_INSTRUCTIONS,
            KUNPENG_PMU::COMMON::INSTRUCTIONS
        }
    },
    {
        KUNPENG_PMU::COMMON::STALLED_CYCLES_BACKEND,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_STALLED_CYCLES_BACKEND,
            KUNPENG_PMU::COMMON::STALLED_CYCLES_BACKEND
        }
    },
    {
        KUNPENG_PMU::COMMON::STALLED_CYCLES_FRONTEND,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_STALLED_CYCLES_FRONTEND,
            KUNPENG_PMU::COMMON::STALLED_CYCLES_FRONTEND
        }
    },
    {
        KUNPENG_PMU::COMMON::L1_DCACHE_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10000,
            KUNPENG_PMU::COMMON::L1_DCACHE_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::IDLE_CYCLES_BACKEND,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_STALLED_CYCLES_BACKEND,
            KUNPENG_PMU::COMMON::IDLE_CYCLES_BACKEND
        }
    },
    {
        KUNPENG_PMU::COMMON::L1_ICACHE_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10001,
            KUNPENG_PMU::COMMON::L1_ICACHE_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::IDLE_CYCLES_FRONTEND,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_STALLED_CYCLES_FRONTEND,
            KUNPENG_PMU::COMMON::IDLE_CYCLES_FRONTEND
        }
    },
    {
        KUNPENG_PMU::COMMON::L1_ICACHE_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x1,
            KUNPENG_PMU::COMMON::L1_ICACHE_LOADS
        }
    },
    {
        KUNPENG_PMU::COMMON::LLC_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10002,
            KUNPENG_PMU::COMMON::LLC_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::LLC_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x2,
            KUNPENG_PMU::COMMON::LLC_LOADS
        }
    },
    {
        KUNPENG_PMU::COMMON::BRANCH_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10005,
            KUNPENG_PMU::COMMON::BRANCH_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::BRANCH_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x5,
            KUNPENG_PMU::COMMON::BRANCH_LOADS
        }
    },
    {
        KUNPENG_PMU::COMMON::DTLB_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10003,
            KUNPENG_PMU::COMMON::DTLB_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::DTLB_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x3,
            KUNPENG_PMU::COMMON::DTLB_LOADS
        }
    },
    {
        KUNPENG_PMU::COMMON::ITLB_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10004,
            KUNPENG_PMU::COMMON::ITLB_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::ITLB_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x4,
            KUNPENG_PMU::COMMON::ITLB_LOADS
        }
    },
};

const std::unordered_map<std::string, KUNPENG_PMU::CoreConfig> HIP_C_CORE_PMU_MAP{
    {
        KUNPENG_PMU::COMMON::BRANCH_MISSES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_BRANCH_MISSES,
            KUNPENG_PMU::COMMON::BRANCH_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::CACHE_MISSES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_CACHE_MISSES,
            KUNPENG_PMU::COMMON::CACHE_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::CACHE_REFERENCES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_CACHE_REFERENCES,
            KUNPENG_PMU::COMMON::CACHE_REFERENCES
        }
    },
    {
        KUNPENG_PMU::COMMON::CPU_CYCLES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_CPU_CYCLES,
            KUNPENG_PMU::COMMON::CPU_CYCLES
        }
    },
    {
        KUNPENG_PMU::COMMON::CYCLES,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_CPU_CYCLES,
            KUNPENG_PMU::COMMON::CYCLES
        }
    },
    {
        KUNPENG_PMU::COMMON::INSTRUCTIONS,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_INSTRUCTIONS,
            KUNPENG_PMU::COMMON::INSTRUCTIONS
        }
    },
    {
        KUNPENG_PMU::COMMON::STALLED_CYCLES_BACKEND,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_STALLED_CYCLES_BACKEND,
            KUNPENG_PMU::COMMON::STALLED_CYCLES_BACKEND
        }
    },
    {
        KUNPENG_PMU::COMMON::STALLED_CYCLES_FRONTEND,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_STALLED_CYCLES_FRONTEND,
            KUNPENG_PMU::COMMON::STALLED_CYCLES_FRONTEND
        }
    },
    {
        KUNPENG_PMU::COMMON::L1_DCACHE_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10000,
            KUNPENG_PMU::COMMON::L1_DCACHE_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::IDLE_CYCLES_BACKEND,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_STALLED_CYCLES_BACKEND,
            KUNPENG_PMU::COMMON::IDLE_CYCLES_BACKEND
        }
    },
    {
        KUNPENG_PMU::COMMON::L1_ICACHE_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10001,
            KUNPENG_PMU::COMMON::L1_ICACHE_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::IDLE_CYCLES_FRONTEND,
        {
            PERF_TYPE_HARDWARE,
            PERF_COUNT_HW_STALLED_CYCLES_FRONTEND,
            KUNPENG_PMU::COMMON::IDLE_CYCLES_FRONTEND
        }
    },
    {
        KUNPENG_PMU::COMMON::L1_ICACHE_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x1,
            KUNPENG_PMU::COMMON::L1_ICACHE_LOADS
        }
    },
    {
        KUNPENG_PMU::COMMON::LLC_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10002,
            KUNPENG_PMU::COMMON::LLC_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::LLC_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x2,
            KUNPENG_PMU::COMMON::LLC_LOADS
        }
    },
    {
        KUNPENG_PMU::COMMON::BRANCH_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10005,
            KUNPENG_PMU::COMMON::BRANCH_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::BRANCH_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x5,
            KUNPENG_PMU::COMMON::BRANCH_LOADS
        }
    },
    {
        KUNPENG_PMU::COMMON::DTLB_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10003,
            KUNPENG_PMU::COMMON::DTLB_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::DTLB_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x3,
            KUNPENG_PMU::COMMON::DTLB_LOADS
        }
    },
    {
        KUNPENG_PMU::COMMON::ITLB_LOAD_MISSES,
        {
            PERF_TYPE_HW_CACHE,
            0x10004,
            KUNPENG_PMU::COMMON::ITLB_LOAD_MISSES
        }
    },
    {
        KUNPENG_PMU::COMMON::ITLB_LOADS,
        {
            PERF_TYPE_HW_CACHE,
            0x4,
            KUNPENG_PMU::COMMON::ITLB_LOADS
        }
    },
};

const KUNPENG_PMU::CORE_EVT_MAP KUNPENG_PMU::CORE_EVENT_MAP = {
    {CHIP_TYPE::HIPA, HIP_A_CORE_PMU_MAP},
    {CHIP_TYPE::HIPB, HIP_B_CORE_PMU_MAP},
    {CHIP_TYPE::HIPC, HIP_C_CORE_PMU_MAP},
};

static struct PmuEvt* ConstructPmuEvtFromCore(KUNPENG_PMU::CoreConfig config, int collectType)
{
    auto* pmuEvtPtr = new PmuEvt {0};
    pmuEvtPtr->config = config.config;
    pmuEvtPtr->name = config.eventName;
    pmuEvtPtr->type = config.type;
    pmuEvtPtr->pmuType = KUNPENG_PMU::CORE_TYPE;
    pmuEvtPtr->collectType = collectType;
    pmuEvtPtr->cpumask = -1;
    return pmuEvtPtr;
}

static int64_t GetKernelCoreEventConfig(const string &name)
{
    string eventPath = "/sys/devices/armv8_pmuv3_0/events/" + name;
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
    string eventPath = "/sys/devices/armv8_pmuv3_0/type";
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
    pmuEvtPtr->cpumask = -1;
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