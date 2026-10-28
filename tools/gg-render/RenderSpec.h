// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_RENDER_RENDERSPEC_H
#define GG_RENDER_RENDERSPEC_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gg/core/ParamIds.h"
#include "gg/grain/Tier.h"

/// The render spec: a JSON description of one offline render, read by gg-render. It has two
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
/// An FX render ("mode": "fx") runs Giga Grains FX's gg::core::Processor over an input, with
/// parameters and automation in their CLAP value domains (see FxSpec):
///
/// ```json
/// {"mode":"fx","sampleRate":48000,"durationSeconds":4.0,"blockSize":128,"seed":1,
///  "input":{"kind":"sine|sweep|noise","frequencyHz":220,"startHz":50,"endHz":8000,
///           "seconds":3.0,"amplitude":0.5},
///  "channels":1,"params":{"Size":-2.7},
///  "automation":[{"time":1.0,"param":"Density","value":6.0}]}
/// ```
///
/// - "mode" is optional. In both modes sampleRate, durationSeconds, blockSize and seed are
///   required.
/// - Sample mode requires "source" and "stream", and every "stream" key but "distribution",
///   which defaults to 0 (periodic onsets, as in M0), and "quality", which defaults to "normal"
///   (see StreamSpec::quality). "source" needs "kind" plus the keys its kind uses (see
///   SourceSpec); "source.sampleRate" is optional. Keys a kind doesn't use may be present and
///   are ignored.
/// - FX mode requires "input", read as a source but without "sampleRate" or the "file" kind,
///   and "channels"; "params" and "automation" are optional.
/// - Any other key, including the other mode's, is an error, so a misspelt key never silently
///   falls back to a default.
/// - A sample-mode render covers durationSeconds of stream activity plus a release tail, so it
///   never stops in the middle of a grain; an FX render is exactly durationSeconds long (see
///   render() in Render.h).
namespace gg::render
{
inline constexpr double kMaxSampleRate = 768000.0;
inline constexpr double kMaxSeconds = 3600.0;
inline constexpr uint32_t kMaxBlockSize = 65536;

enum class SourceKind
{
    Sine,
    Sweep,
    Noise,
    File,
};

/// A test signal: what the grains read in sample mode, or the FX's input. Every number is
/// finite.
struct SourceSpec
{
    SourceKind kind{SourceKind::Sine};
    double frequencyHz{220.0}; ///< sine; > 0.
    double startHz{50.0};      ///< sweep; > 0.
    double endHz{8000.0};      ///< sweep; > 0.
    double seconds{4.0};       ///< sine, sweep, noise; in (0, kMaxSeconds].
    double amplitude{0.5};     ///< sine, sweep, noise: the peak level; >= 0.
    /// sine, sweep, noise: the rate the source is synthesized at, in (0, kMaxSampleRate].
    /// Optional; without it the source is synthesized at the render's sampleRate. A rate other
    /// than the render's exercises the engine's source-rate conversion.
    std::optional<double> sampleRate;
    /// file: a WAV, FLAC or MP3 file, non-empty. In a spec file it is relative to the spec's
    /// directory; gg-render resolves it before rendering, and makeSignal opens it as given.
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
    Sample, ///< The engine's Cloud stream over a source, plus a release tail.
    Fx,     ///< Giga Grains FX's Processor over an input, exactly durationSeconds long.
};

/// One automation point: the parameter named `param` (see paramSlotNamed) is set to `value` at
/// frame round(timeSeconds × sampleRate), as a host's parameter value event would set it.
struct AutomationPoint
{
    double timeSeconds{0.0}; ///< >= 0, on a frame before the render's end.
    std::string param;
    double value{0.0}; ///< In the parameter's CLAP value domain and range.

    friend bool operator==(const AutomationPoint &, const AutomationPoint &) = default;
};

/// An FX render: what the plugin's host would feed Giga Grains FX. Values are in the CLAP value
/// domain of Global Constraints' parameter table (Density 6.0 is 2^6 = 64 Hz, Size is log2
/// seconds, Level dB, and so on), within each parameter's range, and are converted with
/// static_cast<float> when they reach the Processor.
struct FxSpec
{
    /// The input signal, synthesized at the render's rate (so without a sampleRate of its own),
    /// silent after its `seconds`. Not a file.
    SourceSpec input;
    uint32_t channels{1}; ///< 1 (mono) or 2 (stereo; see makeInput in Signals.h).
    /// Base values set before the render, by parameter name. A parameter left out keeps its
    /// default.
    std::vector<std::pair<std::string, double>> params;
    /// In time order (equal times allowed: the later point wins).
    std::vector<AutomationPoint> automation;

    friend bool operator==(const FxSpec &, const FxSpec &) = default;
};

struct RenderSpec
{
    RenderMode mode{RenderMode::Sample};
    /// Output rate, and the default synthesis rate: a whole number of Hz, 1..768000.
    double sampleRate{48000.0};
    /// Seconds of stream activity, in (0, kMaxSeconds]. In sample mode the output is longer by
    /// the release tail when grains are still sounding at the end; an FX render is exactly this
    /// long.
    double durationSeconds{3.0};
    /// Frames per process() call, 1..kMaxBlockSize; in FX mode also the Processor's maxBlockSize.
    uint32_t blockSize{128};
    uint64_t seed{0};  ///< Keys the engine's RNG and the noise source (or input).
    SourceSpec source; ///< Sample mode only.
    StreamSpec stream; ///< Sample mode only.
    FxSpec fx;         ///< FX mode only.

    friend bool operator==(const RenderSpec &, const RenderSpec &) = default;
};

/// The parameter whose name (paramMeta's: "Mix", "Level", "Position", "Size", "Density",
/// "Distribution", "Pitch", "Spray", "Stereo" or "Freeze") is `name`, matched exactly.
std::optional<gg::core::ParamSlot> paramSlotNamed(std::string_view name);

/// Parses and validates a render spec. On failure returns nullopt and sets error to a message
/// naming the offending key (for example `stream.grainRateHz is missing` or
/// `automation[0].time must be >= 0 ...`).
std::optional<RenderSpec> parseRenderSpec(const std::string &json, std::string &error);
} // namespace gg::render

#endif // GG_RENDER_RENDERSPEC_H
