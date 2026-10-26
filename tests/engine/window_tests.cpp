// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <limits>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gg/core/Window.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("hann overlap-add at 50% sums to one", "[window]")
{
    const gg::HannTable hann;
    REQUIRE(hann.at(0.0) == 0.0f);
    for (int i = 0; i < 500; ++i)
    {
        const double p = i * 1e-3;
        INFO("p = " << p);
        const double sum = static_cast<double>(hann.at(p)) + hann.at(p + 0.5);
        REQUIRE_THAT(sum, WithinAbs(1.0, 1e-6));
    }
}

TEST_CASE("hann ends at zero and is zero outside [0,1]", "[window]")
{
    constexpr double kInf = std::numeric_limits<double>::infinity();
    const gg::HannTable hann;
    REQUIRE(hann.at(1.0) == 0.0f);
    REQUIRE_THAT(hann.at(0.5), WithinAbs(1.0, 1e-7));
    REQUIRE(hann.at(-1e-9) == 0.0f);
    REQUIRE(hann.at(1.0 + 1e-9) == 0.0f);
    REQUIRE(hann.at(-kInf) == 0.0f);
    REQUIRE(hann.at(kInf) == 0.0f);
    REQUIRE(hann.at(std::numeric_limits<double>::quiet_NaN()) == 0.0f);
}
