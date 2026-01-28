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
 * Author: Wu
 * Create: 2026-01-23
 * Description: definition of pebs collection of lbr in Intel environment
 ******************************************************************************/
#ifndef SIMPLE_PEBS_H
#define SIMPLE_PEBS_H

#define SIMPLE_PEBS_BASE  0x7000
#define SIMPLE_PEBS_SET_CPU   (SIMPLE_PEBS_BASE + 1)
#define SIMPLE_PEBS_GET_SIZE  (SIMPLE_PEBS_BASE + 2)
#define SIMPLE_PEBS_GET_OFFSET (SIMPLE_PEBS_BASE + 3)
#define SIMPLE_PEBS_START  (SIMPLE_PEBS_BASE + 4)
#define SIMPLE_PEBS_STOP  (SIMPLE_PEBS_BASE + 5)
#define SIMPLE_PEBS_RESET  (SIMPLE_PEBS_BASE + 6)

/* pebs data configuration */
#define KPERF_MSR_PEBS_DATA_CFG     0x000003f2
#define KPERF_PEBS_DATACFG_MEMINFO  BIT_ULL(0)
#define KPERF_PEBS_DATACFG_GP       BIT_ULL(1)
#define KPERF_PEBS_DATACFG_XMMS     BIT_ULL(2)
#define KPERF_PEBS_DATACFG_LBRS     BIT_ULL(3)
#define KPERF_PEBS_DATACFG_CNTR     BIT_ULL(4)
#define KPERF_PEBS_DATACFG_METRICS  BIT_ULL(5)

#define KPERF_PEBS_DATACFG_LBR_SHIFT  24
#define SIMPLE_PEBS_MAX_LBR     32

/* Arch LBR register */
#define KPERF_MSR_ARCH_LBR_CTL        0x000014ce
#define KPERF_MSR_ARCH_LBR_DEPTH      0x000014cf
#define KPERF_ICL_EVENTSEL_ADAPTIVE   (1ULL << 34)
#define KPERF_CAP_PEBS_BASELINE  BIT_ULL(14)
#define KPERF_ARCH_LBR_CTL_LBREN    BIT_ULL(0)

#define KPERF_LBR_CTL_KERNEL   BIT_ULL(1)
#define KPERF_LBR_CTL_USER     BIT_ULL(2)
#define KPERF_LBR_CTL_JCC      BIT_ULL(16)
#define KPERF_LBR_CTL_REL_JMP  BIT_ULL(17)
#define KPERF_LBR_CTL_IND_JMP  BIT_ULL(18)
#define KPERF_LBR_CTL_REL_CALL BIT_ULL(19)
#define KPERF_LBR_CTL_IND_CALL BIT_ULL(20)
#define KPERF_LBR_CTL_RETURN   BIT_ULL(21)
#define KPERF_LBR_CTL_OTHER    BIT_ULL(22)
#define KPERF_LBR_CTL_ANY (KPERF_LBR_CTL_JCC | KPERF_LBR_CTL_REL_JMP | KPERF_LBR_CTL_IND_JMP | \
             KPERF_LBR_CTL_REL_CALL | KPERF_LBR_CTL_IND_CALL | KPERF_LBR_CTL_RETURN | \
             KPERF_LBR_CTL_OTHER)

#define PEBS_GROUP(b) ((u64)((b)->format_group))
#define PEBS_SIZE_BYTES(b) ((u16)((b)->format_size))

/* refresh LBR record*/
#define KPERF_GLOBAL_OVF_PMC0  BIT_ULL(0)
#define KPERF_GLOBAL_LBR_FRZ   BIT_ULL(58)
#define KPERF_GLOBAL_OVF_BUF   BIT_ULL(62)

struct pebs_basic {
    uint64_t format_group:32,
             retire_latency:16,
             format_size:16;
    uint64_t ip;
    uint64_t applicable_counters;
    uint64_t tsc;
} __attribute__((packed));

struct simple_pebs_out_rec {
    uint16_t size;
    uint8_t cpu;
    uint8_t lbr_depth;
    uint64_t ip;
    uint64_t tsc;
    uint64_t tid;
    uint64_t tgid;
    uint64_t lbr_from[SIMPLE_PEBS_MAX_LBR];
    uint64_t lbr_to[SIMPLE_PEBS_MAX_LBR];
    uint64_t lbr_info[SIMPLE_PEBS_MAX_LBR];
};

struct pebs_lbr_entry {
    uint64_t from;
    uint64_t to;
    uint64_t info;
};

#endif
