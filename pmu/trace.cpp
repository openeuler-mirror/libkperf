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
#include "java_backend.h"
#include "common.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace KUNPENG_PMU;

extern int CheckAttr(enum PmuTaskType collectType, struct PmuAttr *attr);
extern struct PmuTaskAttr *AssignPmuTaskParam(enum PmuTaskType collectType, struct PmuAttr *attr);
extern void PmuTaskAttrFree(PmuTaskAttr *taskAttr);

struct JvmTraceSession {
    int javaPd = -1;
    int nativePd = -1;
};

struct SplitTraceAttr {
    std::vector<std::string> javaModules;
    std::vector<std::string> javaSymbols;
    std::vector<SymbolSource> javaSymSrc;

    std::vector<std::string> nativeModules;
    std::vector<std::string> nativeSymbols;
    std::vector<SymbolSource> nativeSymSrc;
};

struct JavaFunc {
    bool valid = false;
    std::string className;
    std::string methodName;
};

static std::unordered_map<int, JvmTraceSession> jvmTraceSessions;
static std::unordered_map<UTraceData *, int> mergedTraceLens;

static JvmTraceSession *GetJvmTraceSession(int pd)
{
    auto it = jvmTraceSessions.find(pd);
    return it == jvmTraceSessions.end() ? nullptr : &it->second;
}

struct UTraceResourceGuard {
    int pd;
    bool probesInstalled = false;
    bool commit = false;

    int pendingError = SUCCESS;
    std::string pendingErrMsg;

    void SetError(int err, const std::string& msg)
    {
        pendingError = err;
        pendingErrMsg = msg;
    }

    UTraceResourceGuard(int pd) : pd(pd) {}

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
        jvmTraceSessions.erase(pd);

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

    UTraceResourceGuard guard(pd);
    guard.probesInstalled = true;
    pd = -1;
}

static void CloseJavaBackendPd(int &pd)
{
    if (pd < 0) {
        return;
    }

    JavaTraceManager::GetInstance().Close(pd);
    pd = -1;
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

static JavaFunc ParseJavaFunction(const char *symbolName)
{
    JavaFunc out;
    if (symbolName == nullptr || symbolName[0] == '\0') {
        return out;
    }

    // eg: Lorg/example/Foo;::bar, Lorg/example/Foo;::bar(Ljava/lang/String;)V
    // Only class and method name are extracted.
    static const std::regex javaFuncRe(R"(L([^;]+);::([^()\s]+))");

    std::cmatch match;
    if (!std::regex_search(symbolName, match, javaFuncRe) || match.size() < 3) {
        return out;
    }

    out.valid = true;
    out.className = match[1].str();
    out.methodName = match[2].str();
    return out;
}

static void AddSplitSymbol(std::vector<std::string> &modules, std::vector<std::string> &symbols,
                           std::vector<SymbolSource> &out, const std::string &module, const std::string &symbol)
{
    modules.emplace_back(module);
    symbols.emplace_back(symbol);

    SymbolSource src = {0};
    src.moduleName = const_cast<char *>(modules.back().c_str());
    src.symbolName = const_cast<char *>(symbols.back().c_str());
    out.emplace_back(src);
}

static SplitTraceAttr SplitSymbolsByRegex(const UTraceAttr *attr)
{
    SplitTraceAttr out;
    if (attr == nullptr || attr->symSrc == nullptr || attr->numSym <= 0) {
        return out;
    }
    out.javaModules.reserve(attr->numSym);
    out.javaSymbols.reserve(attr->numSym);
    out.javaSymSrc.reserve(attr->numSym);
    out.nativeModules.reserve(attr->numSym);
    out.nativeSymbols.reserve(attr->numSym);
    out.nativeSymSrc.reserve(attr->numSym);

    for (int i = 0; i < attr->numSym; ++i) {
        SymbolSource &src = attr->symSrc[i];
        JavaFunc javaFunc = ParseJavaFunction(src.symbolName);
        if (javaFunc.valid) {
            if (javaFunc.className.empty() || javaFunc.methodName.empty()) {
                continue;
            }
            AddSplitSymbol(out.javaModules, out.javaSymbols, out.javaSymSrc, javaFunc.className, javaFunc.methodName);
            continue;
        }
        std::string module = src.moduleName == nullptr ? "" : src.moduleName;
        std::string symbol = src.symbolName == nullptr ? "" : src.symbolName;
        AddSplitSymbol(out.nativeModules, out.nativeSymbols, out.nativeSymSrc, module, symbol);
    }
    return out;
}

static UTraceAttr MakeSubAttr(const UTraceAttr *src, std::vector<SymbolSource> &symSrc)
{
    UTraceAttr out = *src;
    out.symSrc = symSrc.empty() ? nullptr : symSrc.data();
    out.numSym = static_cast<int>(symSrc.size());
    return out;
}

static std::string BuildJavaSymSrc(const UTraceAttr *attr)
{
    std::unordered_set<std::string> includes;

    for (int i = 0; i < attr->numSym; ++i) {
        const char *module = attr->symSrc[i].moduleName;
        const char *symbol = attr->symSrc[i].symbolName;

        if (module == nullptr || module[0] == '\0') {
            continue;
        }

        std::string mod(module);
        std::string sym = symbol == nullptr ? "" : symbol;

        if (!sym.empty() && sym != "*") {
            includes.emplace(mod + "/" + sym);
        } else {
            includes.emplace(mod);
        }
    }

    std::string result;
    for (const auto &include : includes) {
        if (!result.empty()) {
            result += ",";
        }
        result += include;
    }

    return result;
}

static auto GetProbePoints(const UTraceAttr *attr)
    -> std::unordered_map<std::string, std::vector<ProbePoints>>
{
    std::unordered_map<std::string, std::vector<std::string>> module2Symbols;
    for (int i = 0; i < attr->numSym; ++i) {
        module2Symbols[attr->symSrc[i].moduleName].emplace_back(attr->symSrc[i].symbolName);
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

static int OpenNativeBackend(struct UTraceAttr *attr)
{
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
    pmuAttr.numEvt = evtPtrList.size();
    pmuAttr.pidList = attr->pidList;
    pmuAttr.numPid = attr->numPid;
    pmuAttr.perThread = 1;

    int err = CheckAttr(SAMPLING, &pmuAttr);
    if (err != SUCCESS) {
        guard.SetError(err, Perror());
        return -1;
    }

    std::unique_ptr<PmuTaskAttr, void (*)(PmuTaskAttr *)> pmuTaskAttrHead(AssignPmuTaskParam(SAMPLING, &pmuAttr), PmuTaskAttrFree);
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

static int OpenJavaBackend(struct UTraceAttr *attr)
{
    int pd = PmuList::GetInstance()->NewPd();
    if (pd == -1) {
        pcerr::New(LIBPERF_ERR_NO_AVAIL_PD);
        return -1;
    }

    std::string includeRules = BuildJavaSymSrc(attr);
    int err = JavaTraceManager::GetInstance().Open(
        pd, attr->pidList[0], includeRules.empty() ? nullptr : includeRules.c_str());

    if (err != 0) {
        PmuList::GetInstance()->Close(pd);
        return -1;
    }

    return pd;
}

static int UTraceOpenJvm(struct UTraceAttr *attr)
{
    SplitTraceAttr split = SplitSymbolsByRegex(attr);
    bool hasJava = !split.javaSymSrc.empty();
    bool hasNative = !split.nativeSymSrc.empty();
    if (!hasJava && !hasNative) {
        pcerr::New(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, "No valid UTrace symbols");
        return -1;
    }
    int pd = PmuList::GetInstance()->NewPd();
    if (pd == -1) {
        pcerr::New(LIBPERF_ERR_NO_AVAIL_PD);
        return -1;
    }

    UTraceResourceGuard guard(pd);
    JvmTraceSession session;
    JvmTraceSessionGuard sessionGuard(session);
    if (hasNative) {
        UTraceAttr nativeAttr = MakeSubAttr(attr, split.nativeSymSrc);
        session.nativePd = OpenNativeBackend(&nativeAttr);
        if (session.nativePd < 0) {
            if (!hasJava) {
                pcerr::New(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, "C++ tracing failed: no Java method available");
                return -1;
            }
            session.nativePd = -1;
            pcerr::SetWarn(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, "C++ tracing failed: continue with Java tracing only");
        }
    }
    if (hasJava) {
        UTraceAttr javaAttr = MakeSubAttr(attr, split.javaSymSrc);
        session.javaPd = OpenJavaBackend(&javaAttr);
        if (session.javaPd < 0) {
            pcerr::New(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, "Java tracing failed");
            return -1;
        }
    }

    if (session.javaPd < 0 && session.nativePd < 0) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "Java tracing failed: No backend opened");
        return -1;
    }
    PmuList::GetInstance()->FillPidList(pd, attr->numPid, attr->pidList);
    jvmTraceSessions[pd] = session;

    sessionGuard.commit = true;
    guard.commit = true;
    return pd;
}

int UTraceOpen(struct UTraceAttr *attr)
{
    pcerr::New(SUCCESS);
    if (attr == nullptr) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "UTraceAttr cannot be null");
        return -1;
    }
    if (attr->numSym <= 0 || attr->symSrc == nullptr) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "UTraceAttr symSrc cannot be null");
        return -1;
    }

    bool isJvm = attr->numPid > 0 && attr->pidList != nullptr && IsJvmProcess(attr->pidList[0]);
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

        if (session->nativePd >= 0) {
            nativeRet = PmuEnable(session->nativePd);
        }

        if (session->javaPd >= 0) {
            javaRet = JavaTraceManager::GetInstance().Enable(session->javaPd);
        }

        return nativeRet != 0 ? nativeRet : javaRet;
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

        if (session->javaPd >= 0) {
            javaRet = JavaTraceManager::GetInstance().Disable(session->javaPd);
        }

        if (session->nativePd >= 0) {
            nativeRet = PmuDisable(session->nativePd);
        }

        return nativeRet != 0 ? nativeRet : javaRet;
    }

    return PmuDisable(pd);
}

static char *DupCString(const char *s)
{
    if (s == nullptr) {
        return nullptr;
    }

    size_t len = std::strlen(s);
    char *p = static_cast<char *>(std::malloc(len + 1));
    if (p == nullptr) {
        return nullptr;
    }

    std::memcpy(p, s, len + 1);
    return p;
}

static UTraceData DeepCopyTraceData(const UTraceData &src)
{
    UTraceData dst = src;
    dst.comm = DupCString(src.comm);
    dst.module = DupCString(src.module);
    dst.func = DupCString(src.func);
    return dst;
}

static UTraceData *MergeTraceData(UTraceData *nativeData, int nativeLen,
                                  UTraceData *javaData, int javaLen)
{
    int nLen = nativeLen > 0 ? nativeLen : 0;
    int jLen = javaLen > 0 ? javaLen : 0;
    if ((nLen > 0 && nativeData == nullptr) || (jLen > 0 && javaData == nullptr)) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "Trace data is null while trace length is positive");
        return nullptr;
    }
    int total = nLen + jLen;
    if (total <= 0) {
        return nullptr;
    }

    UTraceData *merged = static_cast<UTraceData *>(std::calloc(total, sizeof(UTraceData)));
    if (merged == nullptr) {
        pcerr::New(Perrorno(), Perror());
        return nullptr;
    }
    int idx = 0;
    for (int i = 0; i < nLen; ++i) {
        merged[idx++] = DeepCopyTraceData(nativeData[i]);
    }
    for (int i = 0; i < jLen; ++i) {
        merged[idx++] = DeepCopyTraceData(javaData[i]);
    }
    mergedTraceLens[merged] = total;
    return merged;
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
            if (javaLen > 0 && javaData == nullptr) {
                pcerr::New(LIBPERF_ERR_NULL_POINTER, "Java trace data is null while trace length is positive");
                return -1;
            }
            if (javaLen < 0) {
                return javaLen;
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

                nativeData = TraceDataManager::GetInstance().ConvertToTraceData( session->nativePd, pmuData, nativeLen);
                if (nativeData == nullptr) {
                    if (javaData != nullptr) {
                        JavaTraceManager::GetInstance().FreeData(javaData);
                    }
                    pcerr::New(LIBPERF_ERR_NULL_POINTER, "Convert native PMU data to UTraceData failed");
                    return -1;
                }
            }
        }

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
    if (len > 0) {
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

    return 0;
}

static bool FreeMergedTraceData(UTraceData *traceData)
{
    auto it = mergedTraceLens.find(traceData);
    if (it == mergedTraceLens.end()) {
        return false;
    }
    int len = it->second;
    mergedTraceLens.erase(it);
    if (len <= 0) {
        std::free(traceData);
        return true;
    }
    for (int i = 0; i < len; ++i) {
        std::free(const_cast<char *>(traceData[i].comm));
        std::free(const_cast<char *>(traceData[i].module));
        std::free(const_cast<char *>(traceData[i].func));
    }
    std::free(traceData);
    return true;
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
        jvmTraceSessions.erase(pd);
        PmuList::GetInstance()->Close(pd);
        return;
    }

    UTraceResourceGuard guard(pd);
    guard.probesInstalled = true;
}
