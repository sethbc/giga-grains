// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <cmath>
#include <cstddef>
#include <cstdint>
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
