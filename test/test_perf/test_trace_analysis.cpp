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
 * Create: 2025-01-18
 * Description: test for analyszing the trace data
 ******************************************************************************/
#include "test_common.h"

using namespace std;

class TestAnaylzeData : public testing::Test {
public:
    void TearDown() {
        if (appPid != 0) {
            KillApp(appPid);
            appPid = 0;
        }
        if (data != nullptr) {
            PmuTraceDataFree(data);
            data = nullptr;
        }
        PmuTraceClose(pd);
    }

protected:
    pid_t RunApp(const string &name)
    {
        char myDir[PATH_MAX] = {0};
        readlink("/proc/self/exe", myDir, sizeof(myDir) - 1);
        auto pid = vfork();
        if (pid == 0) {
            string fullPath = string(dirname(myDir)) + "/case/" + name;
            char *const *dummy = nullptr;
            execvp(fullPath.c_str(), dummy);
            _exit(errno);
        }

        return pid;
    }

    void EnableTracePointer(unsigned pd, unsigned int second) {
        PmuTraceEnable(pd);
        sleep(second);
        PmuTraceDisable(pd);
    }

    int pd;
    pid_t appPid = 0;
    PmuTraceData *data = nullptr;
};

/**
 * @brief test for configing param error
 */
TEST_F(TestAnaylzeData, config_param_error) {
    const char *func1 = "testName";
    const char *funcs[1] = {func1};
    PmuTraceAttr traceAttr = {0};
    traceAttr.funcs = funcs;
    traceAttr.numFuncs = 1;
    pd = PmuTraceOpen(TRACE_SYS_CALL, &traceAttr);
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_SYSCALL_FUN);
}

/**
 * @brief test for collecting single syscall trace data and single cpu
 */
TEST_F(TestAnaylzeData, collect_single_trace_data_success) {
    appPid = RunTestApp("test_12threads");
    int pidList[1] = {appPid};
    int cpuList[1] = {1};
    const char *func1 = "futex";
    const char *funcs[1] = {func1};
    PmuTraceAttr traceAttr = {0};
    traceAttr.funcs = funcs;
    traceAttr.numFuncs = 1;
    traceAttr.pidList = pidList;
    traceAttr.numPid = 1;
    traceAttr.cpuList = cpuList;
    traceAttr.numCpu = 1;

    pd = PmuTraceOpen(TRACE_SYS_CALL, &traceAttr);
    ASSERT_NE(pd, -1);
    EnableTracePointer(pd, 1);
    int len = PmuTraceRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
}

/**
 * @brief test for collecting single syscall trace data and all cpu
 */
TEST_F(TestAnaylzeData, collect_sleep_trace_data_success) {
    appPid = RunApp("test_syscall_sleep");
    int pidList[1] = {appPid};
    const char *func1 = "clock_nanosleep";
    const char *funcs[1] = {func1};
    PmuTraceAttr traceAttr = {0};
    traceAttr.funcs = funcs;
    traceAttr.numFuncs = 1;
    traceAttr.pidList = pidList;
    traceAttr.numPid = 1;

    pd = PmuTraceOpen(TRACE_SYS_CALL, &traceAttr);
    ASSERT_NE(pd, -1);
    EnableTracePointer(pd, 1);
    int len = PmuTraceRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_LT(data[0].elapsedTime, 0.1);
}

/**
 * @brief test for collecting double syscall trace data and all cpu
 */
TEST_F(TestAnaylzeData, collect_double_trace_data_success) {
    appPid = RunApp("test_syscall_read_write");
    int pidList[1] = {appPid};
    const char *func1 = "write";
    const char *func2 = "read";
    const char *funcs[2] = {func1, func2};
    PmuTraceAttr traceAttr = {0};
    traceAttr.funcs = funcs;
    traceAttr.numFuncs = 2;
    traceAttr.pidList = pidList;
    traceAttr.numPid = 1;

    pd = PmuTraceOpen(TRACE_SYS_CALL, &traceAttr);
    ASSERT_NE(pd, -1);
    EnableTracePointer(pd, 1);
    int len = PmuTraceRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
}

/**
 * @brief test for collecting all syscall trace data in all cpu and all process
 */
TEST_F(TestAnaylzeData, collect_all_trace_data_success) {
    PmuTraceAttr traceAttr = {0};

    pd = PmuTraceOpen(TRACE_SYS_CALL, &traceAttr);
    ASSERT_NE(pd, -1);
    EnableTracePointer(pd, 1);
    int len = PmuTraceRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    EXPECT_TRUE(data[len - 1].funcs, != nullptr);
}