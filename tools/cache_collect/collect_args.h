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
#include <unistd.h>

enum class BoltOption {
    NONE,
    CYCLES,
    L2I_CACHE,
    L2I_CACHE_REFILL,
    ALL
};

class CollectArgs {
public:
    unsigned long duration = 10;
    unsigned long summaryTime = 5;
    unsigned frequency = 1000;
    unsigned interval = 1000;
    bool enableData = false;
    bool enableInst = false;
    BoltOption boltOption = BoltOption::NONE;
    std::vector<pid_t> pids;

    bool ParseOption(int argc, char* argv[]);
    void printUsage();

private:
    std::string pidList;
    void ParsePidList();
    BoltOption ParseBoltOption(const std::string& value);
};
#endif