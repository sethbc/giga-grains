// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The global delay (extremes spec §6.4): bypassed bit for bit at amount 0, echoing at its time
// with feedback through the soft limiter, ping-pong across the channels, and a time that glides.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Delay.h"
#include "gg/core/Rng.h"

using gg::core::Delay;

namespace
{
constexpr double kSr = 48000.0;

// A uniform draw in [-1, 1) for index i: SplitMix64 over (seed, i), no std:: distributions.
float noise(uint64_t seed, uint64_t i)
{
    return static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(seed * 0x10000000ull + i)));
}
} // namespace

TEST_CASE("the delay at amount 0 is the identity", "[core][fx]")
{
    // 1 s of stereo noise, with the awkward values among it: ±0 and denormals.
    const float specials[] = {0.0f, -0.0f, std::numeric_limits<float>::denorm_min(),
                              -std::numeric_limits<float>::denorm_min(), -3.0e-39f};
    const auto frames = static_cast<size_t>(kSr);
    std::vector<float> in(2 * frames);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = i % 7 == 0 ? specials[(i / 7) % std::size(specials)] : noise(1, i);

    struct Setting
    {
        float feedback, damp;
        bool pingPong;
    };
    for (const Setting &s :
         {Setting{0.0f, 1.0f, false}, Setting{1.0f, 0.3f, true}, Setting{0.6f, 0.05f, false}})
    {
        CAPTURE(s.feedback, s.damp, s.pingPong);
        Delay d;
        d.prepare(kSr);
        d.setTimeSamples(1000.0);
        std::vector<float> out(in.size());
        for (size_t i = 0; i < frames; ++i)
        {
            float l = in[2 * i], r = in[2 * i + 1];
            d.process(l, r, 0.0f, s.feedback, s.damp, s.pingPong);
            out[2 * i] = l;
            out[2 * i + 1] = r;
        }
        CHECK(std::memcmp(in.data(), out.data(), in.size() * sizeof(float)) == 0);

        if (s.feedback == 0.0f)
        {
            // The line was written through the bypass: at amount 1 over silence, the next
            // 1,000 frames are the noise's last 1,000, exactly.
            for (size_t k = 0; k < 1000; ++k)
            {
                float l = 0.0f, r = 0.0f;
                d.process(l, r, 1.0f, 0.0f, 1.0f, false);
                const size_t src = frames - 1000 + k;
                CAPTURE(k);
                REQUIRE(l == in[2 * src]);
                REQUIRE(r == in[2 * src + 1]);
            }
        }
    }
}

TEST_CASE("an impulse echoes at the time", "[core][fx]")
{
    // The impulse is 0.5, at the soft limiter's knee, so the loop is linear: each repeat is
    // exactly half the one before.
    constexpr float kImpulse = 0.5f, kAmount = 0.8f;
    Delay d;
    d.prepare(kSr);
    d.setTimeSamples(1000.0);
    std::vector<float> outL(3500), outR(3500);
    for (size_t i = 0; i < outL.size(); ++i)
    {
        float l = i == 0 ? kImpulse : 0.0f, r = 0.0f;
        d.process(l, r, kAmount, 0.5f, 1.0f, false);
        outL[i] = l;
        outR[i] = r;
    }
    CHECK(outL[0] == kImpulse); // the input itself, plus an empty line
    const double expected[] = {1.0, 0.5, 0.25};
    for (size_t k = 0; k < 3; ++k)
    {
        CAPTURE(k);
        CHECK(std::abs(outL[1000 * (k + 1)] - expected[k] * kImpulse * kAmount) < 1e-6);
    }
    for (size_t i = 1; i < outL.size(); ++i)
    {
        CAPTURE(i);
        if (i % 1000 != 0)
            REQUIRE(outL[i] == 0.0f);
        REQUIRE(outR[i] == 0.0f); // no crosstalk without ping-pong
    }
}

TEST_CASE("feedback 1 holds and stays below 1", "[core][fx]")
{
    // A 10 ms burst at 0.9 into a 0.5 s loop at feedback 1, no damping: the soft limiter brings
    // each repeat down toward its fixed point, 0.5, ever more slowly, so the repeat holds.
    Delay d;
    d.prepare(kSr);
    d.setTimeSamples(0.5 * kSr);
    const auto second = static_cast<size_t>(kSr);
    const size_t burst = second / 100;
    float peakTenth = 0.0f, peakLast = 0.0f;
    bool bounded = true;
    for (size_t i = 0; i < 60 * second; ++i)
    {
        float l = i < burst ? 0.9f : 0.0f, r = l;
        d.process(l, r, 1.0f, 1.0f, 1.0f, false);
        if (!(std::isfinite(l) && std::isfinite(r) && std::abs(l) < 1.0f && std::abs(r) < 1.0f))
        {
            CAPTURE(i, l, r);
            bounded = false;
            break;
        }
        const float peak = std::max(std::abs(l), std::abs(r));
        if (i >= 9 * second && i < 10 * second)
            peakTenth = std::max(peakTenth, peak);
        if (i >= 59 * second)
            peakLast = std::max(peakLast, peak);
    }
    CHECK(bounded);
    REQUIRE(peakTenth > 0.0f);
    const double db = 20.0 * std::log10(static_cast<double>(peakLast) / peakTenth);
    CAPTURE(peakTenth, peakLast, db);
    CHECK(peakLast > 0.4f);
    CHECK(std::abs(db) < 1.0);
}

TEST_CASE("ping-pong alternates channels", "[core][fx]")
{
    // An impulse on L: the law reads each channel's own line, and feeds each line from the
    // other channel's read, so the repeats go L, then R alone, then L alone.
    Delay d;
    d.prepare(kSr);
    d.setTimeSamples(1000.0);
    std::vector<float> outL(3500), outR(3500);
    for (size_t i = 0; i < outL.size(); ++i)
    {
        float l = i == 0 ? 0.5f : 0.0f, r = 0.0f;
        d.process(l, r, 1.0f, 0.5f, 1.0f, true);
        outL[i] = l;
        outR[i] = r;
    }
    CHECK(std::abs(outL[1000] - 0.5f) < 1e-6);
    CHECK(outR[1000] == 0.0f);
    CHECK(outL[2000] == 0.0f);
    CHECK(std::abs(outR[2000] - 0.25f) < 1e-6);
    CHECK(std::abs(outL[3000] - 0.125f) < 1e-6);
    CHECK(outR[3000] == 0.0f);
    for (size_t i = 1; i < outL.size(); ++i)
    {
        CAPTURE(i);
        if (i % 1000 != 0)
        {
            REQUIRE(outL[i] == 0.0f);
            REQUIRE(outR[i] == 0.0f);
        }
    }
}

TEST_CASE("the time glides at most half a sample per sample", "[core][fx]")
{
    const double from = 0.1 * kSr, to = 0.5 * kSr;
    for (const bool up : {true, false})
    {
        CAPTURE(up);
        Delay d;
        d.prepare(kSr);
        d.setTimeSamples(up ? from : to);
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < 100; ++i)
            d.process(l, r, 1.0f, 0.4f, 0.5f, false);
        REQUIRE(d.timeSamples() == (up ? from : to));

        const double target = up ? to : from;
        d.setTimeSamples(target);
        REQUIRE(d.timeSamples() == (up ? from : to)); // set, but not yet moved
        double before = d.timeSamples();
        size_t calls = 0;
        bool stepsOk = true;
        while (d.timeSamples() != target && calls < 100000)
        {
            d.process(l, r, 1.0f, 0.4f, 0.5f, false);
            const double step = d.timeSamples() - before;
            if (std::abs(step) > Delay::kGlideMaxRate || (up ? step <= 0.0 : step >= 0.0))
                stepsOk = false;
            before = d.timeSamples();
            ++calls;
        }
        CHECK(stepsOk);
        CHECK(d.timeSamples() == target);
        CHECK(calls == static_cast<size_t>((to - from) / Delay::kGlideMaxRate));
    }

    // The target is clamped to [1, 2 s].
    Delay d;
    d.prepare(kSr);
    d.setTimeSamples(1.0e9);
    CHECK(d.timeSamples() == Delay::kMaxSeconds * kSr);
    d.setTimeSamples(0.25);
    CHECK(d.timeSamples() == 1.0);
    d.setTimeSamples(std::numeric_limits<double>::quiet_NaN());
    CHECK(d.timeSamples() == 1.0);
}

TEST_CASE("the first target applies at once", "[core][fx]")
{
    Delay d;
    d.prepare(kSr);
    CHECK(d.timeSamples() == 1.0);
    // Before anything runs, every target applies at once, the last one standing.
    d.setTimeSamples(20000.0);
    CHECK(d.timeSamples() == 20000.0);
    d.setTimeSamples(3000.0);
    CHECK(d.timeSamples() == 3000.0);

    // So an impulse into a fresh delay echoes at that time, not on the way there.
    float l = 0.5f, r = 0.0f;
    d.process(l, r, 1.0f, 0.0f, 1.0f, false);
    for (int i = 1; i < 3000; ++i)
    {
        l = 0.0f;
        d.process(l, r, 1.0f, 0.0f, 1.0f, false);
        REQUIRE(l == 0.0f);
    }
    l = 0.0f;
    d.process(l, r, 1.0f, 0.0f, 1.0f, false);
    CHECK(l == 0.5f);

    // Once it has run, a new target glides.
    d.setTimeSamples(10000.0);
    CHECK(d.timeSamples() == 3000.0);

    // Fill the whole line with noise, looping, so a reset that left anything would show.
    for (uint64_t i = 0; i < static_cast<uint64_t>(2.1 * kSr); ++i)
    {
        l = noise(2, 2 * i);
        r = noise(2, 2 * i + 1);
        d.process(l, r, 1.0f, 0.9f, 0.5f, true);
    }

    // reset() forgets the glide: the read stands at its target, and the next applies at once.
    d.reset();
    CHECK(d.timeSamples() == 10000.0);
    d.setTimeSamples(500.0);
    CHECK(d.timeSamples() == 500.0);
    // It cleared the line and the dampers: over silence, nothing comes back.
    for (int i = 0; i < 600; ++i)
    {
        l = 0.0f;
        r = 0.0f;
        d.process(l, r, 1.0f, 0.9f, 0.5f, true);
        REQUIRE(l == 0.0f);
        REQUIRE(r == 0.0f);
    }
    d.setTimeSamples(4000.0);
    CHECK(d.timeSamples() == 500.0);

    // prepare() too.
    d.prepare(44100.0);
    CHECK(d.timeSamples() == 1.0);
    d.setTimeSamples(7000.0);
    CHECK(d.timeSamples() == 7000.0);
}
