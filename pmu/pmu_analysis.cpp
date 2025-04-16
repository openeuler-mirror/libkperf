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
#include <unordered_map>
#include <fstream>
#include <algorithm>
#include "pcerr.h"
#include "pmu_analysis.h"

using namespace std;
using namespace pcerr;

namespace KUNPENG_PMU {
    // Initializing PmuAnalysis singleton instance and global lock
    std::mutex PmuAnalysis::funcsListMtx;
    std::mutex PmuAnalysis::traceDataListMtx;

    const char *SYSCALL_FUNC_ENTER_PREFIX = "syscalls:sys_enter_";
    const char *SYSCALL_FUNC_EXIT_PREFIX = "syscalls:sys_exit_";
    const size_t g_enterPrefixLen = strlen(SYSCALL_FUNC_ENTER_PREFIX);
    const size_t g_exitPrefixLen = strlen(SYSCALL_FUNC_EXIT_PREFIX);

    char *ENTER_RAW_SYSCALL = "raw_syscalls:sys_enter";
    char *EXIT_RAW_SYSCALL = "raw_syscalls:sys_exit";

    static const string UNISTD_PATH = "/usr/include/asm-generic/unistd.h";
    static unordered_map<long, string> syscallTable;

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

    int PmuAnalysis::GenerateSysCallTable()
    {
        ifstream syscallFile(UNISTD_PATH);
        if (!syscallFile.is_open()) {
            New(LIBPERF_ERR_OPEN_SYSCALL_HEADER_FAILED,
                "open /usr/include/asm-generic/unistd.h file failed! unable to generate syscall table!");
            return LIBPERF_ERR_OPEN_SYSCALL_HEADER_FAILED;
        }

        string line;
        const std::string perfixDefineNr = "#define __NR_";
        const std::string perfixDefineNr3264 = "#define __NR3264_";
        size_t nameStart = 0;
        while (getline(syscallFile, line)) {
            if (line.compare(0, perfixDefineNr.length(), perfixDefineNr) == 0) {
                nameStart = perfixDefineNr.length();
            } else if (line.compare(0, perfixDefineNr3264.length(), perfixDefineNr3264) == 0) {
                nameStart = perfixDefineNr3264.length();
            } else {
                continue; // skip line if it doesn't start with perfixDefineNr or perfixDefineNr3264
            }
            size_t nameEnd = line.find(' ', nameStart);
            size_t numberStart = line.find_last_of(' ') + 1;

            if (nameStart != string::npos && nameEnd != string::npos && numberStart != string::npos) {
                string funName = line.substr(nameStart, nameEnd - nameStart);
                string numberStr = line.substr(numberStart); // get the number of NR_SYSCALL

                try {
                    int syscallNumber = stoi(numberStr);
                    syscallTable[syscallNumber] = funName;
                } catch (const invalid_argument& e) {
                    // Handle invalid argument exception
                    continue;
                }
            }
        }

        syscallFile.close();

        return SUCCESS;
    }

    bool PmuAnalysis::IsPdAlive(const unsigned pd) const
    {
        lock_guard<mutex> lg(funcsListMtx);
        return funcsList.find(pd) != funcsList.end();
    }

    static bool CheckEventIsRawSysCall(const char *evt)
    {
        return (strcmp(evt, ENTER_RAW_SYSCALL) == 0) || (strcmp(evt, EXIT_RAW_SYSCALL) == 0);
    }

    static bool CheckEventIsFunName(const char *evt, const char *funName)
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
        traceDataItem.startTs = enterPmuData.ts;
        traceDataItem.elapsedTime = (double)(exitPmuData.ts - enterPmuData.ts) / nsToMsUnit; // convert to ms
        traceDataItem.pid = enterPmuData.pid;
        traceDataItem.tid = enterPmuData.tid;
        traceDataItem.cpu = enterPmuData.cpu;
        traceDataItem.comm = enterPmuData.comm;

        traceData.emplace_back(traceDataItem);
    }

    static int GetRawSysCallId(PmuData &pmuData)
    {
        long NR_SYSCALL = 0;
        int ret = PmuGetField(pmuData.rawData, "id", &NR_SYSCALL, sizeof(NR_SYSCALL));
        if (ret != SUCCESS) {
            return -1;
        }
        return NR_SYSCALL;
    }

    static const char *GetRawSysCallName(long NR_SYSCALL)
    {
        // already test checked that all syscall numbers is in the syscall table.
        // so we can use it directly.
        return syscallTable[NR_SYSCALL].c_str();
    }

    std::vector<PmuTraceData>& PmuAnalysis::AnalyzeRawTraceData(int pd, PmuData *pmuData, unsigned len)
    {
        vector<PmuTraceData> traceData;
        const int pairNum = 2;
        traceData.reserve(len / pairNum);
        unordered_map<int, vector<PmuData>> tidPmuDatas;
        // Collect the original data according to the TID number.
        for (int orilen = 0; orilen < len; ++orilen) {
            tidPmuDatas[pmuData[orilen].tid].emplace_back(pmuData[orilen]);
        }
        for (auto &tidPmuData : tidPmuDatas) {
            // Collect the original data according to the syscall id.
            // The key is the syscall id, and the value is a pair of vectors.
            // the pair of vectors is the enter and exit events corresponding to this system call function
            unordered_map<long, pair<vector<PmuData>, vector<PmuData>>> sysCallPairs;
            for (int j = 0; j < tidPmuData.second.size(); ++j) {
                long sysCallId = GetRawSysCallId(tidPmuData.second[j]);
                if (strcmp(tidPmuData.second[j].evt, ENTER_RAW_SYSCALL) == 0) {
                    sysCallPairs[sysCallId].first.emplace_back(tidPmuData.second[j]);
                } else if (strcmp(tidPmuData.second[j].evt, EXIT_RAW_SYSCALL) == 0) {
                    sysCallPairs[sysCallId].second.emplace_back(tidPmuData.second[j]);
                }
            }
            for (auto &sysCallPair : sysCallPairs) {
                auto& [enterEvts, exitEvts] = sysCallPair.second;
                if (enterEvts.empty() || exitEvts.empty()) {
                    continue;
                }
                sort(enterEvts.begin(), enterEvts.end(), CompareByTimeStamp);
                sort(exitEvts.begin(), exitEvts.end(), CompareByTimeStamp);
                int enterIndex = 0;
                int exitIndex = 0;
                while (enterIndex < enterEvts.size() && exitIndex < enterEvts.size()) {
                    if (enterEvts[enterIndex].ts < exitEvts[exitIndex].ts) {
                        CollectPmuTraceData(GetRawSysCallName(sysCallPair.first), enterEvts[enterIndex],
                                            exitEvts[exitIndex], traceData);
                        enterIndex++;
                        exitIndex++;
                    } else {
                        exitIndex++;
                    }
                }
            }
        }

        lock_guard<std::mutex> lg(traceDataListMtx);
        TraceEventData newTraceData = {
            .pd = pd,
            .traceType = TRACE_SYS_CALL,
            .data = move(traceData),
        };
        oriPmuData[newTraceData.data.data()] = pmuData;
        auto inserted = traceDataList.emplace(newTraceData.data.data(), move(newTraceData));
        return inserted.first->second.data;
    }

    std::vector<PmuTraceData>& PmuAnalysis::AnalyzeTraceData(int pd, PmuData *pmuData, unsigned len)
    {
        int oriDataLen = len;
        vector<string>& funList = funcsList.at(pd);
        int oriLen = 0;
        vector<PmuTraceData> traceData;
        const int pairNum = 2;
        traceData.reserve(len / pairNum);
        for (size_t i = 0; i < funList.size(); ++i) {
            unordered_map<int, vector<PmuData>> tidPmuDatas;
            string& funName = funList[i];
            // Collect the original data according to the TID number.
            for (; oriLen < oriDataLen; ++oriLen) {
                if (!CheckEventIsFunName(pmuData[oriLen].evt, funName.c_str())) {
                    // if the event is not the function name, break.
                    // The current system call function data processing is completed.
                    break;
                }
                tidPmuDatas[pmuData[oriLen].tid].emplace_back(pmuData[oriLen]);
            }

            for (auto& tidPmuData : tidPmuDatas) {
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
        lock_guard<std::mutex> lg(traceDataListMtx);
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