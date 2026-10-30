// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gg/core/Interpolate.h"

using Catch::Matchers::WithinAbs;

namespace
{
constexpr int64_t kFrames = 64;

// Sample i sits at t = i/32. All values below are exact in float.
double rampAt(double t) { return t; }
double quadAt(double t) { return 0.5 * t * t - 0.25 * t + 0.125; }

std::vector<float> sampled(double (*fn)(double))
{
    std::vector<float> v(kFrames);
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        v[i] = static_cast<float>(fn(static_cast<double>(i) / 32.0));
    }
    return v;
}
} // namespace

TEST_CASE("interpolators reproduce polynomials", "[interp]")
{
    const auto ramp = sampled(rampAt);
    const auto quad = sampled(quadAt);

    // Dyadic positions keep every intermediate exact, so linear must be bit-exact on a ramp.
    for (int k = 0; 2.0 + k / 16.0 <= kFrames - 3; ++k)
    {
        const double pos = 2.0 + k / 16.0;
        INFO("pos = " << pos);
        REQUIRE(gg::interp::linear(ramp.data(), kFrames, pos) == static_cast<float>(pos / 32.0));
    }

    // Catmull-Rom reproduces polynomials up to degree 2.
    for (int k = 0; 2.0 + k * 0.173 <= kFrames - 3; ++k)
    {
        const double pos = 2.0 + k * 0.173;
        INFO("pos = " << pos);
        REQUIRE_THAT(gg::interp::hermite4(ramp.data(), kFrames, pos),
                     WithinAbs(rampAt(pos / 32.0), 1e-5));
        REQUIRE_THAT(gg::interp::hermite4(quad.data(), kFrames, pos),
                     WithinAbs(quadAt(pos / 32.0), 1e-5));
    }
}

TEST_CASE("interpolators read zero outside the buffer", "[interp]")
{
    constexpr double kInf = std::numeric_limits<double>::infinity();
    const std::vector<float> ones(kFrames, 1.0f);
    for (double pos : {-10.0, static_cast<double>(kFrames + 10), -kInf, kInf,
                       std::numeric_limits<double>::quiet_NaN()})
    {
        INFO("pos = " << pos);
        REQUIRE(gg::interp::linear(ones.data(), kFrames, pos) == 0.0f);
        REQUIRE(gg::interp::hermite4(ones.data(), kFrames, pos) == 0.0f);
    }

    // A 1-frame buffer: every tap but one is outside, so nothing may be read past it.
    const float one = 1.0f;
    REQUIRE(gg::interp::linear(&one, 1, 0.0) == 1.0f);
    REQUIRE(gg::interp::hermite4(&one, 1, 0.0) == 1.0f);
    for (double pos : {-3.0, -2.0, -1.5, -1.0, -0.5, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0})
    {
        INFO("pos = " << pos);
        REQUIRE(std::isfinite(gg::interp::linear(&one, 1, pos)));
        REQUIRE(std::isfinite(gg::interp::hermite4(&one, 1, pos)));
    }

    // An empty buffer is never dereferenced.
    REQUIRE(gg::interp::linear(nullptr, 0, 0.0) == 0.0f);
    REQUIRE(gg::interp::hermite4(nullptr, 0, 0.0) == 0.0f);
}

TEST_CASE("strided readers equal unit-stride readers", "[interp]")
{
    // An interleaved buffer read at stride 2 gives its channel's bits read at stride 1 (spec
    // §6.3: the scalar reference reads a stereo level's pairs through the stride).
    std::vector<float> left(kFrames);
    std::vector<float> right(kFrames);
    std::vector<float> pairs(2 * kFrames);
    for (int64_t f = 0; f < kFrames; ++f)
    {
        left[static_cast<std::size_t>(f)] = std::sin(0.37f * static_cast<float>(f));
        right[static_cast<std::size_t>(f)] = std::cos(0.11f * static_cast<float>(f)) - 0.25f;
        pairs[static_cast<std::size_t>(2 * f)] = left[static_cast<std::size_t>(f)];
        pairs[static_cast<std::size_t>(2 * f + 1)] = right[static_cast<std::size_t>(f)];
    }
    const auto same = [](float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; };
    std::vector<double> positions{std::numeric_limits<double>::quiet_NaN(),
                                  -std::numeric_limits<double>::infinity(),
                                  std::numeric_limits<double>::infinity()};
    for (double pos = -3.0; pos < static_cast<double>(kFrames) + 3.0; pos += 0.0371)
    {
        positions.push_back(pos);
    }
    for (const double pos : positions)
    {
        INFO("pos = " << pos);
        REQUIRE(same(gg::interp::linear<2>(pairs.data(), kFrames, pos),
                     gg::interp::linear(left.data(), kFrames, pos)));
        REQUIRE(same(gg::interp::linear<2>(pairs.data() + 1, kFrames, pos),
                     gg::interp::linear(right.data(), kFrames, pos)));
        REQUIRE(same(gg::interp::hermite4<2>(pairs.data(), kFrames, pos),
                     gg::interp::hermite4(left.data(), kFrames, pos)));
        REQUIRE(same(gg::interp::hermite4<2>(pairs.data() + 1, kFrames, pos),
                     gg::interp::hermite4(right.data(), kFrames, pos)));
    }
}
