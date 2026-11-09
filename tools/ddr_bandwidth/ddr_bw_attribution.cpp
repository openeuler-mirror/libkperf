#include <algorithm>
#include <cstddef>
#include "ddr_bw_types.h"

static uint64_t OtherUserSamples(uint64_t systemSamples,
                                 uint64_t targetSamples,
                                 uint64_t kernelSamples)
{
    const uint64_t nonTargetSamples = systemSamples > targetSamples
        ? systemSamples - targetSamples : 0;
    return nonTargetSamples > kernelSamples
        ? nonTargetSamples - kernelSamples : 0;
}

template <typename Bucket>
static void AddContribution(Bucket& bucket, const SpeContribution& contribution)
{
    if (contribution.read) ++bucket.systemReadSamples;
    if (contribution.write) ++bucket.systemWriteSamples;
    if (contribution.kernel) {
        if (contribution.read) ++bucket.kernelReadSamples;
        if (contribution.write) ++bucket.kernelWriteSamples;
        return;
    }
    if (!contribution.target) return;

    if (contribution.read) {
        ++bucket.targetReadSamples;
        if (!contribution.funcKey.empty()) {
            ++bucket.funcReadSamples[contribution.funcKey];
        }
    }
    if (contribution.write) {
        ++bucket.targetWriteSamples;
        if (!contribution.funcKey.empty()) {
            ++bucket.funcWriteSamples[contribution.funcKey];
        }
    }
}

static void ResetAttribution(BwContext& ctx)
{
    ctx.attribution = {};

    for (auto& entry : ctx.funcStats) {
        entry.second.attributedReadBytes = 0.0;
        entry.second.attributedWriteBytes = 0.0;
    }
    for (auto& window : ctx.windows) {
        window.attributedReadBytes = 0.0;
        window.attributedWriteBytes = 0.0;
        window.effectiveFuncWriteSamples.clear();
        window.effectiveTargetWriteSamples = 0;
        window.effectiveKernelWriteSamples = 0;
        window.effectiveSystemWriteSamples = 0;
        window.attributedKernelReadBytes = 0.0;
        window.attributedOtherReadBytes = 0.0;
        window.attributedKernelWriteBytes = 0.0;
        window.attributedOtherWriteBytes = 0.0;
        window.unattributedReadBytes = 0.0;
        window.unattributedWriteBytes = 0.0;
    }
}

static void BuildEffectiveWriteSamples(BwContext& ctx)
{
    constexpr std::size_t HISTORY_WINDOWS = 2;
    for (std::size_t i = 0; i < ctx.windows.size(); ++i) {
        auto& current = ctx.windows[i];
        const std::size_t begin = i > HISTORY_WINDOWS ? i - HISTORY_WINDOWS : 0;
        for (std::size_t j = begin; j <= i; ++j) {
            const auto& source = ctx.windows[j];
            if (!source.speValid) continue;
            current.effectiveTargetWriteSamples += source.targetWriteSamples;
            current.effectiveKernelWriteSamples += source.kernelWriteSamples;
            current.effectiveSystemWriteSamples += source.systemWriteSamples;
            for (const auto& entry : source.funcWriteSamples) {
                current.effectiveFuncWriteSamples[entry.first] += entry.second;
            }
        }
    }
}

static void FinalizeReadAttribution(BwContext& ctx, uint64_t minSamples)
{
    // Same-window read-sample-count attribution:
    // residual_read[i] is distributed by eligible read sample counts observed
    // in window i. HIPG uses DRAM-source loads; the source-less fallback uses
    // all valid SPE loads as approximate statistical weights.
    for (auto& window : ctx.windows) {
        if (window.residualReadBytes <= 0.0) continue;
        if (!window.valid) {
            window.unattributedReadBytes = window.residualReadBytes;
            ctx.attribution.unattributedReadBytes += window.unattributedReadBytes;
            continue;
        }
        if (window.systemReadSamples == 0) {
            ++ctx.attribution.noDenominatorReadWindows;
            window.unattributedReadBytes = window.residualReadBytes;
            ctx.attribution.unattributedReadBytes += window.unattributedReadBytes;
            continue;
        }
        const uint64_t otherReadSamples = OtherUserSamples(
            window.systemReadSamples, window.targetReadSamples,
            window.kernelReadSamples);
        if (window.kernelReadSamples > 0) {
            window.attributedKernelReadBytes = window.residualReadBytes *
                static_cast<double>(window.kernelReadSamples) /
                static_cast<double>(window.systemReadSamples);
            ctx.attribution.kernelReadBytes +=
                window.attributedKernelReadBytes;
        }
        if (otherReadSamples > 0) {
            window.attributedOtherReadBytes = window.residualReadBytes *
                static_cast<double>(otherReadSamples) /
                static_cast<double>(window.systemReadSamples);
            ctx.attribution.otherReadBytes += window.attributedOtherReadBytes;
        }
        if (window.targetReadSamples < minSamples) {
            ++ctx.attribution.sparseReadWindows;
            window.unattributedReadBytes = std::max(0.0,
                window.residualReadBytes - window.attributedKernelReadBytes -
                window.attributedOtherReadBytes);
            ctx.attribution.unattributedReadBytes += window.unattributedReadBytes;
            continue;
        }

        double attributedInWindow = 0.0;
        for (const auto& entry : window.funcReadSamples) {
            if (entry.second == 0) {
                continue;
            }

            const double bytes = window.residualReadBytes *
                static_cast<double>(entry.second) /
                static_cast<double>(window.systemReadSamples);
            ctx.funcStats[entry.first].attributedReadBytes += bytes;
            ctx.attribution.readBytes += bytes;
            attributedInWindow += bytes;
        }

        window.attributedReadBytes = attributedInWindow;
        if (attributedInWindow > 0.0) {
            ++ctx.attribution.readWindows;
        }
        window.unattributedReadBytes = std::max(0.0,
            window.residualReadBytes - window.attributedReadBytes -
            window.attributedKernelReadBytes - window.attributedOtherReadBytes);
        ctx.attribution.unattributedReadBytes += window.unattributedReadBytes;
    }
}

static void FinalizeWriteAttribution(BwContext& ctx, uint64_t minSamples)
{
    // The current window and its two predecessors provide one common
    // system-wide store denominator. Target user, other user and kernel are
    // mutually exclusive shares of the current residual write traffic.
    BuildEffectiveWriteSamples(ctx);
    for (auto& window : ctx.windows) {
        if (window.residualWriteBytes <= 0.0) continue;
        if (!window.valid) {
            window.unattributedWriteBytes = window.residualWriteBytes;
            ctx.attribution.unattributedWriteBytes += window.unattributedWriteBytes;
            continue;
        }
        if (window.effectiveSystemWriteSamples == 0) {
            ++ctx.attribution.noDenominatorWriteWindows;
            window.unattributedWriteBytes = window.residualWriteBytes;
            ctx.attribution.unattributedWriteBytes += window.unattributedWriteBytes;
            continue;
        }

        const uint64_t otherWriteSamples = OtherUserSamples(
            window.effectiveSystemWriteSamples,
            window.effectiveTargetWriteSamples,
            window.effectiveKernelWriteSamples);
        if (window.effectiveKernelWriteSamples > 0) {
            window.attributedKernelWriteBytes = window.residualWriteBytes *
                static_cast<double>(window.effectiveKernelWriteSamples) /
                static_cast<double>(window.effectiveSystemWriteSamples);
            ctx.attribution.kernelWriteBytes +=
                window.attributedKernelWriteBytes;
        }
        if (otherWriteSamples > 0) {
            window.attributedOtherWriteBytes = window.residualWriteBytes *
                static_cast<double>(otherWriteSamples) /
                static_cast<double>(window.effectiveSystemWriteSamples);
            ctx.attribution.otherWriteBytes += window.attributedOtherWriteBytes;
        }
        if (window.effectiveTargetWriteSamples < minSamples) {
            ++ctx.attribution.sparseWriteWindows;
            window.unattributedWriteBytes = std::max(0.0,
                window.residualWriteBytes - window.attributedKernelWriteBytes -
                window.attributedOtherWriteBytes);
            ctx.attribution.unattributedWriteBytes += window.unattributedWriteBytes;
            continue;
        }

        double attributedInWindow = 0.0;
        for (const auto& entry : window.effectiveFuncWriteSamples) {
            if (entry.second == 0) {
                continue;
            }

            const double bytes = window.residualWriteBytes *
                static_cast<double>(entry.second) /
                static_cast<double>(window.effectiveSystemWriteSamples);
            ctx.funcStats[entry.first].attributedWriteBytes += bytes;
            ctx.attribution.writeBytes += bytes;
            attributedInWindow += bytes;
        }
        window.attributedWriteBytes = attributedInWindow;
        if (attributedInWindow > 0.0) {
            ++ctx.attribution.writeWindows;
        }
        window.unattributedWriteBytes = std::max(0.0,
            window.residualWriteBytes - window.attributedWriteBytes -
            window.attributedKernelWriteBytes - window.attributedOtherWriteBytes);
        ctx.attribution.unattributedWriteBytes += window.unattributedWriteBytes;
    }
}

void FinalizeAttribution(BwContext& ctx, uint64_t minSamples)
{
    ResetAttribution(ctx);
    FinalizeReadAttribution(ctx, minSamples);
    FinalizeWriteAttribution(ctx, minSamples);
    for (const auto& window : ctx.windows) {
        if (!window.valid) ++ctx.attribution.invalidWindows;
    }
}

bool BucketSpeContribution(BwContext& ctx, SpeWindowState& state,
                           const SpeContribution& contribution)
{
    const int64_t timestampNs = contribution.timestampNs;
    if (timestampNs <= 0) {
        ++ctx.speTimestamps.missing;
        return false;
    }
    if (timestampNs < state.collectionStartNs) {
        ++ctx.speTimestamps.beforeStart;
        return false;
    }
    if (state.finalEndNs > 0 && timestampNs >= state.finalEndNs) {
        ++ctx.speTimestamps.afterStop;
        return false;
    }

    if (timestampNs >= state.openStartNs) {
        AddContribution(state.open, contribution);
        return true;
    }

    auto window = std::upper_bound(
        ctx.windows.begin(), ctx.windows.end(), timestampNs,
        [](int64_t timestamp, const WindowRecord& item) {
            return timestamp < item.endNs;
        });
    if (window == ctx.windows.end() ||
        timestampNs < window->startNs || timestampNs >= window->endNs) {
        ++ctx.speTimestamps.unbucketed;
        return false;
    }

    ++ctx.speTimestamps.late;
    AddContribution(*window, contribution);
    return true;
}
