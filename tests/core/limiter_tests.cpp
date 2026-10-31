// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The global limiter (extremes spec §6.5): Off the identity bit for bit, Soft M1's law scaled to
// the ceiling, Peak a stereo-linked zero-lookahead limiter that never passes the ceiling, and a
// crossfade on every mode change.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Limiter.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Rng.h"

using gg::core::Limiter;
using gg::core::LimiterMode;

namespace
{
constexpr double kSr = 48000.0;
constexpr uint32_t kRamp = 480; // the Processor's 10 ms at 48 kHz

// A uniform draw in [-1, 1) for index i: SplitMix64 over (seed, i), no std:: distributions.
float noise(uint64_t seed, uint64_t i)
{
    return static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(seed * 0x10000000ull + i)));
}

// The per-sample release coefficient of a one-pole with time constant seconds.
float releaseCoeff(double seconds)
{
    return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * kSr)));
}

// A stereo sine, interleaved: amplitude a, frequency f, the right channel a quarter turn on.
std::vector<float> sine(size_t frames, float a, double f)
{
    std::vector<float> v(2 * frames);
    for (size_t i = 0; i < frames; ++i)
    {
        const double ph = 2.0 * std::numbers::pi * f * static_cast<double>(i) / kSr;
        v[2 * i] = a * static_cast<float>(std::sin(ph));
        v[2 * i + 1] = a * static_cast<float>(std::cos(ph));
    }
    return v;
}

// The largest sample-to-sample step of channel ch (0 or 1) of an interleaved buffer, over frames
// [from, to).
float largestStep(const std::vector<float> &v, int ch, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t>(from, 1); i < to; ++i)
        m = std::max(m, std::abs(v[2 * i + ch] - v[2 * (i - 1) + ch]));
    return m;
}
} // namespace

TEST_CASE("off is the identity", "[core][fx]")
{
    // Stereo noise up to ±4, with the awkward values among it: ±0, denormals, ±inf and NaN.
    const float specials[] = {0.0f,
                              -0.0f,
                              std::numeric_limits<float>::denorm_min(),
                              -std::numeric_limits<float>::denorm_min(),
                              -3.0e-39f,
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()};
    const size_t frames = 24000;
    std::vector<float> in(2 * frames);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = i % 7 == 0 ? specials[(i / 7) % std::size(specials)] : 4.0f * noise(2, i);

    const auto runOff = [&](Limiter &lim) {
        std::vector<float> out(in.size());
        for (size_t i = 0; i < frames; ++i)
        {
            float l = in[2 * i], r = in[2 * i + 1];
            lim.process(l, r, LimiterMode::Off, 0.5f, 0.01f);
            out[2 * i] = l;
            out[2 * i + 1] = r;
        }
        return out;
    };

    SECTION("from the start")
    {
        Limiter lim;
        lim.prepare(kSr, kRamp);
        const auto out = runOff(lim);
        CHECK(std::memcmp(in.data(), out.data(), in.size() * sizeof(float)) == 0);
    }
    SECTION("once a fade out of Peak has ended")
    {
        Limiter lim;
        lim.prepare(kSr, kRamp);
        for (size_t i = 0; i < 2000; ++i)
        {
            float l = 3.0f * noise(3, 2 * i), r = 3.0f * noise(3, 2 * i + 1);
            lim.process(l, r, LimiterMode::Peak, 0.5f, 0.01f);
        }
        CHECK(lim.gain() < 1.0f);
        // Off's fade: kRamp calls, the last of which is Off's alone.
        for (uint32_t i = 0; i < kRamp; ++i)
        {
            float l = 0.0f, r = 0.0f;
            lim.process(l, r, LimiterMode::Off, 0.5f, 0.01f);
        }
        CHECK(lim.gain() == 1.0f);
        const auto out = runOff(lim);
        CHECK(std::memcmp(in.data(), out.data(), in.size() * sizeof(float)) == 0);
    }
}

TEST_CASE("soft is M1's law at the ceiling", "[core][fx]")
{
    const size_t frames = 48000;
    for (const float ceiling : {1.0f, 0.5f, 0.0630957f})
    {
        CAPTURE(ceiling);
        Limiter lim;
        lim.prepare(kSr, 0); // no fade: the first call is Soft's alone
        for (size_t i = 0; i < frames; ++i)
        {
            const float x = 3.0f * noise(4, 2 * i), y = 0.2f * noise(4, 2 * i + 1);
            float l = x, r = y;
            lim.process(l, r, LimiterMode::Soft, ceiling, 0.01f);
            CAPTURE(i, x, y);
            if (ceiling == 1.0f)
            {
                // Bit for bit M1's limiter.
                const float sx = gg::core::softLimit(x);
                REQUIRE(std::memcmp(&l, &sx, sizeof(float)) == 0);
                const float sy = gg::core::softLimit(y);
                REQUIRE(std::memcmp(&r, &sy, sizeof(float)) == 0);
            }
            REQUIRE(l == ceiling * gg::core::softLimit(x / ceiling));
            REQUIRE(r == ceiling * gg::core::softLimit(y / ceiling));
            REQUIRE(std::abs(l) < ceiling);
            REQUIRE(std::abs(r) < ceiling);
        }
    }
}

TEST_CASE("peak never passes the ceiling and attacks at once", "[core][fx]")
{
    SECTION("a 1.0 step after silence, ceiling 0.5")
    {
        Limiter lim;
        lim.prepare(kSr, kRamp);
        // Silence through the fade into Peak, and past it.
        for (size_t i = 0; i < 1000; ++i)
        {
            float l = 0.0f, r = 0.0f;
            lim.process(l, r, LimiterMode::Peak, 0.5f, releaseCoeff(0.1));
            REQUIRE(l == 0.0f);
            REQUIRE(r == 0.0f);
        }
        CHECK(lim.gain() == 1.0f);
        for (size_t i = 0; i < 4800; ++i)
        {
            float l = 1.0f, r = 1.0f;
            lim.process(l, r, LimiterMode::Peak, 0.5f, releaseCoeff(0.1));
            CAPTURE(i);
            if (i == 0)
            {
                // No lookahead and no attack time: the step's first sample is at the ceiling.
                REQUIRE(l == 0.5f);
                REQUIRE(r == 0.5f);
            }
            REQUIRE(l <= 0.5f);
            REQUIRE(r <= 0.5f);
        }
    }
    SECTION("stereo-linked: the louder channel sets the gain of both")
    {
        Limiter lim;
        lim.prepare(kSr, 0);
        float l = 0.25f, r = -2.0f;
        lim.process(l, r, LimiterMode::Peak, 0.5f, 0.0f);
        CHECK(lim.gain() == 0.25f);
        CHECK(r == -0.5f);
        CHECK(l == 0.0625f);
    }
    SECTION("hot noise at -1 dB, every sample")
    {
        const float ceiling = 0.891250938f;
        for (const float k : {0.0f, releaseCoeff(0.01), releaseCoeff(1.0), 1.0f})
        {
            CAPTURE(k);
            Limiter lim;
            lim.prepare(kSr, 0);
            for (size_t i = 0; i < 96000; ++i)
            {
                const float a = 0.5f + 7.5f * (0.5f + 0.5f * noise(6, i));
                float l = a * noise(5, 2 * i), r = a * noise(5, 2 * i + 1);
                lim.process(l, r, LimiterMode::Peak, ceiling, k);
                CAPTURE(i);
                REQUIRE(std::abs(l) <= ceiling);
                REQUIRE(std::abs(r) <= ceiling);
                REQUIRE(lim.gain() <= 1.0f);
                REQUIRE(lim.gain() > 0.0f);
            }
        }
    }
}

TEST_CASE("peak releases with the coefficient", "[core][fx]")
{
    for (const float k : {releaseCoeff(0.01), releaseCoeff(0.1), releaseCoeff(1.0)})
    {
        CAPTURE(k);
        Limiter lim;
        lim.prepare(kSr, 0);
        for (size_t i = 0; i < 480; ++i)
        {
            float l = 1.0f, r = -1.0f;
            lim.process(l, r, LimiterMode::Peak, 0.5f, k);
        }
        const double g0 = lim.gain();
        CHECK(g0 == 0.5);
        double decay = 1.0; // (1 − k)^n
        for (size_t n = 1; n <= 96000; ++n)
        {
            float l = 0.1f * noise(7, 2 * n), r = 0.1f * noise(7, 2 * n + 1);
            lim.process(l, r, LimiterMode::Peak, 0.5f, k);
            decay *= 1.0 - static_cast<double>(k);
            CAPTURE(n);
            REQUIRE(std::abs(static_cast<double>(lim.gain()) - (1.0 - (1.0 - g0) * decay)) <= 1e-6);
        }
    }
}

TEST_CASE("a mode change crossfades", "[core][fx]")
{
    // A hot stereo sine, Off for 0.1 s, then Peak at 0.5 (-6 dB). The sine's own largest step
    // bounds every step over the fade, and from kRamp − 1 calls after the switch the output is
    // Peak's own, bit for bit: a Limiter with no fade switched to Peak at the same sample.
    const size_t frames = 24000, at = 4800;
    const float ceiling = 0.5f, k = releaseCoeff(0.1);
    const auto in = sine(frames, 2.0f, 997.0);
    const float own = std::max(largestStep(in, 0, 0, frames), largestStep(in, 1, 0, frames));

    Limiter lim, ref;
    lim.prepare(kSr, kRamp);
    ref.prepare(kSr, 0);
    std::vector<float> out(in.size()), peak(in.size());
    for (size_t i = 0; i < frames; ++i)
    {
        float l = in[2 * i], r = in[2 * i + 1];
        lim.process(l, r, i < at ? LimiterMode::Off : LimiterMode::Peak, ceiling, k);
        out[2 * i] = l;
        out[2 * i + 1] = r;
        if (i >= at)
        {
            float pl = in[2 * i], pr = in[2 * i + 1];
            ref.process(pl, pr, LimiterMode::Peak, ceiling, k);
            peak[2 * i] = pl;
            peak[2 * i + 1] = pr;
        }
    }
    // Off before the switch: the input itself.
    CHECK(std::memcmp(in.data(), out.data(), 2 * at * sizeof(float)) == 0);
    // Over the fade, no step larger than the sine's own.
    CHECK(largestStep(out, 0, at - 1, at + kRamp + 1) <= own);
    CHECK(largestStep(out, 1, at - 1, at + kRamp + 1) <= own);
    // Half-way, neither law alone: the output is between them.
    {
        const size_t mid = at + kRamp / 2;
        const float lo = std::min(in[2 * mid], peak[2 * mid]);
        const float hi = std::max(in[2 * mid], peak[2 * mid]);
        CHECK(out[2 * mid] > lo);
        CHECK(out[2 * mid] < hi);
    }
    // From the fade's last call, Peak's exactly.
    const size_t end = at + kRamp - 1;
    CHECK(std::memcmp(out.data() + 2 * end, peak.data() + 2 * end,
                      2 * (frames - end) * sizeof(float)) == 0);
    CHECK(std::memcmp(out.data() + 2 * (end - 1), peak.data() + 2 * (end - 1), 2 * sizeof(float)) !=
          0);
}

TEST_CASE("a mode change in the middle of a fade stays continuous", "[core][fx]")
{
    // Off, then Peak, back to Off a third of the way through Peak's fade, then Soft a third of
    // the way through that one: every step stays within the sine's own, and kRamp calls after
    // the last change the output is Soft's exactly.
    const size_t frames = 12000, t1 = 2000, t2 = t1 + kRamp / 3, t3 = t2 + kRamp / 3;
    const float ceiling = 0.5f, k = releaseCoeff(0.1);
    const auto in = sine(frames, 2.0f, 997.0);
    const float own = std::max(largestStep(in, 0, 0, frames), largestStep(in, 1, 0, frames));

    Limiter lim;
    lim.prepare(kSr, kRamp);
    std::vector<float> out(in.size());
    for (size_t i = 0; i < frames; ++i)
    {
        const LimiterMode m = i < t1   ? LimiterMode::Off
                              : i < t2 ? LimiterMode::Peak
                              : i < t3 ? LimiterMode::Off
                                       : LimiterMode::Soft;
        float l = in[2 * i], r = in[2 * i + 1];
        lim.process(l, r, m, ceiling, k);
        out[2 * i] = l;
        out[2 * i + 1] = r;
    }
    CHECK(largestStep(out, 0, 0, frames) <= own);
    CHECK(largestStep(out, 1, 0, frames) <= own);
    for (size_t i = t3 + kRamp - 1; i < frames; ++i)
    {
        CAPTURE(i);
        REQUIRE(out[2 * i] == ceiling * gg::core::softLimit(in[2 * i] / ceiling));
        REQUIRE(out[2 * i + 1] == ceiling * gg::core::softLimit(in[2 * i + 1] / ceiling));
    }
    // Peak's gain went back to 1 once its weight reached 0.
    CHECK(lim.gain() == 1.0f);
}

TEST_CASE("reset keeps the mode and ends the fade", "[core][fx]")
{
    Limiter lim;
    lim.prepare(kSr, kRamp);
    for (size_t i = 0; i < 100; ++i)
    {
        float l = 2.0f, r = 2.0f;
        lim.process(l, r, LimiterMode::Peak, 0.5f, 0.0f);
    }
    lim.reset();
    CHECK(lim.gain() == 1.0f);
    // No fade after the reset: Peak is still the mode, so the next call is Peak's alone.
    float l = 2.0f, r = 1.0f;
    lim.process(l, r, LimiterMode::Peak, 0.5f, 0.0f);
    CHECK(l == 0.5f);
    CHECK(r == 0.25f);
}

TEST_CASE("jump snaps the mode with no fade", "[core][fx]")
{
    // A fade from Off would pass 10 ms of the first samples unlimited; jump makes the first
    // sample the mode's law alone.
    constexpr float kCeiling = 0.25f;
    Limiter lim;
    lim.prepare(kSr, kRamp);
    lim.jump(LimiterMode::Peak);
    float l = 1.0f, r = -0.5f;
    lim.process(l, r, LimiterMode::Peak, kCeiling, releaseCoeff(0.1));
    CHECK(std::abs(l) <= kCeiling);
    CHECK(std::abs(r) <= kCeiling);
    CHECK_FALSE(lim.isOff());

    lim.jump(LimiterMode::Soft);
    l = 1.0f;
    r = 0.0f;
    lim.process(l, r, LimiterMode::Soft, kCeiling, releaseCoeff(0.1));
    CHECK(l < kCeiling);
    CHECK(lim.gain() == 1.0f);

    lim.jump(LimiterMode::Off);
    CHECK(lim.isOff());
    l = 3.0f;
    lim.process(l, r, LimiterMode::Off, kCeiling, releaseCoeff(0.1));
    CHECK(l == 3.0f);
}
