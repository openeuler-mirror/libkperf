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
 * Description: Implementation of raw kernel trace session, process and data
 ******************************************************************************/

#include "kernel_trace_manager.h"
#include "kernel_trace_util.h"
#include <algorithm>
#include <atomic>
#include <utility>
#include <unordered_set>
#include "pcerr.h"
#include "trace_log.h"
#include "trace_filter_config.h"

using namespace pcerr;

namespace {
static std::atomic<int> g_nextKernelTracePd(1);


static const char *RecordDataStr(KernelTraceManager::TraceBlock &block, const std::string &value)
{
    block.strings.emplace_back(new std::string(value));
    return block.strings.back()->c_str();
}

static const char *RecordCachedDataStr(KernelTraceManager::TraceBlock &block,
    std::unordered_map<std::string, const char *> &cache, const std::string &value)
{
    auto existing = cache.find(value);
    if (existing != cache.end()) {
        return existing->second;
    }
    const char *recorded = RecordDataStr(block, value);
    cache.emplace(value, recorded);
    return recorded;
}
} // namespace

int KernelTraceManager::Open(const UTraceAttr *traceAttr, const std::vector<std::string> &functions,
    const std::vector<std::string> &includePatterns, const std::vector<std::string> &excludePatterns,
    uint32_t bufferSizeKb, bool traceIrqs)
{
    New(SUCCESS);
    if (traceAttr == nullptr || traceAttr->pidList == nullptr || traceAttr->numPid == 0) {
        New(LIBPERF_ERR_NULL_POINTER, "Kernel trace pidList is empty");
        return -1;
    }
    if (bufferSizeKb == 0) {
        bufferSizeKb = kernel_trace::K_DEFAULT_BUFFER_SIZE_KB;
    }
    if (bufferSizeKb > kernel_trace::K_MAX_BUFFER_SIZE_KB) {
        New(LIBPERF_ERR_INVALID_EVTLIST, "kernel_ftrace_buffer_size_kb must be in [1, " +
            std::to_string(kernel_trace::K_MAX_BUFFER_SIZE_KB) + "]");
        return -1;
    }

    std::string traceFsRoot = kernel_trace::FindTraceFsRoot();
    if (traceFsRoot.empty()) {
        New(LIBPERF_ERR_KERNEL_TRACE_FAILED, "Cannot find a mounted tracefs for kernel tracing");
        return -1;
    }
    if (!kernel_trace::HasRawKernelTraceSupport(traceFsRoot)) {
        New(LIBPERF_ERR_KERNEL_TRACE_FAILED, std::string("trace_pipe_raw requires the function_graph tracer") +
            ", but it is not listed in " + traceFsRoot + "/available_tracers");
        return -1;
    }

    std::string availablePath = traceFsRoot + "/available_filter_functions";
    std::unordered_set<std::string> available;
    if (!kernel_trace::LoadAvailableKernelFunctions(availablePath, available)) {
        New(LIBPERF_ERR_KERNEL_TRACE_FAILED, "Cannot read kernel functions from " + availablePath);
        return -1;
    }

    kernel_trace::FunctionSelectionStats selectionStats;
    std::vector<std::string> selected;
    bool selectionOk = kernel_trace::SelectTraceableFunctions(functions, includePatterns, excludePatterns,
                                  available, availablePath, selected, &selectionStats);
    TraceLog("[trace-kernel] filter: " + FormatTraceFilterRuleStatus(includePatterns, excludePatterns,
        selectionStats.includeRuleMatched, selectionStats.excludeRuleMatched) + "\n");
    TraceLog("[trace-kernel] options: buffer_size_kb_per_cpu=" + std::to_string(bufferSizeKb) +
        ", funcgraph_irqs=" + std::string(traceIrqs ? "true" : "false") + "\n");
    if (!selectionOk) {
        return -1;
    }

    int pd = g_nextKernelTracePd.fetch_add(1);
    Session session;
    session.traceFsPath = traceFsRoot;
    for (unsigned i = 0; i < traceAttr->numPid; ++i) {
        if (traceAttr->pidList[i] > 0) {
            session.targetPids.push_back(traceAttr->pidList[i]);
        }
    }
    if (session.targetPids.empty()) {
        New(LIBPERF_ERR_NULL_POINTER, "Kernel trace pidList has no positive PID");
        return -1;
    }

    std::string error;
    std::string globalNotraceFilter;
    if (!kernel_trace::AcquireGlobalTraceFs(session, &globalNotraceFilter, &error)) {
        New(LIBPERF_ERR_KERNEL_TRACE_FAILED, error);
        TraceLog("[trace-kernel] error: " + error + "\n");
        return -1;
    }
    if (!kernel_trace::ApplyGlobalNotraceFilter(selected, globalNotraceFilter, &error)) {
        kernel_trace::ResetGlobalTraceFs(session);
        New(LIBPERF_ERR_KERNEL_TRACE_FAILED, error);
        TraceLog("[trace-kernel] error: " + error + "\n");
        return -1;
    }
    session.addresses = kernel_trace::ResolveKernelSymbols(selected);
    session.patchAddresses = kernel_trace::ResolveKernelPatchSites(traceFsRoot, selected);
    if (!kernel_trace::ConfigureGlobalTraceFs(session, selected, bufferSizeKb, traceIrqs, &error)) {
        kernel_trace::ResetGlobalTraceFs(session);
        New(LIBPERF_ERR_KERNEL_TRACE_FAILED, error);
        TraceLog("[trace-kernel] error: " + error + "\n");
        return -1;
    }
    sessions_.emplace(pd, std::move(session));
    return pd;
}

int KernelTraceManager::Enable(int pd)
{
    New(SUCCESS);
    auto sessionIt = sessions_.find(pd);
    if (sessionIt == sessions_.end()) {
        New(LIBPERF_ERR_INVALID_PD, "Kernel trace session not found");
        return -1;
    }
    Session &session = sessionIt->second;
    if (session.state == SessionState::ENABLED) {
        return 0;
    }

    std::string error;
    auto failEnable = [&session](const std::string &step, const std::string &message) {
        kernel_trace::SetTracing(session, false, nullptr);
        kernel_trace::StopRawTraceStream(session, nullptr);
        kernel_trace::SetCurrentTracer(session, "nop");
        session.rawStream.reset();
        std::string detail = "Enable failed at " + step + ": " + message;
        TraceLog("[trace-kernel] error: " + detail + "\n");
        New(LIBPERF_ERR_KERNEL_TRACE_FAILED, detail);
        return -1;
    };

    // keep the tracer stopped while refreshing the target's live thread list and clearing stale data
    if (!kernel_trace::PrepareCollection(session, &error)) {
        return failEnable("prepare collection", error);
    }
    session.rawStream.reset();
    if (!kernel_trace::StartRawTraceStream(session, &error)) {
        return failEnable("start trace_pipe_raw reader", error);
    }
    if (!kernel_trace::SetTracing(session, true, &error)) {
        return failEnable("start tracing", error);
    }
    session.state = SessionState::ENABLED;
    return 0;
}

int KernelTraceManager::Disable(int pd)
{
    New(SUCCESS);
    auto sessionIt = sessions_.find(pd);
    if (sessionIt == sessions_.end()) {
        New(LIBPERF_ERR_INVALID_PD, "Kernel trace session not found");
        return -1;
    }
    Session &session = sessionIt->second;
    if (session.state != SessionState::ENABLED) {
        return 0;
    }

    std::string error;
    if (!kernel_trace::SetTracing(session, false, &error)) {
        kernel_trace::SetCurrentTracer(session, "nop");
        kernel_trace::StopRawTraceStream(session, nullptr);
        New(LIBPERF_ERR_KERNEL_TRACE_FAILED, error);
        return -1;
    }
    session.state = SessionState::DISABLED;
    if (!kernel_trace::StopRawTraceStream(session, &error)) {
        New(LIBPERF_ERR_KERNEL_TRACE_FAILED, error);
        TraceLog("[trace-kernel] error: " + error + "\n");
        return -1;
    }
    return 0;
}

int KernelTraceManager::Read(int pd, KernelTraceData **traceData)
{
    New(SUCCESS);
    if (traceData == nullptr) {
        New(LIBPERF_ERR_NULL_POINTER, "KernelTraceData cannot be null");
        return -1;
    }
    *traceData = nullptr;

    auto sessionIt = sessions_.find(pd);
    if (sessionIt == sessions_.end()) {
        New(LIBPERF_ERR_INVALID_PD, "Kernel trace session not found");
        return -1;
    }
    Session &session = sessionIt->second;
    if (session.state == SessionState::ENABLED) {
        New(LIBPERF_ERR_KERNEL_TRACE_FAILED, "Disable kernel trace collection before reading");
        return -1;
    }
    if (session.state == SessionState::OPENED) {
        TraceLog("[trace-kernel] warning: read requested without a completed enable/disable collection window\n");
        return 0;
    }
    if (session.state == SessionState::COLLECTED) {
        TraceLog("[trace-kernel] warning: kernel trace collection has already been read\n");
        return 0;
    }

    std::vector<kernel_trace::RawFunctionGraphEvent> rawEvents;
    std::unordered_map<int, std::string> commByTid;
    uint64_t rawBytes = 0;
    std::string error;
    if (!kernel_trace::TakeRawTraceEvents(session, rawEvents, &commByTid, &rawBytes, &error)) {
        New(LIBPERF_ERR_KERNEL_TRACE_FAILED, error);
        return -1;
    }
    uint64_t lostEvents = kernel_trace::ReadLostEvents(session);
    kernel_trace::SetCurrentTracer(session, "nop");
    session.state = SessionState::COLLECTED;

    TraceLog("[trace-kernel] Kernel raw stream: raw_bytes=" + std::to_string(rawBytes) +
        ", raw_events=" + std::to_string(rawEvents.size()) +
        ", lost_events=" + std::to_string(lostEvents) + "\n");
    if (lostEvents != 0) {
        std::string message = "Kernel trace lost " + std::to_string(lostEvents) +
            " events; returning only decoded, completely paired calls. Statistics are partial; "
            "reduce the traced function set or collection window, or increase "
            "kernel_ftrace_buffer_size_kb";
        TraceLog("[trace-kernel] warning: " + message + "\n");
        SetWarn(LIBPERF_WARN_UTRACE_KERNEL_FAILED, message);
    }

    // Per-CPU raw streams are drained independently. Merge by monotonic timestamp,
    // retaining decoder order for ties, then discard incomplete boundaries so a
    // missing record cannot poison the consumer's per-TID call stack.
    std::stable_sort(rawEvents.begin(), rawEvents.end(),
        [](const kernel_trace::RawFunctionGraphEvent &left,
            const kernel_trace::RawFunctionGraphEvent &right) {
            return left.timestamp < right.timestamp;
        });
    std::vector<bool> complete(rawEvents.size(), false);
    std::unordered_map<int, std::vector<size_t>> stacksByTid;
    uint64_t droppedEntries = 0;
    uint64_t droppedReturns = 0;
    for (size_t index = 0; index < rawEvents.size(); ++index) {
        const auto &raw = rawEvents[index];
        auto &stack = stacksByTid[raw.tid];
        if (!raw.isRet) {
            stack.push_back(index);
            continue;
        }
        auto matching = std::find_if(stack.rbegin(), stack.rend(),
            [&raw, &rawEvents](size_t entryIndex) {
                return rawEvents[entryIndex].address == raw.address;
            });
        if (matching == stack.rend()) {
            ++droppedReturns;
            continue;
        }
        size_t matchingIndex = *matching;
        while (stack.back() != matchingIndex) {
            stack.pop_back();
            ++droppedEntries;
        }
        complete[stack.back()] = true;
        complete[index] = true;
        stack.pop_back();
    }
    for (const auto &threadStack : stacksByTid) {
        droppedEntries += threadStack.second.size();
    }
    if (droppedEntries != 0 || droppedReturns != 0) {
        TraceLog("[trace-kernel] warning: dropped incomplete raw call boundaries: entries=" +
            std::to_string(droppedEntries) + ", returns=" + std::to_string(droppedReturns) + "\n");
    }

    TraceBlock block;
    block.data.reserve(static_cast<size_t>(std::count(complete.begin(), complete.end(), true)));

    std::unordered_map<uint64_t, std::string> functionByAddress;
    for (const auto &symbol : session.addresses) {
        if (symbol.second != 0) {
            functionByAddress.emplace(symbol.second, symbol.first);
        }
    }
    std::unordered_map<std::string, const char *> stringCache;
    for (size_t index = 0; index < rawEvents.size(); ++index) {
        if (!complete[index]) {
            continue;
        }
        const kernel_trace::RawFunctionGraphEvent &raw = rawEvents[index];
        KernelTraceData data;
        auto functionIt = functionByAddress.find(raw.address);
        if (functionIt == functionByAddress.end()) {
            continue;
        }
        auto commIt = commByTid.find(raw.tid);
        std::string fallbackComm = "tid-" + std::to_string(raw.tid);
        const std::string &comm = commIt == commByTid.end() ? fallbackComm : commIt->second;

        data.timestamp = raw.timestamp;
        data.pid = static_cast<pid_t>(kernel_trace::ResolveTgid(session, raw.tid));
        data.tid = raw.tid;
        data.cpu = raw.cpu;
        data.isRet = raw.isRet;
        data.comm = RecordCachedDataStr(block, stringCache, comm);
        data.function = RecordCachedDataStr(block, stringCache, functionIt->second);
        data.address = raw.address;

        block.data.push_back(data);
    }

    if (block.data.empty()) {
        return 0;
    }
    KernelTraceData *result = block.data.data();
    int length = static_cast<int>(block.data.size());
    traceBlocks_.emplace(result, std::move(block));
    *traceData = result;
    return length;
}

void KernelTraceManager::FreeData(KernelTraceData *traceData)
{
    if (traceData != nullptr) {
        traceBlocks_.erase(traceData);
    }
}

void KernelTraceManager::Close(int pd)
{
    auto sessionIt = sessions_.find(pd);
    if (sessionIt == sessions_.end()) {
        return;
    }
    kernel_trace::SetTracing(sessionIt->second, false, nullptr);
    kernel_trace::StopRawTraceStream(sessionIt->second, nullptr);
    kernel_trace::SetCurrentTracer(sessionIt->second, "nop");
    kernel_trace::ResetGlobalTraceFs(sessionIt->second);
    sessions_.erase(sessionIt);
}
