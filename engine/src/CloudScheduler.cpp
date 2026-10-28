// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/grain/CloudScheduler.h"

namespace gg
{
namespace
{
constexpr double kMinDensityHz = 0.01;
constexpr double kMinDurationSamples = 16.0; // the minimum window length
// Keeps grainEndSample and the int64 sample arithmetic exact; about 3000 years at 48 kHz.
constexpr double kMaxDurationSamples = 0x1.0p52;
constexpr double kMaxPitchSemitones = 48.0;
constexpr float kMaxGain = 4.0f;

double finiteOr(double v, double fallback) noexcept { return std::isfinite(v) ? v : fallback; }
float finiteOr(float v, float fallback) noexcept { return std::isfinite(v) ? v : fallback; }
} // namespace

void CloudScheduler::prepare(double sampleRate) noexcept
{
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0) ? sampleRate : 48000.0;
    updateDerived();
    reset();
}

void CloudScheduler::setParams(const CloudParams &in) noexcept
{
    requested_ = in;
    const CloudParams d{};
    CloudParams p;
    // No bound here: 0 and below mean off, and [0.01, sampleRate] depends on the sample rate, so
    // updateDerived() applies it.
    p.densityHz = finiteOr(in.densityHz, d.densityHz);
    p.distribution = std::clamp(finiteOr(in.distribution, d.distribution), 0.0, 1.0);
    p.grainDurationSec = finiteOr(in.grainDurationSec, d.grainDurationSec);
    // Ruling M1-k: a fraction of the source in Sample mode, seconds behind the head in Live mode.
    const double maxPosition = mode_ == SourceMode::Live ? ringSeconds_ : 1.0;
    p.position = std::clamp(finiteOr(in.position, d.position), 0.0, maxPosition);
    p.spray = std::clamp(finiteOr(in.spray, d.spray), 0.0, maxPosition);
    p.pitchSemitones = std::clamp(finiteOr(in.pitchSemitones, d.pitchSemitones),
                                  -kMaxPitchSemitones, kMaxPitchSemitones);
    p.stereo = std::clamp(finiteOr(in.stereo, d.stereo), 0.0, 1.0);
    p.gain = std::clamp(finiteOr(in.gain, d.gain), 0.0f, kMaxGain);

    const double oldPeriod = period_;
    const double oldDistribution = params_.distribution;
    params_ = p;
    updateDerived();

    if (!(densityHz_ > 0.0))
    {
        // Off: once it is turned back on there is no previous grain to rescale from (rulings
        // M1-h, M1-j).
        hasLast_ = false;
        rescalePending_ = false;
    }
    else if (hasLast_ && (period_ != oldPeriod || params_.distribution != oldDistribution))
    {
        // Ruling M1-h. The next schedule() call rescales from the last grain with the values in
        // effect then, so several changes before it give the result of the last one alone.
        rescalePending_ = true;
    }
}

void CloudScheduler::setSourceMode(SourceMode mode, double ringSeconds) noexcept
{
    mode_ = mode;
    ringSeconds_ = std::isfinite(ringSeconds) && ringSeconds > 0.0 ? ringSeconds : 0.0;
    // The period and the distribution don't change, so this never moves the pending onset.
    setParams(requested_);
}

void CloudScheduler::reset() noexcept
{
    nextOnset_ = 0.0;
    nextK_ = 0;
    hasLast_ = false;
    rescalePending_ = false;
}

void CloudScheduler::updateDerived() noexcept
{
    if (params_.densityHz > 0.0)
    {
        // At most one onset per output sample on average, as in M0: the ceiling is the sample
        // rate (INTENT §23 #45, which amends ruling M1-a). The max() keeps the range valid
        // for a sample rate below 0.01 Hz.
        const double maxHz = std::max(kMinDensityHz, sampleRate_);
        densityHz_ = std::clamp(params_.densityHz, kMinDensityHz, maxHz);
        period_ = sampleRate_ / densityHz_;
    }
    else
    {
        densityHz_ = 0.0; // off (ruling M1-a)
        period_ = 0.0;
    }
    // The product can overflow to +inf, which the clamp maps to the maximum.
    durationSamples_ = std::clamp(params_.grainDurationSec * sampleRate_, kMinDurationSamples,
                                  kMaxDurationSamples);
    pitchRatio_ = exp2Det(params_.pitchSemitones / 12.0);
}
} // namespace gg
