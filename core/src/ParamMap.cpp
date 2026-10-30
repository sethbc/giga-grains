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
        return SourceMode::Live;
    case SourceKind::Resample:
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

ParamValues macroOffsets(const ParamValues &effectiveMacros, Mode mode) noexcept
{
    // Every entry starts at +0.0f, and each recipe's term is exactly 0 at rest: 2 × 0.5f − 1 is
    // exactly 0, so are max(0, 0), 0/0.75, smoothstep below its edge and 0 × 0. +0 plus or minus
    // (±0) is +0, so a resting entry stays +0.0f.
    ParamValues o{};
    const auto macro = [&effectiveMacros](ParamSlot slot) { return read(effectiveMacros, slot); };
    const auto offset = [&o](ParamSlot slot) -> float & { return o[static_cast<size_t>(slot)]; };
    const auto bipolar = [](float v) { return 2.0f * v - 1.0f; };

    const float b1 = bipolar(macro(ParamSlot::Macro1));
    offset(ParamSlot::Size) += 3.0f * b1;
    offset(ParamSlot::Density) -= 1.5f * b1;

    const float b2 = bipolar(macro(ParamSlot::Macro2));
    offset(ParamSlot::Density) += 4.0f * b2;
    offset(ParamSlot::Distribution) += 0.25f * std::max(b2, 0.0f);

    offset(ParamSlot::Pitch) += 24.0f * bipolar(macro(ParamSlot::Macro3));

    const float b4 = bipolar(macro(ParamSlot::Macro4));
    offset(ParamSlot::Spray) += 0.5f * b4;
    offset(ParamSlot::Stereo) += 0.3f * std::max(b4, 0.0f);

    const float u5 = macro(ParamSlot::Macro5);
    offset(ParamSlot::Texture) += std::min(u5 / 0.75f, 1.0f);
    offset(ParamSlot::Diffusion) += smoothstep(0.75f, 1.0f, u5);

    offset(ParamSlot::Freeze) += macro(ParamSlot::Macro6);

    // Macro 7 is Macro Feedback in both products (M2 sub-project 1's spec §6.9, §6.10); Feedback
    // acts on a layer that reads its live ring (toWetParams).
    const float v7 = macro(ParamSlot::Macro7);
    offset(ParamSlot::Feedback) += 0.9f * (v7 * v7);

    const float v8 = macro(ParamSlot::Macro8);
    if (mode == Mode::Instrument)
    {
        offset(ParamSlot::Level) += 12.0f * bipolar(v8);
    }
    else
    {
        offset(ParamSlot::Mix) += 0.5f * bipolar(v8);
    }
    return o;
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
