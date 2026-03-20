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
 * Description: definition of LBR collection in Intel environment
 ******************************************************************************/
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <poll.h>
#include "simple-lbr-trace.h"
#include "pmu.h"

struct CpuChunk {
    int cpu = -1;
    uint64_t beginSeq = 0;
    uint64_t endSeq = 0;

    CpuChunk(int c, uint64_t b, uint64_t e)
        : cpu(c), beginSeq(b), endSeq(e) {}
};

struct LbrDriverSession {
    int fd = -1;                  // single owner fd
    int cpuNum = 0;
    int driverMapSize = 0;
    int targetPid = 0;

    std::vector<void*> maps;
    std::vector<uint64_t> consumedSeq;    // userspace consumed seq per cpu
    std::vector<uint64_t> lastTailSeq;    // last valid tail, for debug/warn
    std::vector<uint64_t> lastOverwritten;
    std::vector<uint64_t> lastFailures;
    pollfd pfd{.fd = -1, .events = POLLIN, .revents = 0};
    bool running = false;
};

class LbrDriverManager {
public:
    static LbrDriverManager& Instance() {
        static LbrDriverManager inst;
        return inst;
    }

    static constexpr int kPdBase = 0x10000000;
    static inline bool IsDriverPd(int pd) {
        return (pd & kPdBase) == kPdBase;
    }

    int Open(struct PmuAttr *attr);
    int Enable(int pd);
    int Disable(int pd);
    int Read(int pd, PmuData** out);
    void Close(int pd);

private:
    LbrDriverManager() = default;

    bool ProbeDevice(int* out_size, int* out_cpus);
    bool OpenDevice(LbrDriverSession& s, int targetPid);
    void Stop(LbrDriverSession& s);
    std::shared_ptr<LbrDriverSession> GetSession(int pid);

    std::atomic<int> nextDriverPd{kPdBase + 1};  // distinguished from PMU pd
    std::unordered_map<int, std::shared_ptr<LbrDriverSession>> sess;  // key: driver pd, value: LbrDriverSession
};
