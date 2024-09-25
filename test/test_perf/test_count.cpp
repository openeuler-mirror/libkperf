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
 * Author: Mr.Gan
 * Create: 2024-04-24
 * Description: Unit test for counting.
 ******************************************************************************/
#include "test_common.h"
#include <dirent.h>

using namespace std;

class TestCount : public testing::Test {
public:
    void TearDown()
    {
        if (appPid != 0) {
            KillApp(appPid);
            appPid = 0;
        }
        if (data != nullptr) {
            PmuDataFree(data);
            data = nullptr;
        }
        PmuClose(pd);
    }

protected:
    map<string, uint64_t> CollectProcessEvent(const string &caseName, const vector<string> &evtName)
    {
        appPid = RunTestApp(caseName);
        int pidList[1] = {appPid};
        char **evtList = new char *[evtName.size()];
        for (int i = 0; i < evtName.size(); ++i)
        {
            evtList[i] = const_cast<char *>(evtName[i].c_str());
        }
        PmuAttr attr = {0};
        attr.pidList = pidList;
        attr.numPid = 1;
        attr.evtList = evtList;
        attr.numEvt = evtName.size();

        pd = PmuOpen(COUNTING, &attr);
        PmuCollect(pd, 4000, collectInterval);
        KillApp(appPid);
        int len = PmuRead(pd, &data);
        map<string, uint64_t> ret;
        for (int i = 0; i < len; ++i)
        {
            ret[data[i].evt] += data[i].count;
        }
        delete[] evtList;
        return ret;
    }

    int pd;
    pid_t appPid = 0;
    PmuData *data = nullptr;
    static const unsigned collectInterval = 100;
    static constexpr float relativeErr = 0.01;
};

TEST_F(TestCount, CountProcess)
{
    // Count a process with one event.
    appPid = RunTestApp("simple");
    int pidList[1] = {appPid};
    char *evtList[1] = {"r11"};
    PmuAttr attr = {0};
    attr.pidList = pidList;
    attr.numPid = 1;
    attr.evtList = evtList;
    attr.numEvt = 1;

    pd = PmuOpen(COUNTING, &attr);
    int ret = PmuCollect(pd, 100, collectInterval);
    ASSERT_EQ(ret, SUCCESS);
    int len = PmuRead(pd, &data);
    // Only one sample, only one buffer for one pid.
    ASSERT_EQ(len, 1);
    ASSERT_TRUE(CheckDataEvt(data, len, "r11"));
    ASSERT_TRUE(CheckDataPid(data, len, appPid));
}

TEST_F(TestCount, CountSystem)
{
    // Count system with one event.
    char *evtList[1] = {"r11"};
    PmuAttr attr = {0};
    attr.evtList = evtList;
    attr.numEvt = 1;

    pd = PmuOpen(COUNTING, &attr);
    int ret = PmuCollect(pd, 100, collectInterval);
    ASSERT_EQ(ret, SUCCESS);
    int len = PmuRead(pd, &data);
    // Only one sample, only one buffer for one pid.
    ASSERT_EQ(len, GetCpuNums());
    ASSERT_TRUE(CheckDataEvt(data, len, "r11"));
}

TEST_F(TestCount, OpenDDRC)
{
    // Open flux_rd and flux_wr of all DDRC device.
    vector<char*> eventNames;
    vector<int> scclIdx = {1, 3, 5, 7};
    vector<int> ddrcIdx = {0, 1, 2, 3};
    for (auto sccl : scclIdx) {
        for (auto ddrc : ddrcIdx) {
            const unsigned maxEvtLen =1024;
            char *fluxRdEvt = new char[maxEvtLen];
            snprintf(fluxRdEvt, maxEvtLen, "hisi_sccl%d_ddrc%d/flux_rd/",sccl,ddrc);
            char *fluxWrEvt = new char[maxEvtLen];
            snprintf(fluxWrEvt, maxEvtLen, "hisi_sccl%d_ddrc%d/flux_wr/",sccl,ddrc);
            eventNames.push_back(fluxRdEvt);
            eventNames.push_back(fluxWrEvt);
        }
    }
    PmuAttr attr = {0};
    attr.evtList = eventNames.data();
    attr.numEvt = eventNames.size();

    pd = PmuOpen(COUNTING, &attr);
    ASSERT_NE(pd, -1);
    for (auto evt : eventNames) {
        delete[] evt;
    }
}

TEST_F(TestCount, NumaFluxWr)
{
    // Test data of uncore event ddrc/flux_wr/.

    // Run application which will write memory on numa node 2.
    appPid = RunTestApp("write_on_numa2");

    // Prepare ddr events.
    char *evtList[4] = {"hisi_sccl5_ddrc0/flux_wr/", "hisi_sccl5_ddrc1/flux_wr/", "hisi_sccl5_ddrc2/flux_wr/", "hisi_sccl5_ddrc3/flux_wr/"};
    PmuAttr attr = {0};
    attr.evtList = evtList;
    attr.numEvt = 4;

    // Collect pmu data.
    pd = PmuOpen(COUNTING, &attr);
    ASSERT_NE(pd, -1);
    DelayContinue(appPid, 100);
    int ret = PmuCollect(pd, 8000, collectInterval);
    ASSERT_EQ(ret, SUCCESS);
    int len = PmuRead(pd, &data);
    ASSERT_EQ(len, 4);

    // Check flux event count which should be greater than total memory bytes divided by 256 bits.
    size_t cntSum = 0;
    for (int i=0;i<len;++i) {
        cntSum += data[i].count;
    }
    ASSERT_GE(cntSum, (1024 * 256 * 4 * 64) / 32);
}

TEST_F(TestCount, AggregateUncoreEvents)
{
    // Test aggregate of uncore events.

    char* aggreUncore[1] = {"hisi_sccl1_ddrc/flux_rd/"};
    char* uncoreList[4] = {"hisi_sccl1_ddrc0/flux_rd/", "hisi_sccl1_ddrc1/flux_rd/", "hisi_sccl1_ddrc2/flux_rd/", "hisi_sccl1_ddrc3/flux_rd/"};
    PmuAttr attr = {0};
    attr.evtList = aggreUncore;
    attr.numEvt = 1;
    int pd1 = PmuOpen(COUNTING, &attr);
    attr.evtList = uncoreList;
    attr.numEvt = 4;
    int pd2 = PmuOpen(COUNTING, &attr);
    PmuEnable(pd1);
    PmuEnable(pd2);
    sleep(2);
    PmuDisable(pd1);
    PmuDisable(pd2);

    PmuData *data1 = nullptr;
    int len1 = PmuRead(pd1, &data1);
    ASSERT_EQ(len1, 1);
    PmuData *data2 = nullptr;
    int len2 = PmuRead(pd2, &data2);
    ASSERT_EQ(len2, 4);

    uint64_t aggreCnt = data1[0].count;
    unsigned long uncoreSum = 0;
    for (int i = 0; i < len2; ++i) {
        uncoreSum += data2[i].count;
    }
    ASSERT_NEAR(aggreCnt, uncoreSum, uncoreSum * 0.5);
}

TEST_F(TestCount, PwritevFile)
{
    // Test data of tracepoint syscalls:sys_enter_pwritev.

    // Run an application repeatedly call pwritev.
    appPid = RunTestApp("pwritev_file");

    char *evtList[1] = {"syscalls:sys_enter_pwritev"};
    int pidList[1] = {appPid};
    PmuAttr attr = {0};
    attr.evtList = evtList;
    attr.numEvt = 1;
    attr.pidList = pidList;
    attr.numPid = 1;

    // Collect pmu data.
    pd = PmuOpen(COUNTING, &attr);
    ASSERT_NE(pd, -1);
    int ret = PmuCollect(pd, 1000, collectInterval);
    ASSERT_EQ(ret, SUCCESS);
    int len = PmuRead(pd, &data);
    ASSERT_EQ(len, 1);
    ASSERT_GT(data->count, 0);
}

TEST_F(TestCount, RawEventCycles)
{
    // Test whether raw event is the same as named event.
    string cycles = "cycles";
    string cyclesRaw = "r11";
    vector<string> evts = {cycles, cyclesRaw};
    auto evtMap = CollectProcessEvent("simple", evts);
    ASSERT_EQ(evtMap.size(), evts.size());
    ASSERT_NEAR(evtMap[cycles], evtMap[cyclesRaw], evtMap[cyclesRaw] * relativeErr);
}

TEST_F(TestCount, BranchMissRatio)
{
    // Check branch miss ratio of two cases.
    // One case has bad condition and one case has a predictable condition.
    string brMis = "branch-load-misses";
    string brPred = "branch-loads";
    vector<string> branchEvts = {brMis, brPred};
    auto evtMap = CollectProcessEvent("bad_branch_pred", branchEvts);
    auto missRatio1 = (double)evtMap[brMis]/evtMap[brPred];
    ASSERT_LT(missRatio1, 0.2);
    evtMap = CollectProcessEvent("good_branch_pred", branchEvts);
    auto missRatio2 = (double)evtMap[brMis]/evtMap[brPred];
    ASSERT_LT(missRatio2, 0.2);
    ASSERT_GT(missRatio1, missRatio2);
}

TEST_F(TestCount, BranchMissEvents)
{
    // Check event count of branch-load-misses and branch-misses.
    // They should be very close.
    vector<string> branchEvts = {"branch-load-misses", "branch-misses", "br_mis_pred"};
    auto evtMap = CollectProcessEvent("bad_branch_pred", branchEvts);
    ASSERT_EQ(evtMap.size(), branchEvts.size());
    ASSERT_NEAR(evtMap["branch-load-misses"], evtMap["branch-misses"], evtMap["branch-misses"] * relativeErr);
    ASSERT_NEAR(evtMap["br_mis_pred"], evtMap["branch-misses"], evtMap["branch-misses"] * relativeErr);
}

TEST_F(TestCount, L1CacheMissRatio)
{
    // Check cache miss ratio of two cases.
    // One case has bad locality and one case has good locality.
    string cacheMiss = "l1d_cache_refill";
    string cache = "l1d_cache";
    vector<string> evts = {cacheMiss, cache};
    auto evtMap = CollectProcessEvent("bad_cache_locality", evts);
    ASSERT_EQ(evtMap.size(), evts.size());
    auto missRatio1 = (double)evtMap[cacheMiss]/evtMap[cache];
    ASSERT_GT(missRatio1, 0.01);
    evtMap = CollectProcessEvent("good_cache_locality", evts);
    auto missRatio2 = (double)evtMap[cacheMiss]/evtMap[cache];
    ASSERT_LT(missRatio2, 0.01);
    ASSERT_GT(missRatio1, missRatio2);
}

TEST_F(TestCount, LLCacheMissRatio)
{
    // Check last level cache miss ratio of two cases.
    // One case has two threads in the same node, 
    // and one case has two threads in different sockets.
    string cacheMiss = "ll_cache_miss";
    string cache = "ll_cache";
    vector<string> evts = {cacheMiss, cache};
    auto evtMap = CollectProcessEvent("cross_socket_access", evts);
    ASSERT_EQ(evtMap.size(), evts.size());
    auto missRatio1 = (double)evtMap[cacheMiss]/evtMap[cache];
    ASSERT_GT(missRatio1, 0.1);
    evtMap = CollectProcessEvent("in_node_access", evts);
    auto missRatio2 = (double)evtMap[cacheMiss]/evtMap[cache];
    ASSERT_LT(missRatio2, 0.01);
    ASSERT_GT(missRatio1, missRatio2);
}

TEST_F(TestCount, SimdRatio)
{
    // Test ASE_SPEC and INST_SPEC.
    // Run a case with vectorized loop which has many simd instructions.
    string aseSpec = "r74";
    string instSpec = "r1b";
    vector<string> evts = {aseSpec, instSpec};
    auto evtMap = CollectProcessEvent("vectorized_loop", evts);
    ASSERT_EQ(evtMap.size(), evts.size());
    auto simdRatio = (double)evtMap[aseSpec]/evtMap[instSpec];
    ASSERT_GT(simdRatio, 0.1);
}

static std::vector<string> GetHHADirs() {
    vector<string> hhaEvents;
    unique_ptr<DIR, decltype(&closedir)> dir(opendir("/sys/devices"), &closedir);
    if(!dir) {
        return hhaEvents;
    }

    struct dirent* dt;
    while((dt = readdir(dir.get())) != nullptr) {
        std::string name = dt->d_name;
        if(name == "." || name == "..") {
            continue;
        }

        if(dt->d_type == DT_DIR && strstr(name.c_str(), "hha") != nullptr) {
            hhaEvents.push_back(name + "/");
        }
    }                                                                                                   
    return hhaEvents;
}


TEST_F(TestCount, DeleteEvtAfterOpenPmuu) {
    struct PmuAttr attr = {nullptr};
    vector<string> hhaEvents = GetHHADirs();
    ASSERT_TRUE(hhaEvents.size() > 0);
    set<string> evtNames;
    vector<string> eventsStr = {"rx_outer", "rx_sccl", "rx_ops_num"};
    int evtNum = hhaEvents.size() * eventsStr.size();
    char **evtList = new char *[evtNum];
    for (int i = 0; i < hhaEvents.size(); ++i) {
        for (int j = 0; j < eventsStr.size(); ++j) {
             int evtLen = hhaEvents[i].size() + eventsStr[j].size() + 2;
             evtList[i * eventsStr.size() + j] = new char[evtLen];
             snprintf(evtList[i * eventsStr.size() + j], evtLen, "%s%s/", hhaEvents[i].c_str(), eventsStr[j].c_str());
             evtNames.emplace(hhaEvents[i] + eventsStr[j] + "/");
        }
    }
    attr.evtList = evtList;
    attr.numEvt = evtNum;
    int pd = PmuOpen(COUNTING, &attr);
    for (int i = 0; i < evtNum; i++) {
        delete[] evtList[i];
    }
    delete[] evtList;
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    PmuData* pmuData = nullptr;
    int len = PmuRead(pd, &pmuData);
    for(int i = 0; i < len; i++) {
        ASSERT_TRUE(evtNames.find(pmuData[i].evt) != evtNames.end());
    }
    PmuClose(pd);
}