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
 * Author: yupan
 * Create: 2025-07-31
 * Description: Provide some funtions for reading register and counting
 ******************************************************************************/
#include "read_reg.h"

uint64_t ReadPmccntr()
{
    uint64_t val = 0;
#if defined(__aarch64__)
    asm volatile("mrs %0, pmccntr_el0" : "=r"(val));
#endif
    return val;
}

uint64_t ReadPmevcntr(int idx)
{
    uint64_t val = 0;
#if defined(__aarch64__)
    switch (idx) {
        case 0:
            asm volatile("mrs %0, pmevcntr0_el0" : "=r"(val));
            break;
        case 1:
            asm volatile("mrs %0, pmevcntr1_el0" : "=r"(val));
            break;
        case 2:
            asm volatile("mrs %0, pmevcntr2_el0" : "=r"(val));
            break;
        case 3:
            asm volatile("mrs %0, pmevcntr3_el0" : "=r"(val));
            break;
        case 4:
            asm volatile("mrs %0, pmevcntr4_el0" : "=r"(val));
            break;
        case 5:
            asm volatile("mrs %0, pmevcntr5_el0" : "=r"(val));
            break;
        case 6:
            asm volatile("mrs %0, pmevcntr6_el0" : "=r"(val));
            break;
        case 7:
            asm volatile("mrs %0, pmevcntr7_el0" : "=r"(val));
            break;
        case 8:
            asm volatile("mrs %0, pmevcntr8_el0" : "=r"(val));
            break;
        case 9:
            asm volatile("mrs %0, pmevcntr9_el0" : "=r"(val));
            break;
        case 10:
            asm volatile("mrs %0, pmevcntr10_el0" : "=r"(val));
            break;
        case 11:
            asm volatile("mrs %0, pmevcntr11_el0" : "=r"(val));
            break;
        case 12:
            asm volatile("mrs %0, pmevcntr12_el0" : "=r"(val));
            break;
        case 13:
            asm volatile("mrs %0, pmevcntr13_el0" : "=r"(val));
            break;
        case 14:
            asm volatile("mrs %0, pmevcntr14_el0" : "=r"(val));
            break;
        case 15:
            asm volatile("mrs %0, pmevcntr15_el0" : "=r"(val));
            break;
        case 16:
            asm volatile("mrs %0, pmevcntr16_el0" : "=r"(val));
            break;
        case 17:
            asm volatile("mrs %0, pmevcntr17_el0" : "=r"(val));
            break;
        case 18:
            asm volatile("mrs %0, pmevcntr18_el0" : "=r"(val));
            break;
        case 19:
            asm volatile("mrs %0, pmevcntr19_el0" : "=r"(val));
            break;
        case 20:
            asm volatile("mrs %0, pmevcntr20_el0" : "=r"(val));
            break;
        case 21:
            asm volatile("mrs %0, pmevcntr21_el0" : "=r"(val));
            break;
        case 22:
            asm volatile("mrs %0, pmevcntr22_el0" : "=r"(val));
            break;
        case 23:
            asm volatile("mrs %0, pmevcntr23_el0" : "=r"(val));
            break;
        case 24:
            asm volatile("mrs %0, pmevcntr24_el0" : "=r"(val));
            break;
        case 25:
            asm volatile("mrs %0, pmevcntr25_el0" : "=r"(val));
            break;
        case 26:
            asm volatile("mrs %0, pmevcntr26_el0" : "=r"(val));
            break;
        case 27:
            asm volatile("mrs %0, pmevcntr27_el0" : "=r"(val));
            break;
        case 28:
            asm volatile("mrs %0, pmevcntr28_el0" : "=r"(val));
            break;
        case 29:
            asm volatile("mrs %0, pmevcntr29_el0" : "=r"(val));
            break;
        case 30:
            asm volatile("mrs %0, pmevcntr30_el0" : "=r"(val));
            break;
        default:
            break;
    }
#endif
    return val;
}

uint64_t ReadPerfCounter(int idx)
{
    if (idx >= 0 && idx <= 30) {
        return ReadPmevcntr(idx);
    }

    if (idx == 31) {
        return ReadPmccntr();
    }
    return 0;
}

uint64_t ReadCntvct()
{
    uint64_t val = 0;
#if defined(__aarch64__)
    asm volatile("mrs %0, cntvct_el0" : "=r"(val));
#endif
    return val;
}