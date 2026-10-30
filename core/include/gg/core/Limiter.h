// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_LIMITER_H
#define GG_CORE_LIMITER_H

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <sst/cpputils/rtsan_support.h>

#include "gg/core/FpContract.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"

namespace gg::core
{
/// The global limiter (extremes spec §6.5): one stereo stage on the wet side, after the reverb's
/// return and before the mix. The Processor owns one.
///
/// **The laws,** with c the ceiling's linear gain (> 0):
/// - **Off** is the identity: both samples pass untouched, with no arithmetic on them, bit for
///   bit (the sign of zero, infinities and NaN included).
/// - **Soft** is M1's limiter scaled to the ceiling, per channel: `c × softLimit(x / c)`, so at
///   c = 1 it is `softLimit(x)` bit for bit, and always below c in magnitude.
/// - **Peak** is a stereo-linked, zero-lookahead peak limiter: with p = max(|l|, |r|),
///   `g_rel = g_prev + releaseCoeff × (1 − g_prev)` and `g = min(g_rel, c / p)` (just g_rel when
///   p ≤ c); each channel's output is `g × x`. No latency and no attack time: a step is at the
///   ceiling on its first sample. The gain and the product are doubles, the product rounded to
///   float once: in float the release would stall short of 1 (once releaseCoeff × (1 − g) is
///   under half an ulp, about 1 − g < 1.5e-3 at a 1 s Release), and c / p and g × x rounded in
///   float could land one ulp over c. In double, g × |x| ≤ c (1 + 2⁻⁵³)², which rounds to at
///   most c: the output never passes the ceiling.
///   Peak's gain runs only while Peak is in use (its mode, or a fade's weight on it) and goes
///   back to 1 when it stops being, so entering Peak always starts from gain 1.
///
/// **A mode change crossfades.** When process() sees a mode other than the previous call's, the
/// three laws' weights move linearly, over rampSamples calls, from where they stand to the new
/// mode's alone; every law with a non-zero weight is computed during the fade, and the output is
/// their weighted sum, in the order Off, Soft, Peak. A change in the middle of a fade starts the
/// next one from the weights of the moment, so no change of mode ever jumps. The fade's last
/// call is the new mode's law alone, exactly. With rampSamples 0 a change applies at once.
///
/// **Determinism.** Plain arithmetic per sample (float, and Peak's gain in double), no libm, no
/// RNG, no block quantum: the output is the same however the calls are split into blocks.
class Limiter
{
  public:
    /// Sets the mode crossfade's length, in calls (the Processor's 10 ms ramp), then resets.
    /// sampleRate is finite and > 0; nothing here depends on it (the caller derives
    /// releaseCoeff). Allocates nothing; the mode stays as last set (Off on a new Limiter).
    void prepare(double sampleRate, uint32_t rampSamples);

    /// Peak's gain back to 1 and any fade ended: the next call is the last-set mode's law alone.
    void reset() noexcept;

    /// Snaps to `mode` with no fade: the weights are the mode's alone, Peak's gain 1, and the
    /// next call is the mode's law from its first sample. For the Processor's construction, a
    /// loaded state and reset, where a fade from Off would let 10 ms of the wet signal through
    /// unlimited.
    void jump(LimiterMode mode) noexcept;

    /// True when process() is the identity until the mode changes: the mode is Off and no fade
    /// is running. The Processor skips the wet side's split then.
    bool isOff() const noexcept { return mode_ == LimiterMode::Off && !fading_; }

    /// Limits one frame in place, per the laws above. ceiling is the linear ceiling (> 0);
    /// releaseCoeff, 0–1, is Peak's per-sample one-pole release coefficient.
    void process(float &l, float &r, LimiterMode mode, float ceiling,
                 float releaseCoeff) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        assert(static_cast<uint8_t>(mode) <= static_cast<uint8_t>(LimiterMode::Peak));
        if (mode != mode_)
            beginFade(mode);
        if (!fading_)
        {
            if (mode_ == LimiterMode::Off)
                return;
            assert(ceiling > 0.0f);
            if (mode_ == LimiterMode::Soft)
            {
                l = soft(l, ceiling);
                r = soft(r, ceiling);
            }
            else
            {
                peak(l, r, ceiling, releaseCoeff);
            }
            return;
        }

        assert(ceiling > 0.0f);
        if (++fadePos_ >= ramp_)
        {
            // The fade's last call: the new mode's law alone.
            settle();
            if (mode_ == LimiterMode::Soft)
            {
                l = soft(l, ceiling);
                r = soft(r, ceiling);
            }
            else if (mode_ == LimiterMode::Peak)
            {
                peak(l, r, ceiling, releaseCoeff);
            }
            return;
        }

        const float t = static_cast<float>(fadePos_) * invRamp_;
        const size_t to = index(mode_);
        for (size_t i = 0; i < kModes; ++i)
            weight_[i] = from_[i] + ((i == to ? 1.0f : 0.0f) - from_[i]) * t;

        float outL = 0.0f, outR = 0.0f;
        if (const float w = weight_[index(LimiterMode::Off)]; w != 0.0f)
        {
            outL = outL + w * l;
            outR = outR + w * r;
        }
        if (const float w = weight_[index(LimiterMode::Soft)]; w != 0.0f)
        {
            outL = outL + w * soft(l, ceiling);
            outR = outR + w * soft(r, ceiling);
        }
        if (const float w = weight_[index(LimiterMode::Peak)]; w != 0.0f)
        {
            float pl = l, pr = r;
            peak(pl, pr, ceiling, releaseCoeff);
            outL = outL + w * pl;
            outR = outR + w * pr;
        }
        l = outL;
        r = outR;
    }

    /// Peak's last gain, 1 while Peak isn't in use. For tests.
    float gain() const noexcept { return static_cast<float>(gain_); }

  private:
    static constexpr size_t kModes = 3;

    /// A mode's slot in the weights, clamped so a stray value can't index past them.
    static size_t index(LimiterMode m) noexcept
    {
        return std::min<size_t>(static_cast<size_t>(m), kModes - 1);
    }

    static float soft(float x, float c) noexcept
    {
        GG_FP_CONTRACT_OFF
        return c * softLimit(x / c);
    }

    void peak(float &l, float &r, float c, float k) noexcept
    {
        GG_FP_CONTRACT_OFF
        const float p = std::max(std::abs(l), std::abs(r));
        const double released = gain_ + static_cast<double>(k) * (1.0 - gain_);
        gain_ =
            p > c ? std::min(released, static_cast<double>(c) / static_cast<double>(p)) : released;
        l = static_cast<float>(gain_ * static_cast<double>(l));
        r = static_cast<float>(gain_ * static_cast<double>(r));
    }

    /// Starts the fade to mode from the weights of the moment, or applies it at once with no ramp.
    void beginFade(LimiterMode mode) noexcept
    {
        mode_ = mode;
        if (ramp_ == 0)
        {
            settle();
            return;
        }
        from_ = weight_;
        fadePos_ = 0;
        fading_ = true;
    }

    /// Ends any fade: the mode's weight 1, the others 0, and Peak's gain 1 unless Peak is the mode.
    void settle() noexcept
    {
        weight_ = {};
        weight_[index(mode_)] = 1.0f;
        fading_ = false;
        fadePos_ = 0;
        if (mode_ != LimiterMode::Peak)
            gain_ = 1.0;
    }

    LimiterMode mode_{LimiterMode::Off}; ///< The last call's mode, which the weights move toward.
    std::array<float, kModes> weight_{1.0f, 0.0f, 0.0f}; ///< Off, Soft, Peak.
    std::array<float, kModes> from_{1.0f, 0.0f, 0.0f};   ///< The weights at the fade's start.
    uint32_t ramp_{0};                                   ///< The fade's length, in calls.
    float invRamp_{0.0f};                                ///< 1 / ramp_, or 0 with no ramp.
    uint32_t fadePos_{0}; ///< Calls into the fade, 1 to ramp_ while fading_.
    bool fading_{false};
    double gain_{1.0}; ///< Peak's gain (a double: see the class comment).
};
} // namespace gg::core

#endif // GG_CORE_LIMITER_H
