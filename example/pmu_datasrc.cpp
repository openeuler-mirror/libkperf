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
    int ldCnt;
    int stCnt;
    int atCnt;
};

static inline bool HasLoad(const VaItem& v)
{
    return v.ldCnt > 0;
}
static inline bool HasStore(const VaItem& v)
{
    return v.stCnt > 0 || v.atCnt > 0;
}

static inline bool ShouldCountFS(const VaItem& a, const VaItem& b)
{
    bool aStore = HasStore(a);
    bool bStore = HasStore(b);
    bool aLoad = HasLoad(a);
    bool bLoad = HasLoad(b);

    // store-store
    if (aStore && bStore) {
        return true;
    }

    // load-store
    if ((aLoad && bStore) || (aStore && bLoad)) {
        return true;
    }
    // load-load / unknown-only
    return false;
}

struct ArgsContext {
    int pid = -1;
    int duration = 10;
    bool isLaunch = false;
    char* cgroupName = nullptr;
    bool computeFs = true;
    int fd[2];
};

struct RacePcCompare {
    bool operator() (const pair<VaItem, VaItem> &a, const pair<VaItem, VaItem> &b) const {
        if (a.first.pc != b.first.pc) {
            return a.first.pc < b.first.pc;
        }
	return a.second.pc < b.second.pc;
    };
};

static inline bool WithinCacheLine(ulong a, ulong b)
{
    const int CACHE_LINE = 64;
    return (a > b) ? (a - b < CACHE_LINE) : (b - a < CACHE_LINE);
}

void ComputeRacePc(const ulong mypc, const map<ulong, int> &vas, 
            const map<ulong, map<ulong, VaItem>> &vaPcMap, map<pair<VaItem, VaItem>, int, RacePcCompare> &racepc)
{
    for (const auto &vaPair : vas) {
        // Iterate over virtual addresses, find which other virtual addresses is near current va,
        // i.e., less than 64 bytes between them.
        auto va = vaPair.first;
        auto vaCnt = vaPair.second;
        VaItem racePc{};
        racePc.pc = mypc;
        racePc.va = va;
        racePc.cnt = vaCnt;

        auto itVa = vaPcMap.find(va);
        if (itVa != vaPcMap.end()) {
            auto itPc = itVa->second.find(mypc);
            if (itPc != itVa->second.end()) {
                racePc.ldCnt = itPc->second.ldCnt;
                racePc.stCnt = itPc->second.stCnt;
            }
        }

        for (const auto &vapc : vaPcMap) {
            auto otherVa = vapc.first;
            if (!WithinCacheLine(va, otherVa)) {
                continue;
            }
            for (const auto &vaItemPair : vapc.second) {
                const VaItem &otherPcItem = vaItemPair.second;
                if (otherPcItem.pc == mypc) {
                    continue;
                }
                VaItem otherRacePc{};
                otherRacePc.pc    = otherPcItem.pc;
                otherRacePc.va    = otherVa;
                otherRacePc.cnt   = otherPcItem.cnt;
                otherRacePc.ldCnt = otherPcItem.ldCnt;
                otherRacePc.stCnt = otherPcItem.stCnt;

                if (!ShouldCountFS(racePc, otherRacePc)) {
                    continue;
                }
                auto key = (mypc > otherPcItem.pc) ? std::make_pair(otherPcItem, racePc) : std::make_pair(racePc, otherPcItem);
                racepc[key] += std::min(racePc.cnt, otherPcItem.cnt);
            }
        }
    }
}

int ExecCommand(std::vector<std::string>& comms, int fd[2])
{
    pipe(fd);
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed!");
        return -1;
    } else if (pid == 0) {
        close(fd[1]);
        char buf[4];
        int ret = read(fd[0], buf, 4);
        if (ret < 1) {
            std::cout << "read error" << std::endl;
            exit(EXIT_FAILURE);
        }
        char **argv = new char*[comms.size() + 1];
        for (size_t i = 0; i < comms.size(); ++i) {
            argv[i] = strdup(comms[i].c_str());
        }
        argv[comms.size()] = NULL;
        execvp(argv[0], argv);
       
        union sigval val;
        val.sival_int = errno;
        if (sigqueue(getppid(), SIGUSR1, val)) {
            perror(argv[0]);
        }
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

static volatile int execErrNo;

static void ExecFailedSignal(int signo, siginfo_t* info, void* ucontext)
{
    execErrNo = info->si_value.sival_int;
}

int ParseArgv(int argc, char** argv, struct ArgsContext& act)
{
    int longIndex;
    int ret;
    int curIndex = 0;
    while((ret = getopt_long(argc, argv, SHORT_OPS, LONG_OPS, &longIndex)) != -1) {
        switch(ret) {
            case 'p':
                curIndex += 2;
                try {
                    act.pid = std::stoi(optarg);
                } catch(...) {
                    std::cout << "pid is number, can't be: " << optarg << std::endl;
                    return -1;
                }
                break;
            case 'd':
                curIndex += 2;
                try {
                    act.duration = std::stoi(optarg);
                } catch(...) {
                    std::cout << "duration is number, can't be: " << optarg << std::endl;
                    return -1;
                }
                break;
            case 'c':
                curIndex += 2;
                act.cgroupName = optarg;
                break;
            case 'h':
                curIndex += 2;
                std::cout << "usage pmu_datasrc -d 2 -p 10001 or pmu_datasrc -d 2 /home/test/falsesharing_demo" << std::endl;
                return -1;
            case 'f':
                curIndex += 2;
                act.computeFs = true;
                break;
            default:
                return -1;
        }
    }

    if (act.pid == -1 && argc > curIndex + 1) {
        std::vector<std::string> comms;
        for (int i = curIndex + 1; i < argc; ++i) {
            comms.push_back(argv[i]);
        }
        act.pid = ExecCommand(comms, act.fd);
        act.isLaunch = true;
        struct sigaction si;
        si.sa_flags = SA_SIGINFO;
        si.sa_sigaction = ExecFailedSignal;
        sigaction(SIGUSR1, &si, NULL);
        close(act.fd[0]);
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

void KillApp(int pid, bool isLaunch)
{
    if (isLaunch) {
        kill(pid, 9);
    }
}

int main(int argc, char** argv)
{
    startTs = Time();
    struct ArgsContext act;

    int err = ParseArgv(argc, argv, act);
    if (err == -1) {
        return -1;
    }

    if (act.pid == -1 && act.cgroupName == nullptr) {
        std::cout << "usage pmu_datasrc -d 2 -p 10001 or pmu_datasrc -d 2 /home/test/falsesharing_demo" << std::endl;
        return -1;
    }

    if (act.pid > 0 && act.cgroupName != nullptr) {
        KillApp(act.pid, act.isLaunch);
        std::cout << "Cannot specify both cgroup and pid. Please use only one" << std::endl;
        return -1;
    }

    PmuAttr attr = {0};
    char* cgroupNameList[1] = {act.cgroupName};
    int pidList[1];
    pidList[0] = act.pid;
    if (act.cgroupName != nullptr) {    
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
    if (act.isLaunch) {
        attr.enableOnExec = 1;
    }

    int pd = PmuOpen(SPE_SAMPLING, &attr);
    if (pd == -1) {
        KillApp(act.pid, act.isLaunch);
        std::cout << "kperf pmu open spe failed, err is: " << Perror() << std::endl;
        return -1;
    }

    if (act.isLaunch) {
        int ret = write(act.fd[1], "data", 4);
        if (ret < 0) {
            std::cout << "write error" << std::endl;
            return -1;
        }
    }
    
    PrintTime("start collect");
    int num = act.duration * 100;
    PmuData* data = nullptr;
    int len = 0;
    if (!act.isLaunch) {
        PmuEnable(pd);
    }
    for (int i = 0; i < num; i++) {
        usleep(100 * 100);
        if (execErrNo) {
            std::cout << "exec failed:" <<  strerror(execErrNo) << std::endl;
            PmuClose(pd);
            KillApp(act.pid, act.isLaunch);
            return -1;
        }
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
        if (o.ext->op & SPE_OP_LD) {
            vaItem.ldCnt++;
        }
        if (o.ext->op & SPE_OP_ST) {
            vaItem.stCnt++;
        }
        if ((o.ext->op & SPE_OP_ST) && (o.ext->op & SPE_OP_ATOMIC)) {
            vaItem.atCnt++;
        }
    }

    PrintTime("prepared");

    // Inst addresses pair which may access same cacheline.
    // key: pair
    // value: accumulated overlap hits on the same cacheline (sum of min(hitA, hitB) across matched VA)
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
            if (act.computeFs && HITM_SET.find(source) != HITM_SET.end()) {
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
    long long totalOverlap = 0;
    for (const auto &kv : racepc) {
        totalOverlap += kv.second;
    }
    PrintTime("sorted");

    if (act.computeFs) {
        cout << "Possible false sharing: \n";
        for (auto &race : sortedList) {
            auto &race1 = race.first.first;
            auto &race2 = race.first.second;
            float pct = (totalOverlap > 0) ? (race.second * 100.0f / (float)totalOverlap) : 0.0f;
            cout << std::hex << race1.pc << "<->" << race2.pc
                << " [" << std::dec << race.second << " " 
                << fixed << setprecision(4) << pct << "%]\n";
        }
    }
    PmuClose(pd);
    KillApp(act.pid, act.isLaunch);

    return 0;
}
