// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_RENDER_RENDER_H
#define GG_RENDER_RENDER_H

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "RenderSpec.h"
#include "gg/grain/GrainPool.h"
#include "gg/metrics/AudioData.h"

namespace gg::render
{
/// One line of the grain-event log for a grain that spawned, without the newline:
/// `{"i":<index>,"layer":<layer>,"onset":<onsetSample>,"frac":<d>,"pos":<d>,"rate":<d>,"dur":<d>,`
/// `"tex":<d>,"tier":<t>,"lvl":<l>,"gain":<f>,"pan":<f>}`, "layer" being the layer that spawned it
/// (0 to 3, A to D; M2 sub-project 1's spec §6.7), "tex" the grain's Texture (spec §4), "tier" the
/// tier it latched (0 Eco, 1 Normal, 2 HQ) and "lvl" its latched mip level (M2 engine spec §6.7).
/// Each <d> is fmt's shortest round-trip form of the double and each <f> that of the float, so the
/// line reads back exactly and is byte-identical on every platform. The golden event logs are
/// compared exactly per line (a CR before a newline, and the final newline, are ignored), which
/// makes them the cross-platform determinism gate (INTENT §14, §19): it sees every decision of the
/// overload ladder, the tier, the level and, through formatStealLine, the steals.
std::string formatEventLine(uint32_t layer, const GrainSpec &g);

/// One line of the grain-event log for a grain the overload ladder stole, without the newline:
/// `{"i":<index>,"layer":<layer>,"steal":<onset>}`, "i" the stolen grain's index, "layer" its
/// layer and "steal" the onset sample of the grain it made room for, whose line follows it at
/// once (M2 engine spec §6.7).
std::string formatStealLine(uint32_t layer, uint64_t index, int64_t onset);

/// gg-render's command-line options that reach the render (M2 sub-project 2's spec §6.10). All
/// paths are UTF-8; empty means the option wasn't given.
struct RenderOptions
{
    /// --content-dir: a content root, one of the places a restored state's sample is found by
    /// its content-relative path.
    std::string contentDir;
    /// --captures-dir: where a saved ring or capture goes when the embedded audio would pass
    /// kEmbedCapBytes (as <sha256>.wav), and where a filed ring is found on a restore.
    std::string capturesDir;
    /// --save-state: write the session state (gg::core::encodeState) here at the render's end.
    std::string saveStatePath;
    /// The directory the spec's relative paths were resolved against (the spec's own, or
    /// --media-dir): the other place a restored state's samples are found by content path, and
    /// the root a saved sample's content path is made relative to.
    std::string mediaDir;
};

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
/// **Sessions, ring parameters and operations** (instrument and FX mode; M2 sub-project 2's spec
/// §6.10). Before the first process() call, in this order:
/// 1. the spec's "state", if any, is decoded (gg::core::decodeState) and its values and mod routes
///    set; its samples are then found (gg::media::findSample, through options.contentDir and
///    options.mediaDir alone: never searched by name), loaded and handed to their layers, an
///    embedded one decoded from its payload;
/// 2. the spec's own sample and layer samples, the params, the mod routes and the transport, as
///    before ("params" override the state);
/// 3. each layer's ring: a saved ring is restored (gg::media::restoreRing into a ring of the
///    layer's Ring Length; Processor::setRingNow, frozen when the layer's Freeze is on), and a
///    layer whose Ring Length isn't the default gets a ring of that length (setRingNow), so every
///    ring is in place before the first call, as a block-size-invariant render needs.
/// The memory cap (the spec's memoryCapMiB, else the state's, else the default) is checked as each
/// sample and ring is added, before it is allocated: one that doesn't fit is BadInput with
/// gg::media::MemoryBudget's message. The spec's seed is the render's, not the state's.
///
/// The call loop is cut at each capture's frame; a capture reads the layer's ring directly (the
/// thread is the audio thread between calls), and a splice or clearSplices goes to process() as a
/// CommandEvent. With options.saveStatePath the session is written at the end (gg::core::
/// encodeState): the values and routes in effect, the file samples that were loaded (by content
/// path, relative to options.contentDir or options.mediaDir, when they lie in one; built-in
/// signals are not saved), the captures made into layers, and the rings of the layers whose Freeze
/// or Loop base is on (D1), each as the whole ring's newest frames with its marks. Audio is
/// embedded layer by layer (a layer's ring before its capture) while the total stays within
/// gg::core::kEmbedCapBytes, and the rest filed in options.capturesDir as <sha256>.wav, which is
/// BadInput when it isn't given.
///
/// Each call's grain events are collected in storage sized from CloudScheduler's minimum interval
/// between onsets, two per spawn (a steal at most before each). If eventLines isn't null it is
/// cleared and receives formatEventLine of every grain that spawned, in spawn order (in sample
/// mode, all of them during the first D frames), and formatStealLine of every grain the overload
/// ladder stole, right before the line of the grain it made room for: the engine's order in sample
/// mode, the Processor's (piece, layer, report order) in the others. gg-render sets no quota in
/// sample mode, so there only a full pool steals; in the others Budget sets the quotas.
///
/// Throws:
/// - BadInput for a file (source, sample, input or state) that can't be read or decoded, or whose
///   path isn't valid UTF-8, a state that isn't one, a state's sample or ring that can't be
///   found, a ring or sample past the memory cap, a capture of an empty ring, audio that has to
///   be filed with no captures folder, and --save-state of a sample-mode spec; gg-render exits
///   with status 2;
/// - std::logic_error if a call reports more grain events than that storage holds, or, in
///   sample mode, grains outlast the release tail or the tail would pass its defensive bound;
/// - std::invalid_argument for an unknown parameter name, and for what makeSignal, makeSample and
///   makeInput reject but parseRenderSpec never produces;
/// - std::bad_alloc if memory runs out.
/// gg-render exits with status 3, an internal error, for all but BadInput.
gg::metrics::AudioData render(const RenderSpec &spec, std::vector<std::string> *eventLines,
                              const RenderOptions &options = {});

/// gg-render's exit statuses (CLAUDE.md, "Golden renders").
inline constexpr int kExitPass = 0;
inline constexpr int kExitCompareFailed = 1;
inline constexpr int kExitBadInput = 2;
inline constexpr int kExitInternalError = 3;

/// gg-render's main, around everything it does: returns run()'s status. When run() throws
/// BadInput, prints "gg-render: <what>" to stderr and returns kExitBadInput; when it throws
/// anything else, from the render, the metrics or the writers (std::bad_alloc on an extreme
/// spec, say, or something not even a std::exception), prints "gg-render: internal error" (and
/// the what(), if any) and returns kExitInternalError ([182], [184]).
template <typename Run> int guarded(Run &&run) noexcept
{
    try
    {
        return run();
    }
    catch (const BadInput &e)
    {
        fmt::print(stderr, "gg-render: {}\n", e.what());
        return kExitBadInput;
    }
    catch (const std::exception &e)
    {
        fmt::print(stderr, "gg-render: internal error: {}\n", e.what());
    }
    catch (...)
    {
        fmt::print(stderr, "gg-render: internal error\n");
    }
    return kExitInternalError;
}
} // namespace gg::render

#endif // GG_RENDER_RENDER_H
