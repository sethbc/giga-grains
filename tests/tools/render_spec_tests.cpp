// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "Render.h"
#include "RenderSpec.h"

using Catch::Matchers::ContainsSubstring;
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
    CHECK(sparse.stream.grainRateHz == 8.0);
    CHECK(sparse.stream.grainDurationMs == 60.0);
    CHECK(sparse.stream.position == 0.5);
    CHECK(sparse.stream.positionJitter == 0.2);
    CHECK(sparse.stream.pitchSemitones == 0.0);
    CHECK(sparse.stream.gain == 0.8);

    // sparse-sine-44k1 is the same case at 44.1 kHz.
    RenderSpec sparse44 = sparse;
    sparse44.sampleRate = 44100.0;
    CHECK(parseCase("sparse-sine-44k1") == sparse44);

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
    REQUIRE(ref.channels[0].size() == static_cast<std::size_t>(3.0 * spec.sampleRate));
    REQUIRE_FALSE(refEvents.empty());

    spec.blockSize = blockSize;
    std::vector<std::string> events;
    const auto out = gg::render::render(spec, &events);
    REQUIRE(out.sampleRate == ref.sampleRate);
    REQUIRE(out.channels.size() == 2);
    CHECK(bitIdentical(out.channels[0], ref.channels[0]));
    CHECK(bitIdentical(out.channels[1], ref.channels[1]));
    CHECK(events == refEvents);
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
