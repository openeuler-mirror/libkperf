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
 * Description: Common functions for pmu sampling.
 ******************************************************************************/
#include "test_common.h"

using namespace std;

class TestPMU : public testing::Test {
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
    PmuAttr GetSystemAttribute()
    {
        PmuAttr attr = {0};
        attr.evtList = evtList;
        attr.numEvt = 1;
        attr.pidList = nullptr;
        attr.numPid = 0;
        attr.cpuList = nullptr;
        attr.numCpu = 0;
        attr.freq = 1000;
        attr.useFreq = 1;
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

    int Collect(PmuAttr &attr, PmuData **data, int sec = 1)
    {
        pd = PmuOpen(SAMPLING, &attr);
        int ret = PmuCollect(pd, 1000 * sec, collectInterval);
        int len = PmuRead(pd, data);
        return len;
    }

    int pd;
    pid_t appPid = 0;
    char *evtList[1] = {"r11"};
    PmuData *data = nullptr;
    static const unsigned collectInterval = 100;
};

TEST_F(TestPMU, PmuSystemCollectTwoThreads)
{
    // Start a two-thread process.
    appPid = RunTestApp("test_create_thread");
    auto attr = GetSystemAttribute();
    // Start sampling for SYSTEM.
    pd = PmuOpen(SAMPLING, &attr);
    usleep(1000);
    // Tell process to create thread.
    kill(appPid, SIGCONT);
    int ret = PmuCollect(pd, 100, collectInterval);
    int len = PmuRead(pd, &data);
    ASSERT_TRUE(data != nullptr);
    ASSERT_TRUE(FoundAllTids(data, len, appPid));
}

TEST_F(TestPMU, PmuProcCollectTwoThreads)
{
    // Start a two-thread process.
    appPid = RunTestApp("test_create_thread");
    pid_t pidList[1] = {appPid};
    auto attr = GetProcAttribute(pidList, 1);
    // Start sampling for PROCESS.
    pd = PmuOpen(SAMPLING, &attr);
    usleep(1000);
    // Tell process to create thread.
    DelayContinue(appPid, 100);
    int ret = PmuCollect(pd, 1000, collectInterval);
    int len = PmuRead(pd, &data);
    ASSERT_TRUE(data != nullptr);
    ASSERT_TRUE(FoundAllTids(data, len, appPid));
}

TEST_F(TestPMU, PmuSystemCollect12Threads)
{
    // Start a 12-thread process.
    // Threads are created on startup.
    appPid = RunTestApp("test_12threads");
    auto attr = GetSystemAttribute();
    // Start sampling for system.
    int len = Collect(attr, &data);
    ASSERT_TRUE(data != nullptr);
    // Check all threads are sampled.
    ASSERT_TRUE(FoundAllTids(data, len, appPid));
}

TEST_F(TestPMU, PmuProcCollect12Threads)
{
    // Start a 12-thread process.
    // Threads are created on startup.
    appPid = RunTestApp("test_12threads");
    pid_t pidList[1] = {appPid};
    auto attr = GetProcAttribute(pidList, 1);
    // Start sampling for process.
    int len = Collect(attr, &data);
    ASSERT_TRUE(data != nullptr);
    // Check all threads are sampled.
    ASSERT_TRUE(FoundAllTids(data, len, appPid));
}

TEST_F(TestPMU, PmuSystemCollectSubProc)
{
    // Start a process that will for child.
    appPid = RunTestApp("test_fork");
    auto attr = GetSystemAttribute();
    // Start sampling for SYSTEM.
    pd = PmuOpen(SAMPLING, &attr);
    usleep(1000);
    // Tell process to create thread.
    kill(appPid, SIGCONT);
    int ret = PmuCollect(pd, 100, collectInterval);
    int len = PmuRead(pd, &data);
    ASSERT_TRUE(data != nullptr);
    ASSERT_TRUE(FoundAllChildren(data, len, appPid));
}

TEST_F(TestPMU, PmuProcCollectSubProc)
{
    // Start a process that will for child.
    appPid = RunTestApp("test_fork");
    pid_t pidList[1] = {appPid};
    auto attr = GetProcAttribute(pidList, 1);
    // Start sampling for SYSTEM.
    pd = PmuOpen(SAMPLING, &attr);
    usleep(1000);
    // Tell process to create thread.
    kill(appPid, SIGCONT);
    int ret = PmuCollect(pd, 1000, collectInterval);
    int len = PmuRead(pd, &data);
    ASSERT_TRUE(data != nullptr);
    ASSERT_TRUE(FoundAllChildren(data, len, appPid));
}

TEST_F(TestPMU, NoDataAfterDisable)
{
    // Start a 12-thread process.
    // Threads are created on startup.
    appPid = RunTestApp("test_12threads");
    pid_t pidList[1] = {appPid};
    auto attr = GetProcAttribute(pidList, 1);
    auto pd = PmuOpen(SAMPLING, &attr);
    PmuEnable(pd);
    sleep(1);
    // Disable sampling.
    PmuDisable(pd);
    // Read data for current sampling.
    int len = PmuRead(pd, &data);
    ASSERT_GT(len, 0);
    // Read again and get no data.
    len = PmuRead(pd, &data);
    ASSERT_EQ(len, 0);
}

TEST_F(TestPMU, TestSampling100000ThreadCase)
{
    appPid = RunTestApp("test_100000thread");
    sleep(1);
    pid_t pidList[1] = {appPid};
    int cpuList[1] = {1};
    auto attr = GetProcAttribute(pidList, 1);
    attr.cpuList = cpuList;
    attr.numCpu = 1;
    auto pd = PmuOpen(SAMPLING, &attr);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    int len = PmuRead(pd, &data);
    ASSERT_GT(len, 0);
    PmuDataFree(data);
    PmuClose(pd);
}