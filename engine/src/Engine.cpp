// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

#include "gg/core/DenormalGuard.h"
#include "gg/core/Interpolate.h"

namespace gg
{
namespace
{
const EngineConfig &validated(const EngineConfig &config)
{
    if (!(std::isfinite(config.sampleRate) && config.sampleRate > 0.0))
    {
        throw std::invalid_argument("Engine: sampleRate must be finite and > 0");
    }
    if (config.maxBlockSize == 0)
    {
        throw std::invalid_argument("Engine: maxBlockSize must be > 0");
    }
    return config;
}
} // namespace

Engine::Engine(const EngineConfig &config) : config_(validated(config)), pool_(config.grainCapacity)
{
    stream_.prepare(config_.sampleRate);
}

void Engine::setSource(std::shared_ptr<const SampleBuffer> source)
{
    sourceOwner_ = std::move(source);
    source_ = sourceOwner_.get();
    sourceData_[0] = sourceData_[1] = nullptr;
    sourceFrames_ = 0;
    sourceRate_ = config_.sampleRate;
    monoSource_ = true;
    if (source_ != nullptr)
    {
        sourceRate_ = source_->sampleRate();
        // A source without channels has nothing to read; keep frames at 0 so the
        // interpolators never touch its (null) data.
        if (source_->channels() > 0)
        {
            monoSource_ = source_->channels() == 1;
            sourceData_[0] = source_->channel(0);
            sourceData_[1] = monoSource_ ? sourceData_[0] : source_->channel(1);
            sourceFrames_ = source_->frames();
        }
    }
}

void Engine::setStreamParams(const StreamParams &params) noexcept { stream_.setParams(params); }

void Engine::setEventSink(GrainEventSink sink, void *ctx) noexcept
{
    sink_ = sink;
    sinkCtx_ = ctx;
}

void Engine::reset() noexcept
{
    pool_.clear();
    stream_.reset();
    clock_ = 0;
    dropped_ = 0;
}

void Engine::process(float *const *out, uint32_t numChannels, uint32_t numFrames) noexcept
{
    ScopedDenormalsOff denormalsOff;
    if (out == nullptr)
    {
        numChannels = 0;
    }
    for (uint32_t done = 0; done < numFrames;)
    {
        const uint32_t n = std::min(numFrames - done, config_.maxBlockSize);
        processBlock(out, numChannels, done, n);
        done += n;
    }
}

void Engine::processBlock(float *const *out, uint32_t numChannels, uint32_t offset,
                          uint32_t numFrames) noexcept
{
    for (uint32_t c = 0; c < numChannels; ++c)
    {
        if (out[c] != nullptr)
        {
            std::fill_n(out[c] + offset, numFrames, 0.0f);
        }
    }
    float *const dst[2] = {
        numChannels > 0 && out[0] != nullptr ? out[0] + offset : nullptr,
        numChannels > 1 && out[1] != nullptr ? out[1] + offset : nullptr,
    };

    // Spawn this block's grains, then render, then retire. renderedTo is the sample up to
    // which the pool has already been rendered; admit() moves it forward when it has to
    // retire grains early to make room.
    const int64_t blockStart = clock_;
    const int64_t blockEnd = blockStart + static_cast<int64_t>(numFrames);
    int64_t renderedTo = blockStart;
    stream_.schedule(blockStart, numFrames, sourceFrames_, sourceRate_, config_.seed,
                     [&](const GrainSpec &g) { admit(g, dst, blockStart, renderedTo); });
    renderPool(dst, blockStart, renderedTo, blockEnd);
    clock_ = blockEnd;
    pool_.retireFinished(blockEnd);
}

void Engine::admit(const GrainSpec &g, float *const dst[2], int64_t blockStart,
                   int64_t &renderedTo) noexcept
{
    if (source_ == nullptr)
    {
        return;
    }
    if (pool_.size() == pool_.capacity())
    {
        // The pool may still hold grains that finish before g's onset but haven't been
        // retired, because retirement normally waits for the end of the block. Count only
        // the grains still sounding at the onset, so admission doesn't depend on where the
        // block boundaries fall.
        if (pool_.earliestEnd() > g.onsetSample)
        {
            ++dropped_;
            return;
        }
        // Finish the pool up to the onset, so the grains retired here have played all their
        // samples, then retire them.
        renderPool(dst, blockStart, renderedTo, g.onsetSample);
        renderedTo = g.onsetSample;
        pool_.retireFinished(g.onsetSample);
    }
    pool_.spawn(g);
    if (sink_ != nullptr)
    {
        sink_(sinkCtx_, g);
    }
}

void Engine::renderPool(float *const dst[2], int64_t blockStart, int64_t from, int64_t to) noexcept
{
    if (from >= to || dst[0] == nullptr)
    {
        return;
    }
    // Grain-major, in spawn order: every grain adds to every sample of [from, to) before the
    // next grain starts, so each sample sums its grains in spawn order.
    const bool twoOutputs = dst[1] != nullptr;
    for (uint32_t i = 0; i < pool_.size(); ++i)
    {
        const GrainSpec &g = pool_.at(i);
        if (twoOutputs)
        {
            if (monoSource_)
            {
                renderGrain<true, true>(g, dst, blockStart, from, to);
            }
            else
            {
                renderGrain<true, false>(g, dst, blockStart, from, to);
            }
        }
        else
        {
            // One output reads only source channel 0, whatever the source's width.
            renderGrain<false, true>(g, dst, blockStart, from, to);
        }
    }
}

template <bool TwoOutputs, bool MonoSource>
void Engine::renderGrain(const GrainSpec &g, float *const dst[2], int64_t blockStart, int64_t from,
                         int64_t to) const noexcept
{
    const int64_t first = std::max(from, g.onsetSample);
    const int64_t last = std::min(to, grainEndSample(g)); // exclusive
    for (int64_t t = first; t < last; ++t)
    {
        // Everything comes from the elapsed time since the onset, so the result for sample t
        // doesn't depend on which block t falls in.
        const double e = static_cast<double>(t - g.onsetSample) - g.onsetFrac;
        const float env = hann_.at(e / g.durationSamples);
        const double pos = g.startPos + g.rate * e;
        const auto j = static_cast<std::size_t>(t - blockStart);
        const float left = interp::hermite4(sourceData_[0], sourceFrames_, pos) * env * g.gain;
        dst[0][j] += left;
        if constexpr (TwoOutputs)
        {
            if constexpr (MonoSource)
            {
                dst[1][j] += left;
            }
            else
            {
                dst[1][j] += interp::hermite4(sourceData_[1], sourceFrames_, pos) * env * g.gain;
            }
        }
    }
}
} // namespace gg
