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
/// and is byte-identical on every platform. The golden event logs are compared exactly per line
/// (a CR before a newline, and the final newline, are ignored), which makes them the
/// cross-platform determinism gate (INTENT §14, §19).
std::string formatEventLine(const GrainSpec &g);

/// Renders durationSeconds of stream activity plus a release tail, until every grain has
/// finished, so the output never stops in the middle of a grain:
/// - the source is makeSignal(spec.source, spec.sampleRate, spec.seed);
/// - a gg::Engine (sample rate, seed and maxBlockSize = blockSize from the spec) with the
///   spec's stream parameters renders D = framesFor(durationSeconds, sampleRate) stereo frames;
/// - then the stream is turned off (grainRateHz 0, the other parameters unchanged), so no grain
///   spawns at or after frame D, and rendering continues until the last grain has finished. The
///   output ends at frame max(D, E + 1), where E is the largest grainEndSample of the spawned
///   grains: its last frame is always silent.
///
/// Every part is rendered in process() calls of blockSize frames (a part's last call may be
/// shorter). The length depends only on the grains, never on blockSize, so the output stays
/// bit-identical for any block size. The tail is at most one grain plus two frames long; a
/// defensive bound of the longest possible grain plus 2 + blockSize frames throws
/// std::runtime_error if it is ever exceeded.
///
/// If eventLines isn't null it is cleared and receives formatEventLine of every grain that
/// spawned, in spawn order (all of them during the first D frames). Throws what makeSignal
/// throws.
gg::metrics::AudioData render(const RenderSpec &spec, std::vector<std::string> *eventLines);
} // namespace gg::render

#endif // GG_RENDER_RENDER_H
