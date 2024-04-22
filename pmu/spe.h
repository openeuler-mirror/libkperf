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
 * Author: Mr.Jin
 * Create: 2024-04-03
 * Description: definition of class Spe for handling System Performance Events (SPE) data collection
 * and processing for each CPU, storing the collected data for further analysis
 ******************************************************************************/
#ifndef __SPE__HH__
#define __SPE__HH__

#include <vector>
#include <map>
#include <unordered_map>
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/perf_event.h>
#include "pmu_event.h"
#include "symbol.h"

#define MB() asm volatile("dsb sy")
#define RMB() asm volatile("dsb ld")
#define WMB() asm volatile("dsb st")

#define EVENT_EXCEPTION_GEN 0x1
#define EVENT_RETIRED 0x2
#define EVENT_L1D_ACCESS 0x4
#define EVENT_L1D_REFILL 0x8
#define EVENT_TLB_ACCESS 0x10
#define EVENT_TLB_REFILL 0x20
#define EVENT_NOT_TAKEN 0x40
#define EVENT_MISPRED 0x80
#define EVENT_LLC_ACCESS 0x100
#define EVENT_LLC_REFILL 0x200
#define EVENT_REMOTE_ACCESS 0x400

#define SPE_SAMPLE_MAX 200000

struct SpeCoreContext {
    int cpu;
    int speFd;
    int dummyFd;
    void *speMpage;
    void *auxMpage;
    void *dummyMpage;
    __u64 prev;
    size_t mask;
};

struct SpeContext {
    int cpuNum;
    __u64 speMmapSize;   /* size of spe event ring buffer + first page */
    __u64 auxMmapSize;   /* size of aux buffer */
    int dummyMmapSize; /* size of dummy event ring buffer + first page */
    int pageSize;
    struct SpeCoreContext *coreCtxes;
};

struct SpeRecord {
    uint64_t event;
    uint64_t pid;
    uint64_t tid;
    int cpu;
    int vaNid;
    uint64_t va;
    uint64_t pa;
    uint64_t timestamp;
    uint64_t pc;
};

struct PerfEventSample {
    struct perf_event_header header;  // only keep the header
};

struct PerfEventSampleAux {
    struct perf_event_header header;  // aux buffer event header
    uint64_t auxOffset;               // current offset in aux buffer
    uint64_t auxSize;                 // current aux data size
    uint64_t flags;                   // data flag
    uint32_t pid, tid;                // process and thread id of spe record
};

struct PerfEventSampleContextSwitch {
    struct perf_event_header header;  // context switch record header
    uint32_t nextPrevPid;  // The process ID of the previous (if switching in) or
    // next (if switching out) process on the CPU.
    uint32_t nextPrevTid;  // The thread ID of the previous (if switching in) or
    // next (if switching out) thread on the CPU.
    uint64_t time;         // timestamp
};

struct PerfTscConversion {
    uint16_t timeShift;
    uint32_t timeMult;
    uint64_t timeZero;
    int capUserTimeZero;
};

struct ContextSwitchData {
    uint32_t nextPrevPid = 0;
    uint32_t nextPrevTid = 0;
    union {
        uint64_t time = 0;
        uint64_t num;
    };
};

struct SampleId {
    __u32 pid, tid;   /* if PERF_SAMPLE_TID set */
    __u64 time;       /* if PERF_SAMPLE_TIME set */
    __u64 id;         /* if PERF_SAMPLE_ID set */
    __u32 cpu, res;   /* if PERF_SAMPLE_CPU set */
    __u64 identifier; /* if PERF_SAMPLE_IDENTIFIER set */
};

/**
 * @brief SPE collector for each cpu.
 */
class Spe {
public:
    explicit Spe(int cpu, std::unordered_map<pid_t, std::shared_ptr<ProcTopology>> &procMap)
            : cpu(cpu), procMap(procMap)
    {}

    ~Spe()
    {
        if (records != nullptr) {
            delete records;
            records = nullptr;
        }
    }

    /**
     * @brief Open SPE ring buffer.
     * @param attr sampling attribute.
     * @return true
     * @return false
     */
    int Open(PmuEvt *attr);

    /**
     * @brief Start collect.
     * @param clearPrevRecords whether clear all records from previos collection.
     */
    int Enable(bool clearPrevRecords = true);

    /**
     * @brief Stop collect.
     */
    int Disable();

    /**
     * @brief Free ring buffer.
     * @return true
     * @return false
     */
    bool Close();

    /**
     * @brief Read data in ring buffer in last collection, and store data in this object. Use GetPidRecords to get data.
     * @return true
     * @return false
     */
    int Read();

    /**
     * @brief The last collceted data have been read.
     * @return true
     * @return false
     */
    bool HaveRead();

    /**
     * @brief Get SPE data of process with <pid>.
     * @param pid
     * @return const std::vector<SpeRecord *>
     */
    const std::vector<SpeRecord *> GetPidRecords(const pid_t &pid) const;

    int GetSpeFd() const
    {
        return fd;
    }

    const std::map<pid_t, std::vector<SpeRecord *>>& GetAllRecords() const
    {
        return pidRecords;
    }

private:
    int SpeReadData(struct SpeContext *context, struct SpeRecord *buf, int size);
    void CoreDummyData(struct SpeCoreContext *context, struct ContextSwitchData *data, int size, int pageSize);
    void UpdateProcMap(__u32 ppid, __u32 pid);

    const unsigned short NONE = 0;
    const unsigned short OPENED = 1 << 0;
    const unsigned short ENABLED = 1 << 1;
    const unsigned short DISABLED = 1 << 2;
    const unsigned short READ = 1 << 3;

    int cpu = 0;
    SpeContext *ctx = nullptr;
    unsigned short status = NONE;
    int dummyFd = 0;
    int fd = 0;
    SpeRecord *records = nullptr;
    ContextSwitchData *dummyData = nullptr;
    std::map<pid_t, std::vector<SpeRecord *>> pidRecords;
    std::unordered_map<pid_t, std::shared_ptr<ProcTopology>> &procMap;
};

#endif
