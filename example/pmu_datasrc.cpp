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

#include "symbol.h"
#include "pmu.h"
#include "pcerrc.h"


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

const char* SHORT_OPS = "p:d:h";
const struct option LONG_OPS[] = 
{
    {"pid", required_argument, nullptr, 'p'},
    {"duration", required_argument, nullptr, 'd'},
    {"help", required_argument, nullptr, 'h'},
    {nullptr, required_argument, nullptr, 0},
};

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

int ParseArgv(int argc, char** argv, int& pid, int& duration, bool& isLaunch)
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
            case 'h':
                curIndex += 2;
                std::cout << "usage pmu_datasrc -d 2 -p 10001 or pmu_datasrc -d 2 /home/test/falsesharing_demo" << std::endl;
                return -1;
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
    ss << std::hex << sym->addr << " " << sym->symbolName << "+0x" << sym->offset << " " << std::dec << sym->fileName << ":" << sym->lineNum;
    return ss.str();
}

typedef std::pair<std::string, int> SYMBOL_NUM_PAIR;

bool SortBySymValue(const SYMBOL_NUM_PAIR& t1, const SYMBOL_NUM_PAIR& t2)
{
    return t1.second > t2.second;
}

int main(int argc, char** argv)
{
    int pid = -1;
    int duration = 10;
    bool isLaunch = false;

    int err = ParseArgv(argc, argv, pid, duration, isLaunch);
    if (err == -1) {
        return -1;
    }

    if (pid == -1) {
        std::cout << "usage pmu_datasrc -d 2 -p 10001 or pmu_datasrc -d 2 /home/test/falsesharing_demo" << std::endl;
        return -1;
    }

    PmuAttr attr = {0};
    int pidList[1];
    pidList[0] = pid;
    attr.pidList = pidList;
    attr.numPid = 1;
    attr.period = 1024;
    attr.dataFilter = SPE_DATA_ALL;
    attr.evFilter = SPE_EVENT_RETIRED;
    attr.symbolMode = SymbolMode::RESOLVE_ELF_DWARF;

    int pd = PmuOpen(SPE_SAMPLING, &attr);
    if (pd == -1) {
        std::cout << "kperf pmu open spe failed, err is: " << Perror() << std::endl;
        return -1;
    }
    PmuEnable(pd);
    sleep(duration);
    PmuDisable(pd);

    PmuData* data = nullptr;
    int len = PmuRead(pd, &data);
    std::map<uint16_t, int> sourceList;
    std::map<uint16_t, std::map<std::string, int>> sourceSymList;

    for (int i = 0; i < len; i++)
    {
        auto o = data[i];
        if (HIP_STR_MAP.find(o.ext->source) == HIP_STR_MAP.end()) {
            continue;
        }
        auto sym = o.stack->symbol;
        if (sym) {
            std::string symStr = ParseSymbol(sym);
            sourceSymList[o.ext->source][symStr] += 1;
        }
        sourceList[o.ext->source] += 1;
    }

    for (const auto& item : sourceList) {
        auto source = item.first;
        auto source_num = item.second;
        std::cout << HIP_STR_MAP[source] << " " << source_num << std::endl;
        if (sourceSymList.find(source) == sourceSymList.end()) {
            continue;
        }
        auto symList = sourceSymList[source];
        std::vector<SYMBOL_NUM_PAIR> sortVec(symList.begin(), symList.end());
        std::sort(sortVec.begin(), sortVec.end(), SortBySymValue);
        for (const auto& symItem : sortVec) {
            std::cout << "    " << "|——" << symItem.first << " [" << symItem.second << "]" << std::endl;
        }
    }
    PmuClose(pd);
    if (isLaunch) {
        kill(pid, 9);
    }
}