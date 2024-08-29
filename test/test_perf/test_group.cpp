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
 * Author: Mr.Lei
 * Create: 2024-08-27
 * Description: Unit tests for event group functions.
 ******************************************************************************/

#include "test_common.h"

using namespace std;
class TestGroup : public testing::Test {
public:
    static void SetUpTestCase()
    {
        // Bind test_perf to cpu 0 and bind test app to cpu 1, for stable collection.
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(0, &mask);
        sched_setaffinity(0, sizeof(mask), &mask);

        // Create a simple process.
        demoPid = RunTestApp(exePath);
        cout << "pid: " << demoPid << endl;
    }

    static void TearDownTestCase()
    {
        KillApp(demoPid);
    }

    void TearDown()
    {
        if (data != nullptr) {
            PmuDataFree(data);
            data = nullptr;
        }
        PmuClose(pd);
        for (int i = 0; i< pdNums; ++i) {
            PmuClose(pds[i]);
        }
    }

protected:

    bool CheckDataEventList(PmuData *data, int len, char** evtName) const
    {
        for (int i = 0; i < len; ++i) {
            if (string(data[i].evt) != string(evtName[i])) {
                return false;
            }
        }

        return true;
    }

    PmuAttr GetPmuAttribute()
    {
        PmuAttr attr = {0};
        attr.evtList = evtList;
        attr.numEvt = numEvt;
        attr.pidList = pidList;
        attr.numPid = numPid;
        attr.cpuList = cpuList;
        attr.numCpu = numCpu;
        attr.freq = 1000;
        attr.useFreq = 1;
        attr.symbolMode = RESOLVE_ELF_DWARF;
        return attr;
    }

protected:
    static const string exePath;
    static pid_t demoPid;

    static const unsigned numCpu = 0;
    static const unsigned numPid = 1;
    static const unsigned numEvt = 1;
    static const string expectFilename;
    static const unsigned expectLine = 17;
    static const unsigned collectInterval = 100;

    int *cpuList = nullptr;
    char *evtList[numEvt] = {"cycles"};
    int pidList[numPid] = {demoPid};
    int pd = 0;
    static const int pdNums = 2;
    int pds[pdNums] = {0};
    PmuData *data = nullptr;
};

pid_t TestGroup::demoPid;
const string TestGroup::exePath = "simple";
const string TestGroup::expectFilename = "simple.cpp";

TEST_F(TestGroup, TestNoEventGroup)
{
    auto attr = GetPmuAttribute();
    unsigned numEvt = 3;
    attr.numEvt = numEvt;
    char *evtList[numEvt] = {"r11", "r3", "r4"};
    attr.evtList = evtList;

    int pd = PmuOpen(COUNTING, &attr);
    ASSERT_TRUE(pd != -1);
    int ret = PmuCollect(pd, 100, collectInterval);
    ASSERT_EQ(ret, SUCCESS);

    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);

    ASSERT_EQ(len, numEvt);
    ASSERT_TRUE(CheckDataEventList(data, len, evtList));
}

TEST_F(TestGroup, TestCountingEventGroup)
{
    auto attr = GetPmuAttribute();
    unsigned numEvt = 16;
    attr.numEvt = numEvt;
    char *evtList[numEvt] = {"r3", "r1", "r14", "r4", "r12", "r5", "r25", "r2", 
                            "r26", "r2d", "r17", "r11", "r8", "r22", "r24", "r10"};
    attr.evtList = evtList;

    struct EvtAttr groupId[numEvt] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 13, 13};
    attr.evtAttr = groupId;

    int pd = PmuOpen(COUNTING, &attr);
    ASSERT_TRUE(pd != -1);
    int ret = PmuCollect(pd, 100, collectInterval);
    ASSERT_EQ(ret, SUCCESS);

    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_EQ(len, numEvt);
    ASSERT_TRUE(CheckDataEventList(data, len, evtList));

    ASSERT_NEAR(data[12].countPercent, data[13].countPercent, 0.0001);
    ASSERT_NEAR(data[13].countPercent, data[14].countPercent, 0.0001);
    ASSERT_NEAR(data[14].countPercent, data[15].countPercent, 0.0001);
}

TEST_F(TestGroup, TestCountingEventGroupAllUncore)
{
    auto attr = GetPmuAttribute();
    unsigned numEvt = 16;
    attr.numEvt = numEvt;
    char *evtList[numEvt] = {"r3", "r1", "r14", "r4", "r12", "r5", "r25", "r2",
                            "r26", "r2d", "r17", "r11", 
                            "hisi_sccl1_ddrc2/flux_rd/", "hisi_sccl1_ddrc0/flux_wr/", "hisi_sccl1_hha2/rx_wbi/", "hisi_sccl1_hha3/bi_num/"};
    attr.evtList = evtList;

    struct EvtAttr groupId[numEvt] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 13, 13};
    attr.evtAttr = groupId;
    int pd = PmuOpen(COUNTING, &attr);
    ASSERT_TRUE(pd == -1);
}

TEST_F(TestGroup, TestCountingEventGroupHasAggregateUncore)
{
    auto attr = GetPmuAttribute();
    unsigned numEvt = 14;
    attr.numEvt = numEvt;
    char *evtList[numEvt] = {"r3", "r1", "r14", "r4", "r12", "r5", "r25", "r2",
                            "r26", "r2d", "r17", "r11",
                            "hisi_sccl1_ddrc/flux_rd/", "r22"};
    attr.evtList = evtList;

    struct EvtAttr groupId[numEvt] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13};
    attr.evtAttr = groupId;
    int pd = PmuOpen(COUNTING, &attr);
    ASSERT_TRUE(pd != -1);
    int ret = PmuCollect(pd, 100, collectInterval);
    ASSERT_EQ(ret, SUCCESS);

    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data!= nullptr);
    ASSERT_EQ(len, numEvt);
    ASSERT_TRUE(CheckDataEventList(data, len, evtList)); 
}

TEST_F(TestGroup, TestCountingEventGroupHasAggregateUncoreEnd)
{
    auto attr = GetPmuAttribute();
    unsigned numEvt = 14;
    attr.numEvt = numEvt;
    char *evtList[numEvt] = {"r3", "r1", "r14", "r4", "r12", "r5", "r25", "r2",
                            "r26", "r2d", "r17", "r11", "r22",
                            "hisi_sccl1_ddrc/flux_rd/"};
    attr.evtList = evtList;

    struct EvtAttr groupId[numEvt] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13};
    attr.evtAttr = groupId;
    int pd = PmuOpen(COUNTING, &attr);
    ASSERT_TRUE(pd != -1);
    int ret = PmuCollect(pd, 100, collectInterval);
    ASSERT_EQ(ret, SUCCESS);

    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_EQ(len, numEvt);
    ASSERT_TRUE(CheckDataEventList(data, len, evtList));
}

TEST_F(TestGroup, TestCountingEventGroupHasAggregateUncoreEnd2)
{
    auto attr = GetPmuAttribute();
    unsigned numEvt = 15;
    attr.numEvt = numEvt;
    char *evtList[numEvt] = {"r3", "r1", "r14", "r4", "r12", "r5", "r25", "r2",
                            "r26", "r2d", "r17", "r11", "r22",
                            "hisi_sccl1_ddrc/flux_rd/", "hisi_sccl1_hha/rx_wbi/"};
    attr.evtList = evtList;

    struct EvtAttr groupId[numEvt] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 13};
    attr.evtAttr = groupId;
    int pd = PmuOpen(COUNTING, &attr);
    ASSERT_TRUE(pd != -1);
    int ret = PmuCollect(pd, 100, collectInterval);
    ASSERT_EQ(ret, SUCCESS);

    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_EQ(len, numEvt);
    ASSERT_TRUE(CheckDataEventList(data, len, evtList));
}

TEST_F(TestGroup, TestCountingEventGroupAllAggregateUncore)
{
    auto attr = GetPmuAttribute();
    unsigned numEvt = 13;
    attr.numEvt = numEvt;
    char *evtList[numEvt] = {"r3", "r1", "r14", "r4", "r12", "r5", "r25", "r2",
                            "r26", "r2d", "r17", "r11",
                            "hisi_sccl1_ddrc/flux_rd/"};
    attr.evtList = evtList;

    struct EvtAttr groupId[numEvt] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    attr.evtAttr = groupId;
    int pd = PmuOpen(COUNTING, &attr);
    ASSERT_TRUE(pd == -1);
}

TEST_F(TestGroup, TestCountingEventGroupHasUncore)
{
    auto attr = GetPmuAttribute();
    unsigned numEvt = 16;
    attr.numEvt = numEvt;
    char *evtList[numEvt] = {"r3", "r1", "r14", "r4", "r12", "r5", "r25", "r2",
                            "r26", "r2d", "r17", "r11",
                            "hisi_sccl1_ddrc2/flux_rd/", "hisi_sccl1_ddrc0/flux_wr/", "r22", "r24"};
    attr.evtList = evtList;

    struct EvtAttr groupId[numEvt] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 13, 13};
    attr.evtAttr = groupId;
    int pd = PmuOpen(COUNTING, &attr);
    ASSERT_TRUE(pd != -1);
    int ret = PmuCollect(pd, 100, collectInterval);
    ASSERT_EQ(ret, SUCCESS);

    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data!= nullptr);
    ASSERT_EQ(len, numEvt);
    ASSERT_TRUE(CheckDataEventList(data, len, evtList));
}

TEST_F(TestGroup, TestSamplingNoEventGroup)
{
    auto attr = GetPmuAttribute();
    unsigned numEvt = 2;
    attr.numEvt = numEvt;
    char *evtList[numEvt] = {"r11", "r3"};
    attr.evtList = evtList;

    int cpuList[1] = {1};
    attr.cpuList = cpuList;
    attr.numCpu = 1;

    struct EvtAttr groupId[numEvt] = {1, 2};
    attr.evtAttr = groupId;

    int pd = PmuOpen(SAMPLING, &attr);
    ASSERT_TRUE(pd!= -1);
    int ret = PmuCollect(pd, 100, collectInterval);
    ASSERT_EQ(ret, SUCCESS);

    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
}

TEST_F(TestGroup, TestSamplingEventGroup)
{
    auto attr = GetPmuAttribute();
    unsigned numEvt = 2;
    attr.numEvt = numEvt;
    char *evtList[numEvt] = {"r11", "r3"};
    attr.evtList = evtList;

    int cpuList[1] = {1};
    attr.cpuList = cpuList;
    attr.numCpu = 1;
    struct EvtAttr groupId[numEvt] = {2, 2};
    attr.evtAttr = groupId;

    int pd = PmuOpen(SAMPLING, &attr);
    ASSERT_TRUE(pd != -1);
    int ret = PmuCollect(pd, 100, collectInterval);
    ASSERT_EQ(ret, SUCCESS);

    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
}

TEST_F(TestGroup, TestSamplingEventGroupHasUncore) 
{
    auto attr = GetPmuAttribute();
    unsigned numEvt = 2;
    attr.numEvt = numEvt;
    char *evtList[numEvt] = {"hisi_sccl1_ddrc/flux_rd/", "r3"};
    attr.evtList = evtList;

    int cpuList[1] = {1};
    attr.cpuList = cpuList;
    attr.numCpu = 1;
    struct EvtAttr groupId[numEvt] = {2, 2};
    attr.evtAttr = groupId;

    int pd = PmuOpen(SAMPLING, &attr);
    ASSERT_TRUE(pd == -1);
}

TEST_F(TestGroup, TestEvtGroupForkNewThread)
{
    auto attr = GetPmuAttribute();
    unsigned numEvt = 2;
    attr.numEvt = numEvt;
    char *evtList[numEvt] = {"r11", "r3"};
    attr.evtList = evtList;
    
    auto pid = RunTestApp("test_new_fork");
    attr.pidList[0] = pid;
    attr.numPid = 1;
    attr.includeNewFork = 1;
    struct EvtAttr groupId[numEvt] = {2, 2};
    attr.evtAttr = groupId;

    int pd = PmuOpen(COUNTING, &attr);
    ASSERT_TRUE(pd != -1);
    int ret = PmuCollect(pd, 10000, collectInterval);
    ASSERT_EQ(ret, SUCCESS);
    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_EQ(len, 5 * 2);

    PmuEnable(pd);
    sleep(3);
    PmuDisable(pd);

    len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_EQ(len, 2);
    ASSERT_TRUE(CheckDataEventList(data, len, evtList));

    PmuClose(pd);
    kill(pid, SIGTERM);
}