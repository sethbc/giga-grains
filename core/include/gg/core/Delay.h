// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_DELAY_H
#define GG_CORE_DELAY_H

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <sst/cpputils/rtsan_support.h>

#include "gg/core/FpContract.h"
#include "gg/core/ParamMap.h"

namespace gg::core
{
/// The global delay (extremes spec §6.4): a stereo line of up to kMaxSeconds with feedback
/// through a one-pole damper and M1's soft limiter, optionally ping-pong, its return added to
/// the input by an amount. The Processor owns one, after the layers' sum and before the reverb.
///
/// **The law,** per channel c, each sample:
/// 1. the read's delay moves toward its target by at most kGlideMaxRate (see setTimeSamples);
/// 2. `y_c` is the line read that many samples back, linearly interpolated at the fraction;
/// 3. the damper `d_c = d_c + dampCoeff × (y_src − d_c)`, where `y_src` is `y_c`, or the other
///    channel's read with ping-pong on; dampCoeff 1 passes its input, 0 holds;
/// 4. the line is written `line_c = in_c + feedback × softLimit(d_c)`, so with feedback ≤ 1 the
///    loop stays below 1 in magnitude, and at feedback 1 with no damping holds a repeat forever;
/// 5. the output is `in_c + amount × y_c`, **added only when amount != 0**: at 0 the frame
///    passes untouched, bit for bit (the sign of zero included).
///
/// **The line is written every call,** whatever the amount, so raising the amount from 0
/// reveals what was already playing, as the diffuser does.
///
/// **Determinism.** Plain float arithmetic per sample (the read's position is a double), no
/// libm, no RNG, no block quantum: the output is the same however the calls are split into
/// blocks.
class Delay
{
  public:
    /// The longest delay, in seconds (Delay Time's top).
    static constexpr double kMaxSeconds = 2.0;

    /// How fast the read's delay moves toward its target, in samples per sample: Space's
    /// pre-delay rule (ruling P1), so a move bends pitch like tape and never clicks.
    static constexpr double kGlideMaxRate = 0.5;

    /// Sizes the line for sampleRate (finite and > 0): ceil(kMaxSeconds × sampleRate) + 1 stereo
    /// frames, cleared. Allocates: call it off the audio thread. The target goes back to 1
    /// sample, and applies at once (see setTimeSamples).
    void prepare(double sampleRate);

    /// Clears the line and the dampers, and forgets the glide: the read stands at its target,
    /// and the next setTimeSamples applies at once (ruling R-T2a).
    void reset() noexcept;

    /// The read's target delay, in samples, clamped to [1, kMaxSeconds × the sample rate]; NaN
    /// counts as 1. While process() hasn't run since prepare() or reset(), the read moves there
    /// at once; otherwise it glides there at kGlideMaxRate.
    void setTimeSamples(double target) noexcept
    {
        double t = target > 1.0 ? target : 1.0;
        t = std::min(t, maxDelay_);
        target_ = t;
        if (!ran_)
            delay_ = t;
    }

    /// The read's current delay, in samples. For tests.
    double timeSamples() const noexcept { return delay_; }

    /// Delays one frame in place, per the law above. amount, feedback and dampCoeff are 0–1.
    /// prepare() must have been called.
    void process(float &l, float &r, float amount, float feedback, float dampCoeff,
                 bool pingPong) noexcept SST_CPPUTILS_NONBLOCKING
    {
        GG_FP_CONTRACT_OFF
        assert(!lineL_.empty() && "prepare() must be called first");
        ran_ = true;
        delay_ += std::clamp(target_ - delay_, -kGlideMaxRate, kGlideMaxRate);

        // delay_ is in [1, size − 1], so whole is too, and whole + 1 ≤ size: the oldest frame,
        // at pos_ itself, is read before this call overwrites it.
        const size_t size = lineL_.size();
        const auto whole = static_cast<size_t>(delay_);
        const auto frac = static_cast<float>(delay_ - static_cast<double>(whole));
        const size_t a = pos_ >= whole ? pos_ - whole : pos_ + size - whole;
        const size_t b = a == 0 ? size - 1 : a - 1;
        const float yl = lineL_[a] + frac * (lineL_[b] - lineL_[a]);
        const float yr = lineR_[a] + frac * (lineR_[b] - lineR_[a]);

        dampL_ = dampL_ + dampCoeff * ((pingPong ? yr : yl) - dampL_);
        dampR_ = dampR_ + dampCoeff * ((pingPong ? yl : yr) - dampR_);
        lineL_[pos_] = l + feedback * softLimit(dampL_);
        lineR_[pos_] = r + feedback * softLimit(dampR_);
        if (++pos_ == size)
            pos_ = 0;

        if (amount != 0.0f)
        {
            l = l + amount * yl;
            r = r + amount * yr;
        }
    }

  private:
    std::vector<float> lineL_, lineR_;
    size_t pos_{0};        ///< Where this call writes; the oldest frame until it does.
    double maxDelay_{1.0}; ///< kMaxSeconds × the sample rate, in samples.
    double target_{1.0};   ///< The read's target delay, in samples.
    double delay_{1.0};    ///< The read's delay now, in samples (64-bit: a position).
    float dampL_{0.0f}, dampR_{0.0f};
    bool ran_{false}; ///< process() has run since prepare() or reset().
};
} // namespace gg::core

#endif // GG_CORE_DELAY_H
