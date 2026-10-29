// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The golden click detector against clicks put into the golden renders themselves (metricsVersion
// 2, docs/m1-carry-over.md): each case is rendered in process, as gg-render renders it, and a
// -20 dBFS step is added at sixteen positions in turn. A clean render must score within the
// limit, and each step must score above it, except where a case's own material hides it, which
// the table below pins case by case.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Render.h"
#include "RenderSpec.h"
#include "Signals.h"
#include "gg/metrics/AudioData.h"
#include "gg/metrics/Metrics.h"
#include "gg/metrics/WavIO.h"
#include "support/Utf8.h"

using gg::metrics::AudioData;
using gg::test::utf8;

namespace
{
/// The injected click: a step of 0.1 (-20 dBFS) added to every channel from its frame to the
/// end, so the mono mix steps by 0.1.
constexpr float kStep = 0.1f;
/// Positions per case, at round(frames · i / (kPositions + 1)) for i = 1 … kPositions.
constexpr int kPositions = 16;

/// The test media (gg-render --write-media), in a directory of the calling test's own: the tests
/// run as parallel processes.
std::string mediaDir(const std::string &test)
{
    const auto dir = std::filesystem::path{GG_TEST_OUTPUT_DIR} / ("click-injection-" + test);
    std::filesystem::create_directories(dir);
    std::string error;
    const bool written = gg::render::writeTestMedia(utf8(dir), error);
    INFO(error);
    REQUIRE(written);
    return utf8(dir);
}

/// Every golden case's name, sorted: the same glob the golden CTests use.
std::vector<std::string> goldenCases()
{
    std::vector<std::string> names;
    for (const auto &entry :
         std::filesystem::directory_iterator{std::filesystem::path{GG_GOLDEN_DIR} / "cases"})
    {
        if (entry.path().extension() == ".json")
        {
            names.push_back(utf8(entry.path().stem()));
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

/// The case's spec, parsed, with its file paths resolved against `media`.
gg::render::RenderSpec loadCase(const std::string &name, const std::string &media)
{
    const auto path = std::filesystem::path{GG_GOLDEN_DIR} / "cases" / (name + ".json");
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    const std::string json{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::string error;
    auto spec = gg::render::parseRenderSpec(json, error);
    INFO(name << ": " << error);
    REQUIRE(spec.has_value());
    gg::render::resolvePaths(*spec, media);
    return *spec;
}

AudioData withStep(const AudioData &audio, std::size_t frame)
{
    AudioData out = audio;
    for (auto &channel : out.channels)
    {
        for (std::size_t n = frame; n < channel.size(); ++n)
        {
            channel[n] += kStep;
        }
    }
    return out;
}

double clickScore(const AudioData &audio) { return gg::metrics::computeMetrics(audio).clickScore; }

/// The cases whose own material hides a -20 dBFS step at some positions, with exactly how many
/// of the kPositions steps score above the limit (measured at metricsVersion 2). Every other case
/// must catch all of them. A step moves d by 0.1 at two samples, so it can't stand out where the
/// material's own d is about that large or larger: in white noise at about -23 dBFS RMS, d's RMS
/// is 0.18; in a sine, d grows with the square of its frequency.
///
/// The counts are exact, so neither a lost catch nor a new one passes unnoticed: when a change to
/// a golden's audio or to the metric moves one, update its entry here with the reason, in the
/// same commit.
const std::map<std::string, int> &partlyHidden()
{
    static const std::map<std::string, int> cases{
        // A noise bed throughout (d RMS 0.18, and 0.15 at Eco). Accepted blind spot: row 58,
        // director option A, 2026-10-29.
        {"dense-noise", 0},     {"dense-noise-eco", 0},
        {"fx-automate", 11},    // the swept input's top octaves, from about 2.3 s to 3.1 s
        {"fx-file", 14},        // loop-48k.wav's noise bursts
        {"fx-noise-stereo", 3}, // a noise bed until the input ends at 3 s
        {"fx-note-freeze", 10}, // loop-48k.wav's bursts, live and frozen
        {"fx-space", 6},        // noise and its reverb, until the tail decays
        {"inst-infinite", 11},  // the note's noise bed, until the infinite tail is quiet enough
        {"inst-release", 5},    // the note's noise bed, from 0.25 s to its release
    };
    return cases;
}

/// Renders every golden case in `mode`, checks its clean click score is within the limit, and
/// counts the -20 dBFS steps it catches against partlyHidden().
void checkInjectedSteps(gg::render::RenderMode mode, const std::string &test)
{
    const double limit = gg::metrics::Tolerances{}.maxClickScore;
    const std::string media = mediaDir(test);
    const auto names = goldenCases();
    int rendered = 0;
    for (const auto &name : names)
    {
        const auto spec = loadCase(name, media);
        if (spec.mode != mode)
        {
            continue;
        }
        ++rendered;
        CAPTURE(name);
        const AudioData clean = gg::render::render(spec, nullptr);
        REQUIRE_FALSE(clean.channels.empty());
        const double cleanScore = clickScore(clean);
        CAPTURE(cleanScore);
        CHECK(cleanScore <= limit);

        const std::size_t frames = clean.channels[0].size();
        int caught = 0;
        std::vector<double> scores;
        for (int i = 1; i <= kPositions; ++i)
        {
            const auto frame = static_cast<std::size_t>(std::llround(
                static_cast<double>(frames) * i / static_cast<double>(kPositions + 1)));
            const double score = clickScore(withStep(clean, frame));
            scores.push_back(score);
            caught += score > limit ? 1 : 0;
        }
        CAPTURE(scores);
        const auto hidden = partlyHidden().find(name);
        const int expected = hidden == partlyHidden().end() ? kPositions : hidden->second;
        CAPTURE(expected);
        CHECK(caught == expected);
    }
    CHECK(rendered > 0);
}

/// The two dense-noise cases' blind spot to a full-scale spike (row 58, director option A,
/// 2026-10-29): a single-sample spike of 1.0 added to every channel at the same sixteen
/// positions scores 9.3 to 12.5 in dense-noise and 11.3 to 14.6 in dense-noise-eco, all under the
/// limit of 15. Exactly how many score above it; update with the reason if it changes.
const std::map<std::string, int> &spikesCaught()
{
    static const std::map<std::string, int> cases{
        {"dense-noise", 0},
        {"dense-noise-eco", 0},
    };
    return cases;
}
} // namespace

// One test per render mode, so they run side by side.
TEST_CASE("a -20 dBFS step is caught in the sample-mode golden renders", "[golden][metrics]")
{
    checkInjectedSteps(gg::render::RenderMode::Sample, "sample");
}

TEST_CASE("a -20 dBFS step is caught in the FX golden renders", "[golden][metrics]")
{
    checkInjectedSteps(gg::render::RenderMode::Fx, "fx");
}

TEST_CASE("a -20 dBFS step is caught in the instrument golden renders", "[golden][metrics]")
{
    checkInjectedSteps(gg::render::RenderMode::Instrument, "instrument");
}

TEST_CASE("a 0 dBFS spike in the dense-noise golden renders stays under the limit",
          "[golden][metrics]")
{
    const double limit = gg::metrics::Tolerances{}.maxClickScore;
    const std::string media = mediaDir("spike");
    for (const auto &[name, expected] : spikesCaught())
    {
        CAPTURE(name);
        const AudioData clean = gg::render::render(loadCase(name, media), nullptr);
        REQUIRE_FALSE(clean.channels.empty());
        const std::size_t frames = clean.channels[0].size();
        int caught = 0;
        std::vector<double> scores;
        for (int i = 1; i <= kPositions; ++i)
        {
            const auto frame = static_cast<std::size_t>(std::llround(
                static_cast<double>(frames) * i / static_cast<double>(kPositions + 1)));
            AudioData spiked = clean;
            for (auto &channel : spiked.channels)
            {
                channel[frame] += 1.0f;
            }
            const double score = clickScore(spiked);
            scores.push_back(score);
            caught += score > limit ? 1 : 0;
        }
        CAPTURE(scores);
        CHECK(caught == expected);
    }
}

TEST_CASE("clicks at fx-note-freeze's freeze transitions are caught", "[golden][metrics]")
{
    // loop-48k.wav, the noise-burst medium, scored 26.7 against v1's limit of 30, so a click at a
    // freeze transition couldn't be told from the bursts' onsets (docs/m1-carry-over.md). The
    // medium's bursts start every 0.25 s, and so do all four of the case's transitions. Its
    // freezes engage at 1.0 s and 2.5 s and release at 2.0 s and 3.0 s. A -20 dBFS step at the
    // release at 2.0 s and the freeze at 2.5 s is caught; at 1.0 s and 3.0 s the step lands on
    // a burst's onset at full level, which hides steps smaller than 0.40 (-7.9 dBFS) and 0.71
    // (-3.0 dBFS) there.
    const double limit = gg::metrics::Tolerances{}.maxClickScore;
    const std::string media = mediaDir("freeze");

    const auto loop =
        gg::metrics::readAudioFile(utf8(std::filesystem::path{media} / "loop-48k.wav"));
    REQUIRE(loop.has_value());
    const double loopScore = clickScore(*loop);
    CAPTURE(loopScore);
    CHECK(loopScore <= limit / 2.0);

    const auto spec = loadCase("fx-note-freeze", media);
    const AudioData clean = gg::render::render(spec, nullptr);
    CHECK(clickScore(clean) <= limit);
    for (const double seconds : {2.0, 2.5})
    {
        CAPTURE(seconds);
        const auto frame = static_cast<std::size_t>(std::llround(seconds * spec.sampleRate));
        CHECK(clickScore(withStep(clean, frame)) > limit);
    }
}
