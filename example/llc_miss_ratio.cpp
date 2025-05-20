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
 * Author:
 * Create: 2025-05-13
 * Description: Collection capability for ddrc and l3c
 * Current capability: Top-N thread sort of l3c_cache_miss ratio 
 ******************************************************************************/
#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
#include <map>
#include <sstream>
#include <fstream>
#include <cstring>
#include <iomanip>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <signal.h>
#include <chrono>
#include "pcerrc.h"
#include "pmu.h"
#include "symbol.h"

static std::map<unsigned, double> numaTotalDDRC;                  //numa Id --> average ddrc bandwidth
static std::unordered_map<unsigned, unsigned*> numaToCpuCore;     //numa Id --> cpu core ids
static std::unordered_map<unsigned, unsigned> numaToCpuNumber;    //numa Id --> length of cpu cores
static std::vector<int> pidBoundCpus;                             //bounded cpus of designated pid
static unsigned numaNum = 0;                                      //number of NUMAs

const int FLOAT_PRECISION = 2;
const int TIME_UNIT_TRANS = 1000;

uint64_t topNum = 0;
uint64_t duration = 0;
uint64_t period = 0;

void totalDDRCBandwidth()
{
    PmuDeviceAttr devAttr[2];
    devAttr[0].metric = PMU_DDR_READ_BW;
    devAttr[1].metric = PMU_DDR_WRITE_BW;
    int pd = PmuDeviceOpen(devAttr, 2);
    PmuEnable(pd);
    sleep(1);
    PmuData *oriData = nullptr;
    int oriLen = PmuRead(pd, &oriData);
    PmuDeviceData *devData = nullptr;
    auto len = PmuGetDevMetric(oriData, oriLen, devAttr, 2, &devData);
    std::unordered_map<int, double> stats;
    for (int i = 0; i < len; ++i) {
        stats[devData[i].ddrNumaId] += devData[i].count / 1024 / 1024;
    }
    for (const auto &entry : stats) {
        int id = entry.first;
        double sum = entry.second;
        numaTotalDDRC[id] = sum;
    }
    numaNum = numaTotalDDRC.size();
    DevDataFree(devData);
    PmuDataFree(oriData);
    PmuDisable(pd);
}

// get numaId --> cpu core ids
void initNumaToCoreList()
{
    unsigned *coreList;
    for (unsigned i = 0; i < numaNum; ++i) {
        coreList = nullptr;
        int len = PmuGetNumaCore(i, &coreList);
        numaToCpuCore[i] = coreList;
        numaToCpuNumber[i] = len;
    }
}

// parse the CPU core range in the format "0-255" or "0-3,5"
std::vector<int> parseCpuRange(const std::string &rangeStr)
{
    std::vector<int> cpus;
    std::stringstream ss(rangeStr);
    std::string part;

    while(getline(ss, part, ',')) {
        size_t hyphen_pos = part.find("-");
        if (hyphen_pos != std::string::npos) {
            int start = std::stoi(part.substr(0, hyphen_pos));
            int end = std::stoi(part.substr(hyphen_pos + 1));
            if (start > end) {
                std::cerr << "Invalid CPU range: " << part << std::endl;
            }
            for (int i = start; i <= end; ++i) {
                cpus.push_back(i);
            }
        } else {
            cpus.push_back(std::stoi(part));
        }
    }

    std::sort(cpus.begin(), cpus.end());
    cpus.erase(unique(cpus.begin(), cpus.end()), cpus.end());
    return cpus;
}

// get cpu core of pid from /proc/[pid]/stat
std::string getCpuAffinityList(int pid)
{
    std::string path = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "Not found: " << path << std::endl;
        return "";
    }
    std::string line;
    const std::string targetKey = "Cpus_allowed_list:";
    while (getline(in, line)) {
        if (line.find(targetKey) == 0) {
            size_t pos = line.find("\t");
            if (pos == std::string::npos)
                pos = targetKey.length();
            return line.substr(pos + 1);
        }
    }
}

int getCpuCore(int pid)
{
    try {
        std::string rangeStr = getCpuAffinityList(pid);
        if (rangeStr == "") {
            return -1;
        }
        pidBoundCpus = parseCpuRange(rangeStr);
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

bool hasCommonCpu(const unsigned *cpuArray, size_t arraySize, const std::vector<int> &cpuVector)
{
    if (cpuArray == nullptr || arraySize == 0 || cpuVector.empty()) {
        return false;
    }

    if (arraySize < cpuVector.size()) {
        std::unordered_set<unsigned> arraySet(cpuArray, cpuArray + arraySize);
        for (const auto &cpu : cpuVector) {
            if (arraySet.count(cpu) > 0) {
                return true;
            }
        }
    } else {
        std::unordered_set<unsigned> vecSet(cpuVector.begin(), cpuVector.end());
        for (size_t i = 0; i < arraySize; ++i) {
            if (vecSet.count(cpuArray[i]) > 0) {
                return true;
            }
        }
    }

    return false;
}

std::string GetL3CMissPercent(unsigned llc_miss, unsigned llc_cache)
{
    std::ostringstream oss;
    double ratio = llc_cache != 0 ? static_cast<double>(llc_miss) / llc_cache * 100.0 : 0.0;
    oss << std::fixed << std::setprecision(FLOAT_PRECISION) << ratio;
    return oss.str();
}


void PrintHotSpotGraph(const std::unordered_map<unsigned, std::pair<unsigned, unsigned>> tidData)
{
    std::vector<std::pair<unsigned, std::pair<unsigned, unsigned>>> sortedVec(tidData.begin(), tidData.end());
    std::sort(sortedVec.begin(), sortedVec.end(), [](const auto& a, const auto& b) {
        double ratioA = (a.second.second == 0) ? 0.0 : static_cast<double>(a.second.first) / a.second.second;
        double ratioB = (b.second.second == 0) ? 0.0 : static_cast<double>(b.second.first) / b.second.second;
        return ratioA > ratioB;
    });

    std::cout << std::string(100, '=') << std::endl;
    std::cout << std::string(100, '-') << std::endl;
    std::cout << " " << std::setw(10) << " " << std::setw(20) << std::left << "Tid" << std::setw(20) << "llc_cache_miss"
        << std::setw(20) << "llc_cache" << std::setw(20) << "llc_cache_miss_ratio" << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    size_t outputNum = std::min(topNum, tidData.size());
    for (int i = 0; i < outputNum; ++i) {
        std::cout << " " << std::setw(10) << i << std::setw(20) << std::left << sortedVec[i].first << std::setw(20)
         << sortedVec[i].second.first << std::setw(20) << sortedVec[i].second.second << std::setw(20)
         << GetL3CMissPercent(sortedVec[i].second.first, sortedVec[i].second.second) + "%" << std::endl;
    }

    std::cout << std::string(100, '_') << std::endl;
}

int GetPmuDataHotspot(PmuData* pmuData, int pmuDataLen)
{
    if (pmuData == nullptr || pmuDataLen == 0) {
        return SUCCESS;
    }

    std::unordered_map<unsigned, std::pair<unsigned, unsigned>> tidData; //tid --> (0x33, 0x32)
    for (int i = 0; i < pmuDataLen; ++i) {
        PmuData& data = pmuData[i];
        if (strcmp(data.evt, "r33") == 0) {
            tidData[data.tid].first += data.count;
        }
        if (strcmp(data.evt, "r32") == 0) {
            tidData[data.tid].second += data.count;
        }
    }
    PrintHotSpotGraph(tidData);
    return SUCCESS;
}

void collectL3CMissRatio(int pid) {
    char* evtList[2];
    evtList[0] = (char*)"r33";
    evtList[1] = (char*)"r32";
    PmuAttr attr = {0};
    attr.evtList = evtList;
    attr.numEvt = 2;
    attr.pidList = &pid;
    attr.numPid = 1;
    attr.cpuList = pidBoundCpus.data();
    attr.numCpu = pidBoundCpus.size();

    int pd = PmuOpen(COUNTING, &attr);
    if (pd == -1) {
        std::cerr << "PmuOpen failed" << std::endl;
        std::cerr << "error msg:" << Perror() << std::endl;
        return;
    }

    PmuEnable(pd);
    int collectTimes = duration * TIME_UNIT_TRANS / period;
    for (int i = 0; i < collectTimes; ++i) {
        usleep(period * TIME_UNIT_TRANS);
        PmuData* pmuData = nullptr;
        int len = PmuRead(pd, &pmuData);
        if (len == -1) {
            std::cerr << "error msg:" << Perror() << std::endl;
            return;
        }
        GetPmuDataHotspot(pmuData, len);
        PmuDataFree(pmuData);
    }
    PmuDisable(pd);
    PmuClose(pd);
    return;
}

// g++ -o llc_miss_ratio llc_miss_ratio.cpp -I ./output/include/ -L ./output/lib/ -lkperf -lsym
// export LD_LIBRARY_PATH=/XXX/libkperf/output/lib/:$LD_LIBRARY_PATH
void print_usage() {
    std::cerr << "Usage: llc_miss_ratio <threshold> <topNum> <duration> <period> <pid>\n";
    std::cerr << "--threshold : the collect threshold of total ddrc bandwidth, unit M/s\n";
    std::cerr << "--topNum : the top N thread of llc miss ratio collection\n";
    std::cerr << "--duration : the total collect time of llc_miss_ratio, unit s\n";
    std::cerr << "--period : the period of llc_miss_ratio collect, unit ms\n";
    std::cerr << " example: llc_miss_ratio 100 10 10 1000 <pid>\n";
}

int main(int argc, char** argv)
{
    if (argc < 5) {
        print_usage();
        return 0;
    }
    double threshold = 0.0;
    int pid = 0;
    bool collectL3CMissFlag = false;

    try {
        threshold = std::stod(argv[1]);
        if (threshold <= 0) {
            throw std::invalid_argument("threshold must be a positive number.");
        }

        topNum = std::stod(argv[2]);
        if (topNum <= 0) {
            throw std::invalid_argument("TopNum must be a positive number.");
        }

        duration = std::stod(argv[3]);
        if (duration <= 0) {
            throw std::invalid_argument("Duration must be a positive number.");
        }

        period = std::stoi(argv[4]);
        if (period <= 0) {
            throw std::invalid_argument("Period must be a positive integer.");
        }

        try {
            pid = std::stoi(argv[5]);
        } catch (const std::invalid_argument& e) {
            std::cerr << "Not valid process id: " << e.what() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing arguments: " << e.what() << "\n";
        print_usage();
        return EXIT_FAILURE;
    }

    totalDDRCBandwidth();
    initNumaToCoreList();
    if(getCpuCore(pid) == -1) {
        return EXIT_FAILURE;
    }

    for (const auto &data : numaTotalDDRC) {
        std::cout << "Numa ID: " << data.first << ", total bandwidth: " << data.second << "M/s";
        // bandwidth of numa greater than the threshold, check whether bounded cpus of pid correspond to this numa cores
        if (data.second > threshold) {
            auto cpuCoreList = numaToCpuCore[data.first];
            if (hasCommonCpu(cpuCoreList, numaToCpuNumber[data.first], pidBoundCpus)) {
                std::cout << " --> exceed threshold, and the process is running on this numa";
                collectL3CMissFlag = true;
            } else {
                std::cout << " --> exceed threshold, the process is not running on this numa";
            }
        } else {
            std::cout << " --> not exceed threshold";
        }
        std::cout << std::endl;
    }

    if (collectL3CMissFlag) {
        collectL3CMissRatio(pid); //begin to collect llc_miss and llc_cache event
    }

    return 0;
}