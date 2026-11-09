// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_AMPENVELOPE_H
#define GG_GRAIN_AMPENVELOPE_H

#include <algorithm>
#include <cstdint>

#include "gg/core/FpContract.h"

namespace gg
{
/// A voice's amplitude (M3 Voice spec V1): the state of its linear ADSR (Env 1) at sample `at`,
/// as the Processor hands it to the engine at a cell, with the slopes that continue it. valueAt
/// is that continuation in closed form, so the engine can read the envelope at any later sample,
/// a grain's peak, without stepping it: what the ADSR would reach there were its gate to stay as
/// it stands at `at`.
struct AmpEnvelope
{
    /// The sample the state below is at, in output samples (the engine's clock).
    int64_t at{0};
    /// The level at `at`, in [0, 1].
    double level{0.0};
    enum class Stage : uint8_t
    {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release,
    } stage{Stage::Idle};
    /// The slopes, in level per output sample, each >= 0: the attack rises to 1 at
    /// attackPerSample, the decay falls from 1 to `sustain` at decayPerSample, the release falls
    /// to 0 at releasePerSample.
    double attackPerSample{0.0};
    double decayPerSample{0.0};
    double releasePerSample{0.0};
    /// The level the decay ends at and the sustain holds, in [0, 1].
    double sustain{1.0};

    /// The level at sample t >= at, clamped to [0, 1] (t before `at` reads the level at `at`):
    /// - Idle: 0;
    /// - Attack: level + attackPerSample × (t − at) until it reaches 1, then the decay from 1
    ///   (decayPerSample × the samples since reaching it) down to sustain, which then holds;
    ///   an attack that doesn't rise (slope 0) holds its level;
    /// - Decay: level − decayPerSample × (t − at), held at sustain once it gets there;
    /// - Sustain: sustain (the stage follows the sustain control, as the ADSR does);
    /// - Release: level − releasePerSample × (t − at), held at 0 once it gets there.
    /// Deterministic: no libm, no contraction.
    double valueAt(int64_t t) const noexcept
    {
        GG_FP_CONTRACT_OFF
        const double dt = t > at ? static_cast<double>(t - at) : 0.0;
        const double s = clamp01(sustain);
        double v = 0.0;
        switch (stage)
        {
        case Stage::Idle:
            v = 0.0;
            break;
        case Stage::Attack:
        {
            const double from = clamp01(level);
            const double rise = attackPerSample * dt;
            if (from + rise < 1.0)
            {
                v = from + rise;
            }
            else
            {
                // The attack-to-decay corner, (1 − from) / attackPerSample samples after `at`
                // (0 when already at 1), then the decay from 1.
                const double toPeak = from < 1.0 ? (1.0 - from) / attackPerSample : 0.0;
                const double fall = decayPerSample * std::max(0.0, dt - toPeak);
                v = std::max(s, 1.0 - fall);
            }
            break;
        }
        case Stage::Decay:
            v = std::max(s, clamp01(level) - decayPerSample * dt);
            break;
        case Stage::Sustain:
            v = s;
            break;
        case Stage::Release:
            v = clamp01(level) - releasePerSample * dt;
            break;
        }
        return clamp01(v);
    }

  private:
    // [0, 1], NaN to 0.
    static double clamp01(double x) noexcept { return x > 0.0 ? (x < 1.0 ? x : 1.0) : 0.0; }
};
} // namespace gg

#endif // GG_GRAIN_AMPENVELOPE_H
