/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Lei
 * Create: 2025-01-17
 * Description: definition of singleton class PmuAnalysis for analyze performance data in the KUNPENG_PMU namespace
 ******************************************************************************/
#ifndef PMU_ANALYSIS_H
#define PMU_ANALYSIS_H
#include <mutex>
#include <vector>
#include <unordered_map>
#include "pmu.h"
#include "pmu_list.h"

namespace KUNPENG_PMU {
    extern const char *SYSCALL_FUNC_ENTER_PREFIX;
    extern const char *SYSCALL_FUNC_EXIT_PREFIX;

    class PmuAnalysis {
        public:
            static PmuAnalysis* GetInstance()
            {
                static PmuAnalysis instance;
                return &instance;
            }

            int Register(const int pd, PmuTraceAttr* traceParam);
            std::vector<PmuTraceData>& AnalyzeTraceData(int pd, PmuData *pmuData, unsigned len);
            void Close(const int pd);
            void FreeTraceData(PmuTraceData* pmuTraceData);

        private:
            PmuAnalysis()
            {}
            PmuAnalysis(const PmuAnalysis&) = delete;
            PmuAnalysis& operator=(const PmuAnalysis&) = delete;
            ~PmuAnalysis() = default;

            struct TraceEventData {
                unsigned pd;
                PmuTraceType traceType;
                std::vector<PmuTraceData> data;
            };

            void FillFuctionList(unsigned pd, PmuTraceAttr* traceParam);
            void EraseFuncsList(const unsigned pd);
            static std::mutex funcsListMtx;
            std::unordered_map<unsigned, std::vector<std::string>> funcsList;

            void EraseTraceDataList(const unsigned pd);
            static std::mutex traceDataListMtx;
            // Key: PmuTraceData raw point
            // Value: TraceEventData
            std::unordered_map<PmuTraceData*, TraceEventData> traceDataList;
    };
}   // namespace KUNPENG_PMU
#endif