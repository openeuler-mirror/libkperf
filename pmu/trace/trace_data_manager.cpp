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
 * Description: Implementation of trace data conversion, storage, and cleanup
 ******************************************************************************/

#include "trace_data_manager.h"
#include "probe_alias_manager.h"
#include "symbol.h"
#include <cstring>

UTraceData *TraceDataManager::ConvertToTraceData(int pd, PmuData *data, int len)
{
    if (!data || len <= 0) {
        return nullptr;
    }

    std::vector<UTraceData> traceData;
    for (int i = 0; i < len; ++i) {
        uint64_t gPtr = 0;
        if (GetFetchG(pd)) {
            PmuGetField(data[i].rawData, "g", &gPtr, sizeof(gPtr));
        }

        const char *colon = std::strchr(data[i].evt, ':');
        if (colon && *(colon + 1)) {
            const auto &binding = ProbeAliasManager::GetInstance().GetBinding(std::string(colon + 1));
            traceData.push_back({data[i].stack->symbol->addr, data[i].comm, data[i].tid, data[i].cpu, data[i].ts, gPtr,
                binding.originalSymRef->c_str(), binding.isRet});
        }
    }

    pmu2trace_[data] = std::move(traceData);

    UTraceData *retPtr = pmu2trace_[data].data();

    trace2Pmu_[retPtr] = data;

    return retPtr;
}

void TraceDataManager::FreeTraceData(UTraceData *traceData)
{
    if (!traceData) {
        return;
    }

    auto it = trace2Pmu_.find(traceData);
    if (it == trace2Pmu_.end()) {
        return;
    }

    PmuData *pmuData = it->second;
    PmuDataFree(pmuData);

    pmu2trace_.erase(pmuData);

    trace2Pmu_.erase(it);
}