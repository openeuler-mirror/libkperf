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
 * Description: definition of singleton class PmuList for managing performance monitoring tasks,
 * collecting data, and handling performance counters in the KUNPENG_PMU namespace
 ******************************************************************************/
#ifndef PMU_LIST_H
#define PMU_LIST_H
#include <mutex>
#include <vector>
#include <unordered_map>
#include <sys/epoll.h>
#include "dummy_event.h"
#include "evt_list.h"
#include "pmu_event.h"

namespace KUNPENG_PMU {

struct PmuTaskAttr {
    int numCpu;                     // number of cpu to be collected
    int* cpuList;                   // list of core ids to be collected
                                    // list length has to be as the same as numCpu
    int numPid;                     // number of (parent) processes  to be collected
    int* pidList;                   // list of pids(tids) to be collected
                                    // list length has to be as the same as numPid
    std::shared_ptr<PmuEvt> pmuEvt;     // which pmu to be collected

    int groupId;                   // event group id
    
    struct PmuTaskAttr* next;       // next task attribute
};

class PmuList {
public:
    static PmuList* GetInstance()
    {
        static PmuList instance;
        return &instance;
    }
    int Register(const int pd, PmuTaskAttr* taskParam);
    /**
     * @brief Read all pmu data of event list, and store data to internal buffer.
     * @param pd
     */
    int ReadDataToBuffer(const int pd);
    /**
     * @brief Read pmu data from internal buffer and return ref.
     * @param pd
     * @return std::vector<PmuData>&
     */
    std::vector<PmuData>& Read(const int pd);
    int AppendData(PmuData* fromData, PmuData** toData, int& len);
    int Start(const int pd);
    int Pause(const int pd);
    void Close(const int pd);
    bool AllPmuDead(const int pd);
    bool IsPdAlive(const int pd) const;
    void FreeData(PmuData* pmuData);
    int GetTaskType(const int pd) const;
    int GetBlockedSampleState(const int pd) const;
    void SetSymbolMode(const int pd, const SymbolMode &mode);
    void SetBranchSampleFilter(const int pd, const unsigned long& branchSampleFilter);

    int NewPd();

    int GetHistoryData(const int pd, std::vector<PmuData>& pmuData);
    void StoreSplitData(unsigned pd, std::pair<unsigned, char**>& previousEventList,
                        std::unordered_map<std::string, char*>& eventSplitMap);
    bool IsAllPidExit(const unsigned pd);
    int ResolvePmuDataSymbol(struct PmuData* iPmuData);

private:
    using ProcPtr = std::shared_ptr<ProcTopology>;
    using CpuPtr = std::shared_ptr<CpuTopology>;
    PmuList()
    {}
    PmuList(const PmuList&) = delete;
    PmuList& operator=(const PmuList&) = delete;
    ~PmuList() = default;

    struct EventData {
        unsigned pd;
        PmuTaskType collectType;
        std::vector<PmuData> data;
        std::vector<PerfSampleIps> sampleIps;
        std::vector<PmuDataExt*> extPool;
        std::vector<PmuSwitchData> switchData;
    };

    void InsertEvtList(const unsigned pd, std::shared_ptr<EvtList> evtList);
    std::vector<std::shared_ptr<EvtList>>& GetEvtList(const unsigned pd);
    void EraseEvtList(const unsigned pd);
    void EraseParentEventMap(const unsigned pd);
    void EraseSymModeList(const unsigned pd);
    void ErasePpidList(const unsigned pd);

    int EvtInit(const bool groupEnable, const std::shared_ptr<EvtList> evtLeader, const int pd, const std::shared_ptr<EvtList> &evtList, bool isMemoryEnough);
    int Init(const int pd, bool isMemoryEnough);

    void InsertDataEvtGroupList(const unsigned pd, groupMapPtr evtGroupList);
    void EraseDataEvtGroupList(const unsigned pd);
    groupMapPtr& GetDataEvtGroupList(const unsigned pd);

    EventData& GetDataList(const unsigned pd);
    void EraseDataList(const unsigned pd);
    // Move pmu data from dataList to userDataList,
    // and return ref of dataList in userDataList.
    std::vector<PmuData>& ExchangeToUserData(const unsigned pd);
    void FillStackInfo(EventData &eventData);
    void EraseUserData(PmuData* pmuData);

    int AddToEpollFd(const int pd, const std::shared_ptr<EvtList> &evtList);
    void RemoveEpollFd(const int pd);
    int GetEpollFd(const int pd);
    std::vector<epoll_event>& GetEpollEvents(const int epollFd);

    bool IsCpuInList(const int &cpu) const;
    void AddSpeCpu(const unsigned &pd, const int &cpu);
    void EraseSpeCpu(const unsigned &pd);
    int PrepareCpuTopoList(
        const unsigned& pd, PmuTaskAttr* pmuTaskAttrHead, std::vector<CpuPtr>& cpuTopoList);
    int PrepareProcTopoList(PmuTaskAttr* pmuTaskAttrHead, std::vector<ProcPtr>& procTopoList) const;
    int CheckRlimit(const unsigned fdNum);
    static unsigned CalRequireFd(unsigned cpuSize, unsigned proSize, const unsigned collectType);
    static void AggregateData(const std::vector<PmuData>& evData, std::vector<PmuData>& newEvData);
    void AggregateUncoreData(const unsigned pd, const std::vector<PmuData> &evData, std::vector<PmuData> &newEvData);
    std::vector<PmuData>& GetPreviousData(const unsigned pd);
    SymbolMode GetSymbolMode(const unsigned pd);
    unsigned long GetBranchSampleFilter(const unsigned pd);
    void FillPidList(PmuTaskAttr* taskParam, const unsigned pd);
    void OpenDummyEvent(PmuTaskAttr* taskParam, const unsigned pd);
    void EraseDummyEvent(const unsigned pd);
    int InitSymbolRecordModule(const unsigned pd, PmuTaskAttr* taskParam);

    static std::mutex pmuListMtx;
    static std::mutex dataListMtx;
    static std::mutex dataEvtGroupListMtx;
    static std::mutex dataParentMtx;
    std::unordered_map<unsigned, std::vector<std::shared_ptr<EvtList>>> pmuList;
    // Key: pd
    // Value: PmuData List.
    // PmuData is stored here before user call <read>.
    std::unordered_map<unsigned, EventData> dataList;
    // Key: PmuData raw pointer
    // Value: PmuData vector for raw pointer.
    // PmuData is stored here after user call <read>.
    std::unordered_map<PmuData*, EventData> userDataList;
    // Key1: pd
    // Key2: parent event
    // Value: child event name list
    // parent event name is stored here before user call <read> to aggregate event.
    std::unordered_map<unsigned, std::unordered_map<std::string, char*>> parentEventMap;
    // Key: pd
    // Value: previous event list and its length
    // previous event list is stored here before user call <read> to aggregate event.
    std::unordered_map<unsigned, std::pair<unsigned, char**>> previousEventMap;
    // Key: pd
    // Value: epoll fd
    std::unordered_map<unsigned, int> epollList;
    // Key: epoll fd
    // Value: epoll event list
    std::unordered_map<int, std::vector<epoll_event>> epollEvents;

    // Key: pd
    // Value: spe sampling cpu list.
    std::unordered_map<unsigned, std::set<int>> speCpuList;
    unsigned maxPd = 0;

    std::unordered_map<unsigned, SymbolMode> symModeList;

    std::unordered_map<unsigned, std::vector<pid_t>> ppidList;

    std::unordered_map<unsigned, DummyEvent*> dummyList;

    std::unordered_map<unsigned, groupMapPtr> dataEvtGroupList;
    // Key: pd
    // Value: branchSampleFilter
    std::unordered_map<unsigned, unsigned long> branchSampleFilterList;
};
}   // namespace KUNPENG_PMU
#endif
