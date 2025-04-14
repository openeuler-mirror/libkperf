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

TEST_F(TestMetric, GetInvalidBdfList)
{
    enum PmuBdfType bdfType = (enum PmuBdfType)5;
    unsigned bdfLen = 0;
    const char** bdfList = PmuDeviceBdfList(bdfType, &bdfLen);
    cout << Perror() << endl;
    ASSERT_EQ(bdfList, nullptr);
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

TEST_F(TestMetric, GetClusterIdListSuccess)
{
    unsigned clusterId = 3;
    unsigned* coreList = nullptr;
    int len = PmuGetClusterCore(clusterId, &coreList);
    cout << Perror() << endl;
    ASSERT_NE(len, -1);
    for (int i = 0; i < len; ++i) {
        cout << coreList[i] << " ";
    }
    cout << endl;
}

TEST_F(TestMetric, GetClusterIdListOverSize)
{
    unsigned clusterId = 33;
    unsigned* coreList = nullptr;
    int len = PmuGetClusterCore(clusterId, &coreList);
    cout << Perror() << endl;
    ASSERT_EQ(len, -1);
}

TEST_F(TestMetric, GetNumaIdList)
{
    unsigned numaId = 2;
    unsigned* coreList = nullptr;
    int len = PmuGetNumaCore(numaId, &coreList);
    cout << Perror() << endl;
    ASSERT_NE(len, -1);
    for (int i = 0; i < len; ++i) {
        cout << coreList[i] << " ";
    }
    cout << endl;
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
    unsigned clusterCount = GetClusterCount();
    ASSERT_EQ(len, clusterCount);
    ASSERT_NE(devData[0].count, 0);
    ASSERT_EQ(devData[0].clusterId, 0);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_CLUSTER);
    ASSERT_EQ(devData[clusterCount - 1].clusterId, clusterCount - 1);
    ASSERT_EQ(devData[clusterCount - 1].mode, PMU_METRIC_CLUSTER);
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
    unsigned clusterCount = GetClusterCount();
    unsigned numaCount = GetNumaNodeCount();
    ASSERT_EQ(len, clusterCount + numaCount);
    ASSERT_NE(devData[0].count, 0);
    ASSERT_EQ(devData[0].metric, PMU_L3_LAT);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_CLUSTER);
    ASSERT_EQ(devData[clusterCount].metric, PMU_DDR_WRITE_BW);
    ASSERT_EQ(devData[clusterCount].mode, PMU_METRIC_NUMA);
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

TEST_F(TestMetric, CollectL3TrafficAndL3REF)
{
    PmuDeviceAttr devAttr[2] = {};
    devAttr[0].metric = PMU_L3_TRAFFIC;
    devAttr[1].metric = PMU_L3_REF;
    int pd = PmuDeviceOpen(devAttr, 2);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    PmuData* oriData = nullptr;
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, 2, &devData);
    unsigned cpuNum = GetCpuNums();
    ASSERT_EQ(len, 2 * cpuNum);
    ASSERT_EQ(devData[0].metric, PMU_L3_TRAFFIC);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_CORE);
    ASSERT_EQ(devData[cpuNum].metric, PMU_L3_REF);
    ASSERT_EQ(devData[cpuNum].mode, PMU_METRIC_CORE);
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
    unsigned clusterCount = GetClusterCount();
    unsigned dataLen = GetCpuNums() + clusterCount;
    ASSERT_EQ(len, dataLen);
    ASSERT_NE(devData[0].count, 0);
    ASSERT_EQ(devData[0].metric, PMU_L3_LAT);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_CLUSTER);
    ASSERT_EQ(devData[clusterCount].metric, PMU_L3_MISS);
    ASSERT_EQ(devData[clusterCount].mode, PMU_METRIC_CORE);
}

TEST_F(TestMetric, GetMetricPcieBandwidth)
{
    const char** bdfList = nullptr;
    unsigned bdfLen = 0;
    bdfList = PmuDeviceBdfList(PMU_BDF_TYPE_PCIE, &bdfLen);
    PmuDeviceAttr devAttr[bdfLen] = {};
    for (int i = 0; i < bdfLen; ++i) {
        devAttr[i].metric = PMU_PCIE_RX_MRD_BW;
        devAttr[i].bdf = strdup(bdfList[i]);
    }

    int pd = PmuDeviceOpen(devAttr, bdfLen);
    cout << Perror() << endl;
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    PmuData* oriData = nullptr;
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, bdfLen, &devData);
    ASSERT_EQ(len, bdfLen);
    ASSERT_EQ(devData[0].metric, PMU_PCIE_RX_MRD_BW);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_BDF);
    ASSERT_TRUE(strcmp(devData[0].bdf, bdfList[0]) == 0);
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
    devAttr[0].bdf = strdup(bdfList[0]);
    devAttr[1].metric = PMU_SMMU_TRAN;
    devAttr[1].bdf = strdup(bdfList[1]);

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
    ASSERT_EQ(devData[0].metric, PMU_SMMU_TRAN);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_BDF);
    ASSERT_TRUE(strcmp(devData[0].bdf, devAttr[0].bdf) == 0);
    ASSERT_EQ(devData[1].metric, PMU_SMMU_TRAN);
    ASSERT_EQ(devData[1].mode, PMU_METRIC_BDF);
    ASSERT_TRUE(strcmp(devData[1].bdf, devAttr[1].bdf) == 0);
    delete[] devAttr[0].bdf;
    delete[] devAttr[1].bdf;
}