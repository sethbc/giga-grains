// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_ENVELOPE_H
#define GG_GRAIN_ENVELOPE_H

#include "gg/core/FpContract.h"

/// The Texture envelope (spec §4): a grain's window is hann(warp(φ)), φ = e × invDuration its
/// phase in [0, 1], where warp squeezes the Hann's rising half into an attack, holds its peak
/// over a sustain and squeezes its falling half into a release. Texture τ, latched at the grain's
/// onset, picks the shape from one fixed family, with d the grain's duration in samples and
/// aMin = min(0.5, max(0.05, 16 / d)) the shortest attack and release (each at least 16 samples
/// and 5% of the grain, never more than half of it):
///
/// - τ <= 0.5: attack a = 0.5 + 0.4τ, release r = 0.5 − 0.4τ: no sustain, the peak moving from
///   the middle (τ = 0) to 70% (τ = 0.5);
/// - τ > 0.5: with s = 2τ − 1, a = (1 − s) × 0.7 + s × aMin and r = (1 − s) × 0.3 + s × aMin,
///   from the 70% peak to the gated anchor (τ = 1), with a sustain 1 − a − r between.
///
/// The second branch is the plan's a = 0.7 + (aMin − 0.7)s written as a lerp, which is the same
/// curve but lands exactly on its anchors: a = 0.7 at s = 0 and a = r = aMin at s = 1 (the
/// plan's form gives 0.050000000000000044 for 0.05).
///
/// At τ = 0, a = r = 0.5 exactly, both scales are exactly 1 and warp(φ) == φ bit for bit, so
/// texture 0 renders as the plain Hann did. Everything here is plain IEEE arithmetic, so the
/// envelope is deterministic on every platform (no libm).
namespace gg
{
/// One grain's envelope, latched at spawn (GrainPool): the attack ends at phase attackEnd = a
/// and the release starts at releaseStart = 1 − r; the scales are 0.5 / a and 0.5 / r.
struct EnvelopeShape
{
    double attackEnd;
    double releaseStart;
    double attackScale;
    double releaseScale;
};

/// The family above. texture in [0, 1] (NaN or out of range reads as 0), durationSamples >= 16.
constexpr EnvelopeShape envelopeShape(double texture, double durationSamples) noexcept
{
    GG_FP_CONTRACT_OFF
    const double tau = (texture >= 0.0 && texture <= 1.0) ? texture : 0.0;
    const double floorFrac = 16.0 / durationSamples;
    const double atLeast = floorFrac > 0.05 ? floorFrac : 0.05;
    // A NaN duration reads as a long grain's 0.05, so the shape stays finite.
    const double aMin = atLeast < 0.5 ? atLeast : 0.5;
    double a = 0.5;
    double r = 0.5;
    if (tau <= 0.5)
    {
        a = 0.5 + 0.4 * tau;
        r = 0.5 - 0.4 * tau;
    }
    else
    {
        const double s = 2.0 * tau - 1.0;
        a = (1.0 - s) * 0.7 + s * aMin;
        r = (1.0 - s) * 0.3 + s * aMin;
    }
    return {a, 1.0 - r, 0.5 / a, 0.5 / r};
}

/// Whether e is texture 0's shape, a = r = 0.5 with both scales 1, for which warpPhase(phase, e)
/// == phase bit for bit for every phase in [0, 1]: phase × 1 is exact, and for phase in
/// (0.5, 1], 1 − phase is exact (Sterbenz) and so is 1 − (1 − phase), whose exact value phase is
/// representable. The SIMD interior skips the warp for such a grain.
constexpr bool isIdentity(const EnvelopeShape &e) noexcept
{
    GG_FP_CONTRACT_OFF
    return e.attackEnd == 0.5 && e.releaseStart == 0.5 && e.attackScale == 1.0 &&
           e.releaseScale == 1.0;
}

/// warp(phase) for phase in [0, 1]:
/// phase < a ? phase × (0.5 / a) : phase > 1 − r ? 1 − (1 − phase) × (0.5 / r) : 0.5.
/// The identity when attackEnd == releaseStart == 0.5 (1 − (1 − phase) is exact for phase in
/// [0.5, 1], by Sterbenz's lemma). It maps [0, 1] onto [0, 1], keeps a phase below 0 below 0 and
/// one above 1 above 1 (both scales are at least 1), and a NaN a NaN (the first test is written
/// !(phase >= a) for that), so
/// HannTable::at gives 0 outside the window as it did without the warp.
constexpr double warpPhase(double phase, const EnvelopeShape &e) noexcept
{
    GG_FP_CONTRACT_OFF
    // Both segments are computed and then selected, so the compiler can emit selects rather than
    // branches (and vectorize the SIMD interior's lane loop); the value is the branchy form's.
    const double attack = phase * e.attackScale;
    const double release = 1.0 - (1.0 - phase) * e.releaseScale;
    const double tail = phase > e.releaseStart ? release : 0.5;
    return !(phase >= e.attackEnd) ? attack : tail;
}
} // namespace gg

#endif // GG_GRAIN_ENVELOPE_H
