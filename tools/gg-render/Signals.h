// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_RENDER_SIGNALS_H
#define GG_RENDER_SIGNALS_H

#include <cstdint>

#include "RenderSpec.h"
#include "gg/metrics/AudioData.h"

namespace gg::render
{
/// round(seconds · sampleRate) for finite arguments >= 0, rounding halves up. Plain IEEE
/// arithmetic (no libm), so the frame count, which reaches the grain-event log through the
/// source length, is the same on every platform.
int64_t framesFor(double seconds, double sampleRate) noexcept;

/// The source a render reads. Synthesized sources are mono, at rate r = source.sampleRate if it
/// is set and `sampleRate` (the render's rate) otherwise, and framesFor(seconds, r) frames long;
/// sample n is at time t = n / r:
/// - sine: amplitude · sin(2π · frequencyHz · t);
/// - sweep: exponential from startHz to endHz over `seconds`, i.e. instantaneous frequency
///   startHz · (endHz / startHz)^(t / seconds), starting at phase 0;
/// - noise: amplitude · gg::rng::bipolar(hash{seed, 0, 0, Field::SignalNoise, n}), white and
///   uniform in [-amplitude, amplitude).
///
/// Sine and sweep use libm, so their last bits may differ between platforms; they are audio,
/// which goldens compare by metrics with tolerances. Noise is bit-identical everywhere.
///
/// A file source is loaded as is, with its own channels and sample rate (`sampleRate` and
/// `seed` are unused); throws std::runtime_error if it can't be read.
gg::metrics::AudioData makeSignal(const SourceSpec &source, double sampleRate, uint64_t seed);

/// The length of an FX input's fades, in frames: kInputFadeSeconds × sampleRate rounded
/// (framesFor), at least 1 (ruling M1-v).
inline constexpr double kInputFadeSeconds = 0.01;
int64_t inputFadeFrames(double sampleRate) noexcept;

/// The raised cosine 0.5 − 0.5·cos(πx) = sin²(πx/2) for x in [0, 1]: 0 at 0, 1 at 1, with zero
/// slope at both ends. sin comes from its Taylor series through θ^19, in plain IEEE arithmetic
/// (no libm), so the value is the same on every platform. On [0, π/2] the series' error is below
/// (π/2)^21 / 21! < 3e-16, so the result is within 1e-15 of the exact raised cosine.
double raisedCosine(double x) noexcept;

/// An FX render's input: `channels` channels of makeSignal(input, sampleRate, seed), at the
/// render's rate, faded in and out (below). Channel 0 is that signal. A second channel differs
/// from it the way a stereo source would, except for the sweep:
/// - noise: the noise of seed + 1, independent of channel 0;
/// - sine: frequencyHz × 1.5, a fifth up, at the same amplitude;
/// - sweep: the same sweep.
///
/// The fades (ruling M1-v): with L = inputFadeFrames(sampleRate) and N frames, sample n of every
/// channel is multiplied by raisedCosine(n / L) for n < L and by raisedCosine((N − 1 − n) / L)
/// for N − 1 − n < L (by both if an input is shorter than 2L), computed in double and rounded to
/// float. So the first and last samples are exactly 0 and every sample in between is unchanged:
/// the input starts and stops without the click a hard edge would put in the dry signal and in
/// the grains that replay it. Sample-mode sources (makeSignal) have no fades.
///
/// Throws std::invalid_argument for what parseRenderSpec never produces: channels other than 1
/// or 2, a file input, or an input with a sampleRate of its own.
gg::metrics::AudioData makeInput(const SourceSpec &input, uint32_t channels, double sampleRate,
                                 uint64_t seed);
} // namespace gg::render

#endif // GG_RENDER_SIGNALS_H
