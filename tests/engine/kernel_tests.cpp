// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The scalar reference renderers (spec §3, ruling 1). Each test writes the spec's table out
// as its own per-sample loop and requires the reference to produce the same bits. The SIMD
// kernels are then held to the reference the same way, with std::memcmp.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <sst/cpputils/rtsan_support.h>

#include "gg/core/Interpolate.h"
#include "gg/core/Rng.h"
#include "gg/core/Window.h"
#include "gg/grain/Budget.h"
#include "gg/grain/Envelope.h"
#include "gg/grain/GrainKernels.h"
#include "gg/grain/GrainPool.h"
#include "gg/grain/SincTable.h"
#include "gg/source/InterleavedStereo.h"
#include "gg/source/LiveSource.h"

namespace
{
using gg::kernels::Hermite;
using gg::kernels::Linear;
using gg::kernels::Sinc;
using gg::kernels::Tables;

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kZero = 0.0f;

// The kernels' tables, built once.
const Tables &kernelTables()
{
    static const gg::HannTable hann;
    static const gg::SincTable sinc;
    static const Tables tables{hann, sinc};
    return tables;
}

// What a reader takes beside its taps: the sinc table and a stretch, which only Sinc reads.
gg::kernels::ReadParams readParams(float stretch = 1.0f) { return {&kernelTables().sinc, stretch}; }

// The spec's stretch for a grain at rate r (§6.2), written out: s = min(1, 1 / r).
float specStretch(double rate) { return rate > 1.0 ? static_cast<float>(1.0 / rate) : 1.0f; }

// The spec's HQ read (§6.2), written out per tap: for k = −7 … 8, x_k = (k − frac) × s and
// c_k = T(x_k), the table read in float; y = (Σ c_k × tap_k) / (Σ c_k), both sums in k order and
// in float, with a tap outside [0, frames) reading 0.
float specSinc(const float *data, int64_t frames, double pos, float stretch)
{
    if (!(pos > -9.0 && pos < static_cast<double>(frames) + 8.0))
    {
        return 0.0f; // every tap is outside the source
    }
    const double base = std::floor(pos);
    const auto i = static_cast<int64_t>(base);
    const auto frac = static_cast<float>(pos - base);
    float num = 0.0f;
    float den = 0.0f;
    for (int64_t k = -7; k <= 8; ++k)
    {
        const float x = (static_cast<float>(k) - frac) * stretch;
        const float c = kernelTables().sinc.at(x);
        const int64_t j = i + k;
        const float tap = j >= 0 && j < frames ? data[j] : 0.0f;
        num += c * tap;
        den += c;
    }
    return den != 0.0f ? num / den : 0.0f;
}

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

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

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
// grainEndSample), e = double(t − onsetSample) − onsetFrac, w = hann(warp(e × invDuration)) with
// the warp of the grain's Texture envelope (spec §4; the identity at texture 0), skip unless
// w > 0, pos = startPos + rate × e, dst[c][t] += read_c(pos) × (w × gain_c), with read the tier's
// interpolator (read(data, frames, pos)). One output reads channel 0 with gain alone.
template <class Read>
void specSample(const gg::GrainSpec &g, Read read, const float *const src[2], int64_t frames,
                int outputs, std::vector<float> (&out)[2], const Tables &tables)
{
    const double invDuration = 1.0 / g.durationSamples;
    const gg::EnvelopeShape shape = gg::envelopeShape(g.texture, g.durationSamples);
    const ChannelGains gains = channelGains(g, outputs);
    for (int64_t t = g.onsetSample; t < gg::grainEndSample(g); ++t)
    {
        const int64_t n = t - g.onsetSample;
        const double e = static_cast<double>(n) - g.onsetFrac;
        const float w = tables.hann.at(gg::warpPhase(e * invDuration, shape));
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
               std::vector<float> (&out)[2], const std::vector<int64_t> &cuts, const Tables &tables)
{
    for (std::size_t k = 0; k + 1 < cuts.size(); ++k)
    {
        const int64_t from = cuts[k];
        const int64_t to = cuts[k + 1];
        const gg::kernels::Output o{
            {out[0].data() + from, outputs == 2 ? out[1].data() + from : nullptr}, from};
        gg::kernels::renderSampleRef<Reader>(rows, 0, src, o, from, to, tables);
    }
}

// Hermite, but a live read any of whose taps (floor(pos) − 1 .. floor(pos) + 2) falls outside
// [100, 600) returns NaN in both channels, as the sample test's source would; and it counts its
// reads. The ring itself can't hold NaN (LiveSource stores non-finite input as 0).
struct NaNOutside : Hermite
{
    static inline int64_t reads = 0;

    static bool readLive(const gg::LiveSource &live, double pos, int64_t head,
                         gg::LiveSource::Cursor &cursor, const gg::kernels::ReadParams &p, float &l,
                         float &r) noexcept SST_CPPUTILS_NONBLOCKING
    {
        ++reads;
        const bool ok = Hermite::readLive(live, pos, head, cursor, p, l, r);
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
// one output from a stereo source. The grain has the given Texture (spec §4) and rate.
template <class Reader, class Read>
void requireSampleArithmetic(Read read, double texture = 0.0, double rate = 1.5)
{
    constexpr int64_t kFrames = 2000;
    constexpr int64_t kLength = 1000;
    const Tables &tables = kernelTables();
    const std::vector<float> left = noise(kFrames, 7, 0);
    const std::vector<float> right = noise(kFrames, 7, 1);
    gg::GrainSpec g = testGrain(10, 200.25);
    g.texture = texture;
    g.rate = rate;
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
        specSample(g, read, channels, kFrames, shape.outputs, expected, tables);
        refSample<Reader>(rows, {{channels[0], channels[1]}, kFrames, shape.mono, nullptr},
                          shape.outputs, actual, cuts, tables);

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
    requireSampleArithmetic<Hermite>(&gg::interp::hermite4<>);
}

TEST_CASE("textured grains match the spec loop", "[kernel]")
{
    // The reference's window is hann(warp(e × invDuration)) with the shape latched at spawn,
    // bit for bit, across the family: no sustain (0.25, 0.5), a sustain (0.75), the gate (1).
    for (const double texture : {0.25, 0.5, 0.75, 1.0})
    {
        INFO("texture " << texture);
        requireSampleArithmetic<Hermite>(&gg::interp::hermite4<>, texture);
        requireSampleArithmetic<Linear>(&gg::interp::linear<>, texture);
    }
    // The latched shape is the one envelopeShape gives the spec's texture and duration.
    gg::GrainSpec g = testGrain(10, 200.25);
    g.texture = 0.75;
    const OneGrain one(g);
    const gg::GrainRows &rows = one.pool.rows();
    const gg::EnvelopeShape e = gg::envelopeShape(0.75, g.durationSamples);
    REQUIRE(rows.texture[0] == 0.75);
    REQUIRE(rows.envAttackEnd[0] == e.attackEnd);
    REQUIRE(rows.envReleaseStart[0] == e.releaseStart);
    REQUIRE(rows.envAttackScale[0] == e.attackScale);
    REQUIRE(rows.envReleaseScale[0] == e.releaseScale);
    REQUIRE(one.pool.spec(0) == g);
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
    const Tables &tables = kernelTables();
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
    REQUIRE(tables.hann.at(0.0 * invDuration) == 0.0f);
    REQUIRE(tables.hann.at(1.0 * invDuration) > 0.0f);
    REQUIRE(tables.hann.at(497.0 * invDuration) > 0.0f);
    REQUIRE(tables.hann.at(498.0 * invDuration) == 0.0f);
    REQUIRE(tables.hann.at(499.0 * invDuration) == 0.0f);

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
                    {left.data(), mono ? left.data() : right.data()}, kFrames, mono, nullptr};
                refSample(rows, src, outputs, out, {0, 999, 1000, 1001, 1300, 1499, kLength},
                          tables);
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
                gg::kernels::renderLiveRef<NaNOutside>(rows, 0, ring, 1, o, b, b + kBlock, tables);
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
    const Tables &tables = kernelTables();
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
                const float w = tables.hann.at(e * invDuration);
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
            gg::kernels::renderLiveRef<Hermite>(rows, 0, ring, 1, o, b, b + kBlock, tables);
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
    const Tables &tables = kernelTables();

    SECTION("sample source")
    {
        requireSampleArithmetic<Linear>(&gg::interp::linear<>);

        // Not vacuous: the two tiers' renders of the same grain differ.
        constexpr int64_t kFrames = 2000;
        constexpr int64_t kLength = 1000;
        const std::vector<float> left = noise(kFrames, 7, 0);
        const std::vector<float> right = noise(kFrames, 7, 1);
        const OneGrain one(testGrain(10, 200.25));
        const gg::kernels::SampleSource src{{left.data(), right.data()}, kFrames, false, nullptr};
        std::vector<float> eco[2] = {std::vector<float>(kLength, 0.0f),
                                     std::vector<float>(kLength, 0.0f)};
        std::vector<float> normal[2] = {std::vector<float>(kLength, 0.0f),
                                        std::vector<float>(kLength, 0.0f)};
        refSample<Linear>(one.pool.rows(), src, 2, eco, {0, kLength}, tables);
        refSample<Hermite>(one.pool.rows(), src, 2, normal, {0, kLength}, tables);
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
                    const float w = tables.hann.at(e * invDuration);
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
                gg::kernels::renderLiveRef<Linear>(rows, 0, ring, 1, o, b, b + kBlock, tables);
                const gg::kernels::Output n{
                    {normal[0].data() + b, outputs == 2 ? normal[1].data() + b : nullptr}, b};
                gg::kernels::renderLiveRef<Hermite>(rows, 0, ring, 1, n, b, b + kBlock, tables);
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
            REQUIRE(Linear::readLive(ring, pos, head, cursor, readParams(), l, r));
            REQUIRE(l == gg::interp::linear(left.data(), head, pos));
            REQUIRE(r == gg::interp::linear(right.data(), head, pos));
        };
        const auto expectNoRead = [&](double pos, int64_t head) {
            INFO("pos " << pos << ", head " << head);
            gg::LiveSource::Cursor cursor;
            float l = 0.25f;
            float r = 0.25f;
            REQUIRE_FALSE(Linear::readLive(ring, pos, head, cursor, readParams(), l, r));
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
            REQUIRE(Linear::readLive(ring, pos, head, cursor, readParams(), l, r));
            REQUIRE(l == gg::interp::linear(left.data(), head, pos));
            REQUIRE(r == gg::interp::linear(right.data(), head, pos));
        }
    }
}
TEST_CASE("sinc reference follows the spec's arithmetic", "[kernel]")
{
    // The HQ tier (spec §6.2) is the same per-sample arithmetic with the 16-tap windowed sinc,
    // written out tap by tap (specSinc), in place of the Hermite: at stretch 1 (rate 0.75) and
    // stretched (rate 1/0.6, so s = 0.6).
    const Tables &tables = kernelTables();

    SECTION("sample source")
    {
        for (const double rate : {0.75, 1.0 / 0.6})
        {
            const float stretch = specStretch(rate);
            INFO("rate " << rate << ", stretch " << stretch);
            REQUIRE(gg::kernels::stretchOf(rate) == stretch);
            const auto read = [stretch](const float *data, int64_t frames, double pos) {
                return specSinc(data, frames, pos, stretch);
            };
            requireSampleArithmetic<Sinc>(read, 0.0, rate);
            requireSampleArithmetic<Sinc>(read, 0.75, rate);
        }
        REQUIRE(specStretch(1.0 / 0.6) <= 0.6f);
        REQUIRE(specStretch(1.0) == 1.0f);

        // Not vacuous: the HQ and Normal renders of the same grain differ.
        constexpr int64_t kFrames = 2000;
        constexpr int64_t kLength = 1000;
        const std::vector<float> left = noise(kFrames, 7, 0);
        const std::vector<float> right = noise(kFrames, 7, 1);
        const OneGrain one(testGrain(10, 200.25));
        const gg::kernels::SampleSource src{{left.data(), right.data()}, kFrames, false, nullptr};
        std::vector<float> hq[2] = {std::vector<float>(kLength, 0.0f),
                                    std::vector<float>(kLength, 0.0f)};
        std::vector<float> normal[2] = {std::vector<float>(kLength, 0.0f),
                                        std::vector<float>(kLength, 0.0f)};
        refSample<Sinc>(one.pool.rows(), src, 2, hq, {0, kLength}, tables);
        refSample<Hermite>(one.pool.rows(), src, 2, normal, {0, kLength}, tables);
        REQUIRE(anyNonZero(hq[0]));
        REQUIRE_FALSE(sameBits(hq[0], normal[0]));
        REQUIRE_FALSE(sameBits(hq[1], normal[1]));
    }

    SECTION("the reader's read and its stride")
    {
        // Sinc::read is the spec's read at any position, across both ends of the source and
        // outside it, and at a stride of 2 it reads frame f at data[2f]: one channel of
        // interleaved pairs.
        constexpr int64_t kFrames = 40;
        const std::vector<float> left = noise(kFrames, 9, 0);
        const std::vector<float> right = noise(kFrames, 9, 1);
        gg::InterleavedStereo pairs;
        pairs.assign(left.data(), right.data(), kFrames);
        int64_t nonZero = 0;
        for (const float stretch : {1.0f, 0.6f, 0.25f})
        {
            for (double pos = -12.0; pos < static_cast<double>(kFrames) + 12.0; pos += 0.37)
            {
                INFO("stretch " << stretch << ", pos " << pos);
                const float want = specSinc(left.data(), kFrames, pos, stretch);
                const float got = Sinc::read(left.data(), kFrames, pos, readParams(stretch));
                REQUIRE(std::memcmp(&got, &want, sizeof(float)) == 0);
                const float strided =
                    Sinc::read<2>(pairs.data(), kFrames, pos, readParams(stretch));
                REQUIRE(std::memcmp(&strided, &want, sizeof(float)) == 0);
                const float wantRight = specSinc(right.data(), kFrames, pos, stretch);
                const float stridedRight =
                    Sinc::read<2>(pairs.data() + 1, kFrames, pos, readParams(stretch));
                REQUIRE(std::memcmp(&stridedRight, &wantRight, sizeof(float)) == 0);
                nonZero += want != 0.0f;
            }
        }
        REQUIRE(nonZero > 300);
        for (const double pos :
             {-9.0, static_cast<double>(kFrames) + 8.0, std::numeric_limits<double>::quiet_NaN(),
              std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
        {
            INFO("pos " << pos);
            const float got = Sinc::read(left.data(), kFrames, pos, readParams());
            REQUIRE(std::memcmp(&got, &kZero, sizeof(float)) == 0);
        }
    }

    SECTION("live ring")
    {
        // The eco test's frozen ring, read with the sinc against a copy of what the ring stores,
        // with the HQ fade written out for the sinc's reach, taps floor(pos) − 7 .. floor(pos) +
        // 8 (spec §6.2): g = clamp((R − 1 − (pos + 8)) / M, 0, 1) × clamp(((pos − 7) − (R − N)) /
        // M, 0, 1). Each grain's fade runs from 1 through (0, 1) to 0: at rate 1 (stretch 1) it
        // closes in once the head stops, at rate 1/0.6 (stretch 0.6) from the start.
        constexpr double kRate = 48000.0;
        constexpr int64_t kBlock = 100;
        constexpr int64_t kFreezeAt = 3000;
        constexpr int64_t kLength = 4500;
        constexpr auto kMargin = static_cast<double>(gg::LiveSource::kHeadMargin);
        const auto unit = [](double x) { return x > 0.0 ? (x < 1.0 ? x : 1.0) : 0.0; };
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
        struct Case
        {
            double rate;
            double startPos;
            double duration;
        };
        for (const Case c : {Case{1.0, 3360.25, 700.0}, Case{1.0 / 0.6, 3300.25, 480.0}})
        {
            gg::GrainSpec g = testGrain(3700, c.startPos);
            g.rate = c.rate;
            g.durationSamples = c.duration;
            REQUIRE(gg::grainEndSample(g) <= kLength);
            const OneGrain one(g);
            const gg::GrainRows &rows = one.pool.rows();
            const double invDuration = 1.0 / g.durationSamples;
            const float stretch = specStretch(c.rate);
            for (const int outputs : {2, 1})
            {
                INFO("rate " << c.rate << ", " << outputs << " outputs");
                const ChannelGains gains = channelGains(g, outputs);
                gg::LiveSource ring;
                ring.prepare(kRate, 1.0, static_cast<uint32_t>(kBlock));
                const int64_t frames = ring.frames();
                std::vector<float> expected[2] = {std::vector<float>(kLength, 0.0f),
                                                  std::vector<float>(kLength, 0.0f)};
                std::vector<float> actual[2] = {std::vector<float>(kLength, 0.0f),
                                                std::vector<float>(kLength, 0.0f)};
                int64_t fadeOne = 0;
                int64_t fadePartial = 0;
                int64_t fadeZero = 0;
                for (int64_t b = 0; b < kLength; b += kBlock)
                {
                    ring.setFrozen(b >= kFreezeAt);
                    const float *in[2] = {input[0].data() + b, input[1].data() + b};
                    ring.write(in, 2, 0, b, static_cast<uint32_t>(kBlock));

                    for (int64_t t = std::max(b, g.onsetSample);
                         t < std::min(b + kBlock, gg::grainEndSample(g)); ++t)
                    {
                        const double e = static_cast<double>(t - g.onsetSample) - g.onsetFrac;
                        const float w = tables.hann.at(e * invDuration);
                        if (!(w > 0.0f))
                        {
                            continue;
                        }
                        const double pos = g.startPos + g.rate * e;
                        const int64_t head = ring.headAt(t);
                        const auto r = static_cast<double>(head);
                        const double toHead = (r - 1.0 - (pos + 8.0)) / kMargin;
                        const double fromTail =
                            ((pos - 7.0) - (r - static_cast<double>(frames))) / kMargin;
                        const auto f = static_cast<float>(unit(toHead) * unit(fromTail));
                        REQUIRE(Sinc::fade(ring, pos, head) == unit(toHead) * unit(fromTail));
                        fadeOne += f == 1.0f;
                        fadePartial += f > 0.0f && f < 1.0f;
                        fadeZero += f == 0.0f;
                        // All 16 taps must be in the window.
                        const auto i = static_cast<int64_t>(std::floor(pos));
                        if (!(f > 0.0f) || i - 7 < head - frames || i + 8 > head - 1)
                        {
                            continue;
                        }
                        for (int ch = 0; ch < outputs; ++ch)
                        {
                            const float read = specSinc(stored[ch].data(), kLength, pos, stretch);
                            expected[ch][static_cast<std::size_t>(t)] +=
                                read * ((w * f) * gains.c[ch]);
                        }
                    }

                    const gg::kernels::Output o{
                        {actual[0].data() + b, outputs == 2 ? actual[1].data() + b : nullptr}, b};
                    gg::kernels::renderLiveRef<Sinc>(rows, 0, ring, 1, o, b, b + kBlock, tables);
                }
                REQUIRE(ring.head() == kFreezeAt + ramp);
                REQUIRE(fadeOne > 0);
                REQUIRE(fadePartial > 0);
                REQUIRE(fadeZero > 0);
                REQUIRE(anyNonZero(expected[0]));
                REQUIRE(sameBits(actual[0], expected[0]));
                REQUIRE(sameBits(actual[1], expected[1]));
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The SIMD sample kernel, held to the reference bit for bit (std::memcmp).

namespace
{
using gg::kernels::SampleSource;

constexpr int64_t kNoiseFrames = 96000; // 2 s at 48 kHz

// A uniform draw in [0, 1) from gg::rng, one stream per quantity.
double u01(uint64_t seed, uint32_t stream, uint64_t index)
{
    return gg::rng::uniform01(gg::rng::hash({seed, 1, stream, gg::rng::Field::SignalNoise, index}));
}

// The overload ladder's steal of the grain in `row` at output sample `onset` (GrainPool::steal,
// spec §6.5).
struct Steal
{
    uint32_t row;
    int64_t onset;
};

// Grains spawned in order into a pool large enough to admit all of them, so its rows are
// latched as the engine's are. The specs must be in non-decreasing onset order.
struct Pool
{
    explicit Pool(const std::vector<gg::GrainSpec> &specs)
        : pool(static_cast<uint32_t>(specs.size()) + 1)
    {
        for (const gg::GrainSpec &g : specs)
        {
            REQUIRE(pool.canAdmit(g.onsetSample));
            REQUIRE(pool.spawn(g));
        }
    }
    // The specs spawned in order without asking canAdmit, so no grain's end is ever forgotten,
    // then each steal applied: its grain must sound at its onset and not have been stolen. With
    // every heap entry still there, the steals may come in any order. The kernels read only the
    // rows, which hold each stolen grain's moved end, fadeStart and invFade.
    Pool(const std::vector<gg::GrainSpec> &specs, const std::vector<Steal> &steals)
        : pool(static_cast<uint32_t>(specs.size()) + 1)
    {
        for (const gg::GrainSpec &g : specs)
        {
            REQUIRE(pool.spawn(g));
        }
        for (const Steal &s : steals)
        {
            REQUIRE(s.row < pool.size());
            REQUIRE(pool.rows().onsetSample[s.row] <= s.onset);
            REQUIRE(s.onset < pool.rows().endSample[s.row]);
            REQUIRE(pool.rows().fadeStart[s.row] == gg::kNeverStolen);
            pool.steal(s.row, s.onset);
        }
    }
    const gg::GrainRows &rows() const { return pool.rows(); }
    uint32_t size() const { return pool.size(); }
    gg::GrainPool pool;
};

// The brief's random grains: rates 2^[−2, 2] (or one of `rates`, when given), onsetFrac in
// [0, 1), durations 16–9,600, textures in [0, 1) (spec §4; every fourth exactly 0, the plain Hann,
// which the SIMD interior renders without the warp), pans ±1, onsets spread over [−4,000, 8,000) in
// order, and start positions over the source and up to 2,000 frames past either end, so many reads
// cross an end.
std::vector<gg::GrainSpec> randomGrains(uint64_t seed, uint32_t count, int64_t frames,
                                        const std::vector<double> &rates = {})
{
    std::vector<gg::GrainSpec> specs;
    for (uint32_t k = 0; k < count; ++k)
    {
        const double rate =
            rates.empty() ? std::exp2(-2.0 + 4.0 * u01(seed, 0, k)) : rates[k % rates.size()];
        specs.push_back({
            .index = k,
            .onsetSample = -4000 + static_cast<int64_t>(k) * 12000 / count,
            .onsetFrac = u01(seed, 1, k),
            .startPos = -2000.0 + u01(seed, 2, k) * static_cast<double>(frames + 4000),
            .rate = rate,
            .durationSamples = 16.0 + u01(seed, 3, k) * (9600.0 - 16.0),
            .texture = k % 4 == 0 ? 0.0 : u01(seed, 6, k),
            .gain = 0.25f + 0.75f * static_cast<float>(u01(seed, 4, k)),
            .pan = static_cast<float>(2.0 * u01(seed, 5, k) - 1.0),
        });
    }
    return specs;
}

// Output buffers for one render call over [from, to), filled with `fill`.
struct Buffers
{
    Buffers(int64_t from, int64_t to, int outputs, float fill = 0.0f)
        : from(from), outputs(outputs),
          ch{std::vector<float>(static_cast<std::size_t>(std::max<int64_t>(to - from, 1)), fill),
             std::vector<float>(static_cast<std::size_t>(std::max<int64_t>(to - from, 1)), fill)}
    {
    }
    gg::kernels::Output output()
    {
        return {{ch[0].data(), outputs == 2 ? ch[1].data() : nullptr}, from};
    }
    int64_t from;
    int outputs;
    std::vector<float> ch[2];
};

enum class Kernel
{
    Ref,
    Simd
};

// Every grain of the pool, in spawn order, over [from, to) in one call each.
template <class R>
void renderAll(Kernel kernel, const Pool &pool, const SampleSource &src, Buffers &b, int64_t from,
               int64_t to, const Tables &tables)
{
    const gg::kernels::Output o = b.output();
    for (uint32_t i = 0; i < pool.size(); ++i)
    {
        if (kernel == Kernel::Ref)
        {
            gg::kernels::renderSampleRef<R>(pool.rows(), i, src, o, from, to, tables);
        }
        else
        {
            gg::kernels::renderSampleSimd<R>(pool.rows(), i, src, o, from, to, tables);
        }
    }
}

// Renders the pool over [from, to) with the reference and with the SIMD kernel, each into fresh
// buffers filled with `fill`, and returns both.
template <class R>
std::pair<Buffers, Buffers> renderBoth(const Pool &pool, const SampleSource &src, int outputs,
                                       int64_t from, int64_t to, const Tables &tables,
                                       float fill = 0.0f)
{
    Buffers ref(from, to, outputs, fill);
    Buffers simd(from, to, outputs, fill);
    renderAll<R>(Kernel::Ref, pool, src, ref, from, to, tables);
    renderAll<R>(Kernel::Simd, pool, src, simd, from, to, tables);
    return {std::move(ref), std::move(simd)};
}

// Equal bits, or both NaN: a NaN's payload may depend on the operand order the compiler picks.
bool sameOrBothNaN(const std::vector<float> &a, const std::vector<float> &b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t k = 0; k < a.size(); ++k)
    {
        if (std::memcmp(&a[k], &b[k], sizeof(float)) != 0 &&
            !(std::isnan(a[k]) && std::isnan(b[k])))
        {
            return false;
        }
    }
    return true;
}

bool anyNaN(const std::vector<float> &v)
{
    return std::any_of(v.begin(), v.end(), [](float x) { return std::isnan(x); });
}

// Each reader's taps, floor(pos) − before .. floor(pos) + after, stated here independently of the
// readers' own kBefore and kAfter, so a wrong reach in the kernel can't hide behind the same
// constant in the test.
template <class R> struct Reach;
template <> struct Reach<Hermite>
{
    static constexpr double before = 1.0;
    static constexpr double after = 2.0;
};
template <> struct Reach<Linear>
{
    static constexpr double before = 0.0;
    static constexpr double after = 1.0;
};
template <> struct Reach<Sinc>
{
    static constexpr double before = 7.0;
    static constexpr double after = 8.0;
};

// The reach each reader's live fade is for (Reader::fade): the Hermite's for Linear too, whose
// fade is LiveSource::fade (wider than its own taps, so conservative), and the sinc's own.
template <class R> struct FadeReach : Reach<R>
{
};
template <> struct FadeReach<Linear> : Reach<Hermite>
{
};

// The stretch the kernels give grain i read at decimation d (spec §6.2, §6.3): min(1, 1 / r) of
// its residual rate r = rate / d (exact, d a power of two).
float stretchOfGrain(const gg::GrainRows &rows, uint32_t i, uint32_t d = 1)
{
    return specStretch(rows.rate[i] / static_cast<double>(d));
}

// Requires the pool to hold grains at stretch 1 and grains stretched to 0.6 or below (residual
// rate 1/0.6 or above), so that a case run with Sinc reads at both.
void requireBothStretches(const gg::GrainRows &rows, uint32_t count, uint32_t d = 1)
{
    bool unstretched = false;
    bool stretched = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        unstretched = unstretched || stretchOfGrain(rows, i, d) == 1.0f;
        stretched = stretched || stretchOfGrain(rows, i, d) <= 0.6f;
    }
    REQUIRE(unstretched);
    REQUIRE(stretched);
}

// The decimations a reader's sample cases run at (spec §6.3): the sinc's, the HQ tier's, also at
// decimations 2 and 4 (levels 1 and 2), the only reader the engine reads above level 0 with; the
// others at 1 alone.
template <class R> std::vector<uint32_t> decimationsOf() { return {1}; }
template <> std::vector<uint32_t> decimationsOf<Sinc>() { return {1, 2, 4}; }

// The specs with their start position and rate scaled by d, exactly (a power of two): in level-0
// frames, so that read at a level of decimation d they read the same level frames, at the same
// residual rate, as the specs themselves read at decimation 1.
std::vector<gg::GrainSpec> atDecimation(std::vector<gg::GrainSpec> specs, uint32_t d)
{
    for (gg::GrainSpec &g : specs)
    {
        g.startPos *= static_cast<double>(d);
        g.rate *= static_cast<double>(d);
    }
    return specs;
}

// The exact condition under which the SIMD kernel may render output sample t of grain i, reading
// a level of `frames` frames at decimation d: the window's phase is in [0, 1] and every tap of
// the read at posL = pos / d (spec §6.3) is inside [0, frames).
template <class R>
bool simdable(const gg::GrainRows &rows, uint32_t i, int64_t frames, uint32_t d, int64_t t)
{
    const double e = static_cast<double>(t - rows.onsetSample[i]) - rows.onsetFrac[i];
    const double phase = e * rows.invDuration[i];
    const double pos = (rows.startPos[i] + rows.rate[i] * e) / static_cast<double>(d);
    const double base = std::floor(pos);
    return phase >= 0.0 && phase <= 1.0 && base - Reach<R>::before >= 0.0 &&
           base + Reach<R>::after < static_cast<double>(frames);
}

// Requires the SIMD run of every grain over [from, to) to be exactly the samples simdable()
// accepts before the grain's steal fade starts (every fading sample goes through the reference;
// spec §6.5), so the kernel falls back to the bounds-checked reads for exactly the others. It
// reaches into gg::kernels::detail on purpose (accepted ruling, Task 6): std::memcmp alone can miss
// an out-of-bounds read, since the memory just past a large buffer is often zero-filled, which
// reads the same as the reference's zero taps.
template <class R>
void requireExactRuns(const Pool &pool, int64_t frames, uint32_t d, int64_t from, int64_t to)
{
    const gg::GrainRows &rows = pool.rows();
    for (uint32_t i = 0; i < pool.size(); ++i)
    {
        const int64_t first = std::max(from, rows.onsetSample[i]);
        const int64_t last = std::min(to, rows.endSample[i]);
        if (first >= last)
        {
            continue;
        }
        const gg::kernels::detail::Run run =
            gg::kernels::detail::sampleSimdRun<R>(rows, i, frames, d, first, last);
        INFO("grain " << i << ", [" << first << ", " << last << "), run [" << run.begin << ", "
                      << run.end << ")");
        REQUIRE(first <= run.begin);
        REQUIRE(run.begin <= run.end);
        REQUIRE(run.end <= last);
        for (int64_t t = first; t < last; ++t)
        {
            const bool inRun = t >= run.begin && t < run.end;
            const bool want = simdable<R>(rows, i, frames, d, t) && t < rows.fadeStart[i];
            if (inRun != want)
            {
                INFO("t " << t);
                REQUIRE(inRun == want);
            }
        }
    }
}

// A stereo noise source. stereo() carries the interleaved copy the engine builds (§16 lever 2),
// which the SIMD kernel reads; stereoPlanar() leaves it out, so the kernel reads the planar
// channels. interleave() rebuilds the copy after left or right changes.
//
// Given a decimation d above 1, each is the same frames as a level of the pyramid lays them out
// (PreparedSource::Level, spec §6.3) at decimation d: the stereo views read interleaved pairs'
// channels at stride 2 (data {lv, lv + 1}), stereo() with the pairs too and stereoPlanar()
// without them, and mono() reads the left channel at stride 1. Those pairs are their own
// allocation of exactly 2 × frames floats, with none of a level's guard frames, so that under ASan
// a tap read past either end is a heap overflow.
struct Noise
{
    explicit Noise(int64_t frames, uint64_t seed)
        : frames(frames), left(noise(frames, seed, 0)), right(noise(frames, seed, 1))
    {
        interleave();
    }
    void interleave()
    {
        pairs.assign(left.data(), right.data(), frames);
        exactPairs.resize(static_cast<std::size_t>(2 * frames));
        for (int64_t f = 0; f < frames; ++f)
        {
            exactPairs[static_cast<std::size_t>(2 * f)] = left[static_cast<std::size_t>(f)];
            exactPairs[static_cast<std::size_t>(2 * f + 1)] = right[static_cast<std::size_t>(f)];
        }
    }
    SampleSource stereo(uint32_t d = 1) const
    {
        if (d == 1)
        {
            return {{left.data(), right.data()}, frames, false, pairs.data(), 1, 1};
        }
        return {{exactPairs.data(), exactPairs.data() + 1}, frames, false, exactPairs.data(), 2, d};
    }
    SampleSource stereoPlanar(uint32_t d = 1) const
    {
        if (d == 1)
        {
            return {{left.data(), right.data()}, frames, false, nullptr, 1, 1};
        }
        return {{exactPairs.data(), exactPairs.data() + 1}, frames, false, nullptr, 2, d};
    }
    SampleSource mono(uint32_t d = 1) const
    {
        return {{left.data(), left.data()}, frames, true, nullptr, 1, d};
    }
    int64_t frames;
    std::vector<float> left;
    std::vector<float> right;
    gg::InterleavedStereo pairs;
    std::vector<float> exactPairs;
};
} // namespace

TEMPLATE_TEST_CASE("simd sample kernel matches the reference", "[kernel]", Hermite, Linear, Sinc)
{
    // At each decimation the reader runs at (decimationsOf), with the grains' positions and rates
    // scaled to match (atDecimation): a level read at decimation d with positions and rates d
    // times as large reads the same floats at the same positions, so it renders the decimation-1
    // render's bits (spec §6.3's exact scaling).
    const Tables &tables = kernelTables();
    const Noise src(kNoiseFrames, 11);
    const Pool unscaled(randomGrains(21, 500, kNoiseFrames));
    const auto unscaledRender = renderBoth<TestType>(unscaled, src.stereo(), 2, 0, 4096, tables);
    for (const uint32_t d : decimationsOf<TestType>())
    {
        INFO("decimation " << d);
        const Pool pool(atDecimation(randomGrains(21, 500, kNoiseFrames), d));
        requireBothStretches(pool.rows(), pool.size(), d);
        {
            INFO("block [0, 4096)");
            auto [ref, simd] = renderBoth<TestType>(pool, src.stereo(d), 2, 0, 4096, tables);
            REQUIRE(anyNonZero(ref.ch[0]));
            REQUIRE(anyNonZero(ref.ch[1]));
            REQUIRE(sameBits(simd.ch[0], ref.ch[0]));
            REQUIRE(sameBits(simd.ch[1], ref.ch[1]));
            REQUIRE(sameBits(ref.ch[0], unscaledRender.first.ch[0]));
            REQUIRE(sameBits(ref.ch[1], unscaledRender.first.ch[1]));
            requireExactRuns<TestType>(pool, kNoiseFrames, d, 0, 4096);
        }
        for (uint64_t k = 0; k < 200; ++k)
        {
            const int64_t from = -4000 + static_cast<int64_t>(u01(31, 0, k) * 22000.0);
            const int64_t to = from + static_cast<int64_t>(u01(31, 1, k) * 512.0);
            INFO("call " << k << ", [" << from << ", " << to << ")");
            auto [ref, simd] = renderBoth<TestType>(pool, src.stereo(d), 2, from, to, tables);
            REQUIRE(sameBits(simd.ch[0], ref.ch[0]));
            REQUIRE(sameBits(simd.ch[1], ref.ch[1]));
            requireExactRuns<TestType>(pool, kNoiseFrames, d, from, to);
        }
    }
}

TEMPLATE_TEST_CASE("simd sample kernel matches the reference on sources of 1 to 4 frames",
                   "[kernel]", Hermite, Linear, Sinc)
{
    // Sources too short for the Hermite's 4 taps (1–3 frames) or just long enough (4), and for
    // the line's 2 taps (1 frame) or more. Each planar channel is its own allocation of exactly
    // its frames (an interleaved copy has 3 guard frames), so a tap read past either end of a
    // planar one is a heap overflow under ASan, which std::memcmp alone could miss. Grains start a
    // quarter frame at a time from 3 frames before the source to 3 past it, slow enough to stay
    // inside it for many samples, and each is rendered in calls of 1–7 samples at every alignment.
    // For the sinc, sources of 15 and 16 frames too: one frame short of its 16 taps, and just long
    // enough. The sinc also at decimations 2 and 4, over levels of those frames (Noise's views).
    const Tables &tables = kernelTables();
    for (const uint32_t d : decimationsOf<TestType>())
    {
        INFO("decimation " << d);
        const auto longest = std::max<int64_t>(
            4, static_cast<int64_t>(Reach<TestType>::before + Reach<TestType>::after) + 1);
        for (int64_t frames = 1; frames <= longest; ++frames)
        {
            if (frames > 4 && frames < longest - 1)
            {
                continue;
            }
            INFO(frames << " frames");
            const Noise src(frames, 71);
            std::vector<gg::GrainSpec> specs;
            uint64_t k = 0;
            for (int quarter = -12; quarter <= 4 * static_cast<int>(frames) + 12; ++quarter)
            {
                for (const double rate : {1.0 / 16.0, 0.25, 1.0, -0.25, 1.0 / 0.6})
                {
                    specs.push_back({
                        .index = k,
                        .onsetSample = static_cast<int64_t>(k % 8),
                        .onsetFrac = u01(73, 0, k),
                        .startPos = 0.25 * quarter,
                        .rate = rate,
                        .durationSamples = 16.0 + u01(73, 1, k) * 48.0,
                        .gain = 0.5f,
                        .pan = static_cast<float>(2.0 * u01(73, 2, k) - 1.0),
                    });
                    ++k;
                }
            }
            std::stable_sort(specs.begin(), specs.end(),
                             [](const gg::GrainSpec &a, const gg::GrainSpec &b) {
                                 return a.onsetSample < b.onsetSample;
                             });
            const Pool pool(atDecimation(specs, d));
            requireBothStretches(pool.rows(), pool.size(), d);
            int64_t end = 0;
            for (const gg::GrainSpec &g : specs)
            {
                end = std::max(end, gg::grainEndSample(g));
            }
            // The SIMD interior runs wherever the reader's reach fits in the source.
            const bool fits = Reach<TestType>::before + Reach<TestType>::after < frames;
            int64_t longestRun = 0;
            for (uint32_t i = 0; i < pool.size(); ++i)
            {
                const gg::kernels::detail::Run run = gg::kernels::detail::sampleSimdRun<TestType>(
                    pool.rows(), i, frames, d, pool.rows().onsetSample[i],
                    pool.rows().endSample[i]);
                longestRun = std::max(longestRun, run.end - run.begin);
            }
            REQUIRE((longestRun >= 4) == fits);

            struct Shape
            {
                const char *name;
                int outputs;
                SampleSource source;
            };
            for (const Shape &shape :
                 {Shape{"two outputs, stereo source", 2, src.stereo(d)},
                  Shape{"two outputs, planar stereo source", 2, src.stereoPlanar(d)},
                  Shape{"two outputs, mono source", 2, src.mono(d)},
                  Shape{"one output, stereo source", 1, src.stereo(d)}})
            {
                INFO(shape.name);
                Buffers ref(0, end, shape.outputs);
                renderAll<TestType>(Kernel::Ref, pool, shape.source, ref, 0, end, tables);
                REQUIRE(anyNonZero(ref.ch[0]));
                for (int64_t length = 1; length <= 7; ++length)
                {
                    INFO("calls of " << length);
                    Buffers simd(0, end, shape.outputs);
                    for (int64_t from = 0; from < end; from += length)
                    {
                        const int64_t to = std::min(end, from + length);
                        const gg::kernels::Output o{
                            {simd.ch[0].data() + from,
                             shape.outputs == 2 ? simd.ch[1].data() + from : nullptr},
                            from};
                        for (uint32_t i = 0; i < pool.size(); ++i)
                        {
                            gg::kernels::renderSampleSimd<TestType>(pool.rows(), i, shape.source, o,
                                                                    from, to, tables);
                        }
                        requireExactRuns<TestType>(pool, frames, d, from, to);
                    }
                    REQUIRE(sameBits(simd.ch[0], ref.ch[0]));
                    REQUIRE(sameBits(simd.ch[1], ref.ch[1]));
                }
            }
        }
    }
}

TEMPLATE_TEST_CASE("simd sample kernel matches the reference at the source ends", "[kernel]",
                   Hermite, Linear, Sinc)
{
    // Review focus 1. Grains whose reads come within 3 frames of frame 0 or of the last frame
    // (for the sinc, within 9: one past its reach), in steps of a quarter frame: some start there
    // and move away, some arrive there from
    // outside the source (at 0) or from inside it (at the end) at mid-life, and some end there.
    // Each is rendered in ragged calls, offset by 0–3 samples so the SIMD steps fall at every
    // alignment, and compared with one reference call over the whole span.
    const Tables &tables = kernelTables();
    const Noise src(kNoiseFrames, 13);
    const std::vector<double> rates = {0.25, 0.5, 1.0, 1.0 / 0.6, 1.5, 2.0, 3.0, 4.0};
    std::vector<gg::GrainSpec> specs;
    uint64_t k = 0;
    const int quarters = std::max(12, 4 * (static_cast<int>(Reach<TestType>::after) + 1));
    for (const double boundary : {0.0, static_cast<double>(kNoiseFrames - 1)})
    {
        for (int quarter = -quarters; quarter <= quarters; ++quarter)
        {
            for (const double rate : rates)
            {
                for (const double anchor : {0.0, 0.5, 1.0}) // the share of the life at the target
                {
                    const double duration = 64.0 + u01(41, 0, k) * 400.0;
                    const double target = boundary + 0.25 * quarter;
                    const double e = anchor * duration;
                    specs.push_back({
                        .index = k,
                        .onsetSample = 0,
                        .onsetFrac = u01(41, 1, k),
                        .startPos = target - rate * e,
                        .rate = rate,
                        .durationSamples = duration,
                        .gain = 0.5f,
                        .pan = static_cast<float>(2.0 * u01(41, 2, k) - 1.0),
                    });
                    ++k;
                }
            }
        }
    }
    int64_t end = 0;
    for (const gg::GrainSpec &g : specs)
    {
        end = std::max(end, gg::grainEndSample(g));
    }
    // The sinc also at decimations 2 and 4: the same ends of a level's frames.
    for (const uint32_t d : decimationsOf<TestType>())
    {
        INFO("decimation " << d);
        const Pool pool(atDecimation(specs, d));
        requireBothStretches(pool.rows(), pool.size(), d);
        Buffers ref(0, end, 2);
        renderAll<TestType>(Kernel::Ref, pool, src.stereo(d), ref, 0, end, tables);
        REQUIRE(anyNonZero(ref.ch[0]));
        REQUIRE(anyNonZero(ref.ch[1]));
        const std::vector<int64_t> lengths = {61, 64, 1, 128, 3, 250, 2, 37};
        for (int64_t offset = 0; offset < 4; ++offset)
        {
            INFO("offset " << offset);
            Buffers simd(0, end, 2);
            int64_t from = 0;
            for (std::size_t c = 0; from < end; ++c)
            {
                const int64_t to =
                    std::min(end, c == 0 ? offset : from + lengths[c % lengths.size()]);
                const gg::kernels::Output o{{simd.ch[0].data() + from, simd.ch[1].data() + from},
                                            from};
                for (uint32_t i = 0; i < pool.size(); ++i)
                {
                    gg::kernels::renderSampleSimd<TestType>(pool.rows(), i, src.stereo(d), o, from,
                                                            to, tables);
                }
                requireExactRuns<TestType>(pool, kNoiseFrames, d, from, to);
                from = to;
            }
            REQUIRE(sameBits(simd.ch[0], ref.ch[0]));
            REQUIRE(sameBits(simd.ch[1], ref.ch[1]));
        }
    }
}

TEMPLATE_TEST_CASE("simd sample kernel matches the reference at extreme rates and lengths",
                   "[kernel]", Hermite, Linear, Sinc)
{
    // Review focus 4: pitch ±48 semitones, and a 3,600 s grain at 768 kHz, whose elapsed time
    // and window phase pass 2^31 samples.
    const Tables &tables = kernelTables();
    const Noise src(kNoiseFrames, 17);

    SECTION("rates 1/16 and 16")
    {
        for (const uint32_t d : decimationsOf<TestType>())
        {
            INFO("decimation " << d);
            const Pool pool(
                atDecimation(randomGrains(23, 500, kNoiseFrames, {1.0 / 16.0, 16.0}), d));
            requireBothStretches(pool.rows(), pool.size(), d);
            auto [ref, simd] = renderBoth<TestType>(pool, src.stereo(d), 2, 0, 4096, tables);
            REQUIRE(anyNonZero(ref.ch[0]));
            REQUIRE(sameBits(simd.ch[0], ref.ch[0]));
            REQUIRE(sameBits(simd.ch[1], ref.ch[1]));
            requireExactRuns<TestType>(pool, kNoiseFrames, d, 0, 4096);
            for (uint64_t k = 0; k < 100; ++k)
            {
                const int64_t from = -4000 + static_cast<int64_t>(u01(33, 0, k) * 22000.0);
                const int64_t to = from + static_cast<int64_t>(u01(33, 1, k) * 512.0);
                INFO("call " << k << ", [" << from << ", " << to << ")");
                auto [r, s] = renderBoth<TestType>(pool, src.stereo(d), 2, from, to, tables);
                REQUIRE(sameBits(s.ch[0], r.ch[0]));
                REQUIRE(sameBits(s.ch[1], r.ch[1]));
            }
        }
    }

    SECTION("a 3,600 s grain at 768 kHz")
    {
        constexpr double kDuration = 3600.0 * 768000.0;
        REQUIRE(kDuration == 2764800000.0);
        constexpr int64_t kOnset = 1000;
        const int64_t end = kOnset + static_cast<int64_t>(kDuration) + 2;
        // Around the middle and up to the grain's last sample. Each grain's start position puts
        // its read in the middle of the source at the probe, so the probe reads real samples.
        for (const uint32_t d : decimationsOf<TestType>())
        {
            for (const int64_t probe : {kOnset + static_cast<int64_t>(kDuration / 2), end - 4000})
            {
                for (const double rate : {1.0 / 16.0, 1.0, 16.0})
                {
                    INFO("decimation " << d << ", probe " << probe << ", rate " << rate);
                    const double e = static_cast<double>(probe - kOnset);
                    const Pool pool(atDecimation(
                        {{
                            .index = 0,
                            .onsetSample = kOnset,
                            .onsetFrac = 0.6180339887,
                            .startPos = static_cast<double>(kNoiseFrames / 2) - rate * e,
                            .rate = rate,
                            .durationSamples = kDuration,
                            .gain = 0.9f,
                            .pan = -0.3f,
                        }},
                        d));
                    REQUIRE(pool.rows().endSample[0] == end);
                    const int64_t from = probe - 2000;
                    const int64_t to = std::min(probe + 4096, end + 5);
                    Buffers ref(from, to, 2);
                    renderAll<TestType>(Kernel::Ref, pool, src.stereo(d), ref, from, to, tables);
                    REQUIRE(anyNonZero(ref.ch[0]));
                    REQUIRE(anyNonZero(ref.ch[1]));
                    Buffers simd(from, to, 2);
                    for (int64_t a = from; a < to;)
                    {
                        const int64_t b = std::min(to, a + 509);
                        const gg::kernels::Output o{
                            {simd.ch[0].data() + (a - from), simd.ch[1].data() + (a - from)}, a};
                        gg::kernels::renderSampleSimd<TestType>(pool.rows(), 0, src.stereo(d), o, a,
                                                                b, tables);
                        a = b;
                    }
                    REQUIRE(sameBits(simd.ch[0], ref.ch[0]));
                    REQUIRE(sameBits(simd.ch[1], ref.ch[1]));
                    requireExactRuns<TestType>(pool, kNoiseFrames, d, from, to);
                    const gg::kernels::detail::Run run =
                        gg::kernels::detail::sampleSimdRun<TestType>(pool.rows(), 0, kNoiseFrames,
                                                                     d, from, std::min(to, end));
                    REQUIRE(run.end - run.begin > 1000); // the SIMD path did render it
                }
            }
        }
    }
}

TEMPLATE_TEST_CASE("simd sample kernel matches the reference for every output shape", "[kernel]",
                   Hermite, Linear, Sinc)
{
    // Review focus 5: one output (a null dst[1]), a mono source into one or two outputs, an
    // empty source, and calls of 0, 1, 2, 3 and 5 samples at every alignment against the grains'
    // onsets. The pool's short grains all start within [0, 64), the first with onsetFrac 0, so
    // its first window value is exactly 0 inside a SIMD step; the outputs start at −0 in one
    // pass, which a lane the window masks must leave alone, as the reference does.
    const Tables &tables = kernelTables();
    const Noise src(kNoiseFrames, 19);
    std::vector<gg::GrainSpec> specs = randomGrains(25, 64, kNoiseFrames);
    for (uint32_t k = 0; k < specs.size(); ++k)
    {
        specs[k].onsetSample = k;
        specs[k].startPos = 1000.0 + 1000.0 * k;
        specs[k].durationSamples = 16.0 + u01(43, 0, k) * 200.0;
    }
    specs[0].onsetFrac = 0.0;
    // The sinc also at decimations 2 and 4, an empty level among them.
    for (const uint32_t d : decimationsOf<TestType>())
    {
        INFO("decimation " << d);
        const Pool pool(atDecimation(specs, d));
        requireBothStretches(pool.rows(), pool.size(), d);

        struct Shape
        {
            const char *name;
            int outputs;
            SampleSource source;
        };
        const SampleSource empty{{nullptr, nullptr}, 0, true, nullptr, d == 1 ? 1 : 2, d};
        for (const Shape &shape :
             {Shape{"two outputs, stereo source", 2, src.stereo(d)},
              Shape{"two outputs, planar stereo source", 2, src.stereoPlanar(d)},
              Shape{"two outputs, mono source", 2, src.mono(d)},
              Shape{"one output, stereo source", 1, src.stereo(d)},
              Shape{"one output, mono source", 1, src.mono(d)},
              Shape{"two outputs, empty source", 2, empty},
              Shape{"one output, empty source", 1, empty}})
        {
            for (const float fill : {0.0f, -0.0f})
            {
                INFO(shape.name << ", outputs start at " << (std::signbit(fill) ? "-0" : "+0"));
                {
                    auto [ref, simd] = renderBoth<TestType>(pool, shape.source, shape.outputs, 0,
                                                            300, tables, fill);
                    REQUIRE(sameBits(simd.ch[0], ref.ch[0]));
                    REQUIRE(sameBits(simd.ch[1], ref.ch[1]));
                    REQUIRE(anyNonZero(ref.ch[0]) == (shape.source.frames > 0));
                    REQUIRE(!anyNonZero(ref.ch[1]) ==
                            (shape.outputs == 1 || shape.source.frames == 0));
                }
                for (const int64_t length : {0, 1, 2, 3, 5})
                {
                    for (int64_t from = -3; from < 300; ++from)
                    {
                        INFO("[" << from << ", " << from + length << ")");
                        auto [ref, simd] = renderBoth<TestType>(pool, shape.source, shape.outputs,
                                                                from, from + length, tables, fill);
                        REQUIRE(sameBits(simd.ch[0], ref.ch[0]));
                        REQUIRE(sameBits(simd.ch[1], ref.ch[1]));
                    }
                }
            }
        }
    }
}

TEMPLATE_TEST_CASE("simd sample kernel matches the reference with non-finite taps", "[kernel]",
                   Hermite, Linear, Sinc)
{
    // NaN, +inf and −inf frames scattered through both channels. The outputs compare equal with
    // NaN == NaN, since a NaN's payload may depend on the operand order the compiler picks.
    const Tables &tables = kernelTables();
    Noise src(kNoiseFrames, 29);
    constexpr float kInf = std::numeric_limits<float>::infinity();
    for (int64_t f = 0; f < kNoiseFrames; ++f)
    {
        const auto j = static_cast<std::size_t>(f);
        if (f % 997 == 3)
        {
            src.left[j] = kNaN;
        }
        if (f % 1009 == 5)
        {
            src.left[j] = kInf;
            src.right[j] = kNaN;
        }
        if (f % 1013 == 7)
        {
            src.right[j] = -kInf;
        }
    }
    src.interleave();
    for (const uint32_t d : decimationsOf<TestType>())
    {
        INFO("decimation " << d);
        const Pool pool(atDecimation(randomGrains(27, 500, kNoiseFrames), d));
        requireBothStretches(pool.rows(), pool.size(), d);
        auto [ref, simd] = renderBoth<TestType>(pool, src.stereo(d), 2, 0, 4096, tables);
        REQUIRE(anyNaN(ref.ch[0]));
        REQUIRE(anyNaN(ref.ch[1]));
        REQUIRE(!allFinite(ref.ch[0]));
        REQUIRE(std::any_of(ref.ch[0].begin(), ref.ch[0].end(),
                            [](float x) { return std::isfinite(x) && x != 0.0f; }));
        REQUIRE(sameOrBothNaN(simd.ch[0], ref.ch[0]));
        REQUIRE(sameOrBothNaN(simd.ch[1], ref.ch[1]));
        for (uint64_t k = 0; k < 50; ++k)
        {
            const int64_t from = -4000 + static_cast<int64_t>(u01(35, 0, k) * 22000.0);
            const int64_t to = from + static_cast<int64_t>(u01(35, 1, k) * 512.0);
            INFO("call " << k << ", [" << from << ", " << to << ")");
            auto [r, s] = renderBoth<TestType>(pool, src.stereo(d), 2, from, to, tables);
            REQUIRE(sameOrBothNaN(s.ch[0], r.ch[0]));
            REQUIRE(sameOrBothNaN(s.ch[1], r.ch[1]));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The SIMD live kernel, held to the reference bit for bit (std::memcmp).

namespace
{
constexpr double kLiveRate = 48000.0;
constexpr double kLiveSeconds = 0.5; // N = 24,000 frames
constexpr uint32_t kLiveMaxBlock = 511;
constexpr double kM = static_cast<double>(gg::LiveSource::kHeadMargin);

void prepareRing(gg::LiveSource &ring) { ring.prepare(kLiveRate, kLiveSeconds, kLiveMaxBlock); }

// A live ring's input over output samples [0, length), and its freeze schedule: frozen from
// toggles[0] to toggles[1], from toggles[2] to toggles[3], and so on.
struct LiveInput
{
    LiveInput(int64_t length, uint64_t seed, std::vector<int64_t> toggles)
        : length(length), left(noise(length, seed, 0)), right(noise(length, seed, 1)),
          toggles(std::move(toggles))
    {
    }
    int64_t length;
    std::vector<float> left;
    std::vector<float> right;
    std::vector<int64_t> toggles;
};

// count freeze toggles from sample 2,000 on, with gaps of 4,000 + [0, 2 × spread) samples,
// except every fifth gap, of 1–900 samples: inside the freeze ramp (960 samples at 48 kHz), so a
// freeze turns off while the gain ramps down, or on while it ramps up.
std::vector<int64_t> freezeToggles(uint64_t seed, int count, double spread)
{
    std::vector<int64_t> toggles;
    int64_t t = 2000;
    for (int k = 0; k < count; ++k)
    {
        const double u = u01(seed, 0, static_cast<uint64_t>(k));
        t += k % 5 == 4 ? 1 + static_cast<int64_t>(u * 900.0)
                        : 4000 + static_cast<int64_t>(u * 2.0 * spread);
        toggles.push_back(t);
    }
    return toggles;
}

// Writes the ring over [0, input.length) as the engine does: host blocks of `lengths` frames in
// turn, each split at the freeze toggles inside it, and each piece written whole, with its
// freeze setting, before render(from, to) renders it.
template <class Render>
void driveRing(gg::LiveSource &ring, const LiveInput &input, const std::vector<int64_t> &lengths,
               Render &&render)
{
    ring.reset();
    std::size_t toggle = 0;
    std::size_t block = 0;
    bool frozen = false;
    int64_t hostEnd = 0;
    for (int64_t from = 0; from < input.length;)
    {
        if (from == hostEnd)
        {
            hostEnd += lengths[block++ % lengths.size()];
        }
        while (toggle < input.toggles.size() && input.toggles[toggle] <= from)
        {
            frozen = !frozen;
            ++toggle;
        }
        int64_t to = std::min(hostEnd, input.length);
        if (toggle < input.toggles.size())
        {
            to = std::min(to, input.toggles[toggle]);
        }
        ring.setFrozen(frozen);
        const float *in[2] = {input.left.data() + from, input.right.data() + from};
        ring.write(in, 2, 0, from, static_cast<uint32_t>(to - from));
        render(from, to);
        from = to;
    }
}

// R(t) for every output sample: it depends only on the freeze schedule, not on the blocks.
std::vector<int64_t> headTrajectory(const LiveInput &input)
{
    gg::LiveSource ring;
    prepareRing(ring);
    std::vector<int64_t> heads(static_cast<std::size_t>(input.length));
    driveRing(ring, input, {kLiveMaxBlock}, [&](int64_t from, int64_t to) {
        for (int64_t t = from; t < to; ++t)
        {
            heads[static_cast<std::size_t>(t)] = ring.headAt(t);
        }
    });
    return heads;
}

// Live grains at the given onsets (in order), each starting `behind` frames behind the head at
// its onset sample, in kinds by index that reach every part of the interior's logic:
// - 0: short, 16–48 samples, so its interior is a few samples or none;
// - 1: rate 1, 4–255 frames behind the head: in the head's fade all its life while the head
//   advances, and closer still once it stops, so its interior is empty;
// - 2: rate 1, within 255 frames of the window's oldest frame: in the tail's fade while the head
//   advances, so its interior is empty unless a freeze stops the head;
// - 3: rate exactly 1, which makes R − pos constant while the head advances;
// - 4–7: rates 2^[−2, 2], anywhere from 300 frames past the head to 300 before the window.
// Durations are 16–9,600 samples, except kind 0's; textures are in [0, 1) (spec §4), every third
// exactly 0 (the plain Hann, which the SIMD interior renders without the warp), so every kind
// has grains of both.
std::vector<gg::GrainSpec> liveGrains(uint64_t seed, const std::vector<int64_t> &onsets,
                                      const std::vector<int64_t> &heads, int64_t ringFrames)
{
    std::vector<gg::GrainSpec> specs;
    const auto n = static_cast<double>(ringFrames);
    for (std::size_t k = 0; k < onsets.size(); ++k)
    {
        const auto uk = static_cast<uint64_t>(k);
        const int kind = static_cast<int>(k % 8);
        const double u = u01(seed, 2, uk);
        double rate = std::exp2(-2.0 + 4.0 * u01(seed, 0, uk));
        double behind = -300.0 + u * (n + 600.0);
        double duration = 16.0 + u01(seed, 3, uk) * (9600.0 - 16.0);
        switch (kind)
        {
        case 0:
            duration = 16.0 + u01(seed, 3, uk) * 32.0;
            break;
        case 1:
            rate = 1.0;
            behind = 4.0 + u * (kM - 5.0);
            break;
        case 2:
            rate = 1.0;
            behind = n - 2.0 - u * (kM - 1.0);
            break;
        case 3:
            rate = 1.0;
            break;
        default:
            break;
        }
        const int64_t onset = onsets[k];
        specs.push_back({
            .index = uk,
            .onsetSample = onset,
            .onsetFrac = u01(seed, 1, uk),
            .startPos = static_cast<double>(heads[static_cast<std::size_t>(onset)]) - behind,
            .rate = rate,
            .durationSamples = duration,
            .texture = k % 3 == 0 ? 0.0 : u01(seed, 6, uk),
            .gain = 0.25f + 0.75f * static_cast<float>(u01(seed, 4, uk)),
            .pan = static_cast<float>(2.0 * u01(seed, 5, uk) - 1.0),
        });
    }
    return specs;
}

// count onsets spread evenly over [0, length), in order.
std::vector<int64_t> spreadOnsets(int64_t length, uint32_t count)
{
    std::vector<int64_t> onsets;
    for (uint32_t k = 0; k < count; ++k)
    {
        onsets.push_back(static_cast<int64_t>(k) * length / count);
    }
    return onsets;
}

// How the SIMD live kernel split its calls: samples it rendered 4-wide, calls whose head-still
// part (from blockKink() on) had a 4-wide step, calls with an interior on both sides of the
// kink, and calls over a sounding part of a grain with no interior at all.
struct LiveStats
{
    int64_t simdSamples = 0;
    int64_t stillSimdCalls = 0;
    int64_t bothPartsCalls = 0;
    int64_t emptyCalls = 0;
};

// The grains of a pool (in onset order) that may sound in each piece of a pass over increasing
// [from, to): each(from, to, f) calls f(i) for them in spawn order, skipping the grains that
// finished long before from.
struct ActiveGrains
{
    explicit ActiveGrains(const Pool &pool) : rows(pool.rows()), n(pool.size())
    {
        for (uint32_t i = 0; i < n; ++i)
        {
            maxLife = std::max(maxLife, rows.endSample[i] - rows.onsetSample[i]);
        }
    }
    template <class F> void each(int64_t from, int64_t to, F &&f)
    {
        while (lo < n && rows.onsetSample[lo] + maxLife <= from)
        {
            ++lo;
        }
        while (hi < n && rows.onsetSample[hi] < to)
        {
            ++hi;
        }
        for (uint32_t i = lo; i < hi; ++i)
        {
            f(i);
        }
    }
    const gg::GrainRows &rows;
    uint32_t n;
    int64_t maxLife = 0;
    uint32_t lo = 0;
    uint32_t hi = 0;
};

struct LiveRenders
{
    Buffers ref;
    Buffers simd;
    LiveStats stats;
    int64_t finalHead;
};

// Drives a fresh ring over the input in `lengths`, and renders every grain of the pool over each
// piece, in spawn order, with renderLiveRef<R> and renderLiveSimd<R>, into separate outputs.
template <class R>
LiveRenders renderLiveBoth(const Pool &pool, const LiveInput &input,
                           const std::vector<int64_t> &lengths, int outputs, const Tables &tables)
{
    LiveRenders r{Buffers(0, input.length, outputs), Buffers(0, input.length, outputs), {}, 0};
    const gg::kernels::Output refOut = r.ref.output();
    const gg::kernels::Output simdOut = r.simd.output();
    const gg::GrainRows &rows = pool.rows();
    ActiveGrains active(pool);
    gg::LiveSource ring;
    prepareRing(ring);
    driveRing(ring, input, lengths, [&](int64_t from, int64_t to) {
        active.each(from, to, [&](uint32_t i) {
            gg::kernels::renderLiveRef<R>(rows, i, ring, 1, refOut, from, to, tables);
            gg::kernels::renderLiveSimd<R>(rows, i, ring, 1, simdOut, from, to, tables);
            if (std::max(from, rows.onsetSample[i]) >= std::min(to, rows.endSample[i]))
            {
                return;
            }
            const gg::kernels::LiveInterior in =
                gg::kernels::liveInterior<R>(rows, i, ring, 1, from, to);
            const int64_t moving = (in.end[0] - in.begin[0]) / 4 * 4;
            const int64_t still = (in.end[1] - in.begin[1]) / 4 * 4;
            r.stats.simdSamples += moving + still;
            r.stats.stillSimdCalls += still > 0;
            r.stats.bothPartsCalls += in.end[0] > in.begin[0] && in.end[1] > in.begin[1];
            r.stats.emptyCalls += in.end[0] == in.begin[0] && in.end[1] == in.begin[1];
        });
    });
    r.finalHead = ring.head();
    return r;
}
} // namespace

// The interior's conditions at sample t of grain i, each on its own, computed afresh from the
// rows and the ring, twice over. For a sample inside the interior where the test's own evaluation
// found them not all holding: a condition that fails here with its value far from its bound, or
// that holds here, is a transient fault of the machine, not of the interior. CI runs 40818957844
// (attempt 1) and 40835080133 each failed one such sample, in blocks of 7 under Rosetta on the
// macOS runner, far inside every margin (3,849 and 20,147 frames at the second's), and the same
// binary passed when run again.
template <class R>
std::string describeBadSample(const gg::GrainRows &rows, uint32_t i, const gg::LiveSource &ring,
                              int64_t t, int64_t frames)
{
    std::ostringstream out;
    out.precision(17);
    for (int pass = 1; pass <= 2; ++pass)
    {
        const double e = static_cast<double>(t - rows.onsetSample[i]) - rows.onsetFrac[i];
        const double phase = e * rows.invDuration[i];
        const double pos = rows.startPos[i] + rows.rate[i] * e;
        const int64_t head = ring.headAt(t);
        const auto r = static_cast<double>(head);
        gg::LiveSource::Cursor cursor;
        float left = 0.0f;
        float right = 0.0f;
        const bool phaseOk = phase >= 0.0 && phase <= 1.0;
        const bool posOk = std::floor(pos) - Reach<R>::before >= 0.0;
        const bool fadeOk = R::fade(ring, pos, head) == 1.0;
        const bool readOk =
            R::readLive(ring, pos, head, cursor, readParams(stretchOfGrain(rows, i)), left, right);
        out << "; evaluation " << pass << ": phase " << phase << (phaseOk ? " ok" : " BAD")
            << ", pos " << pos << (posOk ? " ok" : " BAD") << ", head " << head << ", fade "
            << R::fade(ring, pos, head) << (fadeOk ? " ok" : " BAD") << ", read "
            << (readOk ? "ok" : "BAD") << ", margins " << r - 1.0 - (pos + FadeReach<R>::after) - kM
            << " and " << (pos - FadeReach<R>::before) - (r - static_cast<double>(frames)) - kM
            << " past M" << (phaseOk && posOk && fadeOk && readOk ? " (holds)" : " (fails)");
    }
    return out.str();
}

TEMPLATE_TEST_CASE("live interior is exactly where the fade is one", "[kernel]", Hermite, Linear,
                   Sinc)
{
    // 1,000 grains over a ring frozen and released 12 times. For every sample of every interior,
    // in blocks of 7 and 511: the window's phase is in [0, 1], every tap is a frame at or after
    // frame 0, the reference's fade is exactly 1.0, and the reader's read succeeds. The interior
    // is conservative (one frame inside the fade's margins), but it must still hold nearly all of
    // the samples where those hold, and some interiors must be empty: short grains, and grains in
    // a fade all their life. Every interior sample also keeps both of its fade's margins (the
    // Hermite's for Hermite and Linear, the sinc's for Sinc) at least M + 1 − 2^−10 frames:
    // liveRun admits a run only where they are >= M + 1 at its ends, and between them they can
    // fall short of that only by rounding (of order 2^−12 frames).
    const LiveInput input(300000, 51, freezeToggles(53, 12, 12000.0));
    const std::vector<int64_t> heads = headTrajectory(input);
    gg::LiveSource ring;
    prepareRing(ring);
    const int64_t frames = ring.frames();
    REQUIRE(frames == 24000);
    const Pool pool(liveGrains(55, spreadOnsets(input.length, 1000), heads, frames));
    const gg::GrainRows &rows = pool.rows();
    requireBothStretches(rows, pool.size());

    for (const int64_t block : {7, 511})
    {
        INFO("blocks of " << block);
        int64_t interiorSamples = 0;
        int64_t fadeOneSamples = 0; // every sample where the interior's conditions hold
        int64_t emptyCalls = 0;
        int64_t badSamples = 0;
        int64_t badMargins = 0;
        double minMargin = std::numeric_limits<double>::infinity();
        int64_t badStructure = 0;
        int64_t firstBad = -1;
        uint32_t firstBadGrain = 0;
        std::string firstBadDetail;
        ActiveGrains active(pool);
        driveRing(ring, input, {block}, [&](int64_t from, int64_t to) {
            const int64_t kink = ring.blockKink();
            REQUIRE(kink >= from);
            REQUIRE(kink <= to);
            active.each(from, to, [&](uint32_t i) {
                const int64_t first = std::max(from, rows.onsetSample[i]);
                const int64_t last = std::min(to, rows.endSample[i]);
                const gg::kernels::LiveInterior in =
                    gg::kernels::liveInterior<TestType>(rows, i, ring, 1, from, to);
                if (first >= last)
                {
                    badStructure += in.begin[0] != in.end[0] || in.begin[1] != in.end[1];
                    return;
                }
                // The moving part lies before the kink, the still part from it on.
                badStructure += !(first <= in.begin[0] && in.begin[0] <= in.end[0] &&
                                  in.end[0] <= std::max(first, std::min(kink, last)) &&
                                  std::max(first, std::min(kink, last)) <= in.begin[1] &&
                                  in.begin[1] <= in.end[1] && in.end[1] <= last);
                emptyCalls += in.begin[0] == in.end[0] && in.begin[1] == in.end[1];
                gg::LiveSource::Cursor cursor;
                for (int64_t t = first; t < last; ++t)
                {
                    const double e =
                        static_cast<double>(t - rows.onsetSample[i]) - rows.onsetFrac[i];
                    const double phase = e * rows.invDuration[i];
                    const double pos = rows.startPos[i] + rows.rate[i] * e;
                    const int64_t head = ring.headAt(t);
                    float l = 0.0f;
                    float r = 0.0f;
                    const bool holds =
                        phase >= 0.0 && phase <= 1.0 &&
                        std::floor(pos) - Reach<TestType>::before >= 0.0 &&
                        TestType::fade(ring, pos, head) == 1.0 &&
                        TestType::readLive(ring, pos, head, cursor,
                                           readParams(stretchOfGrain(rows, i)), l, r);
                    const bool inside =
                        (t >= in.begin[0] && t < in.end[0]) || (t >= in.begin[1] && t < in.end[1]);
                    fadeOneSamples += holds;
                    interiorSamples += inside;
                    if (inside)
                    {
                        // The margins as liveRun states them: toward the head and from the tail.
                        const auto rt = static_cast<double>(head);
                        const double toHead = rt - 1.0 - (pos + FadeReach<TestType>::after);
                        const double fromTail = (pos - FadeReach<TestType>::before) -
                                                (rt - static_cast<double>(frames));
                        const double margin = std::min(toHead, fromTail);
                        minMargin = std::min(minMargin, margin);
                        badMargins += !(margin >= kM + 1.0 - 0x1.0p-10);
                    }
                    if (inside && !holds)
                    {
                        if (badSamples == 0)
                        {
                            firstBad = t;
                            firstBadGrain = i;
                            firstBadDetail = describeBadSample<TestType>(rows, i, ring, t, frames);
                        }
                        ++badSamples;
                    }
                }
            });
        });
        INFO("first bad sample " << firstBad << " of grain " << firstBadGrain << firstBadDetail);
        INFO("smallest margin " << minMargin - kM << " frames past M");
        REQUIRE(badStructure == 0);
        REQUIRE(badSamples == 0);
        REQUIRE(badMargins == 0);
        REQUIRE(minMargin < kM + 1.0 + 0x1.0p-6); // some interior reaches the bound, so it bites
        REQUIRE(emptyCalls > 0);
        REQUIRE(interiorSamples > 1000000);
        REQUIRE(static_cast<double>(interiorSamples) > 0.97 * static_cast<double>(fadeOneSamples));
    }
}

TEMPLATE_TEST_CASE("simd live kernel matches the reference", "[kernel]", Hermite, Linear, Sinc)
{
    // 300 grains at rates 0.25–4 over a ring that is never frozen, in ragged blocks and in
    // blocks of 511, into two outputs and into one.
    const Tables &tables = kernelTables();
    const LiveInput input(100000, 61, {});
    const std::vector<int64_t> heads = headTrajectory(input);
    const Pool pool(liveGrains(63, spreadOnsets(input.length, 300), heads, 24000));
    requireBothStretches(pool.rows(), pool.size());
    const std::vector<int64_t> ragged = {61, 64, 1, 128, 3, 250, 2, 37, 511};
    for (const std::vector<int64_t> &lengths : {ragged, std::vector<int64_t>{511}})
    {
        for (const int outputs : {2, 1})
        {
            INFO(outputs << " outputs, first block " << lengths[0]);
            const LiveRenders r = renderLiveBoth<TestType>(pool, input, lengths, outputs, tables);
            REQUIRE(anyNonZero(r.ref.ch[0]));
            REQUIRE(anyNonZero(r.ref.ch[1]) == (outputs == 2));
            REQUIRE(r.stats.simdSamples > 200000);
            REQUIRE(r.stats.emptyCalls > 0);
            REQUIRE(sameBits(r.simd.ch[0], r.ref.ch[0]));
            REQUIRE(sameBits(r.simd.ch[1], r.ref.ch[1]));
        }
    }
}

TEMPLATE_TEST_CASE("simd live kernel matches the reference through freeze and wrap", "[kernel]",
                   Hermite, Linear, Sinc)
{
    // Review focus 2. A 0.5 s ring, written until it has wrapped 20 times, frozen and released
    // at 30 random samples, some inside the freeze ramp and nearly all inside a block. 300
    // grains, half of them starting near a toggle, plus at every freeze one reading through the
    // moment the head stops. In blocks of 1, 7, 64 and 511: the head stops inside a block, so
    // an interior has a part before the kink and a part after it, with the edges and the fade
    // through the reference. Every block size must also give the same output.
    const Tables &tables = kernelTables();
    const std::vector<int64_t> toggles = freezeToggles(71, 30, 28000.0);
    int64_t frozenTotal = 0;
    for (std::size_t k = 0; k + 1 < toggles.size(); k += 2)
    {
        frozenTotal += toggles[k + 1] - toggles[k];
    }
    gg::LiveSource probe;
    prepareRing(probe);
    const int64_t capacity = probe.view().capacity;
    REQUIRE(capacity == 24000 + kLiveMaxBlock);
    // The head advances on every sample that isn't frozen.
    const int64_t length = std::max(toggles.back() + 20000, 20 * capacity + frozenTotal + 2000);
    const LiveInput input(length, 73, toggles);
    const std::vector<int64_t> heads = headTrajectory(input);

    std::vector<int64_t> onsets = spreadOnsets(length - 10000, 150);
    for (uint32_t k = 0; k < 150; ++k)
    {
        const int64_t toggle = toggles[k % toggles.size()];
        onsets.push_back(
            std::max<int64_t>(0, toggle - 12000 + static_cast<int64_t>(u01(75, 0, k) * 16000.0)));
    }
    std::sort(onsets.begin(), onsets.end());
    std::vector<gg::GrainSpec> specs = liveGrains(77, onsets, heads, 24000);
    // At each freeze, a grain from 200 samples on, through the head's stop 960 samples later.
    for (std::size_t k = 0; k < toggles.size(); k += 2)
    {
        const int64_t onset = toggles[k] + 200;
        specs.push_back({
            .index = 0,
            .onsetSample = onset,
            .onsetFrac = 0.375,
            .startPos = static_cast<double>(heads[static_cast<std::size_t>(onset)]) - 12000.0,
            .rate = 0.5 + 0.1 * static_cast<double>(k),
            .durationSamples = 3000.0,
            .gain = 0.75f,
            .pan = 0.25f,
        });
    }
    std::stable_sort(specs.begin(), specs.end(),
                     [](const gg::GrainSpec &a, const gg::GrainSpec &b) {
                         return a.onsetSample < b.onsetSample;
                     });
    for (std::size_t k = 0; k < specs.size(); ++k)
    {
        specs[k].index = k;
    }
    const Pool pool(specs);
    requireBothStretches(pool.rows(), pool.size());

    std::vector<float> first[2];
    for (const int64_t block : {511, 64, 7, 1})
    {
        INFO("blocks of " << block);
        const LiveRenders r = renderLiveBoth<TestType>(pool, input, {block}, 2, tables);
        REQUIRE(r.finalHead >= 20 * capacity);
        REQUIRE(anyNonZero(r.ref.ch[0]));
        REQUIRE(anyNonZero(r.ref.ch[1]));
        REQUIRE(r.stats.emptyCalls > 0);
        if (block >= 64)
        {
            REQUIRE(r.stats.simdSamples > 500000);
            REQUIRE(r.stats.stillSimdCalls > 0);
            REQUIRE(r.stats.bothPartsCalls > 0);
        }
        REQUIRE(sameBits(r.simd.ch[0], r.ref.ch[0]));
        REQUIRE(sameBits(r.simd.ch[1], r.ref.ch[1]));
        if (block == 511)
        {
            first[0] = r.simd.ch[0];
            first[1] = r.simd.ch[1];
        }
        REQUIRE(sameBits(r.simd.ch[0], first[0]));
        REQUIRE(sameBits(r.simd.ch[1], first[1]));
    }
    {
        INFO("one output, blocks of 64");
        const LiveRenders r = renderLiveBoth<TestType>(pool, input, {64}, 1, tables);
        REQUIRE(sameBits(r.simd.ch[0], r.ref.ch[0]));
        REQUIRE(!anyNonZero(r.simd.ch[1]));
    }
}

namespace
{
// Sinc, but it counts its live reads and those any of whose 16 taps, floor(pos) − 7 ..
// floor(pos) + 8, is outside the window [head − N, head − 1]: the reference must never ask for
// one, since the HQ fade is 0 before any tap leaves the window.
struct SincWindowProbe : Sinc
{
    static inline int64_t reads = 0;
    static inline int64_t outside = 0;

    static bool readLive(const gg::LiveSource &live, double pos, int64_t head,
                         gg::LiveSource::Cursor &cursor, const gg::kernels::ReadParams &p, float &l,
                         float &r) noexcept SST_CPPUTILS_NONBLOCKING
    {
        ++reads;
        const double base = std::floor(pos);
        outside += base - 7.0 < static_cast<double>(head - live.frames()) ||
                   base + 8.0 > static_cast<double>(head - 1);
        return Sinc::readLive(live, pos, head, cursor, p, l, r);
    }
};
} // namespace

TEST_CASE("hq live reads nothing beyond the window", "[kernel]")
{
    // Review Focus 5. A 0.5 s ring frozen from sample 30,000: the head stops 960 samples later
    // and stays, so the window [R − N, R − 1] is fixed, and its two ends meet at the seam. One
    // grain at a time reads up to and across them: at even indices from below the newest frame
    // at rates 1 to 2 (stretch 1 to 0.5) into and past it, at odd ones from below the oldest
    // frame into the window. Wherever any of a read's 16 taps would be outside the window, both
    // kernels' output is exactly +0, and the reference never asks the reader for such a read.
    const LiveInput input(60000, 81, {30000});
    const std::vector<int64_t> heads = headTrajectory(input);
    const int64_t stopped = heads.back();
    REQUIRE(heads[31000] == stopped);
    constexpr int64_t kFrames = 24000;
    const std::vector<double> rates = {1.0, 1.0 / 0.6, 1.25, 2.0, 0.5, 1.5};
    std::vector<gg::GrainSpec> specs;
    for (uint32_t k = 0; k < 12; ++k)
    {
        const double rate = rates[k % rates.size()];
        const bool towardHead = k % 2 == 0;
        specs.push_back({
            .index = k,
            .onsetSample = 31000 + 2100 * static_cast<int64_t>(k),
            .onsetFrac = 0.3,
            .startPos = towardHead ? static_cast<double>(stopped) - 600.0 - 25.0 * k
                                   : static_cast<double>(stopped - kFrames) - 700.0 + 25.0 * k,
            .rate = rate,
            .durationSamples = 2000.0,
            .gain = 0.75f,
            .pan = 0.25f,
        });
    }
    const Pool pool(specs);
    const gg::GrainRows &rows = pool.rows();
    requireBothStretches(rows, pool.size());

    for (const int64_t block : {511, 64, 7})
    {
        INFO("blocks of " << block);
        SincWindowProbe::reads = 0;
        SincWindowProbe::outside = 0;
        const LiveRenders r =
            renderLiveBoth<SincWindowProbe>(pool, input, {block}, 2, kernelTables());
        REQUIRE(SincWindowProbe::reads > 0);
        REQUIRE(SincWindowProbe::outside == 0);
        REQUIRE(sameBits(r.simd.ch[0], r.ref.ch[0]));
        REQUIRE(sameBits(r.simd.ch[1], r.ref.ch[1]));
        REQUIRE(r.stats.simdSamples > 1000);

        int64_t beyond = 0;   // sounding samples with a tap outside the window
        int64_t sounding = 0; // and with every tap inside, nonzero
        for (uint32_t i = 0; i < pool.size(); ++i)
        {
            for (int64_t t = rows.onsetSample[i]; t < rows.endSample[i]; ++t)
            {
                const double e = static_cast<double>(t - rows.onsetSample[i]) - rows.onsetFrac[i];
                if (!(kernelTables().hann.at(e * rows.invDuration[i]) > 0.0f))
                {
                    continue;
                }
                const double base = std::floor(rows.startPos[i] + rows.rate[i] * e);
                const auto head = static_cast<double>(heads[static_cast<std::size_t>(t)]);
                const bool out =
                    base - 7.0 < head - static_cast<double>(kFrames) || base + 8.0 > head - 1.0;
                const auto j = static_cast<std::size_t>(t);
                for (int c = 0; c < 2; ++c)
                {
                    if (out &&
                        (!sameBits(r.ref.ch[c][j], kZero) || !sameBits(r.simd.ch[c][j], kZero)))
                    {
                        INFO("grain " << i << ", t " << t << ", channel " << c);
                        REQUIRE(sameBits(r.ref.ch[c][j], kZero));
                        REQUIRE(sameBits(r.simd.ch[c][j], kZero));
                    }
                }
                beyond += out;
                sounding += !out && r.ref.ch[0][j] != 0.0f;
            }
        }
        REQUIRE(beyond > 2000);
        REQUIRE(sounding > 2000);
    }
}

namespace
{
// The steal fade's factor at output sample t (spec §6.5) of a grain stolen at `fadeStart`, which
// then ends at `end`, its fade F = end − fadeStart samples long, written out in float:
// min(1, float(end − t) × (1 / F)) from the steal on, and 1 before it.
float specFade(int64_t fadeStart, int64_t end, int64_t t)
{
    if (t < fadeStart)
    {
        return 1.0f;
    }
    const float invFade = 1.0f / static_cast<float>(end - fadeStart);
    return std::min(1.0f, static_cast<float>(end - t) * invFade);
}

// Each reader's read at pos from a planar channel, as the spec writes it (specSample's `read`):
// the Hermite and the line through interp, and the sinc written out at the grain's stretch.
template <class R> float specRead(const float *data, int64_t frames, double pos, float stretch);
template <>
float specRead<Hermite>(const float *data, int64_t frames, double pos, float /*stretch*/)
{
    return gg::interp::hermite4<>(data, frames, pos);
}
template <> float specRead<Linear>(const float *data, int64_t frames, double pos, float /*stretch*/)
{
    return gg::interp::linear<>(data, frames, pos);
}
template <> float specRead<Sinc>(const float *data, int64_t frames, double pos, float stretch)
{
    return specSinc(data, frames, pos, stretch);
}

// One steal per grain, by its index: none for k % 4 == 0; for 1, somewhere in its life at least
// kStealFade before its end, so F is the full 256; for 2, within 255 samples of its end, so F is
// what it has left, 1 to 255; for 3, at its onset or up to 3 samples after, where its window is
// still near 0. A grain shorter than kStealFade gets the third kind's steal.
std::vector<Steal> stealsFor(const std::vector<gg::GrainSpec> &specs, uint64_t seed)
{
    std::vector<Steal> steals;
    for (uint32_t k = 0; k < specs.size(); ++k)
    {
        const gg::GrainSpec &g = specs[k];
        const int64_t end = gg::grainEndSample(g);
        const int64_t life = end - g.onsetSample;
        const double u = u01(seed, 0, k);
        int64_t onset = g.onsetSample + static_cast<int64_t>(u * 4.0);
        switch (k % 4)
        {
        case 0:
            continue;
        case 1:
            if (life > gg::kStealFade)
            {
                onset = g.onsetSample +
                        static_cast<int64_t>(u * static_cast<double>(life - gg::kStealFade));
            }
            break;
        case 2:
            onset = std::max(g.onsetSample, end - 1 - static_cast<int64_t>(u * 255.0));
            break;
        default:
            break;
        }
        steals.push_back({k, std::min(onset, end - 1)});
    }
    return steals;
}

// Requires each stolen grain's rows to hold the steal as spec §6.5 states it: F = min(kStealFade,
// end − onset) with the grain's own end, the new end onset + F, fadeStart the onset and invFade
// 1 / F; and every other grain's rows the unstolen grain's (kNeverStolen, invFade 0, its own end).
// Requires some fades of the full 256 samples and some shorter, down to 1.
void requireStolenRows(const Pool &pool, const std::vector<gg::GrainSpec> &specs,
                       const std::vector<Steal> &steals)
{
    const gg::GrainRows &rows = pool.rows();
    std::vector<int64_t> stolenAt(specs.size(), gg::kNeverStolen);
    for (const Steal &s : steals)
    {
        stolenAt[s.row] = s.onset;
    }
    int64_t full = 0;
    int64_t shorter = 0;
    for (uint32_t i = 0; i < specs.size(); ++i)
    {
        INFO("grain " << i);
        const int64_t end = gg::grainEndSample(specs[i]);
        if (stolenAt[i] == gg::kNeverStolen)
        {
            REQUIRE(rows.fadeStart[i] == gg::kNeverStolen);
            REQUIRE(sameBits(rows.invFade[i], 0.0f));
            REQUIRE(rows.endSample[i] == end);
            continue;
        }
        const int64_t f = std::min(gg::kStealFade, end - stolenAt[i]);
        REQUIRE(rows.fadeStart[i] == stolenAt[i]);
        REQUIRE(rows.endSample[i] == stolenAt[i] + f);
        REQUIRE(sameBits(rows.invFade[i], 1.0f / static_cast<float>(f)));
        full += f == gg::kStealFade;
        shorter += f < gg::kStealFade;
    }
    REQUIRE(full > 10);
    REQUIRE(shorter > 10);
}
} // namespace

TEMPLATE_TEST_CASE("a stolen grain fades through the reference and the simd kernels alike",
                   "[kernel]", Hermite, Linear, Sinc)
{
    // Spec §6.5. A pool of grains, three in four stolen (stealsFor): in the window's rise, in
    // its body and near its end. Over a sample source, every sample of every grain, the fading
    // ones and the rest, is the spec's loop with the fade written out (specFade), bit for bit,
    // and the SIMD kernel's output is the reference's, in one call and in 200 random ones, its
    // runs ending at each fade's start. Then the same over the live ring, frozen and released,
    // where the fade multiplies the window before the head-proximity fade does.
    const Tables &tables = kernelTables();
    constexpr int64_t kLength = 12500;

    SECTION("sample source")
    {
        const Noise src(kNoiseFrames, 91);
        // 120 grains, an onset every 50 samples, 1,000 to 6,000 samples long, at rates 2^[−2, 2],
        // reading inside the source except every tenth, which reads across its end.
        std::vector<gg::GrainSpec> specs;
        for (uint32_t k = 0; k < 120; ++k)
        {
            const double rate = std::exp2(-2.0 + 4.0 * u01(93, 0, k));
            const double duration = 1000.0 + u01(93, 3, k) * 5000.0;
            const double start = k % 10 == 9 ? static_cast<double>(kNoiseFrames) - 500.0 -
                                                   u01(93, 2, k) * duration * rate
                                             : 300.0 + u01(93, 2, k) * 48000.0;
            specs.push_back({
                .index = k,
                .onsetSample = 50 * static_cast<int64_t>(k),
                .onsetFrac = u01(93, 1, k),
                .startPos = start,
                .rate = rate,
                .durationSamples = duration,
                .texture = k % 3 == 0 ? 0.0 : u01(93, 6, k),
                .gain = 0.25f + 0.75f * static_cast<float>(u01(93, 4, k)),
                .pan = static_cast<float>(2.0 * u01(93, 5, k) - 1.0),
            });
            REQUIRE(gg::grainEndSample(specs.back()) <= kLength);
        }
        const std::vector<Steal> steals = stealsFor(specs, 95);
        const Pool pool(specs, steals);
        const gg::GrainRows &rows = pool.rows();
        requireStolenRows(pool, specs, steals);
        requireBothStretches(rows, pool.size());

        // The fading samples the SIMD interior would take but for the fade: the run's bound bites.
        int64_t fadingSimdable = 0;
        for (uint32_t i = 0; i < pool.size(); ++i)
        {
            for (int64_t t = std::max(rows.onsetSample[i], rows.fadeStart[i]);
                 t < rows.endSample[i]; ++t)
            {
                fadingSimdable += simdable<TestType>(rows, i, kNoiseFrames, 1, t);
            }
        }
        REQUIRE(fadingSimdable > 5000);

        struct Shape
        {
            const char *name;
            int outputs;
            bool mono;
        };
        for (const Shape shape : {Shape{"two outputs, stereo source", 2, false},
                                  Shape{"two outputs, mono source", 2, true},
                                  Shape{"one output, stereo source", 1, false}})
        {
            INFO(shape.name);
            const float *const channels[2] = {src.left.data(),
                                              shape.mono ? src.left.data() : src.right.data()};
            const SampleSource source = shape.mono ? src.mono() : src.stereoPlanar();
            // The spec's loop, grain by grain in spawn order: e, w (skip unless > 0), w × the
            // fade from the steal on, pos, then dst[c][t] += read_c(pos) × (w × gain_c), up to
            // the moved end.
            Buffers expected(0, kLength, shape.outputs);
            int64_t partial = 0;     // fading samples with a factor strictly between 0 and 1
            int64_t nonPositive = 0; // and with a factor <= 0, which a fade never has
            for (uint32_t i = 0; i < pool.size(); ++i)
            {
                const gg::GrainSpec &g = specs[i];
                const bool stolen = rows.fadeStart[i] != gg::kNeverStolen;
                const int64_t end =
                    stolen ? rows.fadeStart[i] +
                                 std::min(gg::kStealFade, gg::grainEndSample(g) - rows.fadeStart[i])
                           : gg::grainEndSample(g);
                const double invDuration = 1.0 / g.durationSamples;
                const gg::EnvelopeShape env = gg::envelopeShape(g.texture, g.durationSamples);
                const ChannelGains gains = channelGains(g, shape.outputs);
                const float stretch = specStretch(g.rate);
                for (int64_t t = g.onsetSample; t < end; ++t)
                {
                    const double e = static_cast<double>(t - g.onsetSample) - g.onsetFrac;
                    float w = tables.hann.at(gg::warpPhase(e * invDuration, env));
                    if (!(w > 0.0f))
                    {
                        continue;
                    }
                    if (stolen && t >= rows.fadeStart[i])
                    {
                        const float factor = specFade(rows.fadeStart[i], end, t);
                        nonPositive += !(factor > 0.0f);
                        partial += factor > 0.0f && factor < 1.0f;
                        w = w * factor;
                    }
                    const double pos = g.startPos + g.rate * e;
                    for (int c = 0; c < shape.outputs; ++c)
                    {
                        expected.ch[c][static_cast<std::size_t>(t)] +=
                            specRead<TestType>(channels[c], kNoiseFrames, pos, stretch) *
                            (w * gains.c[c]);
                    }
                }
            }
            REQUIRE(partial > 5000);
            REQUIRE(nonPositive == 0);
            Buffers ref(0, kLength, shape.outputs);
            renderAll<TestType>(Kernel::Ref, pool, source, ref, 0, kLength, tables);
            REQUIRE(anyNonZero(expected.ch[0]));
            REQUIRE(sameBits(ref.ch[0], expected.ch[0]));
            REQUIRE(sameBits(ref.ch[1], expected.ch[1]));
        }

        for (const uint32_t d : decimationsOf<TestType>())
        {
            INFO("decimation " << d);
            const Pool scaled(atDecimation(specs, d), steals);
            for (const int outputs : {2, 1})
            {
                INFO(outputs << " outputs");
                for (const SampleSource &source : {src.stereo(d), src.stereoPlanar(d), src.mono(d)})
                {
                    auto [ref, simd] =
                        renderBoth<TestType>(scaled, source, outputs, 0, kLength, tables);
                    REQUIRE(anyNonZero(ref.ch[0]));
                    REQUIRE(sameBits(simd.ch[0], ref.ch[0]));
                    REQUIRE(sameBits(simd.ch[1], ref.ch[1]));
                }
            }
            requireExactRuns<TestType>(scaled, kNoiseFrames, d, 0, kLength);
            for (uint64_t k = 0; k < 200; ++k)
            {
                const int64_t from = static_cast<int64_t>(u01(97, 0, k) * 12000.0);
                const int64_t to = from + static_cast<int64_t>(u01(97, 1, k) * 512.0);
                INFO("call " << k << ", [" << from << ", " << to << ")");
                auto [ref, simd] = renderBoth<TestType>(scaled, src.stereo(d), 2, from, to, tables);
                REQUIRE(sameBits(simd.ch[0], ref.ch[0]));
                REQUIRE(sameBits(simd.ch[1], ref.ch[1]));
                requireExactRuns<TestType>(scaled, kNoiseFrames, d, from, to);
            }
        }
    }

    SECTION("live ring")
    {
        const LiveInput input(100000, 101, freezeToggles(103, 8, 8000.0));
        const std::vector<int64_t> heads = headTrajectory(input);
        const std::vector<gg::GrainSpec> specs =
            liveGrains(105, spreadOnsets(input.length - 10000, 300), heads, 24000);
        const std::vector<Steal> steals = stealsFor(specs, 107);
        const Pool pool(specs, steals);
        const gg::GrainRows &rows = pool.rows();
        requireStolenRows(pool, specs, steals);
        requireBothStretches(rows, pool.size());

        const std::vector<int64_t> ragged = {61, 64, 1, 128, 3, 250, 2, 37, 511};
        for (const std::vector<int64_t> &lengths : {ragged, std::vector<int64_t>{511}})
        {
            for (const int outputs : {2, 1})
            {
                INFO(outputs << " outputs, first block " << lengths[0]);
                Buffers expected(0, input.length, outputs);
                Buffers ref(0, input.length, outputs);
                Buffers simd(0, input.length, outputs);
                const gg::kernels::Output refOut = ref.output();
                const gg::kernels::Output simdOut = simd.output();
                int64_t fadingInside = 0;   // fading samples where the interior's conditions hold
                int64_t interiorFading = 0; // interior samples at or after a fade's start
                int64_t simdSamples = 0;
                ActiveGrains active(pool);
                gg::LiveSource ring;
                prepareRing(ring);
                driveRing(ring, input, lengths, [&](int64_t from, int64_t to) {
                    active.each(from, to, [&](uint32_t i) {
                        gg::kernels::renderLiveRef<TestType>(rows, i, ring, 1, refOut, from, to,
                                                             tables);
                        gg::kernels::renderLiveSimd<TestType>(rows, i, ring, 1, simdOut, from, to,
                                                              tables);
                        const int64_t first = std::max(from, rows.onsetSample[i]);
                        const int64_t last = std::min(to, rows.endSample[i]);
                        if (first >= last)
                        {
                            return;
                        }
                        const gg::kernels::LiveInterior in =
                            gg::kernels::liveInterior<TestType>(rows, i, ring, 1, from, to);
                        for (int p = 0; p < 2; ++p)
                        {
                            simdSamples += in.end[p] - in.begin[p];
                            interiorFading += std::max<int64_t>(
                                0, in.end[p] - std::max(in.begin[p], rows.fadeStart[i]));
                        }
                        // The spec's loop: e, w (skip unless > 0), w × the steal fade from the
                        // steal on, pos, f = float(fade(pos, R(t))) (skip unless > 0 and the
                        // read succeeds), dst[c][t] += read_c × ((w × f) × gain_c).
                        const gg::GrainSpec &g = specs[i];
                        const bool stolen = rows.fadeStart[i] != gg::kNeverStolen;
                        const double invDuration = 1.0 / g.durationSamples;
                        const gg::EnvelopeShape env =
                            gg::envelopeShape(g.texture, g.durationSamples);
                        const ChannelGains gains = channelGains(g, outputs);
                        const gg::kernels::ReadParams params = readParams(stretchOfGrain(rows, i));
                        gg::LiveSource::Cursor cursor;
                        for (int64_t t = first; t < last; ++t)
                        {
                            const double e = static_cast<double>(t - g.onsetSample) - g.onsetFrac;
                            const double phase = e * invDuration;
                            float w = tables.hann.at(gg::warpPhase(phase, env));
                            if (!(w > 0.0f))
                            {
                                continue;
                            }
                            const bool fading = stolen && t >= rows.fadeStart[i];
                            if (fading)
                            {
                                w = w * specFade(rows.fadeStart[i], rows.endSample[i], t);
                            }
                            const double pos = g.startPos + g.rate * e;
                            const int64_t head = ring.headAt(t);
                            const auto f = static_cast<float>(TestType::fade(ring, pos, head));
                            float read[2] = {0.0f, 0.0f};
                            if (!(f > 0.0f) || !TestType::readLive(ring, pos, head, cursor, params,
                                                                   read[0], read[1]))
                            {
                                continue;
                            }
                            fadingInside += fading && phase >= 0.0 && phase <= 1.0 &&
                                            std::floor(pos) - Reach<TestType>::before >= 0.0 &&
                                            TestType::fade(ring, pos, head) == 1.0;
                            for (int c = 0; c < outputs; ++c)
                            {
                                expected.ch[c][static_cast<std::size_t>(t)] +=
                                    read[c] * ((w * f) * gains.c[c]);
                            }
                        }
                    });
                });
                REQUIRE(fadingInside > 5000);
                REQUIRE(interiorFading == 0);
                REQUIRE(simdSamples > 100000);
                REQUIRE(anyNonZero(expected.ch[0]));
                REQUIRE(sameBits(ref.ch[0], expected.ch[0]));
                REQUIRE(sameBits(ref.ch[1], expected.ch[1]));
                REQUIRE(sameBits(simd.ch[0], ref.ch[0]));
                REQUIRE(sameBits(simd.ch[1], ref.ch[1]));
            }
        }
    }
}
