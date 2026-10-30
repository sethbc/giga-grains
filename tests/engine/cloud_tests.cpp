// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The Cloud scheduler (spec §6.2): onsets from t(k+1) = t(k) + Δ(k), with
// Δ(k) = P × ((1 − d) + d × E(k)), P = sampleRate / densityHz and E(k) = −logDet(1 − u(k)).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/MathDet.h"
#include "gg/core/Rng.h"
#include "gg/engine/Engine.h"
#include "gg/grain/CloudScheduler.h"
#include "gg/grain/Tier.h"
#include "gg/source/SourceMode.h"

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
// Schedules from `start` in blocks of `block` until at least `count` grains are emitted, and
// returns the first `count`.
std::vector<gg::GrainDraw> collectGrains(gg::CloudScheduler &cloud, std::size_t count,
                                         uint32_t block, int64_t start = 0)
{
    std::vector<gg::GrainDraw> grains;
    for (int64_t t = start; grains.size() < count; t += block)
    {
        cloud.schedule(t, block, kSeed, 0, 0, [&](const gg::GrainDraw &g) { grains.push_back(g); });
    }
    grains.resize(count);
    return grains;
}

// The scheduler as it was before notes, for parameters (already in range) that don't change
// over the run: the draws spec §6.2 and the class comment define, with no key held.
std::vector<gg::GrainDraw> drawsWithoutNotes(const gg::CloudParams &p, double sampleRate,
                                             gg::SourceMode mode, std::size_t count)
{
    using gg::rng::Field;
    const double period = sampleRate / p.densityHz;
    const double duration = std::max(p.grainDurationSec * sampleRate, 16.0);
    std::vector<gg::GrainDraw> grains;
    double t = 0.0;
    for (uint64_t k = 0; k < count; ++k)
    {
        const auto key = [&](Field f) { return gg::rng::hash({kSeed, 0, 0, f, k}); };
        const double whole = std::floor(t);
        const uint64_t jitter = key(Field::PositionJitter);
        const double pan = p.stereo * gg::rng::bipolar(key(Field::Pan));
        grains.push_back({
            .index = k,
            .onsetSample = static_cast<int64_t>(whole),
            .onsetFrac = t - whole,
            .positionDraw =
                mode == gg::SourceMode::Live
                    ? p.position + p.spray * gg::rng::uniform01(jitter)
                    : std::clamp(p.position + p.spray * gg::rng::bipolar(jitter), 0.0, 1.0),
            .sprayOffsetSec = 0.0,
            .rate = gg::exp2Det(p.pitchSemitones / 12.0),
            .durationSamples = duration,
            .gain = p.gain,
            .pan =
                std::abs(pan) < std::numeric_limits<float>::min() ? 0.0f : static_cast<float>(pan),
            .tier = p.quality,
        });
        const double e = -gg::logDet(1.0 - gg::rng::uniform01(key(Field::Onset)));
        const double d = p.distribution;
        t += std::max(period * ((1.0 - d) + d * e), gg::CloudScheduler::kMinIntervalSamples);
    }
    return grains;
}

// The key a grain picks from `keys` (spec §6.1): keys[min(floor(u × n), n − 1)].
int16_t pickedKey(std::span<const uint8_t> keys, uint64_t k)
{
    const double u = gg::rng::uniform01(gg::rng::hash({kSeed, 0, 0, gg::rng::Field::Note, k}));
    const auto n = static_cast<double>(keys.size());
    const auto i = std::min(static_cast<std::size_t>(std::floor(u * n)), keys.size() - 1);
    return keys[i];
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

TEST_CASE("texture is sanitized and latched per grain", "[cloud]")
{
    // Spec §4: each grain takes CloudParams::texture as it is at its onset; a non-finite value is
    // the default 0, and the rest is clamped to [0, 1]. Texture changes nothing else in a draw.
    gg::CloudScheduler normal;
    normal.setParams(cloudAt(480.0));
    const std::vector<gg::GrainDraw> reference = collect(normal, 0, 500, 64);
    REQUIRE(reference.size() == 5);
    REQUIRE(reference[0].texture == 0.0); // the default
    struct Case
    {
        double given;
        double latched;
    };
    for (const Case c : {Case{0.3, 0.3}, Case{1.0, 1.0}, Case{-0.5, 0.0}, Case{7.0, 1.0},
                         Case{std::numeric_limits<double>::quiet_NaN(), 0.0},
                         Case{std::numeric_limits<double>::infinity(), 0.0}})
    {
        INFO("texture " << c.given);
        gg::CloudParams p = cloudAt(480.0);
        p.texture = c.given;
        gg::CloudScheduler cloud;
        cloud.setParams(p);
        const std::vector<gg::GrainDraw> grains = collect(cloud, 0, 500, 64);
        REQUIRE(grains.size() == reference.size());
        for (std::size_t i = 0; i < grains.size(); ++i)
        {
            REQUIRE(grains[i].texture == c.latched);
            gg::GrainDraw g = grains[i];
            g.texture = 0.0;
            CHECK(g == reference[i]);
        }
    }
}

TEST_CASE("with no key held, every draw is the scheduler's without notes", "[cloud]")
{
    // The note pick has its own rng::Field, so a stream with no key held draws exactly what it
    // drew before notes existed, and every existing golden's event log stays the same.
    struct Case
    {
        const char *name;
        double sampleRate;
        gg::SourceMode mode;
        gg::CloudParams params;
    };
    const Case cases[] = {
        {"poisson-ish, sample mode, eco",
         48000.0,
         gg::SourceMode::Sample,
         {.densityHz = 200.0,
          .distribution = 0.5,
          .grainDurationSec = 0.05,
          .position = 0.4,
          .spray = 0.3,
          .pitchSemitones = 7.0,
          .stereo = 0.8,
          .gain = 0.7f,
          .quality = gg::Tier::Eco}},
        {"poisson, sample mode, 44.1 kHz",
         44100.0,
         gg::SourceMode::Sample,
         {.densityHz = 1000.0,
          .distribution = 1.0,
          .grainDurationSec = 0.2,
          .position = 0.1,
          .spray = 0.9,
          .pitchSemitones = -13.5,
          .stereo = 0.3,
          .gain = 1.5f}},
        {"periodic, live mode",
         48000.0,
         gg::SourceMode::Live,
         {.densityHz = 6400.0, // a period of 7.5 samples
          .grainDurationSec = 0.01,
          .position = 2.0,
          .spray = 1.5,
          .pitchSemitones = 48.0,
          .gain = 0.25f}},
    };
    constexpr std::size_t kGrains = 10000;
    for (const Case &c : cases)
    {
        INFO(c.name);
        const auto expected = drawsWithoutNotes(c.params, c.sampleRate, c.mode, kGrains);

        gg::CloudScheduler cloud;
        cloud.prepare(c.sampleRate);
        cloud.setSourceMode(c.mode, 16.0);
        cloud.setParams(c.params);
        const auto grains = collectGrains(cloud, kGrains, 97);
        CHECK(grains == expected);

        // Explicitly empty, with the gate off (the FX): the same draws.
        gg::CloudScheduler empty;
        empty.prepare(c.sampleRate);
        empty.setSourceMode(c.mode, 16.0);
        empty.setParams(c.params);
        empty.setNoteGate(false);
        empty.setHeldKeys({});
        CHECK(collectGrains(empty, kGrains, 64) == expected);
        CHECK(std::all_of(grains.begin(), grains.end(),
                          [](const gg::GrainDraw &g) { return g.key == -1; }));
    }
}

TEST_CASE("each grain picks one held key", "[cloud]")
{
    // Spec §6.1: u = uniform01(hash(seed, layer, stream, Note, k)) picks keys[floor(u × n)].
    constexpr std::array<uint8_t, 3> chord{48, 60, 67};
    gg::CloudParams p = cloudAt(1000.0, 1.0);
    p.spray = 0.2;
    p.stereo = 0.6;
    constexpr std::size_t kGrains = 30000;

    gg::CloudScheduler cloud;
    cloud.setParams(p);
    cloud.setHeldKeys(chord);
    const auto grains = collectGrains(cloud, kGrains, 256);

    std::array<std::size_t, 3> counts{};
    bool keyed = true;
    for (const gg::GrainDraw &g : grains)
    {
        keyed = keyed && g.key == pickedKey(chord, g.index);
        for (std::size_t i = 0; i < chord.size(); ++i)
        {
            counts[i] += g.key == chord[i] ? 1 : 0;
        }
    }
    REQUIRE(keyed);
    REQUIRE(counts[0] + counts[1] + counts[2] == kGrains);
    for (const std::size_t n : counts)
    {
        const double share = static_cast<double>(n) / static_cast<double>(kGrains);
        CHECK(std::abs(share - 1.0 / 3.0) <= 0.02);
    }

    SECTION("the same seed picks the same sequence")
    {
        gg::CloudScheduler again;
        again.setParams(p);
        again.setHeldKeys(chord);
        REQUIRE(collectGrains(again, kGrains, 61) == grains);
    }

    SECTION("the pick changes no other draw")
    {
        gg::CloudScheduler none;
        none.setParams(p);
        const auto unpicked = collectGrains(none, kGrains, 256);
        bool same = true;
        for (std::size_t i = 0; i < kGrains; ++i)
        {
            gg::GrainDraw g = grains[i];
            same = same && g.rate == gg::exp2Det((g.key - 60) / 12.0);
            g.key = -1;
            g.rate = unpicked[i].rate;
            same = same && g == unpicked[i];
        }
        REQUIRE(same);
    }
}

TEST_CASE("a held key sets the grain's rate", "[cloud]")
{
    // Spec §6.1: exp2Det(clamp(pitchSemitones + (key − 60), −48, 48) / 12).
    struct Case
    {
        uint8_t key;
        double pitch;
        double rate;
    };
    const Case cases[] = {
        {72, 3.0, gg::exp2Det(15.0 / 12.0)},
        {127, 24.0, gg::exp2Det(4.0)},    // +91, clamped to +48
        {0, -24.0, gg::exp2Det(-4.0)},    // −84, clamped to −48
        {60, 7.3, gg::exp2Det(7.3 / 12)}, // note 60 plays at the Pitch parameter alone
    };
    for (const Case &c : cases)
    {
        INFO("key " << int{c.key} << ", pitch " << c.pitch);
        gg::CloudParams p = cloudAt(100.0); // a period of 480 samples
        p.pitchSemitones = c.pitch;
        gg::CloudScheduler cloud;
        cloud.setParams(p);
        const std::array<uint8_t, 1> held{c.key};
        cloud.setHeldKeys(held);
        const auto grains = collect(cloud, 0, 4800, 64);
        REQUIRE(grains.size() == 10);
        for (const gg::GrainDraw &g : grains)
        {
            CHECK(g.key == c.key);
            CHECK(g.rate == c.rate);
        }
    }

    SECTION("with no key held, the rate is the Pitch parameter's alone")
    {
        gg::CloudParams p = cloudAt(100.0);
        p.pitchSemitones = 3.0;
        gg::CloudScheduler cloud;
        cloud.setParams(p);
        const std::array<uint8_t, 1> held{72};
        cloud.setHeldKeys(held);
        REQUIRE(collect(cloud, 0, 480, 64).front().rate == gg::exp2Det(15.0 / 12.0));
        cloud.setHeldKeys({});
        const auto released = collect(cloud, 480, 480, 64);
        REQUIRE(released.size() == 1);
        CHECK(released[0].key == -1);
        CHECK(released[0].rate == gg::exp2Det(3.0 / 12.0));
    }
}

TEST_CASE("the first held key forces an onset at once", "[cloud]")
{
    // Spec §6.2–§6.3: when the held set goes from empty to non-empty, the next schedule() call
    // moves the pending onset to its blockStart g, whatever the period.
    gg::CloudScheduler cloud;
    cloud.setParams(cloudAt(1.0)); // a period of 48000 samples
    const auto before = collect(cloud, 0, 4800, 64);
    REQUIRE(before.size() == 1); // grain 0 at 0; grain 1 pending at 48000

    const std::array<uint8_t, 1> one{60};
    cloud.setHeldKeys(one);
    const auto forced = collect(cloud, 4800, 4800, 64);
    REQUIRE(forced.size() == 1);
    CHECK(forced[0].index == 1);
    CHECK(forced[0].onsetSample == 4800);
    CHECK(forced[0].onsetFrac == 0.0);
    CHECK(forced[0].key == 60);

    // Adding a key to the held set doesn't move the pending onset, 4800 + 48000.
    const std::array<uint8_t, 2> two{60, 64};
    cloud.setHeldKeys(two);
    const auto added = collect(cloud, 9600, 48000, 64);
    REQUIRE(added.size() == 1);
    CHECK(added[0].index == 2);
    CHECK(onsetTime(added[0]) == 52800.0);

    SECTION("it overrides a rescale pending from the same boundary")
    {
        // Ruling M1-h alone would move the onset pending at 100800 to max(52800 + 24000, g).
        cloud.setHeldKeys({});
        CHECK(collect(cloud, 57600, 9600, 64).empty());
        cloud.setParams(cloudAt(2.0));
        cloud.setHeldKeys(one);
        const auto again = collect(cloud, 67200, 24000, 64);
        REQUIRE(again.size() == 1);
        CHECK(onsetTime(again[0]) == 67200.0);
        // The next interval is the new period's, from the forced onset.
        const auto next = collect(cloud, 91200, 24000, 64);
        REQUIRE(next.size() == 1);
        CHECK(onsetTime(next[0]) == 91200.0);
    }

    SECTION("keys held and released before the next schedule() force nothing")
    {
        cloud.setHeldKeys({});
        cloud.setHeldKeys(one);
        cloud.setHeldKeys({}); // empty again before the boundary: the onset stays at 100800
        const auto kept = collect(cloud, 57600, 48000, 64);
        REQUIRE(kept.size() == 1);
        CHECK(onsetTime(kept[0]) == 100800.0);
        CHECK(kept[0].key == -1);
    }
}

TEST_CASE("the note gate holds the stream until a key is held", "[cloud]")
{
    // Spec §6.2: with the gate on (the instrument), no grain spawns while no key is held; the
    // first key starts the cloud at its boundary, and the grain index keeps counting.
    gg::CloudScheduler cloud;
    cloud.setParams(cloudAt(100.0)); // a period of 480 samples
    cloud.setNoteGate(true);
    REQUIRE(collect(cloud, 0, 480000, 64).empty()); // 10 s

    const std::array<uint8_t, 1> held{67};
    cloud.setHeldKeys(held);
    const auto played = collect(cloud, 480000, 1024, 32);
    REQUIRE(played.size() == 3);
    CHECK(played[0].index == 0);
    CHECK(onsetTime(played[0]) == 480000.0);
    CHECK(onsetTime(played[1]) == 480480.0);
    CHECK(played[2].key == 67);

    cloud.setHeldKeys({});
    CHECK(collect(cloud, 481024, 48000, 32).empty());

    // A second note: the onset is forced again, and k continues from 3.
    CHECK(collect(cloud, 529024, 224, 32).empty());
    cloud.setHeldKeys(held);
    const auto second = collect(cloud, 529248, 32, 32);
    REQUIRE(second.size() == 1);
    CHECK(second[0].index == 3);
    CHECK(onsetTime(second[0]) == 529248.0);

    SECTION("turning the gate off with no key held resumes the stream at the boundary")
    {
        cloud.setHeldKeys({});
        CHECK(collect(cloud, 529280, 4800, 32).empty());
        cloud.setNoteGate(false);
        const auto resumed = collect(cloud, 534080, 480, 32);
        REQUIRE(resumed.size() == 1);
        CHECK(onsetTime(resumed[0]) == 534080.0);
        CHECK(resumed[0].key == -1);
    }
}

TEST_CASE("sample mode can draw spray in seconds (the instrument's Spray)", "[cloud]")
{
    // With sprayInSeconds, Sample mode's spray is seconds, bipolar around the position: the draw
    // keeps the position as given and carries spray × bipolar(u) as sprayOffsetSec, for the
    // source to convert at its own rate. u is the PositionJitter draw, as without it.
    const auto bipolar = [](uint64_t k) {
        return gg::rng::bipolar(gg::rng::hash({kSeed, 0, 0, gg::rng::Field::PositionJitter, k}));
    };
    gg::CloudParams p = cloudAt(1000.0, 0.5);
    p.position = 0.5;
    p.spray = 0.1;
    p.stereo = 0.8;
    REQUIRE_FALSE(p.sprayInSeconds); // the default

    gg::CloudScheduler fraction;
    fraction.setParams(p);
    const auto asFraction = collect(fraction, 0, 48000, 64);
    REQUIRE(asFraction.size() > 900);
    bool fractionKeyed = true;
    for (const gg::GrainDraw &g : asFraction)
    {
        fractionKeyed = fractionKeyed &&
                        g.positionDraw == std::clamp(0.5 + 0.1 * bipolar(g.index), 0.0, 1.0) &&
                        g.sprayOffsetSec == 0.0;
    }
    REQUIRE(fractionKeyed);

    p.sprayInSeconds = true;
    gg::CloudScheduler seconds;
    seconds.setParams(p);
    const auto inSeconds = collect(seconds, 0, 48000, 64);
    REQUIRE(inSeconds.size() == asFraction.size());
    bool secondsKeyed = true;
    for (std::size_t i = 0; i < inSeconds.size(); ++i)
    {
        const gg::GrainDraw &g = inSeconds[i];
        secondsKeyed =
            secondsKeyed && g.positionDraw == 0.5 && g.sprayOffsetSec == 0.1 * bipolar(g.index);
        // Nothing else changes: the onsets and every other draw.
        gg::GrainDraw same = g;
        same.positionDraw = asFraction[i].positionDraw;
        same.sprayOffsetSec = 0.0;
        secondsKeyed = secondsKeyed && same == asFraction[i];
    }
    REQUIRE(secondsKeyed);

    SECTION("spray in seconds isn't clamped to a fraction")
    {
        p.spray = 2.0;
        seconds.setParams(p);
        seconds.reset();
        bool keyed = true;
        for (const gg::GrainDraw &g : collect(seconds, 0, 4800, 64))
        {
            keyed = keyed && g.positionDraw == 0.5 && g.sprayOffsetSec == 2.0 * bipolar(g.index);
        }
        REQUIRE(keyed);
        p.spray = -1.0;
        seconds.setParams(p);
        for (const gg::GrainDraw &g : collect(seconds, 4800, 4800, 64))
        {
            REQUIRE(g.sprayOffsetSec == 0.0);
        }
    }

    SECTION("live mode ignores it: its spray is already seconds")
    {
        p.position = 1.0;
        gg::CloudScheduler live;
        live.setSourceMode(gg::SourceMode::Live, 16.0);
        live.setParams(p);
        gg::CloudScheduler liveUnset;
        liveUnset.setSourceMode(gg::SourceMode::Live, 16.0);
        p.sprayInSeconds = false;
        liveUnset.setParams(p);
        const auto a = collect(live, 0, 48000, 64);
        const auto b = collect(liveUnset, 0, 48000, 64);
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            REQUIRE(a[i] == b[i]);
            REQUIRE(a[i].sprayOffsetSec == 0.0);
        }
    }
}

namespace
{
struct StartLog
{
    std::vector<gg::GrainSpec> grains;
    static void record(void *ctx, const gg::GrainSpec &g)
    {
        static_cast<StartLog *>(ctx)->grains.push_back(g);
    }
};

// The grains an engine at 48 kHz spawns in `seconds` over a silent mono source of `frames`
// frames at `sourceRate`, with params p.
std::vector<gg::GrainSpec> sampleStarts(const gg::CloudParams &p, int64_t frames, double sourceRate,
                                        double seconds)
{
    gg::EngineConfig config;
    config.sampleRate = 48000.0;
    config.maxBlockSize = 4096;
    config.seed = kSeed;
    gg::Engine engine(config);
    engine.setSource(std::make_shared<gg::SampleBuffer>(1, frames, sourceRate));
    engine.setCloudParams(p);
    StartLog log;
    log.grains.reserve(static_cast<std::size_t>(p.densityHz * seconds) + 16);
    engine.setEventSink(&StartLog::record, &log);
    std::vector<float> l(4096), r(4096);
    float *out[2] = {l.data(), r.data()};
    const auto total = static_cast<int64_t>(seconds * 48000.0);
    for (int64_t done = 0; done < total; done += 4096)
    {
        engine.process(out, 2, static_cast<uint32_t>(std::min<int64_t>(4096, total - done)));
    }
    return log.grains;
}
} // namespace

TEST_CASE("spray in seconds spreads start frames around the position", "[cloud]")
{
    // The start frame is clamp(position × (frames − 1) + spray × bipolar(u) × sourceRate, 0,
    // frames − 1), u the PositionJitter draw: Spray's spread in frames doesn't depend on the
    // file's length.
    const auto bipolar = [](uint64_t k) {
        return gg::rng::bipolar(gg::rng::hash({kSeed, 0, 0, gg::rng::Field::PositionJitter, k}));
    };
    gg::CloudParams p = cloudAt(400.0);
    p.grainDurationSec = 0.01;
    p.position = 0.5;
    p.spray = 0.1;
    p.sprayInSeconds = true;

    SECTION("a 10 s source at 48 kHz, position 0.5, spray 0.1 s")
    {
        constexpr int64_t kFrames = 480'000;
        const auto grains = sampleStarts(p, kFrames, 48000.0, 30.0);
        REQUIRE(grains.size() == 12'000);
        const double centre = 0.5 * static_cast<double>(kFrames - 1); // 239_999.5
        REQUIRE(centre == 239'999.5);
        double lowest = centre;
        double highest = centre;
        double sum = 0.0;
        bool exact = true;
        for (const gg::GrainSpec &g : grains)
        {
            exact = exact && g.startPos == std::clamp(centre + 0.1 * bipolar(g.index) * 48000.0,
                                                      0.0, static_cast<double>(kFrames - 1));
            lowest = std::min(lowest, g.startPos);
            highest = std::max(highest, g.startPos);
            sum += g.startPos;
        }
        REQUIRE(exact);
        // Every start lies within ±0.1 s = ±4800 frames of the centre, and the draws reach
        // both ends of that span (as a fraction of the file, spray 0.1 would be ±48_000).
        REQUIRE(lowest >= centre - 4800.0);
        REQUIRE(highest <= centre + 4800.0);
        REQUIRE(lowest < centre - 4700.0);
        REQUIRE(highest > centre + 4700.0);
        // Centred: the mean is within 1% of the span's 9600 frames.
        const double mean = sum / static_cast<double>(grains.size());
        REQUIRE(std::abs(mean - centre) <= 0.01 * 9600.0);
    }

    SECTION("the spread is in the source's own frames")
    {
        // 10 s at 44.1 kHz: ±0.1 s is ±4410 source frames, whatever the output rate.
        constexpr int64_t kFrames = 441'000;
        const auto grains = sampleStarts(p, kFrames, 44100.0, 5.0);
        REQUIRE(grains.size() == 2000);
        const double centre = 0.5 * static_cast<double>(kFrames - 1);
        for (const gg::GrainSpec &g : grains)
        {
            REQUIRE(g.startPos == centre + 0.1 * bipolar(g.index) * 44100.0);
            REQUIRE(std::abs(g.startPos - centre) <= 4410.0);
        }
    }

    SECTION("the start is clamped to the source")
    {
        constexpr int64_t kFrames = 48'000; // 1 s
        constexpr double kLast = static_cast<double>(kFrames - 1);
        p.spray = 1.0;
        for (const double position : {0.0, 1.0})
        {
            INFO("position " << position);
            p.position = position;
            const auto grains = sampleStarts(p, kFrames, 48000.0, 2.0);
            REQUIRE(grains.size() == 800);
            int atEnd = 0;
            for (const gg::GrainSpec &g : grains)
            {
                REQUIRE(g.startPos ==
                        std::clamp(position * kLast + bipolar(g.index) * 48000.0, 0.0, kLast));
                atEnd += g.startPos == position * kLast ? 1 : 0;
            }
            // About half the draws point outside the file and are clamped to its end.
            REQUIRE(atEnd > 300);
        }
    }
}

namespace
{
// A grain's pitch spread (spec §6.6): the rate the scheduler draws for grain k with the given
// Pitch, spread and held key (−1 for none).
double spreadRate(double pitch, double spread, int key, uint64_t k)
{
    const double u = gg::rng::bipolar(gg::rng::hash({kSeed, 0, 0, gg::rng::Field::PitchSpread, k}));
    const double semis = pitch + (key >= 0 ? static_cast<double>(key - 60) : 0.0) + spread * u;
    return gg::exp2Det(std::clamp(semis, -48.0, 48.0) / 12.0);
}

// A grain's size spread (spec §6.6): the duration in samples the scheduler draws for grain k.
double spreadDuration(double sizeSec, double spread, double sampleRate, uint64_t k)
{
    const double u = gg::rng::bipolar(gg::rng::hash({kSeed, 0, 0, gg::rng::Field::SizeSpread, k}));
    const double log2Size = gg::logDet(sizeSec) / gg::logDet(2.0);
    const double seconds = gg::exp2Det(std::clamp(log2Size + spread * u, -7.643856189774724, 1.0));
    return std::max(16.0, seconds * sampleRate);
}

// Non-default values for every control the spreads could disturb.
gg::CloudParams busyParams()
{
    gg::CloudParams p = cloudAt(200.0, 0.5);
    p.grainDurationSec = 0.07;
    p.position = 0.4;
    p.spray = 0.3;
    p.pitchSemitones = 7.3;
    p.stereo = 0.8;
    p.gain = 0.75f;
    p.texture = 0.25;
    return p;
}
} // namespace

TEST_CASE("pitch spread 0 leaves every rate bit for bit", "[cloud]")
{
    for (const gg::CloudParams &base : {gg::CloudParams{}, busyParams()})
    {
        for (const double zero : {0.0, -0.0})
        {
            INFO("pitch " << base.pitchSemitones << ", spread " << zero);
            gg::CloudParams explicitZero = base;
            explicitZero.pitchSpreadSemitones = zero;
            explicitZero.sizeSpreadOctaves = zero;

            gg::CloudScheduler a;
            a.setParams(base);
            gg::CloudScheduler b;
            b.setParams(explicitZero);
            const auto grainsA = collectGrains(a, 1000, 97);
            const auto grainsB = collectGrains(b, 1000, 97);
            REQUIRE(grainsA == grainsB);
            // And both are the scheduler's draws before the spreads: M1's.
            const auto m1 = drawsWithoutNotes(base, 48000.0, gg::SourceMode::Sample, 1000);
            for (std::size_t i = 0; i < m1.size(); ++i)
            {
                gg::GrainDraw expected = m1[i];
                expected.texture = base.texture; // drawsWithoutNotes leaves it at 0
                REQUIRE(grainsB[i] == expected);
            }

            // With a key held, the rate is M1's keyed rate.
            const std::array<uint8_t, 1> held{72};
            a.reset();
            a.setHeldKeys(held);
            b.reset();
            b.setHeldKeys(held);
            const auto keyedA = collectGrains(a, 1000, 97);
            const auto keyedB = collectGrains(b, 1000, 97);
            REQUIRE(keyedA == keyedB);
            const double keyedRate =
                gg::exp2Det(std::clamp(base.pitchSemitones + 12.0, -48.0, 48.0) / 12.0);
            bool allKeyed = true;
            for (const gg::GrainDraw &g : keyedB)
            {
                allKeyed = allKeyed && g.rate == keyedRate;
            }
            REQUIRE(allKeyed);
        }
    }
}

TEST_CASE("pitch spread 24 stays within the clamp", "[cloud]")
{
    struct Case
    {
        double pitch;
        int key; // −1: none held
    };
    // Pitch 24 alone reaches [0, 48) semitones; with key 84 (+24) or key 36 (−24) at Pitch ±24
    // the spread runs past ±48, and the clamp holds the rate at 16 or 1/16.
    for (const Case c : {Case{24.0, -1}, Case{24.0, 84}, Case{-24.0, 36}})
    {
        INFO("pitch " << c.pitch << ", key " << c.key);
        gg::CloudParams p = cloudAt(4800.0); // a period of 10 samples
        p.pitchSemitones = c.pitch;
        p.pitchSpreadSemitones = 24.0;
        gg::CloudScheduler cloud;
        cloud.setParams(p);
        if (c.key >= 0)
        {
            const std::array<uint8_t, 1> held{static_cast<uint8_t>(c.key)};
            cloud.setHeldKeys(held);
        }
        const auto grains = collectGrains(cloud, 10000, 256);

        const double centre =
            gg::exp2Det(std::clamp(c.pitch + (c.key >= 0 ? c.key - 60 : 0), -48.0, 48.0) / 12.0);
        bool inRange = true;
        bool keyed = true;
        std::size_t below = 0;
        std::size_t above = 0;
        std::size_t clamped = 0;
        for (const gg::GrainDraw &g : grains)
        {
            inRange = inRange && g.rate >= gg::exp2Det(-4.0) && g.rate <= gg::exp2Det(4.0);
            keyed = keyed && g.rate == spreadRate(c.pitch, 24.0, c.key, g.index);
            below += g.rate < centre ? 1 : 0;
            above += g.rate > centre ? 1 : 0;
            clamped += (g.rate == 16.0 || g.rate == 1.0 / 16.0) ? 1 : 0;
        }
        REQUIRE(inRange);
        REQUIRE(keyed);
        if (c.key < 0)
        {
            // Both halves of the spread are hit, about equally.
            REQUIRE(below > 4500);
            REQUIRE(above > 4500);
            REQUIRE(clamped == 0);
        }
        else
        {
            // Half the draws pass the bound and are clamped to it.
            REQUIRE(clamped > 4500);
            REQUIRE(clamped < 5500);
        }
    }
}

TEST_CASE("size spread 0 leaves every duration bit for bit", "[cloud]")
{
    for (const double size : {0.1, 0.07, 0.001, 3.0})
    {
        INFO("size " << size);
        gg::CloudParams base = busyParams();
        base.grainDurationSec = size;
        gg::CloudParams explicitZero = base;
        explicitZero.sizeSpreadOctaves = 0.0;

        gg::CloudScheduler a;
        a.setParams(base);
        gg::CloudScheduler b;
        b.setParams(explicitZero);
        const auto grainsA = collectGrains(a, 1000, 97);
        const auto grainsB = collectGrains(b, 1000, 97);
        REQUIRE(grainsA == grainsB);
        const double m1 = std::max(size * 48000.0, 16.0);
        bool allM1 = true;
        for (const gg::GrainDraw &g : grainsB)
        {
            allM1 = allM1 && g.durationSamples == m1;
        }
        REQUIRE(allM1);
    }
}

TEST_CASE("size spread 2 stays within Size's range", "[cloud]")
{
    struct Case
    {
        double size;
        double sampleRate;
        double spread;
    };
    // 100 ms ± 2 octaves is 25–400 ms, inside Size's range; 1 s reaches past 2 s, and 10 ms below
    // 5 ms, which the clamp holds; at 1 kHz, 5 ms is 5 samples, below the 16-sample floor.
    for (const Case c : {Case{0.1, 48000.0, 2.0}, Case{1.0, 48000.0, 2.0}, Case{0.01, 48000.0, 2.0},
                         Case{0.01, 1000.0, 4.0}})
    {
        INFO("size " << c.size << ", rate " << c.sampleRate << ", spread " << c.spread);
        gg::CloudParams p = cloudAt(c.sampleRate / 10.0); // a period of 10 samples
        p.grainDurationSec = c.size;
        p.sizeSpreadOctaves = c.spread;
        gg::CloudScheduler cloud;
        cloud.prepare(c.sampleRate);
        cloud.setParams(p);
        const auto grains = collectGrains(cloud, 10000, 256);

        const double centre = c.size * c.sampleRate;
        // exp2Det(log2 0.005) is 0.005 to within a few ulp.
        const double lowest = std::max(16.0, 0.005 * c.sampleRate * (1.0 - 1e-12));
        // The duration the lower clamp gives, exactly.
        const double clampedLow = std::max(16.0, gg::exp2Det(-7.643856189774724) * c.sampleRate);
        bool inRange = true;
        bool keyed = true;
        bool gainKept = true;
        std::size_t below = 0;
        std::size_t above = 0;
        std::size_t atBound = 0;
        for (const gg::GrainDraw &g : grains)
        {
            inRange = inRange && g.durationSamples >= lowest &&
                      g.durationSamples <= 2.0 * c.sampleRate && g.durationSamples >= 16.0;
            keyed = keyed &&
                    g.durationSamples == spreadDuration(c.size, c.spread, c.sampleRate, g.index);
            gainKept = gainKept && g.gain == p.gain; // the layer's Size normalizes, not the grain's
            below += g.durationSamples < centre ? 1 : 0;
            above += g.durationSamples > centre ? 1 : 0;
            atBound += (g.durationSamples == 2.0 * c.sampleRate || g.durationSamples == clampedLow)
                           ? 1
                           : 0;
        }
        REQUIRE(inRange);
        REQUIRE(keyed);
        REQUIRE(gainKept);
        // At 1 kHz the Size itself, 10 samples, is below the floor: nothing is shorter.
        if (c.sampleRate == 1000.0)
        {
            REQUIRE(below == 0);
        }
        else
        {
            REQUIRE(below > 2500);
        }
        REQUIRE(above > 2500);
        if (c.size == 0.1)
        {
            REQUIRE(atBound == 0);
            REQUIRE(grains[0].durationSamples >= 0.025 * 48000.0 * (1.0 - 1e-12));
        }
        else
        {
            // The bound is 1 octave from the Size, so a quarter of the 4 octaves drawn pass it;
            // at 1 kHz, the 16-sample floor (16 ms) holds every draw below log2 0.016, over half.
            REQUIRE(atBound > (c.sampleRate == 1000.0 ? 5000u : 2200u));
            REQUIRE(atBound < (c.sampleRate == 1000.0 ? 6500u : 2800u));
        }
    }
}

TEST_CASE("the spreads are sanitized", "[cloud]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const gg::CloudParams base = busyParams();
    gg::CloudScheduler reference;
    reference.setParams(base);
    const auto m1 = collectGrains(reference, 200, 97);

    SECTION("a non-finite or negative spread is 0")
    {
        for (const double bad : {nan, inf, -inf, -1.0})
        {
            INFO("spread " << bad);
            gg::CloudParams p = base;
            p.pitchSpreadSemitones = bad;
            p.sizeSpreadOctaves = bad;
            gg::CloudScheduler cloud;
            cloud.setParams(p);
            REQUIRE(collectGrains(cloud, 200, 97) == m1);
        }
    }

    SECTION("a spread past its range is clamped to it")
    {
        gg::CloudParams p = base;
        p.pitchSpreadSemitones = 100.0;
        p.sizeSpreadOctaves = 9.0;
        gg::CloudScheduler cloud;
        cloud.setParams(p);
        const auto grains = collectGrains(cloud, 200, 97);
        bool clamped = true;
        for (const gg::GrainDraw &g : grains)
        {
            clamped =
                clamped && g.rate == spreadRate(base.pitchSemitones, 48.0, -1, g.index) &&
                g.durationSamples == spreadDuration(base.grainDurationSec, 4.0, 48000.0, g.index);
        }
        REQUIRE(clamped);
    }
}

TEST_CASE("a spread changes no other draw", "[cloud]")
{
    const gg::CloudParams base = busyParams();
    gg::CloudParams spread = base;
    spread.pitchSpreadSemitones = 12.0;
    spread.sizeSpreadOctaves = 1.5;
    for (const gg::SourceMode mode : {gg::SourceMode::Sample, gg::SourceMode::Live})
    {
        gg::CloudScheduler a;
        a.setSourceMode(mode, 4.0);
        a.setParams(base);
        gg::CloudScheduler b;
        b.setSourceMode(mode, 4.0);
        b.setParams(spread);
        const std::array<uint8_t, 3> held{48, 60, 67};
        a.setHeldKeys(held);
        b.setHeldKeys(held);
        const auto grainsA = collectGrains(a, 1000, 97);
        const auto grainsB = collectGrains(b, 1000, 97);
        bool same = true;
        bool spreadRates = false;
        bool spreadSizes = false;
        for (std::size_t i = 0; i < grainsA.size(); ++i)
        {
            gg::GrainDraw g = grainsB[i];
            spreadRates = spreadRates || g.rate != grainsA[i].rate;
            spreadSizes = spreadSizes || g.durationSamples != grainsA[i].durationSamples;
            g.rate = grainsA[i].rate;
            g.durationSamples = grainsA[i].durationSamples;
            same = same && g == grainsA[i];
        }
        REQUIRE(same);
        REQUIRE(spreadRates);
        REQUIRE(spreadSizes);
    }
}
