// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/Processor.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>

#include "gg/core/DenormalGuard.h"
#include "gg/core/ParamMap.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

namespace gg::core
{
namespace
{
EngineConfig engineConfig(const ProcessorConfig &c)
{
    EngineConfig e;
    e.sampleRate = c.sampleRate;
    e.maxBlockSize = c.maxBlockSize;
    e.seed = c.seed;
    e.liveSeconds = c.mode == Mode::Fx ? Processor::kLiveSeconds : 0.0;
    return e;
}

/// R = round(kRampSeconds × sampleRate), at least 1. The min keeps llround's argument in range
/// for any finite rate.
int64_t rampLength(double sampleRate)
{
    const double samples = std::min(Processor::kRampSeconds * sampleRate, 0x1.0p40);
    return std::max<int64_t>(1, std::llround(samples));
}

/// The first grid boundary at or after the absolute sample a >= 0.
int64_t gridCeil(int64_t a) noexcept
{
    constexpr auto grid = static_cast<int64_t>(Processor::kControlGrid);
    return (a + grid - 1) / grid * grid;
}

float finiteOrZero(float x) noexcept { return std::isfinite(x) ? x : 0.0f; }
} // namespace

float Processor::Ramp::at(int64_t t, int64_t length) const noexcept
{
    const int64_t i = t - start;
    if (i >= length)
    {
        return to;
    }
    const auto f = static_cast<double>(from);
    const double delta = static_cast<double>(to) - f;
    return static_cast<float>(f + delta * (static_cast<double>(i) / static_cast<double>(length)));
}

void Processor::Ramp::retarget(float target, int64_t t, int64_t length) noexcept
{
    if (target == to)
    {
        return;
    }
    from = at(t, length);
    to = target;
    start = t;
}

void Processor::Ramp::jump(float target) noexcept
{
    from = target;
    to = target;
    start = 0;
}

Processor::Processor(const ProcessorConfig &config)
    : config_(config), engine_(engineConfig(config)), rampSamples_(rampLength(config.sampleRate))
{
    for (auto &channel : wet_)
    {
        channel.assign(config_.maxBlockSize, 0.0f);
    }
    // The first paramMeta() call builds its table, which allocates; make every call here, off
    // the audio thread, so that process() never makes it, and keep the ranges.
    for (size_t s = 0; s < kParamCount; ++s)
    {
        const auto &meta = paramMeta(static_cast<ParamSlot>(s));
        minVal_[s] = meta.minVal;
        maxVal_[s] = meta.maxVal;
        base_[s] = clampToRange(s, meta.defaultVal);
    }
    if (config_.mode == Mode::Fx)
    {
        engine_.setSourceMode(SourceMode::Live);
    }
    updateEffective();
    const WetParams w = toWetParams(effective_);
    mix_.jump(w.mix);
    level_.jump(w.levelGain);
}

float Processor::clampToRange(size_t slot, float value) const noexcept
{
    return std::clamp(value, minVal_[slot], maxVal_[slot]);
}

void Processor::setBase(ParamSlot slot, float value) noexcept
{
    const auto s = static_cast<size_t>(slot);
    if (s >= kParamCount || std::isnan(value))
    {
        return;
    }
    base_[s] = clampToRange(s, value);
    hasPendingBase_[s] = false;
    updateEffective();
    if (slot == ParamSlot::Freeze)
    {
        syncFreeze(); // Freeze set off is a transition's first half, even between calls
    }
    const WetParams w = toWetParams(effective_);
    if (slot == ParamSlot::Mix)
    {
        mix_.jump(w.mix);
    }
    else if (slot == ParamSlot::Level)
    {
        level_.jump(w.levelGain);
    }
}

// ParamSlot::Count asserts; in release builds it reads the last slot rather than out of bounds,
// as paramMeta does.
float Processor::base(ParamSlot slot) const noexcept
{
    const auto s = static_cast<size_t>(slot);
    assert(s < kParamCount);
    return base_[std::min(s, kParamCount - 1)];
}

float Processor::effective(ParamSlot slot) const noexcept
{
    const auto s = static_cast<size_t>(slot);
    assert(s < kParamCount);
    return effective_[std::min(s, kParamCount - 1)];
}

void Processor::commitPending() noexcept
{
    for (size_t s = 0; s < kParamCount; ++s)
    {
        if (hasPendingBase_[s])
        {
            base_[s] = pendingBase_[s];
        }
        if (hasPendingMod_[s])
        {
            mod_[s] = pendingMod_[s];
        }
    }
    hasPendingBase_.fill(false);
    hasPendingMod_.fill(false);
    hasPending_ = false;
}

void Processor::updateEffective() noexcept
{
    // base is finite and in range, and mod is never NaN, so the sum is never NaN; an infinite
    // mod clamps to an end of the range.
    for (size_t s = 0; s < kParamCount; ++s)
    {
        effective_[s] = clampToRange(s, base_[s] + mod_[s]);
    }
    engine_.setCloudParams(toCloudParams(effective_));
}

void Processor::syncFreeze() noexcept
{
    const bool on = toWetParams(effective_).freeze;
    if (!on)
    {
        freezeArmed_ = true; // off seen: the next on engages
    }
    engine_.setFreeze(on && freezeArmed_);
}

void Processor::reset() noexcept
{
    engine_.reset();
    commitPending();
    updateEffective();
    // A fresh ring: Freeze waits for an off→on transition, as after construction.
    freezeArmed_ = false;
    engine_.setFreeze(false);
    const WetParams w = toWetParams(effective_);
    mix_.jump(w.mix);
    level_.jump(w.levelGain);
}

void Processor::setEventSink(GrainEventSink sink, void *ctx) noexcept
{
    engine_.setEventSink(sink, ctx);
}

void Processor::process(const float *const *in, uint32_t numInChannels, float *const *out,
                        uint32_t numOutChannels, uint32_t numFrames,
                        std::span<const ParamEvent> events) noexcept
{
    // One guard for the whole call, the dry/wet sum included (spec §6.4).
    ScopedDenormalsOff denormalsOff;
    if (in == nullptr || config_.mode != Mode::Fx)
    {
        numInChannels = 0;
    }
    if (out == nullptr)
    {
        numOutChannels = 0;
    }
    const int64_t start = engine_.sampleClock();
    const int64_t end = start + static_cast<int64_t>(numFrames);
    const Buffers buffers{in, numInChannels, out, numOutChannels, start};

    // The Freeze in effect from the call's first sample: as at the last boundary, or as
    // setBase() or reset() left it.
    syncFreeze();

    // Renders up to the pending boundary and applies the pending changes there.
    int64_t rendered = start;
    const auto applyPending = [&] {
        assert(pendingAt_ < end);
        render(buffers, rendered, pendingAt_);
        rendered = pendingAt_;
        commitPending();
        updateEffective();
        syncFreeze();
        const WetParams w = toWetParams(effective_);
        mix_.retarget(w.mix, rendered, rampSamples_);
        level_.retarget(w.levelGain, rendered, rampSamples_);
    };

    for (const ParamEvent &e : events)
    {
        const auto s = static_cast<size_t>(e.slot);
        const auto value = static_cast<float>(e.value);
        if (s >= kParamCount || std::isnan(value))
        {
            continue;
        }
        const int64_t a = start + std::min<int64_t>(e.offset, numFrames);
        // max: an out-of-order event can't land before what is already rendered. rendered is
        // either the call's start, which a is at or after, or a boundary.
        const int64_t b = std::max(gridCeil(a), rendered);
        // Sorted events have non-decreasing boundaries, so a later one ends the pending group.
        // Every boundary but the last is at most end − 32, before this call's end.
        if (hasPending_ && b > pendingAt_)
        {
            applyPending();
        }
        if (!hasPending_)
        {
            pendingAt_ = b;
            hasPending_ = true;
        }
        if (e.isModulation)
        {
            pendingMod_[s] = value;
            hasPendingMod_[s] = true;
        }
        else
        {
            pendingBase_[s] = clampToRange(s, value);
            hasPendingBase_[s] = true;
        }
    }
    // A boundary at end carries over, like one past it: the next call's events at offset 0
    // belong to the same group, which a single call spanning both would apply once.
    if (hasPending_ && pendingAt_ < end)
    {
        applyPending();
    }
    render(buffers, rendered, end);

    for (uint32_t c = 2; c < numOutChannels; ++c)
    {
        if (out[c] != nullptr)
        {
            std::fill_n(out[c], numFrames, 0.0f);
        }
    }
}

void Processor::render(const Buffers &b, int64_t from, int64_t to) noexcept
{
    const bool fx = config_.mode == Mode::Fx;
    // Channels 0 and 1 carry the signal; one output channel gets the engine's mono render.
    const uint32_t channels = std::min(b.numOutChannels, 2u);
    float *const wet[2] = {wet_[0].data(), wet_[1].data()};
    while (from < to)
    {
        const auto n = static_cast<uint32_t>(
            std::min<int64_t>(to - from, static_cast<int64_t>(config_.maxBlockSize)));
        const auto offset = static_cast<size_t>(from - b.callStart);

        // This piece's input: a mono input feeds both channels; a missing one is silence.
        const float *dry[2] = {nullptr, nullptr};
        if (b.numInChannels > 0)
        {
            dry[0] = b.in[0];
            dry[1] = b.numInChannels == 1 ? b.in[0] : b.in[1];
        }
        for (const float *&d : dry)
        {
            d = d != nullptr ? d + offset : nullptr;
        }
        engine_.process(dry, b.numInChannels > 0 ? 2 : 0, wet, channels, n);
        if (channels == 0)
        {
            from += n;
            continue; // nothing to write; the engine has still run
        }

        float *dst[2] = {nullptr, nullptr};
        for (uint32_t c = 0; c < channels; ++c)
        {
            dst[c] = b.out[c] != nullptr ? b.out[c] + offset : nullptr;
        }
        for (uint32_t i = 0; i < n; ++i)
        {
            const int64_t t = from + i;
            const float m = mix_.at(t, rampSamples_);
            const float g = level_.at(t, rampSamples_);
            // Read both inputs before writing either output: out may be in, and a mono input
            // feeds both channels.
            float d[2] = {0.0f, 0.0f};
            for (uint32_t c = 0; c < channels; ++c)
            {
                d[c] = dry[c] != nullptr ? finiteOrZero(dry[c][i]) : 0.0f;
            }
            for (uint32_t c = 0; c < channels; ++c)
            {
                if (dst[c] == nullptr)
                {
                    continue;
                }
                const float w = softLimit(wet[c][i] * g);
                dst[c][i] = fx ? d[c] * (1.0f - m) + w * m : w;
            }
        }
        from += n;
    }
}
} // namespace gg::core
