// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gg/core/Window.h"
#include "gg/grain/Envelope.h"

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

// The Texture envelope (spec §4; the plan's fixed family): the anchors, the shortest grain, the
// identity at texture 0, and the shape's continuity.

TEST_CASE("envelope anchors", "[window]")
{
    const gg::EnvelopeShape flat = gg::envelopeShape(0.0, 4800.0);
    REQUIRE(flat.attackEnd == 0.5);
    REQUIRE(flat.releaseStart == 0.5);
    REQUIRE(flat.attackScale == 1.0);
    REQUIRE(flat.releaseScale == 1.0);

    // Texture 0.5: the peak at 70%, no sustain.
    const gg::EnvelopeShape late = gg::envelopeShape(0.5, 4800.0);
    REQUIRE_THAT(late.attackEnd, WithinAbs(0.7, 1e-12));
    REQUIRE_THAT(late.releaseStart, WithinAbs(0.7, 1e-12));
    REQUIRE_THAT(late.attackScale, WithinAbs(0.5 / 0.7, 1e-12));
    REQUIRE_THAT(late.releaseScale, WithinAbs(0.5 / 0.3, 1e-12));
    REQUIRE(late.releaseStart == late.attackEnd);

    // Texture 1: the gated anchor, 5% attack and release on a long grain, 16 samples on a short.
    const gg::EnvelopeShape gate = gg::envelopeShape(1.0, 4800.0);
    REQUIRE(gate.attackEnd == 0.05);
    REQUIRE(gate.releaseStart == 0.95);
    REQUIRE(gg::envelopeShape(1.0, 160.0).attackEnd == 0.1);

    // NaN and out-of-range textures read as 0.
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(), -0.25, 1.5,
                             std::numeric_limits<double>::infinity()})
    {
        INFO("texture " << bad);
        const gg::EnvelopeShape e = gg::envelopeShape(bad, 4800.0);
        REQUIRE(e.attackEnd == 0.5);
        REQUIRE(e.releaseStart == 0.5);
        REQUIRE(e.attackScale == 1.0);
        REQUIRE(e.releaseScale == 1.0);
    }
}

TEST_CASE("texture on the shortest grain is a Hann", "[window]")
{
    // 16 / d = 1 at the minimum duration: aMin is clamped to 0.5, so even texture 1 is the plain
    // Hann, with no division by zero and no NaN.
    const gg::EnvelopeShape e = gg::envelopeShape(1.0, 16.0);
    REQUIRE(e.attackEnd == 0.5);
    REQUIRE(e.releaseStart == 0.5);
    REQUIRE(e.attackScale == 1.0);
    REQUIRE(e.releaseScale == 1.0);
    for (int i = 0; i <= 1000; ++i)
    {
        const double phase = i / 1000.0;
        INFO("phase " << phase);
        REQUIRE(gg::warpPhase(phase, e) == phase);
    }
}

TEST_CASE("warp is the identity at texture 0", "[window]")
{
    const gg::EnvelopeShape shape0 = gg::envelopeShape(0.0, 4800.0);
    // The SIMD interior skips the warp exactly for these shapes.
    for (const double duration : {16.0, 100.0, 4800.0, 1e9})
    {
        REQUIRE(gg::isIdentity(gg::envelopeShape(0.0, duration)));
    }
    REQUIRE(gg::isIdentity(gg::envelopeShape(1.0, 16.0)));
    for (const double texture : {1e-12, 0.25, 0.5, 0.75, 1.0})
    {
        REQUIRE_FALSE(gg::isIdentity(gg::envelopeShape(texture, 4800.0)));
    }
    for (int i = 0; i <= 10000; ++i)
    {
        // Phases that aren't round: i / 10000 nudged by an irrational step, then kept in [0, 1].
        const double raw = i / 10000.0 + (i % 7) * 1.2345678901234567e-6;
        const double phase = raw > 1.0 ? 1.0 : raw;
        const double warped = gg::warpPhase(phase, shape0);
        INFO("phase " << phase);
        REQUIRE(std::memcmp(&warped, &phase, sizeof phase) == 0);
    }
}

TEST_CASE("the envelope is continuous and bounded", "[window]")
{
    const gg::HannTable hann;
    for (const double texture : {0.25, 0.5, 0.75, 1.0})
    {
        for (const double duration : {16.0, 100.0, 4800.0})
        {
            INFO("texture " << texture << ", duration " << duration);
            const gg::EnvelopeShape e = gg::envelopeShape(texture, duration);
            REQUIRE(e.attackEnd > 0.0);
            // No sustain at texture <= 0.5: the release starts where the attack ends, to rounding.
            REQUIRE(e.releaseStart >= e.attackEnd - 1e-15);
            REQUIRE(e.releaseStart < 1.0);
            const auto window = [&](double phase) { return hann.at(gg::warpPhase(phase, e)); };
            REQUIRE(window(0.0) == 0.0f);
            REQUIRE(window(1.0) == 0.0f);
            // One sample per step of the grain (and at least 4,800 steps, so the short grains'
            // segments are sampled densely too).
            const int steps = duration < 4800.0 ? 4800 : static_cast<int>(duration);
            float previous = 0.0f;
            for (int i = 0; i <= steps; ++i)
            {
                const double phase = static_cast<double>(i) / steps;
                const float w = window(phase);
                INFO("phase " << phase << ", w " << w << ", previous " << previous);
                REQUIRE(w >= 0.0f);
                REQUIRE(w <= 1.0f);
                if (phase <= e.attackEnd)
                {
                    REQUIRE(w >= previous);
                }
                else if (phase <= e.releaseStart)
                {
                    REQUIRE(w == 1.0f);
                }
                else
                {
                    REQUIRE(w <= previous);
                }
                if (duration == 4800.0 && i > 0)
                {
                    // Adjacent samples of the grain: the steepest edge is 16 samples long.
                    REQUIRE(std::abs(w - previous) < 2.0f / 16.0f);
                }
                previous = w;
            }
        }
    }
}

namespace
{
constexpr double kTextures[] = {0.0, 0.25, 0.5, 0.75, 1.0};
constexpr double kDurations[] = {16.0, 17.0, 100.0, 320.0, 4800.0, 96000.0};

uint64_t bitsOf(double v) { return std::bit_cast<uint64_t>(v); }

bool sameShape(const gg::EnvelopeShape &x, const gg::EnvelopeShape &y)
{
    return bitsOf(x.attackEnd) == bitsOf(y.attackEnd) &&
           bitsOf(x.releaseStart) == bitsOf(y.releaseStart) &&
           bitsOf(x.attackScale) == bitsOf(y.attackScale) &&
           bitsOf(x.releaseScale) == bitsOf(y.releaseScale);
}

double skewFloor(double d)
{
    const double atLeast = 16.0 / d > 0.01 ? 16.0 / d : 0.01;
    return atLeast < 0.5 ? atLeast : 0.5;
}

double ulpOf(double v) { return std::nextafter(v, 1e300) - v; }
} // namespace

TEST_CASE("skew 0 is the texture envelope bit for bit", "[engine][window][grainshape]")
{
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    for (const double tau : kTextures)
    {
        for (const double d : kDurations)
        {
            INFO("texture " << tau << ", duration " << d);
            const gg::EnvelopeShape today = gg::envelopeShape(tau, d);
            for (const double sigma : {0.0, -0.0, kNaN, 1.5, -2.0})
            {
                REQUIRE(sameShape(gg::envelopeShape(tau, sigma, d), today));
            }
        }
    }
}

TEST_CASE("skew lands on its anchors", "[engine][window][grainshape]")
{
    for (const double tau : kTextures)
    {
        for (const double d : kDurations)
        {
            INFO("texture " << tau << ", duration " << d);
            const gg::EnvelopeSides sides = gg::detail::textureSides(tau, d);
            const double sMin = skewFloor(d);
            const gg::EnvelopeShape early = gg::envelopeShape(tau, -1.0, d);
            REQUIRE(early.attackEnd == std::min(sides.attack, sMin));
            const gg::EnvelopeShape late = gg::envelopeShape(tau, 1.0, d);
            REQUIRE(std::abs((1.0 - late.releaseStart) - std::min(sides.release, sMin)) <=
                    ulpOf(1.0));
            if (d <= 32.0)
            {
                // sMin is 0.5: a side at most that long stays as it is. Texture's sides run past
                // 0.5 at 0.25, 0.5 and 0.75, where the floor takes them down to it.
                const gg::EnvelopeShape today = gg::envelopeShape(tau, d);
                if (sides.attack <= 0.5)
                {
                    REQUIRE(early.attackEnd == today.attackEnd);
                }
                if (sides.release <= 0.5)
                {
                    REQUIRE(std::abs(late.releaseStart - today.releaseStart) <= ulpOf(1.0));
                }
            }
        }
    }
}

TEST_CASE("skew keeps attack plus release", "[engine][window][grainshape]")
{
    for (const double tau : kTextures)
    {
        for (const double d : kDurations)
        {
            const gg::EnvelopeSides sides = gg::detail::textureSides(tau, d);
            const double total = sides.attack + sides.release;
            for (const double sigma : {-1.0, -0.5, -0.01, 0.01, 0.5, 1.0})
            {
                INFO("texture " << tau << ", duration " << d << ", skew " << sigma);
                const gg::EnvelopeShape e = gg::envelopeShape(tau, sigma, d);
                const double r2 = 1.0 - e.releaseStart;
                // releaseStart stores 1 - r', so r' is recovered to within one ulp of 1.
                REQUIRE(std::abs((e.attackEnd + r2) - total) <= ulpOf(total) + ulpOf(1.0));
            }
        }
    }
}

TEST_CASE("skew moves the peak monotonically and never the wrong way",
          "[engine][window][grainshape]")
{
    for (const double tau : kTextures)
    {
        for (const double d : kDurations)
        {
            INFO("texture " << tau << ", duration " << d);
            double previous = 0.0;
            for (int i = 0; i <= 40; ++i)
            {
                const double sigma = -1.0 + 0.05 * i;
                const double attack = gg::envelopeShape(tau, sigma, d).attackEnd;
                REQUIRE(attack >= previous);
                previous = attack;
            }
        }
    }
    // A side already at the floor is never lengthened (d = 100, texture 1: a = r = 0.16 = sMin).
    const double sMin = skewFloor(100.0);
    const gg::EnvelopeSides sides = gg::detail::textureSides(1.0, 100.0);
    REQUIRE(sides.attack <= sMin);
    for (int i = 0; i <= 40; ++i)
    {
        const double sigma = -1.0 + 0.05 * i;
        const gg::EnvelopeShape e = gg::envelopeShape(1.0, sigma, 100.0);
        if (sigma < 0.0)
        {
            REQUIRE(e.attackEnd <= sides.attack + ulpOf(sides.attack));
        }
        else if (sigma > 0.0)
        {
            REQUIRE(1.0 - e.releaseStart <= sides.release + ulpOf(1.0));
        }
    }
}

TEST_CASE("skew keeps the window's mean and energy", "[engine][window][grainshape]")
{
    const gg::HannTable hann;
    constexpr int kSteps = 100000;
    const auto integrate = [&](const gg::EnvelopeShape &e) {
        double mean = 0.0;
        double energy = 0.0;
        for (int i = 0; i < kSteps; ++i)
        {
            const double w = hann.at(gg::warpPhase((i + 0.5) / kSteps, e));
            mean += w;
            energy += w * w;
        }
        return std::pair{mean / kSteps, energy / kSteps};
    };
    for (const double tau : kTextures)
    {
        for (const double d : kDurations)
        {
            const auto [mean0, energy0] = integrate(gg::envelopeShape(tau, d));
            for (const double sigma : {-1.0, -0.5, 0.5, 1.0})
            {
                INFO("texture " << tau << ", duration " << d << ", skew " << sigma);
                const auto [mean, energy] = integrate(gg::envelopeShape(tau, sigma, d));
                REQUIRE(std::abs(mean - mean0) <= 1e-6 * mean0);
                REQUIRE(std::abs(energy - energy0) <= 1e-6 * energy0);
            }
        }
    }
}
