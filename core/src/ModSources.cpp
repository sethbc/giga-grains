// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/ModSources.h"

#include <cmath>
#include <cstddef>
#include <limits>

#include "gg/core/MathDet.h"

namespace gg::core
{
namespace
{
/// The largest phase increment a cell may take: far beyond any rate a control can set, and small
/// enough that the count of periods crossed converts to uint64_t exactly.
constexpr double kMaxIncrement = 4294967296.0; // 2^32

/// Advances a phase in [0, 1) by `increment` periods and wraps it back into [0, 1); returns how
/// many whole periods it crossed. A non-finite, negative or zero increment holds the phase.
/// For a phase ≥ 1, phase − floor(phase) is exact (Sterbenz), so the wrap never rounds up to 1.
uint64_t advancePhase(double &phase, double increment) noexcept
{
    if (!(increment > 0.0))
    {
        return 0;
    }
    phase += increment < kMaxIncrement ? increment : kMaxIncrement;
    const double whole = std::floor(phase);
    phase -= whole;
    return static_cast<uint64_t>(whole);
}

/// x's fraction in [0, 1); 0 for a non-finite x. (x − floor(x) can round to 1 for a tiny
/// negative x, which is the same phase as 0.)
double fraction(double x) noexcept
{
    const double f = x - std::floor(x);
    return f >= 0.0 && f < 1.0 ? f : 0.0;
}

/// A time control in seconds, clamped below at one cell; a non-finite one counts as one cell.
double atLeastOneCell(float seconds, double cellSeconds) noexcept
{
    const double t = static_cast<double>(seconds);
    return t > cellSeconds && t <= std::numeric_limits<double>::max() ? t : cellSeconds;
}

/// Below this, an envelope segment counts as at its end: it absorbs the increments' rounding,
/// so a segment of N cells ends on its Nth cell.
constexpr double kSegmentSnap = 1e-9;

/// Below this the follower's envelope is 0, so its release never reaches subnormals, whose
/// arithmetic would depend on the FPU's flush-to-zero mode.
constexpr float kFollowerFloor = 1e-20f;
} // namespace

float sineDet(float phase) noexcept
{
    GG_FP_CONTRACT_OFF
    float p = phase - std::floor(phase);
    if (!(p >= 0.0f && p < 1.0f))
    {
        p = 0.0f; // a non-finite phase, or the wrap of a tiny negative one rounding to 1
    }
    // sin(2πp) = −sin(2π(p − ½)) for p in [½, 1); both subtractions below are exact (Sterbenz).
    const bool negative = p >= 0.5f;
    if (negative)
    {
        p -= 0.5f;
    }
    // sin(2πp) = sin(2π(½ − p)) for p in [¼, ½].
    if (p > 0.25f)
    {
        p = 0.5f - p;
    }
    const float x = 4.0f * p; // exact: a power-of-two scaling
    const float x2 = x * x;
    const auto &c = detail::kSineDetCoeffs;
    float s = ((c[3] * x2 + c[2]) * x2 + c[1]) * x2 + c[0];
    s *= x;
    // The minimax error's last ripple lifts the polynomial a few ulp above 1 just below x = 1.
    s = s < 1.0f ? s : 1.0f;
    return negative ? -s : s;
}

void Lfo::reset() noexcept { phase_ = 0.0; }

float Lfo::step(const LfoParams &params, double cellSeconds) noexcept
{
    const float p = static_cast<float>(phase_);
    float value = 0.0f;
    switch (params.shape)
    {
    case LfoShape::Sine:
        value = 0.5f + 0.5f * sineDet(p);
        break;
    case LfoShape::Triangle:
        value = p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;
        break;
    case LfoShape::SawUp:
        value = p;
        break;
    case LfoShape::SawDown:
        value = 1.0f - p;
        break;
    case LfoShape::Square:
        value = p < 0.5f ? 1.0f : 0.0f;
        break;
    }
    advancePhase(phase_, static_cast<double>(params.rateHz) * cellSeconds);
    return value;
}

void Adsr::reset() noexcept
{
    level_ = 0.0;
    releaseFrom_ = 0.0;
    stage_ = Stage::Idle;
    gate_ = false;
}

float Adsr::step(const AdsrParams &params, bool gate, double cellSeconds) noexcept
{
    const float out = static_cast<float>(level_);
    if (gate && !gate_)
    {
        stage_ = Stage::Attack;
    }
    else if (!gate && gate_ && stage_ != Stage::Idle)
    {
        stage_ = Stage::Release;
        releaseFrom_ = level_;
    }
    gate_ = gate;

    const double sustain =
        params.sustain >= 0.0f ? (params.sustain <= 1.0f ? params.sustain : 1.0f) : 0.0f;
    switch (stage_)
    {
    case Stage::Idle:
        level_ = 0.0;
        break;
    case Stage::Attack:
        level_ += cellSeconds / atLeastOneCell(params.attackSec, cellSeconds);
        if (level_ >= 1.0 - kSegmentSnap)
        {
            level_ = 1.0;
            stage_ = Stage::Decay;
        }
        break;
    case Stage::Decay:
        level_ -= (1.0 - sustain) * cellSeconds / atLeastOneCell(params.decaySec, cellSeconds);
        if (level_ <= sustain + kSegmentSnap)
        {
            level_ = sustain;
            stage_ = Stage::Sustain;
        }
        break;
    case Stage::Sustain:
        level_ = sustain;
        break;
    case Stage::Release:
        level_ -= releaseFrom_ * cellSeconds / atLeastOneCell(params.releaseSec, cellSeconds);
        if (level_ <= kSegmentSnap)
        {
            level_ = 0.0;
            stage_ = Stage::Idle;
        }
        break;
    }
    return out;
}

void RandomSource::reset() noexcept
{
    phase_ = 0.0;
    k_ = 0;
    target_ = 0.0f;
    out_ = 0.0f;
    started_ = false;
}

float RandomSource::step(const RandomParams &params, double cellSeconds, uint64_t seed,
                         uint32_t layer, rng::Field field) noexcept
{
    auto draw = [&](uint64_t k) noexcept {
        return static_cast<float>(rng::uniform01(rng::hash({seed, layer, 0, field, k})));
    };
    if (!started_)
    {
        k_ = 0;
        target_ = draw(0);
        out_ = target_;
        started_ = true;
    }
    const float result = out_;

    const double rate = static_cast<double>(params.rateHz);
    const uint64_t crossed = advancePhase(phase_, rate * cellSeconds);
    if (crossed > 0)
    {
        k_ += crossed;
        target_ = draw(k_);
    }
    const double smooth = static_cast<double>(params.smooth);
    if (!(smooth > 0.0))
    {
        out_ = target_;
    }
    else
    {
        // cellSeconds / τ with τ = smooth / rate; a rate that holds the phase holds the glide.
        double g = rate > 0.0 ? cellSeconds * rate / smooth : 0.0;
        g = g < 1.0 ? g : 1.0;
        out_ += (target_ - out_) * static_cast<float>(g);
    }
    return result;
}

void StepSequencer::reset() noexcept
{
    phase_ = 0.0;
    k_ = 0;
}

float StepSequencer::step(const StepParams &params, double cellSeconds) noexcept
{
    const uint64_t length = params.length < 1 ? 1 : (params.length > 16 ? 16 : params.length);
    const float value = params.steps[static_cast<std::size_t>(k_ % length)];
    k_ += advancePhase(phase_, static_cast<double>(params.rateHz) * cellSeconds);
    return value;
}

EnvFollower::EnvFollower(double sampleRate) noexcept
{
    constexpr double kLn2 = 0.6931471805599453;
    const double cellSeconds = sampleRate > 0.0 ? 32.0 / sampleRate : 0.0;
    attack_ = static_cast<float>(exp2Det(-cellSeconds / (0.005 * kLn2)));
    release_ = static_cast<float>(exp2Det(-cellSeconds / (0.2 * kLn2)));
}

void EnvFollower::reset() noexcept { env_ = 0.0f; }

float EnvFollower::step(const float *const *in, uint32_t channels, uint32_t frames) noexcept
{
    GG_FP_CONTRACT_OFF
    float peak = 0.0f;
    if (in != nullptr)
    {
        for (uint32_t c = 0; c < channels; ++c)
        {
            const float *ch = in[c];
            if (ch == nullptr)
            {
                continue;
            }
            for (uint32_t i = 0; i < frames; ++i)
            {
                const float a = std::fabs(ch[i]);
                // NaN fails both comparisons and infinity the second: each counts as 0.
                if (a > peak && a <= std::numeric_limits<float>::max())
                {
                    peak = a;
                }
            }
        }
    }
    env_ = peak + (env_ - peak) * (peak > env_ ? attack_ : release_);
    if (env_ < kFollowerFloor)
    {
        env_ = 0.0f;
    }
    return env_ < 1.0f ? env_ : 1.0f;
}

void TransportSource::set(const Transport &t) noexcept { t_ = t; }

void TransportSource::step(double cellSeconds) noexcept
{
    if (!t_.playing)
    {
        return;
    }
    const double beats = t_.tempoBpm * cellSeconds / 60.0;
    if (std::isfinite(beats))
    {
        t_.beatPosition += beats;
    }
}

float TransportSource::beatPhase() const noexcept
{
    return t_.hasBeats ? static_cast<float>(fraction(t_.beatPosition)) : 0.0f;
}

float TransportSource::barPhase() const noexcept
{
    if (!t_.hasBeats)
    {
        return 0.0f;
    }
    const double beatsPerBar = t_.beatsPerBar > 1 ? static_cast<double>(t_.beatsPerBar) : 1.0;
    return static_cast<float>(fraction(t_.beatPosition / beatsPerBar));
}

float TransportSource::playing() const noexcept { return t_.playing ? 1.0f : 0.0f; }

void LayerModulators::reset() noexcept
{
    for (auto &l : lfo)
    {
        l.reset();
    }
    for (auto &e : env)
    {
        e.reset();
    }
    for (auto &r : random)
    {
        r.reset();
    }
    stepSeq.reset();
}
} // namespace gg::core
