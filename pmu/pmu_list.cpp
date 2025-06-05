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
#include "trace_point_parser.h"
#include "pmu_event_list.h"
#include "pmu_list.h"
#include "pfm_event.h"

using namespace std;
using namespace pcerr;

namespace KUNPENG_PMU {
// Initializing pmu list singleton instance and global lock
    std::mutex PmuList::pmuListMtx;
    std::mutex PmuList::dataEvtGroupListMtx;
    std::mutex PmuList::dataListMtx;
    std::mutex PmuList::dataParentMtx;

    static uint64_t PredictRequiredMemory(int collectType, uint64_t cpuNum, uint64_t pidNum) {
        if (collectType == SAMPLING) {
            uint64_t predictMmapNum = cpuNum * pidNum;
            uint64_t reservedSpace  = 2 * 1024 * 1024;
            uint64_t mmapSpaceSize  = sizeof(struct PerfMmap) * predictMmapNum;
            // copiedEvent memory and mmap memory and reserved space memory, mmap memory 528384 is PAGE_SIZE * (pages + 1)
            uint64_t needBytesNum = predictMmapNum * (PERF_SAMPLE_MAX_SIZE) + predictMmapNum * (SAMPLE_PAGES + 1) * SAMPLE_PAGE_SIZE + reservedSpace + mmapSpaceSize;
            return needBytesNum;
        }
        return 0;
    }

    static bool PredictRemainSpaceIsEnough(uint64_t needBytesNum) {
        if (needBytesNum == 0) {
            return true;
        }

        ifstream file("/proc/meminfo");
        if (!file.is_open()) {
            DBG_PRINT("failed to open /proc/meminfo, unable to predict\n");
            return false;
        }

        string line;
        const int strLen = 1024;
        while(std::getline(file, line)) {
            if (line.empty() || !strstr(line.c_str(), "MemFree")) {
                continue;
            }
            char memoryUint[strLen];
            uint64_t memoryFreeNum;
            if (sscanf(line.c_str(), "%*s %lu %s", &memoryFreeNum, &memoryUint) != 2) {
                DBG_PRINT("failed to open parse MemFree data, unable to predict\n");
                break;
            }
         
            char firstUint = tolower(memoryUint[0]);
            switch (firstUint) {
                case 'k':
                    memoryFreeNum = memoryFreeNum * 1024;
                    break;
                case 'm':
                    memoryFreeNum = memoryFreeNum * 1024 * 1024;
                    break;
                case 'g':
                    memoryFreeNum = memoryFreeNum * 1024 * 1024 * 1024;
                    break;
                default:
                    break;
            }
            if (needBytesNum < memoryFreeNum) {
                return true;
            }
            DBG_PRINT("Predict memory is not enough\n");
        }
        return false;
    }

    int PmuList::CheckRlimit(const unsigned fdNum)
    {
        return RaiseNumFd(fdNum);
    }

    unsigned PmuList::CalRequireFd(unsigned cpuSize, unsigned proSize, const unsigned collectType)
    {
        unsigned fd = cpuSize * proSize;
        if (collectType == SPE_SAMPLING) {
            fd += fd;// spe would open dummy event.
        }
        return fd;
    }

    int PmuList::Register(const int pd, PmuTaskAttr* taskParam)
    {
        this->FillPidList(taskParam, pd);
        int symbolErrNo = InitSymbolRecordModule(pd, taskParam);
        if (symbolErrNo != SUCCESS) {
            return symbolErrNo;
        }
        /* Use libpfm to get the basic config for this pmu event */
        struct PmuTaskAttr* pmuTaskAttrHead = taskParam;
        // Init collect type for pmu data,
        // because different type has different free strategy.
        auto& evtData = GetDataList(pd);
        if (pmuTaskAttrHead != nullptr) {
            evtData.collectType = static_cast<PmuTaskType>(pmuTaskAttrHead->pmuEvt->collectType);
            evtData.pd = pd;
        }

        unsigned fdNum = 0;
        uint64_t needBytesNum = 0;
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
            fdNum += CalRequireFd(cpuTopoList.size(), procTopoList.size(), taskParam->pmuEvt->collectType);
            std::shared_ptr<EvtList> evtList =
                    std::make_shared<EvtList>(GetSymbolMode(pd), cpuTopoList, procTopoList, pmuTaskAttrHead->pmuEvt, pmuTaskAttrHead->group_id);
            needBytesNum += PredictRequiredMemory(taskParam->pmuEvt->collectType, cpuTopoList.size(), procTopoList.size());
            evtList->SetBranchSampleFilter(GetBranchSampleFilter(pd));
            InsertEvtList(pd, evtList);
            pmuTaskAttrHead = pmuTaskAttrHead->next;
        }

        auto err = CheckRlimit(fdNum);
        if (err != SUCCESS) {
            return err;
        }

        err = Init(pd, PredictRemainSpaceIsEnough(needBytesNum));
        if (err != SUCCESS)
        {
            return err;
        }
        
        this->OpenDummyEvent(taskParam, pd);
        return SUCCESS;
    }
    
    int PmuList::EvtInit(const bool groupEnable, const std::shared_ptr<EvtList> evtLeader, const int pd, const std::shared_ptr<EvtList> &evtList, bool isMemoryEnough)
    {
        auto err = evtList->Init(groupEnable, evtLeader, isMemoryEnough);
        if (err != SUCCESS) {
            return err;
        }

        err = AddToEpollFd(pd, evtList);
        if (err != SUCCESS) {
            return err;
        }

        return SUCCESS;
    }

    int PmuList::Init(const int pd, bool isMemoryEnough)
    {
        std::unordered_map<int, struct EventGroupInfo> eventGroupInfoMap;
        for (auto& evtList : GetEvtList(pd)) {
            if (evtList->GetGroupId() == -1) {
                auto err = EvtInit(false, nullptr, pd, evtList, isMemoryEnough);
                if (err != SUCCESS) {
                    return err;
                }
                continue;
            } 
            if (eventGroupInfoMap.find(evtList->GetGroupId()) == eventGroupInfoMap.end()) {
                auto err = EvtInit(false, nullptr, pd, evtList, isMemoryEnough);
                if (err != SUCCESS) {
                    return err;
                }
                eventGroupInfoMap[evtList->GetGroupId()].evtLeader = evtList;
                eventGroupInfoMap[evtList->GetGroupId()].uncoreState = static_cast<UncoreState>(UncoreState::InitState);
            } else {
                eventGroupInfoMap[evtList->GetGroupId()].evtGroupChildList.push_back(evtList);
            }
            if (evtList->GetPmuType() == static_cast<PMU_TYPE>(UNCORE_TYPE) || evtList->GetPmuType() == static_cast<PMU_TYPE>(UNCORE_RAW_TYPE)) {
                eventGroupInfoMap[evtList->GetGroupId()].uncoreState = static_cast<UncoreState>(static_cast<int>(UncoreState::HasUncore) | 
                                                                    static_cast<int>(eventGroupInfoMap[evtList->GetGroupId()].uncoreState));
            } else {
                eventGroupInfoMap[evtList->GetGroupId()].uncoreState = static_cast<UncoreState>(static_cast<int>(UncoreState::HasUncore) & 
                                                    static_cast<int>(eventGroupInfoMap[evtList->GetGroupId()].uncoreState));
            }
        }
        // handle the event group child event Init
        for (auto evtGroup : eventGroupInfoMap) {
            for (auto evtChild : evtGroup.second.evtGroupChildList) {
                if (eventGroupInfoMap[evtChild->GetGroupId()].uncoreState == static_cast<UncoreState>(UncoreState::OnlyUncore)) {
                    return LIBPERF_ERR_INVALID_GROUP_ALL_UNCORE;
                }
                int err = 0;
                if (eventGroupInfoMap[evtChild->GetGroupId()].uncoreState == static_cast<UncoreState>(UncoreState::HasUncore)) {
                    SetWarn(LIBPERF_WARN_INVALID_GROUP_HAS_UNCORE);
                    err = EvtInit(false, nullptr, pd, evtChild, isMemoryEnough);
                } else {
                    err = EvtInit(true, eventGroupInfoMap[evtChild->GetGroupId()].evtLeader, pd, evtChild, isMemoryEnough);
                }
                if (err != SUCCESS) {
                    return err;
                }
            }
        }
        groupMapPtr eventDataEvtGroup = std::make_shared<std::unordered_map<int, EventGroupInfo>>(eventGroupInfoMap);
        InsertDataEvtGroupList(pd, eventDataEvtGroup);

        return SUCCESS;
    }

    int PmuList::Start(const int pd)
    {
        auto pmuList = GetEvtList(pd);
        for (auto item: pmuList) {
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
        for (auto item: pmuList) {
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

        auto& evtData = GetDataList(pd);
        if (evtData.data.empty()) {
            // Have not read ring buffer yet.
            // Mostly caller is using PmuEnable and PmuDisable mode.
            auto err = ReadDataToBuffer(pd);
            if (err != SUCCESS) {
                return userDataList[nullptr].data;
            }
        }
        auto& userData = ExchangeToUserData(pd);
        return userData;
    }

    static void TrimKernelStack(PmuData &data)
    {
        auto stack = data.stack;
        while (stack != nullptr && stack->symbol != nullptr) {
            if (strcmp(stack->symbol->module, "[kernel]") == 0) {
                stack = stack->next;
                continue;
            }
            data.stack = stack;
            break;
        }
    }

    void HandleBlockData(std::vector<PmuData>& pmuData, std::vector<PmuSwitchData>& switchData)
    {
        std::sort(switchData.begin(), switchData.end(), [](const PmuSwitchData& a, const PmuSwitchData& b) {
            if (a.tid == b.tid) {
                return a.ts < b.ts;
            }
            return a.tid < b.tid;
        });
        std::unordered_map<int, std::vector<int64_t>> tidToOffTimeStamps;
        int64_t outTime = 0;
        int prevTid = -1;
        for (const auto& item: switchData) {
            if (item.swOut) {
                outTime = item.ts;
                prevTid = item.tid;
                DBG_PRINT("Switch out: tid=%d, ts=%llu\n", item.tid, item.ts);
            } else {
                // if the first event is sched_in, we need to ignore it.
                if (prevTid == -1) {
                    DBG_PRINT("Ignoring first sched_in event: tid=%d, ts=%ld\n", item.tid, item.ts);
                    continue;
                }
                if (prevTid == item.tid && outTime > 0) {
                    tidToOffTimeStamps[item.tid].emplace_back(item.ts - outTime);
                    DBG_PRINT("Switch in: tid=%d, ts=%llu, offTime=%llu\n", item.tid, item.ts, item.ts - outTime);
                    outTime = 0;
                }
            }
        }

        std::sort(pmuData.begin(), pmuData.end(), [](const PmuData& a, const PmuData& b) {
            if (a.tid == b.tid) {
                return a.ts < b.ts;
            }
            return a.tid < b.tid;
        });
        int csCnt = 0;
        int64_t prevTs = 0;
        int64_t currentTs = 0;
        int64_t curPeriod = 0;
        int currentTid = -1;
        for (auto& item: pmuData) {
            if (currentTid != item.tid) {
                currentTid = item.tid;
                csCnt = 0;
                prevTs = 0;
                currentTs = 0;
                curPeriod = 0;
                DBG_PRINT("New tid encountered: tid=%d\n", currentTid);
            }
            if (strcmp(item.evt, "context-switches") == 0) {
                // Convert stack from 'schedule[kernel] -> futex_wait[kernel] -> ...[kernel] -> lock_wait -> start_thread'
                // to 'lock_wait -> start_thread', only keeping user stack.
                TrimKernelStack(item);
                // Before the context-switches event, there is only one cycles event, which we need to ignore. 
                if (currentTs == 0) {
                    currentTs = item.ts;
                    DBG_PRINT("Ignoring first cycles event for tid=%d\n", item.tid);
                    continue;
                }
                // only the on cpu event is cycles, this compute is right.
                if (csCnt < tidToOffTimeStamps[item.tid].size()) {
                    item.period = tidToOffTimeStamps[item.tid][csCnt] * curPeriod / (currentTs - prevTs);
                    DBG_PRINT("Context switch: ts=%llu, tid=%d, period=%llu\n", item.ts, item.tid, item.period);
                    csCnt++;
                }
            } else {
                // on cpu event data update.
                prevTs = currentTs;
                currentTs = item.ts;
                curPeriod = item.period;
            }
        }
    }
    
    int PmuList::ReadDataToBuffer(const int pd)
    {
        // Read data from prev sampling,
        // and store data in <dataList>.
        auto& evtData = GetDataList(pd);
        evtData.pd = pd;
        evtData.collectType = static_cast<PmuTaskType>(GetTaskType(pd));
        auto ts = GetCurrentTime();
        auto eventList = GetEvtList(pd);
        for (auto item: eventList) {
            item->SetTimeStamp(ts);
            auto err = item->Read(evtData.data, evtData.sampleIps, evtData.extPool, evtData.switchData);
            if (err != SUCCESS) {
                return err;
            }
        }

        return SUCCESS;
    }

    int PmuList::AppendData(PmuData* fromData, PmuData** toData, int& len)
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
        if (findToData == userDataList.end()) {
            return LIBPERF_ERR_INVALID_PMU_DATA;
        }
        // For non-null target data list, append source list to end of target vector.
        auto& dataVec = findToData->second.data;
        auto& ipsVec = findToData->second.sampleIps;
        dataVec.insert(dataVec.end(), findFromData->second.data.begin(), findFromData->second.data.end());
        ipsVec.insert(ipsVec.end(), findFromData->second.sampleIps.begin(), findFromData->second.sampleIps.end());
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

    void PmuList::StoreSplitData(const unsigned pd, pair<unsigned, char**>& previousEventList,
                                 unordered_map<string, char*>& eventSplitMap)
    {
        lock_guard<mutex> lg(dataParentMtx);
        parentEventMap.emplace(pd, move(eventSplitMap));
        previousEventMap.emplace(pd, move(previousEventList));
    }

    void PmuList::Close(const int pd)
    {
        auto evtList = GetEvtList(pd);
        for (auto item: evtList) {
            item->Close();
        }
        EraseEvtList(pd);
        EraseSymModeList(pd);
        ErasePpidList(pd);
        EraseDataList(pd);
        EraseDataEvtGroupList(pd);
        RemoveEpollFd(pd);
        EraseSpeCpu(pd);
        EraseDummyEvent(pd);
        EraseParentEventMap(pd);
        SymResolverDestroy();
        PmuEventListFree();
        TraceParser::FreeRawFieldMap();
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
        for (auto& evt: epollEvents) {
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

    int PmuList::GetBlockedSampleState(const int pd) const
    {
        lock_guard<mutex> lg(pmuListMtx);
        auto findEvtList = pmuList.find(pd);
        if (findEvtList == pmuList.end()) {
            return -1;
        }
        if (findEvtList->second.empty()) {
            return -1;
        }
        return findEvtList->second[0]->GetBlockedSample();
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

    void PmuList::EraseSymModeList(const unsigned pd)
    {
        lock_guard<mutex> lg(dataListMtx);
        symModeList.erase(pd);
    }

    void PmuList::ErasePpidList(const unsigned pd)
    {
        ppidList.erase(pd);
    }

    void PmuList::InsertDataEvtGroupList(const unsigned pd, groupMapPtr evtGroupList)
    {
        lock_guard<mutex> lg(dataEvtGroupListMtx);
        dataEvtGroupList[pd] = evtGroupList;
    }

    void PmuList::EraseDataEvtGroupList(const unsigned pd)
    {
        lock_guard<mutex> lg(dataEvtGroupListMtx);
        dataEvtGroupList.erase(pd);
    }

    groupMapPtr& PmuList::GetDataEvtGroupList(const unsigned pd)
    {
        lock_guard<mutex> lg(dataEvtGroupListMtx);
        return dataEvtGroupList[pd];
    }

    void PmuList::EraseParentEventMap(const unsigned pd)
    {
        lock_guard<mutex> lg(dataParentMtx);
        auto iter = parentEventMap.find(pd);
        if (iter != parentEventMap.end()) {
            parentEventMap.at(pd).clear();
            parentEventMap.erase(iter);
        }
        auto preIter = previousEventMap.find(pd);
        if (preIter != previousEventMap.end()) {
            auto pair = previousEventMap.at(pd);
            for (int i = 0; i < pair.first; i++) {
                delete[] pair.second[i];
            }
            delete[] pair.second;
            previousEventMap.erase(preIter);
        }
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
        for (auto iter = userDataList.begin(); iter != userDataList.end();) {
            if (iter->second.pd == pd) {
                iter = userDataList.erase(iter);
            } else {
                ++iter;
            }
        }
    }

    void PmuList::FillStackInfo(EventData& eventData)
    {
        auto symMode = symModeList[eventData.pd];
        // Parse dwarf and elf info of each pid and get stack trace for each pmu data.
        for (size_t i = 0; i < eventData.data.size(); ++i) {
            auto& pmuData = eventData.data[i];
            auto& ipsData = eventData.sampleIps[i];
            if (symMode == RESOLVE_ELF) {
                SymResolverRecordModuleNoDwarf(pmuData.pid);
            } else if (symMode == RESOLVE_ELF_DWARF) {
                SymResolverRecordModule(pmuData.pid);
            } else if (symMode == NO_SYMBOL_RESOLVE) {
                SymResolverRecordModule(pmuData.pid);
                continue;
            } else {
                continue;
            }

            if (pmuData.stack == nullptr) {
                pmuData.stack = StackToHash(pmuData.pid, ipsData.ips.data(), ipsData.ips.size());
            }
        }
        //Exceptions generated by the symbol interface are not directly exposed and are processed as warnings.
        int err = Perrorno();
        if (err < LIBPERF_ERR_NO_AVAIL_PD && err >= LIBSYM_ERR_BASE) {
            pcerr::SetWarn(err, Perror());
            New(SUCCESS);
        }
    }

    int PmuList::ResolvePmuDataSymbol(struct PmuData* iPmuData) 
    {
        if (iPmuData == nullptr) {
            New(LIBPERF_ERR_INVALID_PMU_DATA, "ipmuData is nullptr");
            return LIBPERF_ERR_INVALID_PMU_DATA;
        }
        auto userData = userDataList.find(iPmuData);
        if (userData == userDataList.end()) {
            New(LIBPERF_ERR_PMU_DATA_NO_FOUND, "ipmuData isn't in userDataList");
            return LIBPERF_ERR_PMU_DATA_NO_FOUND;
        }

        auto& eventData = userDataList[iPmuData];
        auto symMode = symModeList[eventData.pd];
        for (size_t i = 0; i < eventData.data.size(); ++i) {
            auto& pmuData = eventData.data[i];
            auto& ipsData = eventData.sampleIps[i];
            if (pmuData.stack == nullptr) {
                pmuData.stack = StackToHash(pmuData.pid, ipsData.ips.data(), ipsData.ips.size());
            }
        }
        New(SUCCESS);
        return SUCCESS;
    }

    void PmuList::AggregateData(const std::vector<PmuData>& evData, std::vector<PmuData>& newEvData)
    {
        // Acccumulate stat data in previous PmuCollect for convenient use.
        // One count for same event + tid + cpu.
        map<std::tuple<string, int, unsigned>, PmuData> mergedMap;
        for (auto& data: evData) {
            auto key = std::make_tuple(
                    data.evt, data.tid, data.cpu);
            if (mergedMap.find(key) == mergedMap.end()) {
                mergedMap[key] = data;
            } else {
                mergedMap[key].count += data.count;
            }
        }
        for (auto& evtData: mergedMap) {
            newEvData.push_back(evtData.second);
        }
    }
    
    
    void PmuList::AggregateUncoreData(const unsigned pd, const vector<PmuData>& evData, vector<PmuData>& newEvData)
    {
        // One count for same parent according to parentEventMap.
        auto parentMap = parentEventMap.at(pd);
        unordered_map<string, PmuData> dataMap;
        vector<string> dataMapKeys;
        for (auto& pmuData: evData) {
            auto parentName = parentMap.at(pmuData.evt);
            if (strcmp(parentName, pmuData.evt) == 0) {
                // collect aggregate event by order, when aggregate event is the middle of eventList
                if (dataMap.size() == 1) {
                    auto it = dataMap.begin();
                    newEvData.emplace_back(it->second);
                    dataMap.erase(it);
                    dataMapKeys.clear();
                }
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
                dataMapKeys.push_back(parentName);
            } else {
                dataMap.at(parentMap.at(pmuData.evt)).count += pmuData.count;
            }
        }
        // if aggregate event is the last event in eventList
        for (auto& key: dataMapKeys) {
            newEvData.emplace_back(dataMap.at(key));
        }
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
                    .data = move(newPmuData),
            };

            auto inserted = userDataList.emplace(newEvData.data.data(), move(newEvData));
            dataList.erase(pd);
            return inserted.first->second.data;
        } else {
            FillStackInfo(evData);
            if (GetBlockedSampleState(pd) == 1) {
                HandleBlockData(evData.data, evData.switchData);
            }
            auto inserted = userDataList.emplace(pData, move(evData));
            dataList.erase(pd);
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
        if (findData->second.collectType == SAMPLING) {
            for (auto &extMem : findData->second.extPool) {
                if (extMem->branchRecords) {
                    delete[] extMem->branchRecords;
                }
                delete extMem;
            }
        } else if (findData->second.collectType == SPE_SAMPLING) {
            // Delete ext pointer malloced in SpeSampler.
            for (auto &extMem : findData->second.extPool) {
                delete[] extMem;
            }
        }

        for (auto pd: findData->second.data) {
            if (pd.rawData != nullptr) {
                TraceParser::FreeTraceData(pd.rawData->data);
                free(pd.rawData);
                pd.rawData = nullptr;
            }
        }
        userDataList.erase(pmuData);
    }

    int PmuList::GetHistoryData(const int pd, std::vector<PmuData>& aggregatedData)
    {
        lock_guard<mutex> lg(dataListMtx);
        std::vector<PmuData> mergedData;
        for (const auto& pair: userDataList) {
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

        for (auto& pair: userDataList) {
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

    int PmuList::AddToEpollFd(const int pd, const std::shared_ptr<EvtList>& evtList)
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
        for (auto fd: evtList->GetFdList()) {
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

    bool PmuList::IsCpuInList(const int& cpu) const
    {
        lock_guard<mutex> lg(pmuListMtx);
        for (auto cpuList: speCpuList) {
            if (cpuList.second.find(cpu) != cpuList.second.end()) {
                return true;
            }
        }
        return false;
    }

    void PmuList::AddSpeCpu(const unsigned& pd, const int& cpu)
    {
        lock_guard<mutex> lg(pmuListMtx);
        speCpuList[pd].insert(cpu);
    }

    void PmuList::EraseSpeCpu(const unsigned& pd)
    {
        lock_guard<mutex> lg(pmuListMtx);
        speCpuList.erase(pd);
    }

    int PmuList::PrepareCpuTopoList(
            const unsigned& pd, PmuTaskAttr* pmuTaskAttrHead, std::vector<CpuPtr>& cpuTopoList)
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
            int masterPid = pmuTaskAttrHead->pidList[i];
            int numChild = 0;
            int* childTidList = GetChildTid(masterPid, &numChild);
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
                procTopo->isMain = masterPid == procTopo->tid;
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

    void PmuList::SetSymbolMode(const int pd, const SymbolMode& mode)
    {
        lock_guard<mutex> lg(dataListMtx);
        symModeList[pd] = mode;
    }

    void PmuList::SetBranchSampleFilter(const int pd, const unsigned long& branchSampleFilter)
    {
        lock_guard<mutex> lg(dataListMtx);
        branchSampleFilterList[pd] = branchSampleFilter;
    }

    SymbolMode PmuList::GetSymbolMode(const unsigned pd)
    {
        lock_guard<mutex> lg(dataListMtx);
        return symModeList[pd];
    }

    unsigned long  PmuList::GetBranchSampleFilter(const unsigned pd)
    {
        lock_guard<mutex> lg(dataListMtx);
        return branchSampleFilterList[pd];
    }


    void PmuList::OpenDummyEvent(KUNPENG_PMU::PmuTaskAttr* taskParam, const unsigned pd)
    {
        if (!taskParam->pmuEvt->includeNewFork) {
            return;
        }
        if (taskParam->pmuEvt->collectType != COUNTING) {
            return;
        }
        if (taskParam->numPid <= 0) {
            return;
        }
        auto* dummyEvent = new DummyEvent(GetEvtList(pd), ppidList.at(pd), GetDataEvtGroupList(pd));
        dummyEvent->ObserverForkThread();
        dummyList[pd] = dummyEvent;
    }

    void PmuList::FillPidList(KUNPENG_PMU::PmuTaskAttr* taskParam, const unsigned int pd)
    {
        std::vector<pid_t> ppids;
        for (int i = 0; i < taskParam->numPid; i++) {
            ppids.push_back(taskParam->pidList[i]);
        }
        ppidList[pd] = ppids;
    }

    bool PmuList::IsAllPidExit(const unsigned pd)
    {
        auto& pidList = this->ppidList[pd];
        if (pidList.empty()) {
            return false;
        }
        int exitPidNum = 0;
        for (const auto& pid: pidList) {
            std::string path = "/proc/" + std::to_string(pid);
            if (!ExistPath(path)) {
                exitPidNum++;
            }
        }
        if (exitPidNum == pidList.size()) {
            return true;
        }
        return false;
    }

    void PmuList::EraseDummyEvent(const unsigned pd)
    {
        lock_guard<mutex> lg(pmuListMtx);
        if (dummyList.find(pd) != dummyList.end()) {
            DummyEvent* pEvent = dummyList.at(pd);
            dummyList.erase(pd);
            delete pEvent;
        }
    }

    int PmuList::InitSymbolRecordModule(const unsigned pd, PmuTaskAttr* taskParam)
    {
        SymbolMode symMode = GetSymbolMode(pd);

        if (taskParam->pmuEvt->collectType == COUNTING) {
            return SUCCESS;
        }

        SymResolverInit();
        int ret = SymResolverRecordKernel();
        if (ret != SUCCESS) {
            return ret;
        }

        std::vector<int> pidList = this->ppidList[pd];
        if (pidList.empty()) {
            return SUCCESS;
        }

        if (this->symModeList[pd] == RESOLVE_ELF) {
            for (const auto& pid: pidList) {
                int rt = SymResolverRecordModuleNoDwarf(pid);
                if (rt != SUCCESS) {
                    return ret;
                }
            }
        }

        if (this->symModeList[pd] == RESOLVE_ELF_DWARF || this->symModeList[pd] == NO_SYMBOL_RESOLVE) {
            for (const auto& pid: pidList) {
                int rt = SymResolverRecordModule(pid);
                if (rt != SUCCESS) {
                    return ret;
                }
            }
        }
        return SUCCESS;
    }
}