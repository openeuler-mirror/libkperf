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
    void TearDown()
    {
    }

protected:
    
};

CpuTopology GetTopo(const unsigned coreId, const unsigned numaId)
{
    CpuTopology topo;
    topo.coreId = coreId;
    topo.numaId = numaId;
    return topo;
}

TEST_F(TestMetric, DDRBandwidth)
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
    ASSERT_EQ(devData[1].count, 400 * 32);
    ASSERT_EQ(devData[1].numaId, 1);
}

TEST_F(TestMetric, L3Traffic)
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
    ASSERT_EQ(devData[1].count, 200 * 64);
    ASSERT_EQ(devData[1].coreId, 1);
    ASSERT_EQ(devData[2].count, 300 * 64);
    ASSERT_EQ(devData[2].coreId, 2);
    ASSERT_EQ(devData[3].count, 400 * 64);
    ASSERT_EQ(devData[3].coreId, 3);
}

TEST_F(TestMetric, PcieBandwidth)
{
    PmuDeviceAttr devAttr[2];
    devAttr[0].metric = PMU_PCIE_RX_MRD_BW;
    devAttr[0].bdf="07:00.0";
    devAttr[1].metric = PMU_PCIE_RX_MWR_BW;
    devAttr[1].bdf="07:01.3";

    PmuData data[4];
    data[0] = {.evt = "hisi_pcie0_core0/config=0x0804, bdf=0x70/", .cpu = 0, .count = 100};
    data[1] = {.evt = "hisi_pcie0_core0/config=0x10804, bdf=0x70/", .cpu = 0, .count = 10};
    data[2] = {.evt = "hisi_pcie0_core0/config=0x0104, bdf=0x7b/", .cpu = 0, .count = 300};
    data[3] = {.evt = "hisi_pcie0_core0/config=0x10104, bdf=0x7b/", .cpu = 0, .count = 20};

    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(data, 4, devAttr, 2, &devData);
    ASSERT_EQ(len, 2);
    ASSERT_EQ(devData[0].count, 40);
    ASSERT_EQ(devData[0].bdf, devAttr[0].bdf);
    ASSERT_EQ(devData[1].count, 60);
    ASSERT_EQ(devData[1].bdf, devAttr[1].bdf);
}