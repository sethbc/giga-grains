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
    // Spec §6.6: the pool's ceiling (it stores 2 × grainCapacity grains).
    if (config.grainCapacity > kMaxGrainCapacity)
    {
        throw std::invalid_argument("Engine: grainCapacity must be at most 16384");
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

void Engine::setStealSink(GrainStealSink sink, void *ctx) noexcept
{
    stealSink_ = sink;
    stealCtx_ = ctx;
}

void Engine::reset() noexcept
{
    clearGrains();
    cloud_.reset();
    live_.reset();
    clock_ = 0;
    dropped_ = 0;
    lowered_ = 0;
    stolen_ = 0;
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
    quota_ = requestedQuota_; // spec §6.4: from this call's first frame
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
    };
    return true;
}

void Engine::admit(const GrainSpec &draw, float *const dst[2], int64_t blockStart,
                   int64_t &renderedTo) noexcept
{
    const int64_t onset = draw.onsetSample;
    if (pool_.storageFull())
    {
        // Many grains finished in this block. Finish the pool up to the onset, so the grains
        // retired here have played all their samples, then retire them, before the ladder looks
        // at the rows: what is left is the grains still sounding at the onset. Retiring early
        // changes no output and no decision, which depend only on the grains still sounding.
        renderPool(dst, blockStart, renderedTo, onset);
        renderedTo = onset;
        pool_.retireFinished(onset);
        if (pool_.storageFull())
        {
            // Every stored grain still sounds: 2 × capacity, which only grains stolen faster
            // than their fades end can make. Nothing can be stored, so the draw drops.
            ++dropped_;
            return;
        }
    }

    // The overload ladder (spec §6.5; the class comment). Count only the grains still sounding
    // at the onset, by their latched ends and weights, so admission doesn't depend on where the
    // block boundaries fall (the pool may still store grains that finished earlier in the block,
    // because retirement normally waits for the block's end; carry-over row 51).
    const bool stereo = draw.sourceSlot == kLiveSlot || !slots_[draw.sourceSlot].mono;
    const uint64_t load = pool_.soundingWeight(onset);
    // The highest tier t <= the draw's with base + w(t) <= quota_, into `tier`.
    const auto fits = [this, &draw, stereo](uint64_t base, Tier &tier) {
        for (int t = static_cast<int>(draw.tier); t >= 0; --t)
        {
            const auto candidate = static_cast<Tier>(t);
            if (base + grainWeight(candidate, stereo) <= quota_)
            {
                tier = candidate;
                return true;
            }
        }
        return false;
    };
    Tier tier = draw.tier;
    bool admitted = pool_.soundingCount(onset) < pool_.capacity() && fits(load, tier);
    if (!admitted)
    {
        // Rung 3: the oldest unstolen sounding grain, once it is half-way, and only if its
        // weight makes room. Its weight stays in the ledger until its new end; the draw is
        // admitted on its credit. The room test leaves out the grains already fading (ruling
        // ES-T2a), each of which made room for a grain it is yielding to, so under a spent quota
        // every draw may steal one grain and the stream refreshes at its onset rate.
        const GrainRows &rows = pool_.rows();
        uint32_t row = 0;
        if (pool_.oldestSounding(onset, row) &&
            static_cast<double>(onset - rows.onsetSample[row]) - rows.onsetFrac[row] >=
                kStealThreshold * rows.durationSamples[row] &&
            fits(load - pool_.fadingWeight(onset) - rows.weight[row], tier))
        {
            const uint64_t index = rows.index[row];
            pool_.steal(row, onset);
            ++stolen_;
            if (stealSink_ != nullptr)
            {
                stealSink_(stealCtx_, config_.layer, index, onset);
            }
            admitted = true;
        }
    }
    if (!admitted)
    {
        ++dropped_;
        return;
    }
    lowered_ += tier != draw.tier ? 1 : 0;
    GrainSpec g = draw;
    g.tier = tier;
    g.weight = static_cast<uint8_t>(grainWeight(tier, stereo));
    // Can't fail: storage isn't full (above), and a steal stores nothing.
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
    const kernels::Tables tables{hann_, sinc_};
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
                kernels::renderLiveSimd<kernels::Linear>(rows, i, live_, output, from, to, tables);
            }
            else if (rows.tier[i] == Tier::HQ)
            {
                kernels::renderLiveSimd<kernels::Sinc>(rows, i, live_, output, from, to, tables);
            }
            else
            {
                kernels::renderLiveSimd<kernels::Hermite>(rows, i, live_, output, from, to, tables);
            }
            continue;
        }
        const kernels::SampleSource &source = sources[rows.sourceSlot[i]];
        if (rows.tier[i] == Tier::Eco)
        {
            kernels::renderSampleSimd<kernels::Linear>(rows, i, source, output, from, to, tables);
        }
        else if (rows.tier[i] == Tier::HQ)
        {
            kernels::renderSampleSimd<kernels::Sinc>(rows, i, source, output, from, to, tables);
        }
        else
        {
            kernels::renderSampleSimd<kernels::Hermite>(rows, i, source, output, from, to, tables);
        }
    }
}
} // namespace gg
