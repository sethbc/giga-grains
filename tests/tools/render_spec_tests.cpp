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

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
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

TEST_CASE("render block size does not change output", "[render]")
{
    const std::string name = GENERATE(as<std::string>{}, "sparse-sine", "sparse-sine-44k1",
                                      "dense-noise", "sweep-stretch");
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
    };
    CHECK(gg::render::formatEventLine(g) ==
          R"({"i":3,"onset":18000,"frac":0.5,"pos":95999.5,"rate":0.5,"dur":2880})");

    // Doubles are written shortest-round-trip, so they read back exactly.
    const gg::GrainSpec h{
        .index = uint64_t{1} << 40,
        .onsetSample = int64_t{1} << 40,
        .onsetFrac = 0.1,
        .startPos = 0.1 + 0.2,
        .rate = 1e-7,
        .durationSamples = 16.0,
        .gain = 1.0f,
    };
    const std::string line = gg::render::formatEventLine(h);
    CHECK(line == R"({"i":1099511627776,"onset":1099511627776,"frac":0.1,)"
                  R"("pos":0.30000000000000004,"rate":1e-07,"dur":16})");
    const auto parsed = nlohmann::json::parse(line);
    CHECK(parsed["frac"].get<double>() == h.onsetFrac);
    CHECK(parsed["pos"].get<double>() == h.startPos);
    CHECK(parsed["rate"].get<double>() == h.rate);
}
