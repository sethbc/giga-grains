// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/ParamMap.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>

#include "gg/core/MathDet.h"

namespace gg::core
{
namespace
{
/// The one reader of a ParamValues here: `slot` must be a parameter. ParamSlot::Count, which
/// layerSlot, levelSlot and layerOnSlot return for a layer of kLayers or more, asserts, and in
/// release builds reads the last slot rather than out of bounds, as Processor::base() does.
float read(const ParamValues &v, ParamSlot slot) noexcept
{
    const auto i = static_cast<size_t>(slot);
    assert(i < kParamCount);
    return v[std::min(i, kParamCount - 1)];
}

double value(const ParamValues &v, ParamSlot slot) noexcept
{
    return static_cast<double>(read(v, slot));
}

/// log2(10): dB × kLog2Of10 / 20 is log2 of the linear gain.
constexpr double kLog2Of10 = 3.321928094887362;

/// The largest float below 1: where the soft limiter stops (see softLimit).
constexpr float kBelowOne = 0x1.fffffep-1f;

/// Smear's law (M2 sub-project 4's spec §6.1, option 1): the target overlap T = 8 + 24s, the
/// position jitter's 0.5 × s × S seconds and the detune's 0.5 × s semitones.
constexpr double kSmearOverlapBase = 8.0;
constexpr double kSmearOverlapSpan = 24.0;
constexpr double kSmearJitter = 0.5;
constexpr double kSmearDetune = 0.5;

/// The Density parameter's ceiling in Hz (ParamIds.cpp): Smear raises no layer past what the
/// knob can reach.
constexpr double kDensityCeilingHz = 400.0;

/// The units a layer of built kind `kind` reads Position in: a Sample or Live layer its own;
/// a kind that reads its material through a stream of its own, the material's.
SourceMode positionUnits(SourceMode kind, SourceMode material) noexcept
{
    switch (kind)
    {
    case SourceMode::Sample:
    case SourceMode::Live:
        return kind;
    case SourceMode::Spectral:
        // The extremes spec §6.2: its Position addresses its material, a sample's fraction or
        // the ring's seconds behind the head, while the scheduler's Spectral mode draws its spray
        // one-sided, as Live's (sprayInSeconds is a Sample-mode flag, which it ignores).
        return material;
    }
    return material;
}
} // namespace

SourceKind sourceKindOf(const ParamValues &effective, uint8_t layer) noexcept
{
    assert(layer < kLayers);
    const float v = read(effective, layerSlot(LayerParam::Source, layer));
    // In range already (the Processor clamps every effective value); the clamp keeps a caller's
    // stray value an enumerator. floor(v + 0.5) is exact: the nearest integer, halves up.
    const float rounded = std::floor(std::clamp(v, 0.0f, 4.0f) + 0.5f);
    return static_cast<SourceKind>(static_cast<uint8_t>(rounded));
}

SourceMode builtKind(SourceKind kind, Mode mode) noexcept
{
    switch (kind)
    {
    case SourceKind::Sample:
        return SourceMode::Sample;
    case SourceKind::Live:
        return SourceMode::Live;
    case SourceKind::Spectral:
        return SourceMode::Spectral;
    case SourceKind::Resample:
    case SourceKind::Oscillator:
        break;
    }
    return mode == Mode::Fx ? SourceMode::Live : SourceMode::Sample;
}

CloudParams toCloudParams(const ParamValues &effective, Mode mode, uint8_t layer,
                          SourceMode material) noexcept
{
    assert(layer < kLayers);
    const auto param = [&effective, layer](LayerParam p) {
        return value(effective, layerSlot(p, layer));
    };
    const double density = exp2Det(param(LayerParam::Density));
    const double size = exp2Det(param(LayerParam::Size));
    const double smear = static_cast<double>(layerSmear(effective, layer));
    const double target = kSmearOverlapBase + kSmearOverlapSpan * smear;
    CloudParams p;
    // D_eff = D + s × (min(400, max(D, T / S)) − D). At s = 0 the product is ±0, so D_eff is D
    // bit for bit (S is at least 5 ms, so T / S is finite).
    p.densityHz =
        density + smear * (std::min(kDensityCeilingHz, std::max(density, target / size)) - density);
    p.distribution = param(LayerParam::Distribution);
    p.grainDurationSec = size;
    if (positionUnits(builtKind(sourceKindOf(effective, layer), mode), material) ==
        SourceMode::Sample)
    {
        p.position = param(LayerParam::Position);
        p.sprayInSeconds = true;
    }
    else
    {
        p.position = 16.0 / 255.0 * (exp2Det(8.0 * param(LayerParam::Position)) - 1.0);
    }
    p.spray = 2.0 / 255.0 * (exp2Det(8.0 * param(LayerParam::Spray)) - 1.0);
    p.pitchSemitones = param(LayerParam::Pitch);
    p.stereo = param(LayerParam::Stereo);
    p.texture = param(LayerParam::Texture);
    p.pitchSpreadSemitones = param(LayerParam::PitchSpread);
    p.sizeSpreadOctaves = param(LayerParam::SizeSpread);
    // Both 0 at s = 0, so the scheduler skips their draws and terms.
    p.smearJitterSec = kSmearJitter * smear * size;
    p.smearDetuneSemitones = kSmearDetune * smear;
    p.busSplit = static_cast<double>(layerBusSplit(effective, layer));
    p.gain = static_cast<float>(1.0 / std::sqrt(std::max(1.0, p.densityHz * p.grainDurationSec)));
    return p;
}

CloudParams toCloudParams(const ParamValues &effective, Mode mode, uint8_t layer) noexcept
{
    // The kind's own units: Sample's for a Sample layer, Live's for every other (a Spectral
    // layer's material is the Processor's to say: layerMaterial).
    const SourceMode kind = builtKind(sourceKindOf(effective, layer), mode);
    return toCloudParams(effective, mode, layer,
                         kind == SourceMode::Sample ? SourceMode::Sample : SourceMode::Live);
}

CloudParams toCloudParams(const ParamValues &effective, Mode mode) noexcept
{
    return toCloudParams(effective, mode, 0);
}

WetParams toWetParams(const ParamValues &effective, Mode mode, uint8_t layer) noexcept
{
    assert(layer < kLayers);
    const auto at = [&effective](ParamSlot slot) { return read(effective, slot); };
    const bool live = builtKind(sourceKindOf(effective, layer), mode) == SourceMode::Live;
    return {
        .levelGain =
            static_cast<float>(exp2Det(value(effective, levelSlot(layer)) * kLog2Of10 / 20.0)),
        .mix = at(ParamSlot::Mix),
        .freeze = at(layerSlot(LayerParam::Freeze, layer)) > 0.5f,
        .feedback = live ? at(layerSlot(LayerParam::Feedback, layer)) : 0.0f,
    };
}

WetParams toWetParams(const ParamValues &effective, Mode mode) noexcept
{
    return toWetParams(effective, mode, 0);
}

float layerDiffusion(const ParamValues &effective, uint8_t layer) noexcept
{
    assert(layer < kLayers);
    return read(effective, layerSlot(LayerParam::Diffusion, layer));
}

bool layerOn(const ParamValues &effective, uint8_t layer) noexcept
{
    assert(layer < kLayers);
    return read(effective, layerOnSlot(layer)) > 0.5f;
}

MorphParams toMorphParams(const ParamValues &effective) noexcept
{
    return {.x = read(effective, ParamSlot::MorphX), .y = read(effective, ParamSlot::MorphY)};
}

SpaceParams toSpaceParams(const ParamValues &effective, Mode mode) noexcept
{
    const auto at = [&effective](ParamSlot slot) { return read(effective, slot); };
    const float decay = at(ParamSlot::ReverbDecay);
    return {
        .diffusion = at(ParamSlot::Diffusion),
        .amount = at(ParamSlot::ReverbAmount),
        .drySend = mode == Mode::Instrument ? 0.0f : at(ParamSlot::DryToReverb),
        .decayLog2 = decay,
        .infinite = decay >= kDecayInfinite,
        .roomSize = 2.0f * at(ParamSlot::ReverbSize) - 1.0f,
        .hfDamping = at(ParamSlot::ReverbDamping),
        .modulation = at(ParamSlot::ReverbModulation),
        .predelaySeconds = at(ParamSlot::ReverbPredelay),
    };
}

namespace
{
/// x rounded to the nearest integer, halves up, after clamping to [lo, hi]: floor(x + 0.5) is
/// exact for these small values (sourceKindOf's rounding).
float roundedIn(float x, float lo, float hi) noexcept
{
    return std::floor(std::clamp(x, lo, hi) + 0.5f);
}

float exp2Of(const ParamValues &v, ParamSlot slot) noexcept
{
    return static_cast<float>(exp2Det(value(v, slot)));
}
} // namespace

LfoParams lfoParams(const ParamValues &effective, uint8_t layer, int which) noexcept
{
    assert(layer < kLayers);
    assert(which == 0 || which == 1);
    const bool first = which == 0;
    const ParamSlot rate = layerSlot(first ? LayerParam::Lfo1Rate : LayerParam::Lfo2Rate, layer);
    const ParamSlot shape = layerSlot(first ? LayerParam::Lfo1Shape : LayerParam::Lfo2Shape, layer);
    return {
        .rateHz = exp2Of(effective, rate),
        .shape = static_cast<LfoShape>(
            static_cast<uint8_t>(roundedIn(read(effective, shape), 0.0f, 4.0f))),
    };
}

AdsrParams envParams(const ParamValues &effective, uint8_t layer, int which) noexcept
{
    assert(layer < kLayers);
    assert(which == 0 || which == 1);
    const bool first = which == 0;
    const auto slot = [layer](LayerParam p) { return layerSlot(p, layer); };
    return {
        .attackSec =
            exp2Of(effective, slot(first ? LayerParam::Env1Attack : LayerParam::Env2Attack)),
        .decaySec = exp2Of(effective, slot(first ? LayerParam::Env1Decay : LayerParam::Env2Decay)),
        .sustain = read(effective, slot(first ? LayerParam::Env1Sustain : LayerParam::Env2Sustain)),
        .releaseSec =
            exp2Of(effective, slot(first ? LayerParam::Env1Release : LayerParam::Env2Release)),
    };
}

RandomParams randomParams(const ParamValues &effective, uint8_t layer, int which) noexcept
{
    assert(layer < kLayers);
    assert(which == 0 || which == 1);
    const bool first = which == 0;
    return {
        .rateHz = exp2Of(
            effective, layerSlot(first ? LayerParam::Random1Rate : LayerParam::Random2Rate, layer)),
        .smooth =
            read(effective,
                 layerSlot(first ? LayerParam::Random1Smooth : LayerParam::Random2Smooth, layer)),
    };
}

StepParams stepParams(const ParamValues &effective, uint8_t layer) noexcept
{
    assert(layer < kLayers);
    StepParams p{};
    p.rateHz = exp2Of(effective, layerSlot(LayerParam::StepRate, layer));
    p.length = static_cast<uint8_t>(
        roundedIn(read(effective, layerSlot(LayerParam::StepLength, layer)), 1.0f, 16.0f));
    for (size_t i = 0; i < p.steps.size(); ++i)
    {
        const auto step = static_cast<LayerParam>(static_cast<size_t>(LayerParam::Step1) + i);
        p.steps[i] = read(effective, layerSlot(step, layer));
    }
    return p;
}

float layerSmear(const ParamValues &effective, uint8_t layer) noexcept
{
    assert(layer < kLayers);
    return read(effective, layerSlot(LayerParam::Smear, layer));
}

float layerBusSplit(const ParamValues &effective, uint8_t layer) noexcept
{
    assert(layer < kLayers);
    return read(effective, layerSlot(LayerParam::BusSplit, layer));
}

bool isModulatorControlSlot(ParamSlot s) noexcept
{
    const std::optional<LayerRef> ref = layerOf(s);
    return ref && ref->param >= LayerParam::Lfo1Rate && ref->param <= LayerParam::Step16;
}

BusParams busParams(const ParamValues &effective, uint8_t layer) noexcept
{
    assert(layer < kLayers);
    static constexpr std::array<std::array<LayerParam, 3>, 2> kBusSlots{{
        {LayerParam::Bus1Filter, LayerParam::Bus1Cutoff, LayerParam::Bus1Resonance},
        {LayerParam::Bus2Filter, LayerParam::Bus2Cutoff, LayerParam::Bus2Resonance},
    }};
    BusParams p{};
    for (size_t bus = 0; bus < kBusSlots.size(); ++bus)
    {
        const auto &slots = kBusSlots[bus];
        p.type[bus] = static_cast<BusFilterType>(static_cast<uint8_t>(
            roundedIn(read(effective, layerSlot(slots[0], layer)), 0.0f, 4.0f)));
        p.cutoffHz[bus] = exp2Of(effective, layerSlot(slots[1], layer));
        p.resonance[bus] = read(effective, layerSlot(slots[2], layer));
    }
    return p;
}

DelayParams delayParams(const ParamValues &effective) noexcept
{
    const auto at = [&effective](ParamSlot slot) { return read(effective, slot); };
    return {
        .amount = at(ParamSlot::DelayAmount),
        .timeSeconds = exp2Of(effective, ParamSlot::DelayTime),
        .feedback = at(ParamSlot::DelayFeedback),
        .damping = at(ParamSlot::DelayDamping),
        .pingPong = at(ParamSlot::DelayPingPong) > 0.5f,
    };
}

LimiterParams limiterParams(const ParamValues &effective) noexcept
{
    return {
        .mode = static_cast<LimiterMode>(
            static_cast<uint8_t>(roundedIn(read(effective, ParamSlot::LimiterMode), 0.0f, 2.0f))),
        .ceilingGain = static_cast<float>(
            exp2Det(value(effective, ParamSlot::LimiterCeiling) * kLog2Of10 / 20.0)),
        .releaseSeconds = exp2Of(effective, ParamSlot::LimiterRelease),
    };
}

float softLimit(float x) noexcept
{
    const float a = std::abs(x);
    if (a <= 0.5f)
    {
        return x;
    }
    if (std::isnan(x))
    {
        return 0.0f;
    }
    // With z = (a − 0.5)/0.5 = 2a − 1, 0.5 + 0.5 × z/(1 + z) = 1 − 0.5/(1 + z) = 1 − 0.25/a.
    // That form is monotonic in floating point too: 0.25/a and 1 − t are each one correctly
    // rounded monotonic step, where z/(1 + z) divides two rounded values. Past about 2^23 it
    // rounds to 1, and the min keeps the result below 1; an infinite a gives exactly 1 − 0.
    const float y = std::min(1.0f - 0.25f / a, kBelowOne);
    return x < 0.0f ? -y : y;
}
} // namespace gg::core
