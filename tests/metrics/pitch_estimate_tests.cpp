// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The f0 estimator that pitch-sync's tests use (M3 sub-project 3).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Rng.h"
#include "support/PitchEstimate.h"

namespace
{
constexpr double kSr = 48000.0;
constexpr std::size_t kLen = 24000; // 0.5 s

std::vector<float> sawtooth(double f, double phase = 0.0, double dc = 0.0, std::size_t len = kLen)
{
    std::vector<float> x(len);
    for (std::size_t i = 0; i < len; ++i)
    {
        double s = 0.0;
        for (int h = 1; h * f < 20000.0; ++h)
            s += std::sin(2.0 * std::numbers::pi * h * (f * static_cast<double>(i) / kSr + phase)) /
                 h;
        x[i] = static_cast<float>(s * 0.5 + dc);
    }
    return x;
}
} // namespace

TEST_CASE("estimateF0 finds band-limited sawtooths to a tenth of a cent", "[metrics][pitchsync]")
{
    for (const double f : {55.0, 110.0, 220.0, 440.0, 1000.0, 2093.0, 4000.0})
    {
        const auto x = sawtooth(f);
        const double est = gg::test::estimateF0(x, kSr, 40.0, 5000.0);
        const double cents = gg::test::centsBetween(est, f);
        INFO("f " << f << " estimate " << est << " cents " << cents);
        CHECK(std::abs(cents) < 0.1);
    }
}

TEST_CASE("estimateF0 returns 0 for white noise", "[metrics][pitchsync]")
{
    std::vector<float> x(kLen);
    for (std::size_t i = 0; i < kLen; ++i)
        x[i] = static_cast<float>(gg::rng::bipolar(
            gg::rng::hash({7, 0, 0, gg::rng::Field::SignalNoise, static_cast<std::uint64_t>(i)})));
    CHECK(gg::test::estimateF0(x, kSr, 40.0, 5000.0) == 0.0);
}

TEST_CASE("estimateF0 ignores a phase offset and a DC offset", "[metrics][pitchsync]")
{
    const auto x = sawtooth(330.0, 0.37, 0.4);
    const double est = gg::test::estimateF0(x, kSr, 40.0, 5000.0);
    CHECK(std::abs(gg::test::centsBetween(est, 330.0)) < 0.1);
}

TEST_CASE("estimateF0 holds a tenth of a cent on 0.25 s windows", "[metrics][pitchsync]")
{
    for (const double f : {55.0, 4000.0})
    {
        const auto x = sawtooth(f, 0.0, 0.0, 12000);
        const double est = gg::test::estimateF0(x, kSr, 40.0, 5000.0);
        INFO("f " << f << " estimate " << est);
        CHECK(std::abs(gg::test::centsBetween(est, f)) < 0.1);
    }
}

TEST_CASE("estimateF0 finds a pure sine without an octave error", "[metrics][pitchsync]")
{
    std::vector<float> x(kLen);
    for (std::size_t i = 0; i < kLen; ++i)
        x[i] = static_cast<float>(
            std::sin(2.0 * std::numbers::pi * 330.0 * static_cast<double>(i) / kSr));
    const double est = gg::test::estimateF0(x, kSr, 40.0, 5000.0);
    INFO("estimate " << est);
    CHECK(std::abs(gg::test::centsBetween(est, 330.0)) < 0.1);
}
