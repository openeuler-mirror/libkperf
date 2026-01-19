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
#include <unordered_map>
#include <cstdint>

#include "symbol.h"
#include "pmu.h"
#include "pcerrc.h"

#define RESERVED_SISE 8192

using namespace std;
typedef unsigned long ulong;


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

struct ArgsContext {
    int pid = -1;
    int duration = 10;
    bool isLaunch = false;
    char* cgroupName = nullptr;
    bool computeFs = false;
    int fd[2];
};

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

// in cacheline bucket
struct PcBucket {
    uint32_t cnt   = 0;
    uint32_t ldCnt = 0;
    uint32_t stCnt = 0;
    uint64_t accessMask = 0;
};

static inline bool HasLoad(const PcBucket& a)
{
    return a.ldCnt > 0;
}

static inline bool HasStore(const PcBucket& a) {
    return a.stCnt > 0;
}

struct PcPair {
    ulong a;
    ulong b;
};

struct PcPairHash {
    size_t operator()(const PcPair& p) const noexcept {
        size_t h1 = std::hash<ulong>{}(p.a);
        size_t h2 = std::hash<ulong>{}(p.b);
        return h1 * 1315423911u + h2;
    }
};

struct PcPairEq {
    bool operator()(const PcPair& x, const PcPair& y) const noexcept {
        return x.a == y.a && x.b == y.b;
    }
};

static inline PcPair MakePcPair(ulong x, ulong y)
{
    return (x < y) ? PcPair{x, y} : PcPair{y, x};
}

using SourceList = std::map<uint16_t, int>;
using SourceSymMap = std::map<uint16_t, std::map<std::string, Item>>;
using LinePcBucketMap = std::unordered_map<ulong, std::unordered_map<ulong, PcBucket>>;
using PcBucketMap = std::unordered_map<ulong, PcBucket>;
using RacePcMap = std::unordered_map<PcPair, int, PcPairHash, PcPairEq>;

static bool CollectPmuData(const ArgsContext &act, int &pd, PmuData* &data, int &len)
{
    PmuAttr attr = {0};
    static char* cgroupNameList[1];
    static int pidList[1];
    cgroupNameList[0] = act.cgroupName;
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

    pd = PmuOpen(SPE_SAMPLING, &attr);
    if (pd == -1) {
        std::cout << "kperf pmu open spe failed, err is: " << Perror() << std::endl;
        return false;
    }

    if (act.isLaunch) {
        int ret = write(act.fd[1], "data", 4);
        if (ret < 0) {
            std::cout << "write error" << std::endl;
            PmuClose(pd);
            return false;
        }
    }

    PrintTime("start collect");
    int num = act.duration * 100;
    if (!act.isLaunch) {
        PmuEnable(pd);
    }
    for (int i = 0; i < num; i++) {
        usleep(100 * 100);
        if (execErrNo) {
            std::cout << "exec failed:" << strerror(execErrNo) << std::endl;
            PmuClose(pd);
            return false;
        }
        PmuData* fromData = nullptr;
        PmuRead(pd, &fromData);
        int curLen = PmuAppendData(fromData, &data);
        if (curLen) {
            len = curLen;
        }
    }
    PrintTime("end collect");
    return true;
}

static void BuildAggregations(const ArgsContext &act, PmuData* data, int len, SourceList &sourceList, SourceSymMap &sourceSymList,
                              LinePcBucketMap &linePcBucket,PcBucketMap &pcGlobalBucket)
{
    linePcBucket.reserve(RESERVED_SISE);    // cachelie -> (pc -> PcBucket), for combine PCs in same cache line
    pcGlobalBucket.reserve(RESERVED_SISE);  // pc -> PcBucket, for global statistics of print

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

        // falsesharing only in HITM sources
        if (!act.computeFs || HITM_SET.find(o.ext->source) == HITM_SET.end()) {
            continue;
        }
        if (!sym) {
            continue;
        }
        ulong pc = sym->codeMapAddr;
        if (pc == 0) {
            continue;
        }

        ulong va = o.ext->va;
        ulong line = (va >> 6);  // mapping all addresses of the same cache line to the same bucket key
        auto &pcMap = linePcBucket[line];
        auto &item = pcMap[pc];  // numbers of this pc on this cacheline
        item.cnt++;
        uint64_t offset = va & 0x03F;
        item.accessMask |= (1ULL << offset);
        if (o.ext->op & SPE_OP_LD) {
            item.ldCnt++;
        }
        if (o.ext->op & SPE_OP_ST) {
            item.stCnt++;
        }
        // summary info
        auto &g = pcGlobalBucket[pc];
        g.cnt++;
        if (o.ext->op & SPE_OP_LD) {
            g.ldCnt++;
        }
        if (o.ext->op & SPE_OP_ST) {
            g.stCnt++;
        }
    }
}

static void PrintSourceSummary(const SourceList &sourceList, const SourceSymMap &sourceSymList, int &totalSource)
{
    totalSource = 0;
    for (const auto& item : sourceList) {
        auto source = item.first;
        auto sourceNum = item.second;
        std::cout << HIP_STR_MAP[source] << " " << sourceNum << std::endl;
        totalSource += sourceNum;
        auto itSysMap = sourceSymList.find(source);
        if (itSysMap == sourceSymList.end()) {
            continue;
        }
        auto &symList = itSysMap->second;
        std::vector<SYMBOL_NUM_PAIR> sortVec(symList.begin(), symList.end());
        std::sort(sortVec.begin(), sortVec.end(), SortBySymValue);
        for (const auto& symItem : sortVec) {
            auto &it = symItem.second;
            std::cout << "    " << "|--" << symItem.first << " [" << it.cnt << "]" << std::endl;
        }
    }
}

static void ComputeFsFromCachelines(const LinePcBucketMap &linePcBucket, RacePcMap &racepc)
{
    racepc.reserve(RESERVED_SISE);

    for (const auto &lineEntry : linePcBucket) {
        const auto &pcMap = lineEntry.second;
        // If fewer than 2 instructions access this cacheline, not included in statistics
        if (pcMap.size() < 2) {
            continue;
        }

        std::vector<std::pair<ulong, const PcBucket*>> stores;
        std::vector<std::pair<ulong, const PcBucket*>> loads;
        stores.reserve(pcMap.size());
        loads.reserve(pcMap.size());
        for (const auto &kv : pcMap) {
            ulong pc = kv.first;
            const PcBucket &item = kv.second;
            bool isStore = HasStore(item);
            bool isLoad  = HasLoad(item);
            if (isStore) {
                stores.push_back({pc, &item});
            } else if (isLoad) {
                loads.push_back({pc, &item});
            }
        }

        if (stores.empty()) {
            continue;
        }
        // store-store
        for (size_t i = 0; i < stores.size(); ++i) {
            for (size_t j = i + 1; j < stores.size(); ++j) {
                if (stores[i].second->accessMask & stores[j].second->accessMask) {
                    continue;  // true sharing
                }
                int score = std::min(stores[i].second->cnt, stores[j].second->cnt);
                if (score > 0) {
                    racepc[MakePcPair(stores[i].first, stores[j].first)] += score;
                }
            }
        }

        // store-load
        for (size_t i = 0; i < stores.size(); ++i) {
            for (size_t j = 0; j < loads.size(); ++j) {
                if (stores[i].second->accessMask & loads[j].second->accessMask) {
                    continue;  // true sharing
                }
                int score = std::min(stores[i].second->cnt, loads[j].second->cnt);
                if (score > 0) {
                    racepc[MakePcPair(stores[i].first, loads[j].first)] += score;
                }
            }
        }
    }
}

static void PrintFsResults(const RacePcMap &racepc, const PcBucketMap &pcGlobalBucket)
{
    std::vector<std::pair<PcPair, int>> sortedList;
    sortedList.reserve(racepc.size());
    for (const auto &kv : racepc) {
        sortedList.push_back(kv);
    }

    std::sort(sortedList.begin(), sortedList.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });

    long long totalOverlap = 0;
    for (const auto &kv : sortedList) {
        totalOverlap += kv.second;
    }
    PrintTime("sorted");

    cout << "Possible false sharing:\n";
    for (const auto &race : sortedList) {
        ulong pc1 = race.first.a;
        ulong pc2 = race.first.b;
        int score = race.second;
        float pct = (totalOverlap > 0) ? (score * 100.0f / (float)totalOverlap) : 0.0f;

        PcBucket a{}, b{};
        auto itA = pcGlobalBucket.find(pc1);
        if (itA != pcGlobalBucket.end()) a = itA->second;
        auto itB = pcGlobalBucket.find(pc2);
        if (itB != pcGlobalBucket.end()) b = itB->second;

        // pc1<->pc2 [score pct] A(...) B(...)
        cout << std::hex << pc1 << "<->" << pc2 << " [" << std::dec << score << " "
             << fixed << setprecision(4) << pct << "%]"
             << " A(" << a.cnt << "/" << a.ldCnt << "/" << a.stCnt << ")"
             << " B(" << b.cnt << "/" << b.ldCnt << "/" << b.stCnt << ")" << "\n";
    }
}

int main(int argc, char** argv)
{
    ArgsContext act;

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

    int pd = -1;
    PmuData* data = nullptr;
    int len = 0;
    if (!CollectPmuData(act, pd, data, len)) {
        KillApp(act.pid, act.isLaunch);
        return -1;
    }

    SourceList sourceList;
    SourceSymMap sourceSymList;
    LinePcBucketMap linePcBucket;
    PcBucketMap pcGlobalBucket;

    BuildAggregations(act, data, len, sourceList, sourceSymList, linePcBucket, pcGlobalBucket);
    PrintTime("prepared");

    int totalSource = 0;
    PrintSourceSummary(sourceList, sourceSymList, totalSource);

    if (act.computeFs) {
        RacePcMap racepc;
        ComputeFsFromCachelines(linePcBucket, racepc);
        PrintTime("computed");
        PrintFsResults(racepc, pcGlobalBucket);
    }

    KillApp(act.pid, act.isLaunch);
    PmuClose(pd);
    return 0;
}
