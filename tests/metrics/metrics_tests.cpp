// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include "gg/core/Rng.h"
#include "gg/metrics/AudioData.h"
#include "gg/metrics/Metrics.h"
#include "gg/metrics/Spectrogram.h"
#include "gg/metrics/WavIO.h"
#include "support/Utf8.h"

using Catch::Matchers::WithinAbs;
using gg::metrics::AudioData;

namespace
{
constexpr double kSampleRate = 48000.0;

std::vector<float> sine(double freq, double amp, size_t frames, double sampleRate = kSampleRate)
{
    std::vector<float> out(frames);
    for (size_t n = 0; n < frames; ++n)
    {
        const double phase = 2.0 * std::numbers::pi * freq * static_cast<double>(n) / sampleRate;
        out[n] = static_cast<float>(amp * std::sin(phase));
    }
    return out;
}

std::vector<float> noise(double amp, size_t frames, uint32_t stream)
{
    std::vector<float> out(frames);
    for (size_t n = 0; n < frames; ++n)
    {
        const auto h = gg::rng::hash({1, 0, stream, gg::rng::Field::SignalNoise, n});
        out[n] = static_cast<float>(amp * gg::rng::bipolar(h));
    }
    return out;
}

/// A scratch file in this build tree, so concurrent builds never share one.
std::string outputPath(const std::string &name)
{
    const std::filesystem::path dir{GG_TEST_OUTPUT_DIR};
    std::filesystem::create_directories(dir);
    return gg::test::utf8(dir / name);
}

/// Lower edge of log-spaced band b (b = 32 is the top edge, Nyquist).
double bandEdgeHz(int b, double sampleRate)
{
    return 20.0 * std::pow(sampleRate / 2.0 / 20.0, b / 32.0);
}
} // namespace

TEST_CASE("peak, dc and rms of a known sine", "[metrics]")
{
    const AudioData audio{kSampleRate, {sine(1000.0, 0.5, 48000)}};
    const auto m = gg::metrics::computeMetrics(audio);

    REQUIRE_THAT(m.peakDb, WithinAbs(-6.0206, 0.01));
    REQUIRE(m.dc.size() == 1);
    REQUIRE(std::abs(m.dc[0]) < 1e-6);
    REQUIRE(m.rmsEnvelopeDb.size() == 100);
    for (size_t i = 0; i < m.rmsEnvelopeDb.size(); ++i)
    {
        INFO("frame " << i);
        REQUIRE_THAT(m.rmsEnvelopeDb[i], WithinAbs(-9.0309, 0.05));
    }
    REQUIRE(m.nonFinite == 0);
    REQUIRE(m.denormals == 0);
}

TEST_CASE("silence floors at -200 dB", "[metrics]")
{
    const AudioData audio{kSampleRate, {std::vector<float>(4800, 0.0f)}};
    const auto m = gg::metrics::computeMetrics(audio);

    REQUIRE(m.peakDb == -200.0);
    REQUIRE(m.rmsEnvelopeDb.size() == 10);
    for (double v : m.rmsEnvelopeDb)
    {
        REQUIRE(v == -200.0);
    }
    REQUIRE(m.bandProfileDb.size() == 32);
    for (double v : m.bandProfileDb)
    {
        REQUIRE(v == -200.0);
    }
    REQUIRE(m.clickScore == 0.0);
}

TEST_CASE("band profile peaks at the sine band", "[metrics]")
{
    const AudioData audio{kSampleRate, {sine(1000.0, 0.5, 48000)}};
    const auto m = gg::metrics::computeMetrics(audio);

    REQUIRE(m.bandProfileDb.size() == 32);
    const auto peak = std::max_element(m.bandProfileDb.begin(), m.bandProfileDb.end());
    const int band = static_cast<int>(std::distance(m.bandProfileDb.begin(), peak));
    INFO("argmax band " << band << " at " << *peak << " dB");
    REQUIRE(bandEdgeHz(band, kSampleRate) <= 1000.0);
    REQUIRE(1000.0 < bandEdgeHz(band + 1, kSampleRate));
    // Band power is normalised so a sine's band holds its mean square (0.125, -9.03 dB).
    REQUIRE_THAT(*peak, WithinAbs(-9.0309, 0.5));
}

TEST_CASE("correlation of identical, inverted, silent", "[metrics]")
{
    const auto left = sine(440.0, 0.5, 48000);
    std::vector<float> inverted(left.size());
    std::transform(left.begin(), left.end(), inverted.begin(), [](float x) { return -x; });

    const auto same = gg::metrics::computeMetrics({kSampleRate, {left, left}});
    REQUIRE_THAT(same.stereoCorrelation, WithinAbs(1.0, 1e-6));

    const auto opposite = gg::metrics::computeMetrics({kSampleRate, {left, inverted}});
    REQUIRE_THAT(opposite.stereoCorrelation, WithinAbs(-1.0, 1e-6));

    const std::vector<float> silent(left.size(), 0.0f);
    const auto oneSilent = gg::metrics::computeMetrics({kSampleRate, {left, silent}});
    REQUIRE(oneSilent.stereoCorrelation == 1.0);

    const auto mono = gg::metrics::computeMetrics({kSampleRate, {left}});
    REQUIRE(mono.stereoCorrelation == 1.0);
}

TEST_CASE("click score separates clicks from clean audio", "[metrics]")
{
    const double limit = gg::metrics::Tolerances{}.maxClickScore;
    const auto clean = gg::metrics::computeMetrics({kSampleRate, {sine(220.0, 0.5, 48000)}});
    INFO("clean sine score " << clean.clickScore);
    REQUIRE(clean.clickScore < limit);

    const auto hiss = gg::metrics::computeMetrics({kSampleRate, {noise(0.5, 48000, 0)}});
    INFO("noise score " << hiss.clickScore);
    REQUIRE(hiss.clickScore < limit);

    auto stepped = sine(220.0, 0.5, 48000);
    for (size_t n = 24000; n < stepped.size(); ++n)
    {
        stepped[n] += 0.5f;
    }
    const auto click = gg::metrics::computeMetrics({kSampleRate, {stepped}});
    INFO("stepped sine score " << click.clickScore);
    REQUIRE(click.clickScore > limit);
}

TEST_CASE("click score is local, so a loud passage does not hide a quiet click", "[metrics]")
{
    // One second of loud noise, then one of a quiet sine with a -40 dBFS step in it. Normalized
    // by d's RMS over the whole render (metricsVersion 1, 0.5 here, set by the noise), the step
    // would score 0.02. Against the sine's own level around it, it stands out.
    const double limit = gg::metrics::Tolerances{}.maxClickScore;
    auto audio = noise(0.5, 48000, 5);
    const auto quiet = sine(220.0, 0.1, 48000);
    audio.insert(audio.end(), quiet.begin(), quiet.end());
    const auto clean = gg::metrics::computeMetrics({kSampleRate, {audio}});
    INFO("clean score " << clean.clickScore);
    REQUIRE(clean.clickScore < limit);

    for (size_t n = 72000; n < audio.size(); ++n)
    {
        audio[n] += 0.01f;
    }
    const auto click = gg::metrics::computeMetrics({kSampleRate, {audio}});
    INFO("stepped score " << click.clickScore);
    REQUIRE(click.clickScore > limit);
}

TEST_CASE("click score leaves out the click's own neighbours and floors the level", "[metrics]")
{
    const double limit = gg::metrics::Tolerances{}.maxClickScore;

    // A single-sample spike in a quiet sine moves d at three samples (a, 2a, a); the level at the
    // middle one leaves out the other two, so the spike reads against the sine alone.
    auto spiked = sine(220.0, 0.1, 48000);
    const auto smooth = gg::metrics::computeMetrics({kSampleRate, {spiked}});
    spiked[24000] += 0.1f;
    const auto spike = gg::metrics::computeMetrics({kSampleRate, {spiked}});
    INFO("smooth " << smooth.clickScore << ", spiked " << spike.clickScore);
    REQUIRE(spike.clickScore > 10.0 * limit);

    // A step out of digital silence: its window holds only zeros, so the level is floored at 1e-6
    // and the score is d / 1e-6, finite.
    std::vector<float> silentStep(4800, 0.0f);
    for (size_t n = 2400; n < silentStep.size(); ++n)
    {
        silentStep[n] = 0.01f;
    }
    const auto step = gg::metrics::computeMetrics({kSampleRate, {silentStep}});
    REQUIRE(std::isfinite(step.clickScore));
    REQUIRE_THAT(step.clickScore, WithinAbs(0.01 / 1e-6, 1e-3));

    // Only d above 1e-3 counts.
    for (size_t n = 2400; n < silentStep.size(); ++n)
    {
        silentStep[n] = 0.0009f;
    }
    REQUIRE(gg::metrics::computeMetrics({kSampleRate, {silentStep}}).clickScore == 0.0);

    // The window needs a sample rate: without one there is no score.
    REQUIRE(gg::metrics::computeMetrics({0.0, {spiked}}).clickScore == 0.0);
}

TEST_CASE("huge finite input reports non-finite band power", "[metrics]")
{
    // Samples near float's limit overflow the float FFT to inf. The band reads -200, the count
    // says why, and compare fails on it, so a golden can't record inf (which JSON can't hold).
    auto loud = sine(1000.0, 1.0, 4800);
    for (auto &x : loud)
    {
        x *= 3.0e38f;
    }
    const auto m = gg::metrics::computeMetrics({kSampleRate, {loud}});
    INFO("nonFiniteBands " << m.nonFiniteBands);
    REQUIRE(m.nonFiniteBands > 0);
    REQUIRE(m.nonFinite == 0);
    for (double v : m.bandProfileDb)
    {
        REQUIRE(std::isfinite(v));
    }

    const auto result = gg::metrics::compare(m, m);
    REQUIRE_FALSE(result.pass);
    const bool named =
        std::any_of(result.failures.begin(), result.failures.end(),
                    [](const std::string &f) { return f.rfind("nonFiniteBands:", 0) == 0; });
    REQUIRE(named);

    const auto parsed = gg::metrics::metricsFromJson(gg::metrics::toJson(m));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->nonFiniteBands == m.nonFiniteBands);
    REQUIRE(parsed->bandProfileDb == m.bandProfileDb);

    const auto ordinary = gg::metrics::computeMetrics({kSampleRate, {sine(1000.0, 0.5, 4800)}});
    REQUIRE(ordinary.nonFiniteBands == 0);
}

TEST_CASE("non-finite and denormal counting", "[metrics]")
{
    AudioData audio{kSampleRate, {sine(1000.0, 0.5, 4800), sine(500.0, 0.25, 4800)}};
    audio.channels[0][100] = std::numeric_limits<float>::quiet_NaN();
    audio.channels[1][200] = std::numeric_limits<float>::infinity();
    audio.channels[0][300] = std::numeric_limits<float>::denorm_min();
    audio.channels[1][400] = -std::numeric_limits<float>::min() / 2.0f;

    const auto m = gg::metrics::computeMetrics(audio);
    REQUIRE(m.nonFinite == 2);
    REQUIRE(m.denormals == 2);

    // Non-finite samples count as silence everywhere else, so every other metric stays finite.
    REQUIRE(std::isfinite(m.peakDb));
    REQUIRE(std::isfinite(m.stereoCorrelation));
    REQUIRE(std::isfinite(m.clickScore));
    for (double v : m.dc)
    {
        REQUIRE(std::isfinite(v));
    }
    for (double v : m.rmsEnvelopeDb)
    {
        REQUIRE(std::isfinite(v));
    }
    for (double v : m.bandProfileDb)
    {
        REQUIRE(std::isfinite(v));
    }

    const auto result = gg::metrics::compare(m, m);
    REQUIRE_FALSE(result.pass);
}

TEST_CASE("compare passes self and fails on gain change", "[metrics]")
{
    const auto left = sine(330.0, 0.4, 48000);
    auto right = noise(0.2, 48000, 1);
    for (size_t n = 0; n < right.size(); ++n)
    {
        right[n] += left[n];
    }
    const AudioData audio{kSampleRate, {left, right}};
    const auto golden = gg::metrics::computeMetrics(audio);

    const auto self = gg::metrics::compare(golden, gg::metrics::computeMetrics(audio));
    INFO("self failures: " << self.failures.size());
    REQUIRE(self.pass);
    REQUIRE(self.failures.empty());

    AudioData quieter = audio;
    for (auto &channel : quieter.channels)
    {
        for (auto &x : channel)
        {
            x *= 0.8f;
        }
    }
    const auto result = gg::metrics::compare(golden, gg::metrics::computeMetrics(quieter));
    REQUIRE_FALSE(result.pass);
    const bool mentionsEnvelope =
        std::any_of(result.failures.begin(), result.failures.end(), [](const std::string &f) {
            return f.find("rmsEnvelope") != std::string::npos;
        });
    REQUIRE(mentionsEnvelope);
}

TEST_CASE("compare ignores quiet frames and bands", "[metrics]")
{
    const AudioData audio{kSampleRate, {sine(1000.0, 0.5, 48000)}};
    const auto golden = gg::metrics::computeMetrics(audio);

    auto actual = golden;
    actual.rmsEnvelopeDb.back() = -200.0;
    REQUIRE_FALSE(gg::metrics::compare(golden, actual).pass);

    auto quietGolden = golden;
    quietGolden.rmsEnvelopeDb.back() = -61.0;
    actual.rmsEnvelopeDb.back() = -150.0;
    REQUIRE(gg::metrics::compare(quietGolden, actual).pass);

    auto bandGolden = golden;
    auto bandActual = golden;
    bandGolden.bandProfileDb.front() = -91.0;
    bandActual.bandProfileDb.front() = -180.0;
    REQUIRE(gg::metrics::compare(bandGolden, bandActual).pass);

    // Only a band that is quiet in both is ignored: one the golden has at -91 dB that comes back
    // at -50 dB is a difference.
    auto loudActual = golden;
    loudActual.bandProfileDb.front() = -50.0;
    const auto oneSided = gg::metrics::compare(bandGolden, loudActual);
    REQUIRE_FALSE(oneSided.pass);
    REQUIRE(oneSided.failures.size() == 1);
    REQUIRE(oneSided.failures.front().rfind("bandProfile:", 0) == 0);

    // The click limit is absolute, not relative to the golden.
    auto clicky = golden;
    clicky.clickScore = 31.0;
    REQUIRE_FALSE(gg::metrics::compare(clicky, clicky).pass);
}

TEST_CASE("wav round trip is bit exact", "[metrics]")
{
    AudioData audio{44100.0, {noise(0.9, 10000, 2), noise(0.9, 10000, 3)}};
    audio.channels[0][0] = 1.0f;
    audio.channels[0][1] = -1.0f;
    audio.channels[1][2] = -0.0f;
    audio.channels[1][3] = std::numeric_limits<float>::denorm_min();

    const auto path = outputPath("round-trip.wav");
    REQUIRE(gg::metrics::writeWavFloat32(path, audio));

    const auto read = gg::metrics::readAudioFile(path);
    REQUIRE(read.has_value());
    REQUIRE(read->sampleRate == audio.sampleRate);
    REQUIRE(read->channels.size() == audio.channels.size());
    for (size_t c = 0; c < audio.channels.size(); ++c)
    {
        REQUIRE(read->channels[c].size() == audio.channels[c].size());
        bool identical = true;
        for (size_t n = 0; n < audio.channels[c].size(); ++n)
        {
            identical = identical && std::bit_cast<uint32_t>(read->channels[c][n]) ==
                                         std::bit_cast<uint32_t>(audio.channels[c][n]);
        }
        REQUIRE(identical);
    }

    REQUIRE_FALSE(gg::metrics::readAudioFile(outputPath("does-not-exist.wav")).has_value());
}

TEST_CASE("metrics json round trip is exact", "[metrics]")
{
    const AudioData audio{kSampleRate, {noise(0.3, 20000, 4), sine(97.0, 0.7, 20000)}};
    const auto m = gg::metrics::computeMetrics(audio);

    const auto parsed = gg::metrics::metricsFromJson(gg::metrics::toJson(m));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->peakDb == m.peakDb);
    REQUIRE(parsed->dc == m.dc);
    REQUIRE(parsed->rmsEnvelopeDb == m.rmsEnvelopeDb);
    REQUIRE(parsed->bandProfileDb == m.bandProfileDb);
    REQUIRE(parsed->stereoCorrelation == m.stereoCorrelation);
    REQUIRE(parsed->clickScore == m.clickScore);
    REQUIRE(parsed->nonFinite == m.nonFinite);
    REQUIRE(parsed->nonFiniteBands == m.nonFiniteBands);
    REQUIRE(parsed->denormals == m.denormals);

    REQUIRE_FALSE(gg::metrics::metricsFromJson("not json").has_value());
    // One mistyped field in otherwise valid metrics (the current metricsVersion included), so the
    // type check is what rejects it, not the version check.
    auto mistyped = nlohmann::json::parse(gg::metrics::toJson(m));
    REQUIRE(gg::metrics::metricsFromJson(mistyped.dump()).has_value());
    mistyped["peakDb"] = "loud";
    REQUIRE_FALSE(gg::metrics::metricsFromJson(mistyped.dump()).has_value());

    // metricsVersion 2 (the local clickScore) reads neither version 1's files nor a file without
    // nonFiniteBands.
    auto json = nlohmann::json::parse(gg::metrics::toJson(m));
    REQUIRE(json["metricsVersion"] == 2);
    auto older = json;
    older["metricsVersion"] = 1;
    REQUIRE_FALSE(gg::metrics::metricsFromJson(older.dump()).has_value());
    auto missing = json;
    missing.erase("nonFiniteBands");
    REQUIRE_FALSE(gg::metrics::metricsFromJson(missing.dump()).has_value());
}

TEST_CASE("spectrogram png is 1024 tall with one column per frame", "[metrics]")
{
    const AudioData audio{kSampleRate, {sine(1000.0, 0.5, 48000)}};
    const auto path = outputPath("spectrogram.png");
    REQUIRE(gg::metrics::writeSpectrogramPng(path, audio));

    std::ifstream in(path, std::ios::binary);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in), {}};
    REQUIRE(bytes.size() > 24);
    const std::vector<unsigned char> signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    REQUIRE(std::equal(signature.begin(), signature.end(), bytes.begin()));
    const auto be32 = [&](size_t at) {
        return (uint32_t{bytes[at]} << 24) | (uint32_t{bytes[at + 1]} << 16) |
               (uint32_t{bytes[at + 2]} << 8) | uint32_t{bytes[at + 3]};
    };
    // IHDR: width then height, big-endian. Frames of 2048 at hop 512 covering 48000 samples.
    REQUIRE(be32(16) == 91);
    REQUIRE(be32(20) == 1024);
}
