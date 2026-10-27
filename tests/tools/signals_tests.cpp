// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
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
using gg::render::makeSignal;
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
