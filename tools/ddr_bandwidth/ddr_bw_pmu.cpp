#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <unordered_set>
#include <vector>
#include <sstream>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include "pcerrc.h"
#include "symbol.h"
#include "ddr_bw_pmu.h"
#include "ddr_bw_collection.h"
using namespace std;
typedef unsigned long ulong;

int64_t NowNs()
{
    timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

double NowSec()
{
    return static_cast<double>(NowNs()) / 1e9;
}


static bool IsDramSource(unsigned short source)
{
    return source == HIP_LOCAL_MEM || source == HIP_REMOTE_MEM;
}

static bool IsDramEventFallback(unsigned long event)
{
    // An LLC miss is the closest event-only indication that a load reached
    // beyond the cache hierarchy. SPE_EV_REMOTE_ACCESS alone can also describe
    // a remote cache access, so it must not be treated as remote DRAM.
    return (event & SPE_EV_LLC_MISS) != 0;
}

static bool ReadInsn32(pid_t pid, ulong pc, uint32_t& insn)
{
    insn = 0;
    if (pid <= 0 || pc == 0) {
        return false;
    }
    iovec local{&insn, sizeof(insn)};
    iovec remote{reinterpret_cast<void*>(pc), sizeof(insn)};
    ssize_t r = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    return r == static_cast<ssize_t>(sizeof(insn));
}

// Debug-only decoder. It is never used by the production bandwidth attribution
// path. Write attribution is based on store sample counts, not operand width.
static int DecodeAccessBytes(uint32_t insn)
{
    bool vectorReg = ((insn >> 26) & 0x1u) != 0;
    uint32_t opc = (insn >> 30) & 0x3u;
    if ((insn & 0x3A000000u) == 0x28000000u) {
        if (!vectorReg) {
            return (4 << ((opc >> 1) & 0x1u)) * 2;
        }
        if (opc == 3) {
            return 0;
        }
        return (4 << opc) * 2;
    }
    if (vectorReg) {
        return 16;
    }
    return static_cast<int>(1u << opc);
}

struct StorePcDebugKey {
    pid_t pid = -1;
    int cpu = -1;
    ulong pc = 0;

    bool operator<(const StorePcDebugKey& other) const
    {
        if (pid != other.pid) return pid < other.pid;
        if (cpu != other.cpu) return cpu < other.cpu;
        return pc < other.pc;
    }
};

struct StorePcDebugStat {
    uint64_t samples = 0;
    string function;
    string module;
    int accessBytes = 0;
    uint32_t insn = 0;
    bool hasInsn = false;
};

static constexpr size_t MAX_DEBUG_STORE_PC_KEYS = 4096;
static constexpr size_t MAX_DEBUG_STORE_PC_ROWS = 128;
static constexpr size_t MAX_DEBUG_SPE_EVENT_MASKS = 256;

struct SpeEventDescriptor {
    unsigned long bit;
    const char* name;
};

static const SpeEventDescriptor SPE_EVENT_DESCRIPTORS[] = {
    {SPE_EV_EXCEPT, "SPE_EV_EXCEPT"},
    {SPE_EV_RETIRED, "SPE_EV_RETIRED"},
    {SPE_EV_L1D_ACCESS, "SPE_EV_L1D_ACCESS"},
    {SPE_EV_L1D_REFILL, "SPE_EV_L1D_REFILL"},
    {SPE_EV_TLB_ACCESS, "SPE_EV_TLB_ACCESS"},
    {SPE_EV_TLB_WALK, "SPE_EV_TLB_WALK"},
    {SPE_EV_NOT_TAKEN, "SPE_EV_NOT_TAKEN"},
    {SPE_EV_MISPRED, "SPE_EV_MISPRED"},
    {SPE_EV_LLC_ACCESS, "SPE_EV_LLC_ACCESS"},
    {SPE_EV_LLC_MISS, "SPE_EV_LLC_MISS"},
    {SPE_EV_REMOTE_ACCESS, "SPE_EV_REMOTE_ACCESS"},
    {SPE_EV_ALIGNMENT, "SPE_EV_ALIGNMENT"},
    {SPE_EV_PARTIAL_PRED, "SPE_EV_PARTIAL_PRED"},
    {SPE_EV_EMPTY_PRED, "SPE_EV_EMPTY_PRED"},
    {SPE_FORWARD_HAZARD, "SPE_FORWARD_HAZARD"},
    {SPE_STRUCTURE_HAZARD, "SPE_STRUCTURE_HAZARD"},
};

static bool g_speDebugEnabled = false;
static map<StorePcDebugKey, StorePcDebugStat> g_storePcDebugStats;
static uint64_t g_droppedStorePcDebugSamples = 0;
static uint64_t g_speEventRecords = 0;
static uint64_t g_speEventNoneRecords = 0;
static uint64_t g_speEventUnknownRecords = 0;
static unsigned long g_speEventUnknownBits = 0;
static uint64_t g_speEventCounts[sizeof(SPE_EVENT_DESCRIPTORS) /
                                 sizeof(SPE_EVENT_DESCRIPTORS[0])] = {};
static map<unsigned long, uint64_t> g_speEventMaskStats;
static uint64_t g_droppedSpeEventMasks = 0;

void ConfigureSpeDebug(bool enabled)
{
    g_speDebugEnabled = enabled;
    g_storePcDebugStats.clear();
    g_droppedStorePcDebugSamples = 0;
    g_speEventRecords = 0;
    g_speEventNoneRecords = 0;
    g_speEventUnknownRecords = 0;
    g_speEventUnknownBits = 0;
    for (auto& count : g_speEventCounts) count = 0;
    g_speEventMaskStats.clear();
    g_droppedSpeEventMasks = 0;
}

static unsigned long KnownSpeEventBits()
{
    static const unsigned long known = []() {
        unsigned long bits = 0;
        for (const auto& descriptor : SPE_EVENT_DESCRIPTORS) {
            bits |= descriptor.bit;
        }
        return bits;
    }();
    return known;
}

static string SpeEventMaskNames(unsigned long event)
{
    string result;
    for (const auto& descriptor : SPE_EVENT_DESCRIPTORS) {
        if ((event & descriptor.bit) == 0) continue;
        if (!result.empty()) result += '|';
        result += descriptor.name;
    }
    const unsigned long unknown = event & ~KnownSpeEventBits();
    if (unknown != 0) {
        ostringstream value;
        value << "UNKNOWN(0x" << hex << unknown << ')';
        if (!result.empty()) result += '|';
        result += value.str();
    }
    return result.empty() ? "NONE" : result;
}

static void AccountSpeEventDebug(unsigned long event)
{
    if (!g_speDebugEnabled) return;

    ++g_speEventRecords;
    if (event == 0) ++g_speEventNoneRecords;
    for (size_t i = 0; i < sizeof(SPE_EVENT_DESCRIPTORS) /
                            sizeof(SPE_EVENT_DESCRIPTORS[0]); ++i) {
        if ((event & SPE_EVENT_DESCRIPTORS[i].bit) != 0) {
            ++g_speEventCounts[i];
        }
    }
    const unsigned long unknown = event & ~KnownSpeEventBits();
    if (unknown != 0) {
        ++g_speEventUnknownRecords;
        g_speEventUnknownBits |= unknown;
    }

    auto it = g_speEventMaskStats.find(event);
    if (it != g_speEventMaskStats.end()) {
        ++it->second;
    } else if (g_speEventMaskStats.size() < MAX_DEBUG_SPE_EVENT_MASKS) {
        g_speEventMaskStats.emplace(event, 1);
    } else {
        ++g_droppedSpeEventMasks;
    }
}

void PrintSpeEventDebug()
{
    if (!g_speDebugEnabled) return;

    cout << "\n" << string(80, '=') << "\n"
         << "DEBUG SPE EVENT STAT\n"
         << string(80, '=') << "\n"
         << "Records with SPE extension (before pipeline filtering): "
         << g_speEventRecords << "\n"
         << left << setw(30) << "Event bit" << right << setw(14) << "Records" << "\n"
         << string(44, '-') << "\n";
    for (size_t i = 0; i < sizeof(SPE_EVENT_DESCRIPTORS) /
                            sizeof(SPE_EVENT_DESCRIPTORS[0]); ++i) {
        cout << left << setw(30) << SPE_EVENT_DESCRIPTORS[i].name
             << right << setw(14) << g_speEventCounts[i] << "\n";
    }
    cout << left << setw(30) << "NONE"
         << right << setw(14) << g_speEventNoneRecords << "\n"
         << left << setw(30) << "UNKNOWN_BITS"
         << right << setw(14) << g_speEventUnknownRecords << "\n";

    cout << "Observed event masks (event is a bit map; per-bit counts can overlap):\n";
    for (const auto& entry : g_speEventMaskStats) {
        ostringstream mask;
        mask << "0x" << hex << entry.first;
        cout << "SPE_EVENT_MASK mask=" << mask.str()
             << " events=" << SpeEventMaskNames(entry.first)
             << " records=" << entry.second << "\n";
    }
    if (g_speEventUnknownBits != 0 || g_droppedSpeEventMasks > 0) {
        ostringstream unknown;
        unknown << "0x" << hex << g_speEventUnknownBits;
        cout << "DEBUG SPE EVENT NOTE: unknown_bits=" << unknown.str()
             << " dropped_records_after_mask_cap=" << g_droppedSpeEventMasks << "\n";
    }
    cout << string(80, '=') << "\n";
}

static void AccountStorePcDebug(const PmuData& record, const SampleInfo& sample,
                                const string& funcName, const string& moduleName)
{
    if (!g_speDebugEnabled || !sample.isStore || sample.pc == 0) {
        return;
    }

    StorePcDebugKey key;
    key.pid = sample.pid;
    key.cpu = record.cpu;
    key.pc = sample.pc;

    auto it = g_storePcDebugStats.find(key);
    if (it == g_storePcDebugStats.end()) {
        if (g_storePcDebugStats.size() >= MAX_DEBUG_STORE_PC_KEYS) {
            ++g_droppedStorePcDebugSamples;
            return;
        }

        StorePcDebugStat stat;
        stat.function = funcName;
        stat.module = moduleName;

        uint32_t insn = 0;
        if (ReadInsn32(sample.pid, sample.pc, insn)) {
            stat.insn = insn;
            stat.hasInsn = true;
            int decoded = DecodeAccessBytes(insn);
            if (decoded > 0) {
                stat.accessBytes = decoded;
            }
        }
        it = g_storePcDebugStats.emplace(key, move(stat)).first;
    }

    ++it->second.samples;
}

void PrintSpeStorePcDebug()
{
    if (!g_speDebugEnabled) {
        return;
    }

    struct Row {
        StorePcDebugKey key;
        StorePcDebugStat stat;
    };

    vector<Row> rows;
    rows.reserve(g_storePcDebugStats.size());
    for (const auto& entry : g_storePcDebugStats) {
        rows.push_back({entry.first, entry.second});
    }
    sort(rows.begin(), rows.end(), [](const Row& lhs, const Row& rhs) {
        if (lhs.stat.samples != rhs.stat.samples) {
            return lhs.stat.samples > rhs.stat.samples;
        }
        if (lhs.key.pid != rhs.key.pid) return lhs.key.pid < rhs.key.pid;
        if (lhs.key.cpu != rhs.key.cpu) return lhs.key.cpu < rhs.key.cpu;
        return lhs.key.pc < rhs.key.pc;
    });

    cout << "\n" << string(126, '=') << "\n"
         << "DEBUG SPE STORE PC STAT\n"
         << string(126, '=') << "\n"
         << left << setw(10) << "PID"
         << setw(6) << "CPU"
         << setw(14) << "PC"
         << setw(44) << "Function"
         << setw(12) << "Samples"
         << setw(10) << "Width(B)"
         << setw(14) << "Insn"
         << "Module\n"
         << string(126, '-') << "\n";

    const size_t rowCount = min(rows.size(), MAX_DEBUG_STORE_PC_ROWS);
    for (size_t i = 0; i < rowCount; ++i) {
        const Row& row = rows[i];
        ostringstream pc;
        pc << "0x" << hex << row.key.pc;
        ostringstream insn;
        if (row.stat.hasInsn) {
            insn << "0x" << hex << setw(8) << setfill('0') << row.stat.insn;
        } else {
            insn << "-";
        }

        string function = row.stat.function;
        if (function.size() > 42) {
            function = function.substr(0, 39) + "...";
        }

        cout << left << setfill(' ')
             << setw(10) << row.key.pid
             << setw(6) << row.key.cpu
             << setw(14) << pc.str()
             << setw(44) << function
             << setw(12) << dec << row.stat.samples
             << setw(10) << row.stat.accessBytes
             << setw(14) << insn.str()
             << row.stat.module << "\n";

        // Stable machine-readable form for debug tooling.
        cout << "STORE_PC"
             << " pid=" << row.key.pid
             << " cpu=" << row.key.cpu
             << " pc=" << pc.str()
             << " samples=" << row.stat.samples
             << " width=" << row.stat.accessBytes
             << " insn=" << insn.str()
             << "\n";
    }

    if (rows.size() > rowCount || g_droppedStorePcDebugSamples > 0) {
        cout << "DEBUG STORE PC NOTE: unique_keys=" << rows.size()
             << " shown=" << rowCount
             << " dropped_samples_after_key_cap=" << g_droppedStorePcDebugSamples
             << "\n";
    }
    cout << string(126, '=') << "\n";
}

static string ProcessSymbolName(const Symbol* sym)
{
    if (sym == nullptr) {
        return UNKNOWN;
    }
    if (sym->symbolName != nullptr && strcmp(sym->symbolName, UNKNOWN) != 0) {
        return sym->symbolName;
    }

    char buf[32];
    ulong address = sym->codeMapAddr > 0 ? sym->codeMapAddr : sym->addr;
    snprintf(buf, sizeof(buf), "0x%lx", address);
    return buf;
}

static SampleInfo PrepSample(const PmuData& data)
{
    SampleInfo sample{};
    if (data.ext == nullptr) {
        return sample;
    }

    sample.hasExt = true;
    sample.source = data.ext->source;
    sample.hasSource = sample.source != numeric_limits<unsigned short>::max();
    sample.event = data.ext->event;
    bool isAtomic = (data.ext->op & SPE_OP_ATOMIC) != 0;
    sample.isLoad = (data.ext->op & SPE_OP_LD) != 0 || isAtomic;
    sample.isStore = (data.ext->op & SPE_OP_ST) != 0 || isAtomic;
    sample.pid = data.pid;
    if (data.stack != nullptr && data.stack->symbol != nullptr) {
        sample.sym = data.stack->symbol;
        sample.pc = sample.sym->codeMapAddr > 0 ? sample.sym->codeMapAddr : sample.sym->addr;
        sample.hasSym = true;
    }
    return sample;
}

static bool IsKernelSample(const SampleInfo& sample)
{
    return sample.hasSym && sample.sym != nullptr &&
           sample.sym->module != nullptr &&
           strcmp(sample.sym->module, "[kernel]") == 0;
}

void ProcessSpeChunk(PmuData* data, int len, BwContext& ctx,
                     const vector<int>& targetPids, SpeWindowState& windows,
                     int64_t readEndNs)
{
    if (data == nullptr || len <= 0) {
        return;
    }

    // Full system-wide SPE (user + kernel) supplies the denominator. Kernel-PC
    // records are retained only in a system-level bucket; this set selects the
    // target user-space numerators.
    static thread_local vector<int> cachedTargetPids;
    static thread_local unordered_set<int> targetPidSet;
    if (cachedTargetPids != targetPids) {
        cachedTargetPids = targetPids;
        targetPidSet.clear();
        targetPidSet.reserve(targetPids.size() * 2 + 1);
        targetPidSet.insert(targetPids.begin(), targetPids.end());
    }

    SpeDecodeStats* decode = g_speDebugEnabled ? &ctx.speDecode : nullptr;

    for (int i = 0; i < len; ++i) {
        const auto& record = data[i];
        if (decode != nullptr) ++decode->recordsSeen;
        if (record.ext != nullptr) AccountSpeEventDebug(record.ext->event);
        if (readEndNs > 0 && record.ts > readEndNs + 1000000LL) {
            if (decode != nullptr) ++decode->afterRead;
            ++ctx.speTimestamps.afterRead;
            continue;
        }

        SampleInfo sample = PrepSample(record);
        if (!sample.hasExt) {
            if (decode != nullptr) ++decode->missingExt;
            continue;
        }
        if (!sample.isLoad && !sample.isStore) {
            if (decode != nullptr) ++decode->nonLoadStore;
            continue;
        }
        const bool useEventSourceFallback = sample.isLoad && !sample.hasSource;
        if (useEventSourceFallback && !ctx.speSourceFallbackWarningPrinted) {
            cerr << "Warning: SPE data source is unavailable; using SPE_EV_LLC_MISS "
                    "as an approximate DRAM-load fallback. Read attribution may be less accurate.\n";
            ctx.speSourceFallbackWarningPrinted = true;
        }
        bool accountRead = sample.isLoad && (sample.hasSource
            ? IsDramSource(sample.source)
            : IsDramEventFallback(sample.event));
        bool accountWrite = sample.isStore;
        const bool isTarget = targetPidSet.find(record.pid) != targetPidSet.end();
        const bool isKernel = IsKernelSample(sample);
        const bool isTargetUser = isTarget && !isKernel;
        if (decode != nullptr) {
            if (isKernel) {
                ++decode->kernelRecords;
            } else if (isTargetUser) {
                ++decode->targetUserRecords;
            } else {
                ++decode->otherUserRecords;
            }
            if (useEventSourceFallback) ++decode->sourceUnavailableLoads;
            if (useEventSourceFallback && accountRead) {
                ++decode->eventFallbackDramLoads;
            }
            if (sample.isLoad && !accountRead) ++decode->nonDramLoads;
            if (accountRead) ++decode->eligibleDramLoads;
            if (accountWrite) ++decode->eligibleStores;
        }
        string funcName = UNKNOWN;
        string moduleName = UNKNOWN;
        if (isTargetUser && sample.hasSym) {
            funcName = ProcessSymbolName(sample.sym);
            if (sample.sym->module != nullptr) {
                moduleName = sample.sym->module;
            }
        }
        if (isTargetUser && sample.isLoad && !accountRead) {
            ++ctx.spe.filteredSamples;
        }
        if (!accountRead && !accountWrite) {
            continue;
        }

        string funcKey;
        if (isTargetUser) {
            funcKey = to_string(sample.pid) + ':' + funcName + "@" + moduleName;
        }
        SpeContribution contribution;
        contribution.timestampNs = record.ts;
        contribution.target = isTargetUser;
        contribution.kernel = isKernel;
        contribution.read = accountRead;
        contribution.write = accountWrite;
        contribution.funcKey = funcKey;
        if (!BucketSpeContribution(ctx, windows, contribution)) {
            if (decode != nullptr) ++decode->bucketRejected;
            continue;
        }
        if (decode != nullptr) {
            ++decode->bucketedRecords;
            if (isTargetUser) ++decode->targetBucketedRecords;
        }
        if (accountRead) ++ctx.spe.systemReadSamples;
        if (accountWrite) ++ctx.spe.systemWriteSamples;
        if (isKernel) {
            if (accountRead) ++ctx.spe.kernelReadSamples;
            if (accountWrite) ++ctx.spe.kernelWriteSamples;
            continue;
        }
        if (!isTargetUser) {
            continue;
        }

        AccountStorePcDebug(record, sample, funcName, moduleName);

        if (accountRead) {
            ++ctx.spe.readWeight;
            ++ctx.spe.readSamples;
        }
        if (accountWrite) {
            ++ctx.spe.writeWeight;
            ++ctx.spe.writeSamples;
        }

        auto& stat = ctx.funcStats[funcKey];
        if (stat.funcName.empty()) {
            stat.funcName = funcName;
            stat.moduleName = moduleName;
            stat.funcKey = funcKey;
            stat.pid = sample.pid;
        }

        if (accountRead) {
            // Physical DDR read attribution uses DRAM-source load sample counts,
            // not the logical load operand width.
            ++stat.readWeight;
            ++stat.readSamples;
        }

        if (accountWrite) {
            // Physical DDR write attribution uses store sample counts only.
            // Operand width is intentionally not part of the production weight:
            // a small store can dirty a full cache line and later cause a line
            // writeback, and SPE store sampling density is opcode-dependent.
            ++stat.writeWeight;
            ++stat.writeSamples;
        }
    }
}

bool ReadDdrcInterval(int pd, PmuDeviceAttr* attrs, unsigned attrLen, double& readBytes, double& writeBytes)
{
    PmuData* rawData = nullptr;
    int rawLen = PmuRead(pd, &rawData);
    if (rawLen <= 0 || rawData == nullptr || Perrorno() != 0) {
        if (rawData != nullptr) {
            PmuDataFree(rawData);
        }
        return false;
    }

    PmuDeviceData* metricData = nullptr;
    int metricLen = PmuGetDevMetric(rawData, rawLen, attrs, attrLen, &metricData);
    if (metricLen <= 0 || metricData == nullptr) {
        if (metricData != nullptr) DevDataFree(metricData);
        PmuDataFree(rawData);
        return false;
    }

    bool hasRead = false;
    bool hasWrite = false;
    for (int i = 0; i < metricLen; ++i) {
        if (metricData[i].metric == PMU_DDR_READ_BW) {
            hasRead = true;
            readBytes += metricData[i].count;
        } else if (metricData[i].metric == PMU_DDR_WRITE_BW) {
            hasWrite = true;
            writeBytes += metricData[i].count;
        }
    }

    DevDataFree(metricData);
    PmuDataFree(rawData);
    return hasRead && hasWrite;
}

static bool BdfToConfigValue(const string& bdf, string& configValue)
{
    unsigned bus = 0;
    unsigned device = 0;
    unsigned function = 0;
    char trailing = 0;
    if (sscanf(bdf.c_str(), "%x:%x.%x%c", &bus, &device, &function, &trailing) != 3 ||
        bus > 0xff || device > 0x1f || function > 0x7) {
        return false;
    }

    unsigned encoded = (bus << 8) | (device << 3) | function;
    ostringstream value;
    value << "0x" << hex << encoded;
    configValue = value.str();
    return true;
}

bool IsPciBridge(const string& bdf)
{
    string path = "/sys/bus/pci/devices/0000:" + bdf + "/class";
    ifstream file(path);
    if (!file.is_open()) {
        // Unknown class: keep the BDF rather than dropping a potentially valid endpoint.
        return false;
    }

    string classText;
    file >> classText;
    if (classText.empty()) {
        return false;
    }
    try {
        unsigned long pciClass = stoul(classText, nullptr, 0);
        unsigned baseClass = static_cast<unsigned>((pciClass >> 16) & 0xffUL);
        return baseClass == 0x06;
    } catch (...) {
        return false;
    }
}


static string CurrentPmuError(const char* operation)
{
    int errorNo = Perrorno();
    const char* errorMessage = Perror();
    ostringstream error;
    error << operation << " failed: err=" << errorNo
          << " msg=" << (errorMessage == nullptr ? "<none>" : errorMessage);
    return error.str();
}

bool PrimePmuRead(int pd, double& readSec, string& error)
{
    PmuData* data = nullptr;
    int len = PmuRead(pd, &data);
    if (len < 0 || Perrorno() != 0) {
        error = CurrentPmuError("baseline PmuRead");
        if (data != nullptr) {
            PmuDataFree(data);
        }
        return false;
    }
    if (data != nullptr) {
        PmuDataFree(data);
    }
    readSec = NowSec();
    return true;
}

static bool ResolvePciePmuName(const string& bdf, string& pmuName, string& error)
{
    PmuDeviceAttr probeAttr = {};
    probeAttr.metric = PMU_PCIE_RX_MRD_BW;
    probeAttr.bdf = const_cast<char*>(bdf.c_str());

    int probePd = PmuDeviceOpen(&probeAttr, 1);
    if (probePd == -1) {
        error = CurrentPmuError("PmuDeviceOpen PCIe PMU probe");
        return false;
    }
    if (PmuEnable(probePd) != 0) {
        error = CurrentPmuError("PmuEnable PCIe PMU probe");
        PmuClose(probePd);
        return false;
    }
    if (PmuDisable(probePd) != 0) {
        error = CurrentPmuError("PmuDisable PCIe PMU probe");
        PmuClose(probePd);
        return false;
    }

    PmuData* rawData = nullptr;
    int rawLen = PmuRead(probePd, &rawData);
    if (rawLen < 0) {
        error = CurrentPmuError("PmuRead PCIe PMU probe");
        if (rawData != nullptr) {
            PmuDataFree(rawData);
        }
        PmuClose(probePd);
        return false;
    }
    for (int i = 0; rawData != nullptr && i < rawLen; ++i) {
        if (rawData[i].evt == nullptr) {
            continue;
        }
        string eventName = rawData[i].evt;
        size_t slash = eventName.find('/');
        if (slash != string::npos && eventName.rfind("hisi_pcie", 0) == 0) {
            pmuName = eventName.substr(0, slash);
            break;
        }
    }
    if (rawData != nullptr) {
        PmuDataFree(rawData);
    }
    PmuClose(probePd);
    if (pmuName.empty()) {
        error = "PCIe PMU probe returned no hisi_pcie event name";
        return false;
    }
    return true;
}

bool OpenPcieCollector(const string& bdf, PcieCollector& collector,
                              string& error)
{
    string pmuName;
    string bdfValue;
    if (!BdfToConfigValue(bdf, bdfValue)) {
        error = "invalid BDF; expected bb:dd.f";
        return false;
    }
    if (!ResolvePciePmuName(bdf, pmuName, error)) {
        return false;
    }

    //   TX CPL -> device DMA Read from host memory
    //   RX MWr -> device DMA Write to host memory
    const vector<string> eventNames = {
        pmuName + "/rx_mwr_flux,bdf=" + bdfValue + ",len_mode=1/",
        pmuName + "/tx_cpl_flux,bdf=" + bdfValue + ",len_mode=1/"
    };

    collector.eventStorage.reserve(eventNames.size());
    collector.eventList.reserve(eventNames.size());
    for (const auto& eventName : eventNames) {
        auto buffer = make_unique<char[]>(eventName.size() + 1);
        strcpy(buffer.get(), eventName.c_str());
        collector.eventList.push_back(buffer.get());
        collector.eventStorage.emplace_back(move(buffer));
    }

    PmuAttr attr = {};
    attr.evtList = collector.eventList.data();
    attr.numEvt = collector.eventList.size();
    attr.pidList = nullptr;
    attr.numPid = 0;
    attr.cpuList = nullptr;
    attr.numCpu = 0;

    int pd = PmuOpen(COUNTING, &attr);
    if (pd == -1) {
        ostringstream details;
        details << CurrentPmuError("PmuOpen(COUNTING)");
        for (size_t i = 0; i < eventNames.size(); ++i) {
            details << "\n  event[" << i << "]=" << eventNames[i];
        }
        error = details.str();
        return false;
    }

    collector.pd = pd;
    collector.bdf = bdf;
    return true;
}


struct PcieIntervalSample {
    PcieCollector* collector = nullptr;
    PcieBytes bytes;
};

PcieReadResult ReadPcieInterval(vector<PcieCollector>& collectors,
                                BwContext& ctx, PcieBytes& total,
                                double stoppedSec)
{
    total = {};
    PcieReadResult result;
    vector<PcieIntervalSample> staged;
    staged.reserve(collectors.size());

    for (auto& collector : collectors) {
        if (collector.pd == -1) {
            continue;
        }
        ++result.attempted;

        PmuData* rawData = nullptr;
        int rawLen = PmuRead(collector.pd, &rawData);
        if (rawLen <= 0 || rawData == nullptr || Perrorno() != 0) {
            if (rawData != nullptr) {
                PmuDataFree(rawData);
            }
            collector.synchronized = false;
            ++result.failed;
            result.incompleteBdfs.push_back(collector.bdf);
            continue;
        }

        const double readSec = stoppedSec > 0.0 ? stoppedSec : NowSec();
        PcieIntervalSample sample;
        sample.collector = &collector;
        if (collector.lastReadSec > 0.0 && readSec > collector.lastReadSec) {
            sample.bytes.elapsedSec = readSec - collector.lastReadSec;
        }
        collector.lastReadSec = readSec;

        bool sawRxMwr = false;
        bool sawTxCpl = false;
        for (int i = 0; i < rawLen; ++i) {
            if (rawData[i].evt == nullptr) {
                continue;
            }
            if (rawData[i].countPercent > 0.0) {
                sample.bytes.minRunningRatio = sample.bytes.hasRunningRatio
                    ? min(sample.bytes.minRunningRatio, rawData[i].countPercent)
                    : rawData[i].countPercent;
                sample.bytes.hasRunningRatio = true;
            }
            string eventName = rawData[i].evt;
            double bytes = static_cast<double>(rawData[i].count) * 4.0;
            if (eventName.find("rx_mwr_flux") != string::npos) {
                sample.bytes.rxMwr += bytes;
                sawRxMwr = true;
            } else if (eventName.find("tx_cpl_flux") != string::npos) {
                sample.bytes.txCpl += bytes;
                sawTxCpl = true;
            }
        }
        PmuDataFree(rawData);

        // A successful read after a failed one establishes a new baseline. Its
        // counts may span multiple DDRC windows, so discard it for attribution.
        if (!collector.synchronized) {
            collector.synchronized = true;
            ++result.resynchronized;
            result.incompleteBdfs.push_back(collector.bdf);
            continue;
        }

        if (!sawRxMwr || !sawTxCpl) {
            ++result.failed;
            result.incompleteBdfs.push_back(collector.bdf);
            continue;
        }

        ++result.succeeded;
        staged.emplace_back(move(sample));
    }

    // Commit only an all-collector, same-window snapshot. Partial data must not
    // be used for DMA deduction because it would inflate the CPU residual.
    if (!result.Complete()) {
        return result;
    }

    for (const auto& sample : staged) {
        PcieBytes& device = ctx.traffic.pcieByBdf[sample.collector->bdf];
        device.rxMwr += sample.bytes.rxMwr;
        device.txCpl += sample.bytes.txCpl;
        device.elapsedSec += sample.bytes.elapsedSec;
        if (sample.bytes.hasRunningRatio) {
            device.minRunningRatio = device.hasRunningRatio
                ? min(device.minRunningRatio, sample.bytes.minRunningRatio)
                : sample.bytes.minRunningRatio;
            device.hasRunningRatio = true;
        }
        total.rxMwr += sample.bytes.rxMwr;
        total.txCpl += sample.bytes.txCpl;
    }
    return result;
}
