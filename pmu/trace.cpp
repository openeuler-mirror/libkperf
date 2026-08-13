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
 * Author: Xie Jingwei
 * Create: 2026-01-21
 * Description: Implementation of UTrace lifecycle: open, enable/disable, read, free, and close operations
 ******************************************************************************/

#include "pmu.h"
#include "pcerr.h"
#include "pmu_list.h"
#include "elf_scanner.h"
#include "probe_registrar.h"
#include "trace_data_manager.h"
#include "probe_alias_manager.h"
#include "java_trace_manager.h"
#include "java_trace_util.h"
#include "trace_filter_config.h"
#include "trace_log.h"
#include "kernel_trace_manager.h"
#include "common.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <time.h>
#include <unordered_map>
#include <vector>

using namespace KUNPENG_PMU;

extern int CheckAttr(enum PmuTaskType collectType, struct PmuAttr *attr);
extern struct PmuTaskAttr *AssignPmuTaskParam(enum PmuTaskType collectType, struct PmuAttr *attr);
extern void PmuTaskAttrFree(PmuTaskAttr *taskAttr);

struct JvmTraceSession {
    int javaPd = -1;
    int nativePd = -1;
};

struct KernelTraceSession {
    int kernelPd = -1;
    bool kernelOnly = false;
    int64_t windowStartNs = 0;
    int64_t windowEndNs = 0;
};

struct KernelUTraceBlock {
    std::vector<UTraceData> data;
};

static std::unordered_map<int, JvmTraceSession> g_jvmTraceSessions;
static std::unordered_map<int, KernelTraceSession> g_kernelTraceSessions;
static std::unordered_map<UTraceData *, int> g_mergedTraceLens;
static std::unordered_map<int, std::string> g_traceLogSessionIds;

static JvmTraceSession *GetJvmTraceSession(int pd)
{
    auto it = g_jvmTraceSessions.find(pd);
    if (it == g_jvmTraceSessions.end()) {
        return nullptr;
    }
    return &it->second;
}

static void EraseJvmTraceSession(int pd)
{
    g_jvmTraceSessions.erase(pd);
}

static KernelTraceSession *GetKernelTraceSession(int pd)
{
    auto it = g_kernelTraceSessions.find(pd);
    if (it == g_kernelTraceSessions.end()) {
        return nullptr;
    }
    return &it->second;
}

static void EraseKernelTraceSession(int pd)
{
    g_kernelTraceSessions.erase(pd);
}

struct UTraceSymbolSplit {
    std::vector<SymbolSource> userSymbols;
    std::vector<std::string> kernelFunctions;
    std::deque<std::string> configuredNativeModules;
    std::deque<std::string> configuredNativeSymbols;
};

static bool IsUnresolvedSymbolSource(const SymbolSource &source)
{
    return source.moduleName == nullptr || source.moduleName[0] == '\0' || source.symbolName == nullptr ||
        source.symbolName[0] == '\0' || std::strcmp(source.symbolName, "UNKNOWN") == 0;
}

static UTraceSymbolSplit SplitUTraceSymbols(const UTraceAttr *attr, const UTraceSymbolFilterConfig &filter)
{
    UTraceSymbolSplit split;
    if (attr == nullptr || attr->symSrc == nullptr) {
        return split;
    }
    UTraceSymbolFilterConfig attrFilter = filter;
    attrFilter.kernelIncludes.clear();
    split.userSymbols.reserve(attr->numSym);
    split.kernelFunctions.reserve(attr->numSym);
    for (unsigned i = 0; i < attr->numSym; ++i) {
        const SymbolSource &source = attr->symSrc[i];
        if (IsUnresolvedSymbolSource(source)) {
            continue;
        }
        if (source.moduleName != nullptr && (std::strcmp(source.moduleName, "[kernel]") == 0 ||
            std::strcmp(source.moduleName, "kernel") == 0 || std::strcmp(source.moduleName, "vmlinux") == 0)) {
            if (IsTraceSymbolAllowed(attrFilter, TraceSymbolDomain::KERNEL, source.moduleName, source.symbolName)) {
                split.kernelFunctions.emplace_back(source.symbolName);
            }
        } else {
            split.userSymbols.emplace_back(source);
        }
    }
    return split;
}

static size_t FilterNativeSymbols(std::vector<SymbolSource> &symbols, const UTraceSymbolFilterConfig &filter)
{
    size_t before = symbols.size();
    symbols.erase(std::remove_if(symbols.begin(), symbols.end(), [&filter](const SymbolSource &source) {
        std::string module = source.moduleName == nullptr ? "" : source.moduleName;
        std::string symbol = source.symbolName == nullptr ? "" : source.symbolName;
        return module == "UNKNOWN" || symbol == "UNKNOWN" ||
            !IsTraceSymbolAllowed(filter, TraceSymbolDomain::NATIVE, module, symbol);}), symbols.end());
    return before - symbols.size();
}

static bool IsLiteralNativeIncludeRule(const std::string &rule, std::string &module, std::string &symbol)
{
    size_t separator = rule.rfind("::");
    if (separator == std::string::npos) {
        return false;
    }
    module = rule.substr(0, separator);
    int charLength = 2;
    symbol = rule.substr(separator + charLength);
    return !module.empty() && module.front() == '/' && !symbol.empty() &&
           module.find_first_of("*?[") == std::string::npos && symbol.find_first_of("*?[") == std::string::npos;
}

static size_t AppendConfiguredNativeSymbols(UTraceSymbolSplit &split, const UTraceSymbolFilterConfig &filter)
{
    size_t appended = 0;
    for (const std::string &rule : filter.nativeIncludes) {
        std::string module;
        std::string symbol;
        if (!IsLiteralNativeIncludeRule(rule, module, symbol)) {
            continue;
        }
        bool alreadyPresent = std::any_of(split.userSymbols.begin(), split.userSymbols.end(),
            [&module, &symbol](const SymbolSource &source) {
                return source.moduleName != nullptr && source.symbolName != nullptr &&
                       module == source.moduleName && symbol == source.symbolName;
            });
        if (alreadyPresent) {
            continue;
        }
        split.configuredNativeModules.emplace_back(std::move(module));
        split.configuredNativeSymbols.emplace_back(std::move(symbol));
        SymbolSource source = {0};
        source.moduleName = const_cast<char *>(split.configuredNativeModules.back().c_str());
        source.symbolName = const_cast<char *>(split.configuredNativeSymbols.back().c_str());
        split.userSymbols.emplace_back(source);
        ++appended;
    }
    return appended;
}

static void AppendKernelTraceData(KernelUTraceBlock &block, const KernelTraceData *data, int len)
{
    if (data == nullptr || len <= 0) {
        return;
    }
    block.data.reserve(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) {
        UTraceData event = {0};
        event.addr = static_cast<unsigned long>(data[i].address);
        event.comm = data[i].comm;
        event.tid = data[i].tid;
        event.cpu = data[i].cpu;
        event.timestamp = data[i].timestamp;
        event.module = "[kernel]";
        event.func = data[i].function == nullptr ? "" : data[i].function;
        event.isRet = data[i].isRet;
        block.data.emplace_back(event);
    }
}

static int64_t GetMonotonicTimeNs()
{
    struct timespec timestamp = {};
    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0) {
        return 0;
    }
    static constexpr int64_t K_NS_PER_SECOND = 1000000000LL;
    return static_cast<int64_t>(timestamp.tv_sec) * K_NS_PER_SECOND + timestamp.tv_nsec;
}

static bool IsInTraceWindow(const UTraceData &data, int64_t windowStartNs, int64_t windowEndNs)
{
    if (windowStartNs <= 0 || windowEndNs < windowStartNs) {
        return true;
    }
    return data.timestamp >= windowStartNs && data.timestamp <= windowEndNs;
}

static int CountTraceDataInWindow(const UTraceData *data, int len, int64_t windowStartNs, int64_t windowEndNs)
{
    if (data == nullptr || len <= 0) {
        return 0;
    }
    return static_cast<int>(std::count_if(data, data + len,
        [windowStartNs, windowEndNs](const UTraceData &event) {
            return IsInTraceWindow(event, windowStartNs, windowEndNs);
        }));
}

static UTraceData *MergeTraceData(UTraceData *nativeData, int nativeLen, UTraceData *javaData, int javaLen,
    UTraceData *kernelData = nullptr, int kernelLen = 0, int64_t windowStartNs = 0, int64_t windowEndNs = 0)
{
    if ((nativeLen > 0 && nativeData == nullptr) || (javaLen > 0 && javaData == nullptr) ||
        (kernelLen > 0 && kernelData == nullptr)) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "Trace data is null while trace length is positive");
        return nullptr;
    }
    int nLen = CountTraceDataInWindow(nativeData, nativeLen, windowStartNs, windowEndNs);
    int jLen = CountTraceDataInWindow(javaData, javaLen, windowStartNs, windowEndNs);
    int kLen = CountTraceDataInWindow(kernelData, kernelLen, windowStartNs, windowEndNs);
    int total = nLen + jLen + kLen;
    if (total <= 0) {
        return nullptr;
    }
    UTraceData *merged = static_cast<UTraceData *>(std::calloc(total, sizeof(UTraceData)));
    if (merged == nullptr) {
        pcerr::New(COMMON_ERR_NOMEM, "calloc merged UTraceData failed");
        return nullptr;
    }

    int idx = 0;
    for (int i = 0; i < nativeLen; ++i) {
        if (IsInTraceWindow(nativeData[i], windowStartNs, windowEndNs)) {
            merged[idx++] = DeepCopyTraceData(nativeData[i]);
        }
    }
    for (int i = 0; i < javaLen; ++i) {
        if (IsInTraceWindow(javaData[i], windowStartNs, windowEndNs)) {
            merged[idx++] = DeepCopyTraceData(javaData[i]);
        }
    }
    for (int i = 0; i < kernelLen; ++i) {
        if (IsInTraceWindow(kernelData[i], windowStartNs, windowEndNs)) {
            merged[idx++] = DeepCopyTraceData(kernelData[i]);
        }
    }
    SortTraceDataByTimestamp(merged, static_cast<size_t>(total));
    g_mergedTraceLens[merged] = total;
    return merged;
}

static bool FreeMergedTraceData(UTraceData *traceData)
{
    auto it = g_mergedTraceLens.find(traceData);
    if (it == g_mergedTraceLens.end()) {
        return false;
    }

    int len = it->second;
    g_mergedTraceLens.erase(it);
    for (int i = 0; i < len; ++i) {
        FreeTraceDataFields(traceData[i]);
    }
    std::free(traceData);
    return true;
}

struct UTraceResourceGuard {
    int pd;
    bool probesInstalled = false;
    bool commit = false;
    int pendingError = SUCCESS;
    std::string pendingErrMsg;

    explicit UTraceResourceGuard(int pdIn) : pd(pdIn) {}

    void SetError(int err, const std::string &msg)
    {
        pendingError = err;
        pendingErrMsg = msg;
    }

    ~UTraceResourceGuard()
    {
        if (commit) {
            return;
        }

        PmuList::GetInstance()->Close(pd);

        if (probesInstalled) {
            ProbeRegistrar::GetInstance().UninstallProbes(pd);
        }

        ProbeRegistrar::GetInstance().EraseProbeEvents(pd);
        TraceDataManager::GetInstance().Erase(pd);
        ProbeAliasManager::GetInstance().Erase(pd);
        EraseJvmTraceSession(pd);

        if (pendingError != SUCCESS) {
            pcerr::New(pendingError, pendingErrMsg);
        }
    }
};

static void CloseNativeBackendPd(int &pd)
{
    if (pd < 0) {
        return;
    }
    int oldPd = pd;
    pd = -1;
    UTraceResourceGuard guard(oldPd);
    guard.probesInstalled = true;
}

static void CloseJavaBackendPd(int &pd)
{
    if (pd < 0) {
        return;
    }
    int oldPd = pd;
    pd = -1;
    JavaTraceManager::GetInstance().Close(oldPd);
    PmuList::GetInstance()->Close(oldPd);
}

struct JvmTraceSessionGuard {
    JvmTraceSession &session;
    bool commit = false;

    explicit JvmTraceSessionGuard(JvmTraceSession &s) : session(s) {}

    ~JvmTraceSessionGuard()
    {
        if (commit) {
            return;
        }
        CloseJavaBackendPd(session.javaPd);
        CloseNativeBackendPd(session.nativePd);
    }
};

static auto GetProbePoints(const UTraceAttr *attr)
    -> std::unordered_map<std::string, std::vector<ProbePoints>>
{
    std::unordered_map<std::string, std::vector<std::string>> module2Symbols;
    for (unsigned i = 0; i < attr->numSym; ++i) {
        const char *module = attr->symSrc[i].moduleName;
        const char *symbol = attr->symSrc[i].symbolName;
        if (module == nullptr || symbol == nullptr ||
            module[0] == '\0' || symbol[0] == '\0') {
            continue;
        }
        module2Symbols[module].emplace_back(symbol);
    }

    auto module2ProbePoints = ElfScanner::ResolveElfs(module2Symbols);
    std::string elfScanFailures = ElfScanner::FormatFailures();
    if (module2ProbePoints.empty()) {
        TraceLog(MakeLogMessage("[trace-native] error: no probe points resolved; failures=", elfScanFailures, "\n"));
        pcerr::New(LIBPERF_ERR_UTRACE_ELF_SCAN_FAILED, "No probe points resolved from ELF modules");
        pcerr::SetWarn(LIBPERF_WARN_UTRACE_ELF_SCAN_FAILED, elfScanFailures);
        return {};
    }
    if (!elfScanFailures.empty()) {
        TraceLog(MakeLogMessage("[trace-native] warning: ELF scan failures: ", elfScanFailures, "\n"));
        pcerr::SetWarn(LIBPERF_WARN_UTRACE_ELF_SCAN_FAILED, elfScanFailures);
    }
    return module2ProbePoints;
}

static void GetEvtList(int pd, std::vector<std::string>& evtListCache, std::vector<char*>& evtPtrList)
{
    const auto &probeEvents = ProbeRegistrar::GetInstance().GetProbeEvents(pd);
    evtListCache.reserve(probeEvents.size());
    evtPtrList.reserve(probeEvents.size());

    for (const auto &probeEvent : probeEvents) {
        evtListCache.emplace_back(probeEvent.groupName + ":" + probeEvent.eventName);
        evtPtrList.push_back(const_cast<char *>(evtListCache.back().c_str()));
    }
}

static int OpenNativeBackend(UTraceAttr *attr, uint32_t samplePeriod)
{
    if (attr == nullptr || attr->symSrc == nullptr || attr->numSym == 0) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "Native UTraceAttr symSrc is empty");
        return -1;
    }

    TraceLog("[trace-native] sampling period=" + std::to_string(samplePeriod) + "\n");

    auto module2ProbePoints = GetProbePoints(attr);
    if (module2ProbePoints.empty()) {
        return -1;
    }

    int pd = PmuList::GetInstance()->NewPd();
    if (pd == -1) {
        pcerr::New(LIBPERF_ERR_NO_AVAIL_PD);
        return -1;
    }

    UTraceResourceGuard guard(pd);

    ProbeRegistrar::GetInstance().ConvertToProbeEvents(pd, module2ProbePoints);

    if (!ProbeRegistrar::GetInstance().InstallProbes(pd, attr->fetchG)) {
        guard.SetError(Perrorno(), Perror());
        return -1;
    }

    guard.probesInstalled = true;

    std::vector<std::string> evtListCache;
    std::vector<char *> evtPtrList;
    GetEvtList(pd, evtListCache, evtPtrList);

    PmuAttr pmuAttr = {0};
    pmuAttr.evtList = evtPtrList.data();
    pmuAttr.numEvt = static_cast<unsigned>(evtPtrList.size());
    pmuAttr.pidList = attr->pidList;
    pmuAttr.numPid = attr->numPid;
    pmuAttr.perThread = 1;
    pmuAttr.period = samplePeriod;

    int err = CheckAttr(SAMPLING, &pmuAttr);
    if (err != SUCCESS) {
        guard.SetError(err, Perror());
        return -1;
    }

    std::unique_ptr<PmuTaskAttr, decltype(&PmuTaskAttrFree)> pmuTaskAttrHead(
        AssignPmuTaskParam(SAMPLING, &pmuAttr), PmuTaskAttrFree);
    if (!pmuTaskAttrHead) {
        guard.SetError(Perrorno(), Perror());
        return -1;
    }

    PmuList::GetInstance()->FillPidList(pd, pmuAttr.numPid, pmuAttr.pidList);
    PmuList::GetInstance()->SetSymbolMode(pd, RESOLVE_ELF);
    PmuList::GetInstance()->SetAnalysisStatus(pd, GOING_RESOLVE);

    err = PmuList::GetInstance()->Register(pd, pmuTaskAttrHead.get());
    if (err != SUCCESS) {
        guard.SetError(err, Perror());
        return -1;
    }

    TraceDataManager::GetInstance().SetFetchG(pd, attr->fetchG);

    guard.commit = true;
    return pd;
}

static int OpenJavaBackend(UTraceAttr *attr)
{
    if (attr == nullptr || attr->pidList == nullptr || attr->numPid == 0) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "Java UTraceAttr pidList is empty");
        return -1;
    }
    int pd = PmuList::GetInstance()->NewPd();
    if (pd == -1) {
        pcerr::New(LIBPERF_ERR_NO_AVAIL_PD);
        return -1;
    }

    // if java symbol list is empty, includeRules will be nullptr
    std::string includeRules = BuildJavaSymSrc(attr);
    int err = JavaTraceManager::GetInstance().Open(pd, attr->pidList[0], includeRules.empty() ? nullptr : includeRules.c_str());
    if (err != 0) {
        PmuList::GetInstance()->Close(pd);
        return -1;
    }
    return pd;
}

static void WarnOptionalJvmNativeTraceFailure(int warning, const char *operation,
                                              int error, const std::string &errorMessage)
{
    std::string detail = "Native JVM trace " + std::string(operation) + " failed";
    if (!errorMessage.empty()) {
        detail += ": " + errorMessage;
    } else if (error != SUCCESS) {
        detail += ": error=" + std::to_string(error);
    }
    pcerr::SetWarn(warning, detail);
    TraceLog("[trace-native] warning: " + detail + "; continue with Java trace only\n");
    pcerr::New(SUCCESS);
}

static int UTraceOpenJvm(UTraceAttr *attr, const UTraceSymbolFilterConfig &filter, size_t nativeIncludeMatched)
{
    SplitTraceAttr split = SplitSymbolsByRegex(attr);
    size_t nativeSymbolsBeforeExclude = split.nativeSymSrc.size();
    size_t nativeExcludeMatched = FilterNativeSymbols(split.nativeSymSrc, filter);
    size_t nativeAttrSymbols = nativeSymbolsBeforeExclude >= nativeIncludeMatched ?
        nativeSymbolsBeforeExclude - nativeIncludeMatched : nativeSymbolsBeforeExclude;
    TraceLog("[trace-native] config: attr_symbols=" + std::to_string(nativeAttrSymbols) +
        ", include_matched=" + std::to_string(nativeIncludeMatched) +
        ", exclude_matched=" + std::to_string(nativeExcludeMatched) + "\n");
    bool hasNative = !split.nativeSymSrc.empty();
    int pd = PmuList::GetInstance()->NewPd();
    if (pd == -1) {
        pcerr::New(LIBPERF_ERR_NO_AVAIL_PD);
        return -1;
    }

    UTraceResourceGuard parentGuard(pd);
    JvmTraceSession session;
    JvmTraceSessionGuard sessionGuard(session);
    if (hasNative) {
        UTraceAttr nativeAttr = MakeSubAttr(attr, split.nativeSymSrc);
        session.nativePd = OpenNativeBackend(&nativeAttr, filter.nativeSamplePeriod);
        if (session.nativePd < 0) {
            int nativeErr = Perrorno();
            std::string nativeErrMsg = Perror() == nullptr ? "" : Perror();
            session.nativePd = -1;
            WarnOptionalJvmNativeTraceFailure(LIBPERF_WARN_UTRACE_ELF_SCAN_FAILED, "open", nativeErr, nativeErrMsg);
        }
    }
    UTraceAttr javaAttr = MakeSubAttr(attr, split.javaSymSrc);
    session.javaPd = OpenJavaBackend(&javaAttr);
    if (session.javaPd < 0) {
        const std::string error = "OpenJavaBackend failed in JVM trace";
        parentGuard.SetError(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, error);
        pcerr::New(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, error);
        return -1;
    }

    PmuList::GetInstance()->FillPidList(pd, attr->numPid, attr->pidList);
    g_jvmTraceSessions[pd] = session;
    sessionGuard.commit = true;
    parentGuard.commit = true;
    return pd;
}

static void WriteTraceLogSessionEnd(const std::string &id, int pd, const char *status)
{
    TraceLog(MakeLogMessage("================ [trace-session] END id=", id, ", pd=", pd,
        ", status=", status, ", time=", TimestampSuffix(), " ================\n\n"));
}

static int CommitTraceLogSession(int pd, const std::string &id)
{
    if (pd >= 0) {
        g_traceLogSessionIds[pd] = id;
    }
    return pd;
}

static void WarnOptionalKernelTraceFailure(const char *operation, int error, const std::string &errorMessage)
{
    std::string detail = "Kernel trace " + std::string(operation) + " failed";
    if (!errorMessage.empty()) {
        detail += ": " + errorMessage;
    } else if (error != SUCCESS) {
        detail += ": error=" + std::to_string(error);
    }
    pcerr::SetWarn(LIBPERF_WARN_UTRACE_KERNEL_FAILED, detail);
    TraceLog("[trace-kernel] warning: " + detail + "; continue with user trace only\n");
    pcerr::New(SUCCESS);
}

static void EndTraceLogSession(int pd, const char *status)
{
    auto it = g_traceLogSessionIds.find(pd);
    std::string id = it == g_traceLogSessionIds.end() ? "unknown" : it->second;
    if (it != g_traceLogSessionIds.end()) {
        g_traceLogSessionIds.erase(it);
    }
    WriteTraceLogSessionEnd(id, pd, status);
}

static int PrepareJavaTrace(int pd)
{
    pcerr::New(SUCCESS);
    JvmTraceSession *session = GetJvmTraceSession(pd);
    if (session == nullptr || session->javaPd < 0) {
        return 0;
    }
    return JavaTraceManager::GetInstance().Prepare(session->javaPd);
}

static void CloseUserTrace(int pd)
{
    pcerr::New(SUCCESS);

    JvmTraceSession *session = GetJvmTraceSession(pd);
    if (session != nullptr) {
        CloseJavaBackendPd(session->javaPd);
        CloseNativeBackendPd(session->nativePd);
        EraseJvmTraceSession(pd);
        UTraceResourceGuard parentGuard(pd);
        parentGuard.probesInstalled = false;
        return;
    }

    UTraceResourceGuard guard(pd);
    guard.probesInstalled = true;
}

int UTraceOpen(struct UTraceAttr *attr)
{
    pcerr::SetWarn(SUCCESS);
    pcerr::New(SUCCESS);
    if (attr == nullptr) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "UTraceAttr cannot be null");
        return -1;
    }
    if (attr->pidList == nullptr || attr->numPid == 0) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "UTraceAttr pidList cannot be null");
        return -1;
    }
    TraceLog("[trace] UTraceOpen target pid=" + std::to_string(attr->pidList[0]) + "\n");

    UTraceSymbolFilterConfig filter = LoadUTraceSymbolFilterConfig(FilterConfigPath());
    if (!filter.valid) {
        pcerr::New(LIBPERF_ERR_INVALID_TRACE_CONF, filter.error);
        return -1;
    }
    UTraceSymbolSplit symbols = SplitUTraceSymbols(attr, filter);
    size_t nativeAttrSymbols = symbols.userSymbols.size();
    size_t nativeIncludeMatched = AppendConfiguredNativeSymbols(symbols, filter);
    UTraceAttr userAttr = *attr;
    userAttr.symSrc = symbols.userSymbols.empty() ? nullptr : symbols.userSymbols.data();
    userAttr.numSym = static_cast<unsigned>(symbols.userSymbols.size());
    SplitTraceAttr split = SplitSymbolsByRegex(&userAttr);
    bool hasJavaSymbols = !split.javaSymSrc.empty();
    bool hasJavaConfig = !filter.javaIncludes.empty();
    bool isJvm = (hasJavaSymbols || hasJavaConfig) && IsJvmProcess(attr->pidList[0]);
    if (hasJavaSymbols && !isJvm) {
        pcerr::New(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, "Java symbols were requested, but target process is not a JVM");
        return -1;
    }
    bool hasJavaRequest = hasJavaSymbols || (hasJavaConfig && isJvm);
    if (!hasJavaRequest) {
        size_t nativeExcludeMatched = FilterNativeSymbols(symbols.userSymbols, filter);
        TraceLog("[trace-native] config: attr_symbols=" + std::to_string(nativeAttrSymbols) +
            ", include_matched=" + std::to_string(nativeIncludeMatched) +
            ", exclude_matched=" + std::to_string(nativeExcludeMatched) + "\n");
    }
    TraceLog("[trace-java] decision: attr_symbols=" + std::to_string(split.javaSymSrc.size()) +
        ", config_includes=" + std::to_string(filter.javaIncludes.size()) +
        ", enabled=" + std::string(hasJavaRequest ? "true" : "false") + "\n");
    bool hasUserTrace = !symbols.userSymbols.empty() || hasJavaRequest;
    bool hasKernelTrace = !symbols.kernelFunctions.empty() || !filter.kernelIncludes.empty();
    if (!hasUserTrace && !hasKernelTrace) {
        pcerr::New(LIBPERF_ERR_INVALID_TRACE_CONF, "UTraceAttr symSrc has no symbols allowed by trace_filter.conf");
        return -1;
    }

    std::string traceLogSessionId = TimestampSuffix();
    TraceLog(MakeLogMessage("\n======================================= [trace-session] START id=",
        traceLogSessionId, " =======================================\n"));
    int userPd = -1;
    if (hasUserTrace) {
        userPd = hasJavaRequest ? UTraceOpenJvm(&userAttr, filter, nativeIncludeMatched) :
            OpenNativeBackend(&userAttr, filter.nativeSamplePeriod);
        if (userPd < 0) {
            WriteTraceLogSessionEnd(traceLogSessionId, -1, "open_failed");
            return -1;
        }
    }
    if (!hasKernelTrace) {
        return CommitTraceLogSession(userPd, traceLogSessionId);
    }

    if (hasJavaRequest && userPd >= 0 && PrepareJavaTrace(userPd) != 0) {
        int err = Perrorno();
        std::string errMsg = Perror() == nullptr ? "" : Perror();
        CloseUserTrace(userPd);
        if (err != SUCCESS) {
            pcerr::New(err, errMsg);
        }
        WriteTraceLogSessionEnd(traceLogSessionId, -1, "open_failed");
        return -1;
    }

    int kernelPd = KernelTraceManager::GetInstance().Open(attr, symbols.kernelFunctions, filter.kernelIncludes,
        filter.kernelExcludes, filter.kernelFtraceBufferSizeKb);
    if (kernelPd < 0) {
        int err = Perrorno();
        std::string errMsg = Perror() == nullptr ? "" : Perror();
        if (userPd >= 0) {
            WarnOptionalKernelTraceFailure("open", err, errMsg);
            return CommitTraceLogSession(userPd, traceLogSessionId);
        }
        if (err != SUCCESS) {
            pcerr::New(err, errMsg);
        }
        WriteTraceLogSessionEnd(traceLogSessionId, -1, "open_failed");
        return -1;
    }

    int pd = userPd >= 0 ? userPd : kernelPd;
    KernelTraceSession session;
    session.kernelPd = kernelPd;
    session.kernelOnly = userPd < 0;
    g_kernelTraceSessions[pd] = session;
    return CommitTraceLogSession(pd, traceLogSessionId);
}

static int EnableUserTrace(int pd)
{
    pcerr::New(SUCCESS);

    JvmTraceSession *session = GetJvmTraceSession(pd);
    if (session != nullptr) {
        int nativeRet = 0;
        int javaRet = 0;
        if (session->nativePd >= 0) {
            nativeRet = PmuEnable(session->nativePd);
        }
        if (nativeRet != 0) {
            int nativeErr = Perrorno();
            std::string nativeErrMsg = Perror() == nullptr ? "" : Perror();
            CloseNativeBackendPd(session->nativePd);
            WarnOptionalJvmNativeTraceFailure(LIBPERF_WARN_UTRACE_NATIVE_READ_FAILED, "enable", nativeErr, nativeErrMsg);
        }
        if (session->javaPd >= 0) {
            javaRet = JavaTraceManager::GetInstance().Enable(session->javaPd);
        }
        if (javaRet != 0) {
            int javaErr = Perrorno();
            std::string javaErrMsg = Perror() == nullptr ? "" : Perror();
            if (session->nativePd >= 0) {
                PmuDisable(session->nativePd);
            }
            if (javaErr != SUCCESS) {
                pcerr::New(javaErr, javaErrMsg);
            }
            return javaRet;
        }
        return 0;
    }

    return PmuEnable(pd);
}

static int DisableUserTrace(int pd)
{
    pcerr::New(SUCCESS);

    JvmTraceSession *session = GetJvmTraceSession(pd);
    if (session != nullptr) {
        int javaRet = 0;
        int nativeRet = 0;
        int javaErr = SUCCESS;
        std::string javaErrMsg;
        if (session->javaPd >= 0) {
            javaRet = JavaTraceManager::GetInstance().Disable(session->javaPd);
            if (javaRet != 0) {
                javaErr = Perrorno();
                javaErrMsg = Perror() == nullptr ? "" : Perror();
            }
        }
        if (session->nativePd >= 0) {
            nativeRet = PmuDisable(session->nativePd);
        }
        if (nativeRet != 0) {
            int nativeErr = Perrorno();
            std::string nativeErrMsg = Perror() == nullptr ? "" : Perror();
            CloseNativeBackendPd(session->nativePd);
            WarnOptionalJvmNativeTraceFailure(LIBPERF_WARN_UTRACE_NATIVE_READ_FAILED, "disable", nativeErr, nativeErrMsg);
        }
        if (javaRet != 0) {
            if (javaErr != SUCCESS) {
                pcerr::New(javaErr, javaErrMsg);
            }
            return javaRet;
        }
        return 0;
    }

    return PmuDisable(pd);
}

static int ReadUserTrace(int pd, struct UTraceData **traceData)
{
    pcerr::New(SUCCESS);
    if (traceData == nullptr) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "UTraceData cannot be null");
        return -1;
    }

    *traceData = nullptr;
    JvmTraceSession *session = GetJvmTraceSession(pd);
    if (session != nullptr) {
        UTraceData *javaData = nullptr;
        UTraceData *nativeData = nullptr;
        int javaLen = 0;
        int nativeLen = 0;
        if (session->javaPd >= 0) {
            int err = JavaTraceManager::GetInstance().Read(session->javaPd, &javaData, &javaLen);
            if (err != 0) {
                return -1;
            }
            if (javaLen < 0) {
                return javaLen;
            }
            if (javaLen > 0 && javaData == nullptr) {
                pcerr::New(LIBPERF_ERR_NULL_POINTER, "Java trace data is null while trace length is positive");
                return -1;
            }
        }

        auto continueWithJavaOnly = [session, &nativeData, &nativeLen](const char *operation, int nativeErr,
                                    const std::string &nativeErrMsg) {
            CloseNativeBackendPd(session->nativePd);
            nativeData = nullptr;
            nativeLen = 0;
            WarnOptionalJvmNativeTraceFailure(LIBPERF_WARN_UTRACE_NATIVE_READ_FAILED, operation, nativeErr, nativeErrMsg);
        };

        if (session->nativePd >= 0) {
            PmuData *pmuData = nullptr;
            int nativePmuLen = PmuRead(session->nativePd, &pmuData);
            if (nativePmuLen < 0) {
                int nativeErr = Perrorno();
                std::string nativeErrMsg = Perror() == nullptr ? "" : Perror();
                continueWithJavaOnly("read", nativeErr, nativeErrMsg);
            } else if (nativePmuLen > 0) {
                if (pmuData == nullptr) {
                    const std::string reason = "Native PMU data is null while trace length is positive";
                    continueWithJavaOnly("read", LIBPERF_ERR_NULL_POINTER, reason);
                } else {
                    nativeData = TraceDataManager::GetInstance().ConvertToTraceData(
                        session->nativePd, pmuData, nativePmuLen, &nativeLen);
                    if (nativeData == nullptr) {
                        PmuDataFree(pmuData);
                        const std::string reason ="Convert native PMU data to UTraceData produced no records";
                        continueWithJavaOnly("read", LIBPERF_ERR_NULL_POINTER, reason);
                    }
                }
            }
        }
        // merge native trace data and java trace data
        int totalLen = (nativeLen > 0 ? nativeLen : 0) + (javaLen > 0 ? javaLen : 0);
        UTraceData *merged = MergeTraceData(nativeData, nativeLen, javaData, javaLen);
        if (nativeData != nullptr) {
            TraceDataManager::GetInstance().FreeTraceData(nativeData);
        }
        if (javaData != nullptr) {
            JavaTraceManager::GetInstance().FreeData(javaData);
        }
        if (merged == nullptr && totalLen > 0) {
            return -1;
        }
        pcerr::New(SUCCESS);
        *traceData = merged;
        return totalLen;
    }

    PmuData *pmuData = nullptr;
    int len = PmuRead(pd, &pmuData);
    if (len < 0) {
        TraceLog(MakeLogMessage("[trace-native] error: PmuRead failed: error=", Perrorno(), " (", Perror(), ")\n"));
        return len;
    }
    if (len == 0) {
        return 0;
    }
    if (pmuData == nullptr) {
        TraceLog("[trace-native] error: PMU data is null while trace length is positive\n");
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "PMU data is null while trace length is positive");
        return -1;
    }
    int convertedLen = 0;
    UTraceData *data = TraceDataManager::GetInstance().ConvertToTraceData(pd, pmuData, len, &convertedLen);
    if (data == nullptr) {
        PmuDataFree(pmuData);
        TraceLog("[trace-native] error: convert PMU data to UTraceData failed\n");
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "Convert PMU data to UTraceData failed");
        return -1;
    }
    *traceData = data;
    return convertedLen;
}

void UTraceDataFree(struct UTraceData *traceData)
{
    if (traceData == nullptr) {
        return;
    }

    if (FreeMergedTraceData(traceData)) {
        return;
    }

    TraceDataManager::GetInstance().FreeTraceData(traceData);
}

int UTraceEnable(int pd)
{
    pcerr::New(SUCCESS);

    KernelTraceSession *kernelSession = GetKernelTraceSession(pd);
    if (kernelSession == nullptr) {
        return EnableUserTrace(pd);
    }
    if (kernelSession->kernelOnly) {
        int ret = KernelTraceManager::GetInstance().Enable(kernelSession->kernelPd);
        if (ret == 0) {
            kernelSession->windowStartNs = GetMonotonicTimeNs();
            kernelSession->windowEndNs = 0;
        }
        return ret;
    }

    int userRet = EnableUserTrace(pd);
    if (userRet != 0) {
        return userRet;
    }
    int kernelRet = KernelTraceManager::GetInstance().Enable(kernelSession->kernelPd);
    if (kernelRet != 0) {
        int err = Perrorno();
        std::string errMsg = Perror() == nullptr ? "" : Perror();
        int kernelPd = kernelSession->kernelPd;
        KernelTraceManager::GetInstance().Close(kernelPd);
        EraseKernelTraceSession(pd);
        WarnOptionalKernelTraceFailure("enable", err, errMsg);
        return 0;
    }
    kernelSession->windowStartNs = GetMonotonicTimeNs();
    kernelSession->windowEndNs = 0;
    return 0;
}

int UTraceDisable(int pd)
{
    pcerr::New(SUCCESS);

    KernelTraceSession *kernelSession = GetKernelTraceSession(pd);
    if (kernelSession == nullptr) {
        return DisableUserTrace(pd);
    }
    if (kernelSession->windowStartNs > 0) {
        kernelSession->windowEndNs = GetMonotonicTimeNs();
    }
    if (kernelSession->kernelOnly) {
        return KernelTraceManager::GetInstance().Disable(kernelSession->kernelPd);
    }

    int kernelRet = KernelTraceManager::GetInstance().Disable(kernelSession->kernelPd);
    int kernelErr = Perrorno();
    std::string kernelErrMsg = Perror() == nullptr ? "" : Perror();
    int userRet = DisableUserTrace(pd);
    int userErr = Perrorno();
    std::string userErrMsg = Perror() == nullptr ? "" : Perror();
    if (kernelRet != 0) {
        int kernelPd = kernelSession->kernelPd;
        KernelTraceManager::GetInstance().Close(kernelPd);
        EraseKernelTraceSession(pd);
        WarnOptionalKernelTraceFailure("disable", kernelErr, kernelErrMsg);
        if (userRet != 0 && userErr != SUCCESS) {
            pcerr::New(userErr, userErrMsg);
        }
        return userRet;
    }
    if (userRet != 0 && userErr != SUCCESS) {
        pcerr::New(userErr, userErrMsg);
    }
    return userRet;
}

int UTraceRead(int pd, struct UTraceData **traceData)
{
    pcerr::New(SUCCESS);
    if (traceData == nullptr) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "UTraceData cannot be null");
        return -1;
    }
    *traceData = nullptr;

    KernelTraceSession *kernelSession = GetKernelTraceSession(pd);
    if (kernelSession == nullptr) {
        return ReadUserTrace(pd, traceData);
    }

    UTraceData *userData = nullptr;
    int userLen = 0;
    if (!kernelSession->kernelOnly) {
        userLen = ReadUserTrace(pd, &userData);
        if (userLen < 0) {
            return userLen;
        }
    }

    KernelTraceData *kernelData = nullptr;
    int kernelLen = KernelTraceManager::GetInstance().Read(kernelSession->kernelPd, &kernelData);
    if (kernelLen < 0) {
        int kernelErr = Perrorno();
        std::string kernelErrMsg = Perror() == nullptr ? "" : Perror();
        if (kernelSession->kernelOnly) {
            return kernelLen;
        }
        int kernelPd = kernelSession->kernelPd;
        KernelTraceManager::GetInstance().Close(kernelPd);
        EraseKernelTraceSession(pd);
        WarnOptionalKernelTraceFailure("read", kernelErr, kernelErrMsg);
        *traceData = userData;
        return userLen;
    }

    KernelUTraceBlock kernelBlock;
    AppendKernelTraceData(kernelBlock, kernelData, kernelLen);
    int mergedKernelLen = static_cast<int>(kernelBlock.data.size());
    UTraceData *kernelTraceData = kernelBlock.data.empty() ? nullptr : kernelBlock.data.data();
    int filteredUserLen = CountTraceDataInWindow(userData, userLen,
        kernelSession->windowStartNs, kernelSession->windowEndNs);
    int filteredKernelLen = CountTraceDataInWindow(kernelTraceData, mergedKernelLen,
        kernelSession->windowStartNs, kernelSession->windowEndNs);
    int totalLen = filteredUserLen + filteredKernelLen;
    UTraceData *merged = MergeTraceData(userData, userLen, nullptr, 0, kernelTraceData, mergedKernelLen,
        kernelSession->windowStartNs, kernelSession->windowEndNs);

    if (userData != nullptr) {
        UTraceDataFree(userData);
    }
    if (kernelData != nullptr) {
        KernelTraceManager::GetInstance().FreeData(kernelData);
    }
    if (merged == nullptr && totalLen > 0) {
        return -1;
    }
    pcerr::New(SUCCESS);
    *traceData = merged;
    return totalLen;
}

void UTraceClose(int pd)
{
    pcerr::New(SUCCESS);

    KernelTraceSession *kernelSession = GetKernelTraceSession(pd);
    if (kernelSession == nullptr) {
        CloseUserTrace(pd);
        EndTraceLogSession(pd, "closed");
        return;
    }

    KernelTraceSession session = *kernelSession;
    EraseKernelTraceSession(pd);
    if (session.kernelPd >= 0) {
        KernelTraceManager::GetInstance().Close(session.kernelPd);
    }
    if (!session.kernelOnly) {
        CloseUserTrace(pd);
    }
    EndTraceLogSession(pd, "closed");
}
