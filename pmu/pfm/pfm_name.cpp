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
 * Description: core event name definition
 ******************************************************************************/
#include <string>
#include <vector>
#include <map>
#include <linux/types.h>
#include "pfm_name.h"

/**
 * CORE events for HIP_A
 */
const char* KUNPENG_PMU::COMMON::BRANCH_MISSES = "branch-misses";
const char* KUNPENG_PMU::COMMON::BUS_CYCLES = "bus-cycles";
const char* KUNPENG_PMU::COMMON::CACHE_MISSES = "cache-misses";
const char* KUNPENG_PMU::COMMON::CACHE_REFERENCES = "cache-references";
const char* KUNPENG_PMU::COMMON::CPU_CYCLES = "cpu-cycles";
const char* KUNPENG_PMU::COMMON::CYCLES = "cycles";
const char* KUNPENG_PMU::COMMON::INSTRUCTIONS = "instructions";
const char* KUNPENG_PMU::COMMON::STALLED_CYCLES_BACKEND = "stalled-cycles-backend";
const char* KUNPENG_PMU::COMMON::STALLED_CYCLES_FRONTEND = "stalled-cycles-frontend";
const char* KUNPENG_PMU::COMMON::L1_DCACHE_LOAD_MISSES = "L1-dcache-load-misses";
const char* KUNPENG_PMU::COMMON::IDLE_CYCLES_BACKEND = "idle-cycles-backend";
const char* KUNPENG_PMU::COMMON::L1_ICACHE_LOAD_MISSES = "L1-icache-load-misses";
const char* KUNPENG_PMU::COMMON::IDLE_CYCLES_FRONTEND = "idle-cycles-frontend";
const char* KUNPENG_PMU::COMMON::L1_ICACHE_LOADS = "L1-icache-loads";
const char* KUNPENG_PMU::COMMON::LLC_LOAD_MISSES = "LLC-load-misses";
const char* KUNPENG_PMU::COMMON::LLC_LOADS = "LLC-loads";
const char* KUNPENG_PMU::COMMON::BRANCH_LOAD_MISSES = "branch-load-misses";
const char* KUNPENG_PMU::COMMON::BRANCH_LOADS = "branch-loads";
const char* KUNPENG_PMU::COMMON::DTLB_LOAD_MISSES = "dTLB-load-misses";
const char* KUNPENG_PMU::COMMON::DTLB_LOADS = "dTLB-loads";
const char* KUNPENG_PMU::COMMON::ITLB_LOAD_MISSES = "iTLB-load-misses";
const char* KUNPENG_PMU::COMMON::ITLB_LOADS = "iTLB-loads";
const char* KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_RD = "l1d_cache_rd";
const char* KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WR = "l1d_cache_wr";
const char* KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_REFILL_RD = "l1d_cache_refill_rd";
const char* KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_REFILL_WR = "l1d_cache_refill_rd";
const char* KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WB_VICTIM = "l1d_cache_wb_victim";
const char* KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_WB_CLEAN = "l1d_cache_wb_clean";
const char* KUNPENG_PMU::HIP_A::CORE::L1D_CACHE_INVAL = "l1d_cache_inval";
const char* KUNPENG_PMU::HIP_A::CORE::L1D_TLB_REFILL_RD = "l1d_tlb_refill_rd";
const char* KUNPENG_PMU::HIP_A::CORE::L1D_TLB_REFILL_WR = "l1d_tlb_refill_wr";
const char* KUNPENG_PMU::HIP_A::CORE::L1D_TLB_RD = "l1d_tlb_rd";
const char* KUNPENG_PMU::HIP_A::CORE::L1D_TLB_WR = "l1d_tlb_wr";
const char* KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_RD = "l2d_cache_rd";
const char* KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WR = "l2d_cache_wr";
const char* KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_REFILL_RD = "l2d_cache_refill_rd";
const char* KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_REFILL_WR = "l2d_cache_refill_rd";
const char* KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WB_VICTIM = "l2d_cache_wb_victim";
const char* KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_WB_CLEAN = "l2d_cache_wb_clean";
const char* KUNPENG_PMU::HIP_A::CORE::L2D_CACHE_INVAL = "l2d_cache_inval";
const char* KUNPENG_PMU::HIP_A::CORE::L1I_CACHE_PRF = "l1i_cache_prf";
const char* KUNPENG_PMU::HIP_A::CORE::L1I_CACHE_PRF_REFILL = "l1i_cache_prf_refill";
const char* KUNPENG_PMU::HIP_A::CORE::IQ_IS_EMPTY = "iq_is_empty";
const char* KUNPENG_PMU::HIP_A::CORE::IF_IS_STALL = "if_is_stall";
const char* KUNPENG_PMU::HIP_A::CORE::FETCH_BUBBLE = "fetch_bubble";
const char* KUNPENG_PMU::HIP_A::CORE::PRF_REQ = "prf_req";
const char* KUNPENG_PMU::HIP_A::CORE::HIT_ON_PRF = "hit_on_prf";
const char* KUNPENG_PMU::HIP_A::CORE::EXE_STALL_CYCLE = "exe_stall_cycle";
const char* KUNPENG_PMU::HIP_A::CORE::MEM_STALL_ANYLOAD = "mem_stall_anyload";
const char* KUNPENG_PMU::HIP_A::CORE::MEM_STALL_L1MISS = "mem_stall_l1miss";
const char* KUNPENG_PMU::HIP_A::CORE::MEM_STALL_L2MISS = "mem_stall_l2miss";
