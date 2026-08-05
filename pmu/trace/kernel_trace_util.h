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
 * Create: 2026-08-07
 * Description: Internal utilities for the raw kernel trace backend
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "kernel_trace_manager.h"

namespace kernel_trace {
struct FunctionSelectionStats {
    size_t includeMatched = 0;
    size_t excludeMatched = 0;
};

struct RawFunctionGraphEvent {
    int64_t timestamp = 0;
    uint64_t address = 0;
    int tid = 0;
    int cpu = 0;
    unsigned isRet = 0;
};


constexpr uint32_t K_DEFAULT_BUFFER_SIZE_KB = 1024;
constexpr uint32_t K_MAX_BUFFER_SIZE_KB = 16U * 1024U;


// Read the kernel online CPU list (for example, "0-3,8").
bool ReadOnlineCpus(std::vector<int> &cpus);

// find and return the mounted tracefs root path
std::string FindTraceFsRoot();

// check whether the tracefs supports the ftracer
bool HasRawKernelTraceSupport(const std::string &traceFsRoot);

// acquire the global tracefs lock and prepare the trace environment
bool AcquireGlobalTraceFs(KernelTraceManager::Session &session, std::string *globalNotraceFilter, std::string *error);

// restore the global tracefs configuration and release resources
void ResetGlobalTraceFs(KernelTraceManager::Session &session);

// select traceable kernel functions according to include and exclude rules
bool SelectTraceableFunctions(const std::vector<std::string> &requested,
    const std::vector<std::string> &includePatterns, const std::vector<std::string> &excludePatterns,
    const std::unordered_set<std::string> &available,
    const std::string &availablePath, std::vector<std::string> &selected, FunctionSelectionStats *stats);

// load available kernel functions from the ftrace filter file
bool LoadAvailableKernelFunctions(const std::string &path, std::unordered_set<std::string> &functions);

// apply global notrace rules to remove unnecessary functions
bool ApplyGlobalNotraceFilter(std::vector<std::string> &functions, const std::string &globalNotraceFilter,
    std::string *error);

// resolve kernel function addresses from symbol information
std::unordered_map<std::string, uint64_t> ResolveKernelSymbols(const std::vector<std::string> &functions);
// Resolve dynamic-ftrace patch sites, which may differ from symbol starts.
std::unordered_map<std::string, uint64_t> ResolveKernelPatchSites(const std::string &traceFsRoot,
    const std::vector<std::string> &functions);

// configure global tracefs settings for raw kernel tracing
bool ConfigureGlobalTraceFs(KernelTraceManager::Session &session, const std::vector<std::string> &functions,
    uint32_t bufferSizeKb, std::string *error);

// configure the target thread IDs for ftrace filtering
bool WritePidFilter(KernelTraceManager::Session &session, std::string *error);

// prepare trace configuration before starting collection
bool PrepareCollection(KernelTraceManager::Session &session, std::string *error);

// enable or disable kernel tracing
bool SetTracing(KernelTraceManager::Session &session, bool enabled, std::string *error);

// Start/stop streaming binary ftrace entry/exit records from per-CPU trace_pipe_raw files.
bool StartRawTraceStream(KernelTraceManager::Session &session, std::string *error);
bool StopRawTraceStream(KernelTraceManager::Session &session, std::string *error);
bool TakeRawTraceEvents(KernelTraceManager::Session &session, std::vector<RawFunctionGraphEvent> &events,
    std::unordered_map<int, std::string> *commByTid, uint64_t *rawBytes, std::string *error);

// set the current ftrace tracer type
void SetCurrentTracer(KernelTraceManager::Session &session, const std::string &tracer);

// read the number of lost trace events
uint64_t ReadLostEvents(const KernelTraceManager::Session &session);

// resolve the process ID (TGID) corresponding to a thread ID
int ResolveTgid(const KernelTraceManager::Session &session, int tid);
} // namespace kernel_trace
