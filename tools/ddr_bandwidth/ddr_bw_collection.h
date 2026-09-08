#ifndef DDR_BW_COLLECTION_H
#define DDR_BW_COLLECTION_H

#include <cstdint>
#include <map>
#include <signal.h>
#include <string>
#include <unordered_map>

#include "ddr_bw_cli.h"
#include "ddr_bw_pmu.h"

constexpr double BYTES_PER_MIB = 1024.0 * 1024.0;
constexpr uint64_t MIN_READ_SAMPLES_PER_WINDOW = 3;
constexpr uint64_t MIN_WRITE_SAMPLES_PER_WINDOW = 100;

struct FuncBwStat {
    int pid = 0;
    std::string funcName;
    std::string moduleName;
    std::string funcKey;
    uint64_t readWeight = 0;
    uint64_t writeWeight = 0;
    uint32_t readSamples = 0;
    uint32_t writeSamples = 0;
    double attributedReadBytes = 0.0;
    double attributedWriteBytes = 0.0;
    double readBw = 0.0;
    double writeBw = 0.0;
    double totalBw = 0.0;
};

struct IntervalWeights {
    std::unordered_map<std::string, uint64_t> funcReadSamples;
    std::unordered_map<std::string, uint64_t> funcWriteSamples;
    uint64_t totalReadSamples = 0;
    uint64_t totalWriteSamples = 0;
};

struct CollectionTiming {
    double requestedSec = 0.0;
    double startSec = 0.0;
    double lastCollectSec = 0.0;
    double endSec = 0.0;
    double durationSec = 0.0;
    double disableCostSec = 0.0;
};

struct SpeTotals {
    uint64_t readWeight = 0;
    uint64_t writeWeight = 0;
    uint64_t readSamples = 0;
    uint64_t writeSamples = 0;
    uint64_t filteredSamples = 0;
};

struct TrafficTotals {
    std::map<std::string, PcieBytes> pcieByBdf;
    double ddrcReadBytes = 0.0;
    double ddrcWriteBytes = 0.0;
    double observedDmaReadBytes = 0.0;
    double observedDmaWriteBytes = 0.0;
    double deductedDmaReadBytes = 0.0;
    double deductedDmaWriteBytes = 0.0;
    double ddrcElapsedSec = 0.0;
    uint64_t ddrcWindows = 0;
    uint64_t pcieWindows = 0;
    bool dmaDeductionClamped = false;
};

struct AttributionTotals {
    double readBytes = 0.0;
    double writeBytes = 0.0;
    uint64_t readWindows = 0;
    uint64_t writeWindows = 0;
};

struct BwContext {
    std::unordered_map<int, std::unordered_map<unsigned long, int>> accessBytesByPidAndPc;
    std::unordered_map<std::string, FuncBwStat> funcStats;
    SpeTotals spe;
    TrafficTotals traffic;
    AttributionTotals attribution;
};

int RunDdrBandwidthCollection(ArgsContext& act, volatile sig_atomic_t* stopRequested, volatile int* execErrNo);

#endif
