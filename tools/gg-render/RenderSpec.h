// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_RENDER_RENDERSPEC_H
#define GG_RENDER_RENDERSPEC_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gg/core/ParamIds.h"
#include "gg/grain/Tier.h"

/// The render spec: a JSON description of one offline render, read by gg-render. It has three
/// modes. A sample-mode render ("mode": "sample", the default) drives the engine's Cloud
/// stream over a source directly:
///
/// ```json
/// {"sampleRate":48000,"durationSeconds":3.0,"blockSize":128,"seed":1,
///  "source":{"kind":"sine|sweep|noise|file","frequencyHz":220,"startHz":50,"endHz":8000,
///            "seconds":4.0,"amplitude":0.5,"sampleRate":48000,"path":"relative/to/spec.wav"},
///  "stream":{"grainRateHz":8,"grainDurationMs":60,"position":0.5,"positionJitter":0.2,
///            "pitchSemitones":0,"gain":0.8,"distribution":0,"quality":"eco|normal|hq"}}
/// ```
///
/// An instrument render ("mode": "instrument") runs Giga Grains' gg::core::Processor over a
/// sample, played by notes, and an FX render ("mode": "fx") runs Giga Grains FX's over an input.
/// Both take parameters and automation in their CLAP value domains, and notes (see
/// InstrumentSpec, FxSpec and NoteSpec):
///
/// ```json
/// {"mode":"instrument","sampleRate":48000,"durationSeconds":3.0,"blockSize":128,"seed":1,
///  "sample":{"kind":"sine|sweep|noise|file","frequencyHz":220,"startHz":50,"endHz":8000,
///            "seconds":4.0,"amplitude":0.5,"path":"relative/to/spec.wav"},
///  "params":{"Position":0.5},
///  "automation":[{"time":1.0,"param":"Density","value":6.0}],
///  "notes":[{"time":0.25,"key":60,"duration":2.0,"channel":0}]}
///
/// {"mode":"fx","sampleRate":48000,"durationSeconds":4.0,"blockSize":128,"seed":1,
///  "input":{"kind":"sine|sweep|noise|file","frequencyHz":220,"startHz":50,"endHz":8000,
///           "seconds":3.0,"amplitude":0.5,"path":"relative/to/spec.wav"},
///  "channels":1,"params":{"Size":-2.7},
///  "automation":[{"time":1.0,"param":"Density","value":6.0}],
///  "notes":[{"time":1.0,"key":67,"duration":0.5}]}
/// ```
///
/// Both also take M2 sub-project 1's keys (its spec §6.11), each optional: samples for any layer,
/// mod routes, MIDI controls, a steady transport, and a velocity per note (see InstrumentSpec,
/// FxSpec, RouteSpec, ControlSpec, TransportSpec and NoteSpec):
///
/// ```json
/// {"mode":"fx", ...,
///  "samples":{"B":{"kind":"file","path":"loop-48k.wav"},
///             "C":{"kind":"sine","frequencyHz":330,"seconds":2.0,"amplitude":0.5}},
///  "params":{"Layer B On":1,"B Source":0,"Layer C On":1,"C Source":0},
///  "mod":[{"slot":13,"source":"LFO 1","sourceLayer":"same","target":"Pitch","targetLayer":"B",
///          "depth":12,"curve":"Bipolar"},
///         {"slot":14,"source":"Mod Wheel","target":"Mix","depth":0.5},
///         {"slot":1,"source":"None"}],
///  "controls":[{"time":0.5,"kind":"modWheel","value":1.0,"channel":0}],
///  "transport":{"tempoBpm":120,"playing":true,"startBeat":0},
///  "notes":[{"time":1.0,"key":67,"duration":0.5,"velocity":100}]}
/// ```
///
/// - "mode" is optional. In every mode sampleRate, durationSeconds, blockSize and seed are
///   required, and durationSeconds must round to at least one frame at sampleRate.
/// - Sample mode requires "source" and "stream", and every "stream" key but "distribution",
///   which defaults to 0 (periodic onsets, as in M0), and "quality", which defaults to "normal"
///   (see StreamSpec::quality). "source" needs "kind" plus the keys its kind uses (see
///   SourceSpec), and no key only another kind uses; "source.sampleRate" is optional for a
///   built-in kind and an error for a file.
/// - Instrument mode requires "sample", read as a source but without "sampleRate"; "params",
///   "automation" and "notes" are optional. It has no input, so no "channels".
/// - FX mode requires "input", read as a source but without "sampleRate", and "channels",
///   except with a file input, which has the file's own channels and so takes no "channels";
///   "params", "automation" and "notes" are optional.
/// - Instrument and FX mode both take "samples", "mod", "controls" and "transport", optional;
///   sample mode takes none of them.
/// - Instrument and FX mode also take M2 sub-project 2's keys (its spec §6.10), each optional: a
///   saved session to restore, timed ring operations and the memory cap (see OperationSpec and
///   the members of InstrumentSpec and FxSpec):
///
/// ```json
/// {"mode":"fx", ...,
///  "state":"frozen-ring.ggst",
///  "params":{"A Ring Length":4.321928094887363,"A Loop":1,"A Loop Length":-1.0,
///            "A Overdub":0.5,"A Splice":1},
///  "operations":[{"time":2.0,"layer":"A","op":"splice","behind":1.5},
///                {"time":3.0,"layer":"A","op":"clearSplices"},
///                {"time":4.0,"layer":"A","op":"capture","splice":0,"into":"B","file":"cap.wav"}],
///  "memoryCapMiB":256}
/// ```
///
///   "state" is a GGST file (a session state, relative paths as a file source's), restored
///   before the render as a plugin's stateLoad would (its values, mod routes, samples, rings and
///   memory cap); "params" and "mod" then override it. The samples it names are found only
///   through --content-dir and the spec's directory (or --media-dir), never searched by name. In
///   instrument mode "sample" may be left out beside "state", and then layer A's sample is the
///   state's. The spec's own seed is the render's, not the state's. "memoryCapMiB" (a whole
///   number from 64 to 16384; the state's, or 1024, without it) is the per-instance memory cap;
///   a ring or sample that would go past it is bad input (exit 2). A layer's Ring Length is set
///   before the render ("A Ring Length" in "params"; it can't be automated). "operations" are in
///   time order; see OperationSpec. Sample mode takes none of these. gg-render's --save-state
///   writes the session at the end: a built-in signal sample ("sine", "sweep", "noise") is not in
///   it (there is no file to name), a file sample, a capture made into a layer and the rings of
///   layers whose Freeze or Loop is on (their loop window, with the marks inside it) are.
/// - Parameters (in "params", "automation" and a route's "target") are named as the product
///   displays them, so "B Size" is layer B's; a bare M1 name ("Size"), or "Quality", is layer
///   A's.
/// - Any other key, including another mode's, is an error, so a misspelt key never silently
///   falls back to a default.
/// - A file's "path" is UTF-8. gg-render resolves a relative one against the spec's directory,
///   or the --media-dir directory when it is given (resolvePaths), before rendering.
/// - A sample-mode render covers durationSeconds of stream activity plus a release tail, so it
///   never stops in the middle of a grain; an instrument or FX render is exactly durationSeconds
///   long (see render() in Render.h).
namespace gg::render
{
inline constexpr double kMaxSampleRate = 768000.0;
inline constexpr double kMaxSeconds = 3600.0;
inline constexpr uint32_t kMaxBlockSize = 65536;

/// Thrown while rendering for bad input the spec's parser can't see: a file that can't be read
/// or decoded. gg-render exits with status 2 for it, and with 3 (an internal error) for any other
/// exception.
struct BadInput : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

enum class SourceKind
{
    Sine,
    Sweep,
    Noise,
    File,
};

/// A test signal: what the grains read in sample mode, the instrument's sample, or the FX's
/// input. Every number is finite.
struct SourceSpec
{
    SourceKind kind{SourceKind::Sine};
    double frequencyHz{220.0}; ///< sine; > 0.
    double startHz{50.0};      ///< sweep; > 0.
    double endHz{8000.0};      ///< sweep; > 0.
    double seconds{4.0};       ///< sine, sweep, noise; in (0, kMaxSeconds].
    double amplitude{0.5};     ///< sine, sweep, noise: the peak level; >= 0.
    /// sine, sweep, noise in sample mode: the rate the source is synthesized at, in
    /// (0, kMaxSampleRate]. Optional; without it the source is synthesized at the render's
    /// sampleRate. A rate other than the render's exercises the engine's source-rate conversion.
    /// An instrument's sample and an FX input never have one.
    std::optional<double> sampleRate{};
    /// file: a WAV, FLAC or MP3 file (an AIFF too, for a sample or an input), non-empty, UTF-8.
    /// In a spec file a relative path is relative to the spec's directory, or to --media-dir;
    /// gg-render resolves it before rendering (resolvePaths), and makeSignal, makeSample and
    /// makeInput open it as given.
    std::string path{};

    friend bool operator==(const SourceSpec &, const SourceSpec &) = default;
};

/// The engine's gg::CloudParams as the spec spells them, with M0's names: grainRateHz is the
/// density, positionJitter the spray, and the duration is in milliseconds. Stereo isn't in the
/// spec, so every grain is centred. Every value is a finite number; the engine clamps them to
/// the ranges documented on gg::CloudParams. grainDurationMs is at most kMaxSeconds · 1000,
/// which also bounds the release tail.
struct StreamSpec
{
    double grainRateHz{20.0};
    double grainDurationMs{100.0};
    double position{0.5};
    double positionJitter{0.0};
    double pitchSemitones{0.0};
    double gain{0.5};
    double distribution{0.0}; ///< Optional in the spec.
    /// The engine's CloudParams::quality (ruling 2): "eco", "normal" or "hq" (M2 sub-project 3's
    /// spec §6.1). Optional in the spec. The grain-event lines don't print it, so an Eco render's
    /// event log is the Normal one's.
    Tier quality{Tier::Normal};

    friend bool operator==(const StreamSpec &, const StreamSpec &) = default;
};

enum class RenderMode
{
    Sample,     ///< The engine's Cloud stream over a source, plus a release tail.
    Instrument, ///< Giga Grains' Processor over a sample, exactly durationSeconds long.
    Fx,         ///< Giga Grains FX's Processor over an input, exactly durationSeconds long.
};

/// One automation point: the parameter named `param` (see paramSlotNamed) is set to `value` at
/// frame round(timeSeconds × sampleRate), as a host's parameter value event would set it.
///
/// The Processor applies an event at the next 32-sample grid boundary (gg::core::Processor), and
/// a boundary at or past the render's end never comes. So a point on a frame after the last
/// boundary before the end is accepted but never applies: up to 31 frames, 31 when the render's
/// length is a multiple of 32.
struct AutomationPoint
{
    double timeSeconds{0.0}; ///< >= 0, rounding to a frame before the render's end.
    std::string param;
    double value{0.0}; ///< In the parameter's CLAP value domain and range.

    friend bool operator==(const AutomationPoint &, const AutomationPoint &) = default;
};

/// One note, as a host's note-on and note-off events would play it: an On for (channel, key) at
/// frame round(timeSeconds × sampleRate) and an Off at round((timeSeconds + durationSeconds) ×
/// sampleRate), handed to the Processor merged with the automation in each process() call.
/// - Overlapping notes are chords. On one frame the Offs go before the Ons, so a key that ends
///   where it starts again stays held.
/// - The Processor's held set is one bit per (channel, key), so overlapping notes on the same
///   channel and key release together, at the first one's Off.
/// - A note's Off must fall on a later frame than its On: a note whose Off would round to its
///   On's frame is rejected (ruling R17).
/// - An Off at or after the render's end is never sent: the note is still held when it ends.
/// - Like an automation point, a note in the render's last samples, after its last grid
///   boundary, never applies.
/// - The grains ignore velocity (D6); an On's velocity is the mod matrix's Velocity source (M2
///   sub-project 1's spec §6.8), velocity / 127.
struct NoteSpec
{
    double timeSeconds{0.0};     ///< >= 0, rounding to a frame before the render's end.
    uint8_t key{60};             ///< 0..127; 60 plays at the sample's own pitch.
    double durationSeconds{1.0}; ///< > 0, at most kMaxSeconds; the Off rounds past the On (R17).
    uint8_t channel{0};          ///< 0..15; optional in the spec, 0 by default.
    uint8_t velocity{100};       ///< 0..127; optional in the spec, 100 by default.

    friend bool operator==(const NoteSpec &, const NoteSpec &) = default;
};

/// One route of the mod matrix (gg::core::ModRoute; M2 sub-project 1's spec §6.8), by name, set in
/// its slot before the render on top of the product's factory routes (gg::core::factoryRoutes),
/// which fill slots 1 to 12. A route naming source "None" clears its slot, and takes no key but
/// "slot" and "source". Otherwise "source", "target" and "depth" are required, and the others
/// default as below. No slot may be named twice in a spec.
struct RouteSpec
{
    uint8_t slot{1}; ///< 1..48, as people number them (the Processor's index + 1).
    /// The source's stable name (gg::core::modSourceName): "LFO 1", "Mod Wheel", "Transport Beat",
    /// "None" and so on.
    std::string source{"None"};
    /// A per-layer source's layer (the LFOs, envelopes, randoms and Step): "A" to "D", or "same",
    /// the target's own layer (the default). A global source accepts and ignores it.
    std::string sourceLayer{"same"};
    /// A parameter name of the spec's product: a global ("Mix", "Morph X"), or a layer kind by its
    /// layer-A name ("A Pitch" or the bare M1 "Pitch"; "Level" and "Layer A On" for the per-layer
    /// globals), with targetLayer picking the layers. Layers B to D's names ("B Pitch", "Level B")
    /// are refused: targetLayer reaches them. It must be a modulation target
    /// (gg::core::isValidTarget: not a macro, nor a modulator's own control). "" for "None".
    std::string target{};
    /// A layer target's layers: "A" to "D", or "all" (the default). Not allowed with a global
    /// target.
    std::string targetLayer{"all"};
    /// In the target's CLAP value domain per unit of the curve's output; finite, within float's
    /// range (static_cast<float> when it reaches the Processor).
    double depth{0.0};
    /// The curve's stable name (gg::core::modCurveName): "Bipolar" (the default), "Unipolar",
    /// "BipolarPositive", "UnipolarSquared", "RampThreeQuarters" or "TopQuarter".
    std::string curve{"Bipolar"};

    friend bool operator==(const RouteSpec &, const RouteSpec &) = default;
};

/// One MIDI control, as a host's event would send it (gg::core::ControlEvent): at frame
/// round(timeSeconds × sampleRate), merged with the automation and the notes in each process()
/// call. The times follow the automation's rules (a control after the render's last grid
/// boundary never applies), and in time order (equal times allowed; on one frame, list order).
struct ControlSpec
{
    double timeSeconds{0.0}; ///< >= 0, rounding to a frame before the render's end.
    /// "modWheel" (CC 1), "aftertouch" (channel pressure) or "pitchBend": the mod matrix's Mod
    /// Wheel, Aftertouch and Pitch Bend sources.
    std::string kind;
    double value{0.0};  ///< 0..1; pitch bend's centre is 0.5.
    uint8_t channel{0}; ///< 0..15; optional in the spec, 0 by default.

    friend bool operator==(const ControlSpec &, const ControlSpec &) = default;
};

/// One ring operation (M2 sub-project 2's spec §6.10), at frame round(timeSeconds × sampleRate),
/// on a layer's live ring. gg-render cuts its process() calls at a capture's frame, so a capture
/// lands on that sample whatever the block size; a splice or clearSplices is a
/// gg::core::CommandEvent at its offset in the call that holds its frame, applied at the next
/// control-grid boundary as a plugin's command is.
/// - "splice": a mark `behind` seconds behind the ring's head (gg::core::CommandKind::AddSplice);
///   takes "behind" (>= 0, at most kRingMaxSeconds; 0 by default) and no other optional key.
/// - "clearSplices": removes every mark; takes no optional key.
/// - "capture": reads the ring on the calling thread (the thread is the audio thread between
///   calls, so there is no race). The window is the layer's loop window: the newest min(L, N)
///   frames while its Loop is on (L the Loop Length base), else the ring's newest min(head, N);
///   "splice" 0 (the default) is all of it, and "splice" k (1 to 32) the span of the k-th mark
///   inside the window, counted from the oldest (a k past the count is the last splice), as a
///   stereo buffer at the render's rate. The Splice parameter's selection doesn't narrow it.
///   "into", a layer letter, makes it that layer's sample (a layer reads it with its Source set to
///   Sample); "file" writes it as a 32-bit float WAV (parent directories are made; the path is used
///   as written, so a relative one is relative to the working directory, like -o, not to the spec).
///   At least one of "into" and "file" is required. A capture of an empty ring is bad input.
/// "layer" is a letter "A" to "D". "splice", "into" and "file" belong to "capture" alone and
/// "behind" to "splice" alone, so a key that would go unread is an error.
struct OperationSpec
{
    double timeSeconds{0.0};     ///< >= 0, rounding to a frame before the render's end.
    uint8_t layer{0};            ///< 0..3.
    std::string op;              ///< "splice", "clearSplices" or "capture".
    double behind{0.0};          ///< splice: seconds behind the head.
    uint32_t splice{0};          ///< capture: 0 for the whole ring, else a splice 1..32.
    std::optional<uint8_t> into; ///< capture: the layer whose sample it becomes.
    std::string file;            ///< capture: where to write it; "" for none.

    friend bool operator==(const OperationSpec &, const OperationSpec &) = default;
};

/// A steady host transport, set once before the render (gg::core::Processor::setTransport, with
/// hasBeats and 4 beats to the bar); the Processor advances it itself while it plays. Without
/// one the transport sources hold at 0.
struct TransportSpec
{
    double tempoBpm{120.0}; ///< > 0, finite; required in the spec.
    bool playing{true};     ///< Optional in the spec, true by default.
    double startBeat{0.0};  ///< The beat position at the render's first frame: >= 0, finite;
                            ///< optional in the spec, 0 by default.

    friend bool operator==(const TransportSpec &, const TransportSpec &) = default;
};

/// An instrument render: what the plugin's host would feed Giga Grains, with `sample` loaded.
/// Values are as in FxSpec, but in the instrument's metadata (Position is 0–1 of the sample).
struct InstrumentSpec
{
    /// The sample: a file, loaded with gg-media and converted to the render's rate (makeSample in
    /// Signals.h), or a built-in signal synthesized at the render's rate (so without a sampleRate
    /// of its own).
    SourceSpec sample;
    /// Base values set before the render, by parameter name, as in FxSpec.
    std::vector<std::pair<std::string, double>> params;
    /// In time order (equal times allowed: the later point wins).
    std::vector<AutomationPoint> automation;
    /// In time order of their starts (equal times allowed).
    std::vector<NoteSpec> notes;
    /// "samples": layers' samples by layer (0..3 for "A" to "D"), in layer order, each read as
    /// `sample` is and handed to that layer before the render (Processor::setSourceNow(layer,
    /// ...)). Layer A's is `sample`: "samples" can't name "A".
    std::vector<std::pair<uint8_t, SourceSpec>> layerSamples;
    /// "mod": routes set before the render, in the spec's order, on top of the factory routes.
    std::vector<RouteSpec> mod;
    /// "controls": in time order (equal times allowed).
    std::vector<ControlSpec> controls;
    /// "transport": the steady transport, or none.
    std::optional<TransportSpec> transport;
    /// "state": a GGST session state to restore before the render, or "" for none. gg-render
    /// resolves a relative path as a file source's (resolvePaths).
    std::string statePath;
    /// "operations": in time order (equal times allowed, run in list order).
    std::vector<OperationSpec> operations;
    /// "memoryCapMiB": the per-instance memory cap, kMemoryCapMinMiB to kMemoryCapMaxMiB, or
    /// nothing for the state's (kMemoryCapDefaultMiB without a state).
    std::optional<uint32_t> memoryCapMiB;
    /// True when "sample" was left out, which is allowed beside "state" alone: layer A's sample
    /// is then the state's, and `sample` is unused.
    bool noSample{false};

    friend bool operator==(const InstrumentSpec &, const InstrumentSpec &) = default;
};

/// An FX render: what the plugin's host would feed Giga Grains FX. Values are in the CLAP value
/// domain of Global Constraints' parameter table (Density 6.0 is 2^6 = 64 Hz, Size is log2
/// seconds, Level dB, and so on), within each parameter's range, and are converted with
/// static_cast<float> when they reach the Processor.
struct FxSpec
{
    /// The input signal: a built-in one synthesized at the render's rate (so without a
    /// sampleRate of its own), or a file converted to it with gg-media (ruling M1-aa); faded in
    /// and out, and silent after its own length (makeInput in Signals.h).
    SourceSpec input;
    /// 1 (mono) or 2 (stereo; see makeInput in Signals.h). Not allowed in the spec with a file
    /// input, which has the file's own channels; it stays 1 there, and makeInput ignores it.
    uint32_t channels{1};
    /// Base values set before the render, by parameter name. A parameter left out keeps its
    /// default.
    std::vector<std::pair<std::string, double>> params;
    /// In time order (equal times allowed: the later point wins).
    std::vector<AutomationPoint> automation;
    /// In time order of their starts (equal times allowed). A held note freezes the live ring
    /// and repitches the grains (spec §6.3).
    std::vector<NoteSpec> notes;
    /// "samples": layers' samples by layer (0..3 for "A" to "D"), in layer order, each read and
    /// made as an instrument's sample is and handed to that layer before the render
    /// (Processor::setSourceNow(layer, ...)). A layer reads its sample only when its Source is
    /// Sample (0), set through "params" ("B Source": 0): an FX layer's Source is Live by default.
    std::vector<std::pair<uint8_t, SourceSpec>> layerSamples;
    /// "mod": routes set before the render, in the spec's order, on top of the factory routes.
    std::vector<RouteSpec> mod;
    /// "controls": in time order (equal times allowed).
    std::vector<ControlSpec> controls;
    /// "transport": the steady transport, or none.
    std::optional<TransportSpec> transport;
    /// "state": a GGST session state to restore before the render, or "" for none. gg-render
    /// resolves a relative path as a file source's (resolvePaths).
    std::string statePath;
    /// "operations": in time order (equal times allowed, run in list order).
    std::vector<OperationSpec> operations;
    /// "memoryCapMiB": the per-instance memory cap, kMemoryCapMinMiB to kMemoryCapMaxMiB, or
    /// nothing for the state's (kMemoryCapDefaultMiB without a state).
    std::optional<uint32_t> memoryCapMiB;

    friend bool operator==(const FxSpec &, const FxSpec &) = default;
};

struct RenderSpec
{
    RenderMode mode{RenderMode::Sample};
    /// Output rate, and the default synthesis rate: a whole number of Hz, 1..768000.
    double sampleRate{48000.0};
    /// Seconds of stream activity, in (0, kMaxSeconds], at least one frame once rounded. In
    /// sample mode the output is longer by the release tail when grains are still sounding at
    /// the end; an instrument or FX render is exactly this long.
    double durationSeconds{3.0};
    /// Frames per process() call, 1..kMaxBlockSize; in instrument and FX mode also the
    /// Processor's maxBlockSize.
    uint32_t blockSize{128};
    uint64_t seed{0};          ///< Keys the engine's RNG and the noise source (sample, input).
    SourceSpec source;         ///< Sample mode only.
    StreamSpec stream;         ///< Sample mode only.
    InstrumentSpec instrument; ///< Instrument mode only.
    FxSpec fx;                 ///< FX mode only.

    friend bool operator==(const RenderSpec &, const RenderSpec &) = default;
};

/// The parameter whose name in either product (paramMeta's, matched exactly) is `name`, or nothing.
/// The bare M1 names of layer A's eleven M1 parameters ("Position" ... "Diffusion", displayed "A
/// Position" ... since M2 sub-project 1's spec §6.9) are layer A's too, so every M1 spec parses,
/// and so is "Quality" (M2 sub-project 3's spec §6.9).
/// A spec's params and automation take only its own product's names: parseRenderSpec rejects the
/// other's ("Macro Level" in an FX spec) as an unknown parameter.
std::optional<gg::core::ParamSlot> paramSlotNamed(std::string_view name);

/// Parses and validates a render spec. On failure returns nullopt and sets error to a message
/// naming the offending key (for example `stream.grainRateHz is missing` or
/// `notes[0].key must be an integer from 0 to 127`). File paths are left as written.
std::optional<RenderSpec> parseRenderSpec(const std::string &json, std::string &error);

/// Makes the spec's relative file paths (a file source, sample, layer sample, input or state)
/// relative to `baseDir`: baseDir / path, joined as std::filesystem paths. Absolute paths, and
/// the empty path of a built-in signal, are kept. Every path is UTF-8. gg-render passes the
/// spec's directory, or the --media-dir directory when it is given.
/// Throws BadInput for a path that isn't valid UTF-8 (on Windows).
void resolvePaths(RenderSpec &spec, const std::string &baseDir);

/// The path that UTF-8 `text` names (gg::media::fromUtf8): on Windows a wide path, so the
/// standard library's wide-character functions open it. Throws BadInput for text the conversion
/// refuses.
std::filesystem::path pathFromUtf8(const std::string &text);

/// `path` as UTF-8 (gg::media::toUtf8). Throws BadInput for a path the conversion refuses.
std::string utf8FromPath(const std::filesystem::path &path);
} // namespace gg::render

#endif // GG_RENDER_RENDERSPEC_H
