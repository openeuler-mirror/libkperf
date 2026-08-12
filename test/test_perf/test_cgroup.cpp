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
 * Author: Ms.Wu
 * Create: 2025-7-9
 * Description: Unit tests for cgroup collection.
 ******************************************************************************/

#include "test_common.h"
#include "common.h"
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>

using namespace std;

class TestCgroup : public testing::Test {
public:
    string testCgroupPath;

    void SetUp()
    {
        // Create a simple process.
        demoPid = RunTestApp(exePath);

        // Set cgroup directory path.
        testCgroupPath = GetCgroupPath(::testing::UnitTest::GetInstance()->current_test_info()->name());
        if (testCgroupPath.empty()) {
            FAIL() << "Cgroup Path does not exist: " << testCgroupPath;
        }
    }
    int tryTimes = 10;
    int tryUsleep = 5000;
    void TearDown()
    {
        if (data != nullptr) {
            PmuDataFree(data);
            data = nullptr;
        }
        if (pd > 0) {
            PmuClose(pd);
            pd = 0;
        }
        for (int i = 0; i< pdNums; ++i) {
            if (pds[i] > 0) {
                PmuClose(pds[i]);
                pds[i] = 0;
            }
        }

        if (demoPid > 0) {
            KillApp(demoPid);
            int status = 0;
            for (int retry = 0; retry < tryTimes; ++retry) {
                pid_t ret = waitpid(demoPid, &status, WNOHANG);
                if (ret == demoPid || (ret == -1 && errno == ECHILD)) {
                    break;
                }
                usleep(tryUsleep);
            }
            demoPid = 0;
        }

        int removeErrno = 0;
        for (int retry = 0; retry < tryTimes; ++retry) {
            if (rmdir(testCgroupPath.c_str()) == 0 || errno == ENOENT) {
                removeErrno = 0;
                break;
            }
            removeErrno = errno;
            if (removeErrno != EBUSY && removeErrno != ENOTEMPTY) {
                break;
            }
            usleep(tryUsleep);
        }
        if (removeErrno != 0) {
            ADD_FAILURE() << "Failed to remove cgroup " << testCgroupPath << ": " << strerror(removeErrno);
        }
    }

protected:
    PmuAttr GetPmuAttribute()
    {
        PmuAttr attr = {0};
        attr.evtList = evtList;
        attr.numEvt = numEvt;
        attr.freq = 1000;
        attr.useFreq = 1;
        attr.symbolMode = RESOLVE_ELF_DWARF;
        return attr;
    }

protected:
    static const string exePath;
    static pid_t demoPid;

    static const unsigned numEvt = 1;
    static const string expectFilename;
    static const unsigned collectInterval = 100;

    char *evtList[numEvt] = {"cycles"};
    int pd = 0;
    static const int pdNums = 2;
    int pds[pdNums] = {0};
    PmuData *data = nullptr;
};

pid_t TestCgroup::demoPid;
const string TestCgroup::exePath = "simple";
const string TestCgroup::expectFilename = "simple.cpp";

TEST_F(TestCgroup, ValidCgroupName)
{
    ASSERT_EQ(mkdir(testCgroupPath.c_str(), 0755), 0) << strerror(errno);
    auto attr = GetPmuAttribute();
    char* cgroupName[1];
    cgroupName[0] = (char*)"ValidCgroupName";
    attr.cgroupNameList = cgroupName;
    attr.numCgroup = 1;
    pd = PmuOpen(SAMPLING, &attr);
    ASSERT_GT(pd, 0);
}

TEST_F(TestCgroup, TestCgroupCounting) {
    ASSERT_EQ(mkdir(testCgroupPath.c_str(), 0755), 0) << strerror(errno);
    ofstream ofs(testCgroupPath + "/cgroup.procs");
    ofs << demoPid;
    ofs.close();

    auto attr = GetPmuAttribute();
    char* cgroupName[1];
    cgroupName[0] = (char*)"TestCgroupCounting";
    attr.cgroupNameList = cgroupName;
    attr.numCgroup = 1;

    pd = PmuOpen(COUNTING, &attr);
    ASSERT_NE(pd, -1);
    int ret = PmuCollect(pd, 1000, collectInterval);
    ASSERT_EQ(ret, SUCCESS);
    int len = PmuRead(pd, &data);
    ASSERT_GT(len, 0);
    ASSERT_STREQ(data[0].cgroupName, "TestCgroupCounting");
}

TEST_F(TestCgroup, TestCgroupSampling)
{
    ASSERT_EQ(mkdir(testCgroupPath.c_str(), 0755), 0) << strerror(errno);
    ofstream ofs(testCgroupPath + "/cgroup.procs");
    ofs << demoPid;
    ofs.close();

    auto attr = GetPmuAttribute();
    attr.period = 1000;
    attr.dataFilter = SPE_DATA_ALL;
    attr.evFilter = SPE_EVENT_RETIRED;
    attr.minLatency = 0x40;
    char* cgroupName[1];
    cgroupName[0] = (char*)"TestCgroupSampling";
    attr.cgroupNameList = cgroupName;
    attr.numCgroup = 1;

    pd = PmuOpen(SAMPLING, &attr);
    ASSERT_NE(pd, -1);
    int ret = PmuCollect(pd, 5000, collectInterval);
    int len = PmuRead(pd, &data);
    ASSERT_GT(len, 0);
    ASSERT_STREQ(data[0].cgroupName, "TestCgroupSampling");
    ASSERT_GT(data[0].period, 0);
}

TEST_F(TestCgroup, TestCgroupSPE)
{
    if (!HasSpeDevice()) {
        GTEST_SKIP();
    }
    ASSERT_EQ(mkdir(testCgroupPath.c_str(), 0755), 0) << strerror(errno);
    ofstream ofs(testCgroupPath + "/cgroup.procs");
    ofs << demoPid;
    ofs.close();

    auto attr = GetPmuAttribute();
    attr.dataFilter = LOAD_FILTER;
    attr.period = 8192;
    char* cgroupName[1];
    cgroupName[0] = (char*)"TestCgroupSPE";
    attr.cgroupNameList = cgroupName;
    attr.numCgroup = 1;

    pd = PmuOpen(SAMPLING, &attr);
    ASSERT_NE(pd, -1);
    int ret = PmuCollect(pd, 5000, collectInterval);
    int len = PmuRead(pd, &data);
    ASSERT_GT(len, 0);
    ASSERT_STREQ(data[0].cgroupName, "TestCgroupSPE");
    ASSERT_GT(data[0].period, 0);
}
