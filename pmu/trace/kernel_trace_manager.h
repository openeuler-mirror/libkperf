/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Wu
 * Create: 2026-07-23
 * Description: Definition of raw kernel trace session, process and data
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include "pmu.h"

namespace kernel_trace {
struct RawTraceStream;
}

struct KernelTraceData {
    int64_t timestamp = 0;
    pid_t pid = 0;
    int tid = 0;
    int cpu = 0;
    unsigned isRet = 0;
    const char *comm = nullptr;
    const char *function = nullptr;
    uint64_t address = 0;
};

class KernelTraceManager {
public:
    static KernelTraceManager &GetInstance()
    {
        static KernelTraceManager instance;
        return instance;
    }

    KernelTraceManager(const KernelTraceManager &) = delete;
    KernelTraceManager &operator=(const KernelTraceManager &) = delete;

    int Open(const struct UTraceAttr *traceAttr, const std::vector<std::string> &functions,
        const std::vector<std::string> &includePatterns, const std::vector<std::string> &excludePatterns, uint32_t bufferSizeKb);
    int Enable(int pd);
    int Disable(int pd);
    int Read(int pd, struct KernelTraceData **traceData);
    void Close(int pd);
    void FreeData(struct KernelTraceData *traceData);

    enum class SessionState {
        OPENED,
        ENABLED,
        DISABLED,
        COLLECTED,
    };

    // save status before kernel trace
    struct SavedTraceFsState {
        std::string tracer;
        std::string clock;
        std::string functionFilter;
        std::string graphFunctionFilter;
        std::string graphNotraceFilter;
        std::string pidFilter;
        std::string notracePidFilter;
        std::string maxGraphDepth;
        std::string tracingThreshold;
        std::string tracingCpuMask;
        std::string bufferSizeKb;
        std::unordered_map<std::string, std::string> perCpuBufferSizeKb;
        std::unordered_map<std::string, std::string> options;
    };

    struct Session {
        SessionState state = SessionState::OPENED;
        int traceLockFd = -1;
        std::string traceFsPath;
        SavedTraceFsState saved;
        std::vector<int> targetPids;
        std::unordered_map<std::string, uint64_t> addresses;
        std::unordered_map<std::string, uint64_t> patchAddresses;
        std::unordered_map<int, int> tgidByTid;
        std::shared_ptr<kernel_trace::RawTraceStream> rawStream;
    };

    // the memory of data returned to users
    struct TraceBlock {
        std::vector<KernelTraceData> data;
        std::vector<std::unique_ptr<std::string>> strings;
    };

private:
    KernelTraceManager() = default;
    ~KernelTraceManager() = default;

    std::unordered_map<int, Session> sessions_;
    std::unordered_map<KernelTraceData *, TraceBlock> traceBlocks_;
};
