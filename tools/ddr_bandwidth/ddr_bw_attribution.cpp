#include <algorithm>
#include <cstddef>
#include "ddr_bw_types.h"

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
        window.observedKernelWriteBytes = 0.0;
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
    // Same-window DRAM-sample-count attribution:
    // residual_read[i] is distributed only by DRAM-source load sample counts
    // observed in window i.
    for (auto& window : ctx.windows) {
        if (!window.valid || window.residualReadBytes <= 0.0) continue;
        if (window.systemReadSamples == 0) {
            if (window.targetReadSamples < minSamples) {
                ++ctx.attribution.sparseReadWindows;
            } else {
                ++ctx.attribution.noDenominatorReadWindows;
            }
            continue;
        }
        if (window.kernelReadSamples > 0) {
            window.attributedKernelReadBytes = window.residualReadBytes *
                static_cast<double>(window.kernelReadSamples) /
                static_cast<double>(window.systemReadSamples);
            ctx.attribution.kernelReadBytes +=
                window.attributedKernelReadBytes;
        }
        if (window.targetReadSamples < minSamples) {
            ++ctx.attribution.sparseReadWindows;
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
    }
}

static void FinalizeWriteAttribution(BwContext& ctx, uint64_t minSamples)
{
    BuildEffectiveWriteSamples(ctx);
    for (auto& window : ctx.windows) {
        if (!window.valid || window.residualWriteBytes <= 0.0) continue;
        if (window.effectiveKernelWriteSamples > 0 &&
            window.effectiveSystemWriteSamples > 0) {
            window.observedKernelWriteBytes = window.residualWriteBytes *
                static_cast<double>(window.effectiveKernelWriteSamples) /
                static_cast<double>(window.effectiveSystemWriteSamples);
            ctx.attribution.kernelObservedWriteBytes +=
                window.observedKernelWriteBytes;
        }
        if (window.effectiveTargetWriteSamples < minSamples) {
            ++ctx.attribution.sparseWriteWindows;
            continue;
        }
        if (window.effectiveTargetWriteSamples == 0) {
            ++ctx.attribution.noDenominatorWriteWindows;
            continue;
        }

        // Write traffic is normalized within the target PID set. Once the
        // target has enough store evidence, the whole residual belongs to
        // the target; function shares divide that residual by target stores.
        window.attributedWriteBytes = window.residualWriteBytes;
        ctx.attribution.writeBytes += window.residualWriteBytes;
        ++ctx.attribution.writeWindows;

        for (const auto& entry : window.effectiveFuncWriteSamples) {
            if (entry.second < minSamples) {
                continue;
            }

            const double bytes = window.residualWriteBytes *
                static_cast<double>(entry.second) /
                static_cast<double>(window.effectiveTargetWriteSamples);
            ctx.funcStats[entry.first].attributedWriteBytes += bytes;
        }
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
