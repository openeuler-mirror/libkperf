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
            outData[i].count == expect.count &&
            outData[i].coreId == expect.coreId
            ) {
            return true;
        }
    }
    return false;
}

TEST_F(TestMetric, GetMetricDDRBandwidth)
{
    PmuDeviceAttr devAttr;
    devAttr.metric = PMU_DDR_READ_BW;

    auto topo0 = GetTopo(0, 0);
    auto topo1 = GetTopo(24, 1);
    PmuData data[4];
    data[0] = {.evt = "hisi_sccl1_ddrc0/config=0x84/", .cpu = 0, .cpuTopo = &topo0, .count = 100};
    data[1] = {.evt = "hisi_sccl1_ddrc1/config=0x84/", .cpu = 0, .cpuTopo = &topo0, .count = 100};
    data[2] = {.evt = "hisi_sccl1_ddrc0/config=0x84/", .cpu = topo1.coreId, .cpuTopo = &topo1, .count = 200};
    data[3] = {.evt = "hisi_sccl1_ddrc1/config=0x84/", .cpu = topo1.coreId, .cpuTopo = &topo1, .count = 200};

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(data, 4, &devAttr, 1, &devData);
    ASSERT_EQ(len, 2);
    ASSERT_EQ(devData[0].count, 200 * 32);
    ASSERT_EQ(devData[0].numaId, 0);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_NUMA);
    ASSERT_EQ(devData[1].count, 400 * 32);
    ASSERT_EQ(devData[1].numaId, 1);
    ASSERT_EQ(devData[1].mode, PMU_METRIC_NUMA);
}

TEST_F(TestMetric, GetMetricL3Latency)
{
    PmuDeviceAttr devAttr;
    devAttr.metric = PMU_L3_LAT;

    auto topo0 = GetTopo(0, 0);
    auto topo1 = GetTopo(24, 1);
    PmuData data[4];
    data[0] = {.evt = "hisi_sccl1_l3c0/config=0x80/", .cpu = 0, .cpuTopo = &topo0, .count = 100};
    data[1] = {.evt = "hisi_sccl1_l3c1/config=0x80/", .cpu = 0, .cpuTopo = &topo0, .count = 100};
    data[2] = {.evt = "hisi_sccl3_l3c0/config=0x80/", .cpu = topo1.coreId, .cpuTopo = &topo1, .count = 200};
    data[3] = {.evt = "hisi_sccl3_l3c1/config=0x80/", .cpu = topo1.coreId, .cpuTopo = &topo1, .count = 200};

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(data, 4, &devAttr, 1, &devData);
    ASSERT_EQ(len, 2);
    ASSERT_EQ(devData[0].count, 200);
    ASSERT_EQ(devData[0].numaId, 0);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_NUMA);
    ASSERT_EQ(devData[1].count, 400);
    ASSERT_EQ(devData[1].numaId, 1);
    ASSERT_EQ(devData[1].mode, PMU_METRIC_NUMA);
}

TEST_F(TestMetric, GetMetricL3LatencyAndDDR)
{
    PmuDeviceAttr devAttr[2];
    devAttr[0].metric = PMU_L3_LAT;
    devAttr[1].metric = PMU_DDR_WRITE_BW;

    auto topo0 = GetTopo(0, 0);
    auto topo1 = GetTopo(24, 1);
    PmuData data[4];
    data[0] = {.evt = "hisi_sccl1_l3c0/config=0x80/", .cpu = 0, .cpuTopo = &topo0, .count = 100};
    data[1] = {.evt = "hisi_sccl1_l3c1/config=0x80/", .cpu = 0, .cpuTopo = &topo0, .count = 100};
    data[2] = {.evt = "hisi_sccl1_ddrc0/config=0x83/", .cpu = topo1.coreId, .cpuTopo = &topo1, .count = 200};
    data[3] = {.evt = "hisi_sccl1_ddrc1/config=0x83/", .cpu = topo1.coreId, .cpuTopo = &topo1, .count = 200};

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(data, 4, devAttr, 2, &devData);
    ASSERT_EQ(len, 2);
    PmuDeviceData l3Data = {.metric = PMU_L3_LAT, .count = 200, .mode = PMU_METRIC_NUMA, .numaId = 0};
    ASSERT_TRUE(HasDevData(devData, len, l3Data));
    PmuDeviceData ddrData = {.metric = PMU_DDR_WRITE_BW, .count = 400 * 32, .mode = PMU_METRIC_NUMA, .numaId = 1};
    ASSERT_TRUE(HasDevData(devData, len, ddrData));
}

TEST_F(TestMetric, GetMetricL3Traffic)
{
    PmuDeviceAttr devAttr;
    devAttr.metric = PMU_L3_TRAFFIC;

    PmuData data[4];
    data[0] = {.evt = "armv8_pmuv3_0/config=0x0032/", .cpu = 0, .count = 100};
    data[1] = {.evt = "armv8_pmuv3_0/config=0x0032/", .cpu = 1, .count = 200};
    data[2] = {.evt = "armv8_pmuv3_0/config=0x0032/", .cpu = 2, .count = 300};
    data[3] = {.evt = "armv8_pmuv3_0/config=0x0032/", .cpu = 3, .count = 400};

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(data, 4, &devAttr, 1, &devData);
    ASSERT_EQ(len, 4);
    ASSERT_EQ(devData[0].count, 100 * 64);
    ASSERT_EQ(devData[0].coreId, 0);
    ASSERT_EQ(devData[0].mode, PMU_METRIC_CORE);
    ASSERT_EQ(devData[1].count, 200 * 64);
    ASSERT_EQ(devData[1].coreId, 1);
    ASSERT_EQ(devData[1].mode, PMU_METRIC_CORE);
    ASSERT_EQ(devData[2].count, 300 * 64);
    ASSERT_EQ(devData[2].coreId, 2);
    ASSERT_EQ(devData[2].mode, PMU_METRIC_CORE);
    ASSERT_EQ(devData[3].count, 400 * 64);
    ASSERT_EQ(devData[3].coreId, 3);
    ASSERT_EQ(devData[3].mode, PMU_METRIC_CORE);
}

TEST_F(TestMetric, GetMetricL3LatencyAndL3Miss)
{
    PmuDeviceAttr devAttr[2];
    devAttr[0].metric = PMU_L3_LAT;
    devAttr[1].metric = PMU_L3_MISS;

    auto topo0 = GetTopo(0, 0);
    PmuData data[4];
    data[0] = {.evt = "hisi_sccl1_l3c0/config=0x80/", .cpu = 0, .cpuTopo = &topo0, .count = 100};
    data[1] = {.evt = "hisi_sccl1_l3c1/config=0x80/", .cpu = 0, .cpuTopo = &topo0, .count = 100};
    data[2] = {.evt = "armv8_pmuv3_0/config=0x0033/", .cpu = 4, .count = 50};
    data[3] = {.evt = "armv8_pmuv3_0/config=0x0033/", .cpu = 5, .count = 60};

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(data, 4, devAttr, 2, &devData);
    ASSERT_EQ(len, 3);
    PmuDeviceData l3Data = {.metric = PMU_L3_LAT, .count = 200, .mode = PMU_METRIC_NUMA, .numaId = 0};
    ASSERT_TRUE(HasDevData(devData, len, l3Data));
    PmuDeviceData l3miss4 = {.metric = PMU_L3_MISS, .count = 50, .mode = PMU_METRIC_CORE, .coreId = 4};
    PmuDeviceData l3miss5 = {.metric = PMU_L3_MISS, .count = 60, .mode = PMU_METRIC_CORE, .coreId = 5};
    ASSERT_TRUE(HasDevData(devData, len, l3miss4));
    ASSERT_TRUE(HasDevData(devData, len, l3miss5));
}

TEST_F(TestMetric, GetMetricPcieBandwidth)
{
    PmuDeviceAttr devAttr[2];
    devAttr[0].metric = PMU_PCIE_RX_MRD_BW;
    devAttr[0].bdf="07:00.0";
    devAttr[1].metric = PMU_PCIE_RX_MWR_BW;
    devAttr[1].bdf="07:01.3";

    PmuData data[4];
    data[0] = {.evt = "hisi_pcie0_core0/config=0x0804, bdf=0x700/", .cpu = 0, .count = 100};
    data[1] = {.evt = "hisi_pcie0_core0/config=0x10804, bdf=0x700/", .cpu = 0, .count = 10};
    data[2] = {.evt = "hisi_pcie0_core0/config=0x0104, bdf=0x70b/", .cpu = 0, .count = 300};
    data[3] = {.evt = "hisi_pcie0_core0/config=0x10104, bdf=0x70b/", .cpu = 0, .count = 20};

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(data, 4, devAttr, 2, &devData);
    ASSERT_EQ(len, 2);
    PmuDeviceData pcieData1 = {.metric = PMU_PCIE_RX_MRD_BW, .count = 40, .mode = PMU_METRIC_BDF, .bdf = devAttr[0].bdf};
    PmuDeviceData pcieData2 = {.metric = PMU_PCIE_RX_MWR_BW, .count = 60, .mode = PMU_METRIC_BDF, .bdf = devAttr[1].bdf};
    ASSERT_TRUE(HasDevData(devData, len, pcieData1));
    ASSERT_TRUE(HasDevData(devData, len, pcieData2));
}

TEST_F(TestMetric, GetMetricSmmuTransaction)
{
    PmuDeviceAttr devAttr[2];
    devAttr[0].metric = PMU_SMMU_TRAN;
    devAttr[0].bdf="07:0d.0";
    devAttr[1].metric = PMU_SMMU_TRAN;
    devAttr[1].bdf="74:01.3";

    PmuData data[4];
    data[0] = {.evt = "smmuv3_pmcg_100020/config=0x1,filter_enable=1,filter_stream_id=0x768/", .count = 100};
    data[1] = {.evt = "smmuv3_pmcg_200000020/config=0x1,filter_enable=1,filter_stream_id=0x768/", .count = 10};
    data[2] = {.evt = "smmuv3_pmcg_100020/config=0x1,filter_enable=1,filter_stream_id=0x740b/", .count = 20};
    data[3] = {.evt = "smmuv3_pmcg_200010020/config=0x1,filter_enable=1,filter_stream_id=0x740b/", .count = 30};

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(data, 4, devAttr, 2, &devData);
    ASSERT_EQ(len, 2);
    PmuDeviceData smmuData1 = {.metric = PMU_SMMU_TRAN, .count = 110, .mode = PMU_METRIC_BDF, .bdf = devAttr[0].bdf};
    PmuDeviceData smmuData2 = {.metric = PMU_SMMU_TRAN, .count = 50, .mode = PMU_METRIC_BDF, .bdf = devAttr[1].bdf};
    ASSERT_TRUE(HasDevData(devData, len, smmuData1));
    ASSERT_TRUE(HasDevData(devData, len, smmuData2));
}