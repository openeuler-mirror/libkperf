#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <unistd.h>
#include <vector>

#include "pcerrc.h"
#include "pmu.h"
#include "symbol.h"

struct Options {
    int pid = -1;
    int duration = 1;
    unsigned long period = 8192;
    std::vector<int> cpus;
};

struct SampleKey {
    unsigned long pc = 0;
    std::string function;
    std::string module;

    bool operator<(const SampleKey& other) const
    {
        return std::tie(pc, function, module) < std::tie(other.pc, other.function, other.module);
    }
};

struct SampleStat {
    uint64_t samples = 0;
    uint64_t l1dAccess = 0;
    uint64_t l1dMiss = 0;
    uint64_t llcAccess = 0;
    uint64_t llcMiss = 0;
    uint64_t tlbAccess = 0;
    uint64_t tlbWalk = 0;
    uint64_t remoteAccess = 0;
    uint64_t latencySum = 0;
    uint64_t maxLatency = 0;
    unsigned long symbolOffset = 0;
    std::string file;
    unsigned int line = 0;
};

struct ReportRow {
    SampleKey key;
    SampleStat stat;
};

static void PrintUsage(const char* program)
{
    fprintf(stderr,
        "Usage: %s [-p PID] [-c CPU_LIST] [-d SEC] [-P PERIOD]\n"
        "Options:\n"
        "  -p PID       Target process ID\n"
        "  -c CPU_LIST  CPU list, e.g. 0, 0,2, 0-3, 0,2,4-7\n"
        "  -d SEC       Sampling duration in seconds, default: 1\n"
        "  -P PERIOD    SPE sampling period, default: 8192\n"
        "  -h           Show this help\n"
        "At least one of -p or -c must be specified.\n"
        "Examples:\n"
        "  %s -p 571512\n"
        "  %s -p 571512 -d 10 -P 4096\n"
        "  %s -c 56 -d 1 -P 8192\n"
        "  %s -p 571512 -c 3 -d 10 -P 16384\n",
        program, program, program, program, program);
}

static bool ParseCpuList(const std::string& input, std::vector<int>& cpus)
{
    cpus.clear();

    for (size_t start = 0; start < input.size();) {
        size_t end = input.find(',', start);
        std::string token = input.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (token.empty()) {
            return false;
        }

        size_t dash = token.find('-');
        try {
            if (dash == std::string::npos) {
                int cpu = std::stoi(token);
                if (cpu < 0) {
                    return false;
                }
                cpus.push_back(cpu);
            } else {
                std::string firstText = token.substr(0, dash);
                std::string lastText = token.substr(dash + 1);
                if (firstText.empty() || lastText.empty()) {
                    return false;
                }

                int first = std::stoi(firstText);
                int last = std::stoi(lastText);
                if (first < 0 || last < 0 || first > last) {
                    return false;
                }

                for (int cpu = first; cpu <= last; ++cpu) {
                    cpus.push_back(cpu);
                }
            }
        } catch (...) {
            return false;
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    std::sort(cpus.begin(), cpus.end());
    cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
    return !cpus.empty();
}

static bool ParseOptions(int argc, char* argv[], Options& options)
{
    int opt;
    while ((opt = getopt(argc, argv, "p:d:c:P:h")) != -1) {
        switch (opt) {
            case 'p':
                try {
                    options.pid = std::stoi(optarg);
                } catch (...) {
                    fprintf(stderr, "Invalid PID: %s\n", optarg);
                    return false;
                }
                if (options.pid <= 0) {
                    fprintf(stderr, "Invalid PID: %s\n", optarg);
                    return false;
                }
                break;

            case 'd':
                try {
                    options.duration = std::stoi(optarg);
                } catch (...) {
                    fprintf(stderr, "Invalid duration: %s\n", optarg);
                    return false;
                }
                if (options.duration <= 0) {
                    fprintf(stderr, "Duration must be greater than 0\n");
                    return false;
                }
                break;

            case 'c':
                if (!ParseCpuList(optarg, options.cpus)) {
                    fprintf(stderr, "Invalid CPU list: %s\n", optarg);
                    return false;
                }
                break;

            case 'P':
                try {
                    options.period = std::stoul(optarg);
                } catch (...) {
                    fprintf(stderr, "Invalid period: %s\n", optarg);
                    return false;
                }
                if (options.period == 0) {
                    fprintf(stderr, "Period must be greater than 0\n");
                    return false;
                }
                break;

            case 'h':
                PrintUsage(argv[0]);
                exit(0);

            default:
                return false;
        }
    }

    if (options.pid <= 0 && options.cpus.empty()) {
        fprintf(stderr, "At least one of -p or -c must be specified\n");
        return false;
    }

    return true;
}

static bool IsKernelSymbol(const Symbol* sym)
{
    return sym != nullptr && sym->module != nullptr && strcmp(sym->module, "[kernel]") == 0;
}

static double Percent(uint64_t value, uint64_t total)
{
    return total == 0 ? 0.0 : static_cast<double>(value) * 100.0 / static_cast<double>(total);
}

static std::string FormatCpuList(const std::vector<int>& cpus)
{
    if (cpus.empty()) {
        return "all";
    }

    std::string result;
    for (size_t i = 0; i < cpus.size(); ++i) {
        if (i != 0) {
            result += ",";
        }
        result += std::to_string(cpus[i]);
    }
    return result;
}

int main(int argc, char* argv[])
{
    Options options;
    if (!ParseOptions(argc, argv, options)) {
        PrintUsage(argv[0]);
        return -1;
    }

    PmuAttr attr = {};
    int pidList[1];

    if (options.pid > 0) {
        pidList[0] = options.pid;
        attr.pidList = pidList;
        attr.numPid = 1;
    }

    if (!options.cpus.empty()) {
        attr.cpuList = options.cpus.data();
        attr.numCpu = static_cast<unsigned int>(options.cpus.size());
    }

    attr.period = options.period;
    attr.dataFilter = LOAD_FILTER;
    attr.excludeUser = 0;
    attr.excludeKernel = 1;
    attr.symbolMode = SymbolMode::RESOLVE_ELF_DWARF;

    int pd = PmuOpen(SPE_SAMPLING, &attr);
    if (pd == -1) {
        fprintf(stderr, "PmuOpen failed: %s\n", Perror());
        return -1;
    }

    if (PmuEnable(pd) == -1) {
        fprintf(stderr, "PmuEnable failed: %s\n", Perror());
        PmuClose(pd);
        return -1;
    }

    sleep(options.duration);

    if (PmuDisable(pd) == -1) {
        fprintf(stderr, "PmuDisable failed: %s\n", Perror());
        PmuClose(pd);
        return -1;
    }

    PmuData* data = nullptr;
    int len = PmuRead(pd, &data);
    if (len < 0) {
        fprintf(stderr, "PmuRead failed: %s\n", Perror());
        PmuClose(pd);
        return -1;
    }

    if (GetWarn() != 0) {
        fprintf(stderr, "PmuRead warning: %s\n", GetWarnMsg());
    }

    std::map<SampleKey, SampleStat> stats;
    uint64_t totalSamples = 0;
    uint64_t userSamples = 0;
    uint64_t kernelSamples = 0;
    uint64_t userL1dMiss = 0;
    uint64_t kernelL1dMiss = 0;
    uint64_t userLlcMiss = 0;
    uint64_t kernelLlcMiss = 0;
    uint64_t unresolvedSamples = 0;

    for (int i = 0; i < len; ++i) {
        const PmuData& sample = data[i];
        if (sample.ext == nullptr) {
            continue;
        }

        const PmuDataExt* ext = sample.ext;
        const Symbol* sym = sample.stack != nullptr ? sample.stack->symbol : nullptr;
        bool isKernel = IsKernelSymbol(sym);

        ++totalSamples;
        isKernel ? ++kernelSamples : ++userSamples;

        SampleKey key;
        if (sym != nullptr) {
            key.pc = sym->addr;
            key.function = sym->symbolName != nullptr ? sym->symbolName : "[unknown]";
            key.module = sym->module != nullptr ? sym->module : "[unknown]";
        } else {
            key.function = "[unknown]";
            key.module = "[unknown]";
            ++unresolvedSamples;
        }

        SampleStat& stat = stats[key];
        ++stat.samples;

        if (ext->event & SPE_EV_L1D_ACCESS) {
            ++stat.l1dAccess;
        }

        if (ext->event & SPE_EV_L1D_REFILL) {
            ++stat.l1dMiss;
            isKernel ? ++kernelL1dMiss : ++userL1dMiss;
        }

        if (ext->event & SPE_EV_LLC_ACCESS) {
            ++stat.llcAccess;
        }

        if (ext->event & SPE_EV_LLC_MISS) {
            ++stat.llcMiss;
            isKernel ? ++kernelLlcMiss : ++userLlcMiss;
        }

        if (ext->event & SPE_EV_TLB_ACCESS) {
            ++stat.tlbAccess;
        }

        if (ext->event & SPE_EV_TLB_WALK) {
            ++stat.tlbWalk;
        }

        if (ext->event & SPE_EV_REMOTE_ACCESS) {
            ++stat.remoteAccess;
        }

        stat.latencySum += ext->lat;
        stat.maxLatency = std::max<uint64_t>(stat.maxLatency, ext->lat);

        if (sym != nullptr) {
            stat.symbolOffset = sym->offset;
            if (sym->fileName != nullptr) {
                stat.file = sym->fileName;
            }
            stat.line = sym->lineNum;
        }
    }

    std::vector<ReportRow> rows;
    rows.reserve(stats.size());

    for (const auto& item : stats) {
        rows.push_back({item.first, item.second});
    }

    std::sort(rows.begin(), rows.end(), [](const ReportRow& a, const ReportRow& b) {
        return a.stat.samples > b.stat.samples;
    });

    std::string pidText = options.pid > 0 ? std::to_string(options.pid) : "all";
    std::string cpuText = FormatCpuList(options.cpus);

    printf("# libkperf ARM SPE load report\n");
    printf("# PID: %s  CPU: %s  Duration: %ds  Period: %lu  Samples: %lu  User: %lu  Kernel: %lu\n",
        pidText.c_str(), cpuText.c_str(), options.duration, options.period,
        totalSamples, userSamples, kernelSamples);

    printf("# User L1D Miss: %lu  Kernel L1D Miss: %lu  User LLC Miss: %lu  Kernel LLC Miss: %lu  Unresolved: %lu\n",
        userL1dMiss, kernelL1dMiss, userLlcMiss, kernelLlcMiss, unresolvedSamples);

    printf(
        "%9s %8s %9s %9s %8s %9s %9s %8s %9s %9s %8s %8s %-18s | %-32s | %-32s | %s\n",
        "Ratio", "Samples", "L1D_ACC", "L1D_MISS", "L1D_M%", "LLC_ACC", "LLC_MISS", "LLC_M%",
        "TLB_ACC", "TLB_WALK", "Remote", "AvgLat", "PC", "Symbol", "Module", "Source");

    printf(
        "----------------------------------------------------------------"
        "----------------------------------------------------------------"
        "----------------------------------------------------------------"
        "------------------------------------------------------------\n");

    for (const ReportRow& row : rows) {
        const SampleKey& key = row.key;
        const SampleStat& stat = row.stat;

        double ratio = Percent(stat.samples, totalSamples);
        double l1dMissRate = Percent(stat.l1dMiss, stat.l1dAccess);
        double llcMissRate = Percent(stat.llcMiss, stat.llcAccess);
        double avgLatency = stat.samples == 0 ? 0.0 :
            static_cast<double>(stat.latencySum) / static_cast<double>(stat.samples);

        char symbolBuffer[512];
        snprintf(symbolBuffer, sizeof(symbolBuffer), "%s+0x%lx", key.function.c_str(), stat.symbolOffset);

        std::string source = "[unknown]";
        if (!stat.file.empty()) {
            source = stat.file;
            if (stat.line != 0) {
                source += ":" + std::to_string(stat.line);
            }
        }

        char pcBuffer[32];
        snprintf(pcBuffer, sizeof(pcBuffer), "0x%lx", key.pc);

        printf(
            "%8.2f%% %8lu %9lu %9lu %7.2f%% %9lu %9lu %7.2f%% %9lu %9lu %8lu %8.1f "
            "%-18s | %-32s | %-32s | %s\n",
            ratio, stat.samples, stat.l1dAccess, stat.l1dMiss, l1dMissRate,
            stat.llcAccess, stat.llcMiss, llcMissRate, stat.tlbAccess, stat.tlbWalk,
            stat.remoteAccess, avgLatency, pcBuffer, symbolBuffer, key.module.c_str(), source.c_str());
    }

    PmuDataFree(data);
    PmuClose(pd);
    return 0;
}