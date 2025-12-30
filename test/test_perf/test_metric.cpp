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
#include "common.h"
#include "cpu_map.h"
#include <dirent.h>

using namespace std;

class TestMetric : public testing::Test {
public:
     void TearDown()
    {
        if (data != nullptr) {
            PmuDataFree(data);
            data = nullptr;
        }
        
        if (devData) {
            DevDataFree(devData);
            devData = nullptr;
        }

        if (oriData) {
            PmuDataFree(oriData);
            oriData = nullptr;
        }
        
        PmuClose(pd);
    }

protected:
    int pd = 0;
    PmuData *data = nullptr;
    PmuData* oriData = nullptr;
    PmuDeviceData *devData = nullptr;
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
    ASSERT_EQ(Perrorno(), 1064);
    ASSERT_EQ(bdfList, nullptr);
}

TEST_F(TestMetric, GetPcieBdfList)
{
    enum PmuBdfType bdfType = PMU_BDF_TYPE_PCIE;
    unsigned bdfLen = 0;
    const char** bdfList = PmuDeviceBdfList(bdfType, &bdfLen);
    if (bdfList == nullptr) {
        GTEST_SKIP() << "No pcie device";
    }
    ASSERT_EQ(Perrorno(), SUCCESS);
}

TEST_F(TestMetric, GetSmmuBdfList)
{
    enum PmuBdfType bdfType = PMU_BDF_TYPE_SMMU;
    unsigned bdfLen = 0;
    const char** bdfList = PmuDeviceBdfList(bdfType, &bdfLen);
    if (bdfList == nullptr) {
        GTEST_SKIP() << "No smmu device";
    }
    ASSERT_EQ(Perrorno(), SUCCESS);
}

TEST_F(TestMetric, GetCpuFreq)
{
    unsigned core = 6;
    int64_t cpu6Freq = PmuGetCpuFreq(core);
    ASSERT_EQ(Perrorno(), SUCCESS);
    ASSERT_NE(cpu6Freq, -1);
}

TEST_F(TestMetric, GetClusterIdListSuccess)
{
    unsigned clusterId = 3;
    unsigned* coreList = nullptr;
    int len = PmuGetClusterCore(clusterId, &coreList);
    ASSERT_EQ(Perrorno(), SUCCESS);
    ASSERT_NE(len, -1);
}

TEST_F(TestMetric, GetClusterIdListOverSize)
{
    unsigned clusterId = 33;
    unsigned* coreList = nullptr;
    int len = PmuGetClusterCore(clusterId, &coreList);
    ASSERT_EQ(Perrorno(), 1063);
    ASSERT_EQ(len, -1);
}

TEST_F(TestMetric, GetNumaIdList)
{
    unsigned numaId = 1;
    unsigned* coreList = nullptr;
    int len = PmuGetNumaCore(numaId, &coreList);
    ASSERT_EQ(Perrorno(), SUCCESS);
    ASSERT_NE(len, -1);
}

TEST_F(TestMetric, CollectDDRBandwidth)
{
    PmuDeviceAttr devAttr[2] = {};
    devAttr[0].metric = PMU_DDR_READ_BW;
    devAttr[1].metric = PMU_DDR_WRITE_BW;
    pd = PmuDeviceOpen(devAttr, 2);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, 2, &devData);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_CHANNEL);
    ASSERT_EQ(devData[0].metric, PMU_DDR_READ_BW);
    ASSERT_EQ(devData[len - 1].metric, PMU_DDR_WRITE_BW);
}

TEST_F(TestMetric, CollectL3Latency)
{
    CHIP_TYPE chipType = GetCpuType();
    if (chipType != HIPB) {
        GTEST_SKIP() << "Unsupported chip";
    }
    PmuDeviceAttr devAttr = {};
    devAttr.metric = PMU_L3_LAT;
    pd = PmuDeviceOpen(&devAttr, 1);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    auto len = PmuGetDevMetric(oriData, oriLen, &devAttr, 1, &devData);
    unsigned clusterCount = GetClusterCount();
    ASSERT_EQ(len, clusterCount);
    ASSERT_NE(devData[0].count, 0);
    ASSERT_EQ(devData[0].clusterId, 0);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_CLUSTER);
    ASSERT_EQ(devData[clusterCount - 1].clusterId, clusterCount - 1);
    ASSERT_EQ(devData[clusterCount - 1].mode, PMU_METRIC_CLUSTER);
}

TEST_F(TestMetric, CollectL3Traffic)
{
    PmuDeviceAttr devAttr = {};
    devAttr.metric = PMU_L3_TRAFFIC;
    pd = PmuDeviceOpen(&devAttr, 1);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    auto len = PmuGetDevMetric(oriData, oriLen, &devAttr, 1, &devData);
    ASSERT_EQ(len, GetCpuNums());
    ASSERT_EQ(devData[0].mode, PMU_METRIC_CORE);
}

TEST_F(TestMetric, CollectL3TrafficAndL3REF)
{
    PmuDeviceAttr devAttr[2] = {};
    devAttr[0].metric = PMU_L3_TRAFFIC;
    devAttr[1].metric = PMU_L3_REF;
    pd = PmuDeviceOpen(devAttr, 2);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, 2, &devData);
    unsigned cpuNum = GetCpuNums();
    ASSERT_EQ(len, 2 * cpuNum);
    ASSERT_EQ(devData[0].metric, PMU_L3_TRAFFIC);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_CORE);
    ASSERT_EQ(devData[cpuNum].metric, PMU_L3_REF);
    ASSERT_EQ(devData[cpuNum].mode, PMU_METRIC_CORE);
}

TEST_F(TestMetric, CollectL3Miss)
{
    PmuDeviceAttr devAttr[1] = {};
    devAttr[0].metric = PMU_L3_MISS;

    pd = PmuDeviceOpen(devAttr, 1);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, 1, &devData);
    unsigned clusterCount = GetClusterCount();
    unsigned dataLen = GetCpuNums();
    ASSERT_EQ(len, dataLen);
    ASSERT_NE(devData[0].count, 0);
    ASSERT_EQ(devData[0].metric, PMU_L3_MISS);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_CORE);
}

TEST_F(TestMetric, GetMetricPcieBandwidth)
{
    CHIP_TYPE chipType = GetCpuType();
    if (chipType != HIPB) {
        GTEST_SKIP() << "Unsupported chip";
    }
    const char** bdfList = nullptr;
    unsigned bdfLen = 0;
    bdfList = PmuDeviceBdfList(PMU_BDF_TYPE_PCIE, &bdfLen);
    PmuDeviceAttr devAttr[bdfLen] = {};
    for (int i = 0; i < bdfLen; ++i) {
        devAttr[i].metric = PMU_PCIE_RX_MRD_BW;
        devAttr[i].bdf = strdup(bdfList[i]);
    }

    pd = PmuDeviceOpen(devAttr, bdfLen);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, bdfLen, &devData);
    ASSERT_EQ(len, bdfLen);
    ASSERT_EQ(devData[0].metric, PMU_PCIE_RX_MRD_BW);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_BDF);
    ASSERT_TRUE(strcmp(devData[0].bdf, bdfList[0]) == 0);
    for (int i = 0; i < bdfLen; ++i) {
        free(devAttr[i].bdf);
    }
}

TEST_F(TestMetric, GetMetricSmmuTransaction)
{
    const char** bdfList = nullptr;
    unsigned bdfLen = 0;
    bdfList = PmuDeviceBdfList(PMU_BDF_TYPE_SMMU, &bdfLen);
    if (bdfList == nullptr) {
        GTEST_SKIP() << "No smmu device";
    }
    PmuDeviceAttr devAttr[bdfLen] = {};
    for (int i = 0; i < bdfLen; ++i) {
        devAttr[i].metric = PMU_SMMU_TRAN;
        devAttr[i].bdf = strdup(bdfList[i]);
    }

    pd = PmuDeviceOpen(devAttr, bdfLen);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, bdfLen, &devData);
    ASSERT_EQ(len, bdfLen);
    ASSERT_EQ(devData[0].metric, PMU_SMMU_TRAN);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_BDF);
    ASSERT_TRUE(strcmp(devData[0].bdf, devAttr[0].bdf) == 0);
    ASSERT_EQ(devData[1].metric, PMU_SMMU_TRAN);
    ASSERT_EQ(devData[1].mode, PMU_METRIC_BDF);
    ASSERT_TRUE(strcmp(devData[1].bdf, devAttr[1].bdf) == 0);
    for (int i = 0; i < bdfLen; ++i) {
        free(devAttr[i].bdf);
    }
}

TEST_F(TestMetric, GetMetricHHACross)
{
    PmuDeviceAttr devAttr[2] = {};
    devAttr[0].metric = PMU_HHA_CROSS_NUMA;
    devAttr[1].metric = PMU_HHA_CROSS_SOCKET;
    pd = PmuDeviceOpen(devAttr, 2);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, 2, &devData);
    ASSERT_EQ(devData[0].metric, PMU_HHA_CROSS_NUMA);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_NUMA);
    ASSERT_EQ(devData[len - 1].metric, PMU_HHA_CROSS_SOCKET);
    ASSERT_EQ(devData[len - 1].mode, PMU_METRIC_NUMA);
}

TEST_F(TestMetric, GetMetricPcieLatency)
{
    CHIP_TYPE chipType = GetCpuType();
    if (chipType != HIPB) {
        GTEST_SKIP() << "Unsupported chip";
    }
    const char** bdfList = nullptr;
    unsigned bdfLen = 0;
    bdfList = PmuDeviceBdfList(PMU_BDF_TYPE_PCIE, &bdfLen);
    PmuDeviceAttr devAttr[bdfLen] = {};
    for (int i = 0; i < bdfLen; ++i) {
        devAttr[i].metric = PMU_PCIE_RX_MRD_LAT;
        devAttr[i].port = strdup(bdfList[i]);
    }

    pd = PmuDeviceOpen(devAttr, bdfLen);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);

    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, bdfLen, &devData);
    ASSERT_EQ(len, bdfLen);
    ASSERT_EQ(devData[0].metric, PMU_PCIE_RX_MRD_LAT);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_BDF);
    ASSERT_TRUE(strcmp(devData[0].port, bdfList[0]) == 0);
    for (int i = 0; i < bdfLen; ++i) {
        free(devAttr[i].port);
    }
}

TEST_F(TestMetric, TestHwMetric)
{
    CHIP_TYPE chipType = GetCpuType();
    if (chipType != HIPG) {
        GTEST_SKIP() << "Unsupported chip";
    }

    if (!CheckCurKernelConfig("CONFIG_HISILICON_HW_METRIC=y")) {
        GTEST_SKIP() << "Current kernel can't support hw metric";
    }

    double thresholdList[2] = {0.8, 0.2};
    unsigned basePeriodList[2] = {1000000, 100000};
    PmuHwMetricAttr attr = {PmuHwMetric::PMU_HWM_CPI | PMU_HWM_L3_CACHE_MISS, basePeriodList, thresholdList, 0};
    pd = PmuOpenWithHWMetric(&attr);
    ASSERT_NE(pd, -1);
    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);
    int oriLen = PmuRead(pd, &oriData);
    ASSERT_NE(oriLen, -1);
    for (int i = 0; i < oriLen; i++) {
        if (strstr(oriData[i].evt, "cpu_cycles")) {
            ASSERT_EQ(oriData[i].period, 800000);
        }

        if (strstr(oriData[i].evt, "LLC-load-misses")) {
            ASSERT_EQ(oriData[i].period, 20000);
        }
    }
}