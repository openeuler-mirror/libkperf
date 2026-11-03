#ifndef DDR_BW_TYPES_H
#define DDR_BW_TYPES_H

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

struct PcieBytes {
    double rxMwr = 0.0;
    double txCpl = 0.0;
    double elapsedSec = 0.0;
    double minRunningRatio = 1.0;
    bool hasRunningRatio = false;
};

constexpr double BYTES_PER_MIB = 1024.0 * 1024.0;

struct FuncBwStat {
    int pid = 0;
    std::string funcName;
    std::string moduleName;
    std::string funcKey;
    uint64_t readWeight = 0;
    uint64_t writeWeight = 0;
    uint64_t readSamples = 0;
    uint64_t writeSamples = 0;
    double attributedReadBytes = 0.0;
    double attributedWriteBytes = 0.0;
    double readRate = 0.0;
    double writeRate = 0.0;
    double totalRate = 0.0;
};

struct IntervalWeights {
    bool valid = true;
    std::unordered_map<std::string, uint64_t> funcReadSamples;
    std::unordered_map<std::string, uint64_t> funcWriteSamples;
    uint64_t targetReadSamples = 0;
    uint64_t targetWriteSamples = 0;
    uint64_t kernelReadSamples = 0;
    uint64_t kernelWriteSamples = 0;
    uint64_t systemReadSamples = 0;
    uint64_t systemWriteSamples = 0;
};

struct WindowRecord {
    bool valid = true;
    bool ddrcValid = true;
    bool speValid = true;
    bool pcieValid = false;
    int64_t startNs = 0;
    int64_t endNs = 0;
    double ddrcReadBytes = 0.0;
    double ddrcWriteBytes = 0.0;
    double observedDmaReadBytes = 0.0;
    double observedDmaWriteBytes = 0.0;
    double deductedDmaReadBytes = 0.0;
    double deductedDmaWriteBytes = 0.0;
    double residualReadBytes = 0.0;
    double residualWriteBytes = 0.0;
    std::unordered_map<std::string, uint64_t> funcReadSamples;
    std::unordered_map<std::string, uint64_t> funcWriteSamples;
    uint64_t targetReadSamples = 0;
    uint64_t targetWriteSamples = 0;
    uint64_t kernelReadSamples = 0;
    uint64_t kernelWriteSamples = 0;
    uint64_t systemReadSamples = 0;
    uint64_t systemWriteSamples = 0;
    std::unordered_map<std::string, uint64_t> effectiveFuncWriteSamples;
    uint64_t effectiveTargetWriteSamples = 0;
    uint64_t effectiveKernelWriteSamples = 0;
    uint64_t effectiveSystemWriteSamples = 0;
    uint64_t speReadCostNs = 0;
    uint64_t speDrains = 0;
    double attributedReadBytes = 0.0;
    double attributedWriteBytes = 0.0;
    double attributedKernelReadBytes = 0.0;
    double observedKernelWriteBytes = 0.0;
};

struct SpeTimestampStats {
    uint64_t missing = 0;
    uint64_t beforeStart = 0;
    uint64_t afterStop = 0;
    uint64_t afterRead = 0;
    uint64_t late = 0;
    uint64_t unbucketed = 0;
};

// Counts each stage of the SPE record processing pipeline. These counters
// describe records (an atomic record can be both a load and a store).
struct SpeDecodeStats {
    uint64_t recordsSeen = 0;
    uint64_t afterRead = 0;
    uint64_t missingExt = 0;
    uint64_t nonLoadStore = 0;
    uint64_t targetUserRecords = 0;
    uint64_t kernelRecords = 0;
    uint64_t otherUserRecords = 0;
    uint64_t nonDramLoads = 0;
    uint64_t eligibleDramLoads = 0;
    uint64_t eligibleStores = 0;
    uint64_t bucketRejected = 0;
    uint64_t bucketedRecords = 0;
    uint64_t targetBucketedRecords = 0;
};

struct SpeWindowState {
    int64_t collectionStartNs = 0;
    int64_t openStartNs = 0;
    int64_t finalEndNs = 0;
    int64_t lastReadEndNs = 0;
    int64_t failedReadStartNs = 0;
    uint64_t openSpeReadCostNs = 0;
    uint64_t openSpeDrains = 0;
    IntervalWeights open;
};

struct SpeContribution {
    int64_t timestampNs = 0;
    bool target = false;
    bool kernel = false;
    bool read = false;
    bool write = false;
    std::string funcKey;
};

struct CollectionTiming {
    double requestedSec = 0.0;
    int64_t startNs = 0;
    int64_t endNs = 0;
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
    uint64_t kernelReadSamples = 0;
    uint64_t kernelWriteSamples = 0;
    uint64_t systemReadSamples = 0;
    uint64_t systemWriteSamples = 0;
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
    uint64_t pcieWindows = 0;
    uint64_t pcieIncompleteWindows = 0;
    bool dmaDeductionClamped = false;
};

struct AttributionTotals {
    double readBytes = 0.0;
    double writeBytes = 0.0;
    double kernelReadBytes = 0.0;
    double kernelObservedWriteBytes = 0.0;
    uint64_t readWindows = 0;
    uint64_t writeWindows = 0;
    uint64_t sparseReadWindows = 0;
    uint64_t sparseWriteWindows = 0;
    uint64_t noDenominatorReadWindows = 0;
    uint64_t noDenominatorWriteWindows = 0;
    uint64_t invalidWindows = 0;
};

struct BwContext {
    std::unordered_map<std::string, FuncBwStat> funcStats;
    std::vector<WindowRecord> windows;
    SpeTotals spe;
    SpeDecodeStats speDecode;
    SpeTimestampStats speTimestamps;
    TrafficTotals traffic;
    AttributionTotals attribution;
};

void FinalizeAttribution(BwContext& ctx, uint64_t minSamples);
bool BucketSpeContribution(BwContext& ctx, SpeWindowState& state,
                           const SpeContribution& contribution);

#endif
