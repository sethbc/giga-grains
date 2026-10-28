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
/// throws std::runtime_error if it is ever exceeded.
///
/// **FX mode** runs the gg::core::Processor that Giga Grains FX runs, fed as a host feeds it, for
/// exactly D = framesFor(durationSeconds, sampleRate) frames, with no release tail:
/// - a gg::core::Processor in Mode::Fx (sample rate and seed from the spec, maxBlockSize =
///   blockSize) gets each of spec.fx.params with setBase(slot, static_cast<float>(value));
/// - its input is makeInput(spec.fx.input, spec.fx.channels, sampleRate, seed), one or two input
///   channels, silent after its own length; the output is always two channels;
/// - each automation point is a ParamEvent (a base value, not a modulation) at absolute frame
///   round(timeSeconds × sampleRate), handed to the process() call that contains that frame at
///   its offset in the call; the Processor applies it at the next 32-sample grid boundary.
///
/// The spec must be one parseRenderSpec accepts; in FX mode an unknown parameter name throws
/// std::invalid_argument.
///
/// Each call's grains are collected in storage sized from CloudScheduler's minimum interval
/// between onsets, and std::logic_error is thrown if a call ever spawns more. If eventLines isn't
/// null it is cleared and receives formatEventLine of every grain that spawned, in spawn order
/// (in sample mode, all of them during the first D frames). Throws what makeSignal throws.
gg::metrics::AudioData render(const RenderSpec &spec, std::vector<std::string> *eventLines);
} // namespace gg::render

#endif // GG_RENDER_RENDER_H
