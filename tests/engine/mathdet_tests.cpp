// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <cmath>
#include <limits>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gg/core/MathDet.h"

using Catch::Matchers::WithinRel;

TEST_CASE("exp2Det matches std::exp2", "[math]")
{
    // std::exp2 is only the reference here; the engine never calls it.
    for (int i = -512; i <= 512; ++i)
    {
        const double x = i / 64.0;
        INFO("x = " << x);
        REQUIRE_THAT(gg::exp2Det(x), WithinRel(std::exp2(x), 1e-14));
    }
    for (int n = -8; n <= 8; ++n)
    {
        INFO("n = " << n);
        REQUIRE(gg::exp2Det(n) == std::ldexp(1.0, n));
    }
}

TEST_CASE("exp2Det saturates on non-finite and huge input", "[math]")
{
    constexpr double kInf = std::numeric_limits<double>::infinity();
    REQUIRE(std::isnan(gg::exp2Det(std::numeric_limits<double>::quiet_NaN())));
    REQUIRE(gg::exp2Det(kInf) == kInf);
    REQUIRE(gg::exp2Det(-kInf) == 0.0);
    REQUIRE(gg::exp2Det(1e300) == kInf);
    REQUIRE(gg::exp2Det(-1e300) == 0.0);
    REQUIRE(gg::exp2Det(1023.0) == std::ldexp(1.0, 1023));
    REQUIRE(gg::exp2Det(1024.0) == kInf);
    REQUIRE(gg::exp2Det(-1074.0) == std::ldexp(1.0, -1074));
    REQUIRE(gg::exp2Det(-1075.0) == 0.0);
}
