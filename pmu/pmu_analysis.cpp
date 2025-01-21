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
 * Description: functions for analyze performance data in the KUNPENG_PMU namespace
 ******************************************************************************/
#include <iostream>
#include <string>
#include <algorithm>
#include "pmu_analysis.h"

using namespace std;

namespace KUNPENG_PMU {
    // Initializing PmuAnalysis singleton instance and global lock
    std::mutex PmuAnalysis::funcsListMtx;
    std::mutex PmuAnalysis::traceDataListMtx;

    const char *SYSCALL_FUNC_ENTER_PREFIX = "syscalls:sys_enter_";
    const char *SYSCALL_FUNC_EXIT_PREFIX = "syscalls:sys_exit_";
    const size_t g_enterPrefixLen = strlen(SYSCALL_FUNC_ENTER_PREFIX);
    const size_t g_exitPrefixLen = strlen(SYSCALL_FUNC_EXIT_PREFIX);

    int PmuAnalysis::Register(const int pd, PmuTraceAttr* traceParam)
    {
        this->FillFuctionList(pd, traceParam);
        return SUCCESS;
    }

    void PmuAnalysis::FillFuctionList(unsigned pd, PmuTraceAttr* traceParam)
    {
        lock_guard<std::mutex> lg(funcsListMtx);
        std::vector<std::string> funcs;
        for (int i = 0; i < traceParam->numFuncs; ++i) {
            funcs.emplace_back(traceParam->funcs[i]);
        }
        funcsList[pd] = funcs;
    }

    bool PmuAnalysis::IsPdAlive(const unsigned pd) const
    {
        lock_guard<mutex> lg(funcsListMtx);
        return funcsList.find(pd) != funcsList.end();
    }

    bool CheckEventIsFunName(const char *evt, const char *funName)
    {
        const char *pos;

        if ((pos = strstr(evt, SYSCALL_FUNC_ENTER_PREFIX))) {
            pos += g_enterPrefixLen;
            if (strcmp(pos, funName) == 0) {
                return true;
            }
        }

        if ((pos = strstr(evt, SYSCALL_FUNC_EXIT_PREFIX))) {
            pos += g_exitPrefixLen;
            if (strcmp(pos, funName) == 0) {
                return true;
            }
        }

        return false;
    }

    static bool CompareByTimeStamp(const PmuData &a, const PmuData &b)
    {
        return a.ts < b.ts;
    }

    static void CollectPmuTraceData(
        const char *funName, const PmuData &enterPmuData, const PmuData &exitPmuData, vector<PmuTraceData> &traceData)
    {
        PmuTraceData traceDataItem = {0};
        traceDataItem.funcs = funName;
        double nsToMsUnit = 1000000.0;
        traceDataItem.elapsedTime = (double)(exitPmuData.ts - enterPmuData.ts) / nsToMsUnit; // convert to ms
        traceDataItem.pid = enterPmuData.pid;
        traceDataItem.tid = enterPmuData.tid;
        traceDataItem.cpu = enterPmuData.cpu;
        traceDataItem.comm = enterPmuData.comm;

        traceData.emplace_back(traceDataItem);
    }

    std::vector<PmuTraceData>& PmuAnalysis::AnalyzeTraceData(int pd, PmuData *pmuData, unsigned len)
    {
        lock_guard<std::mutex> lg(traceDataListMtx);
        int oriDataLen = len;
        vector<string>& funList = funcsList.at(pd);
        int oriLen = 0;
        vector<PmuTraceData> traceData;
        for (size_t i = 0; i < funList.size(); ++i) {
            map<int, vector<PmuData>> tidPmuData;
            string& funName = funList[i];
            for (; oriLen < oriDataLen; ++oriLen) {
                if (!CheckEventIsFunName(pmuData[oriLen].evt, funName.c_str())) {
                    break;
                }
                tidPmuData[pmuData[oriLen].tid].emplace_back(pmuData[oriLen]);
            }

            for (auto& tidPmuData : tidPmuData) {
                vector<PmuData> enterList;
                vector<PmuData> exitList;
                for (int j = 0; j < tidPmuData.second.size(); ++j) {
                    if (strstr(tidPmuData.second[j].evt, SYSCALL_FUNC_ENTER_PREFIX)) {
                        enterList.emplace_back(tidPmuData.second[j]);
                    } else if (strstr(tidPmuData.second[j].evt, SYSCALL_FUNC_EXIT_PREFIX)) {
                        exitList.emplace_back(tidPmuData.second[j]);
                    }
                }
                if (enterList.size() == 0 || exitList.size() == 0) {
                    continue;
                }
                sort(enterList.begin(), enterList.end(), CompareByTimeStamp);
                sort(exitList.begin(), exitList.end(), CompareByTimeStamp);
                int enterIndex = 0;
                int exitIndex = 0;
                while (enterIndex < enterList.size() && exitIndex < exitList.size()) {
                    if (enterList[enterIndex].ts < exitList[exitIndex].ts) {
                        CollectPmuTraceData(funName.c_str(), enterList[enterIndex], exitList[exitIndex], traceData);
                        enterIndex++;
                        exitIndex++;
                    } else {
                        exitIndex++;
                    }
                }
            }
        }

        TraceEventData newTraceData = {
            .pd = pd,
            .traceType = TRACE_SYS_CALL,
            .data = move(traceData),
        };
        oriPmuData[newTraceData.data.data()] = pmuData;
        auto inserted = traceDataList.emplace(newTraceData.data.data(), move(newTraceData));
        return inserted.first->second.data;
    }

    void PmuAnalysis::EraseFuncsList(const unsigned pd)
    {
        lock_guard<mutex> lg(funcsListMtx);
        funcsList.erase(pd);
    }

    void PmuAnalysis::EraseTraceDataList(const unsigned pd)
    {
        lock_guard<mutex> lg(traceDataListMtx);
        for (auto iter = traceDataList.begin(); iter != traceDataList.end();) {
            if (iter->second.pd == pd) {
                PmuDataFree(oriPmuData[iter->first]); // free the corresponding PmuData
                iter = traceDataList.erase(iter);
            } else {
                ++iter;
            }
        }
    }

    void PmuAnalysis::FreeTraceData(PmuTraceData* pmuTraceData)
    {
        lock_guard<mutex> lg(traceDataListMtx);
        auto findData = traceDataList.find(pmuTraceData);
        if (findData == traceDataList.end()) {
            return;
        }
        PmuDataFree(oriPmuData[findData->first]); // free the corresponding PmuData
        traceDataList.erase(pmuTraceData);
    }

    void PmuAnalysis::Close(const int pd)
    {   
        EraseFuncsList(pd);
        EraseTraceDataList(pd);
        PmuClose(pd);
    }

}