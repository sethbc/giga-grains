// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "RenderSpec.h"
#include "Signals.h"
#include "gg/media/Convert.h"
#include "gg/media/Decode.h"
#include "gg/metrics/AudioData.h"
#include "gg/metrics/WavIO.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/SampleBuffer.h"

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

/// `path` as the UTF-8 the spec's paths are in.
std::string utf8(const std::filesystem::path &path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

/// The path UTF-8 `text` names.
std::filesystem::path fromUtf8(const std::string &text)
{
    return std::u8string(text.begin(), text.end());
}

/// A fresh directory of this name (UTF-8) under GG_TEST_OUTPUT_DIR, as UTF-8.
std::string freshDirectory(const std::string &name)
{
    const std::filesystem::path dir = std::filesystem::path{GG_TEST_OUTPUT_DIR} / fromUtf8(name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return utf8(dir);
}

/// What gg-media decodes `path` to, converted to `rate`.
std::shared_ptr<gg::SampleBuffer> decodeAt(const std::string &path, double rate)
{
    auto decoded = gg::media::decodeAudioFile(fromUtf8(path));
    INFO(decoded.error);
    REQUIRE(decoded.audio.has_value());
    return gg::media::convertRate(decoded.audio->buffer, rate);
}

std::vector<float> channelOf(const gg::SampleBuffer &b, uint32_t c)
{
    const float *x = b.channel(c);
    return {x, x + b.frames()};
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
    CHECK_THROWS_AS(makeSignal(missing, 48000.0, 1), gg::render::BadInput);

    // Paths are UTF-8 (on Windows WavIO opens them with dr_libs' wide functions, B-m9).
    const std::string dir = freshDirectory("signals-file-source-\xc3\xa9t\xc3\xa9");
    const std::string unicode = dir + "/gr\xc3\xa4in-\xe9\x9f\xb3.wav";
    REQUIRE(gg::metrics::writeWavFloat32(unicode, written));
    CHECK(
        bitIdentical(makeSignal({.kind = SourceKind::File, .path = unicode}, 48000.0, 1), written));
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

    // What parseRenderSpec never produces: other channel counts, a rate of its own.
    CHECK_THROWS_AS(makeInput(noise, 0, sr, 7), std::invalid_argument);
    CHECK_THROWS_AS(makeInput(noise, 3, sr, 7), std::invalid_argument);
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

TEST_CASE("file inputs are converted to the render's rate and faded", "[render]")
{
    // Ruling M1-aa: a file input goes through gg-media, mono or stereo as the file is, whatever
    // `channels` says, then gets the built-in inputs' fades (ruling M1-v).
    const std::string dir = freshDirectory("signals-file-input");
    const SourceSpec sine{
        .kind = SourceKind::Sine, .frequencyHz = 220.0, .seconds = 0.5, .amplitude = 0.5};
    AudioData stereo = makeSignal(sine, 44100.0, 1);
    SourceSpec fifth = sine;
    fifth.frequencyHz = 330.0;
    stereo.channels.push_back(makeSignal(fifth, 44100.0, 1).channels[0]);
    const std::string stereoPath = dir + "/stereo-44k1.wav";
    REQUIRE(gg::metrics::writeWavFloat32(stereoPath, stereo));
    const std::string monoPath = dir + "/mono-48k.wav";
    REQUIRE(gg::metrics::writeWavFloat32(monoPath, makeSignal(sine, 48000.0, 1)));

    const SourceSpec stereoFile{.kind = SourceKind::File, .path = stereoPath};
    for (const uint32_t channels : {1u, 2u})
    {
        CAPTURE(channels);
        const AudioData in = makeInput(stereoFile, channels, 48000.0, 7);
        CHECK(in.sampleRate == 48000.0);
        REQUIRE(in.channels.size() == 2);
        const auto raw = decodeAt(stereoPath, 48000.0);
        REQUIRE(raw->frames() == 24000); // llround(22050 × 48000 / 44100)
        checkFaded(in.channels[0], channelOf(*raw, 0), 48000.0);
        checkFaded(in.channels[1], channelOf(*raw, 1), 48000.0);
    }

    const AudioData mono = makeInput({.kind = SourceKind::File, .path = monoPath}, 2, 48000.0, 7);
    REQUIRE(mono.channels.size() == 1);
    checkFaded(mono.channels[0], makeSignal(sine, 48000.0, 1).channels[0], 48000.0);

    const SourceSpec missing{.kind = SourceKind::File, .path = dir + "/no-such-file.wav"};
    CHECK_THROWS_AS(makeInput(missing, 1, 48000.0, 7), gg::render::BadInput);
}

TEST_CASE("instrument samples are prepared at the render's rate", "[render]")
{
    // A built-in sample is synthesized at the render's rate, mono.
    const SourceSpec sweep{.kind = SourceKind::Sweep,
                           .startHz = 50.0,
                           .endHz = 8000.0,
                           .seconds = 0.5,
                           .amplitude = 0.5};
    const auto built = gg::render::makeSample(sweep, 48000.0, 3);
    REQUIRE(built != nullptr);
    CHECK(built->buffer().sampleRate() == 48000.0);
    REQUIRE(built->buffer().channels() == 1);
    CHECK(channelOf(built->buffer(), 0) == makeSignal(sweep, 48000.0, 3).channels[0]);
    CHECK(built->pairs().data() == nullptr);

    // A file goes through gg-media's loader: converted, its channels kept, no fades.
    const std::string dir = freshDirectory("signals-instrument-sample");
    AudioData stereo = makeSignal(sweep, 44100.0, 3);
    stereo.channels.push_back(stereo.channels[0]);
    const std::string path = dir + "/sample-44k1.wav";
    REQUIRE(gg::metrics::writeWavFloat32(path, stereo));
    const auto file = gg::render::makeSample({.kind = SourceKind::File, .path = path}, 48000.0, 3);
    REQUIRE(file != nullptr);
    CHECK(file->buffer().sampleRate() == 48000.0);
    REQUIRE(file->buffer().channels() == 2);
    const auto raw = decodeAt(path, 48000.0);
    CHECK(channelOf(file->buffer(), 0) == channelOf(*raw, 0));
    CHECK(channelOf(file->buffer(), 1) == channelOf(*raw, 1));
    CHECK(file->pairs().data() != nullptr);

    CHECK_THROWS_AS(
        gg::render::makeSample({.kind = SourceKind::File, .path = dir + "/none.wav"}, 48000.0, 3),
        gg::render::BadInput);
}

TEST_CASE("write-media writes the test media", "[render]")
{
    const std::string dir = freshDirectory("signals-write-media");
    std::string error;
    REQUIRE(gg::render::writeTestMedia(dir, error));
    CHECK(error.empty());

    // tone-44k1.wav: stereo, 44.1 kHz, 4 s; a 50 Hz to 8 kHz exponential sweep on the left, a
    // 220 Hz sine on the right, each at -6 dBFS.
    const double minus6 = std::pow(10.0, -6.0 / 20.0);
    const auto tone = gg::metrics::readAudioFile(dir + "/tone-44k1.wav");
    REQUIRE(tone.has_value());
    CHECK(tone->sampleRate == 44100.0);
    REQUIRE(tone->channels.size() == 2);
    CHECK(tone->channels[0].size() == 176400);
    CHECK(tone->channels[1].size() == 176400);
    const SourceSpec sweep{.kind = SourceKind::Sweep,
                           .startHz = 50.0,
                           .endHz = 8000.0,
                           .seconds = 4.0,
                           .amplitude = minus6};
    const SourceSpec sine{
        .kind = SourceKind::Sine, .frequencyHz = 220.0, .seconds = 4.0, .amplitude = minus6};
    CHECK(tone->channels[0] == makeSignal(sweep, 44100.0, 0).channels[0]);
    CHECK(tone->channels[1] == makeSignal(sine, 44100.0, 0).channels[0]);
    CHECK_THAT(20.0 * std::log10(peak(tone->channels[0])), WithinAbs(-6.0, 0.01));
    CHECK_THAT(20.0 * std::log10(peak(tone->channels[1])), WithinAbs(-6.0, 0.01));

    // loop-48k.wav: mono, 48 kHz, 4 s; a 30 ms noise burst every 250 ms, decaying, -3 dBFS peak.
    const auto loop = gg::metrics::readAudioFile(dir + "/loop-48k.wav");
    REQUIRE(loop.has_value());
    CHECK(loop->sampleRate == 48000.0);
    REQUIRE(loop->channels.size() == 1);
    const auto &x = loop->channels[0];
    REQUIRE(x.size() == 192000);
    CHECK_THAT(20.0 * std::log10(peak(x)), WithinAbs(-3.0, 1e-4));
    constexpr std::size_t period = 12000; // 250 ms
    constexpr std::size_t burst = 1440;   // 30 ms
    for (std::size_t start = 0; start < x.size(); start += period)
    {
        CAPTURE(start);
        const std::vector<float> head(x.begin() + start, x.begin() + start + 240);
        const std::vector<float> tail(x.begin() + start + burst - 240, x.begin() + start + burst);
        const std::vector<float> gap(x.begin() + start + burst, x.begin() + start + period);
        CHECK(peak(head) > 0.3f);
        CHECK(peak(tail) < 0.01f * peak(head)); // decays
        CHECK(peak(gap) == 0.0f);               // silent between bursts
    }
    // Seed 7's noise, so every burst differs.
    CHECK_FALSE(std::equal(x.begin(), x.begin() + burst, x.begin() + period));

    // Writing again gives the same files.
    const std::string again = freshDirectory("signals-write-media-again");
    REQUIRE(gg::render::writeTestMedia(again, error));
    CHECK(gg::metrics::readAudioFile(again + "/loop-48k.wav")->channels == loop->channels);
    CHECK(gg::metrics::readAudioFile(again + "/tone-44k1.wav")->channels == tone->channels);

    // A directory that can't be made is an error naming it.
    const std::string blocked = outputPath("signals-write-media-blocked");
    {
        std::ofstream file(blocked); // a file where the directory would go
    }
    CHECK_FALSE(gg::render::writeTestMedia(blocked + "/media", error));
    CHECK_FALSE(error.empty());
}
