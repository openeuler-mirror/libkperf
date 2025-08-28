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
 * Author: Mr.Wang
 * Create: 2024-04-03
 * Description: Get CPU topology and chip type.
 ******************************************************************************/
#include <iostream>
#include <string>
#include <map>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <memory>
#include <mutex>
#include <dirent.h>
#include "common.h"
#include "pcerr.h"
#include "cpu_map.h"

using namespace std;

static const std::string CPU_TOPOLOGY_PACKAGE_ID = "/sys/bus/cpu/devices/cpu%d/topology/physical_package_id";
static const std::string MIDR_EL1 = "/sys/devices/system/cpu/cpu0/regs/identification/midr_el1";
static const std::string CPU_ONLINE_PATH = "/sys/devices/system/cpu/online";
static const std::string NUMA_PATH = "/sys/devices/system/node";

static constexpr int PATH_LEN = 256;
static constexpr int LINE_LEN = 1024;

static CHIP_TYPE g_chipType = CHIP_TYPE::UNDEFINED_TYPE;
static map<string, CHIP_TYPE> chipMap = {{"0x00000000481fd010", HIPA},
                                         {"0x00000000480fd020", HIPB},
                                         {"0x00000000480fd030", HIPC},
                                         {"0x00000000480fd220", HIPF},
                                         {"0x00000000480fd450", HIPE},
                                         {"0x00000000480fd060", HIPG}};

static std::set<int> onLineCpuIds;
static vector<unsigned> coreArray;
static vector<vector<unsigned>> numaCoreLists;
static unordered_map<unsigned, unsigned> cpuOfNumaNodeMap;
static mutex pmuCoreListMtx;
static bool cpuNumaMapInit = false;

static inline bool ReadCpuPackageId(int coreId, CpuTopology* cpuTopo)
{
    char filename[PATH_LEN];
    if (snprintf(filename, PATH_LEN, CPU_TOPOLOGY_PACKAGE_ID.c_str(), coreId) < 0) {
        return false;
    }
    std::ifstream packageFile(filename);
    if (!packageFile.is_open()) {
        return false;
    }
    std::string packageId;
    packageFile >> packageId;
    try {
        cpuTopo->socketId = std::stoi(packageId);
    } catch (...) {
        return false;
    }
    return true;
}

static int GetNumaNodeCount()
{
    DIR *dir = opendir(NUMA_PATH.c_str());
    if (dir == nullptr) {
        return -1;
    }

    int numaNodeCount = 0;
    struct dirent* entry;
    while((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR && strncmp(entry->d_name, "node", 4) == 0) {
            ++numaNodeCount;
        }
    }
    closedir(dir);
    return numaNodeCount;
}

unsigned* GetCoreList(int start) {
    lock_guard<mutex> lg(pmuCoreListMtx);
    if (coreArray.empty()) {
        for (unsigned i = 0; i < MAX_CPU_NUM; ++i) {
            coreArray.emplace_back(i);
        }
    }
    return &coreArray[start];
}

static void InitialzeCpuOfNumaList()
{
    lock_guard<mutex> lock(pmuCoreListMtx);
    if (cpuNumaMapInit) {
        return;
    }
    unsigned maxNode = GetNumaNodeCount();
    numaCoreLists.resize(maxNode);
    for (unsigned nodeId = 0; nodeId < maxNode; ++nodeId) {
        std::string nodeListFile = "/sys/devices/system/node/node" + to_string(nodeId) + "/cpulist";
        ifstream in(nodeListFile);
        if (!in.is_open()) {
            continue;
        }
        std::string cpulist;
        getline(in, cpulist);
        in.close();
        vector<unsigned> cores;
        auto rangeList = SplitStringByDelimiter(cpulist, ',');
        for (const auto& range : rangeList) {
            auto split = SplitStringByDelimiter(range, '-');
            if (split.size() == 1) {
                auto cpu = stoi(split[0]);
                cores.push_back(cpu);
                cpuOfNumaNodeMap[cpu] = nodeId;
            } else if (split.size() == 2) {
                auto start = stoul(split[0]);
                auto end = stoul(split[1]);
                for (auto i = start; i <= end; ++i) {
                    cores.push_back(i);
                    cpuOfNumaNodeMap[i] = nodeId;
                }
            }
        }
        numaCoreLists[nodeId] = move(cores);
    }
    cpuNumaMapInit = true;
}

int GetNumaCore(unsigned nodeId, unsigned **coreList)
{
    if (!cpuNumaMapInit) {
        InitialzeCpuOfNumaList();
    }

    if (nodeId >= numaCoreLists.size() || numaCoreLists[nodeId].empty()) {
        return -1;
    }

    *coreList = numaCoreLists[nodeId].data();
    return numaCoreLists[nodeId].size();
}

int GetNumaNodeOfCpu(int coreId)
{
    if (!cpuNumaMapInit) {
        InitialzeCpuOfNumaList();
    }

    auto it = cpuOfNumaNodeMap.find(coreId);
    return (it != cpuOfNumaNodeMap.end()) ? it->second : -1;
}

struct CpuTopology* GetCpuTopology(int coreId)
{
    auto cpuTopo = std::unique_ptr<CpuTopology>(new CpuTopology());
    memset(cpuTopo.get(), 0, sizeof(CpuTopology));
    cpuTopo->coreId = coreId;
    if (coreId == -1) {
        cpuTopo->numaId = -1;
        cpuTopo->socketId = -1;
        return cpuTopo.release();
    }

    if (!ReadCpuPackageId(coreId, cpuTopo.get())) {
        cpuTopo->socketId = -1;
        pcerr::SetWarn(LIBPERF_ERR_FAIL_GET_CPU, "failed to obtain the socketdId for " + std::to_string(coreId) + " core.");
    }

    cpuTopo->numaId = GetNumaNodeOfCpu(coreId);
    return cpuTopo.release();
}

bool InitCpuType()
{
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);
    std::ifstream cpuFile(MIDR_EL1);
    std::string cpuId;
    cpuFile >> cpuId;
    auto findCpu = chipMap.find(cpuId);
    if (findCpu == chipMap.end()) {
        pcerr::New(LIBPERF_ERR_CHIP_TYPE_INVALID, "invalid chip type");
        return false;
    }
    g_chipType = findCpu->second;
    return true;
}

CHIP_TYPE GetCpuType()
{
#ifdef IS_X86
    return HIPX86;
#else
    if (g_chipType == UNDEFINED_TYPE && !InitCpuType()) {
        return UNDEFINED_TYPE;
    }
    return g_chipType;
#endif
}

set<int> GetOnLineCpuIds()
{
    if (!onLineCpuIds.empty()) {
        return onLineCpuIds;
    }
    ifstream onLineFile(CPU_ONLINE_PATH);
    if (!onLineFile.is_open()) {
        for (int i = 0; i < MAX_CPU_NUM; i++)
        {
            onLineCpuIds.emplace(i);
        }
        return onLineCpuIds;
    }
    char line[LINE_LEN];
    onLineFile >> line;
    onLineFile.close();
    char *tokStr = strtok(line, ",");
    while (tokStr != nullptr) {
        if (strstr(tokStr, "-") != nullptr) {
            int minCpu, maxCpu;
            if (sscanf(tokStr, "%d-%d", &minCpu, &maxCpu) != 2) {
                continue;
            }
            for (int i = minCpu; i <= maxCpu; i++) {
                onLineCpuIds.emplace(i);
            }
        } else {
            int aloneNumber;
            if (sscanf(tokStr, "%d", &aloneNumber) == 1) {
                onLineCpuIds.emplace(aloneNumber);
            }
        }
        tokStr = strtok(nullptr, ",");
    }
    return onLineCpuIds;
}