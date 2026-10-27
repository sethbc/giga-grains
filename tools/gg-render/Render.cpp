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
/// Collects the grains spawned during one process() call. The engine calls the sink from inside
/// process(), which is annotated non-blocking, so the sink only copies into storage reserved up
/// front and never allocates; that keeps RTSan builds of the tests clean. The lines are
/// formatted between process() calls.
struct GrainCollector
{
    explicit GrainCollector(std::size_t capacity) { pending.reserve(capacity); }

    static void sink(void *ctx, const GrainSpec &g)
    {
        auto &self = *static_cast<GrainCollector *>(ctx);
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

StreamParams toStreamParams(const StreamSpec &s)
{
    return {
        .grainRateHz = s.grainRateHz,
        .grainDurationSec = s.grainDurationMs / 1000.0,
        .position = s.position,
        .positionJitter = s.positionJitter,
        .pitchSemitones = s.pitchSemitones,
        .gain = static_cast<float>(s.gain),
    };
}
} // namespace

std::string formatEventLine(const GrainSpec &g)
{
    return fmt::format(R"({{"i":{},"onset":{},"frac":{},"pos":{},"rate":{},"dur":{}}})", g.index,
                       g.onsetSample, g.onsetFrac, g.startPos, g.rate, g.durationSamples);
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
    engine.setStreamParams(toStreamParams(spec.stream));

    // The stream spawns at most one grain per output sample (PeriodicStream::setParams), so one
    // block's grains always fit in blockSize slots.
    GrainCollector grains(spec.blockSize);
    if (eventLines != nullptr)
    {
        eventLines->clear();
        engine.setEventSink(&GrainCollector::sink, &grains);
    }

    const auto frames = static_cast<std::size_t>(framesFor(spec.durationSeconds, spec.sampleRate));
    gg::metrics::AudioData out{spec.sampleRate,
                               {std::vector<float>(frames), std::vector<float>(frames)}};
    for (std::size_t done = 0; done < frames;)
    {
        const auto n = static_cast<uint32_t>(std::min<std::size_t>(spec.blockSize, frames - done));
        float *const block[2] = {out.channels[0].data() + done, out.channels[1].data() + done};
        engine.process(block, 2, n);
        done += n;

        if (eventLines != nullptr)
        {
            for (const GrainSpec &g : grains.pending)
            {
                eventLines->push_back(formatEventLine(g));
            }
            grains.pending.clear();
        }
    }
    if (grains.overflowed)
    {
        throw std::logic_error("more grains spawned in one block than the block has frames");
    }
    return out;
}
} // namespace gg::render
