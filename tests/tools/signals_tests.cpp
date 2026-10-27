// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "RenderSpec.h"
#include "Signals.h"
#include "gg/metrics/AudioData.h"
#include "gg/metrics/WavIO.h"

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using gg::metrics::AudioData;
using gg::render::inputFadeFrames;
using gg::render::makeInput;
using gg::render::makeSignal;
using gg::render::raisedCosine;
using gg::render::SourceKind;
using gg::render::SourceSpec;

namespace
{
bool bitIdentical(const AudioData &a, const AudioData &b)
{
    if (a.sampleRate != b.sampleRate || a.channels.size() != b.channels.size())
    {
        return false;
    }
    for (std::size_t c = 0; c < a.channels.size(); ++c)
    {
        const auto &x = a.channels[c];
        const auto &y = b.channels[c];
        if (x.size() != y.size() || std::memcmp(x.data(), y.data(), x.size() * sizeof(float)) != 0)
        {
            return false;
        }
    }
    return true;
}

float peak(const std::vector<float> &x)
{
    float p = 0.0f;
    for (const float v : x)
    {
        p = std::max(p, std::abs(v));
    }
    return p;
}

/// The mean frequency of x over [fromSec, toSec), from its zero crossings.
double zeroCrossingHz(const std::vector<float> &x, double sampleRate, double fromSec, double toSec)
{
    const auto from = static_cast<std::size_t>(fromSec * sampleRate);
    const auto to = static_cast<std::size_t>(toSec * sampleRate);
    int crossings = 0;
    for (std::size_t n = from + 1; n < to; ++n)
    {
        if ((x[n - 1] < 0.0f) != (x[n] < 0.0f))
        {
            ++crossings;
        }
    }
    return crossings / (2.0 * (toSec - fromSec));
}

/// Checks that x is raw with the FX input's fades (ruling M1-v): L = inputFadeFrames(sr) frames
/// at each end shaped by the raised cosine 0.5 − 0.5·cos(π·k/L), k frames from that end, so the
/// first and last samples are exactly 0, and every sample in between bit for bit as in raw.
/// Assumes raw is at least 2L long.
void checkFaded(const std::vector<float> &x, const std::vector<float> &raw, double sr)
{
    const auto fade = static_cast<std::size_t>(inputFadeFrames(sr));
    REQUIRE(x.size() == raw.size());
    const std::size_t frames = x.size();
    REQUIRE(frames >= 2 * fade);
    CHECK(x.front() == 0.0f);
    CHECK(x.back() == 0.0f);
    CHECK(std::memcmp(x.data() + fade, raw.data() + fade, (frames - 2 * fade) * sizeof(float)) ==
          0);
    for (std::size_t k = 0; k < fade; ++k)
    {
        const double g = 0.5 - 0.5 * std::cos(std::numbers::pi * static_cast<double>(k) /
                                              static_cast<double>(fade));
        CHECK_THAT(x[k], WithinAbs(raw[k] * g, 1e-7));
        CHECK_THAT(x[frames - 1 - k], WithinAbs(raw[frames - 1 - k] * g, 1e-7));
    }
    // The fade is exactly L frames: its last frame is still attenuated.
    if (raw[fade - 1] != 0.0f)
    {
        CHECK(std::abs(x[fade - 1]) < std::abs(raw[fade - 1]));
    }
    if (raw[frames - fade] != 0.0f)
    {
        CHECK(std::abs(x[frames - fade]) < std::abs(raw[frames - fade]));
    }
}

std::string outputPath(const std::string &name)
{
    const std::filesystem::path dir{GG_TEST_OUTPUT_DIR};
    std::filesystem::create_directories(dir);
    return (dir / name).string();
}
} // namespace

TEST_CASE("signals are deterministic", "[render]")
{
    const SourceSpec noise{.kind = SourceKind::Noise, .seconds = 1.0, .amplitude = 0.5};
    const AudioData a = makeSignal(noise, 48000.0, 1);
    const AudioData b = makeSignal(noise, 48000.0, 1);
    const AudioData c = makeSignal(noise, 48000.0, 2);

    REQUIRE(a.sampleRate == 48000.0);
    REQUIRE(a.channels.size() == 1);
    REQUIRE(a.channels[0].size() == 48000);
    CHECK(bitIdentical(a, b));
    CHECK_FALSE(bitIdentical(a, c));
    CHECK(peak(a.channels[0]) <= 0.5f);
    CHECK(peak(a.channels[0]) > 0.49f);

    // The same holds for the synthesized tones.
    const SourceSpec sweep{
        .kind = SourceKind::Sweep, .startHz = 50.0, .endHz = 8000.0, .seconds = 1.0};
    CHECK(bitIdentical(makeSignal(sweep, 48000.0, 1), makeSignal(sweep, 48000.0, 1)));
}

TEST_CASE("sine and sweep have the requested level and frequency", "[render]")
{
    constexpr double sr = 48000.0;

    const SourceSpec sine{
        .kind = SourceKind::Sine, .frequencyHz = 220.0, .seconds = 4.0, .amplitude = 0.5};
    const AudioData s = makeSignal(sine, sr, 1);
    REQUIRE(s.sampleRate == sr);
    REQUIRE(s.channels.size() == 1);
    REQUIRE(s.channels[0].size() == 192000);
    CHECK_THAT(peak(s.channels[0]), WithinAbs(0.5, 1e-4));
    CHECK_THAT(zeroCrossingHz(s.channels[0], sr, 1.0, 2.0), WithinAbs(220.0, 1.0));

    // Exponential: halfway through, a 50 -> 8000 Hz sweep is at the geometric mean, 632 Hz
    // (a linear sweep would be at 4025 Hz).
    const SourceSpec sweep{.kind = SourceKind::Sweep,
                           .startHz = 50.0,
                           .endHz = 8000.0,
                           .seconds = 4.0,
                           .amplitude = 0.5};
    const AudioData w = makeSignal(sweep, sr, 1);
    REQUIRE(w.channels.size() == 1);
    REQUIRE(w.channels[0].size() == 192000);
    CHECK_THAT(peak(w.channels[0]), WithinAbs(0.5, 1e-3));
    CHECK_THAT(zeroCrossingHz(w.channels[0], sr, 0.9, 1.1),
               WithinRel(50.0 * std::pow(160.0, 0.25), 0.03));
    CHECK_THAT(zeroCrossingHz(w.channels[0], sr, 1.95, 2.05),
               WithinRel(std::sqrt(50.0 * 8000.0), 0.03));
    CHECK_THAT(zeroCrossingHz(w.channels[0], sr, 2.95, 3.05),
               WithinRel(50.0 * std::pow(160.0, 0.75), 0.03));
}

TEST_CASE("synthesized sources use source.sampleRate, defaulting to the render rate", "[render]")
{
    SourceSpec sine{
        .kind = SourceKind::Sine, .frequencyHz = 220.0, .seconds = 2.0, .amplitude = 0.5};

    const AudioData atRenderRate = makeSignal(sine, 44100.0, 1);
    CHECK(atRenderRate.sampleRate == 44100.0);
    REQUIRE(atRenderRate.channels.size() == 1);
    CHECK(atRenderRate.channels[0].size() == 88200);
    CHECK_THAT(zeroCrossingHz(atRenderRate.channels[0], 44100.0, 0.5, 1.5), WithinAbs(220.0, 1.0));

    sine.sampleRate = 48000.0;
    const AudioData atOwnRate = makeSignal(sine, 44100.0, 1);
    CHECK(atOwnRate.sampleRate == 48000.0);
    REQUIRE(atOwnRate.channels.size() == 1);
    CHECK(atOwnRate.channels[0].size() == 96000);
    CHECK_THAT(zeroCrossingHz(atOwnRate.channels[0], 48000.0, 0.5, 1.5), WithinAbs(220.0, 1.0));

    // Noise too: the same draws, at the source's own rate.
    const SourceSpec noise{
        .kind = SourceKind::Noise, .seconds = 1.0, .amplitude = 0.5, .sampleRate = 96000.0};
    const AudioData n = makeSignal(noise, 48000.0, 1);
    CHECK(n.sampleRate == 96000.0);
    REQUIRE(n.channels.size() == 1);
    CHECK(n.channels[0].size() == 96000);
}

TEST_CASE("file sources keep their channels and sample rate", "[render]")
{
    const AudioData written{44100.0, {{0.0f, 0.25f, -0.5f, 1.0f}, {0.125f, 0.0f, 0.0f, -1.0f}}};
    const std::string path = outputPath("signals-file-source.wav");
    REQUIRE(gg::metrics::writeWavFloat32(path, written));

    const SourceSpec file{.kind = SourceKind::File, .path = path};
    CHECK(bitIdentical(makeSignal(file, 48000.0, 1), written));

    const SourceSpec missing{.kind = SourceKind::File, .path = outputPath("no-such-file.wav")};
    CHECK_THROWS_AS(makeSignal(missing, 48000.0, 1), std::runtime_error);
}

TEST_CASE("stereo input differs per channel", "[render]")
{
    constexpr double sr = 48000.0;
    const SourceSpec noise{.kind = SourceKind::Noise, .seconds = 1.0, .amplitude = 0.5};
    const SourceSpec sine{
        .kind = SourceKind::Sine, .frequencyHz = 220.0, .seconds = 1.0, .amplitude = 0.5};
    const SourceSpec sweep{.kind = SourceKind::Sweep,
                           .startHz = 50.0,
                           .endHz = 8000.0,
                           .seconds = 1.0,
                           .amplitude = 0.5};

    // A mono input is the signal itself, faded in and out, at the render's rate.
    for (const SourceSpec &s : {noise, sine, sweep})
    {
        const AudioData mono = makeInput(s, 1, sr, 7);
        CHECK(mono.sampleRate == sr);
        REQUIRE(mono.channels.size() == 1);
        checkFaded(mono.channels[0], makeSignal(s, sr, 7).channels[0], sr);
    }

    // Noise: channel 1 is the noise of seed + 1.
    const AudioData n = makeInput(noise, 2, sr, 7);
    REQUIRE(n.channels.size() == 2);
    checkFaded(n.channels[0], makeSignal(noise, sr, 7).channels[0], sr);
    checkFaded(n.channels[1], makeSignal(noise, sr, 8).channels[0], sr);
    CHECK_FALSE(n.channels[0] == n.channels[1]);

    // Sine: channel 1 is a fifth up, frequencyHz × 1.5.
    const AudioData s = makeInput(sine, 2, sr, 7);
    REQUIRE(s.channels.size() == 2);
    REQUIRE(s.channels[1].size() == 48000);
    CHECK_FALSE(s.channels[0] == s.channels[1]);
    CHECK_THAT(zeroCrossingHz(s.channels[0], sr, 0.25, 0.75), WithinAbs(220.0, 1.0));
    CHECK_THAT(zeroCrossingHz(s.channels[1], sr, 0.25, 0.75), WithinAbs(330.0, 1.0));
    CHECK_THAT(peak(s.channels[1]), WithinAbs(0.5, 1e-4));
    SourceSpec fifth = sine;
    fifth.frequencyHz = 330.0;
    checkFaded(s.channels[1], makeSignal(fifth, sr, 7).channels[0], sr);

    // Sweep: both channels carry the same sweep.
    const AudioData w = makeInput(sweep, 2, sr, 7);
    REQUIRE(w.channels.size() == 2);
    CHECK(w.channels[0] == w.channels[1]);
    checkFaded(w.channels[1], makeSignal(sweep, sr, 7).channels[0], sr);

    // What parseRenderSpec never produces: other channel counts, a file, a rate of its own.
    CHECK_THROWS_AS(makeInput(noise, 0, sr, 7), std::invalid_argument);
    CHECK_THROWS_AS(makeInput(noise, 3, sr, 7), std::invalid_argument);
    CHECK_THROWS_AS(makeInput({.kind = SourceKind::File, .path = "in.wav"}, 1, sr, 7),
                    std::invalid_argument);
    SourceSpec ownRate = sine;
    ownRate.sampleRate = 44100.0;
    CHECK_THROWS_AS(makeInput(ownRate, 1, sr, 7), std::invalid_argument);
}

TEST_CASE("fx input fades in and out over 10 ms", "[render]")
{
    // round(0.01 × sr) frames, at least 1.
    CHECK(inputFadeFrames(48000.0) == 480);
    CHECK(inputFadeFrames(44100.0) == 441);
    CHECK(inputFadeFrames(96000.0) == 960);
    CHECK(inputFadeFrames(22050.0) == 221); // 220.5 rounds half up
    CHECK(inputFadeFrames(8.0) == 1);

    // The polynomial raised cosine: exact at 0, 1 at 1, and the libm form in between.
    CHECK(raisedCosine(0.0) == 0.0);
    CHECK_THAT(raisedCosine(1.0), WithinAbs(1.0, 1e-15));
    for (int i = 0; i <= 1000; ++i)
    {
        const double x = i / 1000.0;
        CAPTURE(x);
        CHECK_THAT(raisedCosine(x), WithinAbs(0.5 - 0.5 * std::cos(std::numbers::pi * x), 1e-15));
    }

    // Every input kind and both channels, at two rates: zero at both ends, the raised cosine
    // over L frames, and untouched in between.
    for (const double sr : {48000.0, 44100.0})
    {
        CAPTURE(sr);
        for (const SourceKind kind : {SourceKind::Sine, SourceKind::Sweep, SourceKind::Noise})
        {
            const SourceSpec input{.kind = kind, .seconds = 0.5, .amplitude = 0.5};
            const AudioData in = makeInput(input, 2, sr, 3);
            REQUIRE(in.channels.size() == 2);
            checkFaded(in.channels[0], makeSignal(input, sr, 3).channels[0], sr);
        }
    }

    // An input shorter than both fades gets both: still 0 at each end, never louder.
    const SourceSpec shortNoise{.kind = SourceKind::Noise, .seconds = 0.005, .amplitude = 0.5};
    const AudioData shortIn = makeInput(shortNoise, 1, 48000.0, 3);
    const AudioData shortRaw = makeSignal(shortNoise, 48000.0, 3);
    REQUIRE(shortIn.channels[0].size() == 240);
    CHECK(shortIn.channels[0].front() == 0.0f);
    CHECK(shortIn.channels[0].back() == 0.0f);
    for (std::size_t k = 0; k < 240; ++k)
    {
        CHECK(std::abs(shortIn.channels[0][k]) <= std::abs(shortRaw.channels[0][k]));
    }

    // Sample-mode sources have no fades: a sine starts at sin(0) = 0 but its second sample
    // isn't attenuated.
    const SourceSpec sine{.kind = SourceKind::Sine, .seconds = 0.5, .amplitude = 0.5};
    const AudioData source = makeSignal(sine, 48000.0, 3);
    CHECK(source.channels[0][1] ==
          static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 220.0 / 48000.0)));
}
