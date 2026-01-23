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
 * Description: Interfaces for converting, storing, and cleaning up trace data
 ******************************************************************************/

#pragma once

#include "pmu.h"
#include <vector>
#include <unordered_map>

class TraceDataManager {
public:
    static TraceDataManager &GetInstance()
    {
        static TraceDataManager instance;
        return instance;
    }

    TraceDataManager(const TraceDataManager &) = delete;
    TraceDataManager &operator=(const TraceDataManager &) = delete;

    UTraceData *ConvertToTraceData(int pd, PmuData *data, int len);

    void FreeTraceData(UTraceData *traceData);

    void SetFetchG(int pd, bool fetchG)
    {
        pd2FetchG_[pd] = fetchG;
    }

    bool GetFetchG(int pd) const
    {
        auto it = pd2FetchG_.find(pd);
        return (it != pd2FetchG_.end()) ? it->second : false;
    }

    void Erase(int pd)
    {
        pd2FetchG_.erase(pd);
    }

private:
    TraceDataManager() = default;
    ~TraceDataManager() = default;

    std::unordered_map<UTraceData *, PmuData *> trace2Pmu_;

    std::unordered_map<PmuData *, std::vector<UTraceData>> pmu2trace_;

    std::unordered_map<int, bool> pd2FetchG_;
};