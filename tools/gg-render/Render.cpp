// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "Render.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

#include <fmt/format.h>

#include "Signals.h"
#include "gg/engine/Engine.h"
#include "gg/source/SampleBuffer.h"

namespace gg::render
{
namespace
{
/// Collects the grains spawned during one process() call, and the latest grainEndSample of all
/// grains so far. The engine calls the sink from inside process(), which is annotated
/// non-blocking, so the sink only copies into storage reserved up front and never allocates;
/// that keeps RTSan builds of the tests clean. The lines are formatted between process() calls.
struct GrainCollector
{
    explicit GrainCollector(std::size_t capacity) { pending.reserve(capacity); }

    static void sink(void *ctx, const GrainSpec &g)
    {
        auto &self = *static_cast<GrainCollector *>(ctx);
        self.latestEnd = std::max(self.latestEnd, grainEndSample(g));
        if (self.pending.size() < self.pending.capacity())
        {
            self.pending.push_back(g);
        }
        else
        {
            self.overflowed = true;
        }
    }

    std::vector<GrainSpec> pending;
    int64_t latestEnd{-1}; ///< -1 until a grain spawns
    bool overflowed{false};
};

std::shared_ptr<const SampleBuffer> toSampleBuffer(const gg::metrics::AudioData &audio)
{
    const auto channels = static_cast<uint32_t>(audio.channels.size());
    std::size_t frames = channels > 0 ? audio.channels[0].size() : 0;
    for (const auto &c : audio.channels)
    {
        frames = std::min(frames, c.size());
    }
    auto buffer =
        std::make_shared<SampleBuffer>(channels, static_cast<int64_t>(frames), audio.sampleRate);
    for (uint32_t c = 0; c < channels; ++c)
    {
        std::copy_n(audio.channels[c].begin(), frames, buffer->channel(c));
    }
    return buffer;
}

CloudParams toCloudParams(const StreamSpec &s)
{
    return {
        .densityHz = s.grainRateHz,
        .distribution = s.distribution,
        .grainDurationSec = s.grainDurationMs / 1000.0,
        .position = s.position,
        .spray = s.positionJitter,
        .pitchSemitones = s.pitchSemitones,
        .stereo = 0.0,
        .gain = static_cast<float>(s.gain),
    };
}

/// The most grains one process() call of `frames` frames can spawn. Onsets are at least
/// CloudScheduler::kMinIntervalSamples (1/16) apart, less the rounding of t + Δ, which is at
/// most 2^-22 samples while t is below 2^32 (every render gg-render accepts is shorter). So a
/// call of at most kMaxBlockSize frames starts fewer than frames / (1/16 − 2^-22) + 1 grains,
/// which is at most 16 × frames + 5.
std::size_t maxGrainsPerCall(uint32_t frames)
{
    const auto perFrame = static_cast<std::size_t>(1.0 / CloudScheduler::kMinIntervalSamples);
    return perFrame * frames + 16;
}
} // namespace

std::string formatEventLine(const GrainSpec &g)
{
    return fmt::format(
        R"({{"i":{},"onset":{},"frac":{},"pos":{},"rate":{},"dur":{},"gain":{},"pan":{}}})",
        g.index, g.onsetSample, g.onsetFrac, g.startPos, g.rate, g.durationSamples, g.gain, g.pan);
}

gg::metrics::AudioData render(const RenderSpec &spec, std::vector<std::string> *eventLines)
{
    const auto source = toSampleBuffer(makeSignal(spec.source, spec.sampleRate, spec.seed));

    EngineConfig config;
    config.sampleRate = spec.sampleRate;
    config.maxBlockSize = spec.blockSize;
    config.seed = spec.seed;
    Engine engine(config);
    engine.setSource(source);
    engine.setCloudParams(toCloudParams(spec.stream));

    GrainCollector grains(maxGrainsPerCall(spec.blockSize));
    engine.setEventSink(&GrainCollector::sink, &grains);
    if (eventLines != nullptr)
    {
        eventLines->clear();
    }

    gg::metrics::AudioData out{spec.sampleRate, {{}, {}}};
    // Renders frames [from, to) into `out`, which must already hold `to` frames.
    const auto renderFrames = [&](std::size_t from, std::size_t to) {
        for (std::size_t done = from; done < to;)
        {
            const auto n = static_cast<uint32_t>(std::min<std::size_t>(spec.blockSize, to - done));
            float *const block[2] = {out.channels[0].data() + done, out.channels[1].data() + done};
            engine.process(block, 2, n);
            done += n;

            if (eventLines != nullptr)
            {
                for (const GrainSpec &g : grains.pending)
                {
                    eventLines->push_back(formatEventLine(g));
                }
            }
            grains.pending.clear();
        }
    };

    // durationSeconds of stream activity.
    const int64_t duration = framesFor(spec.durationSeconds, spec.sampleRate);
    for (auto &channel : out.channels)
    {
        channel.resize(static_cast<std::size_t>(duration));
    }
    renderFrames(0, static_cast<std::size_t>(duration));

    // The release tail: with the stream off, render until every grain has finished, plus one
    // silent frame. The end comes from the grains, not from polling activeGrains() once per
    // block, so the length doesn't depend on blockSize.
    CloudParams off = toCloudParams(spec.stream);
    off.densityHz = 0.0; // off (ruling M1-a)
    engine.setCloudParams(off);
    const int64_t end = std::max(duration, grains.latestEnd + 1);
    // Defensive bound. No grain is longer than the stream's grain duration (at least 16 frames,
    // as the engine clamps it), and one that started on the last frame of the duration ends
    // floor(length) + 2 frames later (grainEndSample), so the tail never needs more. Hitting the
    // bound means the engine broke that promise; gg-render then exits with status 2.
    const double longestGrain =
        std::max(16.0, spec.stream.grainDurationMs / 1000.0 * spec.sampleRate);
    const int64_t tailBound = static_cast<int64_t>(longestGrain) + 2 + spec.blockSize;
    if (end - duration > tailBound)
    {
        throw std::runtime_error(
            fmt::format("the release tail would last {} frames, more than the {}-frame bound",
                        end - duration, tailBound));
    }
    for (auto &channel : out.channels)
    {
        channel.resize(static_cast<std::size_t>(end));
    }
    renderFrames(static_cast<std::size_t>(duration), static_cast<std::size_t>(end));

    if (engine.activeGrains() != 0)
    {
        throw std::logic_error("grains are still sounding after the release tail");
    }
    if (grains.overflowed)
    {
        throw std::logic_error("more grains spawned in one block than the scheduler allows");
    }
    return out;
}
} // namespace gg::render
