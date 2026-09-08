#include <algorithm>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "ddr_bw_cli.h"
#include "ddr_bw_collection.h"
using namespace std;

static constexpr double BYTES_PER_MIB_LOCAL = 1024.0 * 1024.0;
static const char* SHORT_OPS = "p:d:i:h";
static const struct option LONG_OPS[] = {
    {"pid", required_argument, nullptr, 'p'},
    {"duration", required_argument, nullptr, 'd'},
    {"interval", required_argument, nullptr, 'i'},
    {"period", required_argument, nullptr, 1001},
    {"min-latency", required_argument, nullptr, 1002},
    {"top", required_argument, nullptr, 1003},
    {"pcie-bdf", required_argument, nullptr, 1004},
    {"debug", no_argument, nullptr, 1005},
    {"help", no_argument, nullptr, 'h'},
    {nullptr, 0, nullptr, 0}
};

static bool ParseUnsigned(const char* arg, const char* name, unsigned long& out)
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
    if (end == arg || *end != '\0' || errno == ERANGE) {
        cerr << "Error: parameter '" << name
             << "' is invalid or out of range: '" << arg << "'.\n";
        return false;
    }
    out = val;
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
        if (tail == token.c_str() || *tail != '\0' || errno == ERANGE ||
            value <= 0 || value > INT_MAX) {
            cerr << "Error: invalid pid '" << token << "'.\n";
            return false;
        }
        int pid = static_cast<int>(value);
        if (find(pids.begin(), pids.end(), pid) == pids.end()) {
            pids.push_back(pid);
        }

        if (endPos == string::npos) break;
        begin = endPos + 1;
    }
    return !pids.empty();
}

void PrintHelp()
{
    cerr <<
        "Usage: ddr_bandwidth [options] -p <pid[,pid...]> [-p <pid> ...]\n"
        "       ddr_bandwidth [options] -- <command> [args...]\n\n"
        "Attribute physical DDR bandwidth with DDRC + SPE + PCIe PMUs.\n"
        "Options:\n"
        "  -p, --pid <pid[,pid...]> Target process ID(s); repeatable\n"
        "  -d, --duration <sec>      Collection duration in seconds (default: 10)\n"
        "  -i, --interval <ms>       Attribution window in ms (default: 100)\n"
        "      --period <n>          SPE sampling period\n"
        "      --min-latency <n>     Minimum latency filter in cycles\n"
        "      --top <n>             Show top N functions (default: 20)\n"
        "      --pcie-bdf <bdf>      Collect only this PCIe BDF (repeatable; default: all)\n"
        "      --debug               Print timing, SPE/window health and store-PC diagnostics\n"
        "  -h, --help                Show this help\n\n"
        "Examples:\n"
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
            case 'p':
                if (!AppendPidList(optarg, act.pids)) return false;
                break;
            case 'd': {
                unsigned long val = 0;
                if (!ParseUnsigned(optarg, "duration", val)) return false;
                if (val == 0 || val > static_cast<unsigned long>(INT_MAX)) {
                    cerr << "Error: duration must be in 1.." << INT_MAX << " seconds.\n";
                    return false;
                }
                act.duration = static_cast<int>(val);
                break;
            }
            case 'i': {
                unsigned long val = 0;
                if (!ParseUnsigned(optarg, "interval", val)) return false;
                if (val == 0 || val > static_cast<unsigned long>(INT_MAX)) {
                    cerr << "Error: interval must be in 1.." << INT_MAX << " ms.\n";
                    return false;
                }
                act.interval = static_cast<int>(val);
                break;
            }
            case 1001: {
                unsigned long val = 0;
                if (!ParseUnsigned(optarg, "period", val)) return false;
                if (val == 0 || val > static_cast<unsigned long>(UINT_MAX)) {
                    cerr << "Error: period must be in 1.." << UINT_MAX << ".\n";
                    return false;
                }
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
                if (!ParseUnsigned(optarg, "top", val)) return false;
                if (val == 0 || val > static_cast<unsigned long>(INT_MAX)) {
                    cerr << "Error: top must be in 1.." << INT_MAX << ".\n";
                    return false;
                }
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
            case 'h':
                PrintHelp();
                return false;
            default:
                PrintHelp();
                return false;
        }
    }

    if (!act.pids.empty() && optind < argc) {
        cerr << "Error: cannot combine -p/--pid with a launch command.\n";
        return false;
    }

    if (act.pids.empty() && optind < argc) {
        for (int i = optind; i < argc; ++i) act.command.push_back(argv[i]);
        act.isLaunch = true;
    }

    if (act.pids.empty() && !act.isLaunch) {
        cerr << "Error: must specify -p <pid> or a command to launch.\n";
        PrintHelp();
        return false;
    }

    if (!act.pids.empty()) {
        // Keep the legacy primary-PID field valid for code paths that need one PID.
        act.pid = act.pids.front();
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

static void PrintBwCell(double bw)
{
    if (bw <= 0.0) cout << setw(14) << "-";
    else cout << setw(14) << bw / BYTES_PER_MIB_LOCAL;
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
                stat.readBw = stat.attributedReadBytes / durationSec;
                stat.writeBw = stat.attributedWriteBytes / durationSec;
            } else {
                stat.readBw = static_cast<double>(stat.readWeight) * act.period / durationSec;
                stat.writeBw = static_cast<double>(stat.writeWeight) * act.period / durationSec;
            }
        }
        stat.totalBw = stat.readBw + stat.writeBw;
        stats.push_back(move(stat));
    }

    sort(stats.begin(), stats.end(), [](const FuncBwStat& lhs, const FuncBwStat& rhs) {
        return lhs.totalBw > rhs.totalBw;
    });

    constexpr int FUNC_WIDTH = 72;
    constexpr int MODULE_WIDTH = 45;
    const int tableWidth = FUNC_WIDTH + MODULE_WIDTH + 14 + 14 + 14 + 10;

    cout << string(tableWidth, '=') << "\nCPU DDR BANDWIDTH BY FUNCTION\n"
         << string(tableWidth, '=') << "\n"
         << left << setw(FUNC_WIDTH) << "Function"
         << setw(MODULE_WIDTH) << "Module"
         << right << setw(14) << "Read BW"
         << setw(14) << "Write BW(est*)"
         << setw(14) << "Total BW"
         << setw(10) << "Samples" << "\n"
         << left << setw(FUNC_WIDTH) << ""
         << setw(MODULE_WIDTH) << ""
         << right << setw(14) << "(MiB/s)"
         << setw(14) << "(MiB/s)"
         << setw(14) << "(MiB/s)"
         << setw(10) << "" << "\n"
         << string(tableWidth, '-') << "\n";

    int shown = 0;
    for (const auto& stat : stats) {
        if (shown >= act.topN) break;
        if (stat.totalBw <= 0.0 && stat.readSamples == 0 && stat.writeSamples == 0) continue;

        cout << left << setw(FUNC_WIDTH) << ShortenLeft(stat.funcName, FUNC_WIDTH - 2)
             << setw(MODULE_WIDTH) << ShortenRight(stat.moduleName, MODULE_WIDTH - 2)
             << right << fixed << setprecision(2);
        PrintBwCell(stat.readBw);
        PrintBwCell(stat.writeBw);
        cout << setw(14) << stat.totalBw / BYTES_PER_MIB_LOCAL
             << setw(10) << (stat.readSamples + stat.writeSamples) << "\n";
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
    if (act.pids.size() <= 1) return;

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

    cout << "\n" << string(78, '=') << "\n"
         << "CPU DDR BANDWIDTH BY PROCESS\n"
         << string(78, '=') << "\n"
         << left << setw(12) << "PID"
         << right << setw(16) << "Read BW"
         << setw(16) << "Write BW(est*)"
         << setw(16) << "Total BW"
         << setw(14) << "Samples" << "\n"
         << left << setw(12) << ""
         << right << setw(16) << "(MiB/s)"
         << setw(16) << "(MiB/s)"
         << setw(16) << "(MiB/s)"
         << setw(14) << "" << "\n"
         << string(78, '-') << "\n";

    for (const auto& entry : processes) {
        const ProcessBwStat& p = entry.second;
        double readBw = 0.0;
        double writeBw = 0.0;
        if (durationSec > 0.0) {
            if (calibrated) {
                readBw = p.readBytes / durationSec;
                writeBw = p.writeBytes / durationSec;
            } else {
                readBw = static_cast<double>(p.readWeight) * act.period / durationSec;
                writeBw = static_cast<double>(p.writeWeight) * act.period / durationSec;
            }
        }

        cout << left << setw(12) << p.pid
             << right << fixed << setprecision(2)
             << setw(16) << readBw / BYTES_PER_MIB_LOCAL
             << setw(16) << writeBw / BYTES_PER_MIB_LOCAL
             << setw(16) << (readBw + writeBw) / BYTES_PER_MIB_LOCAL
             << setw(14) << p.samples << "\n";
    }
    cout << string(78, '-') << "\n";
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
    const bool calibrated = ctx.traffic.ddrcWindows > 0;
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
        const double unattributedRead = max(0.0, residualRead - ctx.attribution.readBytes);
        const double unattributedWrite = max(0.0, residualWrite - ctx.attribution.writeBytes);

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
        printBw(act.pids.size() > 1 ? "Target PID(s) attributed" : "Target PID attributed",
                ctx.attribution.readBytes, ctx.attribution.writeBytes);
        printBw("Unattributed/system", unattributedRead, unattributedWrite);
        cout << string(80, '-') << "\n";

        const double residualTotal = residualRead + residualWrite;
        const double attributedTotal = ctx.attribution.readBytes + ctx.attribution.writeBytes;
        const double coverage = residualTotal > 0.0
            ? 100.0 * attributedTotal / residualTotal : 0.0;
        cout << "Attribution coverage: " << coverage << "%    Windows: read "
             << ctx.attribution.readWindows << "/" << ctx.traffic.ddrcWindows
             << ", write " << ctx.attribution.writeWindows << "/"
             << ctx.traffic.ddrcWindows << "\n";

        if (!pcieOpened) {
            cout << "PCIe: unavailable; DMA is included in system residual.\n";
        } else if (ctx.traffic.pcieWindows == 0) {
            cout << "PCIe: no raw flux windows; DMA was not deducted.\n";
        } else {
            cout << "PCIe: " << ctx.traffic.pcieWindows << " synchronized windows\n";
        }

        if (ctx.traffic.dmaDeductionClamped) {
            cout << "Note: PCIe DMA exceeded DDRC in one or more windows; "
                    "deduction was capped at DDRC traffic.\n";
        }
        cout << string(80, '=') << "\n";
        PrintPcieTable(ctx, durationSec);
    } else {
        const double estimatedRead = static_cast<double>(ctx.spe.readWeight) * act.period;
        const double estimatedWrite = static_cast<double>(ctx.spe.writeWeight) * act.period;
        cout << "Estimated CPU DDR bandwidth: read="
             << estimatedRead / durationSec / BYTES_PER_MIB_LOCAL
             << " write(est*)=" << estimatedWrite / durationSec / BYTES_PER_MIB_LOCAL
             << " total=" << (estimatedRead + estimatedWrite) / durationSec / BYTES_PER_MIB_LOCAL
             << " MiB/s (SPE sample-count statistical estimate, period=" << act.period << ")\n";
        cout << "DDRC status: "
             << (ddrcOpened ? "opened but returned no metric windows" : "unavailable") << ".\n";
    }

    cout << "\nSPE samples: DRAM loads=" << ctx.spe.readSamples
         << "  stores=" << ctx.spe.writeSamples
         << "  filtered=" << ctx.spe.filteredSamples
         << "  duration=" << durationSec << "s\n"
         << "* Write BW(est*) uses store sample counts. Store retirement and later "
            "dirty-line DRAM writeback are not one-to-one, and store sampling density "
            "can depend on opcode.\n";

    if (act.debug) {
        cout << fixed << setprecision(6) << "DEBUG TIMING\n"
             << "  requested_duration=" << timing.requestedSec << "\n"
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
