// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/ParamMap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "gg/core/MathDet.h"

namespace gg::core
{
namespace
{
double value(const ParamValues &v, ParamSlot slot) noexcept
{
    return static_cast<double>(v[static_cast<size_t>(slot)]);
}

/// log2(10): dB × kLog2Of10 / 20 is log2 of the linear gain.
constexpr double kLog2Of10 = 3.321928094887362;

/// The largest float below 1: where the soft limiter stops (see softLimit).
constexpr float kBelowOne = 0x1.fffffep-1f;
} // namespace

CloudParams toCloudParams(const ParamValues &effective, Mode mode) noexcept
{
    CloudParams p;
    p.densityHz = exp2Det(value(effective, ParamSlot::Density));
    p.distribution = value(effective, ParamSlot::Distribution);
    p.grainDurationSec = exp2Det(value(effective, ParamSlot::Size));
    if (mode == Mode::Instrument)
    {
        p.position = value(effective, ParamSlot::Position);
        p.sprayInSeconds = true;
    }
    else
    {
        p.position = 16.0 / 255.0 * (exp2Det(8.0 * value(effective, ParamSlot::Position)) - 1.0);
    }
    p.spray = 2.0 / 255.0 * (exp2Det(8.0 * value(effective, ParamSlot::Spray)) - 1.0);
    p.pitchSemitones = value(effective, ParamSlot::Pitch);
    p.stereo = value(effective, ParamSlot::Stereo);
    p.gain = static_cast<float>(1.0 / std::sqrt(std::max(1.0, p.densityHz * p.grainDurationSec)));
    return p;
}

WetParams toWetParams(const ParamValues &effective) noexcept
{
    return {
        .levelGain =
            static_cast<float>(exp2Det(value(effective, ParamSlot::Level) * kLog2Of10 / 20.0)),
        .mix = effective[static_cast<size_t>(ParamSlot::Mix)],
        .freeze = effective[static_cast<size_t>(ParamSlot::Freeze)] > 0.5f,
    };
}

SpaceParams toSpaceParams(const ParamValues &effective, Mode mode) noexcept
{
    const auto at = [&effective](ParamSlot slot) { return effective[static_cast<size_t>(slot)]; };
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
