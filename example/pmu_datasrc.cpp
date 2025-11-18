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
 * Author: Mr.Li
 * Create: 2025-10-21
 * Description: data source analyze for spe sampling.
 ******************************************************************************/
/**
g++ -g pmu_datasrc.cpp -I ../output/include/ -L ../output/lib/ -lkperf -lsym -O3 -o pmu_datasrc
cd case
g++ -o falsesharing_demo falsesharing_demo.cpp -lpthread
cd ..
./pmu_datasrc -d 2 case/falsesharing_demo
*/
#include <iostream>
#include <stdio.h>
#include <cstring>
#include <sstream>
#include <signal.h>
#include <map>
#include <set>
#include <vector>
#include <getopt.h>
#include <algorithm>
#include <iomanip>

#include "symbol.h"
#include "pmu.h"
#include "pcerrc.h"

using namespace std;
typedef unsigned long ulong;

static double startTs = 0;

static std::map<uint16_t, std::string> HIP_STR_MAP = {
    {HIP_PEER_CPU, "HIP_PEER_CPU"},
    {HIP_PEER_CPU_HITM, "HIP_PEER_CPU_HITM"},
    {HIP_L3, "HIP_L3"},
    {HIP_L3_HITM, "HIP_L3_HITM"},
    {HIP_PEER_CLUSTER, "HIP_PEER_CLUSTER"},
    {HIP_PEER_CLUSTER_HITM, "HIP_PEER_CLUSTER_HITM"},
    {HIP_REMOTE_SOCKET, "HIP_REMOTE_SOCKET"},
    {HIP_REMOTE_SOCKET_HITM, "HIP_REMOTE_SOCKET_HITM"},
    {HIP_LOCAL_MEM, "HIP_LOCAL_MEM"},
    {HIP_REMOTE_MEM, "HIP_REMOTE_MEM"},
    {HIP_NC_DEV, "HIP_NC_DEV"},
    {HIP_L2, "HIP_L2"},
    {HIP_L2_HITM, "HIP_L2_HITM"},
    {HIP_L1, "HIP_L1"},
};

static std::set<uint16_t> HITM_SET = {
    HIP_PEER_CPU_HITM,
    HIP_L3_HITM,
    HIP_L2_HITM,
    HIP_PEER_CLUSTER_HITM,
    HIP_REMOTE_SOCKET_HITM
};

const char* SHORT_OPS = "p:d:c:hf";
const struct option LONG_OPS[] = 
{
    {"pid", required_argument, nullptr, 'p'},
    {"duration", required_argument, nullptr, 'd'},
    {"cgroupName", required_argument, nullptr, 'c'},
    {"help", required_argument, nullptr, 'h'},
    {"fs", no_argument, nullptr, 'f'},
    {nullptr, required_argument, nullptr, 0},
};

// Attributes for each inst address
struct Item {
    ulong pc;
    // sample count
    int cnt;
    // virtual addresses accessed by this inst
    // key: va
    // value: hit count
    map<ulong, int> vas;
};

struct VaItem {
    ulong va;
    ulong pc;
    // hit count of va
    int cnt;
};

struct RacePcCompare {
    bool operator() (const pair<VaItem, VaItem> &a, const pair<VaItem, VaItem> &b) const {
        if (a.first.pc != b.first.pc) {
            return a.first.pc < b.first.pc;
        }
	return a.second.pc < b.second.pc;
    };
};

void ComputeRacePc(const ulong mypc, const map<ulong, int> &vas, 
            const map<ulong, map<ulong, VaItem>> &vaPcMap, map<pair<VaItem, VaItem>, int, RacePcCompare> &racepc)
{
    const int CACHE_LINE = 64;
    for (auto vaPair : vas) {
        // Iterate over virtual addresses, find which other virtual addresses is near current va,
        // i.e., less than 64 bytes between them.
        auto va = vaPair.first;
        auto vaCnt = vaPair.second;
        VaItem racePc;
        racePc.pc = mypc;
        racePc.cnt = vaCnt;
        for (auto &vapc : vaPcMap) {
            auto otherVa = vapc.first;
            for (auto &vaItemPair : vapc.second) {
                auto otherPc = vaItemPair.second;
                if (otherPc.pc == mypc) {
                    continue;
                }
                if (va - otherVa <= CACHE_LINE || otherVa - va <= CACHE_LINE) {
    	            VaItem otherRacePc;
	                otherRacePc.pc = otherPc.pc;
	                otherRacePc.cnt = vaItemPair.second.cnt;
                    // remove duplicated race pc, {a,b} and {b,a} are the same.
                    if (mypc > otherPc.pc) {
                        racepc[make_pair(otherRacePc, racePc)] = racePc.cnt + otherRacePc.cnt;
                    } else {
                        racepc[make_pair(racePc, otherRacePc)] = racePc.cnt + otherRacePc.cnt;
                    }
                }
            }
        }
    }
}

int ExecCommand(std::vector<std::string>& comms)
{
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed!");
        return -1;
    } else if (pid == 0) {
        char **argv = new char*[comms.size() + 1];
        for (size_t i = 0; i < comms.size(); ++i) {
            argv[i] = strdup(comms[i].c_str());
        }
        argv[comms.size()] = NULL;
        execvp(argv[0], argv);
        perror("exec commands failed!");
        for (size_t i = 0; i < comms.size(); ++i) {
            free(argv[i]);
        }
        delete []argv;
        exit(EXIT_FAILURE);
    } else {
        return pid;
    }
    return -1;
}

int ParseArgv(int argc, char** argv, int& pid, int& duration, bool& isLaunch, bool &computeFs, char** cgroupName)
{
    int longIndex;
    int ret;
    int curIndex = 0;
    while((ret = getopt_long(argc, argv, SHORT_OPS, LONG_OPS, &longIndex)) != -1) {
        switch(ret) {
            case 'p':
                curIndex += 2;
                try {
                    pid = std::stoi(optarg);
                } catch(...) {
                    std::cout << "pid is number, can't be: " << optarg << std::endl;
                    return -1;
                }
                break;
            case 'd':
                curIndex += 2;
                try {
                    duration = std::stoi(optarg);
                } catch(...) {
                    std::cout << "duration is number, can't be: " << optarg << std::endl;
                    return -1;
                }
                break;
            case 'c':
                curIndex += 2;
                *cgroupName = optarg;
                break;
            case 'h':
                curIndex += 2;
                std::cout << "usage pmu_datasrc -d 2 -p 10001 or pmu_datasrc -d 2 /home/test/falsesharing_demo" << std::endl;
                return -1;
            case 'f':
                curIndex += 2;
                computeFs = true;
                break;
            default:
                return -1;
        }
    }

    if (pid == -1 && argc > curIndex + 1) {
        std::vector<std::string> comms;
        for (int i = curIndex + 1; i < argc; ++i) {
            comms.push_back(argv[i]);
        }
        pid = ExecCommand(comms);
        isLaunch = true;
    }
    return 0;
}

std::string ParseSymbol(Symbol* sym) 
{
    std::stringstream ss;
    ss << std::hex << sym->codeMapAddr << " " << sym->symbolName << "+0x" << sym->offset << " " << std::dec << sym->fileName << ":" << sym->lineNum;
    return ss.str();
}

typedef std::pair<std::string, Item> SYMBOL_NUM_PAIR;

bool SortBySymValue(const SYMBOL_NUM_PAIR& t1, const SYMBOL_NUM_PAIR& t2)
{
    return t1.second.cnt > t2.second.cnt;
}

double Time()
{
    timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec/1e6;
}

void PrintTime(string msg)
{
    printf("[%f]%s\n", Time(), msg.c_str());
}

int main(int argc, char** argv)
{
    startTs = Time();
    int pid = -1;
    int duration = 10;
    bool isLaunch = false;
    char* cgroupName = nullptr;
    bool computeFs = false;

    int err = ParseArgv(argc, argv, pid, duration, isLaunch, computeFs, &cgroupName);
    if (err == -1) {
        return -1;
    }

    if (pid == -1 && cgroupName == nullptr) {
        std::cout << "usage pmu_datasrc -d 2 -p 10001 or pmu_datasrc -d 2 /home/test/falsesharing_demo" << std::endl;
        return -1;
    }

    if (pid > 0 && cgroupName != nullptr) {
        if (isLaunch) {
            kill(pid, 9);
        }
        std::cout << "Cannot specify both cgroup and pid. Please use only one" << std::endl;
        return -1;
    }

    PmuAttr attr = {0};
    char* cgroupNameList[1] = {cgroupName};
    int pidList[1];
    pidList[0] = pid;
    if (cgroupName != nullptr) {
        attr.cgroupNameList = cgroupNameList;
        attr.numCgroup = 1; 
    } else {
        attr.pidList = pidList;
        attr.numPid = 1;
    }
    attr.period = 256;
    attr.dataFilter = SPE_DATA_ALL;
    attr.evFilter = SPE_EVENT_RETIRED;
    attr.symbolMode = SymbolMode::RESOLVE_ELF_DWARF;

    int pd = PmuOpen(SPE_SAMPLING, &attr);
    if (pd == -1) {
        if (isLaunch) {
            kill(pid, 9);
        }
        std::cout << "kperf pmu open spe failed, err is: " << Perror() << std::endl;
        return -1;
    }
    
    PrintTime("start collect");
    int num = duration * 10;
    PmuData* data = nullptr;
    int len = 0;
    for (int i = 0; i < num; i++) {
        PmuEnable(pd);
        usleep(100 * 1000);
        PmuDisable(pd);
        PmuData* fromData = nullptr;
        PmuRead(pd, &fromData);
        int curLen = PmuAppendData(fromData, &data);
        if (curLen) {
            len = curLen;
        }
    }
    PrintTime("end collect");

    std::map<uint16_t, int> sourceList;
    std::map<uint16_t, std::map<std::string, Item>> sourceSymList;
    // va -> pc -> VaItem
    map<ulong, map<ulong, VaItem>> vaList;
    for (int i = 0; i < len; i++) {
        auto o = data[i];
        if (HIP_STR_MAP.find(o.ext->source) == HIP_STR_MAP.end()) {
            continue;
        }
        auto sym = o.stack->symbol;
        if (sym) {
            std::string symStr = ParseSymbol(sym);
            auto &item = sourceSymList[o.ext->source][symStr];
            item.cnt++;
            item.vas[o.ext->va]++;
            item.pc = sym->codeMapAddr;
        }
        sourceList[o.ext->source] += 1;

        auto &vaItem = vaList[o.ext->va][sym->codeMapAddr];
        if (vaItem.cnt == 0) {
            vaItem.pc = sym->codeMapAddr;
            vaItem.va = o.ext->va;
        }
        vaItem.cnt++;
    }
    PrintTime("prepared");

    // Inst addresses pair which may access same cacheline.
    // key: pair
    // value: count of va hit, for later sorting racepc by va hit count.
    map<pair<VaItem, VaItem>, int, RacePcCompare> racepc;
    int totalSource = 0;
    for (const auto& item : sourceList) {
        auto source = item.first;
        auto sourceNum = item.second;
        std::cout << HIP_STR_MAP[source] << " " << sourceNum << std::endl;
        totalSource += sourceNum;
        if (sourceSymList.find(source) == sourceSymList.end()) {
            continue;
        }
        auto symList = sourceSymList[source];
        std::vector<SYMBOL_NUM_PAIR> sortVec(symList.begin(), symList.end());
        std::sort(sortVec.begin(), sortVec.end(), SortBySymValue);
        for (const auto& symItem : sortVec) {
            auto &it = symItem.second;
            std::cout << "    " << "|--" << symItem.first << " [" << symItem.second.cnt << "]" << std::endl;
            if (computeFs && HITM_SET.find(source) != HITM_SET.end()) {
                // Found out which other insts may access same cacheline with va accessed by current inst.
                // Iterate over va from current inst and search in the whole inst set(vaList).
                ComputeRacePc(it.pc, it.vas, vaList, racepc);
            }
        }
    }
    PrintTime("computed");

    vector<pair<pair<VaItem, VaItem>, int>> sortedList(racepc.begin(), racepc.end());
    sort(sortedList.begin(), sortedList.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });
    PrintTime("sorted");

    if (computeFs) {
        cout << "Possible false sharing: \n";
        for (auto &race : sortedList) {
            auto &race1 = race.first.first;
            auto &race2 = race.first.second;
            cout << std::hex << race1.pc << "<->" << race2.pc
                << " [" << std::dec << race.second << " " 
                << fixed << setprecision(4) << race.second*100/(float)totalSource << "%]" << "\n";
        }
    }
    PmuClose(pd);
    if (isLaunch) {
        kill(pid, 9);
    }

    return 0;
}
