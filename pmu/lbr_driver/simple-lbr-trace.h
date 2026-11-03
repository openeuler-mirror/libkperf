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
 * Description: definition of lbr collection in Intel environment
 ******************************************************************************/
#ifndef LBR_TRACE_H
#define LBR_TRACE_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define SIMPLE_LBR_IOC_MAGIC        'q'
#define SIMPLE_LBR_SET_PID          _IOW(SIMPLE_LBR_IOC_MAGIC, 1, int)
#define SIMPLE_LBR_START            _IO(SIMPLE_LBR_IOC_MAGIC, 2)
#define SIMPLE_LBR_STOP             _IO(SIMPLE_LBR_IOC_MAGIC, 3)
#define SIMPLE_LBR_RESET            _IO(SIMPLE_LBR_IOC_MAGIC, 4)
#define SIMPLE_LBR_GET_SIZE         _IOR(SIMPLE_LBR_IOC_MAGIC, 5, int)
#define SIMPLE_LBR_GET_NR_CPUS      _IOR(SIMPLE_LBR_IOC_MAGIC, 6, int)
#define SIMPLE_LBR_GET_CPU_STATUS   _IOWR(SIMPLE_LBR_IOC_MAGIC, 7, struct simple_lbr_cpu_stats)
#define SIMPLE_LBR_ADVANCE_TAIL     _IOW(SIMPLE_LBR_IOC_MAGIC, 8 , struct simple_lbr_tail_advance)

#define KPERF_MSR_IA32_DEBUGCTLMSR    0x000001d9

#define SIMPLE_LBR_MAX_LBR     32
#define SIMPLE_LBR_RING_SLOTS  4096

/* Arch LBR register */
#define KPERF_MSR_ARCH_LBR_CTL        0x000014ce
#define KPERF_MSR_ARCH_LBR_DEPTH      0x000014cf
#define KPERF_MSR_ARCH_LBR_FROM_0     0x00001500
#define KPERF_MSR_ARCH_LBR_TO_0       0x00001600
#define KPERF_MSR_ARCH_LBR_INFO_0     0x00001200

#define KPERF_ARCH_LBR_CTL_LBREN BIT_ULL(0)
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

enum simple_lbr_reason {
    SIMPLE_LBR_REASON_SCHED_OUT = 1,
    SIMPLE_LBR_REASON_STOP = 2,
    SIMPLE_LBR_REASON_TIMER = 3,
};

struct simple_lbr_out_rec {
    uint16_t size;
    uint8_t cpu;
    uint8_t lbr_depth;
    uint64_t ip;
    uint64_t tsc;
    uint64_t tid;
    uint64_t tgid;
    uint16_t reason;
    uint64_t lbr_from[SIMPLE_LBR_MAX_LBR];
    uint64_t lbr_to[SIMPLE_LBR_MAX_LBR];
    uint64_t lbr_info[SIMPLE_LBR_MAX_LBR];
};

struct simple_lbr_cpu_stats {
    uint32_t cpu;
    uint32_t reserved;
    uint64_t head_seq;
    uint64_t tail_seq;
    uint64_t overwrite_records;
    uint64_t enable_failures;
};

struct simple_lbr_tail_advance {
    uint32_t cpu;
    uint32_t reserved;
    uint64_t tail_seq;
};
#endif