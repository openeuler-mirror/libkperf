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
#include <unistd.h>
#include <memory>
#include <mutex>
#include "common.h"
#include "pcerr.h"
#include "cpu_map.h"

using namespace std;

static const std::string CPU_TOPOLOGY_PACKAGE_ID = "/sys/bus/cpu/devices/cpu%d/topology/physical_package_id";
static const std::string MIDR_EL1 = "/sys/devices/system/cpu/cpu0/regs/identification/midr_el1";
static constexpr int PATH_LEN = 256;

static CHIP_TYPE g_chipType = CHIP_TYPE::UNDEFINED_TYPE;
static map<string, CHIP_TYPE> chipMap = {{"0x00000000481fd010", HIPA},
                                         {"0x00000000480fd020", HIPB},
                                         {"0x00000000480fd030", HIPC},
                                         {"0x00000000480fd220", HIPF}};

static inline bool ReadCpuPackageId(int coreId, CpuTopology* cpuTopo)
{
    char filename[PATH_LEN];
    if (snprintf(filename, PATH_LEN, CPU_TOPOLOGY_PACKAGE_ID.c_str(), coreId) < 0) {
        return false;
    }
    std::string realPath = GetRealPath(filename);
    if (!IsValidPath(realPath)) {
        return false;
    }
    std::ifstream packageFile(realPath);
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

struct CpuTopology* GetCpuTopology(int coreId)
{
    auto cpuTopo = std::unique_ptr<CpuTopology>(new CpuTopology());
    memset(cpuTopo.get(), 0, sizeof(CpuTopology));
    if (coreId == -1) {
        cpuTopo->coreId = coreId;
        cpuTopo->numaId = -1;
        cpuTopo->socketId = -1;
        return cpuTopo.release();
    }

    if (!ReadCpuPackageId(coreId, cpuTopo.get())) {
        return nullptr;
    }

    cpuTopo->coreId = coreId;
    cpuTopo->numaId = numa_node_of_cpu(coreId);
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
    if (g_chipType == UNDEFINED_TYPE && !InitCpuType()) {
        return UNDEFINED_TYPE;
    }
    return g_chipType;
}
