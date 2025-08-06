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
 * Description: Unit tests for api functions.
 ******************************************************************************/
#include <thread>
#include <sys/resource.h>
#include <linux/version.h>
#include "util_time.h"
#include "process_map.h"
#include "common.h"
#include "cpu_map.h"
#include "test_common.h"

using namespace std;
class TestAPI : public testing::Test {
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
    bool HasExpectSource(PmuData *data, int len) const
    {
        // Check whether pmu data contains expected filename and line number.
        bool foundData = false;
        for (int i = 0; i < len; ++i) {
            auto stack = data[i].stack;
            while (stack) {
                if (stack->symbol) {
                    if (basename(stack->symbol->fileName) == expectFilename && stack->symbol->lineNum == expectLine) {
                        foundData = true;
                        break;
                    }
                }
                stack = stack->next;
            }
            if (foundData) {
                break;
            }
        }

        return foundData;
    }

    bool HasExpectSymbol(PmuData *data, int len) const
    {
        bool foundData = false;
        for (int i = 0; i < len; ++i) {
            auto stack = data[i].stack;
            while (stack) {
                if (stack->symbol) {
                    if (basename(stack->symbol->module) == exePath) {
                        foundData = true;
                        break;
                    }
                }
                stack = stack->next;
            }
            if (foundData) {
                break;
            }
        }

        return foundData;
    }

    bool CheckDataEvent(PmuData *data, int len, string evtName) const
    {
        for (int i = 0; i < len; ++i) {
            if (string(data[i].evt) != evtName) {
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

    PmuAttr GetSpeAttribute()
    {
        PmuAttr attr = {0};
        attr.pidList = pidList;
        attr.numPid = numPid;
        attr.cpuList = cpuList;
        attr.numCpu = numCpu;
        attr.period = 1000;
        attr.dataFilter = SPE_DATA_ALL;
        attr.evFilter = SPE_EVENT_RETIRED;
        attr.minLatency = 0x40;
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

pid_t TestAPI::demoPid;
const string TestAPI::exePath = "simple";
const string TestAPI::expectFilename = "simple.cpp";

TEST_F(TestAPI, SampleInitSuccess)
{
    auto attr = GetPmuAttribute();
    pd = PmuOpen(SAMPLING, &attr);
    ASSERT_TRUE(pd != -1);
}

TEST_F(TestAPI, SampleCollectSuccess)
{
    auto attr = GetPmuAttribute();
    pd = PmuOpen(SAMPLING, &attr);
    int ret = PmuCollect(pd, 10, collectInterval);
    ASSERT_TRUE(ret == SUCCESS);
}

TEST_F(TestAPI, SampleReadSuccess)
{
    auto attr = GetPmuAttribute();
    pd = PmuOpen(SAMPLING, &attr);
    int ret = PmuCollect(pd, 1000, collectInterval);
    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_TRUE(HasExpectSource(data, len));
}

TEST_F(TestAPI, SampleNoSymbol)
{
    auto attr = GetPmuAttribute();
    attr.symbolMode = NO_SYMBOL_RESOLVE;
    pd = PmuOpen(SAMPLING, &attr);
    int ret = PmuCollect(pd, 1000, collectInterval);
    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);

    for (int i = 0; i< len ;++i) {
        ASSERT_EQ(data[i].stack, nullptr);
    }
}

TEST_F(TestAPI, SampleOnlyElf)
{
    auto attr = GetPmuAttribute();
    attr.symbolMode = RESOLVE_ELF;
    pd = PmuOpen(SAMPLING, &attr);
    int ret = PmuCollect(pd, 1000, collectInterval);
    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);

    for (int i = 0; i< len ;++i) {
        ASSERT_NE(data[i].stack, nullptr);
        auto stack = data[i].stack;
        while (stack) {
            if (stack->symbol) {
                ASSERT_EQ(stack->symbol->lineNum, 0);
            }
            stack = stack->next;
        }
    }
}

TEST_F(TestAPI, SpeNoSymbol)
{
    if (!HasSpeDevice()) {
        GTEST_SKIP();
    }
    auto attr = GetSpeAttribute();
    attr.symbolMode = NO_SYMBOL_RESOLVE;
    pd = PmuOpen(SPE_SAMPLING, &attr);
    int ret = PmuCollect(pd, 3000, collectInterval);
    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);

    for (int i = 0; i< len ;++i) {
        ASSERT_EQ(data[i].stack, nullptr);
    }
}

TEST_F(TestAPI, SpeInitSuccess)
{
    if (!HasSpeDevice()) {
        GTEST_SKIP();
    }
    auto attr = GetSpeAttribute();
    pd = PmuOpen(SPE_SAMPLING, &attr);
    ASSERT_TRUE(pd != -1);
}

TEST_F(TestAPI, SpeCollectSuccess)
{
    if (!HasSpeDevice()) {
        GTEST_SKIP();
    }
    auto attr = GetSpeAttribute();
    pd = PmuOpen(SPE_SAMPLING, &attr);
    ASSERT_TRUE(pd != -1);
    int ret = PmuCollect(pd, 10, collectInterval);
    ASSERT_TRUE(ret == SUCCESS);
}

TEST_F(TestAPI, SpeReadSuccess)
{
    if (!HasSpeDevice()) {
        GTEST_SKIP();
    }
    auto attr = GetSpeAttribute();
    pd = PmuOpen(SPE_SAMPLING, &attr);
    int ret = PmuCollect(pd, 3000, collectInterval);
    ASSERT_TRUE(pd != -1);
    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_TRUE(HasExpectSymbol(data, len));
}

TEST_F(TestAPI, InitSampleNullEvt)
{
    auto attr = GetPmuAttribute();
    attr.evtList = nullptr;
    pd = PmuOpen(SAMPLING, &attr);
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_EVTLIST);
}

TEST_F(TestAPI, InitCountNullEvt)
{
    auto attr = GetPmuAttribute();
    attr.evtList = nullptr;
    pd = PmuOpen(COUNTING, &attr);
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_EVTLIST);
}

TEST_F(TestAPI, InitBadPid)
{
    auto attr = GetPmuAttribute();
    attr.pidList[0] = -1;
    pd = PmuOpen(COUNTING, &attr);
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_PIDLIST);
}

TEST_F(TestAPI, InitBadCpu)
{
    auto attr = GetPmuAttribute();
    attr.cpuList = new int[4];
    attr.numCpu = 4;
    attr.cpuList[0] = 5000;
    pd = PmuOpen(COUNTING, &attr);
    delete[] attr.cpuList;
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_CPULIST);
}

TEST_F(TestAPI, SampleCollectBadEvt)
{
    auto attr = GetPmuAttribute();
    attr.evtList[0] = "abc";
    pd = PmuOpen(SAMPLING, &attr);
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_EVENT);
}

TEST_F(TestAPI, SampleCollectBadPd)
{
    auto ret = PmuCollect(3, 1000, collectInterval);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_PD);
}

TEST_F(TestAPI, SpeInitBusy)
{
    if (!HasSpeDevice()) {
        GTEST_SKIP();
    }
    auto attr = GetSpeAttribute();
    pd = PmuOpen(SPE_SAMPLING, &attr);
    ASSERT_TRUE(pd != -1);
    int badPd = PmuOpen(SPE_SAMPLING, &attr);
    ASSERT_TRUE(badPd == -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_DEVICE_BUSY);
    PmuClose(badPd);
}

TEST_F(TestAPI, SampleSystem)
{
    auto attr = GetPmuAttribute();
    attr.pidList = nullptr;
    attr.numPid = 0;
    pd = PmuOpen(SAMPLING, &attr);
    int ret = PmuCollect(pd, 1000, collectInterval);
    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_TRUE(HasExpectSource(data, len));
}

TEST_F(TestAPI, SpeSystem)
{
    if (!HasSpeDevice()) {
        GTEST_SKIP();
    }
    auto attr = GetSpeAttribute();
    attr.pidList = nullptr;
    attr.numPid = 0;
    pd = PmuOpen(SPE_SAMPLING, &attr);
    ASSERT_TRUE(pd != -1);
    int ret = PmuCollect(pd, 1000, collectInterval);
    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_TRUE(HasExpectSymbol(data, len));
}

static void Stop(int pd)
{
    sleep(2);
    PmuStop(pd);
}

TEST_F(TestAPI, StopSuccess)
{
    auto attr = GetPmuAttribute();
    pd = PmuOpen(SAMPLING, &attr);
    thread th(Stop, pd);
    auto start = GetCurrentTime();
    int ret = PmuCollect(pd, 1000 * 10, collectInterval);
    auto end = GetCurrentTime();
    th.join();
    ASSERT_LE(end - start, 5000);
}

TEST_F(TestAPI, OpenInvalidTaskType)
{
    auto attr = GetPmuAttribute();
    pd = PmuOpen((PmuTaskType)99, &attr);
    ASSERT_TRUE(pd == -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_TASK_TYPE);
}

TEST_F(TestAPI, CollectInvalidTime)
{
    auto attr = GetPmuAttribute();
    pd = PmuOpen(SAMPLING, &attr);
    int ret = PmuCollect(pd, -2, collectInterval);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_TIME);
}

TEST_F(TestAPI, InvalidSampleRate)
{
    auto attr = GetPmuAttribute();
    attr.useFreq = 1;
    attr.freq = 999999;
    pd = PmuOpen(SAMPLING, &attr);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_SAMPLE_RATE);
}

TEST_F(TestAPI, TestRaiseNumFd)
{
    // Given (setup)
    auto attr = GetPmuAttribute();
    auto pid = RunTestApp("test_12threads");
    attr.pidList[0] = pid;
    attr.numPid = 1;
    sleep(1); // Wait for all threads to start
    int numChildTid = 0;
    int* childTidList = GetChildTid(attr.pidList[0], &numChildTid);
    int numCpu = attr.cpuList == nullptr ? sysconf(_SC_NPROCESSORS_ONLN) : attr.numCpu;

    unsigned long setNumFd = numCpu * numChildTid - 100;
    struct rlimit currentlim;
    getrlimit(RLIMIT_NOFILE, &currentlim);
    struct rlimit rlim {
            .rlim_cur = setNumFd, .rlim_max = currentlim.rlim_max,
    };

    int pd = PmuOpen(SAMPLING, &attr);
    std::cout << "pd:" << pd << std::endl;
    PmuEnable(pd);
    // Due to collecting low load applications, collect for long time to ensure pmudata data is present
    sleep(3);

    // When (execution)
    struct PmuData* pmuData = nullptr;
    int len = PmuRead(pd, &pmuData);

    // Then (verification)
    ASSERT_GT(len, 0);

    KillApp(pid);
    PmuDisable(pd);
    PmuDataFree(pmuData);
}

TEST_F(TestAPI, RaiseNumFdFunc)
{
    struct rlimit currentlim;
    ASSERT_NE(getrlimit(RLIMIT_NOFILE, &currentlim), -1);
    auto err = RaiseNumFd(currentlim.rlim_max + 1);
    ASSERT_EQ(err, LIBPERF_ERR_TOO_MANY_FD);
    err = RaiseNumFd(currentlim.rlim_cur - 1);
    ASSERT_EQ(err, SUCCESS);
    err = RaiseNumFd(currentlim.rlim_max - 1);
    ASSERT_EQ(err, SUCCESS);
    err = RaiseNumFd(currentlim.rlim_max - 51);
    ASSERT_EQ(err, SUCCESS);
}

TEST_F(TestAPI, NoDataBeforeEnable)
{
    auto attr = GetPmuAttribute();
    int pd = PmuOpen(SAMPLING, &attr);
    PmuData *data = nullptr;
    // No data before PmuEnable.
    int len = PmuRead(pd, &data);
    ASSERT_EQ(len, 0);
    int err = PmuEnable(pd);
    ASSERT_EQ(err, SUCCESS);
    sleep(1);
    // Has data after PmuEnable.
    len = PmuRead(pd, &data);
    ASSERT_GT(len, 0);
    PmuDisable(pd);
}

TEST_F(TestAPI, NoDataAfterDisable)
{
    auto attr = GetPmuAttribute();
    int pd = PmuOpen(SAMPLING, &attr);
    int err = PmuEnable(pd);
    ASSERT_EQ(err, SUCCESS);
    sleep(1);
    err = PmuDisable(pd);
    ASSERT_EQ(err, SUCCESS);
    PmuData *data = nullptr;
    // Read data from buffer.
    int len = PmuRead(pd, &data);
    ASSERT_GT(len, 0);
    // No data after PmuDisable.
    len = PmuRead(pd, &data);
    ASSERT_EQ(len, 0);
}

TEST_F(TestAPI, AppendPmuDataToNullArray)
{
    auto attr = GetPmuAttribute();
    int pd = PmuOpen(SAMPLING, &attr);
    int err = PmuEnable(pd);
    ASSERT_EQ(err, SUCCESS);

    usleep(3000 * 100);
    // Declare a null array.
    PmuData *total = nullptr;
    PmuData *data = nullptr;
    // Append pmu data to null array, and they will have the same length.
    int len1 = PmuRead(pd, &data);
    int totalLen = PmuAppendData(data, &total);
    ASSERT_EQ(len1, totalLen);
    PmuDataFree(data);

    usleep(3000 * 100);
    // Get another pmu data array.
    int len2 = PmuRead(pd, &data);
    // Append to <total> again.
    totalLen = PmuAppendData(data, &total);
    ASSERT_EQ(len1 + len2, totalLen);
    PmuDataFree(data);
    PmuDataFree(total);
}


TEST_F(TestAPI, AppendPmuDataToExistArray)
{
    auto attr = GetPmuAttribute();
    int pd = PmuOpen(COUNTING, &attr);
    ASSERT_NE(pd, -1);
    int err = PmuEnable(pd);
    ASSERT_EQ(err, SUCCESS);

    usleep(1000 * 100);
    // Get one pmu data array.
    PmuData* data1 = nullptr;
    int len1 = PmuRead(pd, &data1);

    usleep(1000 * 100);
    // Get another pmu data array.
    PmuData* data2 = nullptr;
    int len2 = PmuRead(pd, &data2);
    // Append <data2> to <data1>;
    int totalLen = PmuAppendData(data2, &data1);
    // The total length is sum of two data length.
    ASSERT_EQ(len1 + len2, totalLen);

    // Check data of the second part of <data1>,
    // which equals to <data2>.
    for (int i = 0; i < len2; ++i) {
        ASSERT_EQ(data1[i + len1].count, data2[i].count);
    }

    PmuDataFree(data1);
    PmuDataFree(data2);
}

TEST_F(TestAPI, TestForkNewThread)
{
    auto attr = GetPmuAttribute();
    auto pid = RunTestApp("test_new_fork");
    attr.pidList[0] = pid;
    attr.numPid = 1;
    attr.includeNewFork = 1;
    int pd = PmuOpen(COUNTING, &attr);
    ASSERT_NE(pd, -1);
    int ret = PmuCollect(pd, 10000, collectInterval);
    ASSERT_EQ(ret, SUCCESS);
    int len = PmuRead(pd, &data);
    ASSERT_EQ(len, 5);

    PmuEnable(pd);
    sleep(3);
    PmuDisable(pd);

    len = PmuRead(pd, &data);
    ASSERT_EQ(len, 1);

    PmuClose(pd);
    kill(pid, SIGTERM);
}

TEST_F(TestAPI, TestShortChild)
{
    auto attr = GetPmuAttribute();
    auto pid = RunTestApp("test_short_child");
    attr.pidList[0] = pid;
    attr.numPid = 1;
    attr.includeNewFork = 0;
    int pd = PmuOpen(COUNTING, &attr);
    ASSERT_NE(pd, -1);
    int ret = PmuCollect(pd, 1000, collectInterval);
    ASSERT_EQ(ret, SUCCESS);
    int len = PmuRead(pd, &data);
    ASSERT_LE(len, 3);
    kill(pid, SIGTERM);
}

TEST_F(TestAPI, TestSPEEventGroup)
{
    if (!HasSpeDevice()) {
        GTEST_SKIP();
    }
    auto attr = GetSpeAttribute();
    unsigned numEvt = 16;
    struct EvtAttr groupId[numEvt] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 13, 13};
    attr.evtAttr = groupId;
    pd = PmuOpen(SPE_SAMPLING, &attr);
    ASSERT_TRUE(pd == -1);
}

TEST_F(TestAPI, TestOperationNotSupported)
{
    PmuAttr attr = {0};
    char* evtList[1] = {"hisi_sccl3_ddrc2/flux_rd/"};
    attr.evtList = evtList;
    attr.numEvt = 1;
    attr.freq = 1000;
    attr.useFreq = 1;
    attr.symbolMode = RESOLVE_ELF;
    int pd = PmuOpen(SAMPLING, &attr);
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_EVENT);
}

TEST_F(TestAPI, TestBrBe) 
{
    CHIP_TYPE chipType = GetCpuType();
    if (chipType != HIPF) {
        GTEST_SKIP() << "Unsupported chip";
    }
    auto attr = GetPmuAttribute();
    attr.symbolMode = NO_SYMBOL_RESOLVE;
    auto pid = RunTestApp("bad_branch_pred");
    attr.pidList[0] = pid;
    attr.numPid = 1;
    attr.branchSampleFilter = KPERF_SAMPLE_BRANCH_USER | KPERF_SAMPLE_BRANCH_ANY;
    pd = PmuOpen(SAMPLING, &attr);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(3);
    PmuDisable(pd);
    int len = PmuRead(pd, &data);
    for (int i = 0; i < len; i++)
    {
        PmuData &pmuData = data[i];
        if (pmuData.ext)
        {
            for (int j = 0; j < pmuData.ext->nr; j++)
            {
                auto *rd = pmuData.ext->branchRecords;
                std::cout << std::hex << rd[j].fromAddr << "->" << rd[j].toAddr << " " << rd[j].cycles << std::endl;
            }
        }
    }
    PmuDataFree(data);
    PmuClose(pd);
}

TEST_F(TestAPI, TestBrBeBadFilter) {
    auto attr = GetPmuAttribute();
    attr.symbolMode = NO_SYMBOL_RESOLVE;
    auto pid = RunTestApp("bad_branch_pred");
    attr.pidList[0] = pid;
    attr.numPid = 1;
    attr.branchSampleFilter = KPERF_SAMPLE_BRANCH_USER;
    pd = PmuOpen(SAMPLING, &attr);
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_BRANCH_SAMPLE_FILTER);
}

TEST_F(TestAPI, TestBrBeBadMode) {
    auto attr = GetPmuAttribute();
    attr.symbolMode = NO_SYMBOL_RESOLVE;
    auto pid = RunTestApp("bad_branch_pred");
    attr.pidList[0] = pid;
    attr.numPid = 1;
    attr.branchSampleFilter = KPERF_SAMPLE_BRANCH_USER;
    pd = PmuOpen(COUNTING, &attr);
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_BRANCH_JUST_SUPPORT_SAMPLING);
}

TEST_F(TestAPI, TestCpuFreqSampling) {
    int ret = PmuOpenCpuFreqSampling(100);
    ASSERT_NE(ret, -1);
    PmuCloseCpuFreqSampling();

    unsigned cpuNum = 0;
    PmuCpuFreqDetail* pDetail1 = PmuReadCpuFreqDetail(&cpuNum);
    ASSERT_EQ(cpuNum, MAX_CPU_NUM);
    ret = PmuOpenCpuFreqSampling(100);
    ASSERT_NE(ret, -1);
    sleep(2);
    PmuCpuFreqDetail* pDetail2 = PmuReadCpuFreqDetail(&cpuNum);
    ASSERT_EQ(cpuNum, MAX_CPU_NUM);
    PmuCloseCpuFreqSampling();
}

TEST_F(TestAPI, InvalidCgroupNameList)
{
    auto attr = GetPmuAttribute();
    attr.cgroupNameList = nullptr;
    attr.numCgroup = 1;
    auto pd = PmuOpen(SAMPLING, &attr);
    ASSERT_EQ(pd, -1);
}


TEST_F(TestAPI, NotExistCgroupName)
{
    auto attr = GetPmuAttribute();
    char* cgroupName[1];
    cgroupName[0] = (char*)"mygrouptest";
    attr.cgroupNameList = cgroupName;
    attr.numCgroup = 1;
    auto pd = PmuOpen(SAMPLING, &attr);
    ASSERT_EQ(pd, -1);
}

TEST_F(TestAPI, InvalidCgroupNameListSPE)
{
    auto attr = GetPmuAttribute();
    char* cgroupName[2];
    cgroupName[0] = (char*)"mygrouptest";
    cgroupName[1] = (char*)"mygrouptest1";
    attr.cgroupNameList = cgroupName;
    attr.numCgroup = 2;
    auto pd = PmuOpen(SPE_SAMPLING, &attr);
    ASSERT_EQ(pd, -1);
}

TEST_F(TestAPI, InvalidUserAccessAttr)
{
    if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)) {
        GTEST_SKIP();
    }
    PmuAttr attr = {0};
    attr.enableUserAccess = 1;
    char *evtList[1];
    evtList[0] = (char *)"cycles";
    attr.evtList = evtList;
    attr.numEvt = 1;
    int pidList[1] = {0};
    attr.pidList = pidList;
    attr.numPid = 1;
    int cpuList[1] = {-1};
    attr.cpuList = cpuList;
    attr.numCpu = 1;
    auto pd = PmuOpen(COUNTING, &attr);
    ASSERT_NE(pd, -1);
    PmuClose(pd);
    pd = PmuOpen(SAMPLING, &attr);
    ASSERT_EQ(pd, -1);
    pidList[0] = 999;
    pd = PmuOpen(COUNTING, &attr);
    ASSERT_EQ(pd, -1);
    pidList[0] = 0;
    cpuList[0] = 2;
    pd = PmuOpen(COUNTING, &attr);
    ASSERT_EQ(pd, -1);
}
