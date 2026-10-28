// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Interpolate.h"
#include "gg/core/MathDet.h"
#include "gg/core/Rng.h"
#include "gg/core/Window.h"
#include "gg/grain/CloudScheduler.h"
#include "gg/grain/GrainKernels.h"
#include "gg/grain/GrainPool.h"

// Defined in fp_contract_fast.cpp, which is compiled with flags that allow FP contraction and
// inlines every header function it calls, so it leaves no out-of-line copy behind. The calls
// below, compiled here with the project's -ffp-contract=off, are the reference: whatever they
// call out of line was built with the project's flags.
float ggFastLinear(const float *data, int64_t frames, double pos);
float ggFastHermite(const float *data, int64_t frames, double pos);
double ggFastExp2(double x);
double ggFastLog(double x);
float ggFastHann(double phase01);
int ggFastCloud(const gg::CloudParams &params, int64_t frames, gg::GrainDraw *out, int capacity);
void ggFastSampleSimd(bool eco, const gg::GrainRows &rows, uint32_t count,
                      const gg::kernels::SampleSource &src, const gg::kernels::Output &out,
                      int64_t from, int64_t to, const gg::HannTable &hann);
float ggFastCanary(float a, float b, float c);

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
            record(input);
        }
    }

    void record(double input)
    {
        if (count == 0)
        {
            firstInput = input;
        }
        ++count;
    }

    /// The first differing input at full precision, or "-" if there was none.
    std::string first() const
    {
        if (count == 0)
        {
            return "-";
        }
        std::ostringstream os;
        os << std::setprecision(std::numeric_limits<double>::max_digits10) << firstInput;
        return os.str();
    }
};

/// The Cloud scheduler run the fast TU's way, compiled here with the project's flags. The lambda
/// gives this TU its own instantiation of schedule().
int refCloud(const gg::CloudParams &params, int64_t frames, gg::GrainDraw *out, int capacity)
{
    gg::CloudScheduler cloud;
    cloud.prepare(48000.0);
    cloud.setParams(params);
    int count = 0;
    for (int64_t start = 0; start < frames; start += 64)
    {
        cloud.schedule(start, 64, 1234, 0, 0, [&](const gg::GrainDraw &g) {
            if (count < capacity)
            {
                out[count] = g;
            }
            ++count;
        });
    }
    return count;
}

bool sameBits(const gg::GrainDraw &a, const gg::GrainDraw &b)
{
    return a.index == b.index && a.onsetSample == b.onsetSample &&
           bits(a.onsetFrac) == bits(b.onsetFrac) && bits(a.positionDraw) == bits(b.positionDraw) &&
           bits(a.rate) == bits(b.rate) && bits(a.durationSamples) == bits(b.durationSamples) &&
           bits(a.gain) == bits(b.gain) && bits(a.pan) == bits(b.pan) && a.tier == b.tier;
}
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

    INFO("first differing inputs: linear " << linear.first() << ", hermite4 " << hermite.first()
                                           << ", exp2Det " << exp2.first() << ", logDet "
                                           << log.first() << ", HannTable::at " << window.first());
    CHECK(linear.count == 0);
    CHECK(hermite.count == 0);
    CHECK(exp2.count == 0);
    CHECK(log.count == 0);
    CHECK(window.count == 0);
}

TEST_CASE("the Cloud scheduler is not contracted by an includer's flags", "[math][cloud]")
{
    // About 10,000 grains, part Poisson, with spray and stereo, so interval() and the position
    // and pan draws all do arithmetic that could contract.
    gg::CloudParams params;
    params.densityHz = 2000.0;
    params.distribution = 0.37;
    params.position = 0.4;
    params.spray = 0.6;
    params.stereo = 0.8;
    constexpr int64_t kFrames = 5 * 48000;
    constexpr int kCapacity = 20000;
    std::vector<gg::GrainDraw> fast(kCapacity), ref(kCapacity);
    const int fastCount = ggFastCloud(params, kFrames, fast.data(), kCapacity);
    const int refCount = refCloud(params, kFrames, ref.data(), kCapacity);
    REQUIRE(refCount > 9000);
    REQUIRE(refCount <= kCapacity);
    CHECK(fastCount == refCount);

    MismatchCount grains;
    const int n = fastCount < refCount ? fastCount : refCount;
    for (int i = 0; i < n; ++i)
    {
        const auto &a = fast[static_cast<size_t>(i)];
        const auto &b = ref[static_cast<size_t>(i)];
        if (!sameBits(a, b))
        {
            grains.record(static_cast<double>(i));
        }
    }
    INFO("first differing grain: " << grains.first());
    CHECK(grains.count == 0);
}

TEST_CASE("the simd sample kernel is not contracted by an includer's flags", "[math][kernel]")
{
    // 200 stereo grains over a 2^20-frame noise source, rendered over [0, 8192) through
    // renderSampleSimd at both tiers, so its scalar e, phase and pos and its 4-wide window lerp and
    // reads are all compiled with the fast TU's flags. The one expression there that can
    // contract, pos = startPos + rate × e, changes the output only when it moves the read's float
    // fraction. That is likeliest where e and pos are large, so the grains started up to a
    // million samples before the render, and each one's read at sample 0 is anywhere from just
    // before the source to just past it, some crossing its ends. With renderSampleInterior's
    // guard removed, 102 (Hermite) and 92 (Linear) of the 16,384 output samples differ on arm64
    // (Apple clang 21).
    constexpr int64_t kFrames = int64_t{1} << 20;
    constexpr int64_t kLength = 8192;
    constexpr uint32_t kGrains = 200;
    std::vector<float> left(kFrames), right(kFrames);
    for (int64_t i = 0; i < kFrames; ++i)
    {
        left[static_cast<size_t>(i)] =
            static_cast<float>(draw01(6, static_cast<uint64_t>(i)) - 0.5);
        right[static_cast<size_t>(i)] =
            static_cast<float>(draw01(7, static_cast<uint64_t>(i)) - 0.5);
    }
    gg::GrainPool pool(kGrains);
    for (uint32_t k = 0; k < kGrains; ++k)
    {
        const int64_t onset = -1000000 + static_cast<int64_t>(k) * 4000;
        const double onsetFrac = draw01(8, k);
        const double rate = std::exp2(-2.0 + 4.0 * draw01(10, k));
        // The read position at output sample 0.
        const double at0 = -200.0 + (kFrames + 400.0) * draw01(9, k);
        const gg::GrainSpec g{
            .index = k,
            .onsetSample = onset,
            .onsetFrac = onsetFrac,
            .startPos = at0 - rate * (static_cast<double>(-onset) - onsetFrac),
            .rate = rate,
            .durationSamples = 1200000.0,
            .gain = 0.5f,
            .pan = static_cast<float>(2.0 * draw01(12, k) - 1.0),
        };
        REQUIRE(pool.canAdmit(g.onsetSample));
        REQUIRE(pool.spawn(g));
    }
    const gg::kernels::SampleSource src{{left.data(), right.data()}, kFrames, false};
    const gg::HannTable hann;
    for (const bool eco : {false, true})
    {
        INFO((eco ? "Linear" : "Hermite"));
        std::vector<float> fast[2] = {std::vector<float>(kLength), std::vector<float>(kLength)};
        std::vector<float> ref[2] = {std::vector<float>(kLength), std::vector<float>(kLength)};
        ggFastSampleSimd(eco, pool.rows(), kGrains, src, {{fast[0].data(), fast[1].data()}, 0}, 0,
                         kLength, hann);
        const gg::kernels::Output refOut{{ref[0].data(), ref[1].data()}, 0};
        for (uint32_t i = 0; i < kGrains; ++i)
        {
            if (eco)
            {
                gg::kernels::renderSampleSimd<gg::kernels::Linear>(pool.rows(), i, src, refOut, 0,
                                                                   kLength, hann);
            }
            else
            {
                gg::kernels::renderSampleSimd<gg::kernels::Hermite>(pool.rows(), i, src, refOut, 0,
                                                                    kLength, hann);
            }
        }

        MismatchCount samples;
        bool sounds = false;
        for (int c = 0; c < 2; ++c)
        {
            for (int64_t t = 0; t < kLength; ++t)
            {
                const auto j = static_cast<size_t>(t);
                sounds = sounds || ref[c][j] != 0.0f;
                samples.check(static_cast<double>(t), fast[c][j], ref[c][j]);
            }
        }
        REQUIRE(sounds);
        INFO("first differing output sample: " << samples.first());
        CHECK(samples.count == 0);
    }
}

TEST_CASE("the contraction test's fast TU really contracts", "[math]")
{
    // Positive control, as rtsan_negative_control is for RTSan: with a, b = 1 + 2^-12 and
    // c = -(1 + 2^-11), a·b = 1 + 2^-11 + 2^-24 exactly, which rounds (a tie, to even) to
    // 1 + 2^-11, so the unfused a * b + c is 0, while the fused one is exactly 2^-24.
    const float a = 1.0f + 0x1.0p-12f;
    const float c = -(1.0f + 0x1.0p-11f);
    const float unfused = a * a + c; // this TU: -ffp-contract=off
    REQUIRE(unfused == 0.0f);
#if defined(__clang__) && (defined(__aarch64__) || defined(_M_ARM64))
    // arm64 has an FMA and fp_contract_fast.cpp is built with -ffp-contract=on, so the canary
    // must fuse there.
    CHECK(ggFastCanary(a, a, c) == 0x1.0p-24f);
#else
    // x86 at -march=nehalem has no FMA, and under GCC the file keeps the project flags: nothing
    // can contract.
    CHECK(ggFastCanary(a, a, c) == unfused);
#endif
}
