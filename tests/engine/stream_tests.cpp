// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/MathDet.h"
#include "gg/grain/PeriodicStream.h"

namespace
{
constexpr int64_t kSourceFrames = 96000;
constexpr double kSourceRate = 48000.0;
constexpr uint64_t kSeed = 3;

// Schedules [start, start + frames) in blocks of `block` and returns every emitted grain.
std::vector<gg::GrainSpec> collect(gg::PeriodicStream &stream, int64_t start, int64_t frames,
                                   uint32_t block, double sourceRate = kSourceRate)
{
    std::vector<gg::GrainSpec> grains;
    for (int64_t t = start; t < start + frames; t += block)
    {
        const auto n = static_cast<uint32_t>(std::min<int64_t>(block, start + frames - t));
        stream.schedule(t, n, kSourceFrames, sourceRate, kSeed,
                        [&](const gg::GrainSpec &g) { grains.push_back(g); });
    }
    return grains;
}

gg::StreamParams withRate(double rateHz)
{
    gg::StreamParams p;
    p.grainRateHz = rateHz;
    return p;
}
} // namespace

TEST_CASE("onsets land on the lattice", "[stream]")
{
    gg::PeriodicStream stream;
    stream.prepare(48000.0);

    SECTION("100 Hz at 48 kHz: every 480 samples, no fraction")
    {
        stream.setParams(withRate(100.0));
        const auto grains = collect(stream, 0, 48000, 48000);
        REQUIRE(grains.size() == 100);
        bool onLattice = true;
        for (uint64_t k = 0; k < grains.size(); ++k)
        {
            onLattice = onLattice && grains[k].index == k &&
                        grains[k].onsetSample == static_cast<int64_t>(k * 480) &&
                        grains[k].onsetFrac == 0.0;
        }
        REQUIRE(onLattice);
    }

    SECTION("7 Hz at 48 kHz: fractional onsets, independent of block size")
    {
        stream.setParams(withRate(7.0));
        const auto grains = collect(stream, 0, 480000, 480000);
        REQUIRE(grains.size() == 70);
        bool onLattice = true;
        for (uint64_t k = 0; k < grains.size(); ++k)
        {
            const double t = k * 48000 / 7.0;
            onLattice = onLattice && grains[k].index == k &&
                        grains[k].onsetSample == static_cast<int64_t>(std::floor(t)) &&
                        grains[k].onsetFrac == t - std::floor(t);
        }
        REQUIRE(onLattice);

        for (const uint32_t block : {1u, 37u, 4096u})
        {
            stream.reset();
            INFO("block = " << block);
            REQUIRE(collect(stream, 0, 480000, block) == grains);
        }
    }
}

TEST_CASE("stream timing scales with sample rate", "[stream]")
{
    constexpr double kPitch = 7.0;
    for (const double sr : {44100.0, 96000.0, 192000.0})
    {
        INFO("sampleRate = " << sr);
        gg::PeriodicStream stream;
        stream.prepare(sr);
        gg::StreamParams p;
        p.grainRateHz = 20.0;
        p.grainDurationSec = 0.1;
        p.pitchSemitones = kPitch;
        stream.setParams(p);

        const auto grains = collect(stream, 0, static_cast<int64_t>(2.0 * sr), 512);
        REQUIRE(grains.size() == 40);
        bool timed = true;
        for (uint64_t k = 0; k < grains.size(); ++k)
        {
            const double onsetSeconds = static_cast<double>(grains[k].onsetSample) / sr;
            timed = timed && std::abs(onsetSeconds - static_cast<double>(k) / 20.0) <= 1.0 / sr &&
                    grains[k].rate == gg::exp2Det(kPitch / 12.0) * kSourceRate / sr &&
                    grains[k].durationSamples == 0.1 * sr;
        }
        REQUIRE(timed);
    }
}

TEST_CASE("stream params are sanitized", "[stream]")
{
    gg::PeriodicStream stream;
    stream.prepare(48000.0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    SECTION("non-finite values become the defaults")
    {
        stream.setParams({nan, inf, -inf, nan, inf, std::numeric_limits<float>::quiet_NaN()});
        const auto grains = collect(stream, 0, 48000, 1000);
        REQUIRE(grains.size() == 20); // default 20 Hz
        REQUIRE(grains[1].onsetSample == 2400);
        REQUIRE(grains[1].durationSamples == 4800.0);             // default 0.1 s
        REQUIRE(grains[1].startPos == 0.5 * (kSourceFrames - 1)); // default 0.5, no jitter
        REQUIRE(grains[1].rate == 1.0);                           // default 0 semitones
        REQUIRE(grains[1].gain == 0.5f);
    }

    SECTION("out-of-range values are clamped")
    {
        stream.setParams({1e9, 0.0, 5.0, -3.0, 100.0, 1e9f});
        const auto grains = collect(stream, 0, 120, 120);
        REQUIRE(grains.size() == 10); // 4000 Hz: every 12 samples
        REQUIRE(grains[1].onsetSample == 12);
        REQUIRE(grains[1].durationSamples == 16.0);
        REQUIRE(grains[1].startPos == static_cast<double>(kSourceFrames - 1));
        REQUIRE(grains[1].rate == 16.0); // +48 semitones
        REQUIRE(grains[1].gain == 4.0f);

        stream.setParams({20.0, 0.1, -1.0, 0.0, -100.0, -1.0f});
        const auto low = collect(stream, 120, 4800, 4800);
        REQUIRE_FALSE(low.empty());
        REQUIRE(low[0].startPos == 0.0);
        REQUIRE(low[0].rate == 1.0 / 16.0); // -48 semitones
        REQUIRE(low[0].gain == 0.0f);
    }

    SECTION("a rate of zero or below turns the stream off")
    {
        for (const double rate : {0.0, -1.0, -1e9})
        {
            stream.setParams(withRate(rate));
            REQUIRE(collect(stream, 0, 48000, 512).empty());
        }
    }
}

TEST_CASE("rate changes restart the lattice from the last onset", "[stream]")
{
    gg::PeriodicStream stream;
    stream.prepare(48000.0);
    stream.setParams(withRate(100.0));
    auto grains = collect(stream, 0, 24000, 480); // k = 0..49, last onset at 23520
    REQUIRE(grains.size() == 50);
    REQUIRE(grains.back().onsetSample == 23520);

    // Slower: the next grain comes one new period after the last one, not at k * newPeriod.
    stream.setParams(withRate(1.0));
    const auto slow = collect(stream, 24000, 126000, 480);
    REQUIRE(slow.size() == 2);
    REQUIRE(slow[0].index == 50);
    REQUIRE(slow[0].onsetSample == 23520 + 48000);
    REQUIRE(slow[1].index == 51);
    REQUIRE(slow[1].onsetSample == 23520 + 2 * 48000);

    // Faster: one new period after the last onset is already past, so the next grain starts
    // at the first block after the change.
    stream.setParams(withRate(1000.0));
    const auto fast = collect(stream, 150000, 480, 480);
    REQUIRE(fast.size() == 10);
    bool onLattice = true;
    for (uint64_t i = 0; i < fast.size(); ++i)
    {
        onLattice = onLattice && fast[i].index == 52 + i &&
                    fast[i].onsetSample == 150000 + static_cast<int64_t>(48 * i);
    }
    REQUIRE(onLattice);

    // Setting the same rate again changes nothing.
    stream.setParams(withRate(1000.0));
    const auto same = collect(stream, 150480, 48, 48);
    REQUIRE(same.size() == 1);
    REQUIRE(same[0].onsetSample == 150480);
}
