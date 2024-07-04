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
 * Author: Mr.Li
 * Create: 2024-07-4
 * Description: test for trace pointer event.
 ******************************************************************************/
#include "test_common.h"

using namespace std;

class TestTraceRaw : public testing::Test {
public:
    void TearDown() {
        if (appPid != 0) {
            KillApp(appPid);
            appPid = 0;
        }
        if (pmuData != nullptr) {
            PmuDataFree(pmuData);
            pmuData = nullptr;
        }
        PmuClose(pd);
    }

protected:
    void OpenPointerEvent(char* eventName) {
        PmuAttr attr = {0};
        char *evtList[1];
        evtList[0] = eventName;
        attr.evtList = evtList;
        attr.numEvt = 1;
        int pidList[1];
        attr.pidList = pidList;
        attr.numPid = 0;
        attr.cpuList = nullptr;
        attr.numCpu = 0;
        attr.freq = 99;
        attr.useFreq = 1;
        attr.symbolMode = RESOLVE_ELF;
        pd = PmuOpen(SAMPLING, &attr);
        ASSERT_NE(pd, -1);
    }

    void EnablePointer(unsigned int second)
    {
        PmuEnable(pd);
        sleep(second);
        PmuDisable(pd);
    }

    int pd;
    pid_t appPid = 0;
    PmuData *pmuData = nullptr;
};

/**
 * @brief test for sched_switch event. and all of exceptions maybe happened.
 */
TEST_F(TestTraceRaw, trace_pointer_sched_switch) {
    OpenPointerEvent("sched:sched_switch");
    EnablePointer(1);
    int len = PmuRead(pd, &pmuData);
    if (len > 0) {
        auto pPmuData = &pmuData[0];
        auto rawData = pPmuData->rawData;
        ASSERT_NE(rawData, nullptr);
        ASSERT_NE(rawData->data, nullptr);
        char prev_comm[16];
        int rt = PmuObtainPointerField(rawData, "prev_comm", &prev_comm, sizeof(prev_comm));
        ASSERT_EQ(rt, SUCCESS);
        int prev_pid;
        rt = PmuObtainPointerField(rawData, "prev_pid", &prev_pid, sizeof(prev_pid));
        ASSERT_EQ(rt, SUCCESS);
        char next_comm[1];
        rt = PmuObtainPointerField(rawData, "next_comm", &next_comm, sizeof(next_comm));
        ASSERT_NE(rt, SUCCESS);
        char next_comm_error[16];
        rt = PmuObtainPointerField(nullptr, "next_comm", next_comm_error, sizeof(next_comm_error));
        ASSERT_NE(rt, SUCCESS);
        rt = PmuObtainPointerField(rawData, "", next_comm_error, sizeof(next_comm_error));
        ASSERT_NE(rt, SUCCESS);
        rt = PmuObtainPointerField(rawData, "next_comm", nullptr, sizeof(next_comm_error));
        ASSERT_NE(rt, SUCCESS);
        rt = PmuObtainPointerField(rawData, "next_comm", next_comm_error, 0);
        ASSERT_NE(rt, SUCCESS);
        rt = PmuObtainPointerField(rawData, "next_comm_error", next_comm_error, sizeof(next_comm_error));
        ASSERT_NE(rt, SUCCESS);
    }
}

/**
 * @brief test for format contain __data_loc data.
 */
TEST_F(TestTraceRaw, trace_pointer_sched_process_exec) {
    OpenPointerEvent("sched:sched_process_exec");
    EnablePointer(10);
    int len = PmuRead(pd, &pmuData);
    if (len > 0) {
        auto pPmuData = &pmuData[0];
        auto rawData = pPmuData->rawData;
        ASSERT_NE(rawData, nullptr);
        ASSERT_NE(rawData->data, nullptr);
        char fileName[256];
        int rt = PmuObtainPointerField(rawData, "filename", &fileName, sizeof(fileName));
        ASSERT_EQ(rt, SUCCESS);
        // the filename size must be logger than 4.
        char file[4];
        rt = PmuObtainPointerField(rawData, "filename", &file, sizeof(file));
        ASSERT_NE(rt, SUCCESS);
    }
}