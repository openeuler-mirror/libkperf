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
 * Author: Mr.Zhang
 * Create: 2024-04-03
 * Description: functions for managing performance monitoring tasks, collecting data, and handling
 * performance counters in the KUNPENG_PMU namespace
 ******************************************************************************/
#include <memory>
#include <algorithm>
#include <sys/resource.h>
#include "linked_list.h"
#include "cpu_map.h"
#include "process_map.h"
#include "pcerrc.h"
#include "pcerr.h"
#include "util_time.h"
#include "log.h"
#include "pmu_event_list.h"
#include "pmu_list.h"
#include "common.h"

using namespace std;
using namespace pcerr;

namespace KUNPENG_PMU {
// Initializing pmu list singleton instance and global lock
    std::mutex PmuList::pmuListMtx;
    std::mutex PmuList::dataListMtx;
    std::mutex PmuList::dataParentMtx;

    int PmuList::CheckRlimit(const unsigned fdNum)
    {
        return RaiseNumFd(fdNum);
    }

    int PmuList::Register(const int pd, PmuTaskAttr* taskParam)
    {
        if (GetSymbolMode(pd) != NO_SYMBOL_RESOLVE && taskParam->pmuEvt->collectType != COUNTING) {
            SymResolverInit();
            SymResolverRecordKernel();
        }
        /* Use libpfm to get the basic config for this pmu event */
        struct PmuTaskAttr* pmuTaskAttrHead = taskParam;
        // Init collect type for pmu data,
        // because different type has different free strategy.
        auto &evtData = GetDataList(pd);
        if (pmuTaskAttrHead != nullptr) {
            evtData.collectType = static_cast<PmuTaskType>(pmuTaskAttrHead->pmuEvt->collectType);
            evtData.pd = pd;
        }
        
        unsigned fdNum = 0;
        while (pmuTaskAttrHead) {
            /**
             * Create cpu topology list
             */
            std::vector<CpuPtr> cpuTopoList;
            auto err = PrepareCpuTopoList(pd, pmuTaskAttrHead, cpuTopoList);
            if (err != SUCCESS) {
                return err;
            }

            /**
             * Create process topology list
             */
            std::vector<ProcPtr> procTopoList;
            err = PrepareProcTopoList(pmuTaskAttrHead, procTopoList);
            if (err != SUCCESS) {
                return err;
            }
            fdNum += cpuTopoList.size() * procTopoList.size();
            std::shared_ptr<EvtList> evtList =
                    std::make_shared<EvtList>(GetSymbolMode(pd), cpuTopoList, procTopoList, pmuTaskAttrHead->pmuEvt);
            InsertEvtList(pd, evtList);
            pmuTaskAttrHead = pmuTaskAttrHead->next;
        }

        auto err = CheckRlimit(fdNum);
        if (err != SUCCESS) {
            return err;
        }
        
        for (auto evtList : GetEvtList(pd)) {
            auto err = evtList->Init();
            if (err != SUCCESS) {
                return err;
            }
            err = AddToEpollFd(pd, evtList);
            if (err != SUCCESS) {
                return err;
            }
        }

        return SUCCESS;
    }

    int PmuList::Start(const int pd)
    {
        auto pmuList = GetEvtList(pd);
        for (auto item : pmuList) {
            auto err = item->Start();
            if (err != SUCCESS) {
                return err;
            }
        }
        return SUCCESS;
    }

    int PmuList::Pause(const int pd)
    {
        auto pmuList = GetEvtList(pd);
        for (auto item : pmuList) {
            auto err = item->Pause();
            if (err != SUCCESS) {
                return err;
            }
        }
        return SUCCESS;
    }

    std::vector<PmuData>& PmuList::Read(const int pd)
    {
        // Exchange data in <dataList> to <userDataList>.
        // Return a pointer to data.

        auto &evtData = GetDataList(pd);
        if (evtData.data.empty()) {
            // Have not read ring buffer yet.
            // Mostly caller is using PmuEnable and PmuDisable mode.
            auto err = ReadDataToBuffer(pd);
            if (err != SUCCESS) {
                return userDataList[nullptr].data;
            }
        }
        auto& userData = ExchangeToUserData(pd);
        if (GetTaskType(pd) == COUNTING) {
            // Reset counter after Read is called,
            // thus count in PmuData is pmu count in last epoch.
            auto evtList = GetEvtList(pd);
            for (auto item : evtList) {
                item->Reset();
            }
        }
        return userData;
    }

    int PmuList::ReadDataToBuffer(const int pd)
    {
        // Read data from prev sampling,
        // and store data in <dataList>.
        auto &evtData = GetDataList(pd);
        evtData.pd = pd;
        evtData.collectType = static_cast<PmuTaskType>(GetTaskType(pd));
        auto ts = GetCurrentTime();
        auto eventList = GetEvtList(pd);
        for (auto item : eventList) {
            item->SetTimeStamp(ts);
            auto err = item->Read(evtData.data, evtData.sampleIps, evtData.extPool);
            if (err != SUCCESS) {
                return err;
            }
        }

        return SUCCESS;
    }

    int PmuList::AppendData(PmuData *fromData, PmuData **toData, int &len)
    {
        if (toData == nullptr || fromData == nullptr) {
            return LIBPERF_ERR_INVALID_PMU_DATA;
        }

        lock_guard<mutex> lg(dataListMtx);
        auto findFromData = userDataList.find(fromData);
        if (findFromData == userDataList.end()) {
            return LIBPERF_ERR_INVALID_PMU_DATA;
        }

        if (*toData == nullptr) {
            // For null target data list, copy source list to target.
            // A new pointer to data list is created and is assigned to <*toData>.
            EventData newData = findFromData->second;
            len = newData.data.size();
            auto pData = newData.data.data();
            userDataList[pData] = move(newData);
            *toData = pData;
            return SUCCESS;
        }

        auto findToData = userDataList.find(*toData);
        if (findFromData == userDataList.end()) {
            return LIBPERF_ERR_INVALID_PMU_DATA;
        }
        // For non-null target data list, append source list to end of target vector.
        auto &dataVec = findToData->second.data;
        dataVec.insert(dataVec.end(), findFromData->second.data.begin(), findFromData->second.data.end());
        len = dataVec.size();

        if (*toData != dataVec.data()) {
            // As target vector grows, pointer to list may change.
            // Update the pointer and assign to <*toData>.
            auto newDataPtr = dataVec.data();
            userDataList[newDataPtr] = move(findToData->second);
            userDataList.erase(*toData);
            *toData = newDataPtr;
        }
        return SUCCESS;
    }

    void PmuList::StoreSplitData(const unsigned pd, pair<unsigned, char**> previousEventList,
                                 unordered_map<string, char*> eventSplitMap)
    {
        lock_guard<mutex> lg(dataParentMtx);
        parentEventMap.emplace(pd, move(eventSplitMap));
        previousEventMap.emplace(pd, move(previousEventList));
    }

    void PmuList::Close(const int pd)
    {
        auto evtList = GetEvtList(pd);
        for (auto item : evtList) {
            item->Close();
        }
        EraseEvtList(pd);
        EraseDataList(pd);
        RemoveEpollFd(pd);
        EraseSpeCpu(pd);
        EraseParentEventMap();
        SymResolverDestroy();
        PmuEventListFree();
    }

    int PmuList::NewPd()
    {
        lock_guard<mutex> lg(pmuListMtx);
        if (maxPd == std::numeric_limits<unsigned>::max()) {
            // Search available pd, by search available key in pmuList.
            unsigned availPd = 0;
            auto findPd = pmuList.find(availPd);
            while (findPd != pmuList.end()) {
                ++availPd;
                findPd = pmuList.find(availPd);
                if (availPd == std::numeric_limits<unsigned>::max()) {
                    return -1;
                }
            }
            maxPd = availPd;
        } else {
            maxPd++;
        }

        return maxPd;
    }

    bool PmuList::AllPmuDead(const int pd)
    {
        auto epollFd = GetEpollFd(pd);
        if (epollFd == -1) {
            return true;
        }
        // Check if all fds are EPOLLHUP, which represents all processes exit.
        auto epollEvents = GetEpollEvents(epollFd);
        epoll_wait(epollFd, epollEvents.data(), epollEvents.size(), 0);
        for (auto& evt : epollEvents) {
            if (!(evt.events & EPOLLHUP)) {
                return false;
            }
        }

        return true;
    }

    bool PmuList::IsPdAlive(const int pd) const
    {
        lock_guard<mutex> lg(pmuListMtx);
        return pmuList.find(pd) != pmuList.end();
    }

    void PmuList::FreeData(PmuData* pmuData)
    {
        EraseUserData(pmuData);
    }

    int PmuList::GetTaskType(const int pd) const
    {
        lock_guard<mutex> lg(pmuListMtx);
        auto findEvtList = pmuList.find(pd);
        if (findEvtList == pmuList.end()) {
            return -1;
        }
        if (findEvtList->second.empty()) {
            return -1;
        }
        return findEvtList->second[0]->GetEvtType();
    }

    void PmuList::InsertEvtList(const unsigned pd, std::shared_ptr<EvtList> evtList)
    {
        lock_guard<mutex> lg(pmuListMtx);
        pmuList[pd].push_back(evtList);
    }

    std::vector<std::shared_ptr<EvtList>>& PmuList::GetEvtList(const unsigned pd)
    {
        lock_guard<mutex> lg(pmuListMtx);
        return pmuList[pd];
    }

    void PmuList::EraseEvtList(const unsigned pd)
    {
        lock_guard<mutex> lg(pmuListMtx);
        pmuList.erase(pd);
    }

    void PmuList::EraseParentEventMap()
    {
        lock_guard<mutex> lg(dataParentMtx);
        for (auto& pair : parentEventMap) {
            auto& innerMap = pair.second;
            innerMap.clear();
        }
        parentEventMap.clear();
        previousEventMap.clear();
    }

    PmuList::EventData& PmuList::GetDataList(const unsigned pd)
    {
        lock_guard<mutex> lg(dataListMtx);
        return dataList[pd];
    }

    void PmuList::EraseDataList(const unsigned pd)
    {
        lock_guard<mutex> lg(dataListMtx);
        dataList.erase(pd);
        for (auto iter = userDataList.begin();iter != userDataList.end();) {
            if (iter->second.pd == pd) {
                iter = userDataList.erase(iter);
            } else {
                ++iter;
            }
        }
    }

    void PmuList::FillStackInfo(EventData &eventData)
    {
        auto symMode = symModeList[eventData.pd];
        if (symMode == NO_SYMBOL_RESOLVE) {
            return;
        }
        // Parse dwarf and elf info of each pid and get stack trace for each pmu data.
        for (size_t i = 0; i < eventData.data.size(); ++i) {
            auto &pmuData = eventData.data[i];
            auto &ipsData = eventData.sampleIps[i];
            if (symMode == RESOLVE_ELF) {
                SymResolverRecordModuleNoDwarf(pmuData.pid);
            } else if (symMode == RESOLVE_ELF_DWARF) {
                SymResolverRecordModule(pmuData.pid);
            } else {
                continue;
            }
            if (pmuData.stack == nullptr) {
                pmuData.stack = StackToHash(pmuData.pid, ipsData.ips.data(), ipsData.ips.size());
            }
        }
    }

    void PmuList::AggregateData(const std::vector<PmuData>& evData, std::vector<PmuData>& newEvData)
    {
        // Acccumulate stat data in previous PmuCollect for convenient use.
        // One count for same event + tid + cpu.
        map<std::tuple<string, int, unsigned>, PmuData> mergedMap;
        for (auto& data : evData) {
            auto key = std::make_tuple(
                    data.evt, data.tid, data.cpu);
            if (mergedMap.find(key) == mergedMap.end()) {
                mergedMap[key] = data;
            } else {
                mergedMap[key].count += data.count;
            }
        }
        for (auto &evtData: mergedMap) {
            newEvData.push_back(evtData.second);
        }
    }


    bool comparePmuData(const PmuData& data1, const PmuData& data2)
    {
        return strcmp(data1.evt, data2.evt) < 0;
    }

    void PmuList::AggregateUncoreData(const unsigned pd, const vector<PmuData>& evData, vector<PmuData>& newEvData)
    {
        // One count for same parent according to parentEventMap.
        auto parentMap = parentEventMap.at(pd);
        unordered_map<string, PmuData> dataMap;
        for (auto& pmuData : evData) {
            auto parentName = parentMap.at(pmuData.evt);
            if (strcmp(parentName, pmuData.evt) == 0) {
                // event was not split
                newEvData.emplace_back(pmuData);
                continue;
            }
            if (dataMap.find(parentName) == dataMap.end()) {
                // split uncore event which not recorded in dataMap yet
                dataMap[parentName] = pmuData;
                dataMap[parentName].evt = parentMap.at(pmuData.evt);
                dataMap[parentName].cpu = 0;
                dataMap[parentName].cpuTopo = nullptr;
            } else {
                dataMap.at(parentMap.at(pmuData.evt)).count += pmuData.count;
            }
        }
        for (const auto& pair : dataMap) {
            newEvData.emplace_back(pair.second);
        }
        std::sort(newEvData.begin(), newEvData.end(), comparePmuData);
    }

    vector<PmuData>& PmuList::ExchangeToUserData(const unsigned pd)
    {
        lock_guard<mutex> lg(dataListMtx);
        if (dataList.count(pd) == 0) {
            return GetPreviousData(pd);
        }

        auto& evData = dataList[pd];
        auto pData = evData.data.data();
        if (GetTaskType(pd) == COUNTING) {
            std::vector<PmuData> newPmuData;
            AggregateUncoreData(pd, evData.data, newPmuData);
            EventData newEvData = {
                    .pd = pd,
                    .collectType = COUNTING,
                    .data = newPmuData,
            };

            auto inserted = userDataList.emplace(newEvData.data.data(), move(newEvData));
            dataList.erase(pd);
            return inserted.first->second.data;
        } else {
            auto inserted = userDataList.emplace(pData, move(evData));
            dataList.erase(pd);
            FillStackInfo(inserted.first->second);
            return inserted.first->second.data;
        }
    }

    void PmuList::EraseUserData(PmuData* pmuData)
    {
        lock_guard<mutex> lg(dataListMtx);
        auto findData = userDataList.find(pmuData);
        if (findData == userDataList.end()) {
            return;
        }
        // Delete ext pointer malloced in SpeSampler.
        for (auto &extMem : findData->second.extPool) {
            delete[] extMem;
        }
        userDataList.erase(pmuData);
    }

    int PmuList::GetHistoryData(const int pd, std::vector<PmuData>& aggregatedData)
    {
        lock_guard<mutex> lg(dataListMtx);
        std::vector<PmuData> mergedData;
        for (const auto& pair : userDataList) {
            if (pair.second.pd == pd && pair.second.collectType == COUNTING) {
                mergedData.insert(mergedData.end(), pair.second.data.begin(), pair.second.data.end());
            }
        }
        AggregateData(mergedData, aggregatedData);
        return aggregatedData.size();
    }

    std::vector<PmuData>& PmuList::GetPreviousData(const unsigned pd)
    {
        std::vector<PmuData>* lastData = nullptr;
        int64_t maxTs = 0;

        for (auto& pair : userDataList) {
            if (pair.second.pd == pd && !pair.second.data.empty() && pair.second.data[0].ts > maxTs) {
                maxTs = pair.second.data[0].ts;
                lastData = &pair.second.data;
            }
        }
        if (lastData != nullptr) {
            return *lastData;
        }
        throw runtime_error("");
    }

    int PmuList::AddToEpollFd(const int pd, const std::shared_ptr<EvtList> &evtList)
    {
        lock_guard<mutex> lg(pmuListMtx);
        // Try to create a epoll fd for current pd.
        int epollFd = 0;
        auto findFd = epollList.find(pd);
        if (findFd == epollList.end()) {
            epollFd = epoll_create1(0);
            if (epollFd < 0) {
                return LIBPERF_ERR_FAIL_LISTEN_PROC;
            }
            epollList[pd] = epollFd;
        } else {
            epollFd = findFd->second;
        }

        // Add ring buffer fd list to epoll fd.
        auto& epollEvtList = epollEvents[epollFd];
        for (auto fd : evtList->GetFdList()) {
            epollEvtList.emplace_back(epoll_event{0});
            auto& epollEvt = epollEvtList.back();
            epollEvt.events = EPOLLIN | EPOLLRDHUP;
            epollEvt.data.fd = fd;
            auto ret = epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &epollEvt);
            if (ret != 0) {
                return LIBPERF_ERR_FAIL_LISTEN_PROC;
            }
        }

        return SUCCESS;
    }

    void PmuList::RemoveEpollFd(const int pd)
    {
        lock_guard<mutex> lg(pmuListMtx);
        auto findFd = epollList.find(pd);
        if (findFd != epollList.end()) {
            close(findFd->second);
            epollEvents.erase(findFd->second);
            epollList.erase(pd);
        }
    }

    int PmuList::GetEpollFd(const int pd)
    {
        lock_guard<mutex> lg(pmuListMtx);
        auto findFd = epollList.find(pd);
        if (findFd != epollList.end()) {
            return findFd->second;
        }
        return -1;
    }

    std::vector<epoll_event>& PmuList::GetEpollEvents(const int epollFd)
    {
        lock_guard<mutex> lg(pmuListMtx);
        auto findEvts = epollEvents.find(epollFd);
        if (findEvts != epollEvents.end()) {
            return findEvts->second;
        }

        // Cannot reach here.
        throw runtime_error("cannot find epoll events.");
    }

    bool PmuList::IsCpuInList(const int &cpu) const
    {
        lock_guard<mutex> lg(pmuListMtx);
        for (auto cpuList: speCpuList) {
            if (cpuList.second.find(cpu) != cpuList.second.end()) {
                return true;
            }
        }
        return false;
    }

    void PmuList::AddSpeCpu(const unsigned &pd, const int &cpu)
    {
        lock_guard<mutex> lg(pmuListMtx);
        speCpuList[pd].insert(cpu);
    }

    void PmuList::EraseSpeCpu(const unsigned &pd)
    {
        lock_guard<mutex> lg(pmuListMtx);
        speCpuList.erase(pd);
    }

    int PmuList::PrepareCpuTopoList(
            const unsigned &pd, PmuTaskAttr* pmuTaskAttrHead, std::vector<CpuPtr>& cpuTopoList)
    {
        for (int i = 0; i < pmuTaskAttrHead->numCpu; i++) {
            if (pmuTaskAttrHead->pmuEvt->collectType == SPE_SAMPLING && IsCpuInList(pmuTaskAttrHead->cpuList[i])) {
                // For SPE sampling, one core can only be used by one pd.
                // Therefore, check if core is in sampling.
                return LIBPERF_ERR_DEVICE_BUSY;
            }
            struct CpuTopology* cpuTopo = GetCpuTopology(pmuTaskAttrHead->cpuList[i]);
            if (cpuTopo == nullptr) {
                New(LIBPERF_ERR_FAIL_GET_CPU);
                return LIBPERF_ERR_FAIL_GET_CPU;
            }
            if (pmuTaskAttrHead->pmuEvt->collectType == SPE_SAMPLING) {
                AddSpeCpu(pd, pmuTaskAttrHead->cpuList[i]);
            }
            cpuTopoList.emplace_back(shared_ptr<CpuTopology>(cpuTopo));
        }
        return SUCCESS;
    }

    int PmuList::PrepareProcTopoList(PmuTaskAttr* pmuTaskAttrHead, std::vector<ProcPtr>& procTopoList) const
    {
        if (pmuTaskAttrHead->numPid == 0) {
            struct ProcTopology* procTopo = GetProcTopology(-1);
            if (procTopo == nullptr) {
                New(LIBPERF_ERR_FAIL_GET_PROC);
                return LIBPERF_ERR_FAIL_GET_PROC;
            }
            procTopoList.emplace_back(unique_ptr<ProcTopology, void (*)(ProcTopology*)>(procTopo, FreeProcTopo));
        }
        for (int i = 0; i < pmuTaskAttrHead->numPid; i++) {
            int numChild = 0;
            int* childTidList = GetChildTid(pmuTaskAttrHead->pidList[i], &numChild);
            if (childTidList == nullptr) {
                return LIBPERF_ERR_INVALID_PID;
            }
            bool foundProc = false;
            for (int j = 0; j < numChild; j++) {
                struct ProcTopology* procTopo = GetProcTopology(childTidList[j]);
                if (procTopo == nullptr) {
                    SetWarn(LIBPERF_WARN_FAIL_GET_PROC, "process not found: " + std::to_string(childTidList[j]));
                    continue;
                }
                foundProc = true;
                DBG_PRINT("Add to proc map: %d\n", childTidList[j]);
                procTopoList.emplace_back(shared_ptr<ProcTopology>(procTopo, FreeProcTopo));
            }
            delete[] childTidList;
            if (!foundProc) {
                New(LIBPERF_ERR_FAIL_GET_PROC, "process not found: " + std::to_string(pmuTaskAttrHead->pidList[i]));
                return LIBPERF_ERR_FAIL_GET_PROC;
            }
        }
        return SUCCESS;
    }

    void PmuList::SetSymbolMode(const int pd, const SymbolMode &mode)
    {
        lock_guard<mutex> lg(dataListMtx);
        symModeList[pd] = mode;
    }

    SymbolMode PmuList::GetSymbolMode(const unsigned pd)
    {
        lock_guard<mutex> lg(dataListMtx);
        return symModeList[pd];
    }
}