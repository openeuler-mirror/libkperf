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
 * Author: Mr.Gan
 * Create: 2024-04-03
 * Description: declarations and definitions of interfaces and data structures exposed by perf.so
 ******************************************************************************/
#ifndef PMU_DATA_STRUCT_H
#define PMU_DATA_STRUCT_H
#include <unistd.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#pragma GCC visibility push(default)

enum PmuTaskType {
    COUNTING = 0, // pmu counting task
    SAMPLING = 1, // pmu sampling task
    SPE_SAMPLING = 2, // spe sampling task
    MAX_TASK_TYPE
};

enum AggregateType {
    PER_SYSTEM,
    PER_CORE,
    PER_NUMA,
    PER_SOCKET,
    PER_THREAD,
};

enum SpeFilter {
    SPE_FILTER_NONE = 0,
    TS_ENABLE = 1UL << 0,       // enable timestamping with value of generic timer
    PA_ENABLE = 1UL << 1,       // collect physical address (as well as VA) of loads/stores
    PCT_ENABLE = 1UL << 2,      // collect physical timestamp instead of virtual timestamp
    JITTER = 1UL << 16,         // use jitter to avoid resonance when sampling
    BRANCH_FILTER = 1UL << 32,  // collect branches only
    LOAD_FILTER = 1UL << 33,    // collect loads only
    STORE_FILTER = 1UL << 34,   // collect stores only
    SPE_DATA_ALL = TS_ENABLE | PA_ENABLE | PCT_ENABLE | JITTER | BRANCH_FILTER | LOAD_FILTER | STORE_FILTER
};

enum SpeEventFilter {
    SPE_EVENT_NONE = 0,
    SPE_EVENT_RETIRED = 0x2,        // instruction retired
    SPE_EVENT_L1DMISS = 0x8,        // L1D refill
    SPE_EVENT_TLB_WALK = 0x20,      // TLB refill
    SPE_EVENT_MISPREDICTED = 0x80,  // mispredict
};

struct PmuAttr {
    char** evtList;                 // event list
    unsigned numEvt;                // length of event list
    int* pidList;                   // pid list
    unsigned numPid;                // length of pid list
    int* cpuList;                   // cpu id list
    unsigned numCpu;                // length of cpu id list

    union {
        unsigned period;            // sample period
        unsigned freq;              // sample frequency
    };
    unsigned useFreq : 1;

    // SPE related fields.
    enum SpeFilter dataFilter;      // spe data filter
    enum SpeEventFilter evFilter;   // spe event filter
    unsigned long minLatency;       // collect only samples with latency or higher
};

struct PmuDataExt {
    unsigned long pa;               // physical address
    unsigned long va;               // virtual address
    unsigned long event;            // event id
};

struct PmuData {
    struct Stack* stack;           // call stack
    const char *evt;                // event name
    int64_t ts;                     // time stamp
    pid_t pid;                      // process id
    int tid;                        // thread id
    unsigned cpu;                   // cpu id
    struct CpuTopology *cpuTopo;    // cpu topology
    const char *comm;               // process command

    union {
        uint64_t count;             // event count. Only available for Counting.
        struct PmuDataExt *ext;     // extension. Only available for Spe.
    };
};

/**
 * @brief
 * Initialize the collection target.
 * @param collectType collection typr.
 * @param evtList array of event IDs
 * @param numEvt length of evtList.
 * @param pidList list of PIDs to be collected. Information about subprocess and subthreads of PIDs is collected. If
 * the value is NULL, all process/threads are collected
 * @param numPid length of pidList.
 * @param cpuList CPU ID list. If the value is NULL, all CPUs are collected.
 * @param numCpu cpuList length.
 * @return int
 */
int PmuOpen(enum PmuTaskType collectType, struct PmuAttr *attr);

/**
 * @brief
 * Collect <milliseconds> milliseconds. If <milliseconds> is equal to - 1 and the PID list is not empty, the collection
 * is performed until all processes are complete.
 * @param milliseconds
 * @return int
 */
int PmuCollect(int pd, int milliseconds);

/**
 * @brief
 * Similar to <PmuCollect>, and <PmuCollectV> accepts multiple pds.
 * @param milliseconds
 * @return int
 */
int PmuCollectV(int *pd, unsigned len, int milliseconds);

/**
 * @brief stop a sampling task in asynchronous mode
 * @param pd pmu descriptor.
 */
void PmuStop(int pd);

/**
 * @brief
 * Collect data. If the value is NULL and the error code is 0, no data is available in the current collection time. If
 * the value is NULL and the error code is not 0, an error occurs in the collection process and data cannot be read.
 * @param struct PmuData*
 */
int PmuRead(int pd, struct PmuData** pmuData);

/**
 * @brief Close all the file descriptor opened during collecting process
 */
void PmuClose(int pd);

/**
 * @brief Free PmuData pointer.
 * @param pmuData
 */
void PmuDataFree(struct PmuData* pmuData);

#pragma GCC visibility pop
#ifdef __cplusplus
}
#endif
#endif
