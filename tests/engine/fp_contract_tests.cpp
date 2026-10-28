// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Interpolate.h"
#include "gg/core/MathDet.h"
#include "gg/core/Rng.h"
#include "gg/core/Window.h"

// Defined in fp_contract_fast.cpp, which is compiled with flags that allow FP contraction and
// inlines every header function it calls, so it leaves no out-of-line copy behind. The calls
// below, compiled here with the project's -ffp-contract=off, are the reference: whatever they
// call out of line was built with the project's flags.
float ggFastLinear(const float *data, int64_t frames, double pos);
float ggFastHermite(const float *data, int64_t frames, double pos);
double ggFastExp2(double x);
double ggFastLog(double x);
float ggFastHann(double phase01);

namespace
{
constexpr uint64_t kSeed = 0x46504320u; // "FPC "
constexpr int kInputs = 10000;

/// One draw for input k of a function's stream: each function gets its own stream.
double draw01(uint32_t stream, uint64_t k)
{
    return gg::rng::uniform01(gg::rng::hash({kSeed, 0, stream, gg::rng::Field::SignalNoise, k}));
}

uint32_t bits(float v) { return std::bit_cast<uint32_t>(v); }
uint64_t bits(double v) { return std::bit_cast<uint64_t>(v); }

/// Counts the inputs whose two results differ in any bit, and keeps the first such input.
struct MismatchCount
{
    int count = 0;
    double firstInput = 0.0;

    template <typename T> void check(double input, T fast, T ref)
    {
        if (bits(fast) != bits(ref))
        {
            if (count == 0)
            {
                firstInput = input;
            }
            ++count;
        }
    }
};
} // namespace

TEST_CASE("inline math is not contracted by an includer's flags", "[math]")
{
    // A random signal for the interpolators, read across its whole span and a little past each
    // end, where the taps read as zero.
    constexpr int64_t kFrames = 64;
    std::array<float, kFrames> signal{};
    for (int64_t i = 0; i < kFrames; ++i)
    {
        signal[static_cast<size_t>(i)] =
            static_cast<float>(2.0 * draw01(0, static_cast<uint64_t>(i)) - 1.0);
    }
    const gg::HannTable hann;

    MismatchCount linear, hermite, exp2, log, window;
    for (int k = 0; k < kInputs; ++k)
    {
        const auto uk = static_cast<uint64_t>(k);

        const double pos = -3.0 + (kFrames + 6.0) * draw01(1, uk);
        linear.check(pos, ggFastLinear(signal.data(), kFrames, pos),
                     gg::interp::linear(signal.data(), kFrames, pos));
        hermite.check(pos, ggFastHermite(signal.data(), kFrames, pos),
                      gg::interp::hermite4(signal.data(), kFrames, pos));

        const double x = -40.0 + 80.0 * draw01(2, uk); // pitch ratios and beyond
        exp2.check(x, ggFastExp2(x), gg::exp2Det(x));

        // (0, 1], as the Cloud scheduler's Poisson intervals use, scaled by 2^-30 .. 2^30.
        const int scale = static_cast<int>(std::floor(61.0 * draw01(3, uk))) - 30;
        const double y = std::ldexp(1.0 - draw01(4, uk), scale);
        log.check(y, ggFastLog(y), gg::logDet(y));

        const double phase = draw01(5, uk);
        window.check(phase, ggFastHann(phase), hann.at(phase));
    }

    INFO("first differing inputs: linear " << linear.firstInput << ", hermite4 "
                                           << hermite.firstInput << ", exp2Det " << exp2.firstInput
                                           << ", logDet " << log.firstInput << ", HannTable::at "
                                           << window.firstInput);
    CHECK(linear.count == 0);
    CHECK(hermite.count == 0);
    CHECK(exp2.count == 0);
    CHECK(log.count == 0);
    CHECK(window.count == 0);
}
