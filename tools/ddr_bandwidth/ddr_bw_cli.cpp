#include <algorithm>
#include <cctype>
#include <cerrno>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "ddr_bw_cli.h"
#include "ddr_bw_collection.h"
using namespace std;

static constexpr double BYTES_PER_MIB_LOCAL = 1024.0 * 1024.0;
static constexpr double OPS_PER_MILLION = 1000.0 * 1000.0;
static constexpr char TABLE_COLUMN_GAP[] = "  ";
static constexpr int TABLE_COLUMN_GAP_WIDTH = sizeof(TABLE_COLUMN_GAP) - 1;
static constexpr int RATE_CELL_WIDTH = 14;
static const char* SHORT_OPS = "ap:d:i:h";
static const struct option LONG_OPS[] = {
    {"all", no_argument, nullptr, 'a'},
    {"pid", required_argument, nullptr, 'p'},
    {"duration", required_argument, nullptr, 'd'},
    {"interval", required_argument, nullptr, 'i'},
    {"period", required_argument, nullptr, 1001},
    {"min-latency", required_argument, nullptr, 1002},
    {"top", required_argument, nullptr, 1003},
    {"pcie-bdf", required_argument, nullptr, 1004},
    {"debug", no_argument, nullptr, 1005},
    {"spe-read-interval", required_argument, nullptr, 1006},
    {"min-samples", required_argument, nullptr, 1007},
    {"help", no_argument, nullptr, 'h'},
    {nullptr, 0, nullptr, 0}
};

static bool ParseUnsigned(const char* arg, const char* name, unsigned long& out,
                          unsigned long maximum = std::numeric_limits<unsigned long>::max())
{
    if (arg == nullptr || *arg == '\0') {
        cerr << "Error: parameter '" << name << "' is empty.\n";
        return false;
    }
    for (const char* p = arg; *p; ++p) {
        if (!isdigit(static_cast<unsigned char>(*p))) {
            cerr << "Error: parameter '" << name
                 << "' must be a non-negative integer, but got '" << arg << "'.\n";
            return false;
        }
    }
    char* end = nullptr;
    errno = 0;
    unsigned long val = strtoul(arg, &end, 10);
    if (errno == ERANGE || val > maximum || end == arg || *end != '\0') {
        cerr << "Error: parameter '" << name << "' is invalid: '" << arg << "'.\n";
        return false;
    }
    out = val;
    return true;
}

static bool ParsePositiveUnsigned(
    const char* arg, const char* name, unsigned long& out,
    unsigned long maximum = std::numeric_limits<unsigned long>::max())
{
    if (!ParseUnsigned(arg, name, out, maximum)) {
        return false;
    }
    if (out == 0) {
        cerr << "Error: parameter '" << name << "' must be greater than zero.\n";
        return false;
    }
    return true;
}

static bool AppendPidList(const char* arg, vector<int>& pids)
{
    if (arg == nullptr || *arg == '\0') {
        cerr << "Error: empty PID list.\n";
        return false;
    }

    string text(arg);
    size_t begin = 0;
    while (begin < text.size()) {
        size_t endPos = text.find(',', begin);
        string token = endPos == string::npos
            ? text.substr(begin) : text.substr(begin, endPos - begin);
        if (token.empty()) {
            cerr << "Error: invalid PID list '" << text << "'.\n";
            return false;
        }

        char* tail = nullptr;
        errno = 0;
        long value = strtol(token.c_str(), &tail, 10);
        if (errno == ERANGE || value > numeric_limits<int>::max() ||
            tail == token.c_str() || *tail != '\0' || value <= 0) {
            cerr << "Error: invalid pid '" << token << "'.\n";
            return false;
        }
        int pid = static_cast<int>(value);
        if (find(pids.begin(), pids.end(), pid) == pids.end()) {
            pids.push_back(pid);
        }

        if (endPos == string::npos) break;
        begin = endPos + 1;
        if (begin == text.size()) return false;
    }
    return !pids.empty();
}

void PrintHelp()
{
    cerr <<
        "Usage: ddr_bandwidth [options] [-a | -p <pid[,pid...]> ...]\n"
        "       ddr_bandwidth [options] -- <command> [args...]\n\n"
        "Attribute physical DDR bandwidth with DDRC + SPE + PCIe PMUs.\n"
        "Options:\n"
        "  -a, --all                 Attribute all user-space processes (default without -p)\n"
        "  -p, --pid <pid[,pid...]> Target process ID(s); repeatable\n"
        "  -d, --duration <sec>      Collection duration in seconds (default: 10)\n"
        "  -i, --interval <ms>       Attribution window in ms (default: 100)\n"
        "      --spe-read-interval <ms> SPE drain interval (default: 50; capped by window)\n"
        "      --min-samples <n>     Minimum target-user samples per direction (default: 1)\n"
        "      --period <n>          SPE sampling period (default: 2048; not milliseconds)\n"
        "      --min-latency <n>     Minimum latency filter in cycles\n"
        "      --top <n>             Show top N functions (default: 20)\n"
        "      --pcie-bdf <bdf>      Collect only this PCIe BDF (repeatable; default: all)\n"
        "      --debug               Print aggregate timing, SPE pipeline and health diagnostics\n"
        "  -h, --help                Show this help\n\n"
        "Examples:\n"
        "  ddr_bandwidth -a -d 10\n"
        "  ddr_bandwidth -p 12345 -d 10\n"
        "  ddr_bandwidth -p 12345,12346 --debug -d 10\n"
        "  ddr_bandwidth -p 12345 -p 12346 -d 10\n"
        "  ddr_bandwidth -d 5 -- ./my_app --arg1\n";
}

bool ParseArgs(int argc, char** argv, ArgsContext& act)
{
    int opt;
    int longIndex = 0;
    while ((opt = getopt_long(argc, argv, SHORT_OPS, LONG_OPS, &longIndex)) != -1) {
        switch (opt) {
            case 'a':
                act.systemWide = true;
                break;
            case 'p':
                if (!AppendPidList(optarg, act.pids)) return false;
                break;
            case 'd': {
                unsigned long val = 0;
                if (!ParsePositiveUnsigned(optarg, "duration", val, numeric_limits<int>::max())) return false;
                act.duration = static_cast<int>(val);
                break;
            }
            case 'i': {
                unsigned long val = 0;
                if (!ParsePositiveUnsigned(optarg, "interval", val, numeric_limits<int>::max())) return false;
                act.interval = static_cast<int>(val);
                break;
            }
            case 1001: {
                unsigned long val = 0;
                if (!ParsePositiveUnsigned(optarg, "period", val, numeric_limits<unsigned>::max())) return false;
                act.period = static_cast<unsigned>(val);
                break;
            }
            case 1002: {
                unsigned long val = 0;
                if (!ParseUnsigned(optarg, "min-latency", val)) return false;
                act.minLatency = val;
                break;
            }
            case 1003: {
                unsigned long val = 0;
                if (!ParsePositiveUnsigned(optarg, "top", val, numeric_limits<int>::max())) return false;
                act.topN = static_cast<int>(val);
                break;
            }
            case 1004:
                if (optarg == nullptr || *optarg == '\0') {
                    cerr << "Error: --pcie-bdf requires a non-empty BDF.\n";
                    return false;
                }
                act.pcieBdfs.emplace_back(optarg);
                break;
            case 1005:
                act.debug = true;
                break;
            case 1006: {
                unsigned long val = 0;
                if (!ParsePositiveUnsigned(optarg, "spe-read-interval", val,
                                           numeric_limits<int>::max())) return false;
                act.speReadInterval = static_cast<int>(val);
                break;
            }
            case 1007: {
                unsigned long val = 0;
                if (!ParsePositiveUnsigned(optarg, "min-samples", val)) return false;
                act.minSamples = val;
                break;
            }
            case 'h':
                PrintHelp();
                return false;
            default:
                PrintHelp();
                return false;
        }
    }

    if (act.systemWide && !act.pids.empty()) {
        cerr << "Error: cannot combine -a/--all with -p/--pid.\n";
        return false;
    }

    if ((!act.pids.empty() || act.systemWide) && optind < argc) {
        cerr << "Error: cannot combine PID/system-wide selection with a launch command.\n";
        return false;
    }

    if (act.pids.empty() && !act.systemWide && optind < argc) {
        for (int i = optind; i < argc; ++i) act.command.push_back(argv[i]);
        act.isLaunch = true;
    }

    if (act.pids.empty() && !act.isLaunch) {
        act.systemWide = true;
    }

    if (!act.pids.empty()) {
        // Keep the legacy primary-PID field valid for code paths that need one PID.
        act.pid = act.pids.front();
    }
    return true;
}

static bool ValidateTargetPids(const vector<int>& pids)
{
    for (int pid : pids) {
        errno = 0;
        if (kill(pid, 0) == 0 || errno == EPERM) {
            continue;
        }
        if (errno == ESRCH) {
            cerr << "Error: target pid " << pid << " does not exist.\n";
        } else {
            cerr << "Error: unable to validate target pid " << pid
                 << ": " << strerror(errno) << "\n";
        }
        return false;
    }
    return true;
}

static string ShortenLeft(const string& value, size_t width)
{
    if (value.size() <= width) return value;
    return value.substr(0, width - 3) + "...";
}

static string ShortenRight(const string& value, size_t width)
{
    if (value.size() <= width) return value;
    return "..." + value.substr(value.size() - (width - 3));
}

static void PrintRateCell(double rate, bool calibrated)
{
    if (rate <= 0.0) {
        cout << setw(RATE_CELL_WIDTH) << "-";
    } else {
        cout << setw(RATE_CELL_WIDTH) << rate /
            (calibrated ? BYTES_PER_MIB_LOCAL : OPS_PER_MILLION);
    }
}

static void PrintFunctionTable(BwContext& ctx, bool calibrated,
                               double durationSec, const ArgsContext& act)
{
    vector<FuncBwStat> stats;
    stats.reserve(ctx.funcStats.size());
    for (const auto& entry : ctx.funcStats) {
        FuncBwStat stat = entry.second;
        if (durationSec > 0.0) {
            if (calibrated) {
                stat.readRate = stat.attributedReadBytes / durationSec;
                stat.writeRate = stat.attributedWriteBytes / durationSec;
            } else {
                stat.readRate = static_cast<double>(stat.readWeight) *
                    act.period / durationSec;
                stat.writeRate = static_cast<double>(stat.writeWeight) *
                    act.period / durationSec;
            }
        }
        stat.totalRate = stat.readRate + stat.writeRate;
        stats.push_back(move(stat));
    }

    sort(stats.begin(), stats.end(), [](const FuncBwStat& lhs, const FuncBwStat& rhs) {
        if (lhs.totalRate != rhs.totalRate) return lhs.totalRate > rhs.totalRate;
        const uint64_t lhsSamples = lhs.readSamples + lhs.writeSamples;
        const uint64_t rhsSamples = rhs.readSamples + rhs.writeSamples;
        if (lhsSamples != rhsSamples) return lhsSamples > rhsSamples;
        return lhs.funcKey < rhs.funcKey;
    });

    constexpr int FUNC_WIDTH = 72;
    constexpr int MODULE_WIDTH = 45;
    constexpr int PID_WIDTH = 12;
    constexpr int COLUMN_COUNT = 8;
    const int tableWidth = PID_WIDTH + FUNC_WIDTH + MODULE_WIDTH +
        RATE_CELL_WIDTH * 5 + TABLE_COLUMN_GAP_WIDTH * (COLUMN_COUNT - 1);

    cout << string(tableWidth, '=') << "\n"
         << (calibrated
                ? (act.systemWide ? "SYSTEM-WIDE CPU DDR BANDWIDTH BY USER FUNCTION"
                                  : "CPU DDR BANDWIDTH BY USER FUNCTION")
                : (act.systemWide ? "SYSTEM-WIDE SPE MEMORY OPERATION ESTIMATES BY USER FUNCTION"
                                  : "SPE MEMORY OPERATION ESTIMATES BY USER FUNCTION")) << "\n"
         << string(tableWidth, '=') << "\n"
         << left << setw(PID_WIDTH) << "PID"
         << TABLE_COLUMN_GAP << setw(FUNC_WIDTH) << "Function"
         << TABLE_COLUMN_GAP << setw(MODULE_WIDTH) << "Module"
         << right << TABLE_COLUMN_GAP << setw(RATE_CELL_WIDTH)
         << (calibrated ? "Read BW" : "Read loads")
         << TABLE_COLUMN_GAP << setw(RATE_CELL_WIDTH)
         << (calibrated ? "Write BW(est*)" : "Stores(est*)")
         << TABLE_COLUMN_GAP << setw(RATE_CELL_WIDTH)
         << (calibrated ? "Total BW" : "Total(est*)")
         << TABLE_COLUMN_GAP << setw(RATE_CELL_WIDTH) << "Read samples"
         << TABLE_COLUMN_GAP << setw(RATE_CELL_WIDTH) << "Store samples" << "\n"
         << left << setw(PID_WIDTH) << ""
         << TABLE_COLUMN_GAP << setw(FUNC_WIDTH) << ""
         << TABLE_COLUMN_GAP << setw(MODULE_WIDTH) << ""
         << right << TABLE_COLUMN_GAP << setw(RATE_CELL_WIDTH)
         << (calibrated ? "(MiB/s)" : "(Mops/s)")
         << TABLE_COLUMN_GAP << setw(RATE_CELL_WIDTH)
         << (calibrated ? "(MiB/s)" : "(Mops/s)")
         << TABLE_COLUMN_GAP << setw(RATE_CELL_WIDTH)
         << (calibrated ? "(MiB/s)" : "(Mops/s)")
         << TABLE_COLUMN_GAP << setw(RATE_CELL_WIDTH) << ""
         << TABLE_COLUMN_GAP << setw(RATE_CELL_WIDTH) << "" << "\n"
         << string(tableWidth, '-') << "\n";

    int shown = 0;
    for (const auto& stat : stats) {
        if (shown >= act.topN) break;
        if (stat.totalRate <= 0.0 && stat.readSamples == 0 && stat.writeSamples == 0) continue;

        cout << left << setw(PID_WIDTH) << stat.pid
             << TABLE_COLUMN_GAP << setw(FUNC_WIDTH)
             << ShortenLeft(stat.funcName, FUNC_WIDTH - 2)
             << TABLE_COLUMN_GAP << setw(MODULE_WIDTH)
             << ShortenRight(stat.moduleName, MODULE_WIDTH - 2)
             << right << fixed << setprecision(2);
        cout << TABLE_COLUMN_GAP;
        PrintRateCell(stat.readRate, calibrated);
        cout << TABLE_COLUMN_GAP;
        PrintRateCell(stat.writeRate, calibrated);
        cout << TABLE_COLUMN_GAP << setw(RATE_CELL_WIDTH) << stat.totalRate /
                (calibrated ? BYTES_PER_MIB_LOCAL : OPS_PER_MILLION)
             << TABLE_COLUMN_GAP << setw(RATE_CELL_WIDTH) << stat.readSamples
             << TABLE_COLUMN_GAP << setw(RATE_CELL_WIDTH) << stat.writeSamples << "\n";
        ++shown;
    }
    if (shown == 0) cout << "No eligible SPE load/store samples.\n";
    cout << string(tableWidth, '-') << "\n";
}

struct ProcessBwStat {
    int pid = -1;
    double readBytes = 0.0;
    double writeBytes = 0.0;
    uint64_t readWeight = 0;
    uint64_t writeWeight = 0;
    uint64_t samples = 0;
};

static void PrintProcessTable(const BwContext& ctx, bool calibrated,
                              double durationSec, const ArgsContext& act)
{
    if (!act.systemWide && act.pids.size() <= 1) return;

    map<int, ProcessBwStat> processes;
    for (const auto& entry : ctx.funcStats) {
        const FuncBwStat& stat = entry.second;
        ProcessBwStat& process = processes[stat.pid];
        process.pid = stat.pid;
        process.readBytes += stat.attributedReadBytes;
        process.writeBytes += stat.attributedWriteBytes;
        process.readWeight += stat.readWeight;
        process.writeWeight += stat.writeWeight;
        process.samples += stat.readSamples + stat.writeSamples;
    }

    constexpr int PID_WIDTH = 12;
    constexpr int RATE_WIDTH = 16;
    constexpr int SAMPLE_WIDTH = 14;
    constexpr int COLUMN_COUNT = 5;
    constexpr int TABLE_WIDTH = PID_WIDTH + RATE_WIDTH * 3 + SAMPLE_WIDTH +
        TABLE_COLUMN_GAP_WIDTH * (COLUMN_COUNT - 1);

    cout << "\n" << string(TABLE_WIDTH, '=') << "\n"
         << (calibrated ? "CPU DDR BANDWIDTH BY PROCESS (USER-SPACE)"
                        : "SPE MEMORY OPERATION ESTIMATES BY PROCESS (USER-SPACE)") << "\n"
         << string(TABLE_WIDTH, '=') << "\n"
         << left << setw(PID_WIDTH) << "PID"
         << right << TABLE_COLUMN_GAP << setw(RATE_WIDTH)
         << (calibrated ? "Read BW" : "Read loads")
         << TABLE_COLUMN_GAP << setw(RATE_WIDTH)
         << (calibrated ? "Write BW(est*)" : "Stores(est*)")
         << TABLE_COLUMN_GAP << setw(RATE_WIDTH)
         << (calibrated ? "Total BW" : "Total(est*)")
         << TABLE_COLUMN_GAP << setw(SAMPLE_WIDTH) << "Samples" << "\n"
         << left << setw(PID_WIDTH) << ""
         << right << TABLE_COLUMN_GAP << setw(RATE_WIDTH)
         << (calibrated ? "(MiB/s)" : "(Mops/s)")
         << TABLE_COLUMN_GAP << setw(RATE_WIDTH)
         << (calibrated ? "(MiB/s)" : "(Mops/s)")
         << TABLE_COLUMN_GAP << setw(RATE_WIDTH)
         << (calibrated ? "(MiB/s)" : "(Mops/s)")
         << TABLE_COLUMN_GAP << setw(SAMPLE_WIDTH) << "" << "\n"
         << string(TABLE_WIDTH, '-') << "\n";

    vector<ProcessBwStat> rows;
    rows.reserve(processes.size());
    for (const auto& entry : processes) rows.push_back(entry.second);
    sort(rows.begin(), rows.end(), [calibrated](const ProcessBwStat& lhs,
                                                const ProcessBwStat& rhs) {
        const double lhsScore = calibrated
            ? lhs.readBytes + lhs.writeBytes
            : static_cast<double>(lhs.readWeight + lhs.writeWeight);
        const double rhsScore = calibrated
            ? rhs.readBytes + rhs.writeBytes
            : static_cast<double>(rhs.readWeight + rhs.writeWeight);
        if (lhsScore != rhsScore) return lhsScore > rhsScore;
        return lhs.pid < rhs.pid;
    });

    const size_t rowLimit = act.systemWide
        ? min(rows.size(), static_cast<size_t>(act.topN)) : rows.size();
    for (size_t i = 0; i < rowLimit; ++i) {
        const ProcessBwStat& p = rows[i];
        double readRate = 0.0;
        double writeRate = 0.0;
        if (durationSec > 0.0) {
            if (calibrated) {
                readRate = p.readBytes / durationSec;
                writeRate = p.writeBytes / durationSec;
            } else {
                readRate = static_cast<double>(p.readWeight) *
                    act.period / durationSec;
                writeRate = static_cast<double>(p.writeWeight) *
                    act.period / durationSec;
            }
        }

        const double unitScale = calibrated ? BYTES_PER_MIB_LOCAL : OPS_PER_MILLION;
        cout << left << setw(PID_WIDTH) << p.pid
             << right << fixed << setprecision(2)
             << TABLE_COLUMN_GAP << setw(RATE_WIDTH) << readRate / unitScale
             << TABLE_COLUMN_GAP << setw(RATE_WIDTH) << writeRate / unitScale
             << TABLE_COLUMN_GAP << setw(RATE_WIDTH) << (readRate + writeRate) / unitScale
             << TABLE_COLUMN_GAP << setw(SAMPLE_WIDTH) << p.samples << "\n";
    }
    cout << string(TABLE_WIDTH, '-') << "\n";
}

static void PrintPcieTable(const BwContext& ctx, double durationSec)
{
    if (ctx.traffic.pcieByBdf.empty()) return;

    cout << "\n" << string(68, '=') << "\nPCIe DMA BY DEVICE\n"
         << string(68, '=') << "\n"
         << left << setw(20) << "BDF"
         << right << setw(16) << "DMA Read"
         << setw(16) << "DMA Write"
         << setw(16) << "Total" << "\n"
         << left << setw(20) << ""
         << right << setw(16) << "(MiB/s)"
         << setw(16) << "(MiB/s)"
         << setw(16) << "(MiB/s)" << "\n"
         << string(68, '-') << "\n";

    for (const auto& entry : ctx.traffic.pcieByBdf) {
        const PcieBytes& bytes = entry.second;
        double sec = bytes.elapsedSec > 0.0 ? bytes.elapsedSec : durationSec;
        double read = sec > 0.0 ? bytes.txCpl / sec / BYTES_PER_MIB_LOCAL : 0.0;
        double write = sec > 0.0 ? bytes.rxMwr / sec / BYTES_PER_MIB_LOCAL : 0.0;
        cout << left << setw(20) << entry.first << right << fixed << setprecision(2)
             << setw(16) << read << setw(16) << write << setw(16) << read + write << "\n";
    }
    cout << string(68, '-') << "\n";
}

void PrintSummary(BwContext& ctx, bool ddrcOpened, bool pcieOpened,
                  const CollectionTiming& timing, const ArgsContext& act)
{
    const double durationSec = timing.durationSec;
    const bool calibrated = any_of(
        ctx.windows.begin(), ctx.windows.end(),
        [](const WindowRecord& window) { return window.ddrcValid; });
    const double calibratedDurationSec = ctx.traffic.ddrcElapsedSec > 0.0
        ? ctx.traffic.ddrcElapsedSec : durationSec;

    PrintFunctionTable(ctx, calibrated, calibratedDurationSec, act);
    PrintProcessTable(ctx, calibrated, calibratedDurationSec, act);

    cout << fixed << setprecision(2);
    if (calibrated) {
        const double residualRead = max(0.0,
            ctx.traffic.ddrcReadBytes - ctx.traffic.deductedDmaReadBytes);
        const double residualWrite = max(0.0,
            ctx.traffic.ddrcWriteBytes - ctx.traffic.deductedDmaWriteBytes);

        cout << "\n" << string(80, '=') << "\nDDR BANDWIDTH SUMMARY\n"
             << string(80, '=') << "\n"
             << left << setw(38) << "Metric"
             << right << setw(14) << "Read"
             << setw(14) << "Write"
             << setw(14) << "Total" << "\n"
             << left << setw(38) << ""
             << right << setw(14) << "(MiB/s)"
             << setw(14) << "(MiB/s)"
             << setw(14) << "(MiB/s)" << "\n"
             << string(80, '-') << "\n";

        auto printBw = [calibratedDurationSec](const char* name, double read, double write) {
            cout << left << setw(38) << name << right << fixed << setprecision(2)
                 << setw(14) << read / calibratedDurationSec / BYTES_PER_MIB_LOCAL
                 << setw(14) << write / calibratedDurationSec / BYTES_PER_MIB_LOCAL
                 << setw(14) << (read + write) / calibratedDurationSec / BYTES_PER_MIB_LOCAL << "\n";
        };

        printBw("DDRC physical traffic", ctx.traffic.ddrcReadBytes, ctx.traffic.ddrcWriteBytes);
        printBw("Observed PCIe DMA", ctx.traffic.observedDmaReadBytes, ctx.traffic.observedDmaWriteBytes);
        printBw("PCIe DMA deducted", ctx.traffic.deductedDmaReadBytes, ctx.traffic.deductedDmaWriteBytes);
        printBw("System residual (DDRC - DMA)", residualRead, residualWrite);
        printBw(act.systemWide ? "All user-space (SPE-attributed)" :
                (act.pids.size() > 1 ? "Target PID(s) user (SPE-attributed)" :
                                       "Target PID user (SPE-attributed)"),
                ctx.attribution.readBytes, ctx.attribution.writeBytes);
        printBw("Other user (SPE-attributed)",
                ctx.attribution.otherReadBytes, ctx.attribution.otherWriteBytes);
        printBw("Kernel (SPE-attributed)",
                ctx.attribution.kernelReadBytes, ctx.attribution.kernelWriteBytes);
        printBw("Unattributed",
                ctx.attribution.unattributedReadBytes,
                ctx.attribution.unattributedWriteBytes);
        cout << string(80, '-') << "\n";

        const double residualTotal = residualRead + residualWrite;
        const double unattributedTotal =
            ctx.attribution.unattributedReadBytes +
            ctx.attribution.unattributedWriteBytes;
        const double attributedTotal = max(0.0,
            residualTotal - unattributedTotal);
        const double coverage = residualTotal > 0.0
            ? 100.0 * attributedTotal / residualTotal : 0.0;
        cout << "System partition coverage: " << coverage
             << "%    User function windows: read "
             << ctx.attribution.readWindows << "/" << ctx.windows.size()
             << ", write " << ctx.attribution.writeWindows << "/" << ctx.windows.size() << "\n";

        if (!pcieOpened) {
            cout << "PCIe: unavailable; DMA is included in system residual.\n";
        } else if (ctx.traffic.pcieWindows == 0) {
            cout << "PCIe: no raw flux windows; DMA was not deducted.\n";
        } else {
            cout << "PCIe: " << ctx.traffic.pcieWindows << " synchronized windows\n";
        }
        if (ctx.traffic.pcieIncompleteWindows > 0) {
            cout << "PCIe: " << ctx.traffic.pcieIncompleteWindows
                 << " incomplete windows; DMA deduction and function attribution "
                    "were skipped for those windows.\n";
        }

        if (ctx.traffic.dmaDeductionClamped) {
            cout << "Note: PCIe DMA exceeded DDRC in one or more windows; "
                    "deduction was capped at DDRC traffic.\n";
        }
        cout << string(80, '=') << "\n";
        PrintPcieTable(ctx, durationSec);
    } else {
        const double estimatedReadOps =
            static_cast<double>(ctx.spe.readWeight) * act.period;
        const double estimatedWriteOps =
            static_cast<double>(ctx.spe.writeWeight) * act.period;
        cout << "Estimated SPE operation rates: read loads="
             << estimatedReadOps / durationSec / OPS_PER_MILLION
             << " stores=" << estimatedWriteOps / durationSec / OPS_PER_MILLION
             << " total=" << (estimatedReadOps + estimatedWriteOps) /
                    durationSec / OPS_PER_MILLION
             << " Mops/s (sample-count estimate, period=" << act.period << ")\n";
        cout << "DDRC status: "
             << (ddrcOpened ? "opened but returned no metric windows" : "unavailable")
             << "; physical DDR bandwidth is not available.\n";
    }

    if (act.debug) {
        cout << "\nSPE samples: read_mode="
             << (IsSpeDataSourceSupported() ? "dram-source" : "all-load-fallback")
             << "  attribution_scope="
             << (act.systemWide ? "all-users" : "target-pids")
             << "  selected_read_samples=" << ctx.spe.readSamples
             << "  selected_stores=" << ctx.spe.writeSamples
             << "  system_read_samples=" << ctx.spe.systemReadSamples
             << "  system_stores=" << ctx.spe.systemWriteSamples
             << "  kernel_read_samples=" << ctx.spe.kernelReadSamples
             << "  kernel_stores=" << ctx.spe.kernelWriteSamples
             << "  source_filtered=" << ctx.spe.filteredSamples
             << "  duration=" << durationSec << "s\n";
        cout << "Attribution windows:"
             << " invalid=" << ctx.attribution.invalidWindows
             << " read_sparse=" << ctx.attribution.sparseReadWindows
             << " read_no_denominator=" << ctx.attribution.noDenominatorReadWindows
             << " write_sparse=" << ctx.attribution.sparseWriteWindows
             << " write_no_denominator=" << ctx.attribution.noDenominatorWriteWindows
             << "\n";
        if (calibrated) {
            cout << "* Write BW(est*) partitions residual DDRC write bytes using system-wide "
                    "store sample shares. Store retirement and later dirty-line DRAM "
                    "writeback are not one-to-one.\n";
        } else {
            cout << "* SPE-only Mops/s values estimate sampled operation rates, not bytes or "
                    "physical DDR bandwidth; store sampling density can depend on opcode.\n";
        }

        cout << fixed << setprecision(6) << "DEBUG TIMING\n"
             << "  requested_duration=" << timing.requestedSec << "\n"
             << "  start_ns=" << timing.startNs << "\n"
             << "  end_ns=" << timing.endNs << "\n"
             << "  startSec=" << timing.startSec << "\n"
             << "  lastCollectSec=" << timing.lastCollectSec << "\n"
             << "  endSec=" << timing.endSec << "\n"
             << "  durationSec=" << timing.durationSec << "\n"
             << "  ddrc_elapsed=" << ctx.traffic.ddrcElapsedSec << "\n"
             << "  disable_cost=" << timing.disableCostSec << "\n";
        for (const auto& entry : ctx.traffic.pcieByBdf) {
            const PcieBytes& bytes = entry.second;
            cout << "  " << entry.first
                 << " rx_mwr_dw=" << bytes.rxMwr / 4.0
                 << " tx_cpl_dw=" << bytes.txCpl / 4.0
                 << " rx_mwr_bytes=" << bytes.rxMwr
                 << " pcie_elapsed=" << bytes.elapsedSec
                 << " min_running_pct="
                 << (bytes.hasRunningRatio ? bytes.minRunningRatio * 100.0 : 0.0) << "\n";
        }
        cout << "\n";
    }
}

static volatile int g_execErrNo = 0;
static volatile sig_atomic_t g_stopRequested = 0;

static void StopCollectSignal(int signo)
{
    (void)signo;
    g_stopRequested = 1;
}

static void ExecFailedSignal(int signo, siginfo_t* info, void* ucontext)
{
    (void)signo;
    (void)ucontext;
    g_execErrNo = info->si_value.sival_int;
}

static int ExecCommand(const vector<string>& command, int fd[2])
{
    if (pipe(fd) != 0) {
        perror("pipe failed");
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        close(fd[0]);
        close(fd[1]);
        fd[0] = fd[1] = -1;
        return -1;
    }

    if (pid == 0) {
        close(fd[1]);
        char startToken[4];
        int ret = read(fd[0], startToken, sizeof(startToken));
        close(fd[0]);
        if (ret != static_cast<int>(sizeof(startToken))) _exit(EXIT_FAILURE);

        vector<char*> childArgv;
        childArgv.reserve(command.size() + 1);
        for (const auto& arg : command) childArgv.push_back(const_cast<char*>(arg.c_str()));
        childArgv.push_back(nullptr);
        execvp(childArgv[0], childArgv.data());

        union sigval val;
        val.sival_int = errno;
        sigqueue(getppid(), SIGUSR1, val);
        _exit(EXIT_FAILURE);
    }

    close(fd[0]);
    fd[0] = -1;
    return pid;
}

static void StopLaunchedApp(int pid, bool isLaunch)
{
    if (!isLaunch || pid <= 0) return;
    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == 0) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
}

int ddr_bw_run(int argc, char** argv)
{
    ArgsContext act;
    g_execErrNo = 0;
    g_stopRequested = 0;

    if (!ParseArgs(argc, argv, act)) return 1;
    if (!act.isLaunch && !act.systemWide && !ValidateTargetPids(act.pids)) return 1;

    struct sigaction saInt{};
    saInt.sa_handler = StopCollectSignal;
    sigemptyset(&saInt.sa_mask);
    sigaction(SIGINT, &saInt, nullptr);

    if (act.isLaunch) {
        struct sigaction saExec{};
        saExec.sa_flags = SA_SIGINFO;
        saExec.sa_sigaction = ExecFailedSignal;
        sigemptyset(&saExec.sa_mask);
        sigaction(SIGUSR1, &saExec, nullptr);

        act.pid = ExecCommand(act.command, act.fd);
        if (act.pid <= 0) {
            cerr << "Failed to create target process.\n";
            return 1;
        }
        act.pids.clear();
        act.pids.push_back(act.pid);
    }

    int ret = RunDdrBandwidthCollection(act, &g_stopRequested, &g_execErrNo);

    if (act.fd[1] != -1) {
        close(act.fd[1]);
        act.fd[1] = -1;
    }
    StopLaunchedApp(act.pid, act.isLaunch);
    return ret;
}
