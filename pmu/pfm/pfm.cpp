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
 * Description: event configuration query
 ******************************************************************************/
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <unordered_map>
#include <cstdarg>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include "trace.h"
#include "common.h"
#include "cpu_map.h"
#include "pfm_event.h"
#include "pmu_event.h"
#include "pmu.h"
#include "pcerr.h"
#include "pfm.h"

#include "uncore.h"
#include "core.h"

using namespace std;
using namespace pcerr;
using namespace KUNPENG_PMU;

static constexpr int MAX_STRING_LEN = 2048;

static struct PmuEvt* GetRawEvent(const char* pmuName, int collectType)
{
    // check raw event name like 'r11' or 'r60ea' is valid or not
    const char *numStr = pmuName + 1;
    char *endPtr;
    __u64 config = strtol(numStr, &endPtr, 16);
    if (*endPtr != '\0') {
        return nullptr;
    }
    auto* pmuEvtPtr = new PmuEvt {0};
    pmuEvtPtr->config = config;
    pmuEvtPtr->name = pmuName;
    pmuEvtPtr->type = PERF_TYPE_RAW;
    pmuEvtPtr->pmuType = CORE_TYPE;
    pmuEvtPtr->collectType = collectType;
    return pmuEvtPtr;
}

static int GetSpeType(void)
{
    constexpr char* speTypePath = "/sys/devices/arm_spe_0/type";
    FILE *fp = fopen(speTypePath, "r");
    int type;

    if (!fp) {
        return -1;
    }

    if (fscanf(fp, "%d", &type) != 1) {
        if (fclose(fp) == EOF) {
            return -1;
        }
        return -1;
    }

    if (fclose(fp) == EOF) {
        return -1;
    }
    return type;
}

using EvtRetriever = std::function<struct PmuEvt*(const char*, int)>;

static const std::unordered_map<int, EvtRetriever> EvtMap{
        {KUNPENG_PMU::RAW_TYPE, GetRawEvent},
        {KUNPENG_PMU::CORE_TYPE, GetCoreEvent},
        {KUNPENG_PMU::UNCORE_TYPE, GetUncoreEvent},
        {KUNPENG_PMU::UNCORE_RAW_TYPE, GetUncoreRawEvent},
        {KUNPENG_PMU::TRACE_TYPE, GetKernelTraceEvent},
};

static bool CheckEventInList(enum PmuEventType eventType, const char *pmuName)
{
    unsigned numEvt;
    auto eventList = PmuEventList(eventType, &numEvt);
    if (eventList == nullptr) {
        return false;
    }
    for (int j=0;j<numEvt;++j) {
        if (strcmp(eventList[j], pmuName) == 0) {
            return true;
        }
    }
    return false;
}

static bool CheckRawEvent(const char *pmuName)
{
    // check raw event name like 'r11' or 'r60ea' is valid or not
    const char *numStr = pmuName + 1;
    char *endPtr;
    __u64 _ = strtol(numStr, &endPtr, 16);
    if (*endPtr != '\0') {
        return false;
    }
    return true;
}

static int GetEventType(const char *pmuName)
{
    if (CheckEventInList(CORE_EVENT, pmuName)) {
        return CORE_TYPE;
    }

    if (pmuName[0] == 'r' && CheckRawEvent(pmuName)) {
        return RAW_TYPE;
    }
    std::string strName(pmuName);
    // Parse uncore event name like 'hisi_sccl3_ddrc0/flux_rd/'
    if (CheckEventInList(UNCORE_EVENT, pmuName)) {
        return UNCORE_TYPE;
    }
#ifdef IS_X86
    return -1;
#else
    // Kernel trace point event name like 'block:block_bio_complete'
    if (CheckEventInList(TRACE_EVENT, pmuName)) {
        return TRACE_TYPE;
    }
    // Parse uncore event raw name like 'hisi_sccl3_ddrc0/config=0x0/'
    // or smmuv3_pmcg_100020/transaction,filter_enable=1,filter_stream_id=0x7d/
    if (CheckUncoreRawEvent(pmuName)) {
        return UNCORE_RAW_TYPE;
    }
#endif
    return -1;
}

struct PmuEvt* PfmGetPmuEvent(const char* pmuName, int collectType)
{
    if (pmuName == nullptr) {
        auto* evt = new PmuEvt {0};
        evt->collectType = collectType;
        return evt;
    }
    auto type = GetEventType(pmuName);
    if (type == -1) {
        return nullptr;
    }
    struct PmuEvt* evt = (EvtMap.find(type) != EvtMap.end()) ?
                         EvtMap.at(type)(pmuName, collectType) : nullptr;
    if (evt == nullptr) {
        return evt;
    }
    return evt;
}

struct PmuEvt* PfmGetSpeEvent(
        unsigned long dataFilter, unsigned long eventFilter, unsigned long minLatency, int collectType)
{
    auto* evt = new PmuEvt {0};
    evt->collectType = collectType;
    int type = GetSpeType();
    if (type == -1) {
        delete evt;
        return nullptr;
    }
    evt->type = static_cast<unsigned long>(type);
    evt->config = dataFilter;
    evt->config1 = eventFilter;
    evt->config2 = minLatency;

    return evt;
}

void PmuEvtFree(PmuEvt *evt)
{
    delete evt;
}
