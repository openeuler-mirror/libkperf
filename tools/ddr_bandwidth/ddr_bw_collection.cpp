#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <limits>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "pcerrc.h"
#include "ddr_bw_collection.h"
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
                 << " to pid " << pid << ": " << strerror(errno) << "\\n";
        }
    }
    return signaled;
}

void ConfigureSpeDebug(bool enabled);
void PrintSpeStorePcDebug();

struct CollectionHealth {
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

    uint64_t readEvidenceWindows = 0;
    uint64_t writeEvidenceWindows = 0;
    uint64_t insufficientReadWindows = 0;
    uint64_t insufficientWriteWindows = 0;
    uint64_t noUsableEvidenceWindows = 0;
    uint64_t zeroEvidenceWindows = 0;
    uint64_t totalReadSamples = 0;
    uint64_t totalWriteSamples = 0;
    uint64_t minReadSamples = numeric_limits<uint64_t>::max();
    uint64_t maxReadSamples = 0;
    uint64_t minWriteSamples = numeric_limits<uint64_t>::max();
    uint64_t maxWriteSamples = 0;
};

static void AccountWindowHealth(CollectionHealth* health,
                                const IntervalWeights& weights,
                                bool ddrcAttempted, bool ddrcOk,
                                bool pcieAttempted, bool pcieOk)
{
    if (health == nullptr) return;

    ++health->counterWindows;
    if (ddrcAttempted) {
        ++health->ddrcAttemptedWindows;
        if (ddrcOk) ++health->ddrcValidWindows;
    }
    if (pcieAttempted) {
        ++health->pcieAttemptedWindows;
        if (pcieOk) ++health->pcieValidWindows;
    }

    const bool readUsable =
        weights.totalReadSamples >= MIN_READ_SAMPLES_PER_WINDOW;
    const bool writeUsable =
        weights.totalWriteSamples >= MIN_WRITE_SAMPLES_PER_WINDOW;
    if (readUsable) {
        ++health->readEvidenceWindows;
    } else if (weights.totalReadSamples > 0) {
        ++health->insufficientReadWindows;
    }
    if (writeUsable) {
        ++health->writeEvidenceWindows;
    } else if (weights.totalWriteSamples > 0) {
        ++health->insufficientWriteWindows;
    }
    if (!readUsable && !writeUsable) ++health->noUsableEvidenceWindows;
    if (weights.totalReadSamples == 0 && weights.totalWriteSamples == 0) {
        ++health->zeroEvidenceWindows;
    }

    health->totalReadSamples += weights.totalReadSamples;
    health->totalWriteSamples += weights.totalWriteSamples;
    health->minReadSamples = min(health->minReadSamples, weights.totalReadSamples);
    health->maxReadSamples = max(health->maxReadSamples, weights.totalReadSamples);
    health->minWriteSamples = min(health->minWriteSamples, weights.totalWriteSamples);
    health->maxWriteSamples = max(health->maxWriteSamples, weights.totalWriteSamples);
}

static void PrintCollectionHealth(const CollectionHealth& health)
{
    const double avgDrain = health.speDrains > 0
        ? static_cast<double>(health.speRecords) / static_cast<double>(health.speDrains)
        : 0.0;
    const double avgRead = health.counterWindows > 0
        ? static_cast<double>(health.totalReadSamples) / static_cast<double>(health.counterWindows)
        : 0.0;
    const double avgWrite = health.counterWindows > 0
        ? static_cast<double>(health.totalWriteSamples) / static_cast<double>(health.counterWindows)
        : 0.0;

    const uint64_t minRead = health.counterWindows > 0 ? health.minReadSamples : 0;
    const uint64_t minWrite = health.counterWindows > 0 ? health.minWriteSamples : 0;

    cout << "\n" << string(80, '=') << "\n"
         << "DEBUG SPE / WINDOW HEALTH\n"
         << string(80, '=') << "\n"
         << "SPE drains: total=" << health.speDrains
         << " empty=" << health.speEmptyDrains
         << " failed=" << health.speFailedDrains
         << " records=" << health.speRecords
         << " avg_records/drain=" << fixed << setprecision(2) << avgDrain
         << " max_records/drain=" << health.maxSpeRecordsPerDrain << "\n"
         << "Counter windows: total=" << health.counterWindows
         << " DDRC=" << health.ddrcValidWindows << "/" << health.ddrcAttemptedWindows
         << " PCIe=" << health.pcieValidWindows << "/" << health.pcieAttemptedWindows << "\n"
         << "SPE evidence windows: read=" << health.readEvidenceWindows
         << " write=" << health.writeEvidenceWindows
         << " no-usable=" << health.noUsableEvidenceWindows
         << " zero=" << health.zeroEvidenceWindows << "\n"
         << "Insufficient-sample windows: read=" << health.insufficientReadWindows
         << " write=" << health.insufficientWriteWindows << "\n"
         << "Read samples/window: min=" << minRead
         << " avg=" << avgRead
         << " max=" << health.maxReadSamples << "\n"
         << "Write samples/window: min=" << minWrite
         << " avg=" << avgWrite
         << " max=" << health.maxWriteSamples << "\n";

    if (health.speFailedDrains > 0 ||
        health.ddrcValidWindows < health.ddrcAttemptedWindows) {
        cout << "Health status: WARN (collector read failures detected)\n";
    } else if (health.speDrains > 0 &&
               health.speEmptyDrains * 10 > health.speDrains) {
        cout << "Health status: WARN (>10% SPE drains were empty)\n";
    } else if (health.counterWindows > 0 &&
               health.noUsableEvidenceWindows * 2 > health.counterWindows) {
        cout << "Health status: WARN (>50% windows had no usable SPE attribution evidence)\n";
    } else {
        cout << "Health status: OK\n";
    }
    cout << "Note: this health block detects empty/failed reads and evidence gaps; "
            "it does not claim hardware AUX-loss detection.\n"
         << string(80, '=') << "\n";
}


static void AttributeReadWindow(BwContext& ctx, const IntervalWeights& weights,
                                double residualRead)
{
    if (residualRead <= 0.0 ||
        weights.totalReadSamples < MIN_READ_SAMPLES_PER_WINDOW) {
        return;
    }

    double attributedInWindow = 0.0;
    for (const auto& entry : weights.funcReadSamples) {
        if (entry.second == 0) continue;

        const double bytes = residualRead * static_cast<double>(entry.second) /
            static_cast<double>(weights.totalReadSamples);
        ctx.funcStats[entry.first].attributedReadBytes += bytes;
        ctx.attribution.readBytes += bytes;
        attributedInWindow += bytes;
    }

    if (attributedInWindow > 0.0) ++ctx.attribution.readWindows;
}

static void AttributeWriteWindow(BwContext& ctx, const IntervalWeights& weights,
                                 double residualWrite)
{
    if (residualWrite <= 0.0 ||
        weights.totalWriteSamples < MIN_WRITE_SAMPLES_PER_WINDOW) {
        return;
    }

    double attributedInWindow = 0.0;
    for (const auto& entry : weights.funcWriteSamples) {
        if (entry.second == 0) continue;

        const double bytes = residualWrite * static_cast<double>(entry.second) /
            static_cast<double>(weights.totalWriteSamples);
        ctx.funcStats[entry.first].attributedWriteBytes += bytes;
        ctx.attribution.writeBytes += bytes;
        attributedInWindow += bytes;
    }

    if (attributedInWindow > 0.0) ++ctx.attribution.writeWindows;
}

static void AccountWindow(BwContext& ctx, const IntervalWeights& weights,
                          double ddrcRead, double ddrcWrite,
                          const PcieBytes& pcie, bool ddrcReadOk, bool pcieReadOk)
{
    if (!ddrcReadOk) {
        return;
    }

    ctx.traffic.ddrcReadBytes += ddrcRead;
    ctx.traffic.ddrcWriteBytes += ddrcWrite;
    ++ctx.traffic.ddrcWindows;
    // Keep current PCIe behavior unchanged: no running-ratio or partial-read gating.
    const double observedDmaRead = pcieReadOk ? pcie.txCpl : 0.0;
    const double observedDmaWrite = pcieReadOk ? pcie.rxMwr : 0.0;

    if (pcieReadOk) {
        ++ctx.traffic.pcieWindows;
        ctx.traffic.observedDmaReadBytes += observedDmaRead;
        ctx.traffic.observedDmaWriteBytes += observedDmaWrite;
    }

    // PCIe payload bytes and DDRC bytes are not guaranteed to be one-to-one in every
    // interval. Cap only the amount deducted so the residual cannot become negative.
    const double deductedDmaRead = min(ddrcRead, observedDmaRead);
    const double deductedDmaWrite = min(ddrcWrite, observedDmaWrite);

    ctx.traffic.deductedDmaReadBytes += deductedDmaRead;
    ctx.traffic.deductedDmaWriteBytes += deductedDmaWrite;

    if (observedDmaRead > deductedDmaRead ||
        observedDmaWrite > deductedDmaWrite) {
        ctx.traffic.dmaDeductionClamped = true;
    }

    const double residualRead = ddrcRead - deductedDmaRead;
    const double residualWrite = ddrcWrite - deductedDmaWrite;

    // Attribute immediately while the DDRC, PCIe and SPE data still refer to
    // the same interval. This keeps memory bounded regardless of run length.
    AttributeReadWindow(ctx, weights, residualRead);
    AttributeWriteWindow(ctx, weights, residualWrite);
}

struct CollectorSet {
    int spePd = -1;
    int ddrcPd = -1;
    vector<int> spePids;
    bool ddrcOpened = false;
    bool pcieOpened = false;
    PmuDeviceAttr ddrcAttrs[2] = {};
    vector<PcieCollector> pcieCollectors;
    double ddrcLastReadSec = 0.0;
};

static void CleanupCollectors(CollectorSet& set)
{
    if (set.spePd != -1) PmuClose(set.spePd);
    if (set.ddrcPd != -1) PmuClose(set.ddrcPd);
    for (auto& collector : set.pcieCollectors) {
        if (collector.pd != -1) PmuClose(collector.pd);
    }
    set.spePd = set.ddrcPd = -1;
}

static bool OpenSpeCollector(const ArgsContext& act, CollectorSet& set)
{
    PmuAttr attr = {};
    set.spePids = act.pids;
    attr.period = act.period;
    attr.dataFilter = static_cast<SpeFilter>(LOAD_FILTER | STORE_FILTER | TS_ENABLE | JITTER);
    attr.evFilter = SPE_EVENT_RETIRED;
    attr.minLatency = act.minLatency;
    attr.symbolMode = RESOLVE_ELF;
    attr.excludeKernel = true;
    attr.pidList = set.spePids.data();
    attr.numPid = set.spePids.size();
    attr.enableOnExec = act.isLaunch ? 1 : 0;
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
        cerr << "Warning: DDRC PMU unavailable (" << Perror() << "); using SPE-only statistical estimation.\n";
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
    set.pcieOpened = !set.pcieCollectors.empty();
    if (bdfs.empty()) {
        cerr << "Warning: no PCIe endpoint BDF was discovered; DMA traffic will not be deducted.\n";
    } else if (!set.pcieOpened) {
        cerr << "Warning: no PCIe BDF is collectable; DMA traffic will not be deducted.\n";
    } else if (set.pcieCollectors.size() != bdfs.size()) {
        cerr << "Warning: PCIe PMU is partially available; only " << set.pcieCollectors.size() << " of " << bdfs.size()
             << " requested BDFs will be deducted.\n";
    }
}

static bool EnableCollectors(const ArgsContext& act, CollectorSet& set)
{
    if (set.ddrcOpened && PmuEnable(set.ddrcPd) != 0) {
        cerr << "Warning: failed to enable DDRC PMU (" << Perror() << "); using SPE-only statistical estimation.\n";
        PmuClose(set.ddrcPd);
        set.ddrcPd = -1;
        set.ddrcOpened = false;
    }
    unsigned enabled = 0;
    for (auto& collector : set.pcieCollectors) {
        if (PmuEnable(collector.pd) != 0) {
            cerr << "Warning: failed to enable PCIe PMU for BDF " << collector.bdf << " (" << Perror() << ").\n";
            PmuClose(collector.pd);
            collector.pd = -1;
        } else {
            ++enabled;
        }
    }
    set.pcieOpened = enabled > 0;
    if (!set.pcieCollectors.empty() && !set.pcieOpened) {
        cerr << "Warning: no PCIe PMU could be enabled; DMA traffic will not be deducted.\n";
    }
    if (!act.isLaunch && PmuEnable(set.spePd) != 0) {
        cerr << "PmuEnable SPE_SAMPLING failed: " << Perror() << "\n";
        return false;
    }
    return true;
}

static bool PrimeCollectors(const ArgsContext& act, CollectorSet& set)
{
    string error;
    unsigned primed = 0;
    for (auto& collector : set.pcieCollectors) {
        if (collector.pd == -1) continue;
        if (PrimePmuRead(collector.pd, collector.lastReadSec, error)) {
            ++primed;
        } else {
            cerr << "Warning: PCIe baseline read failed for BDF " << collector.bdf << " (" << error << ").\n";
            PmuClose(collector.pd);
            collector.pd = -1;
        }
        error.clear();
    }
    if (set.pcieOpened) set.pcieOpened = primed > 0;
    if (!set.pcieCollectors.empty() && !set.pcieOpened) {
        cerr << "Warning: no PCIe PMU passed the baseline read; DMA traffic will not be deducted.\n";
    }

    if (set.ddrcOpened &&
        !PrimePmuRead(set.ddrcPd, set.ddrcLastReadSec, error)) {
        cerr << "Warning: DDRC baseline read failed (" << error << "); using SPE-only statistical estimation.\n";
        PmuClose(set.ddrcPd);
        set.ddrcPd = -1;
        set.ddrcOpened = false;
    }
    if (!act.isLaunch) {
        double baselineSec = 0.0;
        error.clear();
        if (!PrimePmuRead(set.spePd, baselineSec, error)) {
            cerr << "SPE baseline read failed: " << error << "\n";
            return false;
        }
    }
    return true;
}

static bool InitializeCollectors(const ArgsContext& act, CollectorSet& set)
{
    if (!OpenSpeCollector(act, set)) {
        return false;
    }
    OpenHardwareCollectors(act, set);
    return EnableCollectors(act, set) && PrimeCollectors(act, set);
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

static void CollectSpeSlice(const ArgsContext& act, CollectorSet& set, BwContext& ctx,
                            IntervalWeights& weights, CollectionHealth* health)
{
    PmuData* speData = nullptr;
    int speLen = PmuRead(set.spePd, &speData);

    if (health != nullptr) {
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
        ProcessSpeChunk(speData, speLen, ctx, act.pids, weights);
    } else if (speLen < 0) {
        cerr << "Warning: SPE read failed: " << Perror() << "\n";
    }

    if (speData != nullptr) {
        PmuDataFree(speData);
    }
}

static void CollectCounterWindow(CollectorSet& set, BwContext& ctx,
                                 const IntervalWeights& weights,
                                 double& lastCollectSec,
                                 CollectionHealth* health)
{
    double ddrcRead = 0.0;
    double ddrcWrite = 0.0;
    const bool ddrcAttempted = set.ddrcOpened;
    bool ddrcOk = set.ddrcOpened &&
        ReadDdrcInterval(set.ddrcPd, set.ddrcAttrs, 2, ddrcRead, ddrcWrite);
    if (ddrcOk) {
        double readSec = NowSec();
        if (set.ddrcLastReadSec > 0.0 && readSec > set.ddrcLastReadSec) {
            ctx.traffic.ddrcElapsedSec += readSec - set.ddrcLastReadSec;
        }
        set.ddrcLastReadSec = readSec;
    }

    PcieBytes pcie;
    const bool pcieAttempted = set.pcieOpened;
    bool pcieOk = set.pcieOpened && ReadPcieInterval(set.pcieCollectors, ctx, pcie);

    AccountWindowHealth(health, weights, ddrcAttempted, ddrcOk,
                        pcieAttempted, pcieOk);
    AccountWindow(ctx, weights, ddrcRead, ddrcWrite, pcie, ddrcOk, pcieOk);
    lastCollectSec = NowSec();
}

static bool TargetExited(const ArgsContext& act)
{
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
        // Keep the conversion within useconds_t and re-check stopRequested at
        // least once per second, even for a very large --interval value.
        const double sleepSec = min(remainingSec, 1.0);
        usleep(static_cast<useconds_t>(max(1.0, sleepSec * 1e6)));
    }
}

int RunDdrBandwidthCollection(ArgsContext& act, volatile sig_atomic_t* stopRequested, volatile int* execErrNo)
{
    constexpr uint64_t SPE_READ_INTERVAL_MS = 10;
    constexpr double MIN_FINAL_WINDOW_SEC = 0.01;

    // ParseArgs validates CLI input, but this function is also a public entry
    // point and must not rely on every caller going through the CLI parser.
    if (act.duration <= 0 || act.interval <= 0) {
        cerr << "Error: collection duration and interval must be positive.\n";
        return 1;
    }

    CollectorSet collectors;
    if (!InitializeCollectors(act, collectors)) {
        CleanupCollectors(collectors);
        return 1;
    }

    BwContext ctx;
    CollectionHealth health;
    CollectionHealth* healthPtr = act.debug ? &health : nullptr;
    ConfigureSpeDebug(act.debug);

    // The synchronization protocol is opt-in and used only by the dedicated
    // exact-window validation demo. Do not tie this to --debug: sending
    // SIGUSR1/SIGUSR2 to an arbitrary application would be unsafe.
    const bool exactStoreSync = ExactStoreSyncEnabled() && !act.isLaunch;
    bool exactStoreStopped = false;

    const double startSec = NowSec();
    if (exactStoreSync) {
        const unsigned signaled =
            SignalTargets(act.pids, SIGUSR1, "SIGUSR1(start)");
        if (signaled == 0) {
            cerr << "Warning: exact-store sync enabled, but no target PID "
                    "accepted SIGUSR1.\n";
        }
    }

    double lastCollectSec = startSec;
    const uint64_t durationMs = static_cast<uint64_t>(act.duration) * 1000;
    const uint64_t intervalMs = static_cast<uint64_t>(act.interval);
    const uint64_t scheduledWindows = (durationMs + intervalMs - 1) / intervalMs;
    uint64_t windowIndex = 1;
    bool completedSchedule = false;
    IntervalWeights windowWeights;
    if (!ReleaseTarget(act)) {
        CleanupCollectors(collectors);
        return 1;
    }

    while (windowIndex <= scheduledWindows &&
           (stopRequested == nullptr || !*stopRequested)) {
        const uint64_t windowStartMs = (windowIndex - 1) * intervalMs;
        const uint64_t windowEndMs = min(windowIndex * intervalMs, durationMs);
        uint64_t nextSpeReadMs = min(windowStartMs + SPE_READ_INTERVAL_MS, windowEndMs);
        bool windowInterrupted = false;

        while (true) {
            const double nextSpeReadSec =
                startSec + static_cast<double>(nextSpeReadMs) / 1000.0;
            if (!SleepUntil(nextSpeReadSec, stopRequested)) {
                windowInterrupted = true;
                break;
            }
            if (execErrNo != nullptr && *execErrNo != 0) {
                cerr << "exec failed: " << strerror(*execErrNo) << "\n";
                CleanupCollectors(collectors);
                return 1;
            }

            CollectSpeSlice(act, collectors, ctx, windowWeights, healthPtr);

            if (exactStoreSync && !exactStoreStopped &&
                windowIndex >= scheduledWindows &&
                nextSpeReadMs >= durationMs) {
                SignalTargets(act.pids, SIGUSR2, "SIGUSR2(stop)");
                exactStoreStopped = true;
            }

            if (nextSpeReadMs >= windowEndMs) {
                break;
            }
            if (TargetExited(act)) {
                windowInterrupted = true;
                break;
            }
            nextSpeReadMs = min(nextSpeReadMs + SPE_READ_INTERVAL_MS, windowEndMs);
        }

        if (windowInterrupted) {
            break;
        }

        CollectCounterWindow(collectors, ctx, windowWeights, lastCollectSec, healthPtr);
        windowWeights = {};

        if (windowIndex >= scheduledWindows) {
            completedSchedule = true;
            break;
        }
        if (TargetExited(act)) {
            break;
        }
        ++windowIndex;
    }

    if (!completedSchedule && NowSec() - lastCollectSec > MIN_FINAL_WINDOW_SEC) {
        CollectSpeSlice(act, collectors, ctx, windowWeights, healthPtr);
        CollectCounterWindow(collectors, ctx, windowWeights, lastCollectSec, healthPtr);
    }

    if (exactStoreSync && !exactStoreStopped) {
        SignalTargets(act.pids, SIGUSR2, "SIGUSR2(stop)");
        exactStoreStopped = true;
    }

    double disableStartSec = NowSec();
    DisableCollectors(collectors);
    CollectionTiming timing;
    timing.requestedSec = static_cast<double>(act.duration);
    timing.startSec = startSec;
    timing.lastCollectSec = lastCollectSec;
    timing.endSec = lastCollectSec;
    timing.durationSec = lastCollectSec - startSec;
    timing.disableCostSec = NowSec() - disableStartSec;
    if (timing.durationSec <= 0.0) {
        timing.durationSec = static_cast<double>(act.duration);
    }

    PrintSummary(ctx, collectors.ddrcOpened, collectors.pcieOpened, timing, act);
    if (act.debug) {
        PrintCollectionHealth(health);
        PrintSpeStorePcDebug();
    }
    CleanupCollectors(collectors);
    return 0;
}
