// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The HQ tier's windowed sinc table (spec §6.2): its shape, and the frequency response of the
// 16-tap kernel the Sinc reader builds from it.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Rng.h"
#include "gg/grain/SincTable.h"

namespace
{
const gg::SincTable &table()
{
    static const gg::SincTable t;
    return t;
}

// A uniform draw in [0, 1) from gg::rng, one stream per quantity.
double u01(uint32_t stream, uint64_t index)
{
    return gg::rng::uniform01(gg::rng::hash({97, 0, stream, gg::rng::Field::SignalNoise, index}));
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// The kernel's coefficients for one read, as the Sinc reader computes them (GrainKernels.h): for
// taps k = −7 … 8, c_k = at((float(k) − frac) × stretch), in float. Returned with their sum, the
// reader's denominator, in double.
struct Phase
{
    double frac;
    double c[gg::SincTable::kTaps];
    double sum;
};
Phase coefficients(float frac, float stretch)
{
    Phase p{frac, {}, 0.0};
    for (int64_t k = -gg::SincTable::kBefore; k <= gg::SincTable::kAfter; ++k)
    {
        const double c = table().at((static_cast<float>(k) - frac) * stretch);
        p.c[k + gg::SincTable::kBefore] = c;
        p.sum += c;
    }
    return p;
}

// The kernel's frequency response at f (in units of the source rate fs), over `phases` evenly
// spaced fractions frac = p / phases: the interpolator that reads the source at every one of them
// upsamples it by `phases`, and this is that upsampler's response, each phase normalized by its
// own sum as the reader does, in double. The source's spectrum is passed below 0.5 fs; its images
// sit around fs, 2 fs, … up to the response's period, phases × fs. With real coefficients |H| is
// even about every multiple of (phases / 2) × fs, so for 2 phases [0, fs] covers the period.
struct Response
{
    Response(float stretch, int phases)
    {
        for (int p = 0; p < phases; ++p)
        {
            // Exact: phases is a power of two.
            set.push_back(
                coefficients(static_cast<float>(p) / static_cast<float>(phases), stretch));
        }
    }
    double db(double f) const
    {
        std::complex<double> h = 0.0;
        for (const Phase &p : set)
        {
            for (int64_t k = -gg::SincTable::kBefore; k <= gg::SincTable::kAfter; ++k)
            {
                const double offset = static_cast<double>(k) - p.frac;
                // c × e^(−i 2π f offset); std::polar would refuse a negative c.
                const double angle = -2.0 * std::numbers::pi * f * offset;
                h += (p.c[k + gg::SincTable::kBefore] / p.sum) *
                     std::complex<double>(std::cos(angle), std::sin(angle));
            }
        }
        return 20.0 * std::log10(std::abs(h) / static_cast<double>(set.size()));
    }
    // The largest |dB| over f in [lo, hi], in steps of step, and the largest dB.
    double worstDeviation(double lo, double hi, double step) const
    {
        double worst = 0.0;
        for (double f = lo; f <= hi + 1e-12; f += step)
        {
            const double d = db(f);
            if (!std::isfinite(d))
            {
                return std::numeric_limits<double>::quiet_NaN(); // fails the pin
            }
            worst = std::max(worst, std::abs(d));
        }
        return worst;
    }
    double peak(double lo, double hi, double step) const
    {
        double worst = -std::numeric_limits<double>::infinity();
        for (double f = lo; f <= hi + 1e-12; f += step)
        {
            const double d = db(f);
            if (std::isnan(d))
            {
                return d; // fails the pin
            }
            worst = std::max(worst, d);
        }
        return worst;
    }
    std::vector<Phase> set;
};
} // namespace

TEST_CASE("the sinc table's constants are the spec's", "[kernel][interp]")
{
    REQUIRE(gg::SincTable::kTaps == 16);
    REQUIRE(gg::SincTable::kBefore == 7);
    REQUIRE(gg::SincTable::kAfter == 8);
    REQUIRE(gg::SincTable::kPointsPerUnit == 512);
    REQUIRE(gg::SincTable::kPoints == 16 * 512 + 1);
    REQUIRE(gg::SincTable::kBeta == 8.0);
}

TEST_CASE("the sinc table is symmetric, unity at zero and zero at the integers", "[kernel][interp]")
{
    const gg::SincTable &t = table();
    REQUIRE(sameBits(t.at(0.0f), 1.0f));
    REQUIRE(sameBits(t.at(-0.0f), 1.0f));
    REQUIRE(sameBits(t.points()[8 * gg::SincTable::kPointsPerUnit], 1.0f));
    for (int k = 1; k <= 8; ++k)
    {
        INFO("x = +-" << k);
        REQUIRE(std::abs(t.at(static_cast<float>(k))) <= 1e-6f);
        REQUIRE(std::abs(t.at(static_cast<float>(-k))) <= 1e-6f);
    }
    // Nothing outside [−8, 8], NaN included.
    for (const float x :
         {8.001f, -8.001f, 100.0f, std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        INFO("x = " << x);
        REQUIRE(sameBits(t.at(x), 0.0f));
    }
    // The tabulated points are symmetric bit for bit, so at() is too wherever it lands on one:
    // x = m / 512 with |m| < 8 × 512 (at ±8 it lerps to the last point from the one before).
    for (uint32_t j = 0; j < gg::SincTable::kPoints; ++j)
    {
        if (!sameBits(t.points()[j], t.points()[gg::SincTable::kPoints - 1 - j]))
        {
            INFO("point " << j);
            REQUIRE(sameBits(t.points()[j], t.points()[gg::SincTable::kPoints - 1 - j]));
        }
    }
    for (uint64_t n = 0; n < 1000; ++n)
    {
        const auto m = static_cast<int>(-4095.0 + 8191.0 * u01(0, n));
        const float x = static_cast<float>(m) / 512.0f;
        INFO("x = " << m << " / 512");
        REQUIRE(sameBits(t.at(x), t.at(-x)));
    }
    // Between the points, at(x) and at(−x) differ only by the rounding of x + 8 and of the lerp.
    for (uint64_t n = 0; n < 1000; ++n)
    {
        const auto x = static_cast<float>(-8.0 + 16.0 * u01(1, n));
        INFO("x = " << x);
        REQUIRE(std::abs(t.at(x) - t.at(-x)) <= 1e-5f);
    }
}

TEST_CASE("the sinc table reads its points with the documented lerp", "[kernel][interp]")
{
    // at(x) = p[j] + (p[j + 1] − p[j]) × t with u = (x + 8) × 512, j = min(uint32(u), kPoints −
    // 2) and t = u − float(j), in float: the order the SIMD kernels must keep.
    const gg::SincTable &t = table();
    const float *p = t.points();
    for (uint64_t n = 0; n < 10000; ++n)
    {
        const auto x = static_cast<float>(-8.0 + 16.0 * u01(2, n));
        const float u = (x + 8.0f) * 512.0f;
        const uint32_t j = std::min(static_cast<uint32_t>(u), gg::SincTable::kPoints - 2);
        const float frac = u - static_cast<float>(j);
        const float want = p[j] + (p[j + 1] - p[j]) * frac;
        INFO("x = " << x);
        REQUIRE(sameBits(t.at(x), want));
    }
    REQUIRE(sameBits(t.at(8.0f),
                     p[gg::SincTable::kPoints - 2] +
                         (p[gg::SincTable::kPoints - 1] - p[gg::SincTable::kPoints - 2]) * 1.0f));
    REQUIRE(sameBits(t.at(-8.0f), p[0]));
}

TEST_CASE("the sinc kernel's response meets its pins", "[kernel][interp]")
{
    // Spec §6.2: at stretch 1, the passband within 0.1 dB of unity to 0.3 fs and the first image
    // at least 70 dB down from 0.7 fs. The brief's pin is the 16 coefficients at frac 0.5, whose
    // response, with frac 0's (the source itself), is the half-sample upsampler's.
    const Response half(1.0f, 2);
    const double pass = half.worstDeviation(0.0, 0.3, 0.001);
    const double stop = half.peak(0.7, 1.0, 0.001);
    INFO("frac 0 and 0.5: passband within " << pass << " dB, stopband " << stop << " dB");
    CHECK(pass <= 0.1);
    CHECK(stop <= -70.0);

    // The same pins over 64 fractions, which read between the table's points, so the table's
    // lerp is in the response too; the first four images.
    const Response dense(1.0f, 64);
    const double densePass = dense.worstDeviation(0.0, 0.3, 0.005);
    const double denseStop = dense.peak(0.7, 4.3, 0.005);
    INFO("64 fractions: passband within " << densePass << " dB, stopband " << denseStop << " dB");
    CHECK(densePass <= 0.1);
    CHECK(denseStop <= -70.0);
}

TEST_CASE("a stretched kernel's cutoff follows the stretch", "[kernel][interp]")
{
    // At stretch s the coefficients are the table at (k − frac) × s, a sinc whose cutoff is
    // s × 0.5 fs: the half-amplitude point moves with s, and at s = 0.5 what the unstretched
    // kernel passes at 0.35 fs is far down. Only 16 taps cover the stretched kernel, so its window
    // is cut at |x| = 8 × s: its stopband is shallower and its passband less flat than at s = 1.
    // Measured at s = 0.5: 37.0 dB down from 0.35 fs, and within 0.30 dB of unity to 0.125 fs.
    // The brief's 70 dB from 0.35 fs is out of reach of 16 taps at s = 0.5 for any Kaiser β
    // (about 40 dB at β = 10); see the task report.
    const Response unstretched(1.0f, 2);
    REQUIRE(std::abs(unstretched.db(0.35)) <= 0.1);
    for (const float s : {1.0f, 0.75f, 0.6f, 0.5f})
    {
        const Response r(s, 2);
        const double pass = r.worstDeviation(0.0, 0.25 * s, 0.001);
        INFO("stretch " << s << ": " << r.db(0.5 * s) << " dB at " << 0.5 * s << " fs, passband to "
                        << 0.25 * s << " fs within " << pass << " dB");
        REQUIRE(std::abs(r.db(0.5 * s) + 6.02) <= 0.5);
        REQUIRE(pass <= 0.5);
    }
    const Response stretched(0.5f, 2);
    const double stop = stretched.peak(0.35, 1.0, 0.001);
    INFO("stretch 0.5: " << stop << " dB from 0.35 fs");
    REQUIRE(stop <= -35.0);
}
