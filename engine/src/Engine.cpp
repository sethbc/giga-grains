// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/engine/Engine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "gg/core/DenormalGuard.h"
#include "gg/grain/GrainKernels.h"

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
    if (!(std::isfinite(config.liveSeconds) && config.liveSeconds >= 0.0))
    {
        throw std::invalid_argument("Engine: liveSeconds must be finite and >= 0");
    }
    // The pool stores 2 × grainCapacity grains, a count held in a uint32_t (GrainPool).
    if (config.grainCapacity >= (uint32_t{1} << 31))
    {
        throw std::invalid_argument("Engine: grainCapacity must be below 2^31");
    }
    return config;
}

// M1 has one layer with one stream (spec §6.2): layer A is layer 0, and its stream is stream 0.
constexpr uint32_t kLayer = 0;
constexpr uint32_t kStream = 0;
} // namespace

Engine::Engine(const EngineConfig &config) : config_(validated(config)), pool_(config.grainCapacity)
{
    cloud_.prepare(config_.sampleRate);
    // process() writes into the ring in blocks of at most maxBlockSize frames (ruling M1-o).
    live_.prepare(config_.sampleRate, config_.liveSeconds, config_.maxBlockSize);
}

void Engine::setSource(std::shared_ptr<const SampleBuffer> source)
{
    sourceOwner_ = std::move(source);
    source_ = sourceOwner_.get();
    sourceData_[0] = sourceData_[1] = nullptr;
    sourceFrames_ = 0;
    sourceRate_ = config_.sampleRate;
    monoSource_ = true;
    sourcePairs_.clear();
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
            if (!monoSource_)
            {
                // The SIMD sample kernel's stereo reads (§16 lever 2); a mono source stays planar.
                sourcePairs_.assign(sourceData_[0], sourceData_[1], sourceFrames_);
            }
        }
    }
}

void Engine::setCloudParams(const CloudParams &params) noexcept { cloud_.setParams(params); }

void Engine::setHeldKeys(std::span<const uint8_t> keys) noexcept { cloud_.setHeldKeys(keys); }

void Engine::setNoteGate(bool gate) noexcept { cloud_.setNoteGate(gate); }

void Engine::setSourceMode(SourceMode mode) noexcept
{
    if (mode == SourceMode::Live && !live_.enabled())
    {
        return; // no ring to read
    }
    requestedMode_ = mode;
}

void Engine::setFreeze(bool freeze) noexcept { freeze_ = freeze; }

void Engine::setEventSink(GrainEventSink sink, void *ctx) noexcept
{
    sink_ = sink;
    sinkCtx_ = ctx;
}

void Engine::reset() noexcept
{
    pool_.clear();
    cloud_.reset();
    live_.reset();
    clock_ = 0;
    dropped_ = 0;
}

void Engine::process(float *const *out, uint32_t numChannels, uint32_t numFrames) noexcept
{
    process(nullptr, 0, out, numChannels, numFrames);
}

void Engine::process(const float *const *in, uint32_t numInChannels, float *const *out,
                     uint32_t numChannels, uint32_t numFrames) noexcept
{
    ScopedDenormalsOff denormalsOff;
    // The source mode and the freeze setting apply from this call's first frame.
    if (requestedMode_ != mode_)
    {
        mode_ = requestedMode_;
        // The pool's grains belong to the old source: their positions mean nothing in the new
        // one.
        pool_.clear();
        cloud_.setSourceMode(mode_, config_.liveSeconds);
    }
    live_.setFrozen(freeze_);
    if (in == nullptr)
    {
        numInChannels = 0;
    }
    if (out == nullptr)
    {
        numChannels = 0;
    }
    for (uint32_t done = 0; done < numFrames;)
    {
        const uint32_t n = std::min(numFrames - done, config_.maxBlockSize);
        processBlock(in, numInChannels, out, numChannels, done, n);
        done += n;
    }
}

void Engine::processBlock(const float *const *in, uint32_t numInChannels, float *const *out,
                          uint32_t numChannels, uint32_t offset, uint32_t numFrames) noexcept
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

    const int64_t blockStart = clock_;
    const int64_t blockEnd = blockStart + static_cast<int64_t>(numFrames);
    // The whole block's input goes into the ring before any grain reads (spec §6.3). At sample t
    // grains read only the window [R(t) − N, R(t) − 1], and the ring keeps numFrames <=
    // maxBlockSize slots beyond the window (ruling M1-o), so nothing written here after t
    // reaches t, not even by overwriting the window's oldest frames.
    if (live_.enabled())
    {
        live_.write(in, numInChannels, offset, blockStart, numFrames);
    }

    // Spawn this block's grains, then render, then retire. renderedTo is the sample up to
    // which the pool has already been rendered; admit() moves it forward when it has to
    // retire grains early to make room.
    int64_t renderedTo = blockStart;
    cloud_.schedule(blockStart, numFrames, config_.seed, kLayer, kStream, [&](const GrainDraw &d) {
        if (mode_ == SourceMode::Live)
        {
            GrainSpec g;
            if (liveGrain(d, g))
            {
                admit(g, dst, blockStart, renderedTo);
            }
            else
            {
                ++dropped_;
            }
        }
        else if (source_ != nullptr)
        {
            admit(sampleGrain(d), dst, blockStart, renderedTo);
        }
    });
    renderPool(dst, blockStart, renderedTo, blockEnd);
    clock_ = blockEnd;
    pool_.retireFinished(blockEnd);
}

GrainSpec Engine::sampleGrain(const GrainDraw &d) const noexcept
{
    // The draw's position is a fraction of the source; its rate is the pitch ratio, converted
    // here from the source's rate to the output rate. Both as in M0.
    const double span = sourceFrames_ > 1 ? static_cast<double>(sourceFrames_ - 1) : 0.0;
    return {
        .index = d.index,
        .onsetSample = d.onsetSample,
        .onsetFrac = d.onsetFrac,
        .startPos = d.positionDraw * span,
        .rate = d.rate * sourceRate_ / config_.sampleRate,
        .durationSamples = d.durationSamples,
        .gain = d.gain,
        .pan = d.pan,
        .tier = d.tier,
    };
}

bool Engine::liveGrain(const GrainDraw &d, GrainSpec &g) const noexcept
{
    // The admission clamp (spec §6.3; the class comment has the derivation). Between output
    // samples the head moves linearly while it advances, so at the real onset T = t0 + frac it
    // is at ρ = R(t0) + min(frac, run), and after T it advances min(e, run − that) more frames
    // in e samples.
    constexpr double kM = static_cast<double>(LiveSource::kHeadMargin);
    const int64_t t0 = d.onsetSample;
    const double run = live_.headRunAfter(t0); // +inf when not frozen
    const double step = std::min(d.onsetFrac, run);
    const double rho = static_cast<double>(live_.headAt(t0)) + step;
    const double dur = d.durationSamples;
    const double rate = d.rate;
    // The frames the head advances during the grain's life.
    const double span = std::min(dur, run - step);
    const double hi = rho - 3.0 - kM - std::max(0.0, dur * rate - span);
    const double lo =
        rho - static_cast<double>(live_.frames()) + 1.0 + kM + std::max(0.0, span * (1.0 - rate));
    if (lo > hi)
    {
        return false; // the grain's read span doesn't fit the ring
    }
    g = {
        .index = d.index,
        .onsetSample = d.onsetSample,
        .onsetFrac = d.onsetFrac,
        .startPos = std::clamp(rho - d.positionDraw * config_.sampleRate, lo, hi),
        .rate = rate, // the ring runs at the output rate
        .durationSamples = dur,
        .gain = d.gain,
        .pan = d.pan,
        .tier = d.tier,
    };
    return true;
}

void Engine::admit(const GrainSpec &g, float *const dst[2], int64_t blockStart,
                   int64_t &renderedTo) noexcept
{
    // Count only the grains still sounding at the onset, so admission doesn't depend on where
    // the block boundaries fall (the pool may still store grains that finished earlier in the
    // block, because retirement normally waits for the block's end).
    if (!pool_.canAdmit(g.onsetSample))
    {
        ++dropped_;
        return;
    }
    if (pool_.storageFull())
    {
        // More than capacity grains finished in this block. Finish the pool up to the onset, so
        // the grains retired here have played all their samples, then retire them: fewer than
        // capacity still sound, so g fits.
        renderPool(dst, blockStart, renderedTo, g.onsetSample);
        renderedTo = g.onsetSample;
        pool_.retireFinished(g.onsetSample);
    }
    // Can't fail: storage was full only if the retirement above just ran, and it left only the
    // grains still sounding at the onset, fewer than capacity by canAdmit.
    [[maybe_unused]] const bool spawned = pool_.spawn(g);
    assert(spawned);
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
    // next grain starts, so each sample sums its grains in spawn order. With one output (dst[1]
    // null) the kernels carry source channel 0, unpanned. Each grain reads with its own latched
    // tier (ruling 2).
    const kernels::Output output{{dst[0], dst[1]}, blockStart};
    const GrainRows &rows = pool_.rows();
    const uint32_t n = pool_.size();
    if (mode_ == SourceMode::Live)
    {
        for (uint32_t i = 0; i < n; ++i)
        {
            if (rows.tier[i] == Tier::Eco)
            {
                kernels::renderLiveSimd<kernels::Linear>(rows, i, live_, output, from, to, hann_);
            }
            else
            {
                kernels::renderLiveSimd<kernels::Hermite>(rows, i, live_, output, from, to, hann_);
            }
        }
        return;
    }
    const kernels::SampleSource source{
        {sourceData_[0], sourceData_[1]}, sourceFrames_, monoSource_, sourcePairs_.data()};
    for (uint32_t i = 0; i < n; ++i)
    {
        if (rows.tier[i] == Tier::Eco)
        {
            kernels::renderSampleSimd<kernels::Linear>(rows, i, source, output, from, to, hann_);
        }
        else
        {
            kernels::renderSampleSimd<kernels::Hermite>(rows, i, source, output, from, to, hann_);
        }
    }
}
} // namespace gg
