// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The expected frequencies of the four tuned goldens (M3 tuning spec §5.8 and §7). Each key's pitch
// is computed here with libm, from the scale's ratios and the keyboard mapping's reference, and
// never through gg::core's tuning table: a grain's rate in the event log must be that pitch's
// ratio (T5: rate 1 is C4 at A440, 261.6255653005986 Hz), and the 19-EDO render's spectrum must
// peak where the ratio says.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <pffft/pffft.h>

#include "Render.h"
#include "RenderSpec.h"
#include "Signals.h"
#include "gg/metrics/AudioData.h"
#include "support/Utf8.h"

using gg::metrics::AudioData;
using gg::test::utf8;

namespace
{
constexpr double kC4Hz = 261.6255653005986;

/// One of the four cases' tuning, as the test knows it: the scale's tones (degree 0, the 1/1, is
/// implied; the last tone is the period), and the mapping, which `linear` leaves out (degree =
/// key - 60, A4 as 12-TET's C4 times the degree's ratio).
struct Tuned
{
    std::vector<double> tones;
    bool linear{true};
    int mapSize{0};
    int middle{60};
    int refKey{69};
    double refHz{440.0};
    int octaveDegree{0};
    std::vector<int> map; ///< Per key of the map: a degree, or -1 for x.

    /// The ratio of the scale's degree d above degree 0: period^floor(d/N) * tone[d mod N].
    double degreeRatio(int d) const
    {
        const int n = static_cast<int>(tones.size());
        const int octave = static_cast<int>(std::floor(static_cast<double>(d) / n));
        const int within = d - octave * n;
        const double period = tones.back();
        return std::pow(period, octave) *
               (within == 0 ? 1.0 : tones[static_cast<size_t>(within - 1)]);
    }

    /// The degree of `key` in the scale, or nothing for an unmapped one.
    std::optional<int> degreeOf(int key) const
    {
        if (linear)
        {
            return key - middle;
        }
        const int n = key - middle;
        const int octave = static_cast<int>(std::floor(static_cast<double>(n) / mapSize));
        const int idx = n - octave * mapSize;
        const int d = map[static_cast<size_t>(idx)];
        if (d < 0)
        {
            return std::nullopt;
        }
        return octave * octaveDegree + d;
    }

    /// The key's frequency, or nothing if the key is unmapped.
    std::optional<double> hzOf(int key) const
    {
        const auto d = degreeOf(key);
        if (!d)
        {
            return std::nullopt;
        }
        if (linear)
        {
            return kC4Hz * degreeRatio(*d);
        }
        const auto dRef = degreeOf(refKey);
        return refHz * degreeRatio(*d) / degreeRatio(*dRef);
    }

    /// The grain rate the key's pitch gives: its frequency over 12-TET's C4.
    std::optional<double> rateOf(int key) const
    {
        const auto hz = hzOf(key);
        if (!hz)
        {
            return std::nullopt;
        }
        return std::exp2(12.0 * std::log2(*hz / kC4Hz) / 12.0);
    }
};

Tuned nineteenEdo()
{
    Tuned t;
    for (int j = 1; j <= 19; ++j)
    {
        t.tones.push_back(std::exp2(j / 19.0));
    }
    return t;
}

Tuned justTwelve()
{
    Tuned t;
    t.tones = {16.0 / 15, 9.0 / 8, 6.0 / 5, 5.0 / 4, 4.0 / 3,  45.0 / 32,
               3.0 / 2,   8.0 / 5, 5.0 / 3, 9.0 / 5, 15.0 / 8, 2.0};
    t.linear = false;
    t.mapSize = 12;
    t.refKey = 69;
    t.refHz = 432.0;
    t.octaveDegree = 12;
    for (int i = 0; i < 12; ++i)
    {
        t.map.push_back(i);
    }
    return t;
}

Tuned pentatonic()
{
    Tuned t;
    t.tones = {9.0 / 8, 5.0 / 4, 3.0 / 2, 5.0 / 3, 2.0};
    t.linear = false;
    t.mapSize = 12;
    t.refKey = 69;
    t.refHz = 440.0;
    t.octaveDegree = 5;
    t.map = {0, -1, 1, -1, 2, -1, -1, 3, -1, 4, -1, -1};
    return t;
}

Tuned bohlenPierce()
{
    Tuned t;
    for (int j = 1; j <= 13; ++j)
    {
        t.tones.push_back(std::exp2(std::log2(3.0) * j / 13.0));
    }
    t.tones.back() = 3.0;
    return t;
}

std::string mediaDir(const std::string &test)
{
    const auto dir = std::filesystem::path{GG_TEST_OUTPUT_DIR} / ("tuning-frequency-" + test);
    std::filesystem::create_directories(dir);
    std::string error;
    const bool written = gg::render::writeTestMedia(utf8(dir), error);
    INFO(error);
    REQUIRE(written);
    return utf8(dir);
}

gg::render::RenderOptions optionsFor(const std::string &media)
{
    gg::render::RenderOptions options;
    options.mediaDir = media;
    options.contentDir = media;
    return options;
}

gg::render::RenderSpec loadCase(const std::string &name, const std::string &media)
{
    const auto path = std::filesystem::path{GG_GOLDEN_DIR} / "cases" / (name + ".json");
    std::ifstream in(path, std::ios::binary);
    INFO(path.string());
    REQUIRE(in.good());
    const std::string json{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::string error;
    auto spec = gg::render::parseRenderSpec(json, error);
    INFO(name << ": " << error);
    REQUIRE(spec.has_value());
    gg::render::resolvePaths(*spec, media);
    return *spec;
}

/// The number after `"key":` in an event line, or nothing.
std::optional<double> field(const std::string &line, const std::string &key)
{
    const std::string needle = "\"" + key + "\":";
    const auto at = line.find(needle);
    if (at == std::string::npos)
    {
        return std::nullopt;
    }
    return std::strtod(line.c_str() + at + needle.size(), nullptr);
}

/// A note's span in seconds: its On to its Off, the Off moved to the sustain pedal's release when
/// the pedal is down at the Off.
struct Span
{
    int key;
    double start;
    double end;
};

std::vector<Span> spansOf(const std::vector<gg::render::NoteSpec> &notes,
                          const std::vector<gg::render::ControlSpec> &controls)
{
    std::vector<Span> spans;
    for (const auto &n : notes)
    {
        Span s{n.key, n.timeSeconds, n.timeSeconds + n.durationSeconds};
        bool down = false;
        double release = -1.0;
        for (const auto &c : controls)
        {
            if (c.kind != "sustain" || c.timeSeconds > s.end)
            {
                if (c.kind == "sustain" && down && c.value < 0.5)
                {
                    release = c.timeSeconds;
                    break;
                }
                continue;
            }
            down = c.value >= 0.5;
        }
        if (down && release > s.end)
        {
            s.end = release;
        }
        spans.push_back(s);
    }
    return spans;
}

struct Checked
{
    int grains{0};
    int heldSpan{0};
};

/// Renders `name` and checks each grain with onset before `until` seconds: with notes held, its
/// rate is one of theirs (relative 1e-12); with none, one of the last-ended note's (a release
/// tail) or, when `restRate` is set, that rate. `unmappedRate` is the rate every grain of an
/// unmapped key's span must have, exactly.
Checked checkGrains(const std::string &name, const Tuned &tuned, double until,
                    std::optional<double> restRate, std::optional<double> unmappedRate,
                    const std::string &test)
{
    const std::string media = mediaDir(test);
    const auto spec = loadCase(name, media);
    const auto &notes =
        spec.mode == gg::render::RenderMode::Fx ? spec.fx.notes : spec.instrument.notes;
    const auto &controls =
        spec.mode == gg::render::RenderMode::Fx ? spec.fx.controls : spec.instrument.controls;
    const auto spans = spansOf(notes, controls);
    std::vector<std::string> lines;
    (void)gg::render::render(spec, &lines, optionsFor(media));
    Checked out;
    for (const auto &line : lines)
    {
        const auto onset = field(line, "onset");
        const auto rate = field(line, "rate");
        if (!onset || !rate)
        {
            continue;
        }
        const double t = *onset / spec.sampleRate;
        if (t >= until)
        {
            continue;
        }
        CAPTURE(name, line, t);
        // The notes held at the grain's onset, and the ones just released (a voice's release
        // tail, at most half a second).
        std::vector<const Span *> held;
        std::vector<const Span *> released;
        for (const auto &s : spans)
        {
            if (s.start <= t && t < s.end)
            {
                held.push_back(&s);
            }
            else if (s.start <= t && t < s.end + 0.5)
            {
                released.push_back(&s);
            }
        }
        ++out.grains;
        auto matches = [&](const Span *s) {
            const auto r = tuned.rateOf(s->key);
            return r && std::abs(*rate - *r) <= 1e-12 * *r;
        };
        if (held.empty())
        {
            bool ok = restRate && *rate == *restRate;
            for (const Span *s : released)
            {
                ok = ok || matches(s);
            }
            CHECK(ok);
            continue;
        }
        ++out.heldSpan;
        bool ok = false;
        std::vector<const Span *> audible = held;
        audible.insert(audible.end(), released.begin(), released.end());
        for (const Span *s : audible)
        {
            if (!tuned.rateOf(s->key))
            {
                // An unmapped key: silent in Voice and Cloud mode; the FX plays at its own Pitch.
                if (unmappedRate)
                {
                    ok = ok || *rate == *unmappedRate;
                }
            }
            else
            {
                ok = ok || matches(s);
            }
        }
        CHECK(ok);
    }
    return out;
}
} // namespace

TEST_CASE("every tuned grain's rate is its key's ratio", "[golden][tuning]")
{
    SECTION("inst-tuning-19edo")
    {
        const auto c = checkGrains("inst-tuning-19edo", nineteenEdo(), 1e9, std::nullopt,
                                   std::nullopt, "rate-19edo");
        CHECK(c.grains > 50);
        CHECK(c.heldSpan > 50);
    }
    SECTION("inst-voice-just, before the bend")
    {
        const auto c = checkGrains("inst-voice-just", justTwelve(), 5.0, std::nullopt, std::nullopt,
                                   "rate-just");
        CHECK(c.grains > 50);
        CHECK(c.heldSpan > 50);
    }
    SECTION("inst-tuning-bp")
    {
        const auto c = checkGrains("inst-tuning-bp", bohlenPierce(), 1e9, std::nullopt,
                                   std::nullopt, "rate-bp");
        CHECK(c.grains > 50);
        CHECK(c.heldSpan > 50);
    }
    SECTION("fx-tuning-unmapped")
    {
        const Tuned t = pentatonic();
        // The unmapped keys' own pitch would be their interpolated one, never 1: the FX's cloud
        // keeps its Pitch (0, rate exp2Det(0) = 1) there, with no key term (constraint A4).
        const auto c = checkGrains("fx-tuning-unmapped", t, 1e9, 1.0, 1.0, "rate-unmapped");
        CHECK(c.grains > 50);
        // The unmapped spans' grains are at rate 1, and no mapped key's rate is 1.
        for (const int key : {60, 62, 67, 69})
        {
            CAPTURE(key);
            CHECK(*t.rateOf(key) != 1.0);
        }
    }
}

namespace
{
/// The spectral peak (Hz) of `x`: a Hann window, zero-padded to 2^15, parabolic interpolation on
/// the log magnitude.
double peakHz(const std::vector<float> &x, double sampleRate)
{
    constexpr int n = 32768;
    auto *in = static_cast<float *>(pffft_aligned_malloc(sizeof(float) * n));
    auto *out = static_cast<float *>(pffft_aligned_malloc(sizeof(float) * n));
    auto *work = static_cast<float *>(pffft_aligned_malloc(sizeof(float) * n));
    PFFFT_Setup *setup = pffft_new_setup(n, PFFFT_REAL);
    std::fill(in, in + n, 0.0f);
    const auto m = x.size();
    for (std::size_t i = 0; i < m; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (static_cast<double>(i) + 0.5) /
                                              static_cast<double>(m));
        in[i] = static_cast<float>(x[i] * w);
    }
    pffft_transform_ordered(setup, in, out, work, PFFFT_FORWARD);
    std::vector<double> mag(n / 2, 0.0);
    for (int b = 1; b < n / 2; ++b)
    {
        mag[static_cast<size_t>(b)] = std::hypot(out[2 * b], out[2 * b + 1]);
    }
    int best = 1;
    for (int b = 1; b < n / 2; ++b)
    {
        if (mag[static_cast<size_t>(b)] > mag[static_cast<size_t>(best)])
        {
            best = b;
        }
    }
    const double a = std::log(mag[static_cast<size_t>(best - 1)]);
    const double b0 = std::log(mag[static_cast<size_t>(best)]);
    const double c = std::log(mag[static_cast<size_t>(best + 1)]);
    const double delta = 0.5 * (a - c) / (a - 2.0 * b0 + c);
    pffft_destroy_setup(setup);
    pffft_aligned_free(in);
    pffft_aligned_free(out);
    pffft_aligned_free(work);
    return (best + delta) * sampleRate / n;
}
} // namespace

TEST_CASE("nineteen edo sounds at its frequencies", "[golden][tuning]")
{
    const std::string media = mediaDir("peaks");
    const auto spec = loadCase("inst-tuning-19edo", media);
    const Tuned t = nineteenEdo();
    const AudioData audio = gg::render::render(spec, nullptr, optionsFor(media));
    REQUIRE_FALSE(audio.channels.empty());
    // The single notes are the first twenty (keys 60 to 79); the chord after them is not read.
    int checked = 0;
    for (const auto &note : spec.instrument.notes)
    {
        if (note.durationSeconds > 0.35)
        {
            continue;
        }
        const double first = (note.timeSeconds + 0.025) * spec.sampleRate;
        const auto from = static_cast<std::size_t>(std::llround(first));
        const auto len = static_cast<std::size_t>(std::llround(0.25 * spec.sampleRate));
        REQUIRE(from + len <= audio.channels[0].size());
        std::vector<float> mono(len);
        for (std::size_t i = 0; i < len; ++i)
        {
            float sum = 0.0f;
            for (const auto &ch : audio.channels)
            {
                sum += ch[from + i];
            }
            mono[i] = sum / static_cast<float>(audio.channels.size());
        }
        const double expected = 220.0 * *t.rateOf(note.key);
        const double got = peakHz(mono, spec.sampleRate);
        const double cents = 1200.0 * std::log2(got / expected);
        CAPTURE(static_cast<int>(note.key), expected, got, cents);
        CHECK(std::abs(cents) < 1.0);
        ++checked;
    }
    CHECK(checked == 20);
}

namespace
{
const std::string kTwelveEdoScl = [] {
    std::string s = "! 12edo.scl\n!\n12-EDO\n 12\n!\n";
    for (int j = 1; j < 12; ++j)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), " %.12f\n", 100.0 * j);
        s += buf;
    }
    return s + " 2/1\n";
}();
const std::string kA440Kbm = "! a440.kbm\n0\n0\n127\n60\n69\n440.0\n12\n";

bool sameSamples(const AudioData &a, const AudioData &b)
{
    if (a.channels.size() != b.channels.size())
    {
        return false;
    }
    for (std::size_t c = 0; c < a.channels.size(); ++c)
    {
        if (a.channels[c].size() != b.channels[c].size() ||
            std::memcmp(a.channels[c].data(), b.channels[c].data(),
                        a.channels[c].size() * sizeof(float)) != 0)
        {
            return false;
        }
    }
    return true;
}
} // namespace

TEST_CASE("an explicit twelve edo tuning renders bit for bit", "[golden][tuning]")
{
    const std::string media = mediaDir("twelve-edo");
    for (const char *name : {"inst-chord", "fx-note-freeze", "inst-voice-chord"})
    {
        CAPTURE(name);
        const auto plain = loadCase(name, media);
        const AudioData base = gg::render::render(plain, nullptr, optionsFor(media));
        REQUIRE_FALSE(base.channels.empty());
        for (const bool withKbm : {false, true})
        {
            CAPTURE(withKbm);
            auto spec = plain;
            gg::render::TuningSpec ts;
            ts.sclText = kTwelveEdoScl;
            if (withKbm)
            {
                ts.kbmText = kA440Kbm;
            }
            (spec.mode == gg::render::RenderMode::Fx ? spec.fx.tuning : spec.instrument.tuning) =
                ts;
            const AudioData tuned = gg::render::render(spec, nullptr, optionsFor(media));
            CHECK(sameSamples(base, tuned));
        }
    }
}
