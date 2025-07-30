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
 * Description: Common functions for spe sampling.
 ******************************************************************************/
#include "test_common.h"
#include "common.h"

using namespace std;

class TestSPE : public testing::Test {
public:
    void SetUp() {
        if (!HasSpeDevice()) {
            GTEST_SKIP();
        }
    }

    void TearDown() {
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
    PmuAttr GetSystemAttribute()
    {
        PmuAttr attr = {0};
        attr.pidList = nullptr;
        attr.numPid = 0;
        attr.cpuList = nullptr;
        attr.numCpu = 0;
        attr.period = 1000;
        attr.useFreq = 0;
        attr.dataFilter = SPE_DATA_ALL;
        attr.evFilter = SPE_EVENT_RETIRED;
        attr.minLatency = 0x40;
        attr.symbolMode = RESOLVE_ELF_DWARF;
        return attr;
    }

    PmuAttr GetProcAttribute(pid_t pid[], int nums)
    {
        auto attr = GetSystemAttribute();
        attr.pidList = pid;
        attr.numPid = nums;
        return attr;    
    }

    int SpeCollect(PmuAttr &attr, PmuData **data, int sec = 1)
    {
        pd = PmuOpen(SPE_SAMPLING, &attr);
        int ret = PmuCollect(pd, 1000 * sec, collectInterval);
        int len = PmuRead(pd, data);
        return len;
    }

    int pd;
    pid_t appPid = 0;
    PmuData *data = nullptr;
    static const unsigned collectInterval = 100;
};

TEST_F(TestSPE, SpeSystemCollectTwoThreads)
{
    // Start a two-thread process.
    appPid = RunTestApp("test_create_thread");
    auto attr = GetSystemAttribute();
    // Start sampling for SYSTEM.
    pd = PmuOpen(SPE_SAMPLING, &attr);
    ASSERT_NE(pd, -1);
    usleep(1000);
    // Tell process to create thread.
    DelayContinue(appPid, 100);
    int ret = PmuCollect(pd, 3000, collectInterval);
    int len = PmuRead(pd, &data);
    ASSERT_TRUE(data != nullptr);
    ASSERT_TRUE(FoundAllTids(data, len, appPid));
}

TEST_F(TestSPE, SpeProcCollectTwoThreads)
{
    // Start a two-thread process.
    appPid = RunTestApp("test_create_thread");
    pid_t pidList[1] = {appPid};
    auto attr = GetProcAttribute(pidList, 1);
    // Start sampling for PROCESS.
    pd = PmuOpen(SPE_SAMPLING, &attr);
    usleep(1000);
    // Tell process to create thread.
    DelayContinue(appPid, 100);
    int ret = PmuCollect(pd, 3000, collectInterval);
    int len = PmuRead(pd, &data);
    ASSERT_TRUE(data != nullptr);
    ASSERT_TRUE(FoundAllTids(data, len, appPid));
}

TEST_F(TestSPE, SpeSystemCollect12Threads)
{
    // Start a 12-thread process.
    // Threads are created on startup.
    appPid = RunTestApp("test_12threads");
    auto attr = GetSystemAttribute();
    // Start sampling for system.
    int len = SpeCollect(attr, &data);
    ASSERT_TRUE(data != nullptr);
    // Check all threads are sampled.
    ASSERT_TRUE(FoundAllTids(data, len, appPid));
}

TEST_F(TestSPE, SpeProcCollect12Threads)
{
    // Start a 12-thread process.
    // Threads are created on startup.
    appPid = RunTestApp("test_12threads");
    pid_t pidList[1] = {appPid};
    auto attr = GetProcAttribute(pidList, 1);
    // Start sampling for process.
    int len = SpeCollect(attr, &data);
    ASSERT_TRUE(data != nullptr);
    // Check all threads are sampled.
    ASSERT_TRUE(FoundAllTids(data, len, appPid));
}

TEST_F(TestSPE, SpeSystemCollectSubProc)
{
    // Start a process that will for child.
    appPid = RunTestApp("test_fork");
    auto attr = GetSystemAttribute();
    // Start sampling for SYSTEM.
    pd = PmuOpen(SPE_SAMPLING, &attr);
    usleep(1000);
    // Tell process to create thread.
    kill(appPid, SIGCONT);
    int ret = PmuCollect(pd, 3000, collectInterval);
    int len = PmuRead(pd, &data);
    ASSERT_TRUE(data != nullptr);
    ASSERT_TRUE(FoundAllChildren(data, len, appPid));
}

TEST_F(TestSPE, SpeProcCollectSubProc)
{
    // Start a process that will for child.
    appPid = RunTestApp("test_fork");
    pid_t pidList[1] = {appPid};
    auto attr = GetProcAttribute(pidList, 1);
    // Start sampling for SYSTEM.
    pd = PmuOpen(SPE_SAMPLING, &attr);
    // Tell process to create thread.
    DelayContinue(appPid, 100);
    int ret = PmuCollect(pd, 3000, collectInterval);
    int len = PmuRead(pd, &data);
    ASSERT_TRUE(data != nullptr);
    ASSERT_TRUE(FoundAllChildren(data, len, appPid));
}

TEST_F(TestSPE, SpeProcCollect100000threadCase)
{
    appPid = RunTestApp("test_100000thread");
    sleep(1);
    pid_t pidList[1] = {appPid};
    int cpuList[1] = {1};
    auto attr = GetProcAttribute(pidList, 1);
    attr.cpuList = cpuList;
    attr.numCpu = 1;
    pd = PmuOpen(SPE_SAMPLING, &attr);
    PmuEnable(pd);
    sleep(3);
    PmuDisable(pd);
    int len = PmuRead(pd, &data);
    ASSERT_GT(len, 0);
    PmuDataFree(data);
    PmuClose(pd);
}
