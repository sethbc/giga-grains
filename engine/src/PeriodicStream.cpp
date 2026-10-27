// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/grain/PeriodicStream.h"

#include "gg/core/MathDet.h"

namespace gg
{
namespace
{
constexpr double kMaxRateHz = 4000.0;
constexpr double kMinDurationSamples = 16.0; // the minimum window length
// Keeps grainEndSample and the int64 sample arithmetic exact; about 3000 years at 48 kHz.
constexpr double kMaxDurationSamples = 0x1.0p52;
constexpr double kMaxPitchSemitones = 48.0;
constexpr float kMaxGain = 4.0f;

double finiteOr(double v, double fallback) noexcept { return std::isfinite(v) ? v : fallback; }
float finiteOr(float v, float fallback) noexcept { return std::isfinite(v) ? v : fallback; }
} // namespace

void PeriodicStream::prepare(double sampleRate) noexcept
{
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0) ? sampleRate : 48000.0;
    updateDerived();
    reset();
}

void PeriodicStream::setParams(const StreamParams &in) noexcept
{
    const StreamParams d{};
    StreamParams p;
    p.grainRateHz = std::min(finiteOr(in.grainRateHz, d.grainRateHz), kMaxRateHz);
    p.grainDurationSec = finiteOr(in.grainDurationSec, d.grainDurationSec);
    p.position = std::clamp(finiteOr(in.position, d.position), 0.0, 1.0);
    p.positionJitter = std::clamp(finiteOr(in.positionJitter, d.positionJitter), 0.0, 1.0);
    p.pitchSemitones = std::clamp(finiteOr(in.pitchSemitones, d.pitchSemitones),
                                  -kMaxPitchSemitones, kMaxPitchSemitones);
    p.gain = std::clamp(finiteOr(in.gain, d.gain), 0.0f, kMaxGain);
    params_ = p;

    const double oldRate = rateHz_;
    updateDerived();
    if (rateHz_ != oldRate)
    {
        restartLattice_ = true;
    }
}

void PeriodicStream::reset() noexcept
{
    nextK_ = 0;
    anchorK_ = 0;
    anchorTime_ = 0.0;
    lastOnset_ = 0.0;
    hasLastOnset_ = false;
    restartLattice_ = false;
}

void PeriodicStream::restartLattice(double blockStart) noexcept
{
    double anchor = blockStart;
    if (hasLastOnset_)
    {
        anchor = std::max(anchor, lastOnset_ + sampleRate_ / rateHz_);
    }
    anchorTime_ = anchor;
    anchorK_ = nextK_;
    restartLattice_ = false;
}

void PeriodicStream::updateDerived() noexcept
{
    // At most one onset per output sample, so schedule() emits at most numFrames grains.
    rateHz_ = params_.grainRateHz > 0.0 ? std::min(params_.grainRateHz, sampleRate_) : 0.0;
    // The product can overflow to +inf, which the clamp maps to the maximum.
    durationSamples_ = std::clamp(params_.grainDurationSec * sampleRate_, kMinDurationSamples,
                                  kMaxDurationSamples);
    pitchRatio_ = exp2Det(params_.pitchSemitones / 12.0);
}
} // namespace gg
