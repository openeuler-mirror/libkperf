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
#include <cstring>
#include "java_trace_util.h"
#include "probe_alias_manager.h"
#include "symbol.h"

UTraceData *TraceDataManager::ConvertToTraceData(int pd, PmuData *data, int len, int *convertedLen)
{
    if (convertedLen != nullptr) {
        *convertedLen = 0;
    }
    if (data == nullptr || len <= 0) {
        return nullptr;
    }

    std::vector<UTraceData> traceData;
    for (int i = 0; i < len; ++i) {
        if (data[i].stack == nullptr || data[i].stack->symbol == nullptr || data[i].evt == nullptr) {
            continue;
        }
        uint64_t gPtr = 0;
        if (GetFetchG(pd)) {
            PmuGetField(data[i].rawData, "g", &gPtr, sizeof(gPtr));
        }

        const char *colon = std::strchr(data[i].evt, ':');
        if (colon && *(colon + 1)) {
            const auto &binding = ProbeAliasManager::GetInstance().GetBinding(std::string(colon + 1));
            unsigned payloadSize = 0;
            void *payload = BuildMethodPayload(data[i].ts, data[i].stack->symbol->addr, gPtr,
                data[i].tid, data[i].cpu, binding.isRet ? 1U : 0U, data[i].comm,
                data[i].stack->symbol->module, binding.originalSymRef->c_str(), &payloadSize);
            if (payload == nullptr) {
                continue;
            }
            UTraceData entry = {};
            entry.format = UTRACE_FMT_METHOD;
            entry.size = payloadSize;
            entry.payload = payload;
            traceData.push_back(entry);
        }
    }

    if (convertedLen != nullptr) {
        *convertedLen = static_cast<int>(traceData.size());
    }
    if (traceData.empty()) {
        return nullptr;
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

    // payloads are self-contained malloc'd blobs owned by the stored records
    auto blobIt = pmu2trace_.find(pmuData);
    if (blobIt != pmu2trace_.end()) {
        for (UTraceData &entry : blobIt->second) {
            std::free(entry.payload);
            entry.payload = nullptr;
        }
    }

    PmuDataFree(pmuData);

    pmu2trace_.erase(pmuData);

    trace2Pmu_.erase(it);
}