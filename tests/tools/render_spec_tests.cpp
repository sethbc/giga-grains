// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "Render.h"
#include "RenderSpec.h"
#include "Signals.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "gg/grain/Tier.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/SampleBuffer.h"

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using gg::core::Mode;
using gg::core::NoteEvent;
using gg::core::NoteEventType;
using gg::core::ParamEvent;
using gg::core::ParamSlot;
using gg::core::Processor;
using gg::render::AutomationPoint;
using gg::render::NoteSpec;
using gg::render::RenderMode;
using gg::render::RenderSpec;
using gg::render::SourceKind;

namespace
{
std::string readCase(const std::string &name)
{
    const auto path = std::filesystem::path{GG_GOLDEN_DIR} / "cases" / (name + ".json");
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

RenderSpec parseCase(const std::string &name)
{
    std::string error;
    const auto spec = gg::render::parseRenderSpec(readCase(name), error);
    INFO(name << ": " << error);
    REQUIRE(spec.has_value());
    return *spec;
}

/// Parses j, requires the parse to fail, and returns the error message.
std::string rejection(const nlohmann::json &j)
{
    std::string error;
    const auto spec = gg::render::parseRenderSpec(j.dump(), error);
    CHECK_FALSE(spec.has_value());
    CHECK_FALSE(error.empty());
    return error;
}

bool bitIdentical(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

/// An instrument spec of its own (not a golden case's, which may be retuned): a sweep sample and a
/// three-note chord.
nlohmann::json instrumentJson()
{
    return nlohmann::json::parse(R"({
        "mode": "instrument", "sampleRate": 48000, "durationSeconds": 3.5, "blockSize": 128,
        "seed": 1,
        "sample": {"kind": "sweep", "startHz": 50, "endHz": 8000, "seconds": 4.0,
                   "amplitude": 0.5},
        "params": {"Position": 0.5, "Density": 5.0},
        "notes": [{"time": 0.25, "key": 48, "duration": 3.0},
                  {"time": 0.25, "key": 55, "duration": 3.0},
                  {"time": 0.25, "key": 64, "duration": 3.0}]})");
}

/// An FX spec of its own over a file input, with notes.
nlohmann::json fxFileJson()
{
    return nlohmann::json::parse(R"({
        "mode": "fx", "sampleRate": 48000, "durationSeconds": 4.0, "blockSize": 128, "seed": 1,
        "input": {"kind": "file", "path": "loop-48k.wav"},
        "notes": [{"time": 1.0, "key": 60, "duration": 1.0},
                  {"time": 2.5, "key": 67, "duration": 0.5}]})");
}

RenderSpec parseJson(const nlohmann::json &j)
{
    std::string error;
    const auto spec = gg::render::parseRenderSpec(j.dump(), error);
    INFO(error);
    REQUIRE(spec.has_value());
    return *spec;
}

/// `path` as the UTF-8 the spec's paths are in.
std::string utf8(const std::filesystem::path &path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

/// The test media (gg-render --write-media), written into a directory of this test's own under
/// GG_TEST_OUTPUT_DIR; returns the directory, as UTF-8.
std::string writeMedia(const std::string &name)
{
    const auto dir = std::filesystem::path{GG_TEST_OUTPUT_DIR} / name;
    std::filesystem::create_directories(dir);
    std::string error;
    const bool written = gg::render::writeTestMedia(utf8(dir), error);
    INFO(error);
    REQUIRE(written);
    return utf8(dir);
}

/// The grains a direct Processor run spawns, collected in storage reserved up front, since the
/// sink runs inside process().
struct Grains
{
    std::vector<gg::GrainSpec> spawned;
    bool overflowed{false};
    static void record(void *ctx, const gg::GrainSpec &g)
    {
        auto &self = *static_cast<Grains *>(ctx);
        if (self.spawned.size() < self.spawned.capacity())
        {
            self.spawned.push_back(g);
        }
        else
        {
            self.overflowed = true;
        }
    }
    std::vector<std::string> lines() const
    {
        std::vector<std::string> out;
        for (const auto &g : spawned)
        {
            out.push_back(gg::render::formatEventLine(g));
        }
        return out;
    }
};
} // namespace

TEST_CASE("render spec parses all fields", "[render]")
{
    // The golden-case table in the M0 plan, field by field.
    const RenderSpec sparse = parseCase("sparse-sine");
    CHECK(sparse.sampleRate == 48000.0);
    CHECK(sparse.durationSeconds == 3.0);
    CHECK(sparse.blockSize == 128);
    CHECK(sparse.seed == 1);
    CHECK(sparse.source.kind == SourceKind::Sine);
    CHECK(sparse.source.frequencyHz == 220.0);
    CHECK(sparse.source.seconds == 4.0);
    CHECK(sparse.source.amplitude == 0.5);
    // Without source.sampleRate the source is synthesized at the render rate.
    CHECK_FALSE(sparse.source.sampleRate.has_value());
    CHECK(sparse.stream.grainRateHz == 8.0);
    CHECK(sparse.stream.grainDurationMs == 60.0);
    CHECK(sparse.stream.position == 0.5);
    CHECK(sparse.stream.positionJitter == 0.2);
    CHECK(sparse.stream.pitchSemitones == 0.0);
    CHECK(sparse.stream.gain == 0.8);
    // stream.distribution is optional; the M0 cases leave it out, so their onsets are periodic.
    CHECK(sparse.stream.distribution == 0.0);

    // sparse-sine-44k1 is the same case on a 44.1 kHz host, reading a 48 kHz source.
    const RenderSpec sparse44 = parseCase("sparse-sine-44k1");
    CHECK(sparse44.sampleRate == 44100.0);
    REQUIRE(sparse44.source.sampleRate.has_value());
    CHECK(*sparse44.source.sampleRate == 48000.0);
    RenderSpec expected44 = sparse;
    expected44.sampleRate = 44100.0;
    expected44.source.sampleRate = 48000.0;
    CHECK(sparse44 == expected44);

    const RenderSpec noise = parseCase("dense-noise");
    CHECK(noise.sampleRate == 48000.0);
    CHECK(noise.durationSeconds == 3.0);
    CHECK(noise.blockSize == 128);
    CHECK(noise.seed == 1);
    CHECK(noise.source.kind == SourceKind::Noise);
    CHECK(noise.source.seconds == 4.0);
    CHECK(noise.source.amplitude == 0.5);
    CHECK(noise.stream.grainRateHz == 400.0);
    CHECK(noise.stream.grainDurationMs == 50.0);
    CHECK(noise.stream.position == 0.3);
    CHECK(noise.stream.positionJitter == 0.5);
    CHECK(noise.stream.pitchSemitones == 7.0);
    CHECK(noise.stream.gain == 0.1);

    const RenderSpec sweep = parseCase("sweep-stretch");
    CHECK(sweep.sampleRate == 48000.0);
    CHECK(sweep.durationSeconds == 3.0);
    CHECK(sweep.blockSize == 128);
    CHECK(sweep.seed == 1);
    CHECK(sweep.source.kind == SourceKind::Sweep);
    CHECK(sweep.source.startHz == 50.0);
    CHECK(sweep.source.endHz == 8000.0);
    CHECK(sweep.source.seconds == 4.0);
    CHECK(sweep.source.amplitude == 0.5);
    CHECK(sweep.stream.grainRateHz == 30.0);
    CHECK(sweep.stream.grainDurationMs == 200.0);
    CHECK(sweep.stream.position == 0.75);
    CHECK(sweep.stream.positionJitter == 0.01);
    CHECK(sweep.stream.pitchSemitones == -12.0);
    CHECK(sweep.stream.gain == 0.4);
}

TEST_CASE("render spec rejects bad input", "[render]")
{
    const auto good = nlohmann::json::parse(readCase("sparse-sine"));

    SECTION("missing stream")
    {
        auto j = good;
        j.erase("stream");
        CHECK_THAT(rejection(j), ContainsSubstring("stream"));
    }
    SECTION("negative sampleRate")
    {
        auto j = good;
        j["sampleRate"] = -48000;
        CHECK_THAT(rejection(j), ContainsSubstring("sampleRate"));
    }
    SECTION("unknown source kind")
    {
        auto j = good;
        j["source"]["kind"] = "granite";
        CHECK_THAT(rejection(j), ContainsSubstring("kind"));
    }
    SECTION("misspelt key")
    {
        auto j = good;
        j["stream"]["grainDurationSec"] = 0.06;
        CHECK_THAT(rejection(j), ContainsSubstring("grainDurationSec"));
    }
    SECTION("zero block size")
    {
        auto j = good;
        j["blockSize"] = 0;
        CHECK_THAT(rejection(j), ContainsSubstring("blockSize"));
    }
    SECTION("non-positive or NaN source sampleRate")
    {
        // JSON has no NaN: nlohmann writes it as null, which isn't a number either.
        for (const double rate : {0.0, -48000.0, std::numeric_limits<double>::quiet_NaN()})
        {
            CAPTURE(rate);
            auto j = good;
            j["source"]["sampleRate"] = rate;
            CHECK_THAT(rejection(j), ContainsSubstring("source.sampleRate"));
        }
    }
    SECTION("non-numeric distribution")
    {
        auto j = good;
        j["stream"]["distribution"] = "poisson";
        CHECK_THAT(rejection(j), ContainsSubstring("stream.distribution"));
    }
    SECTION("grain longer than the render cap")
    {
        auto j = good;
        j["stream"]["grainDurationMs"] = 3600.0 * 1000.0 + 1.0;
        CHECK_THAT(rejection(j), ContainsSubstring("grainDurationMs"));
    }
    SECTION("not JSON")
    {
        std::string error;
        CHECK_FALSE(gg::render::parseRenderSpec("{\"sampleRate\": 48000,", error).has_value());
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("render spec reads the optional distribution", "[render]")
{
    auto j = nlohmann::json::parse(readCase("sparse-sine"));
    j["stream"]["distribution"] = 0.5;
    std::string error;
    const auto spec = gg::render::parseRenderSpec(j.dump(), error);
    INFO(error);
    REQUIRE(spec.has_value());
    CHECK(spec->stream.distribution == 0.5);
    RenderSpec expected = parseCase("sparse-sine");
    expected.stream.distribution = 0.5;
    CHECK(*spec == expected);
}

TEST_CASE("stream.quality parses and rejects", "[render]")
{
    // Ruling 2: sample mode's optional stream.quality picks the engine's tier.
    using gg::Tier;
    const auto withQuality = [](const nlohmann::json &quality) {
        auto j = nlohmann::json::parse(readCase("sparse-sine"));
        j["stream"]["quality"] = quality;
        return j;
    };
    std::string error;

    const auto eco = gg::render::parseRenderSpec(withQuality("eco").dump(), error);
    INFO(error);
    REQUIRE(eco.has_value());
    CHECK(eco->stream.quality == Tier::Eco);
    RenderSpec expected = parseCase("sparse-sine");
    CHECK(expected.stream.quality == Tier::Normal); // missing: Normal
    expected.stream.quality = Tier::Eco;
    CHECK(*eco == expected);

    const auto normal = gg::render::parseRenderSpec(withQuality("normal").dump(), error);
    REQUIRE(normal.has_value());
    CHECK(normal->stream.quality == Tier::Normal);

    for (const nlohmann::json &bad :
         {nlohmann::json("hq"), nlohmann::json(3), nlohmann::json("Eco"), nlohmann::json(nullptr)})
    {
        INFO(bad.dump());
        CHECK(rejection(withQuality(bad)) == R"(stream.quality must be "eco" or "normal")");
    }

    // The golden case is dense-noise at Eco, and nothing else.
    RenderSpec denseEco = parseCase("dense-noise-eco");
    CHECK(denseEco.stream.quality == Tier::Eco);
    denseEco.stream.quality = Tier::Normal;
    CHECK(denseEco == parseCase("dense-noise"));
}

TEST_CASE("eco golden has the normal golden's events", "[golden]")
{
    // The tier changes how a grain reads, never which grains spawn or what they draw, and the
    // event line doesn't print it: the two expected event logs are the same bytes.
    const auto read = [](const std::string &name) {
        const auto path = std::filesystem::path{GG_GOLDEN_DIR} / "expected" / name;
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    };
    const std::string normal = read("dense-noise.events.jsonl");
    const std::string eco = read("dense-noise-eco.events.jsonl");
    REQUIRE_FALSE(normal.empty());
    CHECK(eco == normal);
}

TEST_CASE("render with Poisson onsets does not depend on block size", "[render]")
{
    // At distribution 1 several grains can start in one sample, so a 1-frame block can spawn
    // more grains than it has frames; gg-render must still log them all.
    RenderSpec spec = parseCase("sparse-sine");
    spec.durationSeconds = 0.5;
    spec.stream.grainRateHz = 4000.0;
    spec.stream.grainDurationMs = 2.0;
    spec.stream.distribution = 1.0;
    spec.blockSize = 4096;
    std::vector<std::string> refEvents;
    const auto ref = gg::render::render(spec, &refEvents);
    REQUIRE(refEvents.size() > 1500);

    spec.blockSize = 1;
    std::vector<std::string> events;
    const auto out = gg::render::render(spec, &events);
    REQUIRE(out.channels.size() == 2);
    CHECK(bitIdentical(out.channels[0], ref.channels[0]));
    CHECK(bitIdentical(out.channels[1], ref.channels[1]));
    CHECK(events == refEvents);

    // Some sample did start more than one grain.
    bool shared = false;
    for (std::size_t i = 1; i < events.size(); ++i)
    {
        shared = shared || nlohmann::json::parse(events[i])["onset"] ==
                               nlohmann::json::parse(events[i - 1])["onset"];
    }
    CHECK(shared);
}

TEST_CASE("render block size does not change output", "[render]")
{
    const std::string name =
        GENERATE(as<std::string>{}, "sparse-sine", "sparse-sine-44k1", "dense-noise",
                 "dense-noise-eco", "sweep-stretch", "fx-sine", "fx-noise-stereo", "fx-automate");
    const uint32_t blockSize = GENERATE(1u, 1000u);
    CAPTURE(name, blockSize);

    RenderSpec spec = parseCase(name);
    std::vector<std::string> refEvents;
    const auto ref = gg::render::render(spec, &refEvents);
    REQUIRE(ref.sampleRate == spec.sampleRate);
    REQUIRE(ref.channels.size() == 2);
    REQUIRE(ref.channels[0].size() >= static_cast<std::size_t>(3.0 * spec.sampleRate));
    REQUIRE_FALSE(refEvents.empty());

    // Same samples, same length (the release tail included), same grains.
    spec.blockSize = blockSize;
    std::vector<std::string> events;
    const auto out = gg::render::render(spec, &events);
    REQUIRE(out.sampleRate == ref.sampleRate);
    REQUIRE(out.channels.size() == 2);
    CHECK(bitIdentical(out.channels[0], ref.channels[0]));
    CHECK(bitIdentical(out.channels[1], ref.channels[1]));
    CHECK(events == refEvents);
}

TEST_CASE("render ends with every grain finished", "[render]")
{
    const std::string name = GENERATE(as<std::string>{}, "sparse-sine", "sparse-sine-44k1",
                                      "dense-noise", "sweep-stretch");
    CAPTURE(name);
    const RenderSpec spec = parseCase(name);
    std::vector<std::string> events;
    const auto out = gg::render::render(spec, &events);
    REQUIRE(out.channels.size() == 2);
    const std::size_t length = out.channels[0].size();
    REQUIRE(out.channels[1].size() == length);

    const auto duration =
        static_cast<std::size_t>(gg::render::framesFor(spec.durationSeconds, spec.sampleRate));
    const auto longestGrain =
        static_cast<std::size_t>(spec.stream.grainDurationMs / 1000.0 * spec.sampleRate);
    CHECK(length >= duration);
    CHECK(length <= duration + longestGrain + spec.blockSize); // the tail is bounded

    // The last frame is silent, so the file never stops in the middle of a grain.
    CHECK(out.channels[0].back() == 0.0f);
    CHECK(out.channels[1].back() == 0.0f);

    if (name == "dense-noise" || name == "sweep-stretch")
    {
        // Their grains overlap the end of the duration: without the tail the render would stop
        // on a sounding sample.
        CHECK(length > duration);
        CHECK(out.channels[0][duration - 1] != 0.0f);
    }
    else
    {
        // The sparse cases' last grain ends at 2.935 s, before the duration does: no tail.
        CHECK(length == duration);
    }

    // The tail spawns no grains: every grain starts within the duration.
    REQUIRE_FALSE(events.empty());
    for (const auto &line : events)
    {
        CHECK(nlohmann::json::parse(line)["onset"].get<int64_t>() < static_cast<int64_t>(duration));
    }
}

TEST_CASE("a 48 kHz source on a 44.1 kHz host keeps its timing and pitch", "[render]")
{
    const RenderSpec spec = parseCase("sparse-sine-44k1");
    std::vector<std::string> events;
    const auto out = gg::render::render(spec, &events);
    REQUIRE(out.sampleRate == 44100.0);
    REQUIRE(events.size() == 24);

    // Timing in seconds: grain k starts at k / 8 s and lasts 60 ms at the host rate.
    const auto grain1 = nlohmann::json::parse(events[1]);
    CHECK(grain1["onset"].get<int64_t>() == 5512);
    CHECK(grain1["frac"].get<double>() == 0.5);
    CHECK(grain1["dur"].get<double>() == 2646.0);
    // The read rate converts the source rate to the host rate.
    CHECK(grain1["rate"].get<double>() == 48000.0 / 44100.0);

    // Pitch: grain 0 (frames 0 .. 2646) still sounds at 220 Hz. Zero crossings are located by
    // linear interpolation; the ends of the window are skipped.
    const auto &x = out.channels[0];
    double first = -1.0;
    double last = -1.0;
    int crossings = 0;
    for (std::size_t n = 20; n < 2626; ++n)
    {
        if ((x[n - 1] < 0.0f) != (x[n] < 0.0f))
        {
            const double t = static_cast<double>(n - 1) + x[n - 1] / (x[n - 1] - x[n]);
            first = first < 0.0 ? t : first;
            last = t;
            ++crossings;
        }
    }
    REQUIRE(crossings > 10);
    const double hz = (crossings - 1) / (2.0 * (last - first) / 44100.0);
    CHECK_THAT(hz, WithinRel(220.0, 0.005));
}

TEST_CASE("event lines use the documented format", "[render]")
{
    const gg::GrainSpec g{
        .index = 3,
        .onsetSample = 18000,
        .onsetFrac = 0.5,
        .startPos = 95999.5,
        .rate = 0.5,
        .durationSamples = 2880.0,
        .gain = 0.8f,
        .pan = 0.0f,
    };
    CHECK(gg::render::formatEventLine(g) ==
          R"({"i":3,"onset":18000,"frac":0.5,"pos":95999.5,"rate":0.5,"dur":2880,)"
          R"("gain":0.8,"pan":0})");

    // Doubles are written shortest-round-trip, so they read back exactly.
    const gg::GrainSpec h{
        .index = uint64_t{1} << 40,
        .onsetSample = int64_t{1} << 40,
        .onsetFrac = 0.1,
        .startPos = 0.1 + 0.2,
        .rate = 1e-7,
        .durationSamples = 16.0,
        .gain = 1.0f / 3.0f,
        .pan = -0.1f,
    };
    const std::string line = gg::render::formatEventLine(h);
    CHECK(line == R"({"i":1099511627776,"onset":1099511627776,"frac":0.1,)"
                  R"("pos":0.30000000000000004,"rate":1e-07,"dur":16,)"
                  R"("gain":0.33333334,"pan":-0.1})");
    const auto parsed = nlohmann::json::parse(line);
    CHECK(parsed["frac"].get<double>() == h.onsetFrac);
    CHECK(parsed["pos"].get<double>() == h.startPos);
    CHECK(parsed["rate"].get<double>() == h.rate);
    // gain and pan are floats, written shortest-round-trip as floats.
    CHECK(parsed["gain"].get<float>() == h.gain);
    CHECK(parsed["pan"].get<float>() == h.pan);
}

TEST_CASE("fx spec parses", "[render]")
{
    // The M0 cases are sample-mode renders.
    const RenderSpec sparse = parseCase("sparse-sine");
    CHECK(sparse.mode == RenderMode::Sample);
    CHECK(sparse.fx == gg::render::FxSpec{});

    // fx-sine: a mono 220 Hz input of 3 s in a 4 s render, every parameter at its default.
    const RenderSpec sine = parseCase("fx-sine");
    CHECK(sine.mode == RenderMode::Fx);
    CHECK(sine.sampleRate == 48000.0);
    CHECK(sine.durationSeconds == 4.0);
    CHECK(sine.blockSize == 128);
    CHECK(sine.seed == 1);
    CHECK(sine.fx.input.kind == SourceKind::Sine);
    CHECK(sine.fx.input.frequencyHz == 220.0);
    CHECK(sine.fx.input.seconds == 3.0);
    CHECK(sine.fx.input.amplitude == 0.5);
    CHECK_FALSE(sine.fx.input.sampleRate.has_value());
    CHECK(sine.fx.channels == 1);
    CHECK(sine.fx.params.empty());
    CHECK(sine.fx.automation.empty());
    // The sample-mode parts keep their defaults.
    CHECK(sine.source == gg::render::SourceSpec{});
    CHECK(sine.stream == gg::render::StreamSpec{});

    // fx-noise-stereo: stereo noise, Stereo 1.0 and Pitch +7. The params are looked up by name,
    // so their order doesn't matter.
    const RenderSpec noise = parseCase("fx-noise-stereo");
    CHECK(noise.mode == RenderMode::Fx);
    CHECK(noise.durationSeconds == 4.0);
    CHECK(noise.fx.input.kind == SourceKind::Noise);
    CHECK(noise.fx.input.seconds == 3.0);
    CHECK(noise.fx.input.amplitude == 0.5);
    CHECK(noise.fx.channels == 2);
    const std::map<std::string, double> params(noise.fx.params.begin(), noise.fx.params.end());
    CHECK(params == std::map<std::string, double>{{"Stereo", 1.0}, {"Pitch", 7.0}});
    CHECK(noise.fx.params.size() == 2);
    CHECK(noise.fx.automation.empty());

    // fx-automate: a sweep, Density to 64 Hz at 1 s, and Freeze on from 1.5 s to 2.5 s.
    const RenderSpec automate = parseCase("fx-automate");
    CHECK(automate.mode == RenderMode::Fx);
    CHECK(automate.fx.input.kind == SourceKind::Sweep);
    CHECK(automate.fx.input.startHz == 50.0);
    CHECK(automate.fx.input.endHz == 8000.0);
    CHECK(automate.fx.input.seconds == 3.0);
    CHECK(automate.fx.channels == 1);
    CHECK(automate.fx.params.empty());
    CHECK(automate.fx.automation == std::vector<AutomationPoint>{{1.0, "Density", 6.0},
                                                                 {1.5, "Freeze", 1.0},
                                                                 {2.5, "Freeze", 0.0}});

    // An explicit "mode": "sample" is the default.
    auto j = nlohmann::json::parse(readCase("sparse-sine"));
    j["mode"] = "sample";
    std::string error;
    const auto explicitSample = gg::render::parseRenderSpec(j.dump(), error);
    INFO(error);
    REQUIRE(explicitSample.has_value());
    CHECK(*explicitSample == sparse);
}

TEST_CASE("fx spec rejects bad input", "[render]")
{
    auto good = nlohmann::json::parse(readCase("fx-automate"));
    good["params"] = {{"Size", -3.0}};
    {
        std::string error;
        const bool parses = gg::render::parseRenderSpec(good.dump(), error).has_value();
        INFO(error);
        REQUIRE(parses);
    }

    SECTION("unknown param name")
    {
        auto j = good;
        j["params"]["Loudness"] = 0.5;
        CHECK_THAT(rejection(j), ContainsSubstring("params.Loudness"));
    }
    SECTION("unknown automation param")
    {
        auto j = good;
        j["automation"][1]["param"] = "freeze"; // the names are case-sensitive
        CHECK_THAT(rejection(j), ContainsSubstring("automation[1].param"));
    }
    SECTION("three channels")
    {
        auto j = good;
        j["channels"] = 3;
        CHECK_THAT(rejection(j), ContainsSubstring("channels"));
    }
    SECTION("negative time")
    {
        auto j = good;
        j["automation"][0]["time"] = -0.5;
        CHECK_THAT(rejection(j), ContainsSubstring("automation[0].time"));
    }
    SECTION("non-numeric param value")
    {
        auto j = good;
        j["params"]["Size"] = "short";
        CHECK_THAT(rejection(j), ContainsSubstring("params.Size"));
    }
    SECTION("non-numeric automation value")
    {
        auto j = good;
        j["automation"][2]["value"] = "off";
        CHECK_THAT(rejection(j), ContainsSubstring("automation[2].value"));
    }
    SECTION("value outside the parameter's range")
    {
        // Density is log2 Hz: 64 means 2^64 Hz, not 64 Hz.
        auto j = good;
        j["params"]["Density"] = 64.0;
        CHECK_THAT(rejection(j), ContainsSubstring("params.Density"));
        j = good;
        j["automation"][0]["value"] = 64.0;
        CHECK_THAT(rejection(j), ContainsSubstring("automation[0].value"));
    }
    SECTION("automation out of time order")
    {
        auto j = good;
        j["automation"][2]["time"] = 1.2;
        CHECK_THAT(rejection(j), ContainsSubstring("automation[2].time"));
    }
    SECTION("automation at or after the end of the render")
    {
        auto j = good;
        j["automation"][2]["time"] = 4.0;
        CHECK_THAT(rejection(j), ContainsSubstring("automation[2].time"));
    }
    SECTION("misspelt automation key")
    {
        auto j = good;
        j["automation"][0]["seconds"] = 1.0;
        CHECK_THAT(rejection(j), ContainsSubstring("automation[0].seconds"));
    }
    SECTION("missing input")
    {
        auto j = good;
        j.erase("input");
        CHECK_THAT(rejection(j), ContainsSubstring("input"));
    }
    SECTION("file input without a path")
    {
        auto j = good;
        j["input"] = {{"kind", "file"}};
        CHECK_THAT(rejection(j), ContainsSubstring("input.path"));
        j["input"]["path"] = "";
        CHECK_THAT(rejection(j), ContainsSubstring("input.path"));
    }
    SECTION("channels with a file input")
    {
        auto j = fxFileJson();
        j["channels"] = 1;
        CHECK(rejection(j) ==
              "channels must be left out with a file input: the input has the file's own channels");
    }
    SECTION("unknown input kind")
    {
        auto j = good;
        j["input"]["kind"] = "granite";
        CHECK_THAT(rejection(j), ContainsSubstring("input.kind"));
    }
    SECTION("input with its own sample rate")
    {
        auto j = good;
        j["input"]["sampleRate"] = 44100;
        CHECK_THAT(rejection(j), ContainsSubstring("input.sampleRate"));
    }
    SECTION("unknown mode")
    {
        auto j = good;
        j["mode"] = "effect";
        CHECK_THAT(rejection(j), ContainsSubstring("mode"));
    }
    SECTION("sample-mode key in an fx spec")
    {
        auto j = good;
        j["stream"] = nlohmann::json::parse(readCase("sparse-sine"))["stream"];
        CHECK_THAT(rejection(j), ContainsSubstring("stream"));
    }
    SECTION("fx key in a sample-mode spec")
    {
        auto j = nlohmann::json::parse(readCase("sparse-sine"));
        j["channels"] = 1;
        CHECK_THAT(rejection(j), ContainsSubstring("channels"));
    }
}

TEST_CASE("fx render matches the processor", "[render]")
{
    // A short FX render in blocks of 100 frames, which the 32-sample grid doesn't divide, so
    // boundaries fall inside blocks and at their ends. One automation point is on a block's
    // first frame off the grid (5000), one on a block's first frame on the grid (12000), and
    // two more fall inside that block (12010 and 12070).
    RenderSpec spec;
    spec.mode = RenderMode::Fx;
    spec.sampleRate = 48000.0;
    spec.durationSeconds = 0.5;
    spec.blockSize = 100;
    spec.seed = 3;
    spec.fx.input = {.kind = SourceKind::Noise, .seconds = 0.4, .amplitude = 0.5};
    spec.fx.channels = 2;
    spec.fx.params = {{"Density", 7.0}, {"Size", -4.0}, {"Mix", 0.75}, {"Position", 0.1}};
    spec.fx.automation = {
        {5000.0 / 48000.0, "Pitch", 5.0},
        {12000.0 / 48000.0, "Freeze", 1.0},
        {12010.0 / 48000.0, "Level", -6.0},
        {12070.0 / 48000.0, "Mix", 0.25},
        {0.3, "Freeze", 0.0},
    };
    std::vector<std::string> events;
    const auto rendered = gg::render::render(spec, &events);

    // The same render straight through a Processor, in one call: its offsets are the frames.
    constexpr std::size_t frames = 24000;
    auto input = gg::render::makeInput(spec.fx.input, 2, spec.sampleRate, spec.seed);
    REQUIRE(input.channels.size() == 2);
    REQUIRE(input.channels[0].size() == 19200);
    for (auto &channel : input.channels)
    {
        channel.resize(frames); // silent after the input's 0.4 s
    }
    Processor processor({spec.sampleRate, 4096, spec.seed, Mode::Fx});
    processor.setBase(ParamSlot::Density, 7.0f);
    processor.setBase(ParamSlot::Size, -4.0f);
    processor.setBase(ParamSlot::Mix, 0.75f);
    processor.setBase(ParamSlot::Position, 0.1f);
    const std::vector<ParamEvent> automation{
        {5000, ParamSlot::Pitch, false, 5.0},   {12000, ParamSlot::Freeze, false, 1.0},
        {12010, ParamSlot::Level, false, -6.0}, {12070, ParamSlot::Mix, false, 0.25},
        {14400, ParamSlot::Freeze, false, 0.0},
    };
    // The sink runs inside process(), so it only fills storage reserved up front.
    struct Grains
    {
        std::vector<gg::GrainSpec> spawned;
        bool overflowed{false};
        static void record(void *ctx, const gg::GrainSpec &g)
        {
            auto &self = *static_cast<Grains *>(ctx);
            if (self.spawned.size() < self.spawned.capacity())
            {
                self.spawned.push_back(g);
            }
            else
            {
                self.overflowed = true;
            }
        }
    } grains;
    grains.spawned.reserve(4096);
    processor.setEventSink(&Grains::record, &grains);
    std::vector<float> left(frames);
    std::vector<float> right(frames);
    const float *const in[2] = {input.channels[0].data(), input.channels[1].data()};
    float *const out[2] = {left.data(), right.data()};
    processor.process(in, 2, out, 2, frames, automation);

    REQUIRE_FALSE(grains.overflowed);
    REQUIRE(grains.spawned.size() > 30);
    std::vector<std::string> expectedEvents;
    for (const auto &g : grains.spawned)
    {
        expectedEvents.push_back(gg::render::formatEventLine(g));
    }

    // Exactly durationSeconds, stereo, with no release tail.
    REQUIRE(rendered.sampleRate == spec.sampleRate);
    REQUIRE(rendered.channels.size() == 2);
    CHECK(bitIdentical(rendered.channels[0], left));
    CHECK(bitIdentical(rendered.channels[1], right));
    CHECK(events == expectedEvents);

    // Guard against a vacuous pass: the automation changed the output. Pitch +5 changed the
    // grains' rate from frame 5024, Level and Mix changed the audio.
    RenderSpec still = spec;
    still.fx.automation.clear();
    std::vector<std::string> stillEvents;
    const auto stillRendered = gg::render::render(still, &stillEvents);
    CHECK_FALSE(stillEvents == events);
    CHECK_FALSE(bitIdentical(stillRendered.channels[0], rendered.channels[0]));
}

TEST_CASE("instrument and note specs parse", "[render]")
{
    const RenderSpec chord = parseJson(instrumentJson());
    CHECK(chord.mode == RenderMode::Instrument);
    CHECK(chord.sampleRate == 48000.0);
    CHECK(chord.durationSeconds == 3.5);
    CHECK(chord.blockSize == 128);
    CHECK(chord.seed == 1);
    CHECK(chord.instrument.sample.kind == SourceKind::Sweep);
    CHECK(chord.instrument.sample.startHz == 50.0);
    CHECK(chord.instrument.sample.endHz == 8000.0);
    CHECK(chord.instrument.sample.seconds == 4.0);
    CHECK(chord.instrument.sample.amplitude == 0.5);
    CHECK_FALSE(chord.instrument.sample.sampleRate.has_value());
    const std::map<std::string, double> params(chord.instrument.params.begin(),
                                               chord.instrument.params.end());
    CHECK(params == std::map<std::string, double>{{"Position", 0.5}, {"Density", 5.0}});
    CHECK(chord.instrument.automation.empty());
    CHECK(chord.instrument.notes ==
          std::vector<NoteSpec>{{0.25, 48, 3.0, 0}, {0.25, 55, 3.0, 0}, {0.25, 64, 3.0, 0}});
    // The other modes' parts keep their defaults.
    CHECK(chord.fx == gg::render::FxSpec{});
    CHECK(chord.source == gg::render::SourceSpec{});
    CHECK(parseCase("fx-sine").instrument == gg::render::InstrumentSpec{});

    // A file sample; the parser leaves its path as written.
    auto j = instrumentJson();
    j["sample"] = {{"kind", "file"}, {"path", "tone-44k1.wav"}};
    const RenderSpec file = parseJson(j);
    CHECK(file.instrument.sample.kind == SourceKind::File);
    CHECK(file.instrument.sample.path == "tone-44k1.wav");

    // Notes in FX mode, over a file input without "channels".
    const RenderSpec fx = parseJson(fxFileJson());
    CHECK(fx.mode == RenderMode::Fx);
    CHECK(fx.fx.input.kind == SourceKind::File);
    CHECK(fx.fx.input.path == "loop-48k.wav");
    CHECK(fx.fx.notes == std::vector<NoteSpec>{{1.0, 60, 1.0, 0}, {2.5, 67, 0.5, 0}});
    CHECK(fx.instrument == gg::render::InstrumentSpec{});

    // "channel" is optional, 0 by default; automation as in FX mode.
    j = instrumentJson();
    j["notes"][0]["channel"] = 15;
    j["automation"] = {{{"time", 1.0}, {"param", "Pitch"}, {"value", -12.0}}};
    const RenderSpec channel = parseJson(j);
    CHECK(channel.instrument.notes ==
          std::vector<NoteSpec>{{0.25, 48, 3.0, 15}, {0.25, 55, 3.0, 0}, {0.25, 64, 3.0, 0}});
    CHECK(channel.instrument.automation == std::vector<AutomationPoint>{{1.0, "Pitch", -12.0}});
}

TEST_CASE("note specs reject bad input", "[render]")
{
    const auto good = instrumentJson();

    SECTION("key 128")
    {
        auto j = good;
        j["notes"][1]["key"] = 128;
        CHECK(rejection(j) == "notes[1].key must be an integer from 0 to 127");
        j["notes"][1]["key"] = -1;
        CHECK(rejection(j) == "notes[1].key must be an integer from 0 to 127");
        j["notes"][1]["key"] = 60.5;
        CHECK(rejection(j) == "notes[1].key must be an integer from 0 to 127");
    }
    SECTION("negative or zero duration")
    {
        auto j = good;
        j["notes"][0]["duration"] = -0.5;
        CHECK_THAT(rejection(j), ContainsSubstring("notes[0].duration must be > 0"));
        j["notes"][0]["duration"] = 0.0;
        CHECK_THAT(rejection(j), ContainsSubstring("notes[0].duration must be > 0"));
    }
    SECTION("a duration that rounds to no frames")
    {
        // Ruling R17: its Off would fall on its On's frame, so the key would stay held.
        auto j = good;
        j["notes"][1]["duration"] = 0.4 / 48000.0;
        CHECK(rejection(j) == "notes[1].duration is shorter than one frame at the render's rate: "
                              "the note would end on the frame it starts");
        j["notes"][1]["duration"] = 1.0 / 48000.0; // one frame is enough
        std::string error;
        const bool parses = gg::render::parseRenderSpec(j.dump(), error).has_value();
        INFO(error);
        CHECK(parses);
    }
    SECTION("channel 16")
    {
        auto j = good;
        j["notes"][2]["channel"] = 16;
        CHECK(rejection(j) == "notes[2].channel must be an integer from 0 to 15");
    }
    SECTION("unsorted notes")
    {
        auto j = good;
        j["notes"][2]["time"] = 0.1;
        CHECK(rejection(j) ==
              "notes[2].time is before notes[1].time: list the notes in time order");
    }
    SECTION("a note at or after the end")
    {
        auto j = good;
        j["notes"][2]["time"] = 3.5;
        CHECK_THAT(rejection(j), ContainsSubstring("notes[2].time must be >= 0"));
        j["notes"][2]["time"] = -0.25;
        CHECK_THAT(rejection(j), ContainsSubstring("notes[2].time must be >= 0"));
    }
    SECTION("missing or misspelt keys")
    {
        auto j = good;
        j["notes"][0].erase("key");
        CHECK(rejection(j) == "notes[0].key is missing");
        j = good;
        j["notes"][0]["velocity"] = 100;
        CHECK(rejection(j) == "unknown key notes[0].velocity");
        j = good;
        j["notes"] = {{"time", 0.0}};
        CHECK(rejection(j) == "notes must be a JSON array");
    }
    SECTION("notes in sample mode")
    {
        auto j = nlohmann::json::parse(readCase("sparse-sine"));
        j["notes"] = good["notes"];
        CHECK(rejection(j) == "unknown key notes");
    }
}

TEST_CASE("instrument spec rejects bad input", "[render]")
{
    const auto good = instrumentJson();

    SECTION("channels in instrument mode")
    {
        auto j = good;
        j["channels"] = 1;
        CHECK(rejection(j) == "unknown key channels");
    }
    SECTION("missing sample")
    {
        auto j = good;
        j.erase("sample");
        CHECK(rejection(j) == "sample is missing");
    }
    SECTION("an input or a source")
    {
        auto j = good;
        j["input"] = good["sample"];
        CHECK(rejection(j) == "unknown key input");
        j = good;
        j["source"] = good["sample"];
        CHECK(rejection(j) == "unknown key source");
    }
    SECTION("a sample with a rate of its own")
    {
        auto j = good;
        j["sample"]["sampleRate"] = 44100;
        CHECK(rejection(j) == "unknown key sample.sampleRate");
    }
    SECTION("a file sample without a path")
    {
        auto j = good;
        j["sample"] = {{"kind", "file"}, {"path", ""}};
        CHECK(rejection(j) == "sample.path must not be empty");
    }
    SECTION("unknown sample kind")
    {
        auto j = good;
        j["sample"]["kind"] = "granite";
        CHECK(rejection(j) ==
              R"(sample.kind must be "sine", "sweep", "noise" or "file", not "granite")");
    }
    SECTION("a value outside the instrument's range")
    {
        auto j = good;
        j["params"]["Position"] = 1.5;
        CHECK_THAT(rejection(j), ContainsSubstring("params.Position"));
    }
    SECTION("unknown mode")
    {
        auto j = good;
        j["mode"] = "synth";
        CHECK(rejection(j) == R"(mode must be "sample", "instrument" or "fx", not "synth")");
    }
}

TEST_CASE("a render shorter than one frame is bad input", "[render]")
{
    // [181]: 0.4 ms at 1 kHz rounds to 0 frames.
    for (const auto &spec : {nlohmann::json::parse(readCase("sparse-sine")),
                             nlohmann::json::parse(readCase("fx-sine")), instrumentJson()})
    {
        const std::string mode = spec.value("mode", "sample");
        CAPTURE(mode);
        auto j = spec;
        j["sampleRate"] = 1000;
        j["durationSeconds"] = 0.0004;
        j.erase("automation");
        j.erase("notes");
        CHECK(rejection(j) == "durationSeconds must round to at least one frame at sampleRate");
        j["durationSeconds"] = 0.0005; // rounds half up, to 1 frame
        std::string error;
        const bool parses = gg::render::parseRenderSpec(j.dump(), error).has_value();
        INFO(error);
        CHECK(parses);
    }
}

TEST_CASE("automation and notes past the render's end are rejected with a clear message",
          "[render]")
{
    // T8 (208).
    auto j = nlohmann::json::parse(readCase("fx-automate"));
    j["automation"][2]["time"] = 4.0;
    CHECK(rejection(j) ==
          "automation[2].time must be >= 0 and round to a frame before the render's end "
          "(durationSeconds)");
    j = instrumentJson();
    j["notes"][0]["time"] = 3.5;
    CHECK(rejection(j) == "notes[0].time must be >= 0 and round to a frame before the render's "
                          "end (durationSeconds)");
}

TEST_CASE("relative paths resolve against the media directory", "[render]")
{
    // gg-render's --media-dir: a relative path is taken from that directory instead of the
    // spec's; an absolute one is kept. Paths are UTF-8.
    const std::filesystem::path base = std::filesystem::path{GG_TEST_OUTPUT_DIR} / "media dir";
    const std::filesystem::path absolute =
        std::filesystem::absolute(std::filesystem::path{GG_TEST_OUTPUT_DIR} / "elsewhere.wav");

    auto withFile = instrumentJson();
    withFile["sample"] = {{"kind", "file"}, {"path", "tone-44k1.wav"}};
    RenderSpec instrument = parseJson(withFile);
    gg::render::resolvePaths(instrument, utf8(base));
    CHECK(instrument.instrument.sample.path == utf8(base / "tone-44k1.wav"));

    RenderSpec fx = parseJson(fxFileJson());
    gg::render::resolvePaths(fx, utf8(base));
    CHECK(fx.fx.input.path == utf8(base / "loop-48k.wav"));

    RenderSpec sample = parseCase("sparse-sine");
    sample.source = {.kind = SourceKind::File, .path = "sub/\xc3\xa9.wav"};
    gg::render::resolvePaths(sample, utf8(base));
    CHECK(sample.source.path == utf8(base / std::filesystem::path(u8"sub/é.wav")));

    fx.fx.input.path = utf8(absolute);
    gg::render::resolvePaths(fx, utf8(base));
    CHECK(fx.fx.input.path == utf8(absolute));

    // A built-in signal has no path to resolve.
    RenderSpec builtIn = parseJson(instrumentJson());
    const RenderSpec before = builtIn;
    gg::render::resolvePaths(builtIn, utf8(base));
    CHECK(builtIn == before);
}

TEST_CASE("mono fx render with notes matches the processor", "[render]")
{
    // T8 (207): the mono path, one input channel, checked against a direct Processor run, with
    // automation and a note that freezes the ring and repitches it.
    RenderSpec spec;
    spec.mode = RenderMode::Fx;
    spec.sampleRate = 48000.0;
    spec.durationSeconds = 0.5;
    spec.blockSize = 100;
    spec.seed = 3;
    spec.fx.input = {.kind = SourceKind::Noise, .seconds = 0.4, .amplitude = 0.5};
    spec.fx.channels = 1;
    spec.fx.params = {{"Density", 7.0}, {"Size", -4.0}, {"Mix", 1.0}, {"Position", 0.1}};
    spec.fx.automation = {{5000.0 / 48000.0, "Pitch", 5.0}};
    spec.fx.notes = {{9000.0 / 48000.0, 64, 6000.0 / 48000.0, 0}};
    std::vector<std::string> events;
    const auto rendered = gg::render::render(spec, &events);

    constexpr std::size_t frames = 24000;
    auto input = gg::render::makeInput(spec.fx.input, 1, spec.sampleRate, spec.seed);
    REQUIRE(input.channels.size() == 1);
    input.channels[0].resize(frames);
    Processor processor({spec.sampleRate, 4096, spec.seed, Mode::Fx});
    processor.setBase(ParamSlot::Density, 7.0f);
    processor.setBase(ParamSlot::Size, -4.0f);
    processor.setBase(ParamSlot::Mix, 1.0f);
    processor.setBase(ParamSlot::Position, 0.1f);
    const std::vector<ParamEvent> automation{{5000, ParamSlot::Pitch, false, 5.0}};
    const std::vector<NoteEvent> notes{{9000, NoteEventType::On, 0, 64},
                                       {15000, NoteEventType::Off, 0, 64}};
    Grains grains;
    grains.spawned.reserve(4096);
    processor.setEventSink(&Grains::record, &grains);
    std::vector<float> left(frames);
    std::vector<float> right(frames);
    const float *const in[1] = {input.channels[0].data()};
    float *const out[2] = {left.data(), right.data()};
    processor.process(in, 1, out, 2, frames, automation, notes);

    REQUIRE_FALSE(grains.overflowed);
    REQUIRE(grains.spawned.size() > 30);
    REQUIRE(rendered.channels.size() == 2);
    CHECK(bitIdentical(rendered.channels[0], left));
    CHECK(bitIdentical(rendered.channels[1], right));
    CHECK(events == grains.lines());

    // Not vacuous: the note repitched the grains it spawned (key 64 is +4 semitones).
    RenderSpec noNotes = spec;
    noNotes.fx.notes.clear();
    std::vector<std::string> noNoteEvents;
    gg::render::render(noNotes, &noNoteEvents);
    CHECK_FALSE(noNoteEvents == events);
}

TEST_CASE("instrument render matches the processor", "[render]")
{
    // Blocks of 100 frames, so grid boundaries fall inside blocks. The notes: a key retriggered
    // on the frame its first note ends (the Off goes first, so it stays held), two channels, an
    // Off and an On on one frame for different keys, and a note that outlasts the render (its
    // Off is never sent).
    RenderSpec spec;
    spec.mode = RenderMode::Instrument;
    spec.sampleRate = 48000.0;
    spec.durationSeconds = 0.5;
    spec.blockSize = 100;
    spec.seed = 5;
    spec.instrument.sample = {.kind = SourceKind::Sweep,
                              .startHz = 100.0,
                              .endHz = 4000.0,
                              .seconds = 1.0,
                              .amplitude = 0.5};
    spec.instrument.params = {{"Density", 7.0}, {"Size", -4.0}, {"Position", 0.3}};
    spec.instrument.automation = {{6000.0 / 48000.0, "Pitch", 3.0}};
    const auto at = [](double frame) { return frame / 48000.0; };
    spec.instrument.notes = {
        {at(1000), 60, at(4000), 0},
        {at(2000), 67, at(10000), 1},
        {at(5000), 60, at(3000), 0},
        {at(12000), 72, 1.0, 0},
    };
    std::vector<std::string> events;
    const auto rendered = gg::render::render(spec, &events);

    // The same render straight through a Processor in one call, with the sample built here.
    constexpr std::size_t frames = 24000;
    const auto signal = gg::render::makeSignal(spec.instrument.sample, spec.sampleRate, spec.seed);
    auto buffer = std::make_shared<gg::SampleBuffer>(
        1, static_cast<int64_t>(signal.channels[0].size()), spec.sampleRate);
    std::copy(signal.channels[0].begin(), signal.channels[0].end(), buffer->channel(0));
    Processor processor({spec.sampleRate, 4096, spec.seed, Mode::Instrument});
    processor.setSourceNow(gg::PreparedSource::make(buffer));
    processor.setBase(ParamSlot::Density, 7.0f);
    processor.setBase(ParamSlot::Size, -4.0f);
    processor.setBase(ParamSlot::Position, 0.3f);
    const std::vector<ParamEvent> automation{{6000, ParamSlot::Pitch, false, 3.0}};
    const std::vector<NoteEvent> notes{
        {1000, NoteEventType::On, 0, 60},  {2000, NoteEventType::On, 1, 67},
        {5000, NoteEventType::Off, 0, 60}, {5000, NoteEventType::On, 0, 60},
        {8000, NoteEventType::Off, 0, 60}, {12000, NoteEventType::Off, 1, 67},
        {12000, NoteEventType::On, 0, 72},
    };
    Grains grains;
    grains.spawned.reserve(4096);
    processor.setEventSink(&Grains::record, &grains);
    std::vector<float> left(frames);
    std::vector<float> right(frames);
    float *const out[2] = {left.data(), right.data()};
    processor.process(nullptr, 0, out, 2, frames, automation, notes);

    REQUIRE_FALSE(grains.overflowed);
    REQUIRE(grains.spawned.size() > 30);
    REQUIRE(rendered.sampleRate == spec.sampleRate);
    REQUIRE(rendered.channels.size() == 2);
    CHECK(bitIdentical(rendered.channels[0], left));
    CHECK(bitIdentical(rendered.channels[1], right));
    CHECK(events == grains.lines());
    // Key 72 is still held at the end: grains are sounding.
    CHECK(left.back() != 0.0f);

    // Not vacuous: without notes the instrument is silent and spawns nothing.
    RenderSpec silent = spec;
    silent.instrument.notes.clear();
    std::vector<std::string> silentEvents;
    const auto quiet = gg::render::render(silent, &silentEvents);
    CHECK(silentEvents.empty());
    CHECK(std::all_of(quiet.channels[0].begin(), quiet.channels[0].end(),
                      [](float x) { return x == 0.0f; }));
}

TEST_CASE("an unreadable sample or input is bad input", "[render]")
{
    auto withFile = instrumentJson();
    withFile["sample"] = {{"kind", "file"}, {"path", "tone-44k1.wav"}};
    RenderSpec instrument = parseJson(withFile);
    gg::render::resolvePaths(instrument, utf8(std::filesystem::path{GG_TEST_OUTPUT_DIR} / "none"));
    CHECK_THROWS_AS(gg::render::render(instrument, nullptr), gg::render::BadInput);
    RenderSpec fx = parseJson(fxFileJson());
    gg::render::resolvePaths(fx, utf8(std::filesystem::path{GG_TEST_OUTPUT_DIR} / "none"));
    CHECK_THROWS_AS(gg::render::render(fx, nullptr), gg::render::BadInput);
}

// The golden note and file cases (tests/golden/cases/).

TEST_CASE("the golden note and file cases parse", "[render]")
{
    // inst-note: a 4 s sweep sample, one note.
    const RenderSpec note = parseCase("inst-note");
    CHECK(note.mode == RenderMode::Instrument);
    CHECK(note.sampleRate == 48000.0);
    CHECK(note.durationSeconds == 3.0);
    CHECK(note.blockSize == 128);
    CHECK(note.seed == 1);
    CHECK(note.instrument.sample.kind == SourceKind::Sweep);
    const std::map<std::string, double> params(note.instrument.params.begin(),
                                               note.instrument.params.end());
    CHECK(params ==
          std::map<std::string, double>{{"Position", 0.5}, {"Density", 5.0}, {"Level", -6.0}});
    CHECK(note.instrument.notes == std::vector<NoteSpec>{{0.25, 60, 2.0, 0}});

    // inst-chord: three notes together.
    CHECK(parseCase("inst-chord").instrument.notes ==
          std::vector<NoteSpec>{{0.25, 48, 3.0, 0}, {0.25, 55, 3.0, 0}, {0.25, 64, 3.0, 0}});

    // inst-file-44k1: a file sample; the parser leaves its path as written.
    const RenderSpec file = parseCase("inst-file-44k1");
    CHECK(file.instrument.sample.kind == SourceKind::File);
    CHECK(file.instrument.sample.path == "tone-44k1.wav");
    CHECK(file.instrument.notes == std::vector<NoteSpec>{{0.25, 60, 1.0, 0}, {1.5, 72, 1.0, 0}});

    // fx-note-freeze: notes in FX mode, over a file input without "channels".
    const RenderSpec freeze = parseCase("fx-note-freeze");
    CHECK(freeze.mode == RenderMode::Fx);
    CHECK(freeze.fx.input.kind == SourceKind::File);
    CHECK(freeze.fx.input.path == "loop-48k.wav");
    CHECK(freeze.fx.notes == std::vector<NoteSpec>{{1.0, 60, 1.0, 0}, {2.5, 67, 0.5, 0}});
    CHECK(parseCase("fx-file").fx.notes.empty());
}

TEST_CASE("note and file renders do not depend on block size", "[render]")
{
    const std::string name = GENERATE(as<std::string>{}, "inst-note", "inst-chord", "inst-release",
                                      "inst-file-44k1", "fx-note-freeze", "fx-file");
    const uint32_t blockSize = GENERATE(1u, 1000u);
    CAPTURE(name, blockSize);

    RenderSpec spec = parseCase(name);
    gg::render::resolvePaths(spec, writeMedia("render-block-size-media"));
    std::vector<std::string> refEvents;
    const auto ref = gg::render::render(spec, &refEvents);
    REQUIRE(ref.channels.size() == 2);
    REQUIRE(ref.channels[0].size() ==
            static_cast<std::size_t>(gg::render::framesFor(spec.durationSeconds, spec.sampleRate)));
    REQUIRE_FALSE(refEvents.empty());

    spec.blockSize = blockSize;
    std::vector<std::string> events;
    const auto out = gg::render::render(spec, &events);
    REQUIRE(out.channels.size() == 2);
    CHECK(bitIdentical(out.channels[0], ref.channels[0]));
    CHECK(bitIdentical(out.channels[1], ref.channels[1]));
    CHECK(events == refEvents);
}

TEST_CASE("a chord's grains take its three keys, and its release decays to silence", "[render]")
{
    // inst-chord: keys 48, 55 and 64 over a sample at the render's rate, so each grain's read
    // rate is 2^((key - 60) / 12).
    std::vector<std::string> events;
    gg::render::render(parseCase("inst-chord"), &events);
    std::set<double> rates;
    for (const auto &line : events)
    {
        rates.insert(nlohmann::json::parse(line)["rate"].get<double>());
    }
    REQUIRE(rates.size() == 3);
    auto rate = rates.begin();
    CHECK_THAT(*rate++, WithinRel(0.5, 1e-12));
    CHECK_THAT(*rate++, WithinRel(std::exp2(-5.0 / 12.0), 1e-12));
    CHECK_THAT(*rate, WithinRel(std::exp2(4.0 / 12.0), 1e-12));

    // inst-release: key 60 from 0.25 s to 1.25 s with 400 ms grains. Grains sound after the
    // note-off, and the render ends in silence.
    const RenderSpec release = parseCase("inst-release");
    const auto out = gg::render::render(release, &events);
    const auto at = [](double seconds) { return static_cast<std::size_t>(seconds * 48000.0); };
    const auto loudest = [&](double from, double to) {
        float p = 0.0f;
        for (std::size_t n = at(from); n < at(to); ++n)
        {
            p = std::max(p, std::abs(out.channels[0][n]));
        }
        return p;
    };
    CHECK(loudest(0.0, 0.25) == 0.0f);
    CHECK(loudest(1.3, 1.4) > 0.01f);
    CHECK(loudest(1.7, 2.0) == 0.0f);
    for (const auto &line : events)
    {
        CHECK(nlohmann::json::parse(line)["onset"].get<int64_t>() < 60000); // none after 1.25 s
    }
}
