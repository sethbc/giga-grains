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
/// round(seconds · sampleRate) for positive, finite arguments, rounding halves up. Plain IEEE
/// arithmetic (no libm), so the frame count, which reaches the grain-event log through the
/// source length, is the same on every platform.
int64_t framesFor(double seconds, double sampleRate) noexcept;

/// The source a render reads. Synthesized sources are mono, at sampleRate, and
/// framesFor(seconds, sampleRate) frames long; sample n is at time t = n / sampleRate:
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
} // namespace gg::render

#endif // GG_RENDER_SIGNALS_H
