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
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <poll.h>
#include "simple-pebs.h"
#include "pmu.h"

struct PebsDriverSession {
    int cpuNum = 0;
    int driverMapSize = 0;
    std::vector<int> fds;
    std::vector<void*> maps;
    std::vector<pollfd> pfds;  // monitor POLLIN
    bool running = false;
};

class PebsDriverManager {
public:
    static PebsDriverManager& Instance() {
        static PebsDriverManager inst;
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
    PebsDriverManager() = default;

    bool ProbeDevice(int* out_size);  // check whether /dev/simple-pebs exists, readable and can get size
    bool OpenCpu(PebsDriverSession& s, int cpu);  // open set mmap for one cpu
    void StopAll(PebsDriverSession& s);           // ioctl stop for all cpus
    void ResetAll(PebsDriverSession& s);          // ioctl reset for all cpus

    std::mutex driverMu;
    std::atomic<int> nextDriverPd{kPdBase + 1};  // distinguished from PMU pd
    std::unordered_map<int, PebsDriverSession> sess;  // key: driver pd, value: PebsDriverSession
};
