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
/// `{"i":<index>,"onset":<onsetSample>,"frac":<d>,"pos":<d>,"rate":<d>,"dur":<d>,"gain":<f>,`
/// `"pan":<f>}`. Each <d> is fmt's shortest round-trip form of the double and each <f> that of
/// the float, so the line reads back exactly and is byte-identical on every platform. The golden
/// event logs are compared exactly per line (a CR before a newline, and the final newline, are
/// ignored), which makes them the cross-platform determinism gate (INTENT §14, §19).
std::string formatEventLine(const GrainSpec &g);

/// Renders the spec in its mode, in process() calls of blockSize frames (a part's last call may
/// be shorter). The output is stereo, and bit-identical for any blockSize.
///
/// **Sample mode** renders durationSeconds of stream activity plus a release tail, until every
/// grain has finished, so the output never stops in the middle of a grain:
/// - the source is makeSignal(spec.source, spec.sampleRate, spec.seed);
/// - a gg::Engine (sample rate, seed and maxBlockSize = blockSize from the spec) with the
///   spec's stream parameters renders D = framesFor(durationSeconds, sampleRate) stereo frames;
/// - then the stream is turned off (density 0, ruling M1-a; the other parameters unchanged), so
///   no grain spawns at or after frame D, and rendering continues until the last grain has
///   finished. The output ends at frame max(D, E + 1), where E is the largest grainEndSample of
///   the spawned grains: its last frame is always silent.
///
/// The length depends only on the grains, never on blockSize. The tail is at most one grain plus
/// two frames long; a defensive bound of the longest possible grain plus 2 + blockSize frames
/// throws std::logic_error if it is ever exceeded.
///
/// **Instrument and FX mode** run the gg::core::Processor that Giga Grains (Mode::Instrument)
/// or Giga Grains FX (Mode::Fx) runs, fed as a host feeds it, for exactly
/// D = framesFor(durationSeconds, sampleRate) frames, with no release tail:
/// - the Processor has the sample rate and seed from the spec and maxBlockSize = blockSize, and
///   gets each of the mode's params with setBase(slot, static_cast<float>(value));
/// - the instrument's source is makeSample(spec.instrument.sample, sampleRate, seed), set with
///   setSourceNow before the render; it has no input;
/// - the FX's input is makeInput(spec.fx.input, spec.fx.channels, sampleRate, seed), one or two
///   input channels (a file's own count for a file input), silent after its own length;
/// - the output is always two channels;
/// - each automation point is a ParamEvent (a base value, not a modulation) at absolute frame
///   round(timeSeconds × sampleRate), and each note an On and an Off NoteEvent at the frames
///   NoteSpec gives; each is handed to the process() call that contains its frame, at its offset
///   in the call, and the Processor applies it at the next 32-sample grid boundary.
///
/// The spec must be one parseRenderSpec accepts, with its file paths resolved (resolvePaths).
///
/// Each call's grains are collected in storage sized from CloudScheduler's minimum interval
/// between onsets. If eventLines isn't null it is cleared and receives formatEventLine of every
/// grain that spawned, in spawn order (in sample mode, all of them during the first D frames).
///
/// Throws:
/// - BadInput for a file (source, sample or input) that can't be read or decoded, or whose path
///   isn't valid UTF-8; gg-render exits with status 2;
/// - std::logic_error if a call spawns more grains than that storage holds, or, in sample mode,
///   grains outlast the release tail or the tail would pass its defensive bound;
/// - std::invalid_argument for an unknown parameter name, and for what makeSignal, makeSample and
///   makeInput reject but parseRenderSpec never produces;
/// - std::bad_alloc if memory runs out.
/// gg-render exits with status 3, an internal error, for all but BadInput.
gg::metrics::AudioData render(const RenderSpec &spec, std::vector<std::string> *eventLines);
} // namespace gg::render

#endif // GG_RENDER_RENDER_H
