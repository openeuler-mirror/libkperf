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
 * Description: definition of structures related to performance event sampling and recording in
 * the KUNPENG_PMU namespace
 ******************************************************************************/
#ifndef PMU_EVENT_H
#define PMU_EVENT_H
#include <memory>
#include <map>
#include <string>
#include <vector>
#include <climits>
#include <linux/types.h>
#include "pmu.h"

#ifndef PMU_SAMPLE_STRUCT_H
#define PMU_SAMPLE_STRUCT_H
#include <linux/types.h>
#include <linux/perf_event.h>

struct PmuEvt {
    __u64 type;     // pmu event type defined in linux/perf_event.h
    __u64 config;   // event configuration
    __u64 config1;  // event filter if applicable
    __u64 config2;  // further filter if necessary on top of config1
    int pmuType;    // if pmu is CORE/UNCORE/SPE and etc (to be implemented)
    int collectType;
    std::string name;   // string name of this pmu event
    int cpumask;    // a representative CPU number for each socket (package) in the motherboard.
    unsigned excludeUser : 1;     // don't count user
    unsigned excludeKernel : 1;   //  don't count kernel
    unsigned callStack : 1;   //  collect complete call stack
    union {
        unsigned period;            // sample period
        unsigned freq;              // sample frequency
    };
    unsigned useFreq : 1;
};

namespace KUNPENG_PMU {

const int PERF_SAMPLE_MAX_SIZE (1 << 16);

struct PerfRawSample {
    __u64 sampleid;
    __u64 ip;
    __u32 pid, tid;
    __u64 time;
    __u64 id;
    int cpu;
    __u64 period;
    __u64 nr;
    unsigned long ips[];
};

struct TraceRawData {
    __u32 size;
    char data[];
};

struct PerfTraceSample {
    __u64 sampleId;
    __u32 tid, pid;
    __u64 time;
    __u32 cpu;
    __u32 size;
    char data[];
};

struct sampleId {
    __u32 pid, tid;     /* if PERF_SAMPLE_TID set */
    __u64 time;         /* if PERF_SAMPLE_TIME set */
    __u64 id;           /* if PERF_SAMPLE_ID set */
    __u32 cpu, res;     /* if PERF_SAMPLE_CPU set */
    __u64 identifier;   /* if PERF_SAMPLE_IDENTIFIER set */
};

struct PerfRawMmap {
    __u32 pid, tid;
    __u64 addr;
    __u64 len;
    __u64 pgoff;
    char filename[];
};

struct PerfRecordMmap {
    struct perf_event_header header;
    __u32 pid, tid;
    __u64 addr;
    __u64 len;
    __u64 pgoff;
    char filename[PATH_MAX];
    struct sampleId sampleId;
};

struct PerfRecordMmap2 {
    struct perf_event_header header;
    __u32 pid, tid;
    __u64 addr;
    __u64 len;
    __u64 pgoff;
    __u32 maj;
    __u32 min;
    __u64 ino;
    __u64 ino_generation;
    __u32 prot, flags;
    char filename[];
};

struct PerfRecordComm {
    struct perf_event_header header;
    __u32 pid, tid;
    char comm[];
};

struct PerfRecordSample {
    struct perf_event_header header;
    __u64 array[];
};

struct PerfRecordFork {
    struct perf_event_header header;
    __u32 pid, ppid;
    __u32 tid, ptid;
    __u64 time;
};

struct PerfRecordExit {
    struct perf_event_header header;
    __u32 pid, ppid;
    __u32 tid, ptid;
    __u64 time;
};

struct PerfMmap {
    struct perf_event_mmap_page* base;
    __u64 mask;
    int fd;
    __u64 prev;
    __u64 start;
    __u64 end;
    bool overwrite;
    __u64 flush;
    char copiedEvent[PERF_SAMPLE_MAX_SIZE];
};

struct PerfSampleIps {
    std::vector<unsigned long> ips;
};

union PerfEvent {
    struct perf_event_header header;
    struct PerfRecordMmap  mmap;
    struct PerfRecordComm comm;
    struct PerfRecordFork fork;
    struct PerfRecordSample sample;
    struct PerfRecordExit exit;
    struct PerfRecordMmap2 mmap2;
};

int MapErrno(int sysErr);
}   // namespace KUNPENG_PMU
#endif
#endif
