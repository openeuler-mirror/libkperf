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
 * Create: 2025-04-01
 * Description: Unit test for metric.
 ******************************************************************************/
#include "test_common.h"
#include <dirent.h>

using namespace std;

class TestMetric : public testing::Test {
public:

protected:
    
};

static CpuTopology GetTopo(const unsigned coreId, const unsigned numaId)
{
    CpuTopology topo;
    topo.coreId = coreId;
    topo.numaId = numaId;
    return topo;
}

static bool HasDevData(PmuDeviceData *outData, int len, PmuDeviceData &expect)
{
    for (int i = 0; i < len; ++i) {
        if (outData[i].metric == expect.metric &&
            outData[i].coreId == expect.coreId
            ) {
            return true;
        }
    }
    return false;
}

TEST_F(TestMetric, GetPcieBdfList)
{
    enum PmuBdfType bdfType = PMU_BDF_TYPE_PCIE;
    unsigned bdfLen = 0;
    const char** bdfList = PmuDeviceBdfList(bdfType, &bdfLen);
    cout << Perror() << endl;
    ASSERT_NE(bdfList, nullptr);
}

TEST_F(TestMetric, GetSmmuBdfList)
{
    enum PmuBdfType bdfType = PMU_BDF_TYPE_SMMU;
    unsigned bdfLen = 0;
    const char** bdfList = PmuDeviceBdfList(bdfType, &bdfLen);
    cout << Perror() << endl;
    ASSERT_NE(bdfList, nullptr);
}

TEST_F(TestMetric, GetCpuFreq)
{
    unsigned core = 6;
    int64_t cpu6Freq = PmuGetCpuFreq(core);
    cout << Perror() << endl;
    ASSERT_NE(cpu6Freq, -1);
}

TEST_F(TestMetric, CollectDDRBandwidth)
{
    PmuDeviceAttr devAttr = {};
    devAttr.metric = PMU_DDR_READ_BW;
    int pd = PmuDeviceOpen(&devAttr, 1);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    PmuData* oriData = nullptr;
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(oriData, oriLen, &devAttr, 1, &devData);
    ASSERT_EQ(len, 4);
    ASSERT_EQ(devData[0].numaId, 0);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_NUMA);
    ASSERT_EQ(devData[1].numaId, 1);
    ASSERT_EQ(devData[1].mode, PMU_METRIC_NUMA);
    ASSERT_EQ(devData[2].numaId, 2);
    ASSERT_EQ(devData[2].mode, PMU_METRIC_NUMA);
    ASSERT_EQ(devData[3].numaId, 3);
    ASSERT_EQ(devData[3].mode, PMU_METRIC_NUMA);
}

TEST_F(TestMetric, CollectL3Latency)
{
    PmuDeviceAttr devAttr = {};
    devAttr.metric = PMU_L3_LAT;
    int pd = PmuDeviceOpen(&devAttr, 1);
    cout << Perror() << endl;
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    PmuData* oriData = nullptr;
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(oriData, oriLen, &devAttr, 1, &devData);
    ASSERT_EQ(len, 4);
    ASSERT_EQ(devData[0].numaId, 0);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_NUMA);
    ASSERT_EQ(devData[1].numaId, 1);
    ASSERT_EQ(devData[1].mode, PMU_METRIC_NUMA);
    ASSERT_EQ(devData[2].numaId, 2);
    ASSERT_EQ(devData[2].mode, PMU_METRIC_NUMA);
    ASSERT_EQ(devData[3].numaId, 3);
    ASSERT_EQ(devData[3].mode, PMU_METRIC_NUMA);
}

TEST_F(TestMetric, CollectL3LatencyAndDDR)
{
    PmuDeviceAttr devAttr[2] = {};
    devAttr[0].metric = PMU_L3_LAT;
    devAttr[1].metric = PMU_DDR_WRITE_BW;

    int pd = PmuDeviceOpen(devAttr, 2);
    cout << Perror() << endl;
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    PmuData* oriData = nullptr;
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, 2, &devData);
    ASSERT_EQ(len, 8);
    PmuDeviceData l3Data = {.metric = PMU_L3_LAT, .count = 200, .mode = PMU_METRIC_NUMA, .numaId = 0};
    ASSERT_TRUE(HasDevData(devData, len, l3Data));
    PmuDeviceData ddrData = {.metric = PMU_DDR_WRITE_BW, .count = 400 * 32, .mode = PMU_METRIC_NUMA, .numaId = 1};
    ASSERT_TRUE(HasDevData(devData, len, ddrData));
}

TEST_F(TestMetric, CollectL3Traffic)
{
    PmuDeviceAttr devAttr = {};
    devAttr.metric = PMU_L3_TRAFFIC;
    int pd = PmuDeviceOpen(&devAttr, 1);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    PmuData* oriData = nullptr;
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(oriData, oriLen, &devAttr, 1, &devData);
    ASSERT_EQ(len, GetCpuNums());
    ASSERT_EQ(devData[0].mode, PMU_METRIC_CORE);
}

TEST_F(TestMetric, CollectL3LatencyAndL3Miss)
{
    PmuDeviceAttr devAttr[2] = {};
    devAttr[0].metric = PMU_L3_LAT;
    devAttr[1].metric = PMU_L3_MISS;

    int pd = PmuDeviceOpen(devAttr, 2);
    cout << Perror() << endl;
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    PmuData* oriData = nullptr;
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, 2, &devData);
    unsigned dataLen = GetCpuNums() + 4;
    ASSERT_EQ(len, dataLen);
    PmuDeviceData l3Data = {.metric = PMU_L3_LAT, .count = 200, .mode = PMU_METRIC_NUMA, .numaId = 0};
    ASSERT_TRUE(HasDevData(devData, len, l3Data));
    PmuDeviceData l3miss4 = {.metric = PMU_L3_MISS, .count = 50, .mode = PMU_METRIC_CORE, .coreId = 4};
    PmuDeviceData l3miss5 = {.metric = PMU_L3_MISS, .count = 60, .mode = PMU_METRIC_CORE, .coreId = 5};
    ASSERT_TRUE(HasDevData(devData, len, l3miss4));
    ASSERT_TRUE(HasDevData(devData, len, l3miss5));
}

TEST_F(TestMetric, GetMetricPcieBandwidth)
{
    PmuDeviceAttr devAttr[2] = {};
    devAttr[0].metric = PMU_PCIE_RX_MRD_BW;
    devAttr[0].bdf="01:03.0";
    devAttr[1].metric = PMU_PCIE_RX_MWR_BW;
    devAttr[1].bdf="01:03.0";

    int pd = PmuDeviceOpen(devAttr, 2);
    cout << Perror() << endl;
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    PmuData* oriData = nullptr;
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, 2, &devData);
    ASSERT_EQ(len, 2);
    PmuDeviceData pcieData1 = {.metric = PMU_PCIE_RX_MRD_BW, .count = 40, .mode = PMU_METRIC_BDF, .bdf = devAttr[0].bdf};
    PmuDeviceData pcieData2 = {.metric = PMU_PCIE_RX_MWR_BW, .count = 60, .mode = PMU_METRIC_BDF, .bdf = devAttr[1].bdf};
    ASSERT_TRUE(HasDevData(devData, len, pcieData1));
    ASSERT_TRUE(HasDevData(devData, len, pcieData2));
}

TEST_F(TestMetric, GetMetricSmmuTransaction)
{
    const char** bdfList = nullptr;
    unsigned bdfLen = 0;
    bdfList = PmuDeviceBdfList(PMU_BDF_TYPE_SMMU, &bdfLen);
    cout << Perror() << endl;
    ASSERT_NE(bdfList, nullptr);
    PmuDeviceAttr devAttr[2] = {};
    devAttr[0].metric = PMU_SMMU_TRAN;
    devAttr[0].bdf= strdup(bdfList[0]);
    devAttr[1].metric = PMU_SMMU_TRAN;
    devAttr[1].bdf= strdup(bdfList[1]);

    int pd = PmuDeviceOpen(devAttr, 2);
    cout << Perror() << endl;
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    PmuData* oriData = nullptr;
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, 2, &devData);
    ASSERT_EQ(len, 2);
    PmuDeviceData smmuData1 = {.metric = PMU_SMMU_TRAN, .count = 110, .mode = PMU_METRIC_BDF, .bdf = devAttr[0].bdf};
    PmuDeviceData smmuData2 = {.metric = PMU_SMMU_TRAN, .count = 50, .mode = PMU_METRIC_BDF, .bdf = devAttr[1].bdf};
    ASSERT_TRUE(HasDevData(devData, len, smmuData1));
    ASSERT_TRUE(HasDevData(devData, len, smmuData2));
    delete[] devAttr[0].bdf;
    delete[] devAttr[1].bdf;
}