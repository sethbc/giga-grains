// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The Cloud scheduler (spec §6.2): onsets from t(k+1) = t(k) + Δ(k), with
// Δ(k) = P × ((1 − d) + d × E(k)), P = sampleRate / densityHz and E(k) = −logDet(1 − u(k)).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/MathDet.h"
#include "gg/core/Rng.h"
#include "gg/grain/CloudScheduler.h"
#include "gg/grain/Tier.h"

namespace
{
constexpr uint64_t kSeed = 3;

// Schedules [start, start + frames) in blocks of `block` and returns every emitted grain.
std::vector<gg::GrainDraw> collect(gg::CloudScheduler &cloud, int64_t start, int64_t frames,
                                   uint32_t block, uint32_t layer = 0, uint32_t stream = 0)
{
    std::vector<gg::GrainDraw> grains;
    for (int64_t t = start; t < start + frames; t += block)
    {
        const auto n = static_cast<uint32_t>(std::min<int64_t>(block, start + frames - t));
        cloud.schedule(t, n, kSeed, layer, stream,
                       [&](const gg::GrainDraw &g) { grains.push_back(g); });
    }
    return grains;
}

gg::CloudParams cloudAt(double densityHz, double distribution = 0.0)
{
    gg::CloudParams p;
    p.densityHz = densityHz;
    p.distribution = distribution;
    return p;
}

// A grain's onset time in samples. Exact: onsetFrac = t − floor(t) is exact, and so is the sum.
double onsetTime(const gg::GrainDraw &g)
{
    return static_cast<double>(g.onsetSample) + g.onsetFrac;
}

// E(k), as spec §6.2 defines it.
double unitExponential(uint64_t k, uint32_t layer = 0, uint32_t stream = 0)
{
    const uint64_t h = gg::rng::hash({kSeed, layer, stream, gg::rng::Field::Onset, k});
    return -gg::logDet(1.0 - gg::rng::uniform01(h));
}

// Δ(k) exactly as the scheduler computes it, floor included.
double interval(double period, double d, uint64_t k)
{
    return std::max(period * ((1.0 - d) + d * unitExponential(k)),
                    gg::CloudScheduler::kMinIntervalSamples);
}

struct IntervalStats
{
    double mean;
    double cv; // coefficient of variation: standard deviation / mean
};

// The statistics of the first n intervals of grains (which must hold at least n + 1 grains).
IntervalStats intervalStats(const std::vector<gg::GrainDraw> &grains, std::size_t n)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        sum += onsetTime(grains[i + 1]) - onsetTime(grains[i]);
    }
    const double mean = sum / static_cast<double>(n);
    double squares = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double dev = onsetTime(grains[i + 1]) - onsetTime(grains[i]) - mean;
        squares += dev * dev;
    }
    return {mean, std::sqrt(squares / static_cast<double>(n)) / mean};
}

// True when every grain's onset follows the recurrence from t(0) = 0, interval by interval,
// and the indices count up from 0.
bool followsRecurrence(const std::vector<gg::GrainDraw> &grains, double period, double d)
{
    double t = 0.0;
    for (uint64_t k = 0; k < grains.size(); ++k)
    {
        const double whole = std::floor(t);
        if (grains[k].index != k || grains[k].onsetSample != static_cast<int64_t>(whole) ||
            grains[k].onsetFrac != t - whole)
        {
            return false;
        }
        t += interval(period, d, k);
    }
    return true;
}
} // namespace

TEST_CASE("periodic at distribution 0", "[cloud]")
{
    gg::CloudScheduler cloud;
    cloud.prepare(48000.0);

    SECTION("20 Hz at 48 kHz: every interval is exactly sampleRate / densityHz")
    {
        cloud.setParams(cloudAt(20.0));
        const auto grains = collect(cloud, 0, 1000 * 2400, 4096);
        REQUIRE(grains.size() == 1000);
        REQUIRE(onsetTime(grains[0]) == 0.0);
        bool periodic = true;
        for (std::size_t k = 0; k + 1 < grains.size(); ++k)
        {
            periodic = periodic && grains[k].index == k &&
                       onsetTime(grains[k + 1]) - onsetTime(grains[k]) == 48000.0 / 20.0;
        }
        REQUIRE(periodic);
    }

    SECTION("7 Hz at 48 kHz: fractional onsets follow the recurrence, whatever the blocks")
    {
        // The recurrence adds the rounded period 70 times, so t(70) lands just below 480000
        // where M0's k × sr / rate hit it exactly: the window stops well clear of any onset.
        cloud.setParams(cloudAt(7.0));
        const auto grains = collect(cloud, 0, 470000, 470000);
        REQUIRE(grains.size() == 69);
        REQUIRE(followsRecurrence(grains, 48000.0 / 7.0, 0.0));
        REQUIRE(grains[1].onsetFrac > 0.0); // not a whole number of samples

        for (const uint32_t block : {1u, 37u, 4096u})
        {
            cloud.reset();
            INFO("block = " << block);
            REQUIRE(collect(cloud, 0, 470000, block) == grains);
        }
    }
}

TEST_CASE("poisson at distribution 1", "[cloud]")
{
    constexpr double kPeriod = 48000.0 / 20.0;
    constexpr std::size_t kIntervals = 100000;
    gg::CloudScheduler cloud;
    cloud.prepare(48000.0);
    cloud.setParams(cloudAt(20.0, 1.0));
    // One call: the onsets don't depend on the blocks (checked below on a shorter run).
    const auto grains = collect(cloud, 0, static_cast<int64_t>(1.05 * kIntervals * kPeriod),
                                std::numeric_limits<uint32_t>::max());
    REQUIRE(grains.size() > kIntervals);
    REQUIRE(followsRecurrence(grains, kPeriod, 1.0));

    const auto stats = intervalStats(grains, kIntervals);
    INFO("mean = " << stats.mean << ", cv = " << stats.cv);
    REQUIRE(std::abs(stats.mean - kPeriod) <= 0.01 * kPeriod);
    REQUIRE(std::abs(stats.cv - 1.0) <= 0.05);

    for (const uint32_t block : {1u, 64u, 511u})
    {
        cloud.reset();
        INFO("block = " << block);
        const auto split = collect(cloud, 0, 480000, block);
        REQUIRE(split.size() > 100);
        REQUIRE(std::equal(split.begin(), split.end(), grains.begin()));
    }
}

TEST_CASE("distribution 0.5 keeps the mean", "[cloud]")
{
    constexpr double kPeriod = 48000.0 / 20.0;
    constexpr std::size_t kIntervals = 100000;
    gg::CloudScheduler cloud;
    cloud.prepare(48000.0);
    cloud.setParams(cloudAt(20.0, 0.5));
    const auto grains = collect(cloud, 0, static_cast<int64_t>(1.05 * kIntervals * kPeriod),
                                std::numeric_limits<uint32_t>::max());
    REQUIRE(grains.size() > kIntervals);
    REQUIRE(followsRecurrence(grains, kPeriod, 0.5));

    // Δ = P × (0.5 + 0.5 × E): mean P, standard deviation 0.5 × P.
    const auto stats = intervalStats(grains, kIntervals);
    INFO("mean = " << stats.mean << ", cv = " << stats.cv);
    REQUIRE(std::abs(stats.mean - kPeriod) <= 0.01 * kPeriod);
    REQUIRE(std::abs(stats.cv - 0.5) <= 0.025);
    // Never closer than the periodic half (up to the rounding of t + Δ).
    bool spaced = true;
    for (std::size_t k = 0; k + 1 < grains.size(); ++k)
    {
        spaced = spaced && onsetTime(grains[k + 1]) - onsetTime(grains[k]) >= 0.5 * kPeriod - 1e-6;
    }
    REQUIRE(spaced);
}

TEST_CASE("a density change rescales the pending onset", "[cloud]")
{
    // Ruling M1-h: a density or distribution change at g (the next schedule() call's
    // blockStart) moves a pending onset t(k) > g to max(t(k−1) + Δ_new(k−1), g), with the same
    // draw E(k−1).
    gg::CloudScheduler cloud;
    cloud.prepare(48000.0);

    SECTION("periodic, 10 Hz to 100 Hz: one new period after the last onset")
    {
        cloud.setParams(cloudAt(10.0));
        const auto before = collect(cloud, 0, 10000, 10000); // 0, 4800, 9600; 14400 pending
        REQUIRE(before.size() == 3);
        cloud.setParams(cloudAt(100.0));
        const auto after = collect(cloud, 10000, 1000, 1000);
        REQUIRE(after.size() == 2);
        REQUIRE(after[0].index == 3);
        REQUIRE(onsetTime(after[0]) == 9600.0 + 480.0); // max(10080, g = 10000)
        REQUIRE(onsetTime(after[1]) == 9600.0 + 2 * 480.0);
    }

    SECTION("periodic, 10 Hz to 100 Hz: never before the change")
    {
        cloud.setParams(cloudAt(10.0));
        REQUIRE(collect(cloud, 0, 10200, 10200).size() == 3); // the last onset is at 9600
        cloud.setParams(cloudAt(100.0));
        const auto after = collect(cloud, 10200, 1000, 1000);
        REQUIRE(after.size() == 3);
        REQUIRE(after[0].index == 3);
        REQUIRE(onsetTime(after[0]) == 10200.0); // max(10080, g = 10200)
        REQUIRE(onsetTime(after[1]) == 10680.0);
    }

    SECTION("periodic, 100 Hz to 10 Hz: one new period after the last onset")
    {
        cloud.setParams(cloudAt(100.0));
        REQUIRE(collect(cloud, 0, 1000, 1000).size() == 3); // 0, 480, 960; 1440 pending
        cloud.setParams(cloudAt(10.0));
        const auto after = collect(cloud, 1000, 10000, 1000);
        REQUIRE(after.size() == 2);
        REQUIRE(after[0].index == 3);
        REQUIRE(onsetTime(after[0]) == 960.0 + 4800.0);
        REQUIRE(onsetTime(after[1]) == 960.0 + 2 * 4800.0);
    }

    SECTION("poisson: the same draw, rescaled to the new density")
    {
        struct Change
        {
            double fromHz;
            double toHz;
        };
        for (const Change c : {Change{10.0, 100.0}, Change{100.0, 10.0}})
        {
            INFO(c.fromHz << " Hz to " << c.toHz << " Hz");
            constexpr int64_t kChange = 20000;
            cloud.reset();
            cloud.setParams(cloudAt(c.fromHz, 1.0));
            const auto before = collect(cloud, 0, kChange, 4096);
            REQUIRE_FALSE(before.empty());
            const gg::GrainDraw last = before.back();
            const double oldPending =
                onsetTime(last) + interval(48000.0 / c.fromHz, 1.0, last.index);
            REQUIRE(oldPending > static_cast<double>(kChange));

            cloud.setParams(cloudAt(c.toHz, 1.0));
            const auto after = collect(cloud, kChange, 500000, 4096);
            REQUIRE(after.size() >= 2);
            REQUIRE(after[0].index == last.index + 1);
            const double rescaled = onsetTime(last) + interval(48000.0 / c.toHz, 1.0, last.index);
            REQUIRE(onsetTime(after[0]) == std::max(rescaled, static_cast<double>(kChange)));
            REQUIRE(onsetTime(after[0]) != oldPending);
            // Then the recurrence at the new density, from the rescaled onset.
            REQUIRE(onsetTime(after[1]) ==
                    onsetTime(after[0]) + interval(48000.0 / c.toHz, 1.0, after[0].index));
        }
    }

    SECTION("a distribution change rescales it too")
    {
        struct Change
        {
            double from;
            double to;
        };
        for (const Change c : {Change{0.0, 1.0}, Change{1.0, 0.0}})
        {
            INFO("distribution " << c.from << " to " << c.to);
            constexpr int64_t kChange = 20000;
            cloud.reset();
            cloud.setParams(cloudAt(20.0, c.from));
            const auto before = collect(cloud, 0, kChange, 4096);
            REQUIRE_FALSE(before.empty());
            const gg::GrainDraw last = before.back();

            cloud.setParams(cloudAt(20.0, c.to));
            const auto after = collect(cloud, kChange, 200000, 4096);
            REQUIRE_FALSE(after.empty());
            REQUIRE(after[0].index == last.index + 1);
            const double rescaled = onsetTime(last) + interval(2400.0, c.to, last.index);
            REQUIRE(onsetTime(after[0]) == std::max(rescaled, static_cast<double>(kChange)));
        }
    }

    SECTION("other parameters, and a change undone before the next block, keep it")
    {
        // The reference: 100 Hz throughout, so grain 3 is at 1440.
        cloud.setParams(cloudAt(100.0));
        REQUIRE(collect(cloud, 0, 1000, 1000).size() == 3);

        gg::CloudParams other = cloudAt(100.0);
        other.pitchSemitones = 12.0;
        other.position = 0.1;
        other.spray = 0.5;
        other.stereo = 1.0;
        other.gain = 2.0f;
        other.grainDurationSec = 0.01;
        cloud.setParams(other);
        auto after = collect(cloud, 1000, 500, 500);
        REQUIRE(after.size() == 1);
        REQUIRE(onsetTime(after[0]) == 1440.0);

        // 10 Hz and back to 100 Hz before the next block: the last values set win.
        cloud.setParams(cloudAt(10.0));
        cloud.setParams(cloudAt(100.0));
        after = collect(cloud, 1500, 500, 500);
        REQUIRE(after.size() == 1);
        REQUIRE(onsetTime(after[0]) == 1920.0);
    }

    SECTION("with no grain since the stream was turned on, the pending onset stays")
    {
        cloud.setParams(cloudAt(100.0));
        REQUIRE(collect(cloud, 0, 1000, 1000).size() == 3); // 1440 pending
        cloud.setParams(cloudAt(0.0));
        REQUIRE(collect(cloud, 1000, 100, 100).empty());
        // Back on at another density, before the pending onset: no previous grain since then,
        // so it isn't rescaled to 960 + 4800 (ruling M1-j keeps it).
        cloud.setParams(cloudAt(10.0));
        const auto after = collect(cloud, 1100, 9000, 1000);
        REQUIRE(after.size() == 2);
        REQUIRE(onsetTime(after[0]) == 1440.0);
        REQUIRE(onsetTime(after[1]) == 1440.0 + 4800.0);
    }

    SECTION("setting the same parameters again changes nothing")
    {
        cloud.reset();
        cloud.setParams(cloudAt(100.0));
        const auto once = collect(cloud, 0, 4800, 4800);
        cloud.reset();
        std::vector<gg::GrainDraw> twice;
        for (int64_t t = 0; t < 4800; t += 100)
        {
            cloud.setParams(cloudAt(100.0));
            cloud.schedule(t, 100, kSeed, 0, 0,
                           [&](const gg::GrainDraw &g) { twice.push_back(g); });
        }
        REQUIRE(once.size() == 10);
        REQUIRE(twice == once);
    }
}

TEST_CASE("turning the stream back on starts it at the first block after the change", "[cloud]")
{
    gg::CloudScheduler cloud;
    cloud.prepare(48000.0);
    cloud.setParams(cloudAt(100.0));
    const auto first = collect(cloud, 0, 1000, 1000); // onsets 0, 480, 960; next due at 1440
    REQUIRE(first.size() == 3);

    // Ruling M1-a: a density of 0 or below is off. The onset due at 1440 doesn't happen.
    cloud.setParams(cloudAt(0.0));
    REQUIRE(collect(cloud, 1000, 4000, 100).empty());

    cloud.setParams(cloudAt(100.0));
    const auto resumed = collect(cloud, 5000, 1000, 1000);
    REQUIRE(resumed.size() == 3);
    REQUIRE(resumed[0].index == 3);
    REQUIRE(onsetTime(resumed[0]) == 5000.0);
    REQUIRE(onsetTime(resumed[1]) == 5480.0);

    // Off for less than the pending interval: the pending onset stays where it was.
    cloud.setParams(cloudAt(0.0));
    REQUIRE(collect(cloud, 6000, 100, 100).empty()); // the next onset is due at 6440
    cloud.setParams(cloudAt(100.0));
    const auto kept = collect(cloud, 6100, 400, 400);
    REQUIRE(kept.size() == 1);
    REQUIRE(onsetTime(kept[0]) == 6440.0);
}

TEST_CASE("timing scales with sample rate", "[cloud]")
{
    constexpr double kPitch = 7.0;
    for (const double sr : {44100.0, 96000.0, 192000.0})
    {
        INFO("sampleRate = " << sr);
        gg::CloudScheduler cloud;
        cloud.prepare(sr);
        gg::CloudParams p;
        p.densityHz = 20.0;
        p.grainDurationSec = 0.1;
        p.pitchSemitones = kPitch;
        cloud.setParams(p);

        const auto grains = collect(cloud, 0, static_cast<int64_t>(2.0 * sr), 512);
        REQUIRE(grains.size() == 40);
        bool timed = true;
        for (uint64_t k = 0; k < grains.size(); ++k)
        {
            const double onsetSeconds = static_cast<double>(grains[k].onsetSample) / sr;
            timed = timed && std::abs(onsetSeconds - static_cast<double>(k) / 20.0) <= 1.0 / sr &&
                    grains[k].rate == gg::exp2Det(kPitch / 12.0) &&
                    grains[k].durationSamples == 0.1 * sr;
        }
        REQUIRE(timed);
    }
}

TEST_CASE("params are sanitized", "[cloud]")
{
    gg::CloudScheduler cloud;
    cloud.prepare(48000.0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    SECTION("non-finite values become the defaults")
    {
        cloud.setParams(
            {nan, inf, -inf, nan, inf, -inf, nan, std::numeric_limits<float>::infinity()});
        const auto grains = collect(cloud, 0, 48000, 1000);
        REQUIRE(grains.size() == 16); // default 16 Hz, periodic
        REQUIRE(onsetTime(grains[1]) == 3000.0);
        REQUIRE(grains[1].durationSamples == 4800.0); // default 0.1 s
        REQUIRE(grains[1].positionDraw == 0.5);       // default 0.5, no spray
        REQUIRE(grains[1].rate == 1.0);               // default 0 semitones
        REQUIRE(grains[1].gain == 0.5f);
        REQUIRE(grains[1].pan == 0.0f); // default stereo 0
    }

    SECTION("out-of-range values are clamped")
    {
        cloud.setParams({1e9, -3.0, 0.0, 5.0, -3.0, 100.0, 7.0, 1e9f});
        const auto grains = collect(cloud, 0, 120, 120);
        REQUIRE(grains.size() == 120); // the sample rate, distribution 0: every sample
        REQUIRE(onsetTime(grains[1]) == 1.0);
        REQUIRE(grains[1].durationSamples == 16.0);
        REQUIRE(grains[1].positionDraw == 1.0);
        REQUIRE(grains[1].rate == 16.0); // +48 semitones
        REQUIRE(grains[1].gain == 4.0f);
        // Stereo 7 is 1: the pan is the bipolar draw itself.
        const double u = gg::rng::bipolar(gg::rng::hash({kSeed, 0, 0, gg::rng::Field::Pan, 1}));
        REQUIRE(grains[1].pan == static_cast<float>(u));

        cloud.reset();
        cloud.setParams({20.0, 9.0, 0.1, -1.0, 0.0, -100.0, -1.0, -1.0f});
        const auto low = collect(cloud, 0, 48000, 4800);
        REQUIRE(followsRecurrence(low, 2400.0, 1.0)); // distribution 9 is 1
        REQUIRE(low[0].positionDraw == 0.0);
        REQUIRE(low[0].rate == 1.0 / 16.0); // -48 semitones
        REQUIRE(low[0].gain == 0.0f);
        REQUIRE(low[0].pan == 0.0f);
    }

    SECTION("a positive density below 0.01 Hz is 0.01 Hz")
    {
        for (const double density : {1e-9, std::numeric_limits<double>::denorm_min(), 0.01})
        {
            INFO("density = " << density);
            cloud.reset();
            cloud.setParams(cloudAt(density));
            const auto grains = collect(cloud, 0, 4800001, 1u << 20);
            REQUIRE(grains.size() == 2);
            REQUIRE(onsetTime(grains[1]) == 4800000.0); // 100 s at 48 kHz
        }
    }

    SECTION("the density reaches 20 kHz at 48 kHz")
    {
        // The §16 case: 2,000 overlapping grains of 100 ms (INTENT §23 #45).
        cloud.setParams(cloudAt(20000.0));
        const auto grains = collect(cloud, 0, 4800, 512);
        REQUIRE(grains.size() == 2000);
        REQUIRE(onsetTime(grains[1]) == 2.4);
    }

    SECTION("the density is capped at one grain per output sample")
    {
        cloud.prepare(1000.0);
        cloud.setParams(cloudAt(4000.0));
        const auto grains = collect(cloud, 0, 100, 7);
        REQUIRE(grains.size() == 100);
        REQUIRE(onsetTime(grains[99]) == 99.0);
    }

    SECTION("a density of 0 or below turns the stream off (ruling M1-a)")
    {
        for (const double density : {0.0, -0.0, -1.0, -1e9})
        {
            INFO("density = " << density);
            cloud.reset();
            cloud.setParams(cloudAt(density));
            REQUIRE(collect(cloud, 0, 48000, 512).empty());
        }
    }
}

TEST_CASE("each draw has its own key, with the real layer and stream", "[cloud]")
{
    gg::CloudParams p;
    p.densityHz = 200.0;
    p.distribution = 0.5;
    p.position = 0.4;
    p.spray = 0.3;
    p.stereo = 0.8;

    auto drawsFor = [&](uint32_t layer, uint32_t stream) {
        gg::CloudScheduler cloud;
        cloud.prepare(48000.0);
        cloud.setParams(p);
        return collect(cloud, 0, 48000, 256, layer, stream);
    };

    struct Ids
    {
        uint32_t layer;
        uint32_t stream;
    };
    for (const Ids ids : {Ids{0, 0}, Ids{2, 5}})
    {
        INFO("layer = " << ids.layer << ", stream = " << ids.stream);
        const auto grains = drawsFor(ids.layer, ids.stream);
        REQUIRE(grains.size() > 100);
        bool keyed = true;
        bool panned = false;
        for (const auto &g : grains)
        {
            using gg::rng::Field;
            const auto key = [&](Field f) {
                return gg::rng::hash({kSeed, ids.layer, ids.stream, f, g.index});
            };
            const double position =
                std::clamp(0.4 + 0.3 * gg::rng::bipolar(key(Field::PositionJitter)), 0.0, 1.0);
            const auto pan = static_cast<float>(0.8 * gg::rng::bipolar(key(Field::Pan)));
            keyed = keyed && g.positionDraw == position && g.pan == pan;
            panned = panned || std::abs(g.pan) > 0.5f;
        }
        REQUIRE(keyed);
        REQUIRE(panned);
    }

    // Another layer or stream draws other onsets, positions and pans.
    const auto a = drawsFor(0, 0);
    const auto b = drawsFor(0, 1);
    const auto c = drawsFor(1, 0);
    REQUIRE(a[1].onsetSample != b[1].onsetSample);
    REQUIRE(a[1].onsetSample != c[1].onsetSample);
    REQUIRE(a[0].positionDraw != b[0].positionDraw);
    REQUIRE(a[0].pan != c[0].pan);
}

TEST_CASE("onsets are at least 1/16 sample apart, which bounds a schedule call", "[cloud]")
{
    // At distribution 1, Δ = P × E(k) can be arbitrarily small (E = 0 when u = 0). With P = 1,
    // about 6% of the drawn intervals are below the floor.
    gg::CloudScheduler cloud;
    cloud.prepare(4000.0);
    cloud.setParams(cloudAt(4000.0, 1.0));
    constexpr int64_t kFrames = 200000;
    std::vector<gg::GrainDraw> grains;
    uint32_t mostInOneFrame = 0;
    for (int64_t t = 0; t < kFrames; ++t)
    {
        uint32_t inFrame = 0;
        cloud.schedule(t, 1, kSeed, 0, 0, [&](const gg::GrainDraw &g) {
            grains.push_back(g);
            ++inFrame;
        });
        mostInOneFrame = std::max(mostInOneFrame, inFrame);
    }
    REQUIRE(followsRecurrence(grains, 1.0, 1.0));

    // Up to the rounding of t + Δ, which is far below 2^-30 for these onsets.
    constexpr double kRounding = 0x1.0p-30;
    std::size_t floored = 0;
    bool spaced = true;
    for (std::size_t k = 0; k + 1 < grains.size(); ++k)
    {
        const double gap = onsetTime(grains[k + 1]) - onsetTime(grains[k]);
        spaced = spaced && gap >= gg::CloudScheduler::kMinIntervalSamples - kRounding;
        floored += std::abs(gap - gg::CloudScheduler::kMinIntervalSamples) <= kRounding ? 1 : 0;
    }
    INFO("floored = " << floored << " of " << grains.size()
                      << ", most in one frame = " << mostInOneFrame);
    REQUIRE(spaced);
    REQUIRE(floored > grains.size() / 50);
    REQUIRE(mostInOneFrame > 1);
    REQUIRE(mostInOneFrame <= 16);
}

TEST_CASE("live mode draws position and spray in seconds (ruling M1-k)", "[cloud]")
{
    // Live mode: position and spray are seconds behind the write head, each clamped to
    // [0, ring seconds], and positionDraw = position + spray × uniform01(u) with u keyed by
    // (seed, layer, stream, PositionJitter, k). Sample mode keeps the [0, 1] fraction and the
    // bipolar spray.
    gg::CloudParams p;
    p.densityHz = 200.0;
    p.distribution = 0.5;
    p.position = 15.9;
    p.spray = 0.05;
    p.stereo = 0.8;
    const auto jitter = [](uint64_t k) {
        return gg::rng::uniform01(gg::rng::hash({kSeed, 0, 0, gg::rng::Field::PositionJitter, k}));
    };

    gg::CloudScheduler live;
    live.prepare(48000.0);
    live.setSourceMode(gg::SourceMode::Live, 16.0);
    live.setParams(p);
    const auto drawn = collect(live, 0, 48000, 256);
    REQUIRE(drawn.size() > 100);
    bool keyed = true;
    double lowest = std::numeric_limits<double>::infinity();
    double highest = -lowest;
    for (const auto &g : drawn)
    {
        keyed = keyed && g.positionDraw == 15.9 + 0.05 * jitter(g.index);
        lowest = std::min(lowest, g.positionDraw);
        highest = std::max(highest, g.positionDraw);
    }
    REQUIRE(keyed);
    REQUIRE(lowest >= 15.9); // not clamped to 1
    REQUIRE(highest < 15.95);
    REQUIRE(highest - lowest > 0.04);

    // Every other field is the sample-mode draw: the mode changes only the position.
    gg::CloudScheduler sample;
    sample.prepare(48000.0);
    sample.setParams(p);
    const auto sampleDrawn = collect(sample, 0, 48000, 256);
    REQUIRE(sampleDrawn.size() == drawn.size());
    bool sameOtherwise = true;
    bool sampleKeyed = true;
    for (std::size_t i = 0; i < drawn.size(); ++i)
    {
        auto g = drawn[i];
        g.positionDraw = sampleDrawn[i].positionDraw;
        sameOtherwise = sameOtherwise && g == sampleDrawn[i];
        // Sample mode clamps position 15.9 to 1, then adds the bipolar spray and clamps again.
        const double u = gg::rng::bipolar(
            gg::rng::hash({kSeed, 0, 0, gg::rng::Field::PositionJitter, sampleDrawn[i].index}));
        sampleKeyed =
            sampleKeyed && sampleDrawn[i].positionDraw == std::clamp(1.0 + 0.05 * u, 0.0, 1.0);
    }
    REQUIRE(sameOtherwise);
    REQUIRE(sampleKeyed);

    SECTION("live values are clamped to [0, ring seconds]")
    {
        live.reset();
        p.position = 40.0;
        p.spray = 99.0;
        live.setParams(p);
        const auto high = collect(live, 0, 4800, 256);
        REQUIRE_FALSE(high.empty());
        for (const auto &g : high)
        {
            REQUIRE(g.positionDraw == 16.0 + 16.0 * jitter(g.index));
        }
        live.reset();
        p.position = -3.0;
        p.spray = -1.0;
        live.setParams(p);
        for (const auto &g : collect(live, 0, 4800, 256))
        {
            REQUIRE(g.positionDraw == 0.0);
        }
        live.reset();
        p.position = std::numeric_limits<double>::quiet_NaN();
        p.spray = std::numeric_limits<double>::infinity();
        live.setParams(p);
        for (const auto &g : collect(live, 0, 4800, 256))
        {
            REQUIRE(g.positionDraw == 0.5); // the defaults: position 0.5 s, no spray
        }
    }

    SECTION("a mode change re-clamps the controls already set")
    {
        // Set in Live mode, Position 15.9 s; back in Sample mode the same controls draw
        // exactly what a scheduler that was never live draws.
        live.setSourceMode(gg::SourceMode::Sample, 16.0);
        live.reset();
        REQUIRE(collect(live, 0, 48000, 256) == sampleDrawn);
        // And back to Live: seconds again.
        live.setSourceMode(gg::SourceMode::Live, 16.0);
        live.reset();
        REQUIRE(collect(live, 0, 48000, 256) == drawn);
    }

    SECTION("sample-mode draws are unchanged")
    {
        // A scheduler set to Sample explicitly draws what the default one does, for controls in
        // the sample range.
        gg::CloudParams q = p;
        q.position = 0.4;
        q.spray = 0.3;
        gg::CloudScheduler explicitSample;
        explicitSample.prepare(48000.0);
        explicitSample.setSourceMode(gg::SourceMode::Sample, 16.0);
        explicitSample.setParams(q);
        sample.reset();
        sample.setParams(q);
        const auto a = collect(explicitSample, 0, 48000, 256);
        REQUIRE(a == collect(sample, 0, 48000, 256));
        bool keyedSample = true;
        for (const auto &g : a)
        {
            const double u = gg::rng::bipolar(
                gg::rng::hash({kSeed, 0, 0, gg::rng::Field::PositionJitter, g.index}));
            keyedSample = keyedSample && g.positionDraw == std::clamp(0.4 + 0.3 * u, 0.0, 1.0);
        }
        REQUIRE(keyedSample);
    }
}

TEST_CASE("quality is latched per grain", "[cloud]")
{
    // Ruling 2: each grain takes CloudParams::quality as it is at the grain's onset, and the
    // quality changes nothing else. 480 Hz periodic is an onset every 100 samples: grains 0–4
    // start in [0, 500) at Eco, and the rest in [500, 1000) at Normal.
    using gg::Tier;
    gg::CloudParams p = cloudAt(480.0);
    REQUIRE(p.quality == Tier::Normal); // the default
    p.quality = Tier::Eco;
    gg::CloudScheduler cloud;
    cloud.setParams(p);
    std::vector<gg::GrainDraw> grains = collect(cloud, 0, 500, 64);
    REQUIRE(grains.size() == 5);
    p.quality = Tier::Normal;
    cloud.setParams(p);
    const std::vector<gg::GrainDraw> later = collect(cloud, 500, 500, 64);
    grains.insert(grains.end(), later.begin(), later.end());
    REQUIRE(grains.size() == 10);
    CHECK(grains[3].tier == Tier::Eco);
    CHECK(grains[7].tier == Tier::Normal);
    for (const gg::GrainDraw &g : grains)
    {
        INFO("grain " << g.index);
        CHECK(g.tier == (g.index < 5 ? Tier::Eco : Tier::Normal));
    }

    // Against a stream that was Normal throughout, only the tier differs.
    gg::CloudScheduler normal;
    normal.setParams(cloudAt(480.0));
    const std::vector<gg::GrainDraw> reference = collect(normal, 0, 1000, 64);
    REQUIRE(reference.size() == grains.size());
    for (std::size_t i = 0; i < grains.size(); ++i)
    {
        gg::GrainDraw g = grains[i];
        g.tier = Tier::Normal;
        CHECK(g == reference[i]);
    }

    SECTION("a source-mode change keeps the quality")
    {
        p.quality = Tier::Eco;
        cloud.setParams(p);
        cloud.setSourceMode(gg::SourceMode::Live, 16.0);
        cloud.reset();
        for (const gg::GrainDraw &g : collect(cloud, 0, 500, 64))
        {
            CHECK(g.tier == Tier::Eco);
        }
    }
}
