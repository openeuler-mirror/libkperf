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
 * Description:  Collecting summary info and hotspot info
 ******************************************************************************/
#ifndef COLLECT_H
#define COLLECT_H

#include <vector>
#include <unordered_map>
#include <sys/types.h>
#include <fstream>
#include <memory>
#include "pmu.h"
#include "symbol.h"
#include "pcerrc.h"
#include "collect_args.h"
#include "utils.h"

static bool dataCollect;
static bool instStat;
static BoltOption boltType;

static std::unordered_map<unsigned, uint64_t> pidPeriod;

// for counting mode statustics of each pid
struct PidSummary {
    int pid;
    double l2Dcache_miss_rate;
    double l2Icache_miss_rate;
    double ipc;
};

struct HotspotFunc {
    PmuData data;
    uint64_t l2RefillPeriod = 0;
    uint64_t l2AccessPeriod = 0;
    uint64_t cyclesPeriod = 0;
    // for output file
    uint64_t l2iAccessCount = 0;
    uint64_t l2iRefillCount = 0;
    uint64_t cyclesCount = 0;
};

struct FileSet {
    std::ofstream cyclesFile;
    std::ofstream l2iCacheRefillFile;
    std::ofstream l2iCacheFile;
    bool enabled = false;
    std::string cyclesPath;
    std::string l2iCacheRefillPath;
    std::string l2iCachePath;
};

struct EventConfig {
    std::vector<std::string> baseEvents;
    std::vector<EvtAttr> groupId;
    std::vector<std::unique_ptr<char[]>> evtStorage;
    std::vector<char*> evtList;
};

// sampling mode: collect l2i cache miss or l2d tlb mis
void collectMiss(CollectArgs& args);
// counting mode: collect ipc, l2i cache miss ratio and l2d cache miss ratio of process
void collectSummaryData(CollectArgs& args);
#endif