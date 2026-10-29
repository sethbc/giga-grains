// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The per-layer diffuser (Space spec §6.2): four allpasses per channel, bypassed bit for bit at
// amount 0, spreading and widening at amount 1 without changing the energy.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Diffuser.h"
#include "gg/core/Rng.h"

using gg::core::Diffuser;

namespace
{
// A uniform draw in [-1, 1) for index i: SplitMix64 over (seed, i), no std:: distributions.
float noise(uint64_t seed, uint64_t i)
{
    return static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(seed * 0x10000000ull + i)));
}

bool isPrime(uint32_t n)
{
    if (n < 2)
        return false;
    for (uint32_t d = 2; d * d <= n; ++d)
        if (n % d == 0)
            return false;
    return true;
}
} // namespace

TEST_CASE("diffuser at zero passes input bit for bit", "[core][space]")
{
    constexpr double sr = 48000.0;
    Diffuser d;
    d.prepare(sr);
    Diffuser ref; // the same input at amount 1 throughout: the allpasses' state continues
    ref.prepare(sr);

    // Fill the allpasses first, so a bypass that leaked their state would show.
    float l = 0.0f, r = 0.0f;
    for (uint64_t i = 0; i < 4800; ++i)
    {
        float a = noise(1, 2 * i), b = noise(1, 2 * i + 1);
        float ra = a, rb = b;
        d.process(a, b, 1.0f);
        ref.process(ra, rb, 1.0f);
    }

    // Random floats plus the awkward ones: ±0, denormals, the smallest normal, ±1e30.
    std::vector<float> in;
    const float specials[] = {0.0f,
                              -0.0f,
                              std::numeric_limits<float>::denorm_min(),
                              -std::numeric_limits<float>::denorm_min(),
                              1.0e-40f,
                              -3.0e-39f,
                              std::numeric_limits<float>::min(),
                              1.0e30f,
                              -1.0e30f};
    for (uint64_t i = 0; i < 2000; ++i)
        in.push_back(i % 5 == 0 ? specials[(i / 5) % std::size(specials)] : noise(2, i));
    REQUIRE(in.size() % 2 == 0);

    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); i += 2)
    {
        l = in[i];
        r = in[i + 1];
        d.process(l, r, 0.0f);
        out[i] = l;
        out[i + 1] = r;
        float ra = in[i], rb = in[i + 1];
        ref.process(ra, rb, 1.0f);
    }
    CHECK(std::memcmp(in.data(), out.data(), in.size() * sizeof(float)) == 0);

    // The allpasses ran through the bypass: back at amount 1, d matches the reference that never
    // stopped, bit for bit.
    for (uint64_t i = 0; i < 4800; ++i)
    {
        float a = noise(3, 2 * i), b = noise(3, 2 * i + 1);
        float ra = a, rb = b;
        d.process(a, b, 1.0f);
        ref.process(ra, rb, 1.0f);
        REQUIRE(std::memcmp(&a, &ra, sizeof a) == 0);
        REQUIRE(std::memcmp(&b, &rb, sizeof b) == 0);
    }
}

TEST_CASE("diffuser at one spreads an impulse", "[core][space]")
{
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        CAPTURE(sr);
        Diffuser d;
        d.prepare(sr);
        const auto n = static_cast<size_t>(sr); // 1 s: the tail is far below -100 dB by then
        const auto early = static_cast<size_t>(4.0 * sr / 1000.0);
        const auto late = static_cast<size_t>(25.0 * sr / 1000.0);

        double total = 0.0, afterEarly = 0.0, afterLate = 0.0, rightEnergy = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            float l = i == 0 ? 1.0f : 0.0f, r = 0.0f;
            d.process(l, r, 1.0f);
            const double e = static_cast<double>(l) * l;
            total += e;
            if (i >= early)
                afterEarly += e;
            if (i >= late)
                afterLate += e;
            rightEnergy += static_cast<double>(r) * r;
        }
        REQUIRE(total > 0.0);
        CHECK(afterEarly >= 0.9 * total);
        CHECK(afterLate > 1.0e-3 * total);
        CHECK(rightEnergy == 0.0); // no crosstalk: R hears only R, so L and R differ
    }

    // The same impulse into both: the two chains' different lengths make L and R differ (width).
    Diffuser d;
    d.prepare(48000.0);
    double diff = 0.0, energy = 0.0;
    for (size_t i = 0; i < 48000; ++i)
    {
        float l = i == 0 ? 1.0f : 0.0f, r = l;
        d.process(l, r, 1.0f);
        diff += (static_cast<double>(l) - r) * (static_cast<double>(l) - r);
        energy += static_cast<double>(l) * l + static_cast<double>(r) * r;
    }
    CHECK(diff > 0.5 * energy); // nearly uncorrelated: 1.0 would be fully, 0 identical
}

TEST_CASE("diffuser lengths are distinct primes", "[core][space]")
{
    constexpr std::array<double, 8> ms{4.1, 5.9, 8.3, 13.7, 4.7, 6.7, 7.9, 12.3};
    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        CAPTURE(sr);
        Diffuser d;
        d.prepare(sr);
        const std::array<uint32_t, 8> len = d.lengths();
        for (size_t i = 0; i < len.size(); ++i)
        {
            CAPTURE(i, len[i]);
            CHECK(isPrime(len[i]));
            // At or just above the nominal length: the next prime (twice at most) isn't far.
            const auto nominal = static_cast<uint32_t>(std::llround(ms[i] * sr / 1000.0));
            CHECK(len[i] >= nominal);
            CHECK(len[i] <= nominal + nominal / 20 + 40);
            for (size_t j = 0; j < i; ++j)
                CHECK(len[i] != len[j]);
        }
    }

    // A worked example at 48 kHz: 4.1 ms is 196.8, rounded to 197, already prime.
    Diffuser d;
    d.prepare(48000.0);
    CHECK(d.lengths()[0] == 197);
}

TEST_CASE("diffuser is energy-preserving at one", "[core][space]")
{
    Diffuser d;
    d.prepare(48000.0);
    double inL = 0.0, inR = 0.0, outL = 0.0, outR = 0.0, changed = 0.0;
    for (uint64_t i = 0; i < 10 * 48000; ++i)
    {
        const float x = noise(7, 2 * i), y = noise(7, 2 * i + 1);
        float l = x, r = y;
        inL += static_cast<double>(x) * x;
        inR += static_cast<double>(y) * y;
        d.process(l, r, 1.0f);
        outL += static_cast<double>(l) * l;
        outR += static_cast<double>(r) * r;
        changed += (static_cast<double>(l) - x) * (static_cast<double>(l) - x);
    }
    CHECK(changed > 0.5 * inL); // it did diffuse: the output isn't the input
    // RMS within 0.5 dB: 10·log10 of the energy ratio.
    const double dbL = 10.0 * std::log10(outL / inL), dbR = 10.0 * std::log10(outR / inR);
    CAPTURE(dbL, dbR);
    CHECK(std::abs(dbL) < 0.5);
    CHECK(std::abs(dbR) < 0.5);
}

TEST_CASE("diffuser reset clears its state", "[core][space]")
{
    Diffuser fresh, used;
    fresh.prepare(48000.0);
    used.prepare(48000.0);
    for (uint64_t i = 0; i < 1000; ++i)
    {
        float l = noise(9, 2 * i), r = noise(9, 2 * i + 1);
        used.process(l, r, 0.5f);
    }
    used.reset();
    for (size_t i = 0; i < 4800; ++i)
    {
        float a = i == 0 ? 1.0f : 0.0f, b = a, fa = a, fb = b;
        used.process(a, b, 1.0f);
        fresh.process(fa, fb, 1.0f);
        REQUIRE(std::memcmp(&a, &fa, sizeof a) == 0);
        REQUIRE(std::memcmp(&b, &fb, sizeof b) == 0);
    }
}
