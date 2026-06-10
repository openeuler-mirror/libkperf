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
 * Author: Wu
 * Create: 2025-10-21
 * Description: collect args
 ******************************************************************************/
#ifndef COLLECT_ARGS_H
#define COLLECT_ARGS_H

#include <string>
#include <vector>
#include <climits>
#include <unistd.h>

enum class BoltOption {
    NONE,
    CYCLES,
    L2I_CACHE,
    L2I_CACHE_REFILL,
    ALL
};

enum class HotspotSortOption {
    CYCLES,
    L1,
    L2
};

class CollectArgs {
public:
    int duration = 10;
    int summaryTime = 5;
    int frequency = 1000;
    int interval = 1000;
    bool enableData = false;
    bool enableInst = false;
    BoltOption boltOption = BoltOption::NONE;
    HotspotSortOption hotspotSortOption = HotspotSortOption::CYCLES;
    std::vector<pid_t> pids;

    bool ParseOption(int argc, char* argv[]);
    void printUsage();

private:
    std::string pidList;
    bool ParsePidList();
    bool ParsePositiveIntArg(const char* arg, const std::string& paramName, int& outValue, int minValue = 1);
    BoltOption ParseBoltOption(const std::string& value);
    HotspotSortOption ParseHotspotSortOption(const std::string& value);
};
#endif
