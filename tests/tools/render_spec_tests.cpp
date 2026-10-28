// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
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

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using gg::core::Mode;
using gg::core::ParamEvent;
using gg::core::ParamSlot;
using gg::core::Processor;
using gg::render::AutomationPoint;
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
    SECTION("file input")
    {
        auto j = good;
        j["input"] = {{"kind", "file"}};
        CHECK_THAT(rejection(j), ContainsSubstring("input.kind"));
        j["input"]["path"] = "in.wav"; // an input has no path
        CHECK_THAT(rejection(j), ContainsSubstring("input.path"));
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
