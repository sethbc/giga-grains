// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The scalar reference renderers (spec §3, ruling 1). Each test writes the spec's table out
// as its own per-sample loop and requires the reference to produce the same bits; the SIMD
// kernels of later tasks are held to the reference the same way.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <sst/cpputils/rtsan_support.h>

#include "gg/core/Interpolate.h"
#include "gg/core/Rng.h"
#include "gg/core/Window.h"
#include "gg/grain/GrainKernels.h"
#include "gg/grain/GrainPool.h"
#include "gg/source/LiveSource.h"

namespace
{
using gg::kernels::Hermite;
using gg::kernels::Linear;

// A sample-buffer interpolator: interp::hermite4 (Normal) or interp::linear (Eco).
using SampleRead = float (*)(const float *, int64_t, double);

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// Uniform noise in [-0.5, 0.5) from gg::rng, one stream per channel.
std::vector<float> noise(int64_t frames, uint64_t seed, uint32_t channel)
{
    std::vector<float> v(static_cast<std::size_t>(frames));
    for (int64_t i = 0; i < frames; ++i)
    {
        using gg::rng::Field;
        v[static_cast<std::size_t>(i)] =
            0.5f * static_cast<float>(gg::rng::bipolar(gg::rng::hash(
                       {seed, 0, channel, Field::SignalNoise, static_cast<uint64_t>(i)})));
    }
    return v;
}

// The brief's grain: onsetFrac 0.37, rate 1.5, duration 480, pan 0.4, gain 0.8.
gg::GrainSpec testGrain(int64_t onsetSample, double startPos)
{
    return {
        .index = 0,
        .onsetSample = onsetSample,
        .onsetFrac = 0.37,
        .startPos = startPos,
        .rate = 1.5,
        .durationSamples = 480.0,
        .gain = 0.8f,
        .pan = 0.4f,
    };
}

// The pool's rows for the single grain g, latched by spawn as the engine's are.
struct OneGrain
{
    explicit OneGrain(const gg::GrainSpec &g) : pool(4)
    {
        REQUIRE(pool.canAdmit(g.onsetSample));
        REQUIRE(pool.spawn(g));
    }
    gg::GrainPool pool;
};

// gain_c from the spec's table: gain × panGains(pan) with two outputs, gain alone with one.
struct ChannelGains
{
    float c[2];
};
ChannelGains channelGains(const gg::GrainSpec &g, int outputs)
{
    const gg::PanGains p = gg::panGains(g.pan);
    if (outputs == 1)
    {
        return {{g.gain, 0.0f}};
    }
    return {{g.gain * p.left, g.gain * p.right}};
}

bool sameBits(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

bool allFinite(const std::vector<float> &v)
{
    for (const float x : v)
    {
        if (!std::isfinite(x))
        {
            return false;
        }
    }
    return true;
}

bool anyNonZero(const std::vector<float> &v)
{
    for (const float x : v)
    {
        if (x != 0.0f)
        {
            return true;
        }
    }
    return false;
}

// The spec's §3 table for a sample source, written out per sample: for t in [onsetSample,
// grainEndSample), e = double(t − onsetSample) − onsetFrac, w = hann(e × invDuration), skip unless
// w > 0, pos = startPos + rate × e, dst[c][t] += read_c(pos) × (w × gain_c), with read the tier's
// interpolator. One output reads channel 0 with gain alone.
void specSample(const gg::GrainSpec &g, SampleRead read, const float *const src[2], int64_t frames,
                int outputs, std::vector<float> (&out)[2], const gg::HannTable &hann)
{
    const double invDuration = 1.0 / g.durationSamples;
    const ChannelGains gains = channelGains(g, outputs);
    for (int64_t t = g.onsetSample; t < gg::grainEndSample(g); ++t)
    {
        const int64_t n = t - g.onsetSample;
        const double e = static_cast<double>(n) - g.onsetFrac;
        const float w = hann.at(e * invDuration);
        if (!(w > 0.0f))
        {
            continue;
        }
        const double pos = g.startPos + g.rate * e;
        for (int c = 0; c < outputs; ++c)
        {
            out[c][static_cast<std::size_t>(t)] += read(src[c], frames, pos) * (w * gains.c[c]);
        }
    }
}

// Renders grain 0 of `rows` with renderSampleRef<Reader> over [0, length), split at `cuts`, each
// piece through an Output whose dst starts at the piece (blockStart = the piece's start), as the
// engine's blocks do.
template <class Reader = Hermite>
void refSample(const gg::GrainRows &rows, const gg::kernels::SampleSource &src, int outputs,
               std::vector<float> (&out)[2], const std::vector<int64_t> &cuts,
               const gg::HannTable &hann)
{
    for (std::size_t k = 0; k + 1 < cuts.size(); ++k)
    {
        const int64_t from = cuts[k];
        const int64_t to = cuts[k + 1];
        const gg::kernels::Output o{
            {out[0].data() + from, outputs == 2 ? out[1].data() + from : nullptr}, from};
        gg::kernels::renderSampleRef<Reader>(rows, 0, src, o, from, to, hann);
    }
}

// Hermite, but a live read any of whose taps (floor(pos) − 1 .. floor(pos) + 2) falls outside
// [100, 600) returns NaN in both channels, as the sample test's source would; and it counts its
// reads. The ring itself can't hold NaN (LiveSource stores non-finite input as 0).
struct NaNOutside
{
    static inline int64_t reads = 0;

    static float read(const float *data, int64_t frames,
                      double pos) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return Hermite::read(data, frames, pos);
    }
    static bool readLive(const gg::LiveSource &live, double pos, int64_t head,
                         gg::LiveSource::Cursor &cursor, float &l,
                         float &r) noexcept SST_CPPUTILS_NONBLOCKING
    {
        ++reads;
        const bool ok = Hermite::readLive(live, pos, head, cursor, l, r);
        const double base = std::floor(pos);
        if (base - 1.0 < 100.0 || base + 2.0 >= 600.0)
        {
            l = r = kNaN;
        }
        return ok;
    }
};

// Requires renderSampleRef<Reader> to match the spec's table, with read as the tier's interpolator,
// for the brief's grain over stereo noise: two outputs from a stereo and from a mono source, and
// one output from a stereo source.
template <class Reader> void requireSampleArithmetic(SampleRead read)
{
    constexpr int64_t kFrames = 2000;
    constexpr int64_t kLength = 1000;
    const gg::HannTable hann;
    const std::vector<float> left = noise(kFrames, 7, 0);
    const std::vector<float> right = noise(kFrames, 7, 1);
    const gg::GrainSpec g = testGrain(10, 200.25);
    REQUIRE(gg::grainEndSample(g) <= kLength);
    const OneGrain one(g);
    const gg::GrainRows &rows = one.pool.rows();
    // Splits at awkward places, the grain's onset and last sample among them.
    const std::vector<int64_t> cuts = {0, 7, 10, 11, 123, 300, 490, 491, 492, kLength};

    struct Shape
    {
        const char *name;
        int outputs;
        bool mono;
    };
    for (const Shape shape :
         {Shape{"two outputs, stereo source", 2, false}, Shape{"two outputs, mono source", 2, true},
          Shape{"one output, stereo source", 1, false}})
    {
        INFO(shape.name);
        // A mono source feeds both outputs from channel 0 (the engine sets data[1] = data[0]).
        const float *const channels[2] = {left.data(), shape.mono ? left.data() : right.data()};
        std::vector<float> expected[2] = {std::vector<float>(kLength, 0.0f),
                                          std::vector<float>(kLength, 0.0f)};
        std::vector<float> actual[2] = {std::vector<float>(kLength, 0.0f),
                                        std::vector<float>(kLength, 0.0f)};
        specSample(g, read, channels, kFrames, shape.outputs, expected, hann);
        refSample<Reader>(rows, {{channels[0], channels[1]}, kFrames, shape.mono}, shape.outputs,
                          actual, cuts, hann);

        REQUIRE(anyNonZero(expected[0]));
        REQUIRE(sameBits(actual[0], expected[0]));
        REQUIRE(sameBits(actual[1], expected[1]));
        if (shape.outputs == 2)
        {
            REQUIRE(anyNonZero(expected[1]));
        }
        else
        {
            REQUIRE(!anyNonZero(actual[1])); // one output never touches a second channel
        }
    }
}
} // namespace

TEST_CASE("sample reference follows the spec's arithmetic", "[kernel]")
{
    requireSampleArithmetic<Hermite>(&gg::interp::hermite4);
}

TEST_CASE("reference reads nothing where the window is zero", "[kernel]")
{
    // The source is NaN except frames [100, 600). The grain (onsetFrac 0, rate 1, duration
    // 498, startPos 100.5) reads pos = 100.5 + n at its n-th sample. Its window is zero at
    // n = 0 (e = 0) and at its last two samples, n = 498 and 499 (e >= duration); there the
    // Hermite taps reach frame 99, 600 and 601, which are NaN. Everywhere the window is
    // positive, n = 1 .. 497, the taps stay inside [100, 600). A read where the window is zero
    // would add NaN × 0 = NaN (carry-over T4 (126)).
    constexpr int64_t kFrames = 2000;
    constexpr int64_t kOnset = 1000;
    constexpr int64_t kLength = 1600;
    constexpr double kDuration = 498.0;
    constexpr double kStart = 100.5;
    const gg::HannTable hann;
    const gg::GrainSpec g{.index = 0,
                          .onsetSample = kOnset,
                          .onsetFrac = 0.0,
                          .startPos = kStart,
                          .rate = 1.0,
                          .durationSamples = kDuration,
                          .gain = 0.8f,
                          .pan = 0.4f};
    REQUIRE(gg::grainEndSample(g) == kOnset + 500);
    const OneGrain one(g);
    const gg::GrainRows &rows = one.pool.rows();
    const double invDuration = 1.0 / kDuration;

    // The premises: the window is zero exactly at n = 0, 498 and 499, and the positions read
    // there touch NaN, while those inside don't.
    REQUIRE(hann.at(0.0 * invDuration) == 0.0f);
    REQUIRE(hann.at(1.0 * invDuration) > 0.0f);
    REQUIRE(hann.at(497.0 * invDuration) > 0.0f);
    REQUIRE(hann.at(498.0 * invDuration) == 0.0f);
    REQUIRE(hann.at(499.0 * invDuration) == 0.0f);

    SECTION("sample source")
    {
        std::vector<float> left(kFrames, kNaN);
        std::vector<float> right(kFrames, kNaN);
        const std::vector<float> l = noise(kFrames, 3, 0);
        const std::vector<float> r = noise(kFrames, 3, 1);
        for (std::size_t i = 100; i < 600; ++i)
        {
            left[i] = l[i];
            right[i] = r[i];
        }
        REQUIRE(std::isnan(gg::interp::hermite4(left.data(), kFrames, kStart)));
        REQUIRE(std::isnan(gg::interp::hermite4(left.data(), kFrames, kStart + 498.0)));
        REQUIRE(std::isnan(gg::interp::hermite4(left.data(), kFrames, kStart + 499.0)));
        REQUIRE(std::isfinite(gg::interp::hermite4(left.data(), kFrames, kStart + 1.0)));
        REQUIRE(std::isfinite(gg::interp::hermite4(left.data(), kFrames, kStart + 497.0)));

        for (const int outputs : {2, 1})
        {
            for (const bool mono : {false, true})
            {
                INFO("outputs " << outputs << ", mono " << mono);
                std::vector<float> out[2] = {std::vector<float>(kLength, 0.0f),
                                             std::vector<float>(kLength, 0.0f)};
                const gg::kernels::SampleSource src{
                    {left.data(), mono ? left.data() : right.data()}, kFrames, mono};
                refSample(rows, src, outputs, out, {0, 999, 1000, 1001, 1300, 1499, kLength}, hann);
                REQUIRE(allFinite(out[0]));
                REQUIRE(allFinite(out[1]));
                REQUIRE(anyNonZero(out[0]));
            }
        }
    }

    SECTION("live ring")
    {
        // The same pattern in the ring's ring-time frames: noise in [100, 600), elsewhere a
        // constant the reader replaces with NaN. 1 s at 48 kHz, written block by block; each
        // block renders right after its write, as the engine does.
        constexpr int64_t kBlock = 100;
        gg::LiveSource ring;
        ring.prepare(48000.0, 1.0, static_cast<uint32_t>(kBlock));
        std::vector<float> left(kLength, 0.25f);
        std::vector<float> right(kLength, 0.25f);
        const std::vector<float> l = noise(kLength, 3, 0);
        const std::vector<float> r = noise(kLength, 3, 1);
        for (std::size_t i = 100; i < 600; ++i)
        {
            left[i] = l[i];
            right[i] = r[i];
        }
        for (const int outputs : {2, 1})
        {
            INFO("outputs " << outputs);
            ring.reset();
            NaNOutside::reads = 0;
            std::vector<float> out[2] = {std::vector<float>(kLength, 0.0f),
                                         std::vector<float>(kLength, 0.0f)};
            for (int64_t b = 0; b < kLength; b += kBlock)
            {
                const float *in[2] = {left.data() + b, right.data() + b};
                ring.write(in, 2, 0, b, static_cast<uint32_t>(kBlock));
                const gg::kernels::Output o{
                    {out[0].data() + b, outputs == 2 ? out[1].data() + b : nullptr}, b};
                gg::kernels::renderLiveRef<NaNOutside>(rows, 0, ring, o, b, b + kBlock, hann);
            }
            REQUIRE(allFinite(out[0]));
            REQUIRE(allFinite(out[1]));
            REQUIRE(anyNonZero(out[0]));
            // The head stays more than M frames ahead of every read, so the fade is 1 and every
            // sample with a positive window reads once: n = 1 .. 497.
            REQUIRE(NaNOutside::reads == 497);
        }
    }
}

TEST_CASE("live reference follows the spec's arithmetic", "[kernel]")
{
    // A 1 s ring at 48 kHz, written with stereo noise in blocks of 100 frames, frozen from frame
    // 3,000: the write gain ramps down over 960 samples and the head stops at R = 3,960. The
    // grain starts at 3,700.37 reading from 3,300.25 at rate 1.5, so it closes in on the head
    // once it stops: its fade runs from 1 through (0, 1) to 0.
    constexpr double kRate = 48000.0;
    constexpr int64_t kBlock = 100;
    constexpr int64_t kFreezeAt = 3000;
    constexpr int64_t kLength = 4400;
    const gg::HannTable hann;
    const std::vector<float> left = noise(kLength, 5, 0);
    const std::vector<float> right = noise(kLength, 5, 1);
    const gg::GrainSpec g = testGrain(3700, 3300.25);
    REQUIRE(gg::grainEndSample(g) <= kLength);
    const OneGrain one(g);
    const gg::GrainRows &rows = one.pool.rows();
    const double invDuration = 1.0 / g.durationSamples;

    for (const int outputs : {2, 1})
    {
        INFO("outputs " << outputs);
        const ChannelGains gains = channelGains(g, outputs);
        gg::LiveSource ring;
        ring.prepare(kRate, 1.0, static_cast<uint32_t>(kBlock));
        std::vector<float> expected[2] = {std::vector<float>(kLength, 0.0f),
                                          std::vector<float>(kLength, 0.0f)};
        std::vector<float> actual[2] = {std::vector<float>(kLength, 0.0f),
                                        std::vector<float>(kLength, 0.0f)};
        int64_t fadeOne = 0;
        int64_t fadePartial = 0;
        int64_t fadeZero = 0;
        gg::LiveSource::Cursor cursor;
        for (int64_t b = 0; b < kLength; b += kBlock)
        {
            ring.setFrozen(b >= kFreezeAt);
            const float *in[2] = {left.data() + b, right.data() + b};
            ring.write(in, 2, 0, b, static_cast<uint32_t>(kBlock));

            // The spec's table, per sample: e, w (skip unless > 0), pos, f =
            // float(fade(pos, R(t))) (skip unless > 0 and the read succeeds), then
            // dst[c][t] += read_c × ((w × f) × gain_c).
            for (int64_t t = std::max(b, g.onsetSample);
                 t < std::min(b + kBlock, gg::grainEndSample(g)); ++t)
            {
                const double e = static_cast<double>(t - g.onsetSample) - g.onsetFrac;
                const float w = hann.at(e * invDuration);
                if (!(w > 0.0f))
                {
                    continue;
                }
                const double pos = g.startPos + g.rate * e;
                const int64_t head = ring.headAt(t);
                const auto f = static_cast<float>(ring.fade(pos, head));
                fadeOne += f == 1.0f;
                fadePartial += f > 0.0f && f < 1.0f;
                fadeZero += f == 0.0f;
                float read[2] = {0.0f, 0.0f};
                if (!(f > 0.0f) || !ring.read(pos, head, cursor, read[0], read[1]))
                {
                    continue;
                }
                for (int c = 0; c < outputs; ++c)
                {
                    expected[c][static_cast<std::size_t>(t)] += read[c] * ((w * f) * gains.c[c]);
                }
            }

            const gg::kernels::Output o{
                {actual[0].data() + b, outputs == 2 ? actual[1].data() + b : nullptr}, b};
            gg::kernels::renderLiveRef<Hermite>(rows, 0, ring, o, b, b + kBlock, hann);
        }
        REQUIRE(ring.head() == kFreezeAt + gg::LiveSource::freezeRampSamples(kRate));
        REQUIRE(fadeOne > 0);
        REQUIRE(fadePartial > 0);
        REQUIRE(fadeZero > 0);
        REQUIRE(anyNonZero(expected[0]));
        REQUIRE(sameBits(actual[0], expected[0]));
        REQUIRE(sameBits(actual[1], expected[1]));
    }
}

TEST_CASE("eco reference reads linearly", "[kernel]")
{
    // The Eco tier (ruling 2) is the same arithmetic with interp::linear, or the ring's linear
    // read, in place of the Hermite.
    const gg::HannTable hann;

    SECTION("sample source")
    {
        requireSampleArithmetic<Linear>(&gg::interp::linear);

        // Not vacuous: the two tiers' renders of the same grain differ.
        constexpr int64_t kFrames = 2000;
        constexpr int64_t kLength = 1000;
        const std::vector<float> left = noise(kFrames, 7, 0);
        const std::vector<float> right = noise(kFrames, 7, 1);
        const OneGrain one(testGrain(10, 200.25));
        const gg::kernels::SampleSource src{{left.data(), right.data()}, kFrames, false};
        std::vector<float> eco[2] = {std::vector<float>(kLength, 0.0f),
                                     std::vector<float>(kLength, 0.0f)};
        std::vector<float> normal[2] = {std::vector<float>(kLength, 0.0f),
                                        std::vector<float>(kLength, 0.0f)};
        refSample<Linear>(one.pool.rows(), src, 2, eco, {0, kLength}, hann);
        refSample<Hermite>(one.pool.rows(), src, 2, normal, {0, kLength}, hann);
        REQUIRE(anyNonZero(eco[0]));
        REQUIRE_FALSE(sameBits(eco[0], normal[0]));
        REQUIRE_FALSE(sameBits(eco[1], normal[1]));
    }

    SECTION("live ring")
    {
        // Task 4's live test, frozen so that the fade runs from 1 through (0, 1) to 0, with the
        // read written out against a copy of what the ring stores: the input up to the freeze,
        // then the input times the documented write gain ramp / L, ramp = L − 1 down to 0, after
        // which the head stops.
        constexpr double kRate = 48000.0;
        constexpr int64_t kBlock = 100;
        constexpr int64_t kFreezeAt = 3000;
        constexpr int64_t kLength = 4400;
        const int64_t ramp = gg::LiveSource::freezeRampSamples(kRate);
        const std::vector<float> input[2] = {noise(kLength, 5, 0), noise(kLength, 5, 1)};
        std::vector<float> stored[2] = {input[0], input[1]};
        for (auto &channel : stored)
        {
            for (int64_t j = kFreezeAt; j < kLength; ++j)
            {
                const int64_t r = std::max<int64_t>(0, ramp - 1 - (j - kFreezeAt));
                channel[static_cast<std::size_t>(j)] *=
                    static_cast<float>(r) / static_cast<float>(ramp);
            }
        }
        const gg::GrainSpec g = testGrain(3700, 3300.25);
        REQUIRE(gg::grainEndSample(g) <= kLength);
        const OneGrain one(g);
        const gg::GrainRows &rows = one.pool.rows();
        const double invDuration = 1.0 / g.durationSamples;

        for (const int outputs : {2, 1})
        {
            INFO("outputs " << outputs);
            const ChannelGains gains = channelGains(g, outputs);
            gg::LiveSource ring;
            ring.prepare(kRate, 1.0, static_cast<uint32_t>(kBlock));
            const int64_t frames = ring.frames();
            std::vector<float> expected[2] = {std::vector<float>(kLength, 0.0f),
                                              std::vector<float>(kLength, 0.0f)};
            std::vector<float> actual[2] = {std::vector<float>(kLength, 0.0f),
                                            std::vector<float>(kLength, 0.0f)};
            std::vector<float> normal[2] = {std::vector<float>(kLength, 0.0f),
                                            std::vector<float>(kLength, 0.0f)};
            int64_t fadeOne = 0;
            int64_t fadePartial = 0;
            int64_t fadeZero = 0;
            gg::LiveSource::Cursor cursor;
            for (int64_t b = 0; b < kLength; b += kBlock)
            {
                ring.setFrozen(b >= kFreezeAt);
                const float *in[2] = {input[0].data() + b, input[1].data() + b};
                ring.write(in, 2, 0, b, static_cast<uint32_t>(kBlock));

                for (int64_t t = std::max(b, g.onsetSample);
                     t < std::min(b + kBlock, gg::grainEndSample(g)); ++t)
                {
                    const double e = static_cast<double>(t - g.onsetSample) - g.onsetFrac;
                    const float w = hann.at(e * invDuration);
                    if (!(w > 0.0f))
                    {
                        continue;
                    }
                    const double pos = g.startPos + g.rate * e;
                    const int64_t head = ring.headAt(t);
                    const auto f = static_cast<float>(ring.fade(pos, head));
                    fadeOne += f == 1.0f;
                    fadePartial += f > 0.0f && f < 1.0f;
                    fadeZero += f == 0.0f;
                    // The linear taps floor(pos) and floor(pos) + 1 must be in the window.
                    const auto i = static_cast<int64_t>(std::floor(pos));
                    if (!(f > 0.0f) || i < head - frames || i + 1 > head - 1)
                    {
                        continue;
                    }
                    // The copy is the ring: its Hermite reads agree with the ring's.
                    float hermite[2] = {0.0f, 0.0f};
                    if (ring.read(pos, head, cursor, hermite[0], hermite[1]))
                    {
                        for (int c = 0; c < 2; ++c)
                        {
                            REQUIRE(hermite[c] ==
                                    gg::interp::hermite4(stored[c].data(), kLength, pos));
                        }
                    }
                    for (int c = 0; c < outputs; ++c)
                    {
                        const float read = gg::interp::linear(stored[c].data(), kLength, pos);
                        expected[c][static_cast<std::size_t>(t)] += read * ((w * f) * gains.c[c]);
                    }
                }

                const gg::kernels::Output o{
                    {actual[0].data() + b, outputs == 2 ? actual[1].data() + b : nullptr}, b};
                gg::kernels::renderLiveRef<Linear>(rows, 0, ring, o, b, b + kBlock, hann);
                const gg::kernels::Output n{
                    {normal[0].data() + b, outputs == 2 ? normal[1].data() + b : nullptr}, b};
                gg::kernels::renderLiveRef<Hermite>(rows, 0, ring, n, b, b + kBlock, hann);
            }
            REQUIRE(ring.head() == kFreezeAt + ramp);
            REQUIRE(fadeOne > 0);
            REQUIRE(fadePartial > 0);
            REQUIRE(fadeZero > 0);
            REQUIRE(anyNonZero(expected[0]));
            REQUIRE(sameBits(actual[0], expected[0]));
            REQUIRE(sameBits(actual[1], expected[1]));
            REQUIRE_FALSE(sameBits(actual[0], normal[0]));
        }
    }

    SECTION("the ring's linear read stays in its own window")
    {
        // A 10 ms ring at 48 kHz: N = 480. Its linear read needs only taps floor(pos) and
        // floor(pos) + 1 in [head − N, head − 1], so it reaches a frame further than the Hermite
        // at both ends.
        constexpr int64_t kBlock = 100;
        gg::LiveSource ring;
        ring.prepare(48000.0, 0.01, static_cast<uint32_t>(kBlock));
        const int64_t frames = ring.frames();
        REQUIRE(frames == 480);
        const std::vector<float> left = noise(1000, 9, 0);
        const std::vector<float> right = noise(1000, 9, 1);
        const auto expectRead = [&](double pos, int64_t head) {
            INFO("pos " << pos << ", head " << head);
            gg::LiveSource::Cursor cursor;
            float l = kNaN;
            float r = kNaN;
            REQUIRE(Linear::readLive(ring, pos, head, cursor, l, r));
            REQUIRE(l == gg::interp::linear(left.data(), head, pos));
            REQUIRE(r == gg::interp::linear(right.data(), head, pos));
        };
        const auto expectNoRead = [&](double pos, int64_t head) {
            INFO("pos " << pos << ", head " << head);
            gg::LiveSource::Cursor cursor;
            float l = 0.25f;
            float r = 0.25f;
            REQUIRE_FALSE(Linear::readLive(ring, pos, head, cursor, l, r));
            REQUIRE(l == 0.25f); // nothing read
            REQUIRE(r == 0.25f);
        };

        // Three blocks: the head is 300, the window [−180, 299], and frames below 0 are silence.
        for (int64_t b = 0; b < 300; b += kBlock)
        {
            const float *in[2] = {left.data() + b, right.data() + b};
            ring.write(in, 2, 0, b, static_cast<uint32_t>(kBlock));
        }
        int64_t head = ring.head();
        REQUIRE(head == 300);
        expectRead(298.5, head); // taps 298 and 299: the newest frame
        expectRead(-0.5, head);  // taps −1 (silence) and 0
        expectRead(-180.0, head);
        expectNoRead(299.0, head); // tap 300 isn't written yet
        expectNoRead(-180.5, head);
        {
            // The Hermite's reach ends a frame earlier, at tap floor(pos) + 2.
            gg::LiveSource::Cursor cursor;
            float l = 0.0f;
            float r = 0.0f;
            REQUIRE_FALSE(ring.read(298.5, head, cursor, l, r));
        }

        // Seven more: the head is 1,000 and the window [520, 999]; the oldest frames have wrapped.
        for (int64_t b = 300; b < 1000; b += kBlock)
        {
            const float *in[2] = {left.data() + b, right.data() + b};
            ring.write(in, 2, 0, b, static_cast<uint32_t>(kBlock));
        }
        head = ring.head();
        REQUIRE(head == 1000);
        expectRead(520.5, head); // taps 520 and 521: the oldest frame
        expectRead(998.25, head);
        expectRead(760.75, head);
        expectNoRead(519.5, head); // tap 519 has left the window
        expectNoRead(999.0, head);
        expectNoRead(std::numeric_limits<double>::quiet_NaN(), head);
        expectNoRead(std::numeric_limits<double>::infinity(), head);
        {
            gg::LiveSource::Cursor cursor;
            float l = 0.0f;
            float r = 0.0f;
            REQUIRE_FALSE(ring.read(520.5, head, cursor, l, r)); // the Hermite needs tap 519
        }

        // Reads moving forward through one cursor, across the slot wrap, match fresh ones.
        gg::LiveSource::Cursor cursor;
        for (double pos = 520.0; pos < 999.0; pos += 0.75)
        {
            INFO("pos " << pos);
            float l = kNaN;
            float r = kNaN;
            REQUIRE(Linear::readLive(ring, pos, head, cursor, l, r));
            REQUIRE(l == gg::interp::linear(left.data(), head, pos));
            REQUIRE(r == gg::interp::linear(right.data(), head, pos));
        }
    }
}
