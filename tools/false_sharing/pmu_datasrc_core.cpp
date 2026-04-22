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
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <array>
#include <cctype>
#include <iomanip>
#include <limits>

#include "symbol.h"
#include "pmu.h"
#include "pcerrc.h"

#define RESERVED_SIZE 8192

using namespace std;
typedef unsigned long ulong;

// cacheline settings
static constexpr size_t CACHELINE_SIZE = 64;

// fixed internal window configs
static constexpr int64_t FS_WINDOW_NS = 5LL * 1000 * 1000; // 5ms
static constexpr int MIN_CONFLICT_WINDOWS = 2;

struct ByteMask {
    uint64_t bits = 0;

    void Or(const ByteMask& rhs)
    {
        bits |= rhs.bits;
    }

    bool Overlaps(const ByteMask& rhs) const
    {
        return (bits & rhs.bits) != 0;
    }
};

// marks which bytes within a cache line are accessed for overlap detection
static inline ByteMask MakeByteMask(uint64_t va, int accessBytes)
{
    ByteMask m;
    if (accessBytes <= 0) {
        accessBytes = 1;
    }

    size_t begin = static_cast<size_t>(va % CACHELINE_SIZE);
    size_t width = std::min(static_cast<size_t>(accessBytes), CACHELINE_SIZE - begin);
    if (width >= 64) {
        m.bits = ~0ULL;
    } else {
        m.bits = ((1ULL << width) - 1ULL) << begin;
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
    uint32_t ldCntAll = 0;
    uint32_t ldCntHitm = 0;
    uint32_t stCnt = 0;
    ulong sampleVa = 0;
    ulong sampleLdVa = 0;
    ulong sampleLdHitmVa = 0;
    ulong sampleStVa = 0;
};

struct PcPair {
    ulong a = 0;
    ulong b = 0;
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

struct PcMeta {
    ulong pc = 0;
    std::string display;
    std::string func;
    std::string module;
    std::string file;
    int line = 0;
};

static PcMeta ParseSymbolMeta(Symbol* sym)
{
    PcMeta meta;
    if (!sym) {
        return meta;
    }
    meta.pc = sym->codeMapAddr;
    meta.func = sym->symbolName;
    meta.module = sym->module;
    meta.file = sym->fileName;
    meta.line = sym->lineNum;

    std::stringstream ss;
    ss << std::hex << sym->codeMapAddr << " " << sym->symbolName << "+0x" << sym->offset
       << " " << std::dec << sym->fileName << ":" << sym->lineNum;
    meta.display = ss.str();
    return meta;
}

typedef std::pair<std::string, Item> SYMBOL_NUM_PAIR;

static bool SortBySymValue(const SYMBOL_NUM_PAIR& t1, const SYMBOL_NUM_PAIR& t2)
{
    return t1.second.cnt > t2.second.cnt;
}

struct Distinct2 {
    int v1 = std::numeric_limits<int>::min();
    int v2 = std::numeric_limits<int>::min();

    void Add(int v)
    {
        if (v1 == std::numeric_limits<int>::min()) {
            v1 = v;
            return;
        }
        if (v == v1) {
            return;
        }
        if (v2 == std::numeric_limits<int>::min()) {
            v2 = v;
        }
    }

    int Count() const
    {
        if (v1 == std::numeric_limits<int>::min()) {
            return 0;
        }
        if (v2 == std::numeric_limits<int>::min()) {
            return 1;
        }
        return 2;
    }

    bool HasAtLeast2() const
    {
        return Count() >= 2;
    }
};

struct LineStat {
    uint64_t totalAccesses = 0;
    Distinct2 cpus;
    Distinct2 tids;
};

struct WindowBucket {
    uint32_t ldCntAll = 0;
    uint32_t ldCntHitm = 0;
    uint32_t ldCntHazard = 0;
    uint32_t stCnt = 0;
    ByteMask mask;
    ulong sampleVa = 0;
    ulong sampleLdVa = 0;
    ulong sampleLdHitmVa = 0;
    ulong sampleStVa = 0;
};

// cacheline(lineKey) -> window -> cpu -> tid -> pc -> mask -> bucket
using MaskBucketMap = std::unordered_map<uint64_t, WindowBucket>;
using PcMaskBucketMap = std::unordered_map<ulong, MaskBucketMap>;
using TidPcMaskBucketMap = std::unordered_map<int, PcMaskBucketMap>;
using CpuTidPcBucketMap = std::unordered_map<int, TidPcMaskBucketMap>;
using WindowCpuTidPcBucketMap = std::unordered_map<uint64_t, CpuTidPcBucketMap>;
using LineWindowCpuTidPcBucketMap = std::unordered_map<ulong, WindowCpuTidPcBucketMap>;

using PcMetaMap = std::unordered_map<ulong, PcMeta>;
using PcAccessBytesMap = std::unordered_map<ulong, int>;
using PcBucketMap = std::unordered_map<ulong, PcBucket>;

struct PairInfo {
    ulong lineKey = 0;
    ulong vaA = 0;
    ulong vaB = 0;
    std::string kind = "NA";
    int bestScore = 0;
    int bestEvidence = 0;
};

struct PairAgg {
    int totalScore = 0;
    int evidenceScore = 0;
    int conflictWindows = 0;
    PairInfo best;
};

struct IssueGroup {
    ulong lineKey = 0;
    ulong repPcA = 0;
    ulong repPcB = 0;
    ulong repVaA = 0;
    ulong repVaB = 0;
    std::string kind = "NA";
    int totalScore = 0;
    int evidenceScore = 0;
    int conflictWindows = 0;
};

struct StatContext {
    SourceList sourceList;
    SourceSymMap sourceSymList;
    LineWindowCpuTidPcBucketMap lineWindowCpuTidPcBucket;
    PcBucketMap pcGlobalBucket;
    PcMetaMap pcMeta;
    PcAccessBytesMap pc2accessBytes;
    std::unordered_map<ulong, LineStat> lineStats;

    void Reserve()
    {
        lineWindowCpuTidPcBucket.reserve(RESERVED_SIZE);
        pcGlobalBucket.reserve(RESERVED_SIZE);
        pcMeta.reserve(RESERVED_SIZE);
        pc2accessBytes.reserve(RESERVED_SIZE);
        lineStats.reserve(RESERVED_SIZE);
    }
};

static inline bool ReadMemProcessVM(pid_t pid, ulong addr, void* out, size_t n)
{
    if (pid <= 0 || addr == 0 || out == nullptr || n == 0) {
        return false;
    }
    iovec local{out, n};
    iovec remote{reinterpret_cast<void*>(addr), n};
    ssize_t r = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    return r == (ssize_t)n;
}

static inline bool ReadInsn32(pid_t pid, ulong pc, uint32_t& insn)
{
    insn = 0;
    return ReadMemProcessVM(pid, pc, &insn, sizeof(insn));
}

static inline int DecodeAccessBytes(uint32_t insn)
{
    bool vr = ((insn >> 26) & 0x1u) != 0;
    uint32_t opc = (insn >> 30) & 0x3u;
    if ((insn & 0x3A000000u) == 0x28000000u) {
        int bytesPerReg = 0;
        if (!vr) {
            bytesPerReg = 4 << ((opc >> 1) & 0x1u);
            return bytesPerReg * 2;
        } else {
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

static inline int ResolveAccessBytesCachedMap(PcAccessBytesMap& cache, pid_t pidForRead, ulong pc, int fallbackBytes)
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

static inline int ResolveAccessBytesCached(StatContext& ctx, pid_t pidForRead, ulong pc, int fallbackBytes)
{
    return ResolveAccessBytesCachedMap(ctx.pc2accessBytes, pidForRead, pc, fallbackBytes);
}

static inline uint64_t MakeWindowId(int64_t ts)
{
    if (ts <= 0) {
        return 0;
    }
    return static_cast<uint64_t>(ts / FS_WINDOW_NS);
}

double Time()
{
    timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

void PrintTime(const string& msg)
{
    printf("[%f]%s\n", Time(), msg.c_str());
}

void KillApp(int pid, bool isLaunch)
{
    if (isLaunch) {
        kill(pid, 9);
    }
}

static std::string ToHex(ulong v)
{
    std::ostringstream ss;
    ss << "0x" << std::hex << v << std::dec;
    return ss.str();
}

static volatile int execErrNo;
static volatile sig_atomic_t g_stopRequested = 0;

static void ExecFailedSignal(int signo, siginfo_t* info, void* ucontext)
{
    (void)signo;
    (void)ucontext;
    execErrNo = info->si_value.sival_int;
}

static void StopCollectSignal(int signo)
{
    (void)signo;
    g_stopRequested = 1;
}

int ExecCommand(std::vector<std::string>& comms, int fd[2])
{
    if (pipe(fd) != 0) {
        perror("pipe failed!");
        return -1;
    }
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed!");
        close(fd[0]);
        close(fd[1]);
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
    while((ret = getopt_long(argc, argv, SHORT_OPS, LONG_OPS, &longIndex)) != -1) {
        switch(ret) {
            case 'p':
                try {
                    act.pid = std::stoi(optarg);
                } catch(...) {
                    std::cout << "pid is number, can't be: " << optarg << std::endl;
                    return -1;
                }
                break;
            case 'd':
                try {
                    act.duration = std::stoi(optarg);
                } catch(...) {
                    std::cout << "duration is number, can't be: " << optarg << std::endl;
                    return -1;
                }
                break;
            case 'c':
                act.cgroupName = optarg;
                break;
            case 'h':
                PrintHelp();
                return -1;
            case 'f':
                act.computeFs = true;
                break;
            case 't':
                act.computeTs = true;
                break;
            case 'F':
                act.format = true;
                break;
            default:
                return -1;
        }
    }

    if (act.pid == -1 && optind < argc) {
        std::vector<std::string> comms;
        for (int i = optind; i < argc; ++i) {
            comms.push_back(argv[i]);
        }
        act.pid = ExecCommand(comms, act.fd);
        act.isLaunch = true;
        struct sigaction si{};
        si.sa_flags = SA_SIGINFO;
        si.sa_sigaction = ExecFailedSignal;
        sigemptyset(&si.sa_mask);
        sigaction(SIGUSR1, &si, NULL);
        close(act.fd[0]);
    }
    return 0;
}

static inline uint16_t NormalizeHipSrc(uint16_t rawSrc)
{
    return (HIP_STR_MAP.find(rawSrc) == HIP_STR_MAP.end()) ? HIP_UNKNOWN : rawSrc;
}

struct SampleInfo {
    bool hasExt = false;
    bool hasSym = false;
    uint16_t src = HIP_UNKNOWN;

    bool isLoad = false;
    bool isStore = false;
    bool isAtomic = false;
    bool isHazard = false;
    bool isHitm = false;

    ulong pc = 0;
    ulong va = 0;
    int cpu = 0;
    int tid = 0;
    int64_t ts = 0;
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

    s.isAtomic = (o.ext->op & SPE_OP_ATOMIC) != 0;
    s.isLoad = ((o.ext->op & SPE_OP_LD) != 0) || s.isAtomic;
    s.isStore = ((o.ext->op & SPE_OP_ST) != 0) || s.isAtomic;
    s.isHazard = (o.ext->event & (SPE_FORWARD_HAZARD | SPE_STRUCTURE_HAZARD)) != 0;
    s.isHitm = IsHitm(s.src);

    s.va = o.ext->va;
    s.cpu = o.cpu;
    s.tid = o.tid;
    s.ts = o.ts;
    if (o.stack && o.stack->symbol) {
        s.sym = o.stack->symbol;
        s.pc = s.sym->codeMapAddr;
        s.hasSym = true;
    }
    return s;
}

static inline void UpdateWindowBucket(LineWindowCpuTidPcBucketMap& lineWindowCpuTidPcBucket, ulong lineKey, uint64_t windowId,
                                      int cpu, int tid, ulong pc, const SampleInfo& si, const ByteMask& mask)
{
    auto& b = lineWindowCpuTidPcBucket[lineKey][windowId][cpu][tid][pc][mask.bits];
    b.mask = mask;
    if (si.isLoad) {
        b.ldCntAll++;
        if (b.sampleLdVa == 0) {
            b.sampleLdVa = si.va;
        }
        if (si.isHitm) {
            b.ldCntHitm++;
            if (b.sampleLdHitmVa == 0) {
                b.sampleLdHitmVa = si.va;
            }
        }
        if (si.isHazard) {
            b.ldCntHazard++;
        }
    }
    if (si.isStore) {
        b.stCnt++;
        if (b.sampleStVa == 0) {
            b.sampleStVa = si.va;
        }
    }
    if (b.sampleVa == 0) {
        b.sampleVa = si.va;
    }
}

static inline void UpdateGlobalPcBucket(PcBucketMap& pcGlobalBucket, ulong pc, const SampleInfo& si)
{
    auto& g = pcGlobalBucket[pc];
    if (si.isLoad) {
        g.ldCntAll++;
        if (g.sampleLdVa == 0) {
            g.sampleLdVa = si.va;
        }
        if (si.isHitm) {
            g.ldCntHitm++;
            if (g.sampleLdHitmVa == 0) {
                g.sampleLdHitmVa = si.va;
            }
        }
    }
    if (si.isStore) {
        g.stCnt++;
        if (g.sampleStVa == 0) {
            g.sampleStVa = si.va;
        }
    }
    if (g.sampleVa == 0) {
        g.sampleVa = si.va;
    }
}

static void BuildAggregations(const ArgsContext& act, PmuData* data, int len, StatContext& context)
{
    context.Reserve();
    for (int i = 0; i < len; i++) {
        const auto& o = data[i];
        SampleInfo si = PrepSample(o);
        if (!si.hasExt) {
            continue;
        }

        const PcMeta* metaPtr = nullptr;
        if (si.hasSym && si.pc != 0) {
            auto itMeta = context.pcMeta.find(si.pc);
            if (itMeta == context.pcMeta.end()) {
                PcMeta meta = ParseSymbolMeta(si.sym);
                auto ret = context.pcMeta.emplace(si.pc, std::move(meta));
                metaPtr = &ret.first->second;
            } else {
                metaPtr = &itMeta->second;
            }
        }

        context.sourceList[si.src] += 1;
        if (si.hasSym && metaPtr != nullptr) {
            auto& item = context.sourceSymList[si.src][metaPtr->display];
            item.cnt++;
            item.vas[si.va]++;
            item.pc = si.pc;
        }
        if (!(act.computeFs || act.computeTs)) {
            continue;
        }
        if (!si.isLoad && !si.isStore) {
            continue;
        }
        if (!si.hasSym || si.pc == 0) {
            continue;
        }

        pid_t pidForRead = (pid_t)act.pid;
        if (pidForRead <= 0 && si.tid > 0) {
            pidForRead = (pid_t)si.tid;
        }

        int accessBytes = ResolveAccessBytesCached(context, pidForRead, si.pc, 4);
        ByteMask mask = MakeByteMask(si.va, accessBytes);
        ulong lineKey = si.va / CACHELINE_SIZE;
        uint64_t windowId = MakeWindowId(si.ts);

        UpdateWindowBucket(context.lineWindowCpuTidPcBucket, lineKey, windowId, si.cpu, si.tid, si.pc, si, mask);
        UpdateGlobalPcBucket(context.pcGlobalBucket, si.pc, si);
        auto& ls = context.lineStats[lineKey];
        ls.totalAccesses++;
        ls.cpus.Add(si.cpu);
        ls.tids.Add(si.tid);
    }
}

// detect datasrc capability
static bool DetectfilterFsByHitm(PmuData* data, int len)
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

static inline void ProcessChunk(const ArgsContext& act, PmuData* chunk, int chunkLen, StatContext& context, bool& useDataSrc)
{
    if (chunk == nullptr || chunkLen <= 0) {
        return;
    }
    if (!useDataSrc) {
        useDataSrc = DetectfilterFsByHitm(chunk, chunkLen);
    }
    BuildAggregations(act, chunk, chunkLen, context);
}

static bool CollectPmuData(const ArgsContext& act, int& pd, StatContext& context, bool& useDataSrc)
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
    attr.minLatency = 10;
    attr.evFilter = SPE_EVENT_RETIRED;
    attr.symbolMode = SymbolMode::RESOLVE_ELF_DWARF;
    attr.excludeKernel = true;
    if (act.isLaunch) {
        attr.enableOnExec = 1;
    }
    pd = -1;
    bool launchFdClosed = false;

    auto closeLaunchFd = [&]() {
        if (!launchFdClosed && act.fd[1] >= 0) {
            close(act.fd[1]);
            launchFdClosed = true;
        }
    };

    auto closePd = [&]() {
        if (pd != -1) {
            PmuClose(pd);
            pd = -1;
        }
    };

    auto readAndProcessOneChunk = [&]() {
        PmuData* chunk = nullptr;
        int chunkLen = PmuRead(pd, &chunk);
        if (chunkLen > 0 && chunk != nullptr) {
            ProcessChunk(act, chunk, chunkLen, context, useDataSrc);
        }
        if (chunk != nullptr) {
            PmuDataFree(chunk);
            chunk = nullptr;
        }
    };

    pd = PmuOpen(SPE_SAMPLING, &attr);
    if (pd == -1) {
        std::cout << "PmuOpen failed, err is: " << Perror() << std::endl;
        return false;
    }

    if (act.isLaunch) {
        int ret = write(act.fd[1], "data", 4);
        closeLaunchFd();
        if (ret < 0) {
            std::cout << "write error" << std::endl;
            closePd();
            return false;
        }
    }

    PrintTime("start collect");
    int num = act.duration * 10;
    if (!act.isLaunch) {
        PmuEnable(pd);
    }

    for (int i = 0; i < num; ++i) {
        if (g_stopRequested) {
            PrintTime("stop requested by SIGINT");
            break;
        }
        usleep(100 * 1000);
        if (g_stopRequested) {
            PrintTime("stop requested by SIGINT");
            break;
        }
        if (execErrNo) {
            std::cout << "exec failed:" << strerror(execErrNo) << std::endl;
            closePd();
            return false;
        }
        readAndProcessOneChunk();
    }
    if (pd != -1) {
        PmuDisable(pd);
    }
    readAndProcessOneChunk();
    if (g_stopRequested) {
        PrintTime("collect interrupted, output partial results");
    } else {
        PrintTime("end collect");
    }
    return true;
}

static void PrintSourceSummary(const StatContext& context)
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

        auto& symList = itSysMap->second;
        std::vector<SYMBOL_NUM_PAIR> sortVec(symList.begin(), symList.end());
        std::sort(sortVec.begin(), sortVec.end(), SortBySymValue);
        for (const auto& symItem : sortVec) {
            auto& it = symItem.second;
            std::cout << "    " << "|--" << symItem.first << " [" << it.cnt << "]" << std::endl;
        }
    }
}

struct AccessRef {
    int cpu = 0;
    int tid = 0;
    ulong pc = 0;
    const WindowBucket* b = nullptr;
    ByteMask mask;
    ulong va = 0;
};

enum class SharingMode : uint8_t {
    FALSE_SHARING = 0,
    TRUE_SHARING = 1
};

static inline bool HasLoadForMode(const WindowBucket& b, SharingMode mode)
{
    (void)mode;
    return b.ldCntAll > 0;
}

static inline uint32_t LoadCountForMode(const WindowBucket& b, SharingMode mode)
{
    (void)mode;
    return b.ldCntAll;
}

static inline ulong LoadSampleVaForMode(const WindowBucket& b, bool useDataSrc)
{
    if (useDataSrc && b.sampleLdHitmVa != 0) {
        return b.sampleLdHitmVa;
    }
    if (b.sampleLdVa != 0) {
        return b.sampleLdVa;
    }
    return b.sampleVa;
}

static inline int EvidenceForLoad(const WindowBucket& b, int score, bool useDataSrc)
{
    int evidence = std::min(score, (int)b.ldCntHazard);
    if (useDataSrc) {
        evidence += std::min(score, (int)b.ldCntHitm);
    }
    return evidence;
}

static inline bool IsSystemModulePath(const std::string& module)
{
    return module.rfind("/usr/lib", 0) == 0 || module.rfind("/lib", 0) == 0;
}

static inline int PairModuleBias(const StatContext& context, ulong pc1, ulong pc2)
{
    int bias = 0;
    auto itA = context.pcMeta.find(pc1);
    if (itA != context.pcMeta.end() && !IsSystemModulePath(itA->second.module)) {
        bias++;
    }
    auto itB = context.pcMeta.find(pc2);
    if (itB != context.pcMeta.end() && !IsSystemModulePath(itB->second.module)) {
        bias++;
    }
    return bias;
}

static inline int KindBias(const std::string& kind)
{
    return (kind == "SL") ? 1 : 0;
}

static inline void UpdatePairAgg(std::unordered_map<PcPair, PairAgg, PcPairHash, PcPairEq>& pairMap, const PcPair& pair, ulong lineKey,
                                 const AccessRef& a, const AccessRef& b, const char* kind, int score, int evidence)
{
    auto& agg = pairMap[pair];
    agg.totalScore += score;
    agg.evidenceScore += evidence;

    PairInfo cand;
    cand.lineKey = lineKey;
    cand.kind = kind;
    cand.bestScore = score;
    cand.bestEvidence = evidence;
    if (a.pc == pair.a) {
        cand.vaA = a.va;
        cand.vaB = b.va;
    } else {
        cand.vaA = b.va;
        cand.vaB = a.va;
    }

    if (score > agg.best.bestScore ||
        (score == agg.best.bestScore && evidence > agg.best.bestEvidence)) {
        agg.best = cand;
    }
}

static inline bool BetterRepCandidate(const PcPair& candPair, const PairAgg& candAgg, const PcPair* bestPair,
                                      const PairAgg* bestAgg, const StatContext& context)
{
    if (bestPair == nullptr || bestAgg == nullptr) {
        return true;
    }

    if (candAgg.totalScore != bestAgg->totalScore) {
        return candAgg.totalScore > bestAgg->totalScore;
    }
    if (candAgg.evidenceScore != bestAgg->evidenceScore) {
        return candAgg.evidenceScore > bestAgg->evidenceScore;
    }
    if (candAgg.conflictWindows != bestAgg->conflictWindows) {
        return candAgg.conflictWindows > bestAgg->conflictWindows;
    }

    int candBias = PairModuleBias(context, candPair.a, candPair.b);
    int bestBias = PairModuleBias(context, bestPair->a, bestPair->b);
    if (candBias != bestBias) {
        return candBias > bestBias;
    }

    int candKindBias = KindBias(candAgg.best.kind);
    int bestKindBias = KindBias(bestAgg->best.kind);
    if (candKindBias != bestKindBias) {
        return candKindBias > bestKindBias;
    }

    if (candPair.a != bestPair->a) {
        return candPair.a < bestPair->a;
    }
    return candPair.b < bestPair->b;
}

static std::vector<IssueGroup> ComputeSharingIssues(
    const LineWindowCpuTidPcBucketMap& lineWindowCpuTidPcBucket,
    SharingMode mode,
    bool useDataSrc,
    const StatContext& context)
{
    std::vector<IssueGroup> issues;
    issues.reserve(lineWindowCpuTidPcBucket.size());

    for (const auto& lineEntry : lineWindowCpuTidPcBucket) {
        ulong lineKey = lineEntry.first;

        auto itLineStat = context.lineStats.find(lineKey);
        if (itLineStat == context.lineStats.end()) {
            continue;
        }
        if (!itLineStat->second.tids.HasAtLeast2()) {
            continue;
        }

        bool lineCrossCpu = itLineStat->second.cpus.HasAtLeast2();
        std::unordered_map<PcPair, PairAgg, PcPairHash, PcPairEq> pairMap;
        pairMap.reserve(64);

        IssueGroup issue;
        issue.lineKey = lineKey;

        for (const auto& windowEntry : lineEntry.second) {
            const auto& cpuMap = windowEntry.second;

            std::vector<AccessRef> stores;
            std::vector<AccessRef> loads;
            Distinct2 windowTids;
            Distinct2 windowCpus;

            for (const auto& cpuEntry : cpuMap) {
                int cpu = cpuEntry.first;
                windowCpus.Add(cpu);
                for (const auto& tidEntry : cpuEntry.second) {
                    int tid = tidEntry.first;
                    windowTids.Add(tid);
                    for (const auto& pcEntry : tidEntry.second) {
                        ulong pc = pcEntry.first;
                        for (const auto& maskEntry : pcEntry.second) {
                            const WindowBucket& b = maskEntry.second;
                            if (b.stCnt > 0) {
                                stores.push_back({
                                    cpu,
                                    tid,
                                    pc,
                                    &b,
                                    b.mask,
                                    (b.sampleStVa != 0) ? b.sampleStVa : b.sampleVa,
                                });
                            }
                            if (HasLoadForMode(b, mode)) {
                                loads.push_back({
                                    cpu,
                                    tid,
                                    pc,
                                    &b,
                                    b.mask,
                                    LoadSampleVaForMode(b, useDataSrc),
                                });
                            }
                        }
                    }
                }
            }

            if (stores.empty()) {
                continue;
            }
            if (!windowTids.HasAtLeast2()) {
                continue;
            }
            if (mode == SharingMode::FALSE_SHARING && !(windowCpus.HasAtLeast2() || lineCrossCpu)) {
                continue;
            }

            int windowBestScore = 0;
            int windowBestEvidence = 0;
            std::unordered_set<PcPair, PcPairHash, PcPairEq> seenPairs;
            seenPairs.reserve(32);

            for (size_t i = 0; i < stores.size(); ++i) {
                for (size_t j = i + 1; j < stores.size(); ++j) {
                    if (stores[i].tid == stores[j].tid) {
                        continue;
                    }

                    bool overlap = stores[i].mask.Overlaps(stores[j].mask);
                    if (mode == SharingMode::TRUE_SHARING ? !overlap : overlap) {
                        continue;
                    }

                    if (mode == SharingMode::FALSE_SHARING &&
                        !(stores[i].cpu != stores[j].cpu || lineCrossCpu)) {
                        continue;
                    }

                    int score = std::min((int)stores[i].b->stCnt, (int)stores[j].b->stCnt);
                    if (score <= 0) {
                        continue;
                    }

                    PcPair pair = MakePcPair(stores[i].pc, stores[j].pc);
                    UpdatePairAgg(pairMap, pair, lineKey, stores[i], stores[j], "SS", score, 0);
                    seenPairs.insert(pair);

                    if (score > windowBestScore) {
                        windowBestScore = score;
                        windowBestEvidence = 0;
                    }
                }
            }

            for (size_t i = 0; i < stores.size(); ++i) {
                for (size_t j = 0; j < loads.size(); ++j) {
                    if (stores[i].tid == loads[j].tid) {
                        continue;
                    }

                    bool overlap = stores[i].mask.Overlaps(loads[j].mask);
                    if (mode == SharingMode::TRUE_SHARING ? !overlap : overlap) {
                        continue;
                    }

                    if (mode == SharingMode::FALSE_SHARING &&
                        !(stores[i].cpu != loads[j].cpu || lineCrossCpu)) {
                        continue;
                    }

                    int score = std::min((int)stores[i].b->stCnt, (int)LoadCountForMode(*loads[j].b, mode));
                    if (score <= 0) {
                        continue;
                    }

                    int evidence = EvidenceForLoad(*loads[j].b, score, useDataSrc);
                    PcPair pair = MakePcPair(stores[i].pc, loads[j].pc);
                    UpdatePairAgg(pairMap, pair, lineKey, stores[i], loads[j], "SL", score, evidence);
                    seenPairs.insert(pair);

                    if (score > windowBestScore ||
                        (score == windowBestScore && evidence > windowBestEvidence)) {
                        windowBestScore = score;
                        windowBestEvidence = evidence;
                    }
                }
            }

            if (windowBestScore > 0) {
                issue.totalScore += windowBestScore;
                issue.evidenceScore += windowBestEvidence;
                issue.conflictWindows += 1;
                for (const auto& pair : seenPairs) {
                    pairMap[pair].conflictWindows += 1;
                }
            }
        }

        if (issue.totalScore <= 0) {
            continue;
        }
        if (issue.conflictWindows < MIN_CONFLICT_WINDOWS) {
            continue;
        }

        const PcPair* bestPair = nullptr;
        const PairAgg* bestAgg = nullptr;
        for (const auto& kv : pairMap) {
            if (BetterRepCandidate(kv.first, kv.second, bestPair, bestAgg, context)) {
                bestPair = &kv.first;
                bestAgg = &kv.second;
            }
        }

        if (bestPair == nullptr || bestAgg == nullptr) {
            continue;
        }

        issue.repPcA = bestPair->a;
        issue.repPcB = bestPair->b;
        issue.repVaA = bestAgg->best.vaA;
        issue.repVaB = bestAgg->best.vaB;
        issue.kind = bestAgg->best.kind;
        issues.push_back(issue);
    }

    std::sort(
        issues.begin(),
        issues.end(),
        [](const IssueGroup& x, const IssueGroup& y) {
            if (x.totalScore != y.totalScore) {
                return x.totalScore > y.totalScore;
            }
            if (x.evidenceScore != y.evidenceScore) {
                return x.evidenceScore > y.evidenceScore;
            }
            if (x.conflictWindows != y.conflictWindows) {
                return x.conflictWindows > y.conflictWindows;
            }
            return x.lineKey < y.lineKey;
        });

    return issues;
}

static inline std::string SanitizeFormatField(const std::string& s)
{
    std::string out = s;
    for (char& ch : out) {
        if (ch == ',' || ch == '|' || ch == '\n' || ch == '\r') {
            ch = ' ';
        }
    }
    return out;
}

static std::string MakeFuncModule(const PcMeta* meta)
{
    if (meta == nullptr) {
        return "UNKNOWN";
    }

    if (!meta->func.empty() && !meta->module.empty()) {
        return meta->func + "@" + meta->module;
    }
    if (!meta->func.empty()) {
        return meta->func;
    }
    return "UNKNOWN";
}

static std::string MakeLineField(const PcMeta* meta)
{
    if (meta == nullptr || meta->line <= 0) {
        return "UNKNOWN";
    }

    return std::to_string(meta->line);
}

static std::string MakeFormatEndpointField(ulong pc, const PcMeta* meta)
{
    std::ostringstream ss;
    ss << ToHex(pc) << "|" << SanitizeFormatField(MakeFuncModule(meta)) << "|" << SanitizeFormatField(MakeLineField(meta));
    return ss.str();
}

static inline void ByteRangeInLine(uint64_t off, int accessBytes, int& begin, int& end, bool& crossLine)
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

static int GetAccessBytesForPc(const StatContext& context, ulong pc, int fallbackBytes = 4)
{
    auto it = context.pc2accessBytes.find(pc);
    if (it != context.pc2accessBytes.end() && it->second > 0) {
        return it->second;
    }
    return fallbackBytes;
}

static std::string MakeSuggestionHint(ulong vaA, int bytesA, ulong vaB, int bytesB, ulong cacheLineAddr)
{
    if (cacheLineAddr == 0 || vaA < cacheLineAddr || vaB < cacheLineAddr) {
        return "same-line";
    }

    ulong offA = vaA - cacheLineAddr;
    ulong offB = vaB - cacheLineAddr;

    bool adjacent = false;
    if (bytesA > 0 && bytesB > 0) {
        adjacent = (offA + (ulong)bytesA == offB) || (offB + (ulong)bytesB == offA);
    }

    std::ostringstream oss;
    oss << (adjacent ? "adjacent" : "same-line")
        << ";+0x" << std::hex << offA << "/+0x" << offB << std::dec
        << ";" << bytesA << "B/" << bytesB << "B";
    return oss.str();
}

static void PrintSharingFormatResults(const char* memType, const std::vector<IssueGroup>& issues, const StatContext& context)
{
    for (size_t i = 0; i < issues.size(); ++i) {
        const auto& issue = issues[i];
        const PcMeta* metaA = nullptr;
        const PcMeta* metaB = nullptr;

        auto itA = context.pcMeta.find(issue.repPcA);
        if (itA != context.pcMeta.end()) {
            metaA = &itA->second;
        }
        auto itB = context.pcMeta.find(issue.repPcB);
        if (itB != context.pcMeta.end()) {
            metaB = &itB->second;
        }

        const int bytesA = GetAccessBytesForPc(context, issue.repPcA, 4);
        const int bytesB = GetAccessBytesForPc(context, issue.repPcB, 4);
        const uint64_t cacheLineAddr = issue.lineKey * CACHELINE_SIZE;
        const std::string hint = MakeSuggestionHint(issue.repVaA, bytesA, issue.repVaB, bytesB, cacheLineAddr);

        std::cout << memType << "," << (i + 1) << ","
                  << MakeFormatEndpointField(issue.repPcA, metaA) << ","
                  << MakeFormatEndpointField(issue.repPcB, metaB) << ","
                  << issue.kind << "," << ToHex(cacheLineAddr) << "," << hint << "\n";
    }
}

static void PrintResults(const std::vector<IssueGroup>& issues, const PcBucketMap& pcGlobalBucket, const StatContext& context)
{
    long long totalScore = 0;
    for (const auto& issue : issues) {
        totalScore += issue.totalScore;
    }

    for (const auto& issue : issues) {
        ulong pc1 = issue.repPcA;
        ulong pc2 = issue.repPcB;
        PcBucket a{}, b{};
        auto itGA = pcGlobalBucket.find(pc1);
        if (itGA != pcGlobalBucket.end()) {
            a = itGA->second;
        }
        auto itGB = pcGlobalBucket.find(pc2);
        if (itGB != pcGlobalBucket.end()) {
            b = itGB->second;
        }

        int bytesA = 4;
        int bytesB = 4;
        auto itBytesA = context.pc2accessBytes.find(pc1);
        if (itBytesA != context.pc2accessBytes.end()) {
            bytesA = itBytesA->second;
        }
        auto itBytesB = context.pc2accessBytes.find(pc2);
        if (itBytesB != context.pc2accessBytes.end()) {
            bytesB = itBytesB->second;
        }

        float pct = (totalScore > 0) ? (issue.totalScore * 100.0f / (float)totalScore) : 0.0f;
        std::cout << std::hex << pc1 << "<->" << pc2 << std::dec << " [" << issue.totalScore << " "
                  << std::fixed << std::setprecision(4) << pct << "%]"
                  << " A(cnt/ld/st=" << (a.ldCntAll + a.stCnt) << "/" << a.ldCntAll << "/" << a.stCnt << ")"
                  << " B(cnt/ld/st=" << (b.ldCntAll + b.stCnt) << "/" << b.ldCntAll << "/" << b.stCnt << ")"
                  << " A_sz=" << bytesA << "B" << " B_sz=" << bytesB << "B" << "\n";

        auto itA = context.pcMeta.find(pc1);
        if (itA != context.pcMeta.end()) {
            std::cout << "    A: " << itA->second.display << "\n";
        } else {
            std::cout << "    A: UNKNOWN\n";
        }
        auto itB = context.pcMeta.find(pc2);
        if (itB != context.pcMeta.end()) {
            std::cout << "    B: " << itB->second.display << "\n";
        } else {
            std::cout << "    B: UNKNOWN\n";
        }

        uint64_t lineAddr = issue.lineKey * CACHELINE_SIZE;
        ulong offA = (issue.repVaA >= lineAddr) ? (issue.repVaA - lineAddr) : 0;
        ulong offB = (issue.repVaB >= lineAddr) ? (issue.repVaB - lineAddr) : 0;
        int aBeg = 0, aEnd = 0, bBeg = 0, bEnd = 0;
        bool aCross = false, bCross = false;

        ByteRangeInLine(offA, bytesA, aBeg, aEnd, aCross);
        ByteRangeInLine(offB, bytesB, bBeg, bEnd, bCross);
        std::cout << "    line=" << ToHex(lineAddr) << " kind=" << issue.kind << " A_va=" << ToHex(issue.repVaA)
                  << "(+0x" << std::hex << offA << std::dec << ")" << " cacheline bytes=" << aBeg << "-" << aEnd << (aCross ? "(cross)" : "")
                  << ", B_va=" << ToHex(issue.repVaB) << "(+0x" << std::hex << offB << std::dec << ")"
                  << " cacheline bytes=" << bBeg << "-" << bEnd << (bCross ? "(cross)" : "") << "\n";
    }
}

int pmu_datasrc_run(int argc, char** argv)
{
    ArgsContext act;
    execErrNo = 0;
    g_stopRequested = 0;

    int err = ParseArgv(argc, argv, act);
    if (err == -1) {
        return -1;
    }
    struct sigaction saInt{};
    saInt.sa_handler = StopCollectSignal;
    sigemptyset(&saInt.sa_mask);
    saInt.sa_flags = 0;
    sigaction(SIGINT, &saInt, NULL);

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
    StatContext context;
    bool useDataSrc = false;

    auto cleanup = [&]() {
        if (pd != -1) {
            PmuClose(pd);
            pd = -1;
        }
        KillApp(act.pid, act.isLaunch);
    };

    if (!CollectPmuData(act, pd, context, useDataSrc)) {
        cleanup();
        return -1;
    }

    if (!act.format) {
        PrintSourceSummary(context);
    }

    if (act.computeFs) {
        auto issues = ComputeSharingIssues(context.lineWindowCpuTidPcBucket, SharingMode::FALSE_SHARING, useDataSrc, context);
        if (!act.format) {
            PrintResults(issues, context.pcGlobalBucket, context);
        } else {
            PrintSharingFormatResults("FS", issues, context);
        }
    }

    if (act.computeTs) {
        auto issues = ComputeSharingIssues(context.lineWindowCpuTidPcBucket, SharingMode::TRUE_SHARING, false, context);
        if (!act.format) {
            PrintResults(issues, context.pcGlobalBucket, context);
        } else {
            PrintSharingFormatResults("TS", issues, context);
        }
    }

    cleanup();
    return 0;
}
