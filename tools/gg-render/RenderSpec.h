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
///            "pitchSemitones":0,"gain":0.8,"distribution":0,"quality":"eco|normal"}}
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
/// - "mode" is optional. In every mode sampleRate, durationSeconds, blockSize and seed are
///   required, and durationSeconds must round to at least one frame at sampleRate.
/// - Sample mode requires "source" and "stream", and every "stream" key but "distribution",
///   which defaults to 0 (periodic onsets, as in M0), and "quality", which defaults to "normal"
///   (see StreamSpec::quality). "source" needs "kind" plus the keys its kind uses (see
///   SourceSpec); "source.sampleRate" is optional. Keys a kind doesn't use may be present and
///   are ignored.
/// - Instrument mode requires "sample", read as a source but without "sampleRate"; "params",
///   "automation" and "notes" are optional. It has no input, so no "channels".
/// - FX mode requires "input", read as a source but without "sampleRate", and "channels",
///   except with a file input, which has the file's own channels and so takes no "channels";
///   "params", "automation" and "notes" are optional.
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
    std::optional<double> sampleRate;
    /// file: a WAV, FLAC or MP3 file (an AIFF too, for a sample or an input), non-empty, UTF-8.
    /// In a spec file a relative path is relative to the spec's directory, or to --media-dir;
    /// gg-render resolves it before rendering (resolvePaths), and makeSignal, makeSample and
    /// makeInput open it as given.
    std::string path;

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
    /// The engine's CloudParams::quality (ruling 2): "eco" or "normal". Optional in the spec. The
    /// grain-event lines don't print it, so an Eco render's event log is the Normal one's.
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
/// - The Processor ignores velocity (D6), so a note has none.
struct NoteSpec
{
    double timeSeconds{0.0};     ///< >= 0, rounding to a frame before the render's end.
    uint8_t key{60};             ///< 0..127; 60 plays at the sample's own pitch.
    double durationSeconds{1.0}; ///< > 0, at most kMaxSeconds; the Off rounds past the On (R17).
    uint8_t channel{0};          ///< 0..15; optional in the spec, 0 by default.

    friend bool operator==(const NoteSpec &, const NoteSpec &) = default;
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

/// The parameter whose name (paramMeta's: "Mix", "Level", "Position", "Size", "Density",
/// "Distribution", "Pitch", "Spray", "Stereo" or "Freeze") is `name`, matched exactly.
std::optional<gg::core::ParamSlot> paramSlotNamed(std::string_view name);

/// Parses and validates a render spec. On failure returns nullopt and sets error to a message
/// naming the offending key (for example `stream.grainRateHz is missing` or
/// `notes[0].key must be an integer from 0 to 127`). File paths are left as written.
std::optional<RenderSpec> parseRenderSpec(const std::string &json, std::string &error);

/// Makes the spec's relative file paths (a file source, sample or input) relative to `baseDir`:
/// baseDir / path, joined as std::filesystem paths. Absolute paths, and the empty path of a
/// built-in signal, are kept. Every path is UTF-8. gg-render passes the spec's directory, or the
/// --media-dir directory when it is given.
/// Throws BadInput for a path that isn't valid UTF-8 (on Windows).
void resolvePaths(RenderSpec &spec, const std::string &baseDir);

/// The path that UTF-8 `text` names: on Windows a wide path, so dr_libs' and the standard
/// library's wide-character functions open it. Throws BadInput for text the conversion refuses.
std::filesystem::path pathFromUtf8(const std::string &text);

/// `path` as UTF-8. Throws BadInput for a path the conversion refuses.
std::string utf8FromPath(const std::filesystem::path &path);
} // namespace gg::render

#endif // GG_RENDER_RENDERSPEC_H
