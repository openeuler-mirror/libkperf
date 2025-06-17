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
 * Author: Mr.Gan
 * Create: 2024-04-03
 * Description: definition of functions for handling performance event sampling processes in
 * the KUNPENG_PMU namespace
 ******************************************************************************/
#ifndef PMU_SAMPLE_PROCESS_H
#define PMU_SAMPLE_PROCESS_H
#include <memory>
#include "pmu_event.h"

#ifdef IS_X86
#define PerfRingbufferSmpStoreRelease(p, v)                                                       \
    ({                                                                                            \
        union {                                                                                   \
            typeof(*p) val;                                                                       \
            char charHead[1];                                                                     \
        } pointerUnion = {.val = (v)};                                                            \
        asm volatile("mov %1, %0" : "=Q"(*p) : "r"(*(__u64 *)pointerUnion.charHead) : "memory"); \
    })
#else
#define PerfRingbufferSmpStoreRelease(p, v)                                                       \
    ({                                                                                            \
        union {                                                                                   \
            typeof(*p) val;                                                                       \
            char charHead[1];                                                                     \
        } pointerUnion = {.val = (v)};                                                            \
        asm volatile("stlr %1, %0" : "=Q"(*p) : "r"(*(__u64 *)pointerUnion.charHead) : "memory"); \
    })
#endif

namespace KUNPENG_PMU {

    int MmapInit(PerfMmap& sampleMmap);
    union PerfEvent* ReadEvent(PerfMmap& map);
    int RingbufferReadInit(PerfMmap& map);
    inline void PerfMmapConsume(PerfMmap& map)
    {
        __u64 prev = map.prev;
        struct perf_event_mmap_page *base = (struct perf_event_mmap_page *)map.base;
        PerfRingbufferSmpStoreRelease(&base->data_tail, prev);
    }
    void PerfMmapReadDone(PerfMmap& map);

}  // namespace KUNPENG_PMU

#endif
