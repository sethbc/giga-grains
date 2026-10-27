// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Kernels.h"
#include "gg/core/Rng.h"
#include "gg/core/Window.h"

namespace
{
using gg::bench::GrainBatch;
using gg::bench::Kernel;

constexpr uint64_t kSeed = 0x62656e6368746573ull;

double draw(uint32_t stream, uint64_t index)
{
    return gg::rng::uniform01(
        gg::rng::hash({kSeed, 0, stream, gg::rng::Field::SignalNoise, index}));
}

std::vector<float> noise(int64_t frames, uint32_t stream)
{
    std::vector<float> v(static_cast<std::size_t>(frames));
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        v[i] = static_cast<float>(0.5 * gg::rng::bipolar(gg::rng::hash(
                                            {kSeed, 0, stream, gg::rng::Field::SignalNoise, i})));
    }
    return v;
}

float maxAbs(const std::vector<float> &v)
{
    float m = 0.0f;
    for (float x : v)
    {
        m = std::max(m, std::fabs(x));
    }
    return m;
}

float maxAbsDiff(const std::vector<float> &a, const std::vector<float> &b)
{
    float m = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        m = std::max(m, std::fabs(a[i] - b[i]));
    }
    return m;
}
} // namespace

TEST_CASE("simd kernels match scalar reference", "[bench]")
{
    constexpr int64_t kFrames = 10 * 48000; // 10 s at 48 kHz
    constexpr uint32_t kGrains = 64;
    const auto srcL = noise(kFrames, 0);
    const auto srcR = noise(kFrames, 1);

    GrainBatch batch;
    batch.resize(kGrains);
    for (uint32_t g = 0; g < kGrains; ++g)
    {
        batch.pos[g] = draw(2, g) * static_cast<double>(kFrames);
        batch.rate[g] = 0.5 + 1.5 * draw(3, g);
        batch.envPhase[g] = static_cast<float>(draw(4, g));
        batch.envInc[g] = 1.0f / 4800.0f; // 100 ms grains at 48 kHz
        batch.gain[g] = static_cast<float>(0.25 + 0.75 * draw(5, g));
    }
    // Some grains straddle the ends of the buffer, so the kernels' bounds-checked paths run too,
    // and one grain's envelope ends partway through the block.
    batch.pos[0] = -3.7;
    batch.pos[1] = 0.25;
    batch.pos[2] = static_cast<double>(kFrames) - 100.6;
    batch.pos[3] = static_cast<double>(kFrames) - 1.5;
    batch.envPhase[4] = 1.0f - 100.0f / 4800.0f;

    struct Pair
    {
        const char *name;
        Kernel simd, scalar;
    };
    const Pair pairs[] = {
        {"linear", gg::bench::renderLinearSimd, gg::bench::renderLinearScalar},
        {"hermite", gg::bench::renderHermiteSimd, gg::bench::renderHermiteScalar},
    };

    // 37 is not a multiple of 4, so it also covers the samples after the last SIMD step.
    for (const uint32_t n : {256u, 37u})
    {
        for (const auto &p : pairs)
        {
            INFO(p.name << ", block " << n);
            std::vector<float> refL(n), refR(n), outL(n), outR(n);
            p.scalar(batch, srcL.data(), srcR.data(), kFrames, refL.data(), refR.data(), n);
            p.simd(batch, srcL.data(), srcR.data(), kFrames, outL.data(), outR.data(), n);

            const float peakL = maxAbs(refL);
            const float peakR = maxAbs(refR);
            REQUIRE(peakL > 0.0f);
            REQUIRE(peakR > 0.0f);
            CHECK(maxAbsDiff(outL, refL) <= 1e-5f * peakL);
            CHECK(maxAbsDiff(outR, refR) <= 1e-5f * peakR);
        }
    }
}

TEST_CASE("sinc kernel reconstructs a smooth signal", "[bench]")
{
    // A sine at 0.01 cycles per sample sits deep in the passband, so the windowed sinc should
    // reproduce it closely. A tap or phase-table misalignment of even 1/50 sample would not.
    constexpr int64_t kFrames = 4096;
    constexpr double kCycles = 0.01;
    std::vector<float> srcL(kFrames), srcR(kFrames);
    for (int64_t i = 0; i < kFrames; ++i)
    {
        const double w = 2.0 * std::numbers::pi * kCycles * static_cast<double>(i);
        srcL[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(w));
        srcR[static_cast<std::size_t>(i)] = static_cast<float>(std::cos(w));
    }

    GrainBatch batch;
    batch.resize(1);
    batch.pos[0] = 1000.3;
    batch.rate[0] = 0.73;
    batch.envPhase[0] = 0.25f;
    batch.envInc[0] = 1.0f / 4800.0f;
    batch.gain[0] = 0.8f;

    constexpr uint32_t n = 256;
    std::vector<float> outL(n), outR(n);
    gg::bench::renderSincScalar(batch, srcL.data(), srcR.data(), kFrames, outL.data(), outR.data(),
                                n);

    const gg::HannTable hann;
    float worst = 0.0f;
    for (uint32_t i = 0; i < n; ++i)
    {
        const double p = batch.pos[0] + static_cast<double>(i) * batch.rate[0];
        const float amp =
            hann.at(batch.envPhase[0] + static_cast<float>(i) * batch.envInc[0]) * batch.gain[0];
        const double w = 2.0 * std::numbers::pi * kCycles * p;
        worst = std::max(worst, std::fabs(outL[i] - amp * static_cast<float>(std::sin(w))));
        worst = std::max(worst, std::fabs(outR[i] - amp * static_cast<float>(std::cos(w))));
    }
    INFO("max error " << worst);
    REQUIRE(maxAbs(outL) > 0.5f);
    REQUIRE(worst <= 1e-4f);
}

TEST_CASE("kernels read outside the source as silence", "[bench]")
{
    constexpr int64_t kFrames = 1000;
    const auto srcL = noise(kFrames, 0);
    const auto srcR = noise(kFrames, 1);

    // Every tap of every kernel is out of range for both grains for the whole block.
    GrainBatch batch;
    batch.resize(2);
    batch.pos = {-500.0, static_cast<double>(kFrames) + 20.0};
    batch.rate = {1.0, 1.0};
    batch.envPhase = {0.25f, 0.25f};
    batch.envInc = {1.0f / 4800.0f, 1.0f / 4800.0f};
    batch.gain = {1.0f, 1.0f};

    const Kernel kernels[] = {gg::bench::renderLinearScalar, gg::bench::renderHermiteScalar,
                              gg::bench::renderSincScalar, gg::bench::renderLinearSimd,
                              gg::bench::renderHermiteSimd};
    for (std::size_t k = 0; k < std::size(kernels); ++k)
    {
        INFO("kernel " << k);
        constexpr uint32_t n = 64;
        std::vector<float> outL(n, 0.0f), outR(n, 0.0f);
        kernels[k](batch, srcL.data(), srcR.data(), kFrames, outL.data(), outR.data(), n);
        REQUIRE(maxAbs(outL) == 0.0f);
        REQUIRE(maxAbs(outR) == 0.0f);
    }
}
