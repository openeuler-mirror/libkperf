#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <limits>
#include <signal.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include "pcerrc.h"
#include "ddr_bw_collection.h"
#include "ddr_bw_schedule.h"
using namespace std;

static bool ExactStoreSyncEnabled()
{
    const char* value = getenv("DDR_BW_EXACT_STORE_SYNC");
    return value != nullptr && strcmp(value, "1") == 0;
}

static unsigned SignalTargets(const vector<int>& pids, int signo, const char* name)
{
    unsigned signaled = 0;
    for (int pid : pids) {
        if (pid <= 0) continue;
        if (kill(pid, signo) == 0) {
            ++signaled;
            continue;
        }
        if (errno != ESRCH) {
            cerr << "Warning: failed to send " << name
                 << " to pid " << pid << ": " << strerror(errno) << "\n";
        }
    }
    return signaled;
}

struct CollectionHealth {
    double speReadCostSec = 0.0;
    double maxSpeReadCostSec = 0.0;
    uint64_t speDrains = 0;
    uint64_t speEmptyDrains = 0;
    uint64_t speFailedDrains = 0;
    uint64_t speRecords = 0;
    uint64_t maxSpeRecordsPerDrain = 0;

    uint64_t counterWindows = 0;
    uint64_t ddrcAttemptedWindows = 0;
    uint64_t ddrcValidWindows = 0;
    uint64_t pcieAttemptedWindows = 0;
    uint64_t pcieValidWindows = 0;
    uint64_t pcieIncompleteWindows = 0;
    uint64_t pcieFailedReads = 0;
    uint64_t pcieResynchronizedReads = 0;

    uint64_t readEvidenceWindows = 0;
    uint64_t writeEvidenceWindows = 0;
    uint64_t zeroEvidenceWindows = 0;
    uint64_t targetReadSamples = 0;
    uint64_t targetWriteSamples = 0;
    uint64_t minReadSamples = numeric_limits<uint64_t>::max();
    uint64_t maxReadSamples = 0;
    uint64_t minWriteSamples = numeric_limits<uint64_t>::max();
    uint64_t maxWriteSamples = 0;
};

static void AccountWindowHealth(CollectionHealth* health,
                                bool ddrcAttempted, bool ddrcOk,
                                bool pcieAttempted,
                                const PcieReadResult& pcieResult)
{
    if (health == nullptr) return;

    ++health->counterWindows;
    if (ddrcAttempted) {
        ++health->ddrcAttemptedWindows;
        if (ddrcOk) ++health->ddrcValidWindows;
    }
    if (pcieAttempted) {
        ++health->pcieAttemptedWindows;
        if (pcieResult.Complete()) {
            ++health->pcieValidWindows;
        } else {
            ++health->pcieIncompleteWindows;
        }
        health->pcieFailedReads += pcieResult.failed;
        health->pcieResynchronizedReads += pcieResult.resynchronized;
    }

}

static void RefreshSpeWindowHealth(CollectionHealth& health, const BwContext& ctx)
{
    health.readEvidenceWindows = 0;
    health.writeEvidenceWindows = 0;
    health.zeroEvidenceWindows = 0;
    health.targetReadSamples = 0;
    health.targetWriteSamples = 0;
    health.minReadSamples = numeric_limits<uint64_t>::max();
    health.maxReadSamples = 0;
    health.minWriteSamples = numeric_limits<uint64_t>::max();
    health.maxWriteSamples = 0;
    for (const auto& window : ctx.windows) {
        if (window.targetReadSamples > 0) ++health.readEvidenceWindows;
        if (window.effectiveTargetWriteSamples > 0) ++health.writeEvidenceWindows;
        if (window.targetReadSamples == 0 &&
            window.effectiveTargetWriteSamples == 0) {
            ++health.zeroEvidenceWindows;
        }
        health.targetReadSamples += window.targetReadSamples;
        health.targetWriteSamples += window.effectiveTargetWriteSamples;
        health.minReadSamples = min(health.minReadSamples, window.targetReadSamples);
        health.maxReadSamples = max(health.maxReadSamples, window.targetReadSamples);
        health.minWriteSamples = min(
            health.minWriteSamples, window.effectiveTargetWriteSamples);
        health.maxWriteSamples = max(
            health.maxWriteSamples, window.effectiveTargetWriteSamples);
    }
}

static void PrintCollectionHealth(const CollectionHealth& health,
                                  const ArgsContext& act, double durationSec)
{
    const double avgDrain = health.speDrains > 0
        ? static_cast<double>(health.speRecords) / static_cast<double>(health.speDrains)
        : 0.0;
    const double avgRead = health.counterWindows > 0
        ? static_cast<double>(health.targetReadSamples) / static_cast<double>(health.counterWindows)
        : 0.0;
    const double avgWrite = health.counterWindows > 0
        ? static_cast<double>(health.targetWriteSamples) / static_cast<double>(health.counterWindows)
        : 0.0;

    const uint64_t minRead = health.counterWindows > 0 ? health.minReadSamples : 0;
    const uint64_t minWrite = health.counterWindows > 0 ? health.minWriteSamples : 0;
    const uint64_t expectedWindows = act.interval > 0
        ? static_cast<uint64_t>(durationSec * 1000.0 / act.interval + 0.5)
        : 0;
    const double speReadRatio = durationSec > 0.0
        ? health.speReadCostSec / durationSec * 100.0 : 0.0;

    cout << "\n" << string(80, '=') << "\n"
         << "DEBUG SPE / WINDOW HEALTH\n"
         << string(80, '=') << "\n"
         << "SPE drains: total=" << health.speDrains
         << " empty=" << health.speEmptyDrains
         << " failed=" << health.speFailedDrains
         << " records=" << health.speRecords
         << " avg_records/drain=" << fixed << setprecision(2) << avgDrain
         << " max_records/drain=" << health.maxSpeRecordsPerDrain << "\n"
         << "SPE PmuRead wall time: total=" << health.speReadCostSec
         << "s max=" << health.maxSpeReadCostSec
         << "s share=" << speReadRatio
         << "% (includes decode/symbol work; not exact SPE pause time)\n"
         << "Counter windows: total=" << health.counterWindows
         << " expected~=" << expectedWindows
         << " DDRC=" << health.ddrcValidWindows << "/" << health.ddrcAttemptedWindows
         << " PCIe=" << health.pcieValidWindows << "/" << health.pcieAttemptedWindows << "\n"
         << "PCIe incomplete windows=" << health.pcieIncompleteWindows
         << " failed_reads=" << health.pcieFailedReads
         << " resynchronized_reads=" << health.pcieResynchronizedReads << "\n"
         << "SPE evidence windows: read=" << health.readEvidenceWindows
         << " write=" << health.writeEvidenceWindows
         << " zero=" << health.zeroEvidenceWindows << "\n"
         << (act.systemWide ? "All-user" : "Target")
         << " read samples/window: min=" << minRead
         << " avg=" << avgRead
         << " max=" << health.maxReadSamples << "\n"
         << (act.systemWide ? "All-user" : "Target")
         << " effective write samples/window (current+2 history): min="
         << minWrite
         << " avg=" << avgWrite
         << " max=" << health.maxWriteSamples << "\n";

    if (health.speFailedDrains > 0 ||
        health.ddrcValidWindows < health.ddrcAttemptedWindows ||
        health.pcieValidWindows < health.pcieAttemptedWindows) {
        cout << "Health status: WARN (collector read failures detected)\n";
    } else if (expectedWindows > 0 &&
               health.counterWindows * 10 < expectedWindows * 8) {
        cout << "Health status: WARN (fewer than 80% of scheduled windows were collected; "
                "increase --period)\n";
    } else if (speReadRatio > 20.0) {
        cout << "Health status: WARN (SPE read/decode consumed more than 20% of "
                "collection time; increase --period)\n";
    } else if (health.speDrains > 0 &&
               health.speEmptyDrains * 10 > health.speDrains) {
        cout << "Health status: WARN (>10% SPE drains were empty)\n";
    } else if (health.counterWindows > 0 &&
               health.zeroEvidenceWindows * 2 > health.counterWindows) {
        cout << "Health status: WARN (>50% windows had no usable SPE attribution evidence)\n";
    } else {
        cout << "Health status: OK\n";
    }
    cout << "Note: this health block detects empty/failed reads, evidence gaps, and "
            "scheduling overload; "
            "it does not claim hardware AUX-loss detection.\n"
         << string(80, '=') << "\n";
}


static void PrintSpePipelineDiagnostics(const CollectionHealth& health,
                                        const BwContext& ctx,
                                        const ArgsContext& act)
{
    const SpeDecodeStats& decode = ctx.speDecode;
    const uint64_t targetSamples = ctx.spe.readSamples + ctx.spe.writeSamples;

    cout << "\n" << string(80, '=') << "\n"
         << "DEBUG SPE SAMPLE PIPELINE DIAGNOSTICS\n"
         << string(80, '=') << "\n"
         << "collector: drains=" << health.speDrains
         << " empty=" << health.speEmptyDrains
         << " failed=" << health.speFailedDrains
         << " records_returned=" << health.speRecords << "\n"
         << "decode: seen=" << decode.recordsSeen
         << " after_read=" << decode.afterRead
         << " missing_ext=" << decode.missingExt
         << " non_load_store=" << decode.nonLoadStore << "\n"
         << "scope: attribution=" << (act.systemWide ? "all-users" : "target-pids")
         << " selected_user=" << decode.targetUserRecords
         << " other_user=" << decode.otherUserRecords
         << " kernel=" << decode.kernelRecords << "\n"
         << "read mode: "
         << (IsSpeDataSourceSupported() ? "dram-source" : "all-load-fallback")
         << " fallback_loads=" << decode.fallbackModeLoads << "\n"
         << "operation: eligible_read_loads=" << decode.eligibleReadLoads
         << " stores=" << decode.eligibleStores
         << " excluded_read_loads=" << decode.excludedReadLoads << "\n"
         << "windowing: bucketed=" << decode.bucketedRecords
         << " selected_bucketed=" << decode.targetBucketedRecords
         << " rejected=" << decode.bucketRejected << "\n"
         << "timestamp rejects: missing=" << ctx.speTimestamps.missing
         << " before_start=" << ctx.speTimestamps.beforeStart
         << " after_stop=" << ctx.speTimestamps.afterStop
         << " after_read=" << ctx.speTimestamps.afterRead
         << " late=" << ctx.speTimestamps.late
         << " unbucketed=" << ctx.speTimestamps.unbucketed << "\n"
         << "result: selected_read_samples=" << ctx.spe.readSamples
         << " selected_stores=" << ctx.spe.writeSamples
         << " function_rows=" << ctx.funcStats.size() << "\n";

    cout << "Diagnosis: ";
    if (health.speFailedDrains > 0 && health.speRecords == 0) {
        cout << "SPE reads failed before any record was collected; check the backend error above.\n";
    } else if (health.speRecords == 0) {
        cout << "the collector returned no SPE records; check SPE support/permissions, sampling period, and whether the workload ran.\n";
    } else if (decode.recordsSeen == 0) {
        cout << "records were returned by PmuRead but none reached processing; inspect the collector/read handoff.\n";
    } else if (decode.eligibleReadLoads == 0 && decode.eligibleStores == 0) {
        cout << "collection worked, but decoding/filtering found no eligible read-load or store records.\n";
    } else if (decode.targetUserRecords == 0) {
        cout << "eligible system SPE traffic exists, but no user record matched the attribution scope; check PID selection and process lifetime.\n";
    } else if (decode.targetBucketedRecords == 0) {
        cout << "selected records exist, but none survived operation and timestamp-window filtering; inspect operation/window counters above.\n";
    } else if (targetSamples == 0) {
        cout << "selected records reached a window but produced no eligible read-load/store sample; inspect excluded_read_loads and timestamp counters.\n";
    } else if (ctx.funcStats.empty()) {
        cout << "selected samples were counted, but function statistics were not created; this indicates a post-processing defect.\n";
    } else {
        cout << "the SPE pipeline produced selected user-function samples.\n";
    }
    cout << string(80, '=') << "\n";
}

static size_t SealWindow(BwContext& ctx, SpeWindowState& windows,
                         int64_t startNs, int64_t endNs,
                         double ddrcRead, double ddrcWrite,
                         const PcieBytes& pcie, bool ddrcReadOk,
                         bool ddrcContinuous, bool pcieAttempted,
                         bool pcieReadOk)
{
    WindowRecord window;
    window.startNs = startNs;
    window.endNs = endNs;
    window.ddrcValid = ddrcReadOk && ddrcContinuous;
    window.speValid = windows.open.valid;
    window.pcieValid = pcieReadOk;
    const bool pcieIncomplete = pcieAttempted && !pcieReadOk;
    window.valid = window.ddrcValid && window.speValid && !pcieIncomplete;
    window.ddrcReadBytes = ddrcRead;
    window.ddrcWriteBytes = ddrcWrite;

    if (ddrcReadOk) {
        ctx.traffic.ddrcReadBytes += ddrcRead;
        ctx.traffic.ddrcWriteBytes += ddrcWrite;
    }
    // An incomplete PCIe snapshot has an unknown DMA component. Keep the DDRC
    // physical total, but do not deduct DMA or attribute that window to functions.
    if (ddrcReadOk && pcieIncomplete) {
        ++ctx.traffic.pcieIncompleteWindows;
    }
    const double observedDmaRead = ddrcReadOk && pcieReadOk ? pcie.txCpl : 0.0;
    const double observedDmaWrite = ddrcReadOk && pcieReadOk ? pcie.rxMwr : 0.0;
    window.observedDmaReadBytes = observedDmaRead;
    window.observedDmaWriteBytes = observedDmaWrite;

    if (ddrcReadOk && pcieReadOk) {
        ++ctx.traffic.pcieWindows;
        ctx.traffic.observedDmaReadBytes += observedDmaRead;
        ctx.traffic.observedDmaWriteBytes += observedDmaWrite;
    }

    // PCIe payload bytes and DDRC bytes are not guaranteed to be one-to-one in every
    // interval. Cap only the amount deducted so the residual cannot become negative.
    const double deductedDmaRead = min(ddrcRead, observedDmaRead);
    const double deductedDmaWrite = min(ddrcWrite, observedDmaWrite);
    window.deductedDmaReadBytes = deductedDmaRead;
    window.deductedDmaWriteBytes = deductedDmaWrite;

    ctx.traffic.deductedDmaReadBytes += deductedDmaRead;
    ctx.traffic.deductedDmaWriteBytes += deductedDmaWrite;

    if (observedDmaRead > deductedDmaRead ||
        observedDmaWrite > deductedDmaWrite) {
        ctx.traffic.dmaDeductionClamped = true;
    }

    const double residualRead = ddrcRead - deductedDmaRead;
    const double residualWrite = ddrcWrite - deductedDmaWrite;
    window.residualReadBytes = ddrcReadOk ? residualRead : 0.0;
    window.residualWriteBytes = ddrcReadOk ? residualWrite : 0.0;

    window.funcReadSamples = move(windows.open.funcReadSamples);
    window.funcWriteSamples = move(windows.open.funcWriteSamples);
    window.targetReadSamples = windows.open.targetReadSamples;
    window.targetWriteSamples = windows.open.targetWriteSamples;
    window.kernelReadSamples = windows.open.kernelReadSamples;
    window.kernelWriteSamples = windows.open.kernelWriteSamples;
    window.systemReadSamples = windows.open.systemReadSamples;
    window.systemWriteSamples = windows.open.systemWriteSamples;
    window.speReadCostNs = windows.openSpeReadCostNs;
    window.speDrains = windows.openSpeDrains;
    ctx.windows.emplace_back(move(window));
    const size_t index = ctx.windows.size() - 1;
    windows.open = {};
    windows.openSpeReadCostNs = 0;
    windows.openSpeDrains = 0;
    windows.openStartNs = endNs;
    return index;
}

struct CollectorSet {
    int spePd = -1;
    int ddrcPd = -1;
    bool ddrcOpened = false;
    bool pcieOpened = false;
    PmuDeviceAttr ddrcAttrs[2] = {};
    vector<PcieCollector> pcieCollectors;
    double ddrcLastReadSec = 0.0;
    bool ddrcPreviousReadOk = true;
    bool pcieIncompleteWarningPrinted = false;
};

static string HexValue(unsigned long value)
{
    ostringstream out;
    out << "0x" << hex << value;
    return out.str();
}

static string SpeDataFilterNames(unsigned long filter)
{
    string result;
    unsigned long known = 0;
    auto append = [&](unsigned long flag, const char* name) {
        known |= flag;
        if ((filter & flag) == 0) return;
        if (!result.empty()) result += '|';
        result += name;
    };
    append(TS_ENABLE, "TS_ENABLE");
    append(PA_ENABLE, "PA_ENABLE");
    append(PCT_ENABLE, "PCT_ENABLE");
    append(JITTER, "JITTER");
    append(BRANCH_FILTER, "BRANCH_FILTER");
    append(LOAD_FILTER, "LOAD_FILTER");
    append(STORE_FILTER, "STORE_FILTER");
    const unsigned long unknown = filter & ~known;
    if (unknown != 0) {
        if (!result.empty()) result += '|';
        result += "UNKNOWN(" + HexValue(unknown) + ')';
    }
    return result.empty() ? "NONE" : result;
}

static string SpeEventFilterNames(unsigned long filter)
{
    string result;
    unsigned long known = 0;
    auto append = [&](unsigned long flag, const char* name) {
        known |= flag;
        if ((filter & flag) == 0) return;
        if (!result.empty()) result += '|';
        result += name;
    };
    append(SPE_EVENT_RETIRED, "SPE_EVENT_RETIRED");
    append(SPE_EVENT_L1DMISS, "SPE_EVENT_L1DMISS");
    append(SPE_EVENT_TLB_WALK, "SPE_EVENT_TLB_WALK");
    append(SPE_EVENT_MISPREDICTED, "SPE_EVENT_MISPREDICTED");
    const unsigned long unknown = filter & ~known;
    if (unknown != 0) {
        if (!result.empty()) result += '|';
        result += "UNKNOWN(" + HexValue(unknown) + ')';
    }
    return result.empty() ? "SPE_EVENT_NONE" : result;
}

static void CleanupCollectors(CollectorSet& set)
{
    if (set.spePd != -1) PmuClose(set.spePd);
    if (set.ddrcPd != -1) PmuClose(set.ddrcPd);
    for (auto& collector : set.pcieCollectors) {
        if (collector.pd != -1) PmuClose(collector.pd);
    }
    set.spePd = set.ddrcPd = -1;
}

static void ClosePcieCollectors(CollectorSet& set, bool disableFirst)
{
    for (auto& collector : set.pcieCollectors) {
        if (collector.pd == -1) {
            continue;
        }
        if (disableFirst) {
            PmuDisable(collector.pd);
        }
        PmuClose(collector.pd);
        collector.pd = -1;
    }
    set.pcieOpened = false;
}

static bool OpenSpeCollector(const ArgsContext& act, CollectorSet& set)
{
    PmuAttr attr = {};
    attr.period = act.period;
    attr.dataFilter = static_cast<SpeFilter>(LOAD_FILTER | STORE_FILTER | TS_ENABLE | JITTER);
    attr.evFilter = SPE_EVENT_RETIRED;
    attr.minLatency = act.minLatency;
    attr.symbolMode = RESOLVE_ELF;
    attr.excludeKernel = false;
    attr.pidList = nullptr;
    attr.numPid = 0;
    // System-wide collection cannot use target-bound enable_on_exec. The child
    // remains blocked until all collectors are enabled and baselined below.
    attr.enableOnExec = 0;
    set.spePd = PmuOpen(SPE_SAMPLING, &attr);
    if (set.spePd == -1) {
        cerr << "PmuOpen SPE_SAMPLING failed: " << Perror() << "\n";
        return false;
    }
    return true;
}

static vector<string> ResolvePcieBdfs(const ArgsContext& act)
{
    vector<string> bdfs = act.pcieBdfs;
    bool explicitBdfs = !bdfs.empty();
    if (bdfs.empty()) {
        unsigned len = 0;
        const char** list = PmuDeviceBdfList(PMU_BDF_TYPE_PCIE, &len);
        for (unsigned i = 0; list != nullptr && i < len; ++i) {
            if (list[i] != nullptr) {
                bdfs.emplace_back(list[i]);
            }
        }
    }
    sort(bdfs.begin(), bdfs.end());
    bdfs.erase(unique(bdfs.begin(), bdfs.end()), bdfs.end());
    if (!explicitBdfs) {
        bdfs.erase(remove_if(bdfs.begin(), bdfs.end(), IsPciBridge), bdfs.end());
    }
    return bdfs;
}

static void OpenHardwareCollectors(const ArgsContext& act, CollectorSet& set)
{
    set.ddrcAttrs[0].metric = PMU_DDR_READ_BW;
    set.ddrcAttrs[1].metric = PMU_DDR_WRITE_BW;
    set.ddrcPd = PmuDeviceOpen(set.ddrcAttrs, 2);
    set.ddrcOpened = set.ddrcPd != -1;
    if (!set.ddrcOpened) {
        cerr << "Warning: DDRC PMU unavailable (" << Perror() << "); only SPE sample counts will be available.\n";
    }

    vector<string> bdfs = ResolvePcieBdfs(act);
    for (const auto& bdf : bdfs) {
        PcieCollector collector;
        string error;
        if (OpenPcieCollector(bdf, collector, error)) {
            set.pcieCollectors.push_back(move(collector));
        } else {
            cerr << "Warning: unable to open raw PCIe payload events for BDF " << bdf << ": " << error << ".\n";
        }
    }
    if (bdfs.empty()) {
        cerr << "Warning: no PCIe endpoint BDF was discovered; DMA traffic will not be deducted.\n";
    } else if (set.pcieCollectors.empty()) {
        cerr << "Warning: no PCIe BDF is collectable; DMA traffic will not be deducted.\n";
    } else if (set.pcieCollectors.size() != bdfs.size()) {
        cerr << "Warning: PCIe PMU is available for only " << set.pcieCollectors.size()
             << " of " << bdfs.size() << " requested BDFs; partial DMA data will not be deducted.\n";
        ClosePcieCollectors(set, false);
    } else {
        set.pcieOpened = true;
    }
}

static bool EnableCollectors(CollectorSet& set)
{
    if (set.ddrcOpened && PmuEnable(set.ddrcPd) != 0) {
        cerr << "Warning: failed to enable DDRC PMU (" << Perror() << "); only SPE sample counts will be available.\n";
        PmuClose(set.ddrcPd);
        set.ddrcPd = -1;
        set.ddrcOpened = false;
    }
    unsigned enabled = 0;
    if (set.pcieOpened) {
        for (auto& collector : set.pcieCollectors) {
            if (PmuEnable(collector.pd) != 0) {
                cerr << "Warning: failed to enable PCIe PMU for BDF " << collector.bdf << " (" << Perror() << ").\n";
                PmuClose(collector.pd);
                collector.pd = -1;
            } else {
                ++enabled;
            }
        }
        if (enabled != set.pcieCollectors.size()) {
            ClosePcieCollectors(set, true);
            cerr << "Warning: not all PCIe PMUs could be enabled; partial DMA data will not be deducted.\n";
        }
    }
    if (PmuEnable(set.spePd) != 0) {
        cerr << "PmuEnable SPE_SAMPLING failed: " << Perror() << "\n";
        return false;
    }
    return true;
}

static bool PrimeCollectors(CollectorSet& set)
{
    string error;
    double baselineSec = 0.0;
    if (!PrimePmuRead(set.spePd, baselineSec, error)) {
        cerr << "SPE baseline read failed: " << error << "\n";
        return false;
    }
    error.clear();

    unsigned primed = 0;
    for (auto& collector : set.pcieCollectors) {
        if (collector.pd == -1) continue;
        if (PrimePmuRead(collector.pd, collector.lastReadSec, error)) {
            collector.synchronized = true;
            ++primed;
        } else {
            cerr << "Warning: PCIe baseline read failed for BDF " << collector.bdf << " (" << error << ").\n";
            PmuClose(collector.pd);
            collector.pd = -1;
        }
        error.clear();
    }
    if (set.pcieOpened && primed != set.pcieCollectors.size()) {
        ClosePcieCollectors(set, true);
        cerr << "Warning: not all PCIe PMUs passed the baseline read; partial DMA data will not be deducted.\n";
    }

    if (set.ddrcOpened &&
        !PrimePmuRead(set.ddrcPd, set.ddrcLastReadSec, error)) {
        cerr << "Warning: DDRC baseline read failed (" << error << "); only SPE sample counts will be available.\n";
        PmuClose(set.ddrcPd);
        set.ddrcPd = -1;
        set.ddrcOpened = false;
    }
    return true;
}

static bool InitializeCollectors(const ArgsContext& act, CollectorSet& set)
{
    if (!OpenSpeCollector(act, set)) {
        return false;
    }
    OpenHardwareCollectors(act, set);
    return EnableCollectors(set) && PrimeCollectors(set);
}

static bool ReleaseTarget(ArgsContext& act)
{
    if (!act.isLaunch) return true;
    int ret = write(act.fd[1], "data", 4);
    close(act.fd[1]);
    act.fd[1] = -1;
    if (ret != 4) {
        cerr << "Failed to release target process.\n";
        return false;
    }
    return true;
}

static void InvalidateSpeRange(BwContext& ctx, SpeWindowState& windows,
                               int64_t beginNs, int64_t endNs)
{
    for (auto& window : ctx.windows) {
        if (window.endNs > beginNs && window.startNs < endNs) {
            window.speValid = false;
            window.valid = false;
        }
    }
    if (endNs > windows.openStartNs) {
        windows.open.valid = false;
    }
}

static void CollectSpeSlice(const ArgsContext& act, CollectorSet& set, BwContext& ctx,
                            SpeWindowState& windows, CollectionHealth* health,
                            WindowRecord* drainWindow = nullptr)
{
    PmuData* speData = nullptr;
    const int64_t readStartNs = NowNs();
    int speLen = PmuRead(set.spePd, &speData);
    const int64_t readEndNs = NowNs();
    const double readCost = static_cast<double>(readEndNs - readStartNs) / 1e9;
    const uint64_t readCostNs = static_cast<uint64_t>(
        max<int64_t>(0, readEndNs - readStartNs));
    if (drainWindow != nullptr) {
        drainWindow->speReadCostNs += readCostNs;
        ++drainWindow->speDrains;
    } else {
        windows.openSpeReadCostNs += readCostNs;
        ++windows.openSpeDrains;
    }
    // PmuRead may return zero while Perrorno still reports a backend failure.
    if (speLen < 0 || Perrorno() != 0) {
        speLen = -1;
        if (windows.failedReadStartNs == 0) {
            windows.failedReadStartNs = windows.lastReadEndNs > 0
                ? windows.lastReadEndNs : windows.collectionStartNs;
        }
        InvalidateSpeRange(ctx, windows, windows.failedReadStartNs, readEndNs);
    } else {
        if (windows.failedReadStartNs > 0) {
            InvalidateSpeRange(ctx, windows, windows.failedReadStartNs, readEndNs);
            windows.failedReadStartNs = 0;
        }
        windows.lastReadEndNs = readEndNs;
    }

    if (health != nullptr) {
        health->speReadCostSec += readCost;
        health->maxSpeReadCostSec = max(health->maxSpeReadCostSec, readCost);
        ++health->speDrains;
        if (speLen > 0 && speData != nullptr) {
            health->speRecords += static_cast<uint64_t>(speLen);
            health->maxSpeRecordsPerDrain = max(
                health->maxSpeRecordsPerDrain, static_cast<uint64_t>(speLen));
        } else if (speLen < 0) {
            ++health->speFailedDrains;
        } else {
            ++health->speEmptyDrains;
        }
    }

    if (speLen > 0 && speData != nullptr) {
        ProcessSpeChunk(speData, speLen, ctx, act.pids, windows, readEndNs);
    } else if (speLen < 0) {
        cerr << "Warning: SPE read failed: " << Perror() << "\n";
    }

    if (speData != nullptr) {
        PmuDataFree(speData);
    }
}

static void CollectCounterWindow(const ArgsContext& act, CollectorSet& set, BwContext& ctx,
                                 SpeWindowState& windows, int64_t& lastCollectNs,
                                 CollectionHealth* health, bool finalWindow = false)
{
    // Snapshot counters before expensive SPE decoding/symbol resolution. This
    // reduces boundary skew; the independent PMUs still are not atomic snapshots.
    double ddrcRead = 0.0;
    double ddrcWrite = 0.0;
    const bool ddrcAttempted = set.ddrcOpened;
    const int64_t ddrcReadStartNs = NowNs();
    bool ddrcOk = set.ddrcOpened &&
        ReadDdrcInterval(set.ddrcPd, set.ddrcAttrs, 2, ddrcRead, ddrcWrite);
    const int64_t ddrcReadEndNs = NowNs();
    int64_t boundaryNs = ddrcReadStartNs +
        (ddrcReadEndNs - ddrcReadStartNs) / 2;
    boundaryNs = max(boundaryNs, lastCollectNs + 1);
    const double boundarySec = static_cast<double>(boundaryNs) / 1e9;
    if (ddrcOk) {
        if (set.ddrcLastReadSec > 0.0 && boundarySec > set.ddrcLastReadSec) {
            ctx.traffic.ddrcElapsedSec += boundarySec - set.ddrcLastReadSec;
        }
        set.ddrcLastReadSec = boundarySec;
    }
    // A recovered cumulative counter cannot be matched to only this window's SPE.
    const bool ddrcContinuous = set.ddrcPreviousReadOk;
    set.ddrcPreviousReadOk = ddrcOk;

    PcieBytes pcie;
    const bool pcieAttempted = set.pcieOpened;
    PcieReadResult pcieResult;
    if (pcieAttempted) {
        pcieResult = ReadPcieInterval(
            set.pcieCollectors, ctx, pcie, finalWindow ? boundarySec : 0.0);
        if (!pcieResult.Complete() && !set.pcieIncompleteWarningPrinted) {
            cerr << "Warning: incomplete PCIe counter window (successful="
                 << pcieResult.succeeded << ", failed=" << pcieResult.failed
                 << ", resynchronized=" << pcieResult.resynchronized
                 << "); DMA deduction and function attribution are skipped";
            if (!pcieResult.incompleteBdfs.empty()) {
                cerr << " for BDF(s):";
                for (const auto& bdf : pcieResult.incompleteBdfs) {
                    cerr << " " << bdf;
                }
            }
            cerr << ".\n";
            set.pcieIncompleteWarningPrinted = true;
        }
    }
    const bool pcieOk = pcieAttempted && pcieResult.Complete();
    if (finalWindow) {
        windows.finalEndNs = boundaryNs;
        PmuDisable(set.spePd);
    }

    const size_t windowIndex = SealWindow(
        ctx, windows, lastCollectNs, boundaryNs, ddrcRead, ddrcWrite,
        pcie, ddrcOk, ddrcContinuous, pcieAttempted, pcieOk);
    CollectSpeSlice(act, set, ctx, windows, health, &ctx.windows[windowIndex]);
    if (finalWindow) {
        // SPE EndRead enables sampling, even when the caller disabled it first.
        PmuDisable(set.spePd);
    }
    AccountWindowHealth(health, ddrcAttempted,
                        ctx.windows[windowIndex].ddrcValid,
                        pcieAttempted, pcieResult);
    lastCollectNs = boundaryNs;
}

static bool TargetExited(const ArgsContext& act)
{
    if (act.systemWide) return false;
    if (act.isLaunch) {
        int status = 0;
        return waitpid(act.pid, &status, WNOHANG) == act.pid;
    }
    return all_of(act.pids.begin(), act.pids.end(), [](int pid) {
        return kill(pid, 0) != 0 && errno == ESRCH;
    });
}

static void DisableCollectors(CollectorSet& set)
{
    if (set.spePd != -1) PmuDisable(set.spePd);
    if (set.ddrcOpened) PmuDisable(set.ddrcPd);
    if (set.pcieOpened) {
        for (auto& collector : set.pcieCollectors) {
            if (collector.pd != -1) PmuDisable(collector.pd);
        }
    }
}

static bool SleepUntil(double targetSec, volatile sig_atomic_t* stopRequested)
{
    while (true) {
        if (stopRequested != nullptr && *stopRequested) {
            return false;
        }
        const double remainingSec = targetSec - NowSec();
        if (remainingSec <= 0.0) {
            return true;
        }
        usleep(static_cast<useconds_t>(min(500000.0, max(1.0, remainingSec * 1e6))));
    }
}

int RunDdrBandwidthCollection(ArgsContext& act, volatile sig_atomic_t* stopRequested, volatile int* execErrNo)
{
    if (act.duration <= 0 || act.interval <= 0 || act.speReadInterval <= 0) {
        cerr << "Error: collection duration, interval, and SPE read interval must be positive.\n";
        return 1;
    }

    ConfigureSpeDebug(act.debug);
    CollectorSet collectors;
    if (!InitializeCollectors(act, collectors)) {
        CleanupCollectors(collectors);
        return 1;
    }

    BwContext ctx;
    CollectionHealth health;
    CollectionHealth* healthPtr = &health;

    // The synchronization protocol is opt-in and used only by the dedicated
    // exact-window validation demo. Do not tie this to --debug: sending
    // SIGUSR1/SIGUSR2 to an arbitrary application would be unsafe.
    const bool exactStoreSync = ExactStoreSyncEnabled() &&
        !act.isLaunch && !act.systemWide;

    const int64_t startNs = NowNs();
    const double startSec = static_cast<double>(startNs) / 1e9;
    if (exactStoreSync) {
        const unsigned signaled =
            SignalTargets(act.pids, SIGUSR1, "SIGUSR1(start)");
        if (signaled == 0) {
            cerr << "Warning: exact-store sync enabled, but no target PID "
                    "accepted SIGUSR1.\n";
        }
    }

    int64_t lastCollectNs = startNs;
    const double endSec = startSec + act.duration;
    const double intervalSec = act.interval / 1000.0;
    const double speIntervalSec = min(act.speReadInterval, act.interval) / 1000.0;
    double nextWindowSec = min(startSec + intervalSec, endSec);
    double nextSpeReadSec = min(startSec + speIntervalSec, nextWindowSec);
    SpeWindowState speWindows;
    speWindows.collectionStartNs = startNs;
    speWindows.openStartNs = startNs;
    speWindows.lastReadEndNs = startNs;
    if (!ReleaseTarget(act)) {
        CleanupCollectors(collectors);
        return 1;
    }

    while (stopRequested == nullptr || !*stopRequested) {
        if (!SleepUntil(nextSpeReadSec, stopRequested)) break;
        if (execErrNo != nullptr && *execErrNo != 0) {
            cerr << "exec failed: " << strerror(*execErrNo) << "\n";
            CleanupCollectors(collectors);
            return 1;
        }
        const double now = NowSec();
        if (now >= endSec || TargetExited(act)) break;
        if (now >= nextWindowSec) {
            CollectCounterWindow(act, collectors, ctx, speWindows, lastCollectNs, healthPtr);
            // Skip deadlines missed during collection: never manufacture a burst
            // of tiny catch-up windows from an already drained SPE buffer.
            nextWindowSec = min(NextCollectionDeadline(startSec, intervalSec, NowSec()), endSec);
        } else {
            CollectSpeSlice(act, collectors, ctx, speWindows, healthPtr);
        }
        nextSpeReadSec = min(NextCollectionDeadline(startSec, speIntervalSec, NowSec()), nextWindowSec);
    }

    if (exactStoreSync) {
        SignalTargets(act.pids, SIGUSR2, "SIGUSR2(stop)");
    }
    // Snapshot DDRC first, then stop and drain SPE. Samples at or beyond the
    // final DDRC boundary are rejected by the half-open window rule.
    CollectCounterWindow(act, collectors, ctx, speWindows, lastCollectNs, healthPtr, true);
    const double disableStartSec = NowSec();
    DisableCollectors(collectors);
    const double disableCostSec = NowSec() - disableStartSec;
    const int64_t stopNs = lastCollectNs;
    const double stopSec = static_cast<double>(stopNs) / 1e9;
    CollectionTiming timing;
    timing.requestedSec = static_cast<double>(act.duration);
    timing.startNs = startNs;
    timing.endNs = stopNs;
    timing.startSec = startSec;
    timing.lastCollectSec = stopSec;
    timing.endSec = stopSec;
    timing.durationSec = max(stopSec - startSec, 1e-9);
    timing.disableCostSec = disableCostSec;

    if (!ctx.windows.empty()) FinalizeAttribution(ctx, act.minSamples);
    RefreshSpeWindowHealth(health, ctx);
    PrintSummary(ctx, collectors.ddrcOpened, collectors.pcieOpened, timing, act);
    if (act.debug) {
        PrintSpePipelineDiagnostics(health, ctx, act);
        PrintCollectionHealth(health, act, timing.durationSec);
        PrintSpeEventDebug();
    }
    CleanupCollectors(collectors);
    return 0;
}
