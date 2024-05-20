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
 * Author: Mr.Ye
 * Create: 2024-04-03
 * Description: trace point event configuration query
 ******************************************************************************/
#include <fstream>
#include "common.h"
#include "pfm_event.h"
#include "pmu_event.h"
#include "trace.h"

using namespace std;
using namespace KUNPENG_PMU;

static int64_t GetTraceEventConfig(const std::string &name)
{
    size_t colon = name.find(':');
    string systemName = name.substr(0, colon);
    string eventName = name.substr(colon + 1);

    string eventPath = "/sys/kernel/tracing/events/" + systemName + "/" + eventName + "/id";
    string realPath = GetRealPath(eventPath);
    if (!IsValidPath(realPath)) {
        return -1;
    }
    ifstream typeIn(realPath);
    if (!typeIn.is_open()) {
        return -1;
    }
    string typeStr;
    typeIn >> typeStr;

    return stoi(typeStr);
}

struct PmuEvt* GetKernelTraceEvent(const char* pmuName, int collectType)
{
    int64_t config = GetTraceEventConfig(pmuName);
    if (config == -1) {
        return nullptr;
    }
    auto* pmuEvtPtr = new PmuEvt {0};
    pmuEvtPtr->config = config;
    pmuEvtPtr->name = pmuName;
    pmuEvtPtr->type = PERF_TYPE_TRACEPOINT;
    pmuEvtPtr->pmuType = TRACE_TYPE;
    pmuEvtPtr->collectType = collectType;
    pmuEvtPtr->cpumask = -1;
    return pmuEvtPtr;
}
