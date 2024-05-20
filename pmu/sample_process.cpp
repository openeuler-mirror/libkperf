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
 * Author: Mr.Zhang
 * Create: 2024-04-03
 * Description: functions for reading and managing data from a ring buffer in the KUNPENG_PMU namespace
 ******************************************************************************/
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include "pcerrc.h"
#include "evt.h"
#include "sample_process.h"

#define PAGE_SIZE (sysconf(_SC_PAGESIZE))
#define MB() asm volatile("dmb ish" ::: "memory")
static constexpr int MAX_DATA_SIZE = 8192;
#define PerfRingbufferSmpStoreRelease(p, v)                                                       \
    ({                                                                                            \
        union {                                                                                   \
            typeof(*p) val;                                                                       \
            char charHead[1];                                                                     \
        } pointerUnion = {.val = (v)};                                                            \
        asm volatile("stlr %1, %0" : "=Q"(*p) : "r"(*(__u64 *)pointerUnion.charHead) : "memory"); \
    })

void KUNPENG_PMU::PerfMmapConsume(PerfMmap &map)
{
     
    __u64 prev = map.prev;
    struct perf_event_mmap_page *base = (struct perf_event_mmap_page *)map.base;
    PerfRingbufferSmpStoreRelease(&base->data_tail, prev);
}

void KUNPENG_PMU::PerfMmapReadDone(PerfMmap &map)
{
    struct perf_event_mmap_page *base = (struct perf_event_mmap_page *)map.base;
    map.prev = ReadOnce(&base->data_head);
}

int KUNPENG_PMU::MmapInit(PerfMmap &sampleMmap)
{
    struct perf_event_mmap_page *base = (struct perf_event_mmap_page *)sampleMmap.base;
    __u64 head = ReadOnce(&base->data_head);
    __u64 prev = sampleMmap.prev;
    unsigned long size;

    sampleMmap.start = sampleMmap.overwrite ? head : prev;
    sampleMmap.end = sampleMmap.overwrite ? prev : head;

    if (__glibc_unlikely((sampleMmap.end - sampleMmap.start) < sampleMmap.flush)) {
        return -EAGAIN;
    }

    size = sampleMmap.end - sampleMmap.start;
    if (size > (unsigned long)(sampleMmap.mask) + 1) {
        if (!sampleMmap.overwrite) {
            sampleMmap.prev = head;
            PerfMmapConsume(sampleMmap);
        }
    }
    return 0;
}

void CopyDataInWhileLoop(KUNPENG_PMU::PerfMmap& map, __u64 offset, unsigned char* data, __u64 len)
{
    /*
     * Here, as long as we still have data inside the mmap, we continue to wrap around
     * the head and tail pointer to a point that
     */
    char *tmpDataPtr = map.copiedEvent;
    __u64 copiedData = 0;
    while (len) {
        __u64 restSize = offset & map.mask;
        copiedData = map.mask + 1 - restSize < len ? map.mask + 1 - restSize : len;

        memcpy(tmpDataPtr, &data[restSize], copiedData);

        offset += copiedData;
        tmpDataPtr += copiedData;
        len -= copiedData;
    }
}

static inline union KUNPENG_PMU::PerfEvent *PerfMmapRead(KUNPENG_PMU::PerfMmap &map, __u64 *startPointer, __u64 end)
{
    /*
     * Logic for reading ringbuffer
     */
    unsigned char *data = (unsigned char *)map.base + PAGE_SIZE;
    union KUNPENG_PMU::PerfEvent *event = nullptr;
    __u64 diff = end - *startPointer;
    if (diff >= sizeof(event->header)) {
        size_t size;

        event = (union KUNPENG_PMU::PerfEvent *)&data[*startPointer & map.mask];
        size = event->header.size;

        if (__glibc_unlikely(size < sizeof(event->header) || diff < size)) {
            return nullptr;
        }

        size_t event_size = sizeof(*event);
        if ((*startPointer & map.mask) + size != ((*startPointer + size) & map.mask)) {
            __u64 offset = *startPointer;
            __u64 len = event_size < size ? event_size : size;
            CopyDataInWhileLoop(map, offset, data, len);
            event = (union KUNPENG_PMU::PerfEvent *)map.copiedEvent;
        }

        *startPointer += size;
    }

    return event;
}

union KUNPENG_PMU::PerfEvent *KUNPENG_PMU::ReadEvent(PerfMmap &map)
{
    if (!map.overwrite) {
        struct perf_event_mmap_page *base = (struct perf_event_mmap_page *)map.base;
        map.end = ReadOnce(&base->data_head);
    }

    union KUNPENG_PMU::PerfEvent *event = PerfMmapRead(map, &map.start, map.end);

    if (!map.overwrite) {
        map.prev = map.start;
    }

    return event;
}

int KUNPENG_PMU::RingbufferReadInit(PerfMmap &map)
{
    __u64 head = ReadOnce(&map.base->data_head);
    __u64 prev = map.prev;
    unsigned long size;

    map.start = map.overwrite ? head : prev;
    map.end = map.overwrite ? prev : head;

    if (__glibc_unlikely((map.end - map.start) < map.flush)) {
        return UNKNOWN_ERROR;
    }

    size = map.end - map.start;
    if (size > (unsigned long)(map.mask) + 1) {
        if (!map.overwrite) {
            map.prev = head;
            PerfMmapConsume(map);
        }
    }
    return SUCCESS;
}
