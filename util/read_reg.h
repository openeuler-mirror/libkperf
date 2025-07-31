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
#ifndef READ_REG_H
#define READ_REG_H

#include <stdint.h>

uint64_t ReadPmccntr();
uint64_t ReadPmevcntr(int idx);
uint64_t ReadPerfCounter(int idx);
uint64_t ReadCntvct();

#ifndef ReadTimestamp
static uint64_t ReadTimestamp(void)
{
    return ReadCntvct();
}
#endif

#ifndef Barrier
static inline void Barrier()
{
#if defined(IS_ARM)
    asm volatile("" : : : "memory");
#endif
}
#endif

#ifndef MulU32U32
static inline uint64_t MulU32U32(uint32_t a, uint32_t b)
{
    return (uint64_t)a * b;
}
#endif

#ifndef MulU64U32Shr
static inline uint64_t MulU64U32Shr(uint64_t a, uint32_t mul, unsigned int shift)
{
    uint32_t ah = a >> 32;
    uint32_t al = a;
    uint64_t ret;

    ret = MulU32U32(al, mul) >> shift;
    if (ah) {
        ret += MulU32U32(ah, mul) << (32 - shift);
    }
    return ret;
}
#endif
#endif