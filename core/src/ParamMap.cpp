// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/ParamMap.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>

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
    case SourceKind::Resample: // a Live-mode engine (M2 sub-project 2's spec §6.2)
        return SourceMode::Live;
    case SourceKind::Spectral:
    case SourceKind::Oscillator:
        break;
    }
    return mode == Mode::Fx ? SourceMode::Live : SourceMode::Sample;
}

CloudParams toCloudParams(const ParamValues &effective, Mode mode, uint8_t layer) noexcept
{
    assert(layer < kLayers);
    const auto param = [&effective, layer](LayerParam p) {
        return value(effective, layerSlot(p, layer));
    };
    CloudParams p;
    p.densityHz = exp2Det(param(LayerParam::Density));
    p.distribution = param(LayerParam::Distribution);
    p.grainDurationSec = exp2Det(param(LayerParam::Size));
    if (builtKind(sourceKindOf(effective, layer), mode) == SourceMode::Sample)
    {
        // The fraction stops at the file's end: Position's range reaches past 1 for the Live
        // kinds' longest ring (M2 sub-project 2's spec §6.3). Below 1 it is M1's value.
        p.position = std::min(param(LayerParam::Position), 1.0);
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
    // The nearest step (lround: halves away from zero, so halves up here), clamped first so that
    // lround's argument is finite; NaN, which the Processor never holds, is Normal.
    const double quality = param(LayerParam::Quality);
    p.quality = std::isnan(quality) ? Tier::Normal
                                    : static_cast<Tier>(std::lround(std::clamp(quality, 0.0, 2.0)));
    p.gain = static_cast<float>(1.0 / std::sqrt(std::max(1.0, p.densityHz * p.grainDurationSec)));
    return p;
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

uint64_t budgetUnits(const ParamValues &effective) noexcept
{
    const float v = read(effective, ParamSlot::Budget);
    // Clamped before rounding, which is the same for every finite count (the bounds are
    // integers) and keeps lround's argument finite; NaN fails both comparisons.
    const float count = std::isnan(v) ? 4096.0f : std::clamp(v, 256.0f, 16384.0f);
    return static_cast<uint64_t>(std::lround(count)) * kBudgetUnitsPerGrain;
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

RingParams toRingParams(const ParamValues &effective, uint8_t layer) noexcept
{
    assert(layer < kLayers);
    const auto slot = [layer](LayerParam p) { return layerSlot(p, layer); };
    return {
        .ringSeconds = exp2Det(value(effective, slot(LayerParam::RingLength))),
        .loop = read(effective, slot(LayerParam::Loop)) > 0.5f,
        .loopSeconds = exp2Det(value(effective, slot(LayerParam::LoopLength))),
        .overdub = read(effective, slot(LayerParam::Overdub)),
        .splice = static_cast<uint32_t>(roundedIn(read(effective, slot(LayerParam::Splice)), 0.0f,
                                                  static_cast<float>(kMaxSplices))),
    };
}

bool freezeOn(const ParamValues &values, uint8_t layer) noexcept
{
    assert(layer < kLayers);
    return read(values, layerSlot(LayerParam::Freeze, layer)) > 0.5f;
}

bool ringSaved(const ParamValues &base, Mode mode, uint8_t layer) noexcept
{
    assert(layer < kLayers);
    return builtKind(sourceKindOf(base, layer), mode) == SourceMode::Live &&
           (freezeOn(base, layer) || read(base, layerSlot(LayerParam::Loop, layer)) > 0.5f);
}

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
