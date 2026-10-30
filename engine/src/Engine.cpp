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
    // Four layers, A to D (spec §6.2).
    if (config.layer >= 4)
    {
        throw std::invalid_argument("Engine: layer must be below 4");
    }
    return config;
}

// A layer has one stream (spec §6.2): stream 0. M3's Voice scheduler adds streams.
constexpr uint32_t kStream = 0;
} // namespace

Engine::Engine(const EngineConfig &config) : config_(validated(config)), pool_(config.grainCapacity)
{
    cloud_.prepare(config_.sampleRate);
    // process() writes into the ring in blocks of at most maxBlockSize frames (ruling M1-o).
    live_.prepare(config_.sampleRate, config_.liveSeconds, config_.maxBlockSize);
}

Engine::SourceSlot Engine::slotFor(const PreparedSource *source) noexcept
{
    SourceSlot slot;
    slot.source = source;
    if (source != nullptr)
    {
        // Level 0, the full-rate buffer, is the one the kernels read (spec §6.5).
        const PreparedSource::Level &level = source->level(0);
        const SampleBuffer &buffer = *level.buffer;
        slot.rate = buffer.sampleRate();
        // A source without channels has nothing to read; keep frames at 0 so the
        // interpolators never touch its (null) data.
        if (buffer.channels() > 0)
        {
            slot.mono = buffer.channels() == 1;
            slot.data[0] = buffer.channel(0);
            slot.data[1] = slot.mono ? slot.data[0] : buffer.channel(1);
            slot.frames = buffer.frames();
            // The SIMD sample kernel's stereo reads (§16 lever 2); null for a mono source, or
            // when PreparedSource::make couldn't allocate the copy.
            slot.interleaved = level.pairs->data();
        }
    }
    return slot;
}

void Engine::setSource(std::shared_ptr<const SampleBuffer> source)
{
    // make() is all that can throw, so a throw changes nothing.
    setPreparedSource(PreparedSource::make(std::move(source)));
}

void Engine::replaceCurrent(const PreparedSource *source) noexcept
{
    // The sources this replaces: adopted ones go back to their owner, oldest first. At most two,
    // and adoptSource made sure at most one was already waiting.
    for (const SourceSlot *slot : {&previousSlot(), &currentSlot()})
    {
        if (slot->source != nullptr && slot->source != owned_.get())
        {
            assert(retiredCount_ < kRetiredCapacity);
            retired_[retiredCount_++] = slot->source;
        }
    }
    if (source == nullptr)
    {
        // No source: the grains still sounding would read nothing, so they end here. Kept, they
        // would hold the current slot until their ends, and a later adoption that flipped back
        // into it would hand them its source mid-window. This ends the ring's grains too: a
        // source removed entirely is the one hard cut that remains, by design.
        clearGrains();
        currentSlot() = SourceSlot{};
        previousSlot() = SourceSlot{};
        return;
    }
    // Every sample grain still sounding now reads the new source, in the current slot; the
    // ring's grains (kLiveSlot) keep reading the ring.
    const int64_t lastEnd = std::max(slots_[0].lastEnd, slots_[1].lastEnd);
    currentSlot() = slotFor(source);
    currentSlot().lastEnd = lastEnd;
    previousSlot() = SourceSlot{};
    pool_.assignSourceSlot(current_, kLiveSlot);
}

void Engine::setPreparedSource(std::shared_ptr<const PreparedSource> prepared) noexcept
{
    replaceCurrent(prepared.get());
    owned_ = std::move(prepared); // frees the source the last setSource made, if no slot reads it
}

void Engine::replaceSource(const PreparedSource *source) noexcept
{
    assert(source == nullptr || source != owned_.get());
    replaceCurrent(source);
    owned_.reset(); // the source the last setSource made: no slot reads it any more
}

bool Engine::adoptSource(const PreparedSource *source) noexcept
{
    if (source == currentSlot().source)
    {
        return true; // already current
    }
#ifndef NDEBUG
    // The caller must not adopt a source it hasn't taken back yet (see Engine.h).
    for (uint32_t i = 0; i < retiredCount_; ++i)
    {
        assert(retired_[i] != source); // retired_ never holds nullptr
    }
#endif
    retirePrevious();
    if (previousSlot().source != nullptr || retiredCount_ > 1)
    {
        return false; // two sources in play, or setSource's hand-backs still waiting
    }
    // The free slot becomes the current one, and the old current one drains.
    current_ = static_cast<uint8_t>(1 - current_);
    currentSlot() = slotFor(source);
    // Retires at once if none of its grains still sound.
    retirePrevious();
    return true;
}

const PreparedSource *Engine::takeRetiredSource() noexcept
{
    if (retiredCount_ == 0)
    {
        return nullptr;
    }
    const PreparedSource *oldest = retired_[0];
    for (uint32_t i = 1; i < retiredCount_; ++i)
    {
        retired_[i - 1] = retired_[i];
    }
    retired_[--retiredCount_] = nullptr;
    return oldest;
}

void Engine::retirePrevious() noexcept
{
    SourceSlot &previous = previousSlot();
    if (previous.source == nullptr || previous.lastEnd > clock_)
    {
        return; // nothing draining, or its grains still sound
    }
    if (previous.source != owned_.get())
    {
        if (retiredCount_ != 0)
        {
            return; // one hand-back at a time
        }
        retired_[retiredCount_++] = previous.source;
    }
    previous = SourceSlot{};
}

void Engine::clearGrains() noexcept
{
    pool_.clear();
    for (SourceSlot &slot : slots_)
    {
        slot.lastEnd = std::numeric_limits<int64_t>::min();
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
    clearGrains();
    cloud_.reset();
    live_.reset();
    clock_ = 0;
    dropped_ = 0;
    retirePrevious(); // no grain reads it any more
}

void Engine::process(float *const *out, uint32_t numChannels, uint32_t numFrames) noexcept
{
    process(nullptr, 0, out, numChannels, numFrames);
}

void Engine::process(const float *const *in, uint32_t numInChannels, float *const *out,
                     uint32_t numChannels, uint32_t numFrames) noexcept
{
    ScopedDenormalsOff denormalsOff;
    // The source mode, the enable gate and the freeze setting apply from this call's first frame.
    if (requestedMode_ != mode_)
    {
        // Spec §6.4: the scheduler draws for the new kind from here, and the pool keeps its
        // grains, each finishing on the source it started on (its row's sourceSlot).
        mode_ = requestedMode_;
        cloud_.setSourceMode(mode_, config_.liveSeconds);
    }
    if (requestedEnabled_ != enabled_)
    {
        enabled_ = requestedEnabled_;
        if (enabled_)
        {
            cloud_.forceNextOnset(); // the first grain sounds at this frame (spec §6.2)
        }
        else
        {
            clearGrains(); // off: the grains still sounding end here
        }
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
    // retire grains early to make room. Disabled, nothing spawns (and the pool is empty).
    int64_t renderedTo = blockStart;
    const auto spawn = [&](const GrainDraw &d) {
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
        else if (currentSlot().source != nullptr)
        {
            admit(sampleGrain(d), dst, blockStart, renderedTo);
        }
    };
    if (enabled_)
    {
        cloud_.schedule(blockStart, numFrames, config_.seed, config_.layer, kStream, spawn);
    }
    renderPool(dst, blockStart, renderedTo, blockEnd);
    clock_ = blockEnd;
    pool_.retireFinished(blockEnd);
    // The previous source retires at the first block end at or past its last grain's end.
    retirePrevious();
}

GrainSpec Engine::sampleGrain(const GrainDraw &d) const noexcept
{
    // The draw's position is a fraction of the source; its rate is the pitch ratio, converted
    // here from the source's rate to the output rate. Both as in M0. A spray drawn in seconds
    // (CloudParams::sprayInSeconds) is converted at the source's rate and the sum clamped to the
    // source; otherwise sprayOffsetSec is +0 and positionDraw × span is already in [0, span],
    // so the start is M0's value. The one difference is the sign of a zero: a −0 start (from a
    // −0 position, which setParams passes through its clamp) becomes +0, the same frame. The
    // grain reads the source current at its onset to its end.
    const SourceSlot &source = slots_[current_];
    const double span = source.frames > 1 ? static_cast<double>(source.frames - 1) : 0.0;
    return {
        .index = d.index,
        .onsetSample = d.onsetSample,
        .onsetFrac = d.onsetFrac,
        .startPos = std::clamp(d.positionDraw * span + d.sprayOffsetSec * source.rate, 0.0, span),
        .rate = d.rate * source.rate / config_.sampleRate,
        .durationSamples = d.durationSamples,
        .texture = d.texture,
        .gain = d.gain,
        .pan = d.pan,
        .tier = d.tier,
        .sourceSlot = current_,
        .bus = d.bus,
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
        .texture = d.texture,
        .gain = d.gain,
        .pan = d.pan,
        .tier = d.tier,
        .sourceSlot = kLiveSlot,
        .bus = d.bus,
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
    if (g.sourceSlot != kLiveSlot)
    {
        // A sample slot's source retires only once its last grain has ended (retirePrevious).
        int64_t &lastEnd = slots_[g.sourceSlot].lastEnd;
        lastEnd = std::max(lastEnd, grainEndSample(g));
    }
    if (sink_ != nullptr)
    {
        sink_(sinkCtx_, config_.layer, g);
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
    // tier (ruling 2) from the source it latched at its onset (spec §6.4): the ring for
    // kLiveSlot, otherwise that sample slot's source, whatever the mode now in effect.
    const kernels::Output output{{dst[0], dst[1]}, blockStart};
    const GrainRows &rows = pool_.rows();
    const uint32_t n = pool_.size();
    const auto view = [](const SourceSlot &s) {
        return kernels::SampleSource{{s.data[0], s.data[1]}, s.frames, s.mono, s.interleaved};
    };
    const kernels::SampleSource sources[2] = {view(slots_[0]), view(slots_[1])};
    for (uint32_t i = 0; i < n; ++i)
    {
        if (rows.sourceSlot[i] == kLiveSlot)
        {
            if (rows.tier[i] == Tier::Eco)
            {
                kernels::renderLiveSimd<kernels::Linear>(rows, i, live_, output, from, to, hann_);
            }
            else
            {
                kernels::renderLiveSimd<kernels::Hermite>(rows, i, live_, output, from, to, hann_);
            }
            continue;
        }
        const kernels::SampleSource &source = sources[rows.sourceSlot[i]];
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
