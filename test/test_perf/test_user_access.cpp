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
 * Author: yupan
 * Create: 2025-08-04
 * Description: Unit test for counting.
 ******************************************************************************/
#include "test_common.h"
#include "common.h"
#include <linux/version.h>

using namespace std;

class TestUserAccessCount : public testing::Test {
public:
    TestUserAccessCount()
    {
        attr.enableUserAccess = 1;
        attr.pidList = pids;
        attr.numPid = 1;
        attr.cpuList = cpus;
        attr.numCpu = 1;
    }

protected:
    PmuAttr attr{0};
    int pids[1] = {0};
    int cpus[1] = {-1};
    std::vector<std::string> events = {"branch-misses",
        "bus-cycles",
        "cache-misses",
        "cache-references",
        "cpu-cycles",
        "instructions",
        "stalled-cycles-backend",
        "stalled-cycles-frontend",
        "L1-dcache-loads",
        "L1-dcache-load-misses",
        "L1-icache-loads",
        "L1-icache-load-misses",
        "LLC-loads",
        "LLC-load-misses",
        "dTLB-loads",
        "dTLB-load-misses",
        "iTLB-loads",
        "iTLB-load-misses",
        "branch-loads",
        "branch-load-misses",
        "br_mis_pred",
        "dTLB-load-misses"
        "br_mis_pred_retired",
        "br_pred",
        "br_retired",
        "br_return_retired",
        "bus_access",
        "bus_cycles",
        "cid_write_retired",
        "cpu_cycles",
        "dtlb_walk",
        "exc_return",
        "exc_taken",
        "inst_retired",
        "inst_spec",
        "itlb_walk",
        "l1d_cache",
        "l1d_cache_refill",
        "l1d_cache_wb",
        "l1d_tlb",
        "l1d_tlb_refill",
        "l1i_cache",
        "l1i_cache_refill",
        "l1i_tlb",
        "l1i_tlb_refill",
        "l2d_cache",
        "l2d_cache_refill",
        "l2d_cache_wb",
        "l2d_tlb",
        "l2d_tlb_refill",
        "l2i_cache",
        "l2i_cache_refill",
        "l2i_tlb",
        "l2i_tlb_refill",
        "ll_cache",
        "ll_cache_miss",
        "ll_cache_miss_rd",
        "ll_cache_rd",
        "mem_access",
        "memory_error",
        "remote_access",
        "remote_access_rd",
        "sample_collision",
        "sample_feed",
        "sample_filtrate",
        "sample_pop",
        "stall_backend",
        "stall_frontend",
        "ttbr_write_retired",
        "exe_stall_cycle",
        "fetch_bubble",
        "hit_on_prf",
        "if_is_stall",
        "iq_is_empty",
        "l1d_cache_inval",
        "l1d_cache_rd",
        "l1d_cache_refill_rd",
        "l1d_cache_wb_clean",
        "l1d_cache_wb_victim",
        "l1d_cache_wr",
        "l1d_tlb_rd",
        "l1d_tlb_refill_rd",
        "l1d_tlb_refill_wr",
        "l1d_tlb_wr",
        "l1i_cache_prf",
        "l1i_cache_prf_refill",
        "l2d_cache_inval",
        "l2d_cache_rd",
        "l2d_cache_refill_rd",
        "l2d_cache_wb_clean",
        "l2d_cache_wb_victim",
        "l2d_cache_wr",
        "mem_stall_anyload",
        "mem_stall_l1miss",
        "mem_stall_l2miss",
        "prf_req"};
};

TEST_F(TestUserAccessCount, TestReadEvent)
{
    if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)) {
        GTEST_SKIP();
    }
    attr.numEvt = 1;
    char *evtList[1];
    for (int i = 0; i < events.size(); i++) {
        evtList[0] = const_cast<char *>(events[i].data());
        attr.evtList = evtList;
        int pd = PmuOpen(COUNTING, &attr);
        if (pd == -1) {
            printf("PmuOpen failed : %s\n", Perror());
            PmuClose(pd);
            continue;
        }
        ASSERT_NE(pd, -1);
        PmuEnable(pd);
        PmuData *data = nullptr;
        int len = PmuRead(pd, &data);
        printf("==============\n");
        for (int i = 0; i < 2; i++) {
            int k = 1e8;
            while (k > 0) {
                k--;
            }
            PmuDataFree(data);
            len = PmuRead(pd, &data);
            ASSERT_EQ(len, 1);
            if (len > 0) {
                for (int j = 0; j < len; j++) {
                    printf("event:%s pid=%d tid=%d cpu=%d groupId=%d comm=%s count=%llu countpercent=%lf\n",
                        data[j].evt,
                        data[j].pid,
                        data[j].tid,
                        data[j].cpu,
                        data[j].groupId,
                        data[j].comm,
                        data[j].count,
                        data[j].countPercent);
                }
            } else {
                printf("%s\n", Perror());
            }
        }
        PmuDisable(pd);
        PmuClose(pd);
    }
}