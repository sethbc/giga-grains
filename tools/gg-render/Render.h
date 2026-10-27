// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_RENDER_RENDER_H
#define GG_RENDER_RENDER_H

#include <string>
#include <vector>

#include "RenderSpec.h"
#include "gg/grain/GrainPool.h"
#include "gg/metrics/AudioData.h"

namespace gg::render
{
/// One line of the grain-event log, without the newline:
/// `{"i":<index>,"onset":<onsetSample>,"frac":<d>,"pos":<d>,"rate":<d>,"dur":<d>}`.
/// Each <d> is fmt's shortest round-trip form of the double, so the line reads back exactly
/// and is byte-identical on every platform. The golden event logs are compared byte for byte,
/// which makes them the cross-platform determinism gate (INTENT §14, §19).
std::string formatEventLine(const GrainSpec &g);

/// Renders the spec offline:
/// - the source is makeSignal(spec.source, spec.sampleRate, spec.seed);
/// - a gg::Engine (sample rate, seed and maxBlockSize = blockSize from the spec) with the
///   spec's stream parameters renders framesFor(durationSeconds, sampleRate) stereo frames, in
///   process() calls of blockSize frames (the last may be shorter).
///
/// If eventLines isn't null it is cleared and receives formatEventLine of every grain that
/// spawned, in spawn order. Throws what makeSignal throws.
gg::metrics::AudioData render(const RenderSpec &spec, std::vector<std::string> *eventLines);
} // namespace gg::render

#endif // GG_RENDER_RENDER_H
