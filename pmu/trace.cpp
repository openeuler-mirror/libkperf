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

using namespace KUNPENG_PMU;

extern int CheckAttr(enum PmuTaskType collectType, struct PmuAttr *attr);
extern struct PmuTaskAttr *AssignPmuTaskParam(enum PmuTaskType collectType, struct PmuAttr *attr);
extern void PmuTaskAttrFree(PmuTaskAttr *taskAttr);

int UTraceOpen(struct UTraceAttr *attr)
{
    pcerr::New(SUCCESS);
    if (attr == nullptr) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "UTraceAttr cannot be null");
        return -1;
    }

    std::unordered_map<std::string, std::vector<std::string>> module2Symbols;
    for (int i = 0; i < attr->numSym; ++i) {
        module2Symbols[attr->symSrc[i].moduleName].emplace_back(attr->symSrc[i].symbolName);
    }
    auto module2ProbePoints = ElfScanner::ResolveElfs(module2Symbols);
    std::string elfScanFailures = ElfScanner::FormatFailures();
    if (module2ProbePoints.empty()) {
        pcerr::New(LIBPERF_ERR_UTRACE_ELF_SCAN_FAILED, "No probe points resolved from ELF modules");
        pcerr::SetWarn(LIBPERF_WARN_UTRACE_ELF_SCAN_FAILED, elfScanFailures);
        return -1;
    }
    if (!elfScanFailures.empty()) {
        pcerr::SetWarn(LIBPERF_WARN_UTRACE_ELF_SCAN_FAILED, elfScanFailures);
    }

    int pd = PmuList::GetInstance()->NewPd();
    if (pd == -1) {
        pcerr::New(LIBPERF_ERR_NO_AVAIL_PD);
        return -1;
    }

    ProbeRegistrar::GetInstance().ConvertToProbeEvents(pd, module2ProbePoints);
    if (!ProbeRegistrar::GetInstance().InstallProbes(pd, attr->fetchG)) {
        ProbeRegistrar::GetInstance().EraseProbeEvents(pd);
        return -1;
    }

    std::vector<std::string> evtListCache;
    for (const auto &probeEvent : ProbeRegistrar::GetInstance().GetProbeEvents(pd)) {
        evtListCache.emplace_back(probeEvent.groupName + ":" + probeEvent.eventName);
    }
    std::vector<char *> evtPtrList;
    for (auto &evt : evtListCache) {
        evtPtrList.push_back(const_cast<char *>(evt.c_str()));
    }
    char **evtList = evtPtrList.data();

    PmuAttr pmuAttr = {0};
    pmuAttr.evtList = evtList;
    pmuAttr.numEvt = evtPtrList.size();
    pmuAttr.pidList = attr->pidList;
    pmuAttr.numPid = attr->numPid;
    pmuAttr.cpuList = attr->cpuList;
    pmuAttr.numCpu = attr->numCpu;

    int err = CheckAttr(SAMPLING, &pmuAttr);
    if (err != SUCCESS) {
        pcerr::New(err);
        ProbeRegistrar::GetInstance().UninstallProbes(pd);
        ProbeRegistrar::GetInstance().EraseProbeEvents(pd);
        return -1;
    }

    std::unique_ptr<PmuTaskAttr, void (*)(PmuTaskAttr *)> pmuTaskAttrHead(AssignPmuTaskParam(SAMPLING, &pmuAttr), PmuTaskAttrFree);
    if (!pmuTaskAttrHead) {
        return -1;
    }

    PmuList::GetInstance()->FillPidList(pd, pmuAttr.numPid, pmuAttr.pidList);
    PmuList::GetInstance()->SetSymbolMode(pd, RESOLVE_ELF);
    PmuList::GetInstance()->SetAnalysisStatus(pd, GOING_RESOLVE);
    err = PmuList::GetInstance()->Register(pd, pmuTaskAttrHead.get());
    if (err != SUCCESS) {
        PmuList::GetInstance()->Close(pd);
        ProbeRegistrar::GetInstance().UninstallProbes(pd);
        ProbeRegistrar::GetInstance().EraseProbeEvents(pd);
        pcerr::New(err);
        return -1;
    }

    TraceDataManager::GetInstance().SetFetchG(pd, attr->fetchG);
    return pd;
}

int UTraceEnable(int pd)
{
    pcerr::New(SUCCESS);
    return PmuEnable(pd);
}

int UTraceDisable(int pd)
{
    pcerr::New(SUCCESS);
    return PmuDisable(pd);
}

int UTraceRead(int pd, struct UTraceData **traceData)
{
    pcerr::New(SUCCESS);
    if (traceData == nullptr) {
        pcerr::New(LIBPERF_ERR_NULL_POINTER, "UTraceData cannot be null");
        return -1;
    }

    PmuData *pmuData = nullptr;
    int len = PmuRead(pd, &pmuData);

    *traceData = (len > 0 && pmuData) ? TraceDataManager::GetInstance().ConvertToTraceData(pd, pmuData, len) : nullptr;

    return len;
}

void UTraceDataFree(struct UTraceData *traceData)
{
    TraceDataManager::GetInstance().FreeTraceData(traceData);
}

void UTraceClose(int pd)
{
    pcerr::New(SUCCESS);
    PmuList::GetInstance()->Close(pd);
    ProbeRegistrar::GetInstance().UninstallProbes(pd);
    ProbeRegistrar::GetInstance().EraseProbeEvents(pd);
    TraceDataManager::GetInstance().Erase(pd);
}
