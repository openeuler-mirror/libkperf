#include <cmath>
#include <cstdlib>
#include <iostream>
#include "ddr_bw_types.h"
#include "ddr_bw_schedule.h"

static void Check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

static bool Near(double lhs, double rhs)
{
    return std::abs(lhs - rhs) < 1e-7;
}

int main()
{
    BwContext ctx;
    WindowRecord first;
    first.residualReadBytes = 1000;
    first.residualWriteBytes = 400;
    first.funcReadSamples = {{"A", 90}, {"B", 10}};
    first.targetReadSamples = first.systemReadSamples = 100;
    first.funcWriteSamples = {{"A", 10}, {"B", 30}};
    first.targetWriteSamples = first.systemWriteSamples = 40;
    ctx.windows.push_back(first);

    // Different traffic rates and phases must retain their own sample shares.
    // Pooling all samples across the run would incorrectly give A 109 bytes.
    WindowRecord second;
    second.residualReadBytes = 10;
    second.funcReadSamples = {{"A", 1000}, {"B", 9000}};
    second.targetReadSamples = second.systemReadSamples = 10000;
    ctx.windows.push_back(second);
    FinalizeAttribution(ctx, 30);
    Check(Near(ctx.funcStats["A"].attributedReadBytes, 901), "phase-local read attribution");
    Check(Near(ctx.funcStats["B"].attributedReadBytes, 109), "phase-local read complement");
    Check(Near(ctx.funcStats["A"].attributedWriteBytes, 0), "per-function write threshold");
    Check(Near(ctx.funcStats["B"].attributedWriteBytes, 300), "write conservation");
    Check(Near(ctx.attribution.readBytes, 1010), "read conservation");

    // One sample must not take all of a high-traffic window by default.
    WindowRecord sparse;
    sparse.residualReadBytes = 1000000;
    sparse.residualWriteBytes = 300;
    sparse.funcReadSamples = {{"C", 1}};
    sparse.targetReadSamples = sparse.systemReadSamples = 1;
    sparse.funcWriteSamples = {{"C", 30}};
    sparse.targetWriteSamples = sparse.systemWriteSamples = 30;
    ctx.windows.push_back(sparse);
    FinalizeAttribution(ctx, 30);
    Check(Near(ctx.attribution.readBytes, 1010), "sparse read remains unattributed");
    Check(Near(ctx.funcStats["C"].attributedWriteBytes, 300.0 * 30.0 / 70.0),
          "threshold is inclusive and direction-specific");
    Check(ctx.attribution.sparseReadWindows == 1, "sparse read diagnostic");
    Check(ctx.attribution.readWindows == 2 && ctx.attribution.writeWindows == 2, "coverage counters");

    WindowRecord empty;
    empty.residualReadBytes = 500;
    empty.residualWriteBytes = 700;
    ctx.windows.push_back(empty);
    WindowRecord invalid = first;
    invalid.valid = false;
    ctx.windows.push_back(invalid);
    FinalizeAttribution(ctx, 30);
    Check(Near(ctx.attribution.readBytes, 1010), "empty and invalid windows cannot borrow evidence");
    Check(Near(ctx.attribution.writeBytes, 1400),
          "writeback may use the preceding target-store history");
    Check(ctx.attribution.sparseReadWindows == 2 && ctx.attribution.sparseWriteWindows == 0,
          "historical write evidence satisfies the target gate");
    Check(ctx.attribution.invalidWindows == 1, "invalid collection diagnostic");
    FinalizeAttribution(ctx, 30);
    Check(Near(ctx.attribution.readBytes, 1010), "finalization must be idempotent");
    FinalizeAttribution(ctx, 1);
    Check(Near(ctx.funcStats["C"].attributedReadBytes, 1000000), "explicit legacy threshold");
    Check(ctx.attribution.invalidWindows == 1, "threshold cannot admit invalid windows");

    BwContext large;
    WindowRecord wide;
    wide.residualReadBytes = 120;
    wide.targetReadSamples = wide.systemReadSamples = (uint64_t(1) << 33);
    wide.funcReadSamples = {{"wide", wide.targetReadSamples}};
    large.windows.push_back(wide);
    FinalizeAttribution(large, 30);
    Check(Near(large.attribution.readBytes, 120), "64-bit sample accounting");
    BwContext none;
    FinalizeAttribution(none, 30);
    Check(none.attribution.readWindows == 0, "empty run");

    // Reads use the system-wide denominator. Writes are normalized within
    // the target PID set, so background and kernel stores do not compress them.
    BwContext systemWide;
    WindowRecord shared;
    shared.residualReadBytes = 1000;
    shared.residualWriteBytes = 500;
    shared.funcReadSamples = {{"target", 20}};
    shared.funcWriteSamples = {{"target", 10}};
    shared.targetReadSamples = 25;
    shared.targetWriteSamples = 12;
    shared.systemReadSamples = 100;
    shared.systemWriteSamples = 50;
    shared.kernelReadSamples = 30;
    shared.kernelWriteSamples = 10;
    systemWide.windows.push_back(shared);
    FinalizeAttribution(systemWide, 1);
    Check(Near(systemWide.funcStats["target"].attributedReadBytes, 200),
          "system-wide read denominator");
    Check(Near(systemWide.funcStats["target"].attributedWriteBytes,
               500.0 * 10.0 / 12.0),
          "target-local write denominator");
    Check(Near(systemWide.attribution.writeBytes, 500),
          "target write receives the full residual");
    Check(Near(systemWide.attribution.kernelReadBytes, 300) &&
          Near(systemWide.attribution.kernelObservedWriteBytes, 100),
          "kernel samples retain read partition and write observation");

    // DDR writeback may lag retired stores. Each write window uses its own
    // store samples plus the two preceding SPE-valid windows.
    BwContext laggedWrite;
    WindowRecord write0;
    write0.funcWriteSamples = {{"A", 10}};
    write0.targetWriteSamples = 10;
    write0.systemWriteSamples = 20;
    write0.kernelWriteSamples = 5;
    laggedWrite.windows.push_back(write0);
    WindowRecord write1;
    write1.funcWriteSamples = {{"B", 30}};
    write1.targetWriteSamples = 30;
    write1.systemWriteSamples = 60;
    write1.kernelWriteSamples = 10;
    laggedWrite.windows.push_back(write1);
    WindowRecord write2;
    write2.residualWriteBytes = 900;
    write2.funcWriteSamples = {{"C", 20}};
    write2.targetWriteSamples = 20;
    write2.systemWriteSamples = 20;
    write2.kernelWriteSamples = 5;
    laggedWrite.windows.push_back(write2);
    WindowRecord write3;
    write3.residualWriteBytes = 600;
    write3.funcWriteSamples = {{"D", 10}};
    write3.targetWriteSamples = 10;
    write3.systemWriteSamples = 20;
    write3.kernelWriteSamples = 5;
    laggedWrite.windows.push_back(write3);
    FinalizeAttribution(laggedWrite, 1);
    Check(laggedWrite.windows[2].effectiveSystemWriteSamples == 100 &&
          laggedWrite.windows[2].effectiveTargetWriteSamples == 60 &&
          laggedWrite.windows[2].effectiveKernelWriteSamples == 20,
          "write window includes two historical store windows");
    Check(laggedWrite.windows[3].effectiveFuncWriteSamples.count("A") == 0,
          "write history excludes samples older than two windows");
    Check(Near(laggedWrite.funcStats["A"].attributedWriteBytes, 150) &&
          Near(laggedWrite.funcStats["B"].attributedWriteBytes, 750) &&
          Near(laggedWrite.funcStats["C"].attributedWriteBytes, 500) &&
          Near(laggedWrite.funcStats["D"].attributedWriteBytes, 100),
          "three-window write attribution");
    Check(Near(laggedWrite.attribution.writeBytes, 1500),
          "three-window write attribution preserves the target residual");
    Check(Near(laggedWrite.attribution.kernelObservedWriteBytes, 300),
          "kernel write observation uses the same three-window evidence");
    FinalizeAttribution(laggedWrite, 1);
    Check(laggedWrite.windows[2].effectiveSystemWriteSamples == 100 &&
          Near(laggedWrite.funcStats["B"].attributedWriteBytes, 750),
          "three-window write finalization is idempotent");

    BwContext invalidWriteHistory;
    WindowRecord badHistory;
    badHistory.speValid = false;
    badHistory.valid = false;
    badHistory.funcWriteSamples = {{"bad", 100}};
    badHistory.targetWriteSamples = badHistory.systemWriteSamples = 100;
    invalidWriteHistory.windows.push_back(badHistory);
    WindowRecord goodCurrent;
    goodCurrent.residualWriteBytes = 100;
    goodCurrent.funcWriteSamples = {{"good", 10}};
    goodCurrent.targetWriteSamples = 10;
    goodCurrent.systemWriteSamples = 20;
    invalidWriteHistory.windows.push_back(goodCurrent);
    FinalizeAttribution(invalidWriteHistory, 1);
    Check(invalidWriteHistory.windows[1].effectiveSystemWriteSamples == 20 &&
          invalidWriteHistory.windows[1].effectiveFuncWriteSamples.count("bad") == 0 &&
          Near(invalidWriteHistory.funcStats["good"].attributedWriteBytes, 100),
          "SPE-invalid history is excluded from write attribution");

    BwContext targetSparse;
    WindowRecord noisy;
    noisy.residualReadBytes = 1000;
    noisy.funcReadSamples = {{"target", 2}};
    noisy.targetReadSamples = 2;
    noisy.systemReadSamples = 1000;
    targetSparse.windows.push_back(noisy);
    FinalizeAttribution(targetSparse, 30);
    Check(Near(targetSparse.attribution.readBytes, 0),
          "sparse gate must use target evidence");

    BwContext missingDenominator;
    WindowRecord inconsistent;
    inconsistent.residualReadBytes = 100;
    inconsistent.funcReadSamples = {{"target", 1}};
    inconsistent.targetReadSamples = 1;
    missingDenominator.windows.push_back(inconsistent);
    FinalizeAttribution(missingDenominator, 1);
    Check(missingDenominator.attribution.noDenominatorReadWindows == 1 &&
          Near(missingDenominator.attribution.readBytes, 0),
          "missing system denominator remains unattributed");

    BwContext bucketed;
    SpeWindowState buckets;
    buckets.collectionStartNs = 1000;
    buckets.openStartNs = 1000;
    auto add = [&](int64_t ts, bool target, bool read, bool write) {
        SpeContribution c;
        c.timestampNs = ts;
        c.target = target;
        c.read = read;
        c.write = write;
        c.funcKey = target ? "f" : "";
        return BucketSpeContribution(bucketed, buckets, c);
    };
    Check(add(1000, true, true, false), "window start is inclusive");
    WindowRecord sealed;
    sealed.startNs = 1000;
    sealed.endNs = 1100;
    sealed.residualReadBytes = 100;
    sealed.residualWriteBytes = 100;
    sealed.funcReadSamples = buckets.open.funcReadSamples;
    sealed.targetReadSamples = buckets.open.targetReadSamples;
    sealed.systemReadSamples = buckets.open.systemReadSamples;
    bucketed.windows.push_back(sealed);
    buckets.open = {};
    buckets.openStartNs = 1100;
    Check(add(1099, false, false, true), "late sample returns to sealed window");
    Check(add(1050, false, true, false), "late background read updates denominator");
    SpeContribution kernel;
    kernel.timestampNs = 1075;
    kernel.kernel = true;
    kernel.write = true;
    Check(BucketSpeContribution(bucketed, buckets, kernel),
          "kernel sample returns to sealed system denominator");
    Check(add(1100, true, true, false), "window end enters the next window");
    Check(!add(999, true, true, false), "pre-start sample is rejected");
    Check(!add(0, true, true, false), "missing timestamp is rejected");
    buckets.finalEndNs = 1200;
    Check(!add(1200, true, true, false), "final end is exclusive");
    Check(bucketed.windows[0].systemReadSamples == 2 &&
          bucketed.windows[0].systemWriteSamples == 2 &&
          bucketed.windows[0].kernelWriteSamples == 1 &&
          buckets.open.systemReadSamples == 1,
          "timestamp buckets are independent");
    FinalizeAttribution(bucketed, 1);
    Check(Near(bucketed.funcStats["f"].attributedReadBytes, 50),
          "late denominator participates in final attribution");
    Check(Near(bucketed.attribution.kernelObservedWriteBytes, 50),
          "kernel sample contributes to the system-level write observation");
    Check(bucketed.speTimestamps.late == 3 &&
          bucketed.speTimestamps.beforeStart == 1 &&
          bucketed.speTimestamps.missing == 1 &&
          bucketed.speTimestamps.afterStop == 1,
          "timestamp diagnostics");

    Check(Near(NextCollectionDeadline(10, .1, 10.35), 10.4), "skip missed deadlines");
    Check(Near(NextCollectionDeadline(10, .1, 10.01), 10.1), "retain grid before deadline");
    Check(NextCollectionDeadline(10, .1, 10.1) > 10.1, "deadline strictly advances");
    Check(NextCollectionDeadline(1e6, .001, 1e6 + .001) > 1e6 + .001,
          "monotonic clock precision at a boundary");
    std::cout << "DDR attribution and scheduling regression checks passed\n";
}
