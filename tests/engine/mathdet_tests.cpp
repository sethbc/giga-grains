// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
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

namespace
{
/// Tracks the worst |logDet(x) - log(x)| over the bound 1e-14 * max(1, |log x|), and where it
/// happened. std::log is only the reference here; the engine never calls it. A NaN result counts
/// as the worst, so it fails the check.
struct LogDetErrorTracker
{
    double worstRatio = 0.0;
    double worstX = 0.0;

    void check(double x)
    {
        const double ref = std::log(x);
        const double bound = 1e-14 * std::max(1.0, std::abs(ref));
        const double ratio = std::abs(gg::logDet(x) - ref) / bound;
        if (!(ratio <= worstRatio))
        {
            worstRatio = ratio;
            worstX = x;
        }
    }
};
} // namespace

TEST_CASE("logDet matches log", "[math]")
{
    // The Cloud scheduler's Poisson intervals take logDet(1 - u), u a multiple of 2^-53 in
    // [0, 1), so (0, 1] down to 2^-53 is the range that reaches the grain-event log.
    LogDetErrorTracker t;
    constexpr int kSteps = 200000;
    for (int i = 0; i <= kSteps; ++i)
    {
        t.check(std::exp2(-53.0 + 53.0 * i / kSteps)); // geometric, 2^-53 to 1
    }
    t.check(1.0);
    t.check(0.5);
    t.check(0x1.0p-53);
    t.check(std::nextafter(1.0, 0.0));
    INFO("worst x = " << t.worstX << ", error / bound = " << t.worstRatio);
    REQUIRE(t.worstRatio <= 1.0);
    REQUIRE(gg::logDet(1.0) == 0.0);
}

TEST_CASE("logDet matches log across the normal range", "[math]")
{
    LogDetErrorTracker t;
    constexpr int kSteps = 100000;
    for (int i = 0; i <= kSteps; ++i)
    {
        t.check(std::exp2(-1022.0 + 2045.0 * i / kSteps)); // geometric, 2^-1022 to 2^1023
    }
    t.check(std::numeric_limits<double>::min());
    t.check(std::numeric_limits<double>::max());
    t.check(std::nextafter(1.0, 2.0));
    t.check(2.0);
    t.check(10.0);
    INFO("worst x = " << t.worstX << ", error / bound = " << t.worstRatio);
    REQUIRE(t.worstRatio <= 1.0);
}

TEST_CASE("logDet handles zero, negative and non-finite input", "[math]")
{
    constexpr double kInf = std::numeric_limits<double>::infinity();
    REQUIRE(gg::logDet(0.0) == -kInf);
    REQUIRE(gg::logDet(-0.0) == -kInf);
    REQUIRE(gg::logDet(kInf) == kInf);
    REQUIRE(std::isnan(gg::logDet(-1.0)));
    REQUIRE(std::isnan(gg::logDet(-kInf)));
    REQUIRE(std::isnan(gg::logDet(std::numeric_limits<double>::quiet_NaN())));
}

TEST_CASE("logDet's atanh coefficients are 1/(2k+1)", "[math]")
{
    for (int k = 0; k < 13; ++k)
    {
        INFO("k = " << k);
        REQUIRE(gg::detail::kLogAtanh[k] == 1.0 / (2 * k + 1));
    }
}

TEST_CASE("logDet is pinned to exact bits", "[math]")
{
    // A regression guard, not an accuracy check: the literal was captured once from logDet's
    // own output when it was written (-0x1.34378fcbda721p+0, which happens to be ln 0.3
    // correctly rounded), not from an independent reference. Any change to logDet's arithmetic
    // can change these bits, and with them every grain-event log that uses logDet. If it fails,
    // the change breaks the determinism contract: update the literal only together with the
    // golden renders.
    REQUIRE(std::bit_cast<uint64_t>(gg::logDet(0.3)) == 0xbff34378fcbda721ull);
}
