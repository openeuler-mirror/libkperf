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
#include <unordered_set>
#include <cstdint>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/uio.h>
#include <fcntl.h>

#include "symbol.h"
#include "pmu.h"
#include "pcerrc.h"

#define RESERVED_SISE 8192

using namespace std;
typedef unsigned long ulong;

// cacheline settings
static constexpr size_t CACHELINE_SIZE = 64;
static constexpr unsigned CACHELINE_SHIFT = 6;
static constexpr uint32_t SLOT_BYTES = 4;
static constexpr uint32_t SLOT_COUNT = CACHELINE_SIZE / SLOT_BYTES;

// check whether different PCs on the same cacheline access the same slot
static inline uint64_t MakeWordMask64(ulong va, int assumedAccessBytes)
{
    ulong off = va & (CACHELINE_SIZE - 1);
    uint32_t off0 = (uint32_t)(off >> 2);
    uint32_t off1 = (uint32_t)((off + (ulong)assumedAccessBytes - 1) >> 2);

    if (off0 >= SLOT_COUNT) {
        return 0;
    }
    if (off1 >= SLOT_COUNT) {
        off1 = SLOT_COUNT - 1;
    }

    uint64_t m = 0;
    for (uint32_t k = off0; k <= off1; ++k) {
        m |= (1ULL << k);
    }
    return m;
}

static constexpr uint16_t HIP_UNKNOWN = 0xFFFF;
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
    {HIP_UNKNOWN, "HIP_UNKNOWN"},
};

static std::set<uint16_t> HITM_SET = {
    HIP_PEER_CPU_HITM,
    HIP_L3_HITM,
    HIP_L2_HITM,
    HIP_PEER_CLUSTER_HITM,
    HIP_REMOTE_SOCKET_HITM
};

static inline bool IsHitm(uint16_t src)
{
    return HITM_SET.find(src) != HITM_SET.end();
}

const char* SHORT_OPS = "p:d:c:hftF";
const struct option LONG_OPS[] =
{
    {"pid", required_argument, nullptr, 'p'},
    {"duration", required_argument, nullptr, 'd'},
    {"cgroupName", required_argument, nullptr, 'c'},
    {"help", no_argument, nullptr, 'h'},
    {"fs", no_argument, nullptr, 'f'},
    {"ts", no_argument, nullptr, 't'},
    {"format", no_argument, nullptr, 'F'},
    {nullptr, 0, nullptr, 0},
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
    bool computeTs = false;
    int fd[2];
    bool format = false;
};

using SourceList   = std::map<uint16_t, int>;
using SourceSymMap = std::map<uint16_t, std::map<std::string, Item>>;

struct PcBucket {
    uint32_t cnt   = 0;
    uint32_t ldCnt = 0;
    uint32_t stCnt = 0;
    uint64_t ldMask = 0;
    uint64_t stMask = 0;
    ulong sampleVa = 0;
    ulong sampleLdVa = 0;
    ulong sampleStVa = 0;
};

static inline bool HasLoad(const PcBucket& a) {
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

// cacheline(lineKey) -> cpu -> tid -> pc -> PcBucket(ld/st cnt, mask and sample VA)
using PcBucketMap  = std::unordered_map<ulong, PcBucket>;                   // pc -> bucket
using TidPcBucketMap = std::unordered_map<int, PcBucketMap>;                // tid -> (pc -> bucket)
using CpuTidPcBucketMap = std::unordered_map<int, TidPcBucketMap>;          // cpu -> (tid -> ...)
using LineCpuTidPcBucketMap = std::unordered_map<ulong, CpuTidPcBucketMap>; // lineKey -> cpuMap

using RacePcMap = std::unordered_map<PcPair, int, PcPairHash, PcPairEq>;
using PcSymMap  = std::unordered_map<ulong, std::string>;
using PcAccessBytesMap = std::unordered_map<ulong, int>;  // pc -> decoded access bytes

struct StatContext {
    SourceList sourceList;
    SourceSymMap sourceSymList;
    LineCpuTidPcBucketMap lineCpuTidPcBucketFs;
    LineCpuTidPcBucketMap lineCpuTidPcBucketTs;
    PcBucketMap pcGlobalBucketFs;
    PcBucketMap pcGlobalBucketTs;
    PcSymMap pc2sym;
    PcAccessBytesMap pc2accessBytes;
    bool filterFsByHitm = false;
    void Reserve() {
        lineCpuTidPcBucketFs.reserve(RESERVED_SISE);
        lineCpuTidPcBucketTs.reserve(RESERVED_SISE);
        pcGlobalBucketFs.reserve(RESERVED_SISE);
        pcGlobalBucketTs.reserve(RESERVED_SISE);
        pc2sym.reserve(RESERVED_SISE);
        pc2accessBytes.reserve(RESERVED_SISE);
    }
};

// read instruction and decode access bytes
static inline bool ReadMemProcessVM(pid_t pid, ulong addr, void* out, size_t n)
{
    if (pid <= 0 || addr == 0 || out == nullptr || n == 0) {
        return false;
    }
    iovec local{ out, n };
    iovec remote{ reinterpret_cast<void*>(addr), n };
    ssize_t r = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    return r == (ssize_t)n;
}

static inline bool ReadInsn32(pid_t pid, ulong pc, uint32_t &insn)
{
    insn = 0;
    if (ReadMemProcessVM(pid, pc, &insn, sizeof(insn))) {
        return true;
    }
    return false;
}

// return the number of bytes accessed by ld/st instruction
static inline int DecodeAccessBytes(uint32_t insn)
{
    bool vr = ((insn >> 26) & 0x1u) != 0;
    uint32_t opc = (insn >> 30) & 0x3u;
    if ((insn & 0x3A000000u) == 0x28000000u) {  // LDP/STP pair
        int bytesPerReg = 0;
        if (!vr) {  // GPR pair
            bytesPerReg = 4 << ((opc >> 1) & 0x1u);
            return bytesPerReg * 2;
        } else {  // SIMD/FD pair
            if (opc == 3) {
                return 0;
            }
            bytesPerReg = 4 << opc;
            return bytesPerReg * 2;
        }
    }
    if (vr) {
        return 16;
    }
    uint32_t size = opc;
    return (int)(1u << size);
}

// resolve access bytes with per-PC cache
static inline int ResolveAccessBytesCachedMap(PcAccessBytesMap &cache, pid_t pidForRead, ulong pc, int fallbackBytes)
{
    auto it = cache.find(pc);
    if (it != cache.end()) {
        return it->second;
    }
    int bytes = fallbackBytes;
    uint32_t insn = 0;
    if (ReadInsn32(pidForRead, pc, insn)) {
        int d = DecodeAccessBytes(insn);
        if (d > 0) {
            bytes = d;
        }
    }
    cache.emplace(pc, bytes);
    return bytes;
}

static inline int ResolveAccessBytesCached(StatContext &ctx, pid_t pidForRead, ulong pc, int fallbackBytes)
{
    return ResolveAccessBytesCachedMap(ctx.pc2accessBytes, pidForRead, pc, fallbackBytes);
}

double Time()
{
    timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec/1e6;
}

void PrintTime(const string &msg)
{
    printf("[%f]%s\n", Time(), msg.c_str());
}

void KillApp(int pid, bool isLaunch)
{
    if (isLaunch) {
        kill(pid, 9);
    }
}

static volatile int execErrNo;

static void ExecFailedSignal(int signo, siginfo_t* info, void* ucontext)
{
    execErrNo = info->si_value.sival_int;
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

static void PrintHelp()
{
    std::cout << "usage:\n"
        << "  ./pmu_datasrc -d 2 -p 10001\n"
        << "  ./pmu_datasrc -d 2 -f case/falsesharing_demo\n"
        << "options:\n"
        << "  -p, --pid <pid>\n"
        << "  -d, --duration <sec>\n"
        << "  -c, --cgroupName <name>\n"
        << "  -f, --fs  enable false sharing analysis\n"
        << "  -t, --ts  enable true sharing analysis\n"
        << "  -F, --format  output the standard format of FS/TS\n";
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
                curIndex += 1;
                PrintHelp();
                return -1;
            case 'f':
                curIndex += 1;
                act.computeFs = true;
                break;
            case 't':
                curIndex += 1;
                act.computeTs = true;
                break;
            case 'F':
                curIndex += 1;
                act.format = true;
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

static inline uint16_t NormalizeHipSrc(uint16_t rawSrc)
{
    return (HIP_STR_MAP.find(rawSrc) == HIP_STR_MAP.end()) ? HIP_UNKNOWN : rawSrc;
}

struct SampleInfo {
    bool hasExt = false;
    bool hasSym = false;
    uint16_t src = HIP_UNKNOWN;
    bool keepLd = false;
    bool keepSt = false;
    ulong pc = 0;
    ulong va = 0;
    int cpu = 0;
    int tid = 0;
    Symbol* sym = nullptr;
};

static inline SampleInfo PrepSample(const PmuData& o)
{
    SampleInfo s{};
    if (!o.ext) {
        return s;
    }
    s.hasExt = true;
    s.src = NormalizeHipSrc(o.ext->source);

    bool isLoad  = (o.ext->op & SPE_OP_LD) != 0;
    bool isStore = (o.ext->op & SPE_OP_ST) != 0;
    bool isAtomic = (o.ext->op & SPE_OP_ATOMIC) != 0;
    if (isAtomic) {
        isStore = true;
    }

    s.keepSt = isStore;
    s.keepLd = isLoad && ((o.ext->event & SPE_FORWARD_HAZARD) || (o.ext->event & SPE_STRUCTURE_HAZARD));
    s.va = o.ext->va;
    s.cpu = o.cpu;
    s.tid = o.tid;
    if (o.stack && o.stack->symbol) {
        s.sym = o.stack->symbol;
        s.pc = s.sym->codeMapAddr;
        s.hasSym = true;
    }
    return s;
}

static bool CollectPmuData(const ArgsContext &act, int &pd, PmuData* &data, int &len, PcAccessBytesMap* outPc2accessBytes)
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
    int lastLen = 0;
    if (outPc2accessBytes) {
        outPc2accessBytes->reserve(RESERVED_SISE);
    }
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
        if (outPc2accessBytes && act.pid > 0 && curLen > lastLen) {
            pid_t pidForRead = (pid_t)act.pid;
            for (int k = lastLen; k < curLen; ++k) {
                const auto &o2 = data[k];
                SampleInfo si = PrepSample(o2);
                if (!si.hasSym) {
                    continue;
                }
                if (!si.keepLd && !si.keepSt) {
                    continue;
                }
                if (outPc2accessBytes->find(si.pc) != outPc2accessBytes->end()) {
                    continue;
                }
                (void)ResolveAccessBytesCachedMap(*outPc2accessBytes, pidForRead, si.pc, 4);
            }
        }
        if (curLen > lastLen) {
            lastLen = curLen;
        }
    }
    PrintTime("end collect");
    return true;
}

// detect datasrc capability
static bool DetectHasKnownDataSrc(PmuData* data, int len)
{
    for (int i = 0; i < std::min(50, len); ++i) {
        auto &o = data[i];
        if (!o.ext) {
            continue;
        }
        uint16_t src = NormalizeHipSrc(o.ext->source);
        if (src != HIP_UNKNOWN) {
            return true;
        }
    }
    return false;
}

static void BuildAggregations(const ArgsContext &act, PmuData* data, int len, StatContext &context, const PcAccessBytesMap* seedPc2accessBytes)
{
    context.Reserve();
    context.filterFsByHitm = DetectHasKnownDataSrc(data, len);

    if (seedPc2accessBytes && !seedPc2accessBytes->empty()) {
        context.pc2accessBytes.insert(seedPc2accessBytes->begin(), seedPc2accessBytes->end());
    }

    for (int i = 0; i < len; i++) {
        const auto &o = data[i];
        SampleInfo si = PrepSample(o);
        if (!si.hasExt) {
            continue;
        }
        uint16_t src = si.src;
        // source summary
        context.sourceList[src] += 1;
        if (si.hasSym) {
            std::string symStr = ParseSymbol(si.sym);
            auto &item = context.sourceSymList[src][symStr];
            item.cnt++;
            item.vas[si.va]++;
            item.pc = si.pc;
            if (si.pc != 0 && context.pc2sym.find(si.pc) == context.pc2sym.end()) {
                context.pc2sym[si.pc] = symStr;
            }
        }
        if (!(act.computeFs || act.computeTs)) {
            continue;
        }
        if (!si.keepLd && !si.keepSt) {
            continue;
        }
        if (!si.hasSym) {
            continue;
        }
        if (si.pc == 0) {
            continue;
        }
        ulong pc = si.pc;
        ulong va = si.va;
        ulong lineKey = va >> CACHELINE_SHIFT;
        int cpu = si.cpu;
        int tid = si.tid;
        pid_t pidForRead = (pid_t)act.pid;
        if (pidForRead <= 0 && tid > 0) {
            pidForRead = (pid_t)tid;
        }
        int accessBytes = ResolveAccessBytesCached(context, pidForRead, pc, 4);
        uint64_t mask = MakeWordMask64(va, accessBytes);
        if (act.computeTs) {
            auto &b = context.lineCpuTidPcBucketTs[lineKey][cpu][tid][pc];
            b.cnt++;
            if (b.sampleVa == 0) {
                b.sampleVa = va;
            }
            if (si.keepLd) {
                b.ldCnt++;
                b.ldMask |= mask;
                if (b.sampleLdVa == 0) {
                    b.sampleLdVa = va;
                }
            }
            if (si.keepSt) {
                b.stCnt++;
                b.stMask |= mask;
                if (b.sampleStVa == 0) {
                    b.sampleStVa = va;
                }
            }
            auto &g = context.pcGlobalBucketTs[pc];
            g.cnt++;
            if (si.keepLd) {
                g.ldCnt++;
            }
            if (si.keepSt) {
                g.stCnt++;
            }
        }
        if (act.computeFs) {
            bool keepLdFs = si.keepLd;
            if (context.filterFsByHitm && si.keepLd) {
                if (!IsHitm(src)) {
                    keepLdFs = false;
                }
            }
            bool keepStFs = si.keepSt;
            if (keepLdFs || keepStFs) {
                auto &b = context.lineCpuTidPcBucketFs[lineKey][cpu][tid][pc];
                b.cnt++;
                if (b.sampleVa == 0) {
                    b.sampleVa = va;
                }
                if (keepLdFs) {
                    b.ldCnt++;
                    b.ldMask |= mask;
                    if (b.sampleLdVa == 0) {
                        b.sampleLdVa = va;
                    }
                }
                if (keepStFs) {
                    b.stCnt++;
                    b.stMask |= mask;
                    if (b.sampleStVa == 0) {
                        b.sampleStVa = va;
                    }
                }
                auto &g = context.pcGlobalBucketFs[pc];
                g.cnt++;
                if (keepLdFs) {
                    g.ldCnt++;
                }
                if (keepStFs) {
                    g.stCnt++;
                }
            }
        }
    }
}

// Print source summary
static void PrintSourceSummary(const StatContext &context)
{
    for (const auto& item : context.sourceList) {
        auto source = item.first;
        auto sourceNum = item.second;
        auto itStr = HIP_STR_MAP.find(source);
        std::string name = (itStr == HIP_STR_MAP.end()) ? "HIP_UNKNOWN" : itStr->second;
        std::cout << name << " " << sourceNum << std::endl;
        auto itSysMap = context.sourceSymList.find(source);
        if (itSysMap == context.sourceSymList.end()) {
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

// sharing compute
struct PcCpuTidRef {
    int cpu;
    int tid;
    ulong pc;
    const PcBucket *b;
};

static void ComputeSharingFromCachelines(const LineCpuTidPcBucketMap &lineCpuTidPcBucket, RacePcMap &racepc, bool needTS)
{
    racepc.reserve(RESERVED_SISE);
    for (const auto &lineEntry : lineCpuTidPcBucket) {
        const auto &cpuMap = lineEntry.second;
        if (cpuMap.size() < 2) {
            continue;
        }
        std::vector<PcCpuTidRef> stores;
        std::vector<PcCpuTidRef> loads;
        for (const auto &cpuEntry : cpuMap) {
            int cpu = cpuEntry.first;
            for (const auto &tidEntry : cpuEntry.second) {
                int tid = tidEntry.first;
                for (const auto &kv : tidEntry.second) {
                    if (HasStore(kv.second)) {
                        stores.push_back({cpu, tid, kv.first, &kv.second});
                    }
                    if (HasLoad(kv.second)) {
                        loads.push_back({cpu, tid, kv.first, &kv.second});
                    }
                }
            }
        }

        if (stores.empty()) {
            continue;
        }

        // store-store
        for (size_t i = 0; i < stores.size(); ++i) {
            for (size_t j = i + 1; j < stores.size(); ++j) {
                if (stores[i].cpu == stores[j].cpu || stores[i].tid == stores[j].tid) {
                    continue;
                }
                bool overlap = (stores[i].b->stMask & stores[j].b->stMask) != 0;
                if (needTS ? !overlap : overlap) {
                    continue;
                }
                int score = std::min((int)stores[i].b->stCnt, (int)stores[j].b->stCnt);
                if (score > 0) {
                    racepc[MakePcPair(stores[i].pc, stores[j].pc)] += score;
                }
            }
        }

        // store-load
        for (size_t i = 0; i < stores.size(); ++i) {
            for (size_t j = 0; j < loads.size(); ++j) {
                if (stores[i].cpu == loads[j].cpu || stores[i].tid == loads[j].tid) {
                    continue;
                }
                bool overlap = (stores[i].b->stMask & loads[j].b->ldMask) != 0;
                if (needTS ? !overlap : overlap) {
                    continue;
                }
                int score = std::min((int)stores[i].b->stCnt, (int)loads[j].b->ldCnt);
                if (score > 0) {
                    racepc[MakePcPair(stores[i].pc, loads[j].pc)] += score;
                }
            }
        }
    }
}

static std::vector<std::pair<PcPair, int>> SortRacePc(const RacePcMap &racepc)
{
    std::vector<std::pair<PcPair, int>> sortedList;
    sortedList.reserve(racepc.size());
    for (const auto &kv : racepc) {
        sortedList.push_back(kv);
    }

    std::sort(sortedList.begin(), sortedList.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });
    return sortedList;
}

// cacheline details for TOP pairs
enum class PairKind : uint8_t {
    STORE_STORE = 0,
    STORE_LOAD = 1
};

struct LineBucket {
    int totalScore = 0;
    int bestScore  = 0;
    PairKind kind  = PairKind::STORE_STORE;
    ulong vaA = 0;
    ulong vaB = 0;
};

struct PairBestLine {
    ulong lineKey = 0;
    LineBucket lb;
};

using PairLineBucketMap = std::unordered_map<PcPair, PairBestLine, PcPairHash, PcPairEq>;

struct AccessRef {
    int cpu;
    int tid;
    ulong pc;
    const PcBucket* b;
    uint64_t mask;
    ulong va;
};

static void CollectCacheLineDetails(const LineCpuTidPcBucketMap &lineCpuTidPcBucket,
                                    const std::vector<PcPair> &topPairs,
                                    PairLineBucketMap &out,
                                    bool needTS)
{
    std::unordered_set<PcPair, PcPairHash, PcPairEq> topSet(topPairs.begin(), topPairs.end());

    for (const auto &lineEntry : lineCpuTidPcBucket) {
        ulong lineKey = lineEntry.first;
        const auto &cpuMap = lineEntry.second;
        if (cpuMap.size() < 2) {
            continue;
        }

        std::vector<PcCpuTidRef> stores;
        std::vector<PcCpuTidRef> loads;

        for (const auto &cpuEntry : cpuMap) {
            int cpu = cpuEntry.first;
            for (const auto &tidEntry : cpuEntry.second) {
                int tid = tidEntry.first;
                for (const auto &kv : tidEntry.second) {
                    if (HasStore(kv.second)) {
                        stores.push_back({cpu, tid, kv.first, &kv.second});
                    }
                    if (HasLoad(kv.second)) {
                        loads.push_back({cpu, tid, kv.first, &kv.second});
                    }
                }
            }
        }

        if (stores.empty()) {
            continue;
        }

        std::unordered_map<PcPair, LineBucket, PcPairHash, PcPairEq> lineAccum;
        lineAccum.reserve(64);

        auto updateLineAccum = [&](const PcPair &pair, int score,
                                   const AccessRef &r1, const AccessRef &r2, PairKind kind) {
            auto &lb = lineAccum[pair];
            lb.totalScore += score;
            if (score > lb.bestScore) {
                lb.bestScore = score;
                lb.kind = kind;
                if (r1.pc == pair.a) {
                    lb.vaA = r1.va;
                    lb.vaB = r2.va;
                } else {
                    lb.vaA = r2.va;
                    lb.vaB = r1.va;
                }
            }
        };

        // store-store
        for (size_t i = 0; i < stores.size(); ++i) {
            for (size_t j = i + 1; j < stores.size(); ++j) {
                if (stores[i].cpu == stores[j].cpu || stores[i].tid == stores[j].tid) {
                    continue;
                }
                bool overlap = (stores[i].b->stMask & stores[j].b->stMask) != 0;
                if (needTS ? !overlap : overlap) {
                    continue;
                }

                int score = std::min((int)stores[i].b->stCnt, (int)stores[j].b->stCnt);
                if (score <= 0) {
                    continue;
                }

                PcPair pair = MakePcPair(stores[i].pc, stores[j].pc);
                if (!topSet.count(pair)) {
                    continue;
                }

                AccessRef r1{stores[i].cpu, stores[i].tid, stores[i].pc, stores[i].b,
                             stores[i].b->stMask,
                             (stores[i].b->sampleStVa != 0) ? stores[i].b->sampleStVa : stores[i].b->sampleVa};
                AccessRef r2{stores[j].cpu, stores[j].tid, stores[j].pc, stores[j].b,
                             stores[j].b->stMask,
                             (stores[j].b->sampleStVa != 0) ? stores[j].b->sampleStVa : stores[j].b->sampleVa};

                updateLineAccum(pair, score, r1, r2, PairKind::STORE_STORE);
            }
        }

        // store-load
        for (size_t i = 0; i < stores.size(); ++i) {
            for (size_t j = 0; j < loads.size(); ++j) {
                if (stores[i].cpu == loads[j].cpu || stores[i].tid == loads[j].tid) {
                    continue;
                }
                bool overlap = (stores[i].b->stMask & loads[j].b->ldMask) != 0;
                if (needTS ? !overlap : overlap) {
                    continue;
                }
                int score = std::min((int)stores[i].b->stCnt, (int)loads[j].b->ldCnt);
                if (score <= 0) {
                    continue;
                }
                PcPair pair = MakePcPair(stores[i].pc, loads[j].pc);
                if (!topSet.count(pair)) {
                    continue;
                }
                AccessRef st{stores[i].cpu, stores[i].tid, stores[i].pc, stores[i].b,
                             stores[i].b->stMask,
                             (stores[i].b->sampleStVa != 0) ? stores[i].b->sampleStVa : stores[i].b->sampleVa};
                AccessRef ld{loads[j].cpu, loads[j].tid, loads[j].pc, loads[j].b,
                             loads[j].b->ldMask,
                             (loads[j].b->sampleLdVa != 0) ? loads[j].b->sampleLdVa : loads[j].b->sampleVa};

                updateLineAccum(pair, score, st, ld, PairKind::STORE_LOAD);
            }
        }

        for (auto &kv : lineAccum) {
            const PcPair &pair = kv.first;
            const LineBucket &lb = kv.second;

            auto &best = out[pair];
            if (best.lineKey == 0 || lb.totalScore > best.lb.totalScore) {
                best.lineKey = lineKey;
                best.lb = lb;
            }
        }
    }
}

static inline void ByteRangeInLine(ulong off, int accessBytes,
                                  int &begin, int &end, bool &crossLine)
{
    if (accessBytes <= 0) {
        accessBytes = 1;
    }
    begin = (int)off;
    end = begin + accessBytes - 1;
    crossLine = (end >= (int)CACHELINE_SIZE);
    if (begin < 0) {
        begin = 0;
    }
    if (begin > (int)CACHELINE_SIZE - 1) {
        begin = (int)CACHELINE_SIZE - 1;
    }
    if (end < 0) {
        end = 0;
    }
    if (end > (int)CACHELINE_SIZE - 1) {
        end = (int)CACHELINE_SIZE - 1;
    }
}

static void PrintSharingFormatResults(const char* prefix, const std::vector<std::pair<PcPair, int>> &sortedList, const PairLineBucketMap &pairLineBucket)
{
    for (size_t idx = 0; idx < sortedList.size(); ++idx) {
        const auto &race = sortedList[idx];
        ulong pc1 = race.first.a;
        ulong pc2 = race.first.b;
        const char* kindStr = "NA";
        ulong lineAddr = 0;
        auto itPair = pairLineBucket.find(race.first);
        if (itPair != pairLineBucket.end()) {
            lineAddr = (itPair->second.lineKey << CACHELINE_SHIFT);
            kindStr = (itPair->second.lb.kind == PairKind::STORE_STORE) ? "SS" : "SL";
        }
        std::cout << prefix << "," << (idx + 1) << ",0x" << std::hex << pc1 << ",0x" << std::hex
                  << pc2 << std::dec << "," << kindStr << ",0x" << std::hex << lineAddr << std::dec << "\n";
    }
}

static void PrintResults(const StatContext &context, const PcBucketMap &pcGlobalBucket, 
                        std::vector<std::pair<PcPair, int>> &sortedList, const PairLineBucketMap &pairLineBucket)
{
    std::vector<std::pair<PcPair, int>> list;
    list.reserve(sortedList.size());
    for (auto &kv : sortedList) {
        list.push_back(kv);
    }
    long long totalOverlap = 0;
    for (const auto &kv : list) {
        totalOverlap += kv.second;
    }
    for (size_t idx = 0; idx < list.size(); ++idx) {
        const auto &race = list[idx];
        ulong pc1 = race.first.a;
        ulong pc2 = race.first.b;
        int score = race.second;
        float pct = (totalOverlap > 0) ? (score * 100.0f / (float)totalOverlap) : 0.0f;
        PcBucket a{}, b{};
        auto itA = pcGlobalBucket.find(pc1);
        if (itA != pcGlobalBucket.end()) {
            a = itA->second;
        }
        auto itB = pcGlobalBucket.find(pc2);
        if (itB != pcGlobalBucket.end()) {
            b = itB->second;
        }

        int bytesA = 4, bytesB = 4;
        auto itBytesA = context.pc2accessBytes.find(pc1);
        if (itBytesA != context.pc2accessBytes.end()) {
            bytesA = itBytesA->second;
        }
        auto itBytesB = context.pc2accessBytes.find(pc2);
        if (itBytesB != context.pc2accessBytes.end()) {
            bytesB = itBytesB->second;
        }

        std::cout << std::hex << pc1 << "<->" << pc2 << std::dec << " [" << score << " " << fixed << setprecision(4) << pct << "%]"
                  << " A(cnt/ld/st=" << a.cnt << "/" << a.ldCnt << "/" << a.stCnt << ")" << " B(cnt/ld/st=" << b.cnt << "/" << b.ldCnt << "/" << b.stCnt << ")";
        if (bytesA > 0 || bytesB > 0) {
            std::cout << " A_sz=" << bytesA << "B" << " B_sz=" << bytesB << "B";
        }

        auto symA = context.pc2sym.find(pc1);
        auto symB = context.pc2sym.find(pc2);
        if (symA != context.pc2sym.end()) {
            std::cout << "\n    A: " << symA->second;
        }
        if (symB != context.pc2sym.end()) {
            std::cout << "\n    B: " << symB->second;
        }
        std::cout << std::endl;

        auto itPair = pairLineBucket.find(race.first);
        if (itPair != pairLineBucket.end()) {
            ulong lineKey = itPair->second.lineKey;
            const LineBucket &la = itPair->second.lb;

            ulong lineAddr = lineKey << CACHELINE_SHIFT;
            ulong offA = (la.vaA >= lineAddr) ? (la.vaA - lineAddr) : 0;
            ulong offB = (la.vaB >= lineAddr) ? (la.vaB - lineAddr) : 0;
            const char* kindStr = (la.kind == PairKind::STORE_STORE) ? "SS" : "SL";
            int aBeg=0, aEnd=0, bBeg=0, bEnd=0;
            bool aCross=false, bCross=false;
            ByteRangeInLine(offA, bytesA, aBeg, aEnd, aCross);
            ByteRangeInLine(offB, bytesB, bBeg, bEnd, bCross);
            std::cout << "    line=0x" << std::hex << lineAddr << std::dec << " kind=" << kindStr
                    << " A_va=0x" << std::hex << la.vaA << std::dec << "(+0x" << std::hex << offA << std::dec << ")"
                    << " cacheline bytes=" << aBeg << "-" << aEnd << (aCross ? "(cross)" : "")
                    << ", B_va=0x" << std::hex << la.vaB << std::dec << "(+0x" << std::hex << offB << std::dec << ")"
                    << " cacheline bytes=" << bBeg << "-" << bEnd << (bCross ? "(cross)" : "") << std::endl;
        }
        std::cout << std::endl;
    }
}

int pmu_datasrc_run(int argc, char** argv)
{
    ArgsContext act;

    int err = ParseArgv(argc, argv, act);
    if (err == -1) {
        return -1;
    }

    if (act.pid == -1 && act.cgroupName == nullptr) {
        PrintHelp();
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
    bool needSharing = (act.computeFs || act.computeTs);
    PcAccessBytesMap seedPc2accessBytes;
    if (!CollectPmuData(act, pd, data, len, needSharing ? &seedPc2accessBytes : nullptr)) {
        KillApp(act.pid, act.isLaunch);
        return -1;
    }

    StatContext context;
    BuildAggregations(act, data, len, context, needSharing ? &seedPc2accessBytes : nullptr);
    if (!act.format) {
        PrintSourceSummary(context);
    }

    if (act.computeFs) {
        RacePcMap racepc;
        ComputeSharingFromCachelines(context.lineCpuTidPcBucketFs, racepc, false);
        auto sortedList = SortRacePc(racepc);
        std::vector<PcPair> topPairs;
        topPairs.reserve(sortedList.size());
        for (auto &x : sortedList) {
            topPairs.push_back(x.first);
        }
        PairLineBucketMap pairLineBucket;
        pairLineBucket.reserve(topPairs.size());
        CollectCacheLineDetails(context.lineCpuTidPcBucketFs, topPairs, pairLineBucket, false);
        if (!act.format) {
            std::cout << "Possible false sharing:" << std::endl;
            PrintResults(context, context.pcGlobalBucketFs, sortedList, pairLineBucket);
        } else {
            PrintSharingFormatResults("FS", sortedList, pairLineBucket);
        }
    }

    if (act.computeTs) {
        RacePcMap racepc;
        ComputeSharingFromCachelines(context.lineCpuTidPcBucketTs, racepc, true);
        auto sortedList = SortRacePc(racepc);

        std::vector<PcPair> topPairs;
        topPairs.reserve(sortedList.size());
        for (auto &x : sortedList) {
            topPairs.push_back(x.first);
        }

        PairLineBucketMap pairLineBucket;
        pairLineBucket.reserve(topPairs.size());
        CollectCacheLineDetails(context.lineCpuTidPcBucketTs, topPairs, pairLineBucket, true);

        if (!act.format) {
            std::cout << "Possible true sharing:" << std::endl;
            PrintResults(context, context.pcGlobalBucketTs, sortedList, pairLineBucket);
        } else {
            PrintSharingFormatResults("TS", sortedList, pairLineBucket);
        }
    }

    KillApp(act.pid, act.isLaunch);
    PmuClose(pd);
    return 0;
}
