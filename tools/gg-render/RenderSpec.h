// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_RENDER_RENDERSPEC_H
#define GG_RENDER_RENDERSPEC_H

#include <cstdint>
#include <optional>
#include <string>

/// The render spec: a JSON description of one offline render, read by gg-render.
///
/// ```json
/// {"sampleRate":48000,"durationSeconds":3.0,"blockSize":128,"seed":1,
///  "source":{"kind":"sine|sweep|noise|file","frequencyHz":220,"startHz":50,"endHz":8000,
///            "seconds":4.0,"amplitude":0.5,"path":"relative/to/spec.wav"},
///  "stream":{"grainRateHz":8,"grainDurationMs":60,"position":0.5,"positionJitter":0.2,
///            "pitchSemitones":0,"gain":0.8}}
/// ```
///
/// - Every top-level key and every "stream" key is required. "source" needs "kind" plus the
///   keys its kind uses (see SourceSpec); the others may be present and are ignored.
/// - Any other key is an error, so a misspelt key never silently falls back to a default.
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

/// What the grains read. Every number is finite.
struct SourceSpec
{
    SourceKind kind{SourceKind::Sine};
    double frequencyHz{220.0}; ///< sine; > 0.
    double startHz{50.0};      ///< sweep; > 0.
    double endHz{8000.0};      ///< sweep; > 0.
    double seconds{4.0};       ///< sine, sweep, noise; in (0, kMaxSeconds].
    double amplitude{0.5};     ///< sine, sweep, noise: the peak level; >= 0.
    /// file: a WAV, FLAC or MP3 file, non-empty. In a spec file it is relative to the spec's
    /// directory; gg-render resolves it before rendering, and makeSignal opens it as given.
    std::string path;

    friend bool operator==(const SourceSpec &, const SourceSpec &) = default;
};

/// gg::StreamParams as the spec spells it: the duration is in milliseconds. Every value is a
/// finite number; the engine clamps them to the ranges documented on gg::StreamParams.
struct StreamSpec
{
    double grainRateHz{20.0};
    double grainDurationMs{100.0};
    double position{0.5};
    double positionJitter{0.0};
    double pitchSemitones{0.0};
    double gain{0.5};

    friend bool operator==(const StreamSpec &, const StreamSpec &) = default;
};

struct RenderSpec
{
    double sampleRate{48000.0};  ///< Output and synthesis rate: a whole number of Hz, 1..768000.
    double durationSeconds{3.0}; ///< Length of the render, in (0, kMaxSeconds].
    uint32_t blockSize{128};     ///< Frames per process() call, 1..kMaxBlockSize.
    uint64_t seed{0};            ///< Keys the engine's RNG and the noise source.
    SourceSpec source;
    StreamSpec stream;

    friend bool operator==(const RenderSpec &, const RenderSpec &) = default;
};

/// Parses and validates a render spec. On failure returns nullopt and sets error to a message
/// naming the offending key (for example `stream.grainRateHz is missing`).
std::optional<RenderSpec> parseRenderSpec(const std::string &json, std::string &error);
} // namespace gg::render

#endif // GG_RENDER_RENDERSPEC_H
