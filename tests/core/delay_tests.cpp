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
#include "gg/core/ParamMap.h"
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
        float pingPong;
    };
    for (const Setting &s :
         {Setting{0.0f, 1.0f, 0.0f}, Setting{1.0f, 0.3f, 1.0f}, Setting{0.6f, 0.05f, 0.0f}})
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
                d.process(l, r, 1.0f, 0.0f, 1.0f, 0.0f);
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
        d.process(l, r, kAmount, 0.5f, 1.0f, 0.0f);
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

TEST_CASE("feedback 1 at Damping 0 holds a sine and darkens noise", "[core][fx]")
{
    // D7a (the director, 2026-10-30): Damping 0 is the damper's 20 kHz one-pole, through the
    // Processor's mapping, not a bypass. A 0.5 s loop at feedback 1, filled once with half a
    // second of material at 0.3, below the soft limiter's knee (so the loop is linear), then
    // left to repeat for 20 s.
    const float coeff = gg::core::dampCoefficient(0.0f, kSr);
    REQUIRE(std::abs(coeff - 0.927f) < 5e-4f);
    const auto loop = static_cast<size_t>(0.5 * kSr);
    const auto second = static_cast<size_t>(kSr);
    // The RMS of the output and of its first difference (a measure of the top octaves) over the
    // output seconds [1, 2) and [19, 20).
    struct Levels
    {
        double early, late, diffEarly, diffLate;
    };
    const auto hold = [&](bool sine) {
        Delay d;
        d.prepare(kSr);
        d.setTimeSamples(static_cast<double>(loop));
        double sum[2] = {0.0, 0.0}, diff[2] = {0.0, 0.0};
        float prev = 0.0f;
        for (size_t i = 0; i < 20 * second; ++i)
        {
            float in = 0.0f;
            if (i < loop)
            {
                // 440 Hz is 220 whole periods per loop, so the loop has no seam.
                in = sine ? static_cast<float>(0.3 * std::sin(2.0 * 3.141592653589793 * 440.0 *
                                                              static_cast<double>(i) / kSr))
                          : 0.3f * noise(3, i);
            }
            float l = in, r = in;
            d.process(l, r, 1.0f, 1.0f, coeff, 0.0f);
            REQUIRE(std::isfinite(l));
            REQUIRE(std::abs(l) < 1.0f);
            const int window = i >= second && i < 2 * second ? 0 : i >= 19 * second ? 1 : -1;
            if (window >= 0)
            {
                sum[window] += static_cast<double>(l) * l;
                diff[window] += static_cast<double>(l - prev) * (l - prev);
            }
            prev = l;
        }
        return Levels{std::sqrt(sum[0]), std::sqrt(sum[1]), std::sqrt(diff[0]), std::sqrt(diff[1])};
    };
    const auto dB = [](double a, double b) { return 20.0 * std::log10(a / b); };

    // A sine holds within 0.1 dB over 18 s (36 passes).
    const Levels sine = hold(true);
    CAPTURE(sine.early, sine.late, dB(sine.late, sine.early));
    REQUIRE(sine.early > 0.0);
    CHECK(std::abs(dB(sine.late, sine.early)) < 0.1);

    // Noise darkens: its top octaves (the first difference) lose far more than its whole level,
    // which falls slowly as the one-pole takes the top of the spectrum pass by pass (measured
    // here: about -17 dB and -7 dB over the 18 s; the sine's change, about -0.04 dB).
    const Levels n = hold(false);
    const double whole = dB(n.late, n.early);
    const double top = dB(n.diffLate, n.diffEarly);
    CAPTURE(whole, top);
    CHECK(top < -10.0);
    CHECK(top < whole - 5.0);
    CHECK(whole < -1.0);
    CHECK(whole > -12.0);
}

TEST_CASE("feedback 1 stays below 1", "[core][fx]")
{
    // A 10 ms burst at 0.9 into a 0.5 s loop at feedback 1 through Damping 0's coefficient: the
    // soft limiter brings each repeat down toward its fixed point, 0.5, and the loop never
    // reaches 1 in magnitude.
    Delay d;
    d.prepare(kSr);
    d.setTimeSamples(0.5 * kSr);
    const float coeff = gg::core::dampCoefficient(0.0f, kSr);
    const auto second = static_cast<size_t>(kSr);
    const size_t burst = second / 100;
    float peakLast = 0.0f;
    bool bounded = true;
    for (size_t i = 0; i < 60 * second; ++i)
    {
        float l = i < burst ? 0.9f : 0.0f, r = l;
        d.process(l, r, 1.0f, 1.0f, coeff, 0.0f);
        if (!(std::isfinite(l) && std::isfinite(r) && std::abs(l) < 1.0f && std::abs(r) < 1.0f))
        {
            CAPTURE(i, l, r);
            bounded = false;
            break;
        }
        if (i >= 59 * second)
            peakLast = std::max(peakLast, std::max(std::abs(l), std::abs(r)));
    }
    CHECK(bounded);
    CAPTURE(peakLast);
    CHECK(peakLast > 0.4f); // still near the fixed point after a minute
}

TEST_CASE("a ramped ping-pong weight does not click", "[core][fx]")
{
    // A stereo 110 Hz tone in antiphase, so the two reads differ most, at feedback 0.5 through
    // Damping 0.3's coefficient. The weight moves from 0 to 1 at kSwitch, over 480 samples (the
    // Processor's 10 ms ramp) or at once. The damper writes the move into the line, which plays
    // it one Delay Time later: around there the ramped output's largest second difference stays
    // near the steady signal's, and a step's (the negative control) is many times it.
    constexpr size_t kTime = 12000, kSwitch = 48000, kRamp = 480, kFrames = 72000;
    const float coeff = gg::core::dampCoefficient(0.3f, kSr);
    const auto render = [&](bool ramped) {
        Delay d;
        d.prepare(kSr);
        d.setTimeSamples(static_cast<double>(kTime));
        std::vector<float> outL(kFrames);
        for (size_t i = 0; i < kFrames; ++i)
        {
            const auto x = static_cast<float>(
                0.3 * std::sin(2.0 * 3.141592653589793 * 110.0 * static_cast<double>(i) / kSr));
            float l = x, r = -x;
            float p = i < kSwitch ? 0.0f : 1.0f;
            if (ramped && i >= kSwitch && i < kSwitch + kRamp)
                p = static_cast<float>(i - kSwitch) / static_cast<float>(kRamp);
            d.process(l, r, 0.5f, 0.5f, coeff, p);
            outL[i] = l;
        }
        return outL;
    };
    const auto largestSecondDifference = [](const std::vector<float> &v, size_t from, size_t to) {
        float worst = 0.0f;
        for (size_t i = from + 2; i < to; ++i)
            worst = std::max(worst, std::abs(v[i] - 2.0f * v[i - 1] + v[i - 2]));
        return worst;
    };
    const std::vector<float> ramped = render(true);
    const std::vector<float> stepped = render(false);
    const size_t echo = kSwitch + kTime;
    // The steady signal's, before the switch and once its echo has passed.
    const float steady =
        std::max(largestSecondDifference(ramped, kSwitch - 6000, kSwitch),
                 largestSecondDifference(ramped, echo + kRamp + 2000, echo + kRamp + 8000));
    const float rampedEcho = largestSecondDifference(ramped, echo - 100, echo + kRamp + 100);
    const float steppedEcho = largestSecondDifference(stepped, echo - 100, echo + kRamp + 100);
    CAPTURE(steady, rampedEcho, steppedEcho);
    REQUIRE(steady > 0.0f);
    CHECK(rampedEcho < 1.5f * steady);
    CHECK(steppedEcho > 10.0f * steady);
    // At weights 0 and 1 the ramp is the switch's law exactly: the two agree before kSwitch, and
    // the loop forgets the ramp only as the feedback decays, so they differ after it.
    CHECK(std::equal(ramped.begin(), ramped.begin() + kSwitch, stepped.begin()));
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
        d.process(l, r, 1.0f, 0.5f, 1.0f, 1.0f);
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
            d.process(l, r, 1.0f, 0.4f, 0.5f, 0.0f);
        REQUIRE(d.timeSamples() == (up ? from : to));

        const double target = up ? to : from;
        d.setTimeSamples(target);
        REQUIRE(d.timeSamples() == (up ? from : to)); // set, but not yet moved
        double before = d.timeSamples();
        size_t calls = 0;
        bool stepsOk = true;
        while (d.timeSamples() != target && calls < 100000)
        {
            d.process(l, r, 1.0f, 0.4f, 0.5f, 0.0f);
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
    d.process(l, r, 1.0f, 0.0f, 1.0f, 0.0f);
    for (int i = 1; i < 3000; ++i)
    {
        l = 0.0f;
        d.process(l, r, 1.0f, 0.0f, 1.0f, 0.0f);
        REQUIRE(l == 0.0f);
    }
    l = 0.0f;
    d.process(l, r, 1.0f, 0.0f, 1.0f, 0.0f);
    CHECK(l == 0.5f);

    // Once it has run, a new target glides.
    d.setTimeSamples(10000.0);
    CHECK(d.timeSamples() == 3000.0);

    // Fill the whole line with noise, looping, so a reset that left anything would show.
    for (uint64_t i = 0; i < static_cast<uint64_t>(2.1 * kSr); ++i)
    {
        l = noise(2, 2 * i);
        r = noise(2, 2 * i + 1);
        d.process(l, r, 1.0f, 0.9f, 0.5f, 1.0f);
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
        d.process(l, r, 1.0f, 0.9f, 0.5f, 1.0f);
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
