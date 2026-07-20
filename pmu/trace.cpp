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
#include "common.h"

#include <cstdlib>
#include <memory>
#include <string>
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

static std::unordered_map<int, JvmTraceSession> g_jvmTraceSessions;
static std::unordered_map<UTraceData *, int> g_mergedTraceLens;

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

// merge Java and native trace streams by timestamp order
static UTraceData *MergeTraceData(UTraceData *nativeData, int nativeLen, UTraceData *javaData, int javaLen)
{
    int nLen = nativeLen > 0 ? nativeLen : 0;
    int jLen = javaLen > 0 ? javaLen : 0;
    int total = nLen + jLen;
    if (total <= 0) {
        return nullptr;
    }
    if ((nLen > 0 && nativeData == nullptr) || (jLen > 0 && javaData == nullptr)) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "Trace data is null while trace length is positive");
        return nullptr;
    }

    UTraceData *merged = static_cast<UTraceData *>(std::calloc(total, sizeof(UTraceData)));
    if (merged == nullptr) {
        pcerr::New(COMMON_ERR_NOMEM, "calloc merged UTraceData failed");
        return nullptr;
    }

    int idx = 0;
    for (int i = 0; i < nLen; ++i) {
        merged[idx++] = DeepCopyTraceData(nativeData[i]);
    }
    for (int i = 0; i < jLen; ++i) {
        merged[idx++] = DeepCopyTraceData(javaData[i]);
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
        pcerr::New(LIBPERF_ERR_UTRACE_ELF_SCAN_FAILED, "No probe points resolved from ELF modules");
        pcerr::SetWarn(LIBPERF_WARN_UTRACE_ELF_SCAN_FAILED, elfScanFailures);
        return {};
    }
    if (!elfScanFailures.empty()) {
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

static int OpenNativeBackend(UTraceAttr *attr)
{
    if (attr == nullptr || attr->symSrc == nullptr || attr->numSym == 0) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "Native UTraceAttr symSrc is empty");
        return -1;
    }

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

static int UTraceOpenJvm(UTraceAttr *attr)
{
    SplitTraceAttr split = SplitSymbolsByRegex(attr);
    bool hasNative = !split.nativeSymSrc.empty();
    int pd = PmuList::GetInstance()->NewPd();
    if (pd == -1) {
        pcerr::New(LIBPERF_ERR_NO_AVAIL_PD);
        return -1;
    }

    UTraceResourceGuard parentGuard(pd);
    JvmTraceSession session;
    JvmTraceSessionGuard sessionGuard(session);
    // Java backend is mandatory. Native backend is opened only when native symbols exist
    if (hasNative) {
        UTraceAttr nativeAttr = MakeSubAttr(attr, split.nativeSymSrc);
        session.nativePd = OpenNativeBackend(&nativeAttr);
        if (session.nativePd < 0) {
            session.nativePd = -1;
            pcerr::SetWarn(LIBPERF_WARN_UTRACE_ELF_SCAN_FAILED, "OpenNativeBackend failed in JVM trace; continue with Java trace only");
        }
    }
    UTraceAttr javaAttr = MakeSubAttr(attr, split.javaSymSrc);
    session.javaPd = OpenJavaBackend(&javaAttr);
    if (session.javaPd < 0) {
        pcerr::New(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, "OpenJavaBackend failed in JVM trace");
        return -1;
    }

    PmuList::GetInstance()->FillPidList(pd, attr->numPid, attr->pidList);
    g_jvmTraceSessions[pd] = session;
    sessionGuard.commit = true;
    parentGuard.commit = true;
    return pd;
}

int UTraceOpen(struct UTraceAttr *attr)
{
    pcerr::New(SUCCESS);
    if (attr == nullptr) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "UTraceAttr cannot be null");
        return -1;
    }
    if (attr->pidList == nullptr || attr->numPid == 0) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "UTraceAttr pidList cannot be null");
        return -1;
    }
    if (attr->numSym == 0 || attr->symSrc == nullptr) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "UTraceAttr symSrc cannot be null");
        return -1;
    }
    bool isJvm = IsJvmProcess(attr->pidList[0]);
    if (isJvm) {
        return UTraceOpenJvm(attr);
    }
    return OpenNativeBackend(attr);
}

int UTraceEnable(int pd)
{
    pcerr::New(SUCCESS);

    JvmTraceSession *session = GetJvmTraceSession(pd);
    if (session != nullptr) {
        int nativeRet = 0;
        int javaRet = 0;
        // enable native first, then Java. If Java attach fails, native sampling is rolled back
        if (session->nativePd >= 0) {
            nativeRet = PmuEnable(session->nativePd);
        }
        if (nativeRet != 0) {
            return nativeRet;
        }

        if (session->javaPd >= 0) {
            javaRet = JavaTraceManager::GetInstance().Enable(session->javaPd);
        }
        if (javaRet != 0) {
            if (session->nativePd >= 0) {
                PmuDisable(session->nativePd);
            }
            return javaRet;
        }
        return 0;
    }

    return PmuEnable(pd);
}

int UTraceDisable(int pd)
{
    pcerr::New(SUCCESS);

    JvmTraceSession *session = GetJvmTraceSession(pd);
    if (session != nullptr) {
        int javaRet = 0;
        int nativeRet = 0;
        // disable native first
        if (session->javaPd >= 0) {
            javaRet = JavaTraceManager::GetInstance().Disable(session->javaPd);
        }
        if (session->nativePd >= 0) {
            nativeRet = PmuDisable(session->nativePd);
        }
        if (nativeRet != 0) {
            return nativeRet;
        }
        return javaRet;
    }

    return PmuDisable(pd);
}

int UTraceRead(int pd, struct UTraceData **traceData)
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

        if (session->nativePd >= 0) {
            PmuData *pmuData = nullptr;
            nativeLen = PmuRead(session->nativePd, &pmuData);
            if (nativeLen < 0) {
                if (javaData != nullptr) {
                    JavaTraceManager::GetInstance().FreeData(javaData);
                }
                return nativeLen;
            }
            if (nativeLen > 0) {
                if (pmuData == nullptr) {
                    if (javaData != nullptr) {
                        JavaTraceManager::GetInstance().FreeData(javaData);
                    }
                    pcerr::New(LIBPERF_ERR_NULL_POINTER, "Native PMU data is null while trace length is positive");
                    return -1;
                }
                nativeData = TraceDataManager::GetInstance().ConvertToTraceData(session->nativePd, pmuData, nativeLen);
                if (nativeData == nullptr) {
                    if (javaData != nullptr) {
                        JavaTraceManager::GetInstance().FreeData(javaData);
                    }
                    pcerr::New(LIBPERF_ERR_NULL_POINTER, "Convert native PMU data to UTraceData failed");
                    return -1;
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
        *traceData = merged;
        return totalLen;
    }

    PmuData *pmuData = nullptr;
    int len = PmuRead(pd, &pmuData);
    if (len < 0) {
        return len;
    }
    if (len == 0) {
        return 0;
    }
    if (pmuData == nullptr) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "PMU data is null while trace length is positive");
        return -1;
    }
    UTraceData *data = TraceDataManager::GetInstance().ConvertToTraceData(pd, pmuData, len);
    if (data == nullptr) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "Convert PMU data to UTraceData failed");
        return -1;
    }
    *traceData = data;
    return len;
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

void UTraceClose(int pd)
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
