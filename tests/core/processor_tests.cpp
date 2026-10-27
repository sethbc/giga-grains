// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The Processor (spec §6.1, §6.4): parameter events on the 32-sample control grid, the Mix and
// Level ramps, the wet chain (× Level → soft limiter → Mix with dry), and its real-time,
// denormal and determinism contracts.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gg/core/DenormalGuard.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Processor.h"
#include "gg/core/Rng.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "support/AllocTripwire.h"

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using gg::core::kParamCount;
using gg::core::Mode;
using gg::core::ParamEvent;
using gg::core::ParamSlot;
using gg::core::Processor;
using gg::test::AllocTripwire;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

size_t at(ParamSlot s) { return static_cast<size_t>(s); }

float clampTo(ParamSlot s, float v)
{
    const auto &meta = gg::core::paramMeta(s);
    return std::clamp(v, meta.minVal, meta.maxVal);
}

// Records spawned grains into preallocated storage: the sink runs inside process().
struct GrainLog
{
    static constexpr std::size_t kCapacity = 16384;
    std::vector<gg::GrainSpec> grains;
    bool overflowed{false};

    GrainLog() { grains.reserve(kCapacity); }

    static void record(void *ctx, const gg::GrainSpec &g)
    {
        auto *log = static_cast<GrainLog *>(ctx);
        if (log->grains.size() < kCapacity)
        {
            log->grains.push_back(g);
        }
        else
        {
            log->overflowed = true;
        }
    }
};

double onset(const gg::GrainSpec &g) { return static_cast<double>(g.onsetSample) + g.onsetFrac; }

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

// Stereo noise in ±amp from gg::rng, the two channels independent.
Stereo noise(int64_t frames, float amp, uint64_t seed)
{
    Stereo s{std::vector<float>(static_cast<std::size_t>(frames)),
             std::vector<float>(static_cast<std::size_t>(frames))};
    for (int64_t i = 0; i < frames; ++i)
    {
        using gg::rng::Field;
        const auto k = static_cast<uint64_t>(i);
        const auto j = static_cast<std::size_t>(i);
        s.left[j] = amp * static_cast<float>(
                              gg::rng::bipolar(gg::rng::hash({seed, 0, 0, Field::SignalNoise, k})));
        s.right[j] = amp * static_cast<float>(gg::rng::bipolar(
                               gg::rng::hash({seed, 0, 1, Field::SignalNoise, k})));
    }
    return s;
}

double uniform(uint64_t seed, uint64_t index)
{
    return gg::rng::uniform01(gg::rng::hash({seed, 0, 0, gg::rng::Field::SignalNoise, index}));
}

bool sameBits(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

bool allFinite(const std::vector<float> &v)
{
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

float peak(const std::vector<float> &v)
{
    float p = 0.0f;
    for (const float x : v)
    {
        p = std::max(p, std::abs(x));
    }
    return p;
}

// The onsets of the first three grains of a fresh FX processor at 8 kHz with periodic onsets
// (Distribution 0) and the default 16 Hz (P = 500 samples), when clockBefore frames are rendered
// and then one event at `offset` of the next block sets Density to its maximum. At 8 kHz the
// maximum, about 400 Hz, gives P of about 20 samples, shorter than a grid cell, so where the
// change lands shows in the onsets (ruling M1-h: the pending onset becomes
// max(t(k−1) + Δ_new(k−1), g) at the boundary g).
std::array<double, 3> firstOnsets(uint32_t clockBefore, uint32_t offset)
{
    Processor p({8000.0, 4096, 7, Mode::Fx});
    p.setBase(ParamSlot::Distribution, 0.0f);
    GrainLog log;
    p.setEventSink(&GrainLog::record, &log);
    std::vector<float> left(1024), right(1024);
    float *out[2] = {left.data(), right.data()};
    if (clockBefore > 0)
    {
        p.process(nullptr, 0, out, 2, clockBefore, {});
    }
    const ParamEvent e{offset, ParamSlot::Density, false,
                       gg::core::paramMeta(ParamSlot::Density).maxVal};
    p.process(nullptr, 0, out, 2, 512, std::span(&e, 1));
    REQUIRE(log.grains.size() >= 3);
    REQUIRE(log.grains[0].index == 0);
    REQUIRE(log.grains[2].index == 2);
    return {onset(log.grains[0]), onset(log.grains[1]), onset(log.grains[2])};
}
} // namespace

TEST_CASE("events apply at the next grid boundary", "[core]")
{
    STATIC_REQUIRE(Processor::kControlGrid == 32);
    gg::core::ParamValues maxDensity{};
    maxDensity[at(ParamSlot::Density)] = gg::core::paramMeta(ParamSlot::Density).maxVal;
    const double newP = 8000.0 / gg::core::toCloudParams(maxDensity).densityHz;
    REQUIRE(newP > 19.9);
    REQUIRE(newP < 20.1);

    struct Case
    {
        uint32_t clockBefore;
        uint32_t offset;
        double boundary; // ceil((clockBefore + offset) / 32) × 32
    };
    for (const Case c : {Case{0, 5, 32.0}, Case{0, 31, 32.0}, Case{0, 32, 32.0}, Case{0, 33, 64.0},
                         Case{100, 0, 128.0}, Case{100, 28, 128.0}, Case{100, 29, 160.0}})
    {
        INFO("clock " << c.clockBefore << ", offset " << c.offset);
        const auto t = firstOnsets(c.clockBefore, c.offset);
        REQUIRE(t[0] == 0.0);
        // Grain 1 was due at 500 with the old period; at the boundary it moves to
        // max(0 + newP, boundary), which is the boundary itself.
        REQUIRE(t[1] == c.boundary);
        // The first interval computed after the boundary uses the new period.
        REQUIRE(t[2] == c.boundary + newP);
    }

    // An event exactly on a boundary at the start of the block applies before grain 0's
    // interval is drawn.
    const auto t = firstOnsets(0, 0);
    REQUIRE(t[1] == newP);
    REQUIRE(t[2] == newP + newP);

    // Without the event, the old period: grain 1 at 500.
    Processor p({8000.0, 4096, 7, Mode::Fx});
    p.setBase(ParamSlot::Distribution, 0.0f);
    GrainLog log;
    p.setEventSink(&GrainLog::record, &log);
    std::vector<float> left(1024), right(1024);
    float *out[2] = {left.data(), right.data()};
    p.process(nullptr, 0, out, 2, 1024, {});
    REQUIRE(log.grains.size() >= 2);
    REQUIRE(onset(log.grains[1]) == 500.0);
}

TEST_CASE("mix and level ramp over 10 ms", "[core]")
{
    SECTION("Mix")
    {
        for (const double sr : {48000.0, 44100.0})
        {
            INFO("sample rate " << sr);
            const auto ramp = static_cast<int64_t>(std::llround(0.01 * sr)); // 480, 441
            Processor p({sr, 4096, 1, Mode::Fx});
            // Grains 16 s behind the head read only silence in this short render, so the wet
            // signal is 0 and the output is the dry part alone: 1 − m for a dry signal of 1.
            p.setBase(ParamSlot::Position, 1.0f);
            p.setBase(ParamSlot::Mix, 0.0f);
            constexpr uint32_t kFrames = 1024;
            const std::vector<float> ones(kFrames, 1.0f);
            const float *in[2] = {ones.data(), ones.data()};
            std::vector<float> left(kFrames), right(kFrames);
            float *out[2] = {left.data(), right.data()};
            const ParamEvent e{5, ParamSlot::Mix, false, 1.0};
            p.process(in, 2, out, 2, kFrames, std::span(&e, 1));

            REQUIRE(sameBits(left, right));
            constexpr int64_t b = 32; // the event's boundary
            const auto y = [&](int64_t t) { return left[static_cast<std::size_t>(t)]; };
            for (int64_t t = 0; t <= b; ++t)
            {
                INFO("t = " << t);
                REQUIRE(y(t) == 1.0f);
            }
            for (int64_t i = 1; i < ramp; ++i)
            {
                INFO("i = " << i);
                const double expected = 1.0 - static_cast<double>(i) / static_cast<double>(ramp);
                REQUIRE_THAT(static_cast<double>(y(b + i)), WithinAbs(expected, 1e-6));
                // Linear: every step is 1/ramp.
                REQUIRE_THAT(static_cast<double>(y(b + i - 1) - y(b + i)),
                             WithinAbs(1.0 / static_cast<double>(ramp), 1e-6));
            }
            REQUIRE(y(b + ramp - 1) > 0.0f);
            REQUIRE(y(b + ramp) == 0.0f); // m reaches 1 exactly, ramp samples after b
            for (int64_t t = b + ramp; t < kFrames; ++t)
            {
                INFO("t = " << t);
                REQUIRE(y(t) == 0.0f);
            }
        }
    }

    SECTION("Level")
    {
        // Mix 1 and a wet signal well inside the limiter's linear range, rendered twice: with
        // Level stepping from 0 dB to -60 dB at an event, and without. Their ratio is the
        // ramped Level gain.
        constexpr uint32_t kFrames = 4096;
        const auto ramp = static_cast<int64_t>(std::llround(0.01 * kSampleRate));
        const Stereo input = noise(kFrames, 0.05f, 11);
        const auto render = [&](std::span<const ParamEvent> events) {
            Processor p({kSampleRate, 4096, 1, Mode::Fx});
            p.setBase(ParamSlot::Mix, 1.0f);
            p.setBase(ParamSlot::Position, 0.0f);
            p.setBase(ParamSlot::Density, 7.0f); // 128 grains/s: always some sounding
            const float *in[2] = {input.left.data(), input.right.data()};
            std::vector<float> left(kFrames), right(kFrames);
            float *out[2] = {left.data(), right.data()};
            p.process(in, 2, out, 2, kFrames, events);
            return left;
        };
        const ParamEvent e{2000, ParamSlot::Level, false, -60.0};
        const std::vector<float> stepped = render(std::span(&e, 1));
        const std::vector<float> flat = render({});
        REQUIRE(peak(flat) > 0.005f);
        REQUIRE(peak(flat) < 0.5f);

        gg::core::ParamValues minLevel{};
        minLevel[at(ParamSlot::Level)] = -60.0f;
        const double target = gg::core::toWetParams(minLevel).levelGain;
        constexpr int64_t b = 2016; // the event's boundary
        int checked = 0;
        for (int64_t t = 0; t < kFrames; ++t)
        {
            const auto j = static_cast<std::size_t>(t);
            if (t <= b)
            {
                INFO("t = " << t);
                REQUIRE(stepped[j] == flat[j]);
                continue;
            }
            if (std::abs(flat[j]) < 1e-4f)
            {
                continue; // too quiet for a ratio
            }
            const int64_t i = t - b;
            const double gain = i >= ramp ? target
                                          : 1.0 + (target - 1.0) * static_cast<double>(i) /
                                                      static_cast<double>(ramp);
            INFO("t = " << t);
            REQUIRE_THAT(static_cast<double>(stepped[j]) / static_cast<double>(flat[j]),
                         WithinRel(gain, 1e-5));
            ++checked;
        }
        REQUIRE(checked > 1500);
    }
}

namespace
{
// 3 s of automation through an FX processor, as absolute sample times.
constexpr int64_t kAutomationFrames = 3 * 48000;

struct TimedEvent
{
    int64_t time;
    ParamSlot slot;
    bool isModulation;
    double value;
};

// 40 events on every slot over 3 s: base values across a range that keeps the render light, and
// every fourth event a modulation offset of up to an eighth of that range. Some events land
// exactly on grid points, and some share a sample with the previous slot's. Times and values
// come from gg::rng.
std::vector<TimedEvent> automation()
{
    struct Range
    {
        float lo, hi;
    };
    std::array<Range, kParamCount> ranges{};
    ranges[at(ParamSlot::Mix)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::Level)] = {-24.0f, 6.0f};
    ranges[at(ParamSlot::Position)] = {0.0f, 0.6f}; // up to 1.7 s behind the head
    ranges[at(ParamSlot::Size)] = {-7.0f, -2.0f};   // 8 to 250 ms
    ranges[at(ParamSlot::Density)] = {1.0f, 7.0f};  // 2 to 128 grains/s
    ranges[at(ParamSlot::Distribution)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::Pitch)] = {-24.0f, 24.0f};
    ranges[at(ParamSlot::Spray)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::Stereo)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::Freeze)] = {0.0f, 1.0f};

    constexpr int kPerSlot = 40;
    std::vector<TimedEvent> events;
    std::array<int64_t, kPerSlot> previousSlotTimes{};
    for (size_t s = 0; s < kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        const Range r = ranges[s];
        for (int k = 0; k < kPerSlot; ++k)
        {
            const uint64_t key = s * 1000 + static_cast<uint64_t>(k) * 4;
            auto time = static_cast<int64_t>(uniform(77, key) * kAutomationFrames);
            if (k % 10 == 3)
            {
                time -= time % 32; // on a grid point
            }
            if (k % 10 == 7 && s > 0)
            {
                time = previousSlotTimes[static_cast<size_t>(k)]; // the same sample
            }
            previousSlotTimes[static_cast<size_t>(k)] = time;
            const double u = uniform(77, key + 1);
            const bool isModulation = k % 4 == 1;
            double value = 0.0;
            if (isModulation)
            {
                value = (2.0 * u - 1.0) * (r.hi - r.lo) / 8.0;
            }
            else if (slot == ParamSlot::Freeze)
            {
                value = u < 0.5 ? 0.0 : 1.0;
            }
            else
            {
                value = r.lo + (r.hi - r.lo) * u;
            }
            events.push_back({time, slot, isModulation, value});
        }
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const TimedEvent &a, const TimedEvent &b) { return a.time < b.time; });
    return events;
}

// Block sizes drawn from gg::rng, enough to cover kAutomationFrames: a tenth of them 0-frame
// calls, three in ten multiples of 32 (so calls often end on a grid boundary), the rest anything
// in 1..4096.
std::vector<uint32_t> randomSplit(uint64_t seed)
{
    std::vector<uint32_t> sizes;
    int64_t total = 0;
    for (uint64_t i = 0; total < kAutomationFrames; ++i)
    {
        const double kind = uniform(seed, 2 * i);
        const double u = uniform(seed, 2 * i + 1);
        const uint32_t n = kind < 0.1   ? 0
                           : kind < 0.4 ? 32 * (1 + static_cast<uint32_t>(u * 128))
                                        : 1 + static_cast<uint32_t>(u * 4096);
        sizes.push_back(n);
        total += n;
    }
    return sizes;
}

struct AutomationRender
{
    Stereo audio;
    GrainLog log;
    int64_t writeHead{0};
};

// Renders the whole input through a fresh FX processor in process() calls of blockSizes in turn
// (cycling), each call given the events that fall inside it. A 0-frame call at sample `done`
// takes the events stamped `done`, at offset 0.
void renderAutomation(const Stereo &input, const std::vector<TimedEvent> &events, uint32_t maxBlock,
                      const std::vector<uint32_t> &blockSizes, AutomationRender &r)
{
    const auto frames = static_cast<int64_t>(input.left.size());
    Processor p({kSampleRate, maxBlock, 42, Mode::Fx});
    p.setEventSink(&GrainLog::record, &r.log);
    r.audio.left.assign(static_cast<std::size_t>(frames), kNaN);
    r.audio.right.assign(static_cast<std::size_t>(frames), kNaN);
    std::vector<ParamEvent> blockEvents;
    blockEvents.reserve(events.size());
    std::size_t nextEvent = 0;
    int64_t done = 0;
    for (std::size_t next = 0; done < frames; ++next)
    {
        const uint32_t want = blockSizes[next % blockSizes.size()];
        const auto n = static_cast<uint32_t>(std::min<int64_t>(want, frames - done));
        const int64_t limit = done + std::max<int64_t>(n, 1);
        blockEvents.clear();
        for (; nextEvent < events.size() && events[nextEvent].time < limit; ++nextEvent)
        {
            const TimedEvent &e = events[nextEvent];
            blockEvents.push_back(
                {static_cast<uint32_t>(e.time - done), e.slot, e.isModulation, e.value});
        }
        const auto j = static_cast<std::size_t>(done);
        const float *in[2] = {input.left.data() + j, input.right.data() + j};
        float *out[2] = {r.audio.left.data() + j, r.audio.right.data() + j};
        p.process(in, 2, out, 2, n, blockEvents);
        done += n;
    }
    r.writeHead = p.engine().liveWriteHead();
}
} // namespace

TEST_CASE("processor is block-size invariant with automation", "[core][determinism]")
{
    const Stereo input = noise(kAutomationFrames, 0.5f, 21);
    const auto events = automation();
    REQUIRE(events.size() == 40 * kParamCount);

    AutomationRender ref;
    renderAutomation(input, events, 4096, {4096}, ref);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(peak(ref.audio.left) > 0.05f);
    REQUIRE_FALSE(sameBits(ref.audio.left, ref.audio.right));
    REQUIRE_FALSE(ref.log.overflowed);
    // Guard against a vacuous pass: grains spawned in every second, and Freeze stopped the head
    // for a while.
    for (int64_t s = 0; s < 3; ++s)
    {
        INFO("second " << s);
        REQUIRE(std::count_if(
                    ref.log.grains.begin(), ref.log.grains.end(), [&](const gg::GrainSpec &g) {
                        return g.onsetSample >= s * 48000 && g.onsetSample < (s + 1) * 48000;
                    }) > 5);
    }
    REQUIRE(ref.writeHead < kAutomationFrames - 4800);

    struct Split
    {
        uint32_t maxBlock;
        std::vector<uint32_t> blockSizes;
    };
    std::vector<Split> splits{{4096, {1}},   {4096, {7}},    {4096, {64}},
                              {4096, {511}}, {4096, {4096}}, {64, {4096}}};
    for (const uint64_t seed : {99, 100, 101})
    {
        auto random = randomSplit(seed);
        REQUIRE(random.size() > 2);
        REQUIRE(std::count(random.begin(), random.end(), 0u) > 0); // 0-frame calls
        splits.push_back({4096, std::move(random)});
    }
    for (const Split &split : splits)
    {
        INFO("maxBlockSize = " << split.maxBlock << ", first block size = "
                               << split.blockSizes.front() << " of " << split.blockSizes.size());
        AutomationRender r;
        renderAutomation(input, events, split.maxBlock, split.blockSizes, r);
        REQUIRE_FALSE(r.log.overflowed);
        REQUIRE(r.log.grains == ref.log.grains);
        REQUIRE(r.writeHead == ref.writeHead);
        REQUIRE(sameBits(ref.audio.left, r.audio.left));
        REQUIRE(sameBits(ref.audio.right, r.audio.right));
    }
}

TEST_CASE("events for one boundary apply as one group across a call's end", "[core][determinism]")
{
    // Review finding I1. A call ends on the grid boundary B = 20480; events for B arrive both
    // at its end (sample 20470) and at the next call's offset 0 (sample 20480). Applied as one
    // group, as a single call applies them, the later events return Mix and Level to the
    // targets of ramps still in flight, which therefore continue. Applied as two groups, the
    // second would restart both ramps from B. Default parameters: grains read 0.25 s back, so
    // the wet signal, and with it Level, sounds from about sample 12000.
    constexpr int64_t kFrames = 48 * 512;
    constexpr int64_t kB = 40 * 512;
    const Stereo input = noise(kFrames, 0.25f, 17);
    const std::vector<TimedEvent> events{
        {kB - 440, ParamSlot::Mix, false, 1.0}, // boundary kB - 416: ramps until kB + 64
        {kB - 440, ParamSlot::Level, false, -12.0},
        {kB - 10, ParamSlot::Mix, false, 0.2}, // boundary kB
        {kB - 10, ParamSlot::Level, false, 6.0},
        {kB, ParamSlot::Mix, false, 1.0}, // boundary kB: back to the ramps' targets
        {kB, ParamSlot::Level, false, -12.0},
    };

    AutomationRender ref;
    renderAutomation(input, events, 4096, {static_cast<uint32_t>(kFrames)}, ref);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(peak(ref.audio.left) > 0.05f);

    struct Split
    {
        const char *name;
        std::vector<uint32_t> blockSizes;
    };
    for (const Split &split :
         {Split{"32-frame calls", {32}}, Split{"512-frame calls", {512}},
          Split{"a 0-frame call at B with the offset-0 events",
                {static_cast<uint32_t>(kB), 0, static_cast<uint32_t>(kFrames - kB)}},
          Split{"7-frame calls (B inside a call)", {7}}})
    {
        INFO(split.name);
        AutomationRender r;
        renderAutomation(input, events, 4096, split.blockSizes, r);
        CHECK(r.log.grains == ref.log.grains);
        CHECK(sameBits(ref.audio.left, r.audio.left));
        CHECK(sameBits(ref.audio.right, r.audio.right));
    }
}

TEST_CASE("the wet chain is Level, then the soft limiter, then the mix", "[core]")
{
    // Spec §6.4: out = dry × (1 − m) + softLimit(wet × Level) × m. Three renders of one input
    // share the engine's wet signal w, which Level and Mix don't touch:
    // - Level 0 dB, Mix 1: w itself, kept below the knee so the limiter passes it unchanged;
    // - Level +12 dB, Mix 1: softLimit(w × g) exactly, with the limiter engaged;
    // - Level +12 dB, Mix 0.5: dry × 0.5 + softLimit(w × g) × 0.5 exactly, and 0.5 ×
    //   softLimit(w × g) once the input is silent (the grains still read the earlier input).
    constexpr uint32_t kFrames = 8192;
    constexpr uint32_t kSilentFrom = 4096;
    Stereo input = noise(kFrames, 0.45f, 23);
    std::fill(input.left.begin() + kSilentFrom, input.left.end(), 0.0f);
    std::fill(input.right.begin() + kSilentFrom, input.right.end(), 0.0f);
    const auto render = [&](float levelDb, float mix) {
        Processor p({kSampleRate, 4096, 1, Mode::Fx});
        p.setBase(ParamSlot::Position, 0.02f); // about 7 ms behind the head
        p.setBase(ParamSlot::Density, 7.0f);
        p.setBase(ParamSlot::Level, levelDb);
        p.setBase(ParamSlot::Mix, mix);
        Stereo out{std::vector<float>(kFrames, kNaN), std::vector<float>(kFrames, kNaN)};
        const float *in[2] = {input.left.data(), input.right.data()};
        float *o[2] = {out.left.data(), out.right.data()};
        p.process(in, 2, o, 2, kFrames, {});
        return out;
    };
    const Stereo w = render(0.0f, 1.0f);
    const Stereo limited = render(12.0f, 1.0f);
    const Stereo mixed = render(12.0f, 0.5f);

    gg::core::ParamValues plus12{};
    plus12[at(ParamSlot::Level)] = 12.0f;
    const float g = gg::core::toWetParams(plus12).levelGain;
    REQUIRE(peak(w.left) < 0.5f);
    REQUIRE(peak(w.right) < 0.5f);
    int engaged = 0;
    int silentAndSounding = 0;
    for (const auto &[dry, wet, lim, mix] :
         {std::tuple{&input.left, &w.left, &limited.left, &mixed.left},
          std::tuple{&input.right, &w.right, &limited.right, &mixed.right}})
    {
        for (std::size_t i = 0; i < kFrames; ++i)
        {
            const float expectedLimited = gg::core::softLimit((*wet)[i] * g);
            const float expectedMixed = (*dry)[i] * (1.0f - 0.5f) + expectedLimited * 0.5f;
            if ((*lim)[i] != expectedLimited || (*mix)[i] != expectedMixed)
            {
                INFO("i = " << i);
                REQUIRE((*lim)[i] == expectedLimited);
                REQUIRE((*mix)[i] == expectedMixed);
            }
            engaged += std::abs((*wet)[i] * g) > 0.5f ? 1 : 0;
            if (i >= kSilentFrom && (*lim)[i] != 0.0f)
            {
                ++silentAndSounding;
                if ((*mix)[i] != 0.5f * (*lim)[i])
                {
                    INFO("i = " << i);
                    REQUIRE((*mix)[i] == 0.5f * (*lim)[i]);
                }
            }
        }
    }
    // Guards against a vacuous pass: the limiter is engaged often, and the wet signal sounds
    // over silent input.
    REQUIRE(engaged > 1000);
    REQUIRE(silentAndSounding > 1000);
}

TEST_CASE("processor survives an automation storm", "[core][rt]")
{
    // Review Focus 1: a thousand parameter and modulation events in one 512-frame block, a
    // hundred of them at offset 0, many out of range (some infinite once converted to float).
    constexpr uint32_t kBlock = 512;
    Processor p({kSampleRate, kBlock, 9, Mode::Fx});
    const Stereo input = noise(8 * kBlock, 0.5f, 5);
    std::vector<float> left(8 * kBlock, kNaN), right(8 * kBlock, kNaN);
    const auto block = [&](uint32_t i) {
        const std::size_t j = static_cast<std::size_t>(i) * kBlock;
        struct Pointers
        {
            const float *in[2];
            float *out[2];
        };
        return Pointers{{input.left.data() + j, input.right.data() + j},
                        {left.data() + j, right.data() + j}};
    };
    for (uint32_t i = 0; i < 6; ++i) // warm up until grains are sounding
    {
        auto ptr = block(i);
        p.process(ptr.in, 2, ptr.out, 2, kBlock, {});
    }

    std::vector<ParamEvent> storm;
    for (uint64_t k = 0; k < 1000; ++k)
    {
        const auto offset =
            k < 100 ? 0u : static_cast<uint32_t>(uniform(31, 4 * k) * kBlock); // 0 .. 511
        const auto slot = static_cast<ParamSlot>(
            static_cast<size_t>(uniform(31, 4 * k + 1) * kParamCount) % kParamCount);
        const bool isModulation = uniform(31, 4 * k + 2) < 0.3;
        const auto &meta = gg::core::paramMeta(slot);
        const double span = static_cast<double>(meta.maxVal) - static_cast<double>(meta.minVal);
        const double u = uniform(31, 4 * k + 3);
        double value = isModulation ? (2.0 * u - 1.0) * span
                                    : static_cast<double>(meta.minVal) + (1.5 * u - 0.25) * span;
        if (k % 97 == 13)
        {
            value = (k % 2 == 0 ? 1.0 : -1.0) * 1e300; // ±inf as a float
        }
        storm.push_back({offset, slot, isModulation, value});
    }
    std::stable_sort(storm.begin(), storm.end(),
                     [](const ParamEvent &a, const ParamEvent &b) { return a.offset < b.offset; });

    // The last event per slot wins, base and modulation separately.
    std::array<float, kParamCount> lastBase{};
    std::array<float, kParamCount> lastMod{};
    for (size_t s = 0; s < kParamCount; ++s)
    {
        lastBase[s] = p.base(static_cast<ParamSlot>(s));
    }
    for (const ParamEvent &e : storm)
    {
        const auto v = static_cast<float>(e.value);
        (e.isModulation ? lastMod : lastBase)[at(e.slot)] = e.isModulation ? v : clampTo(e.slot, v);
    }

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        auto storming = block(6);
        p.process(storming.in, 2, storming.out, 2, kBlock, storm);
        // The last boundary is the block's end: render past it.
        auto after = block(7);
        p.process(after.in, 2, after.out, 2, kBlock, {});
    }
    const uint64_t allocations = AllocTripwire::count();
    REQUIRE(allocations == 0);

    REQUIRE(allFinite(left));
    REQUIRE(allFinite(right));
    // Mix is a crossfade and the wet signal is limited below 1: the output is never louder than
    // the input plus 1.
    REQUIRE(peak(left) < peak(input.left) + 1.0f);
    REQUIRE(peak(right) < peak(input.right) + 1.0f);
    for (size_t s = 0; s < kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        INFO("slot " << s);
        REQUIRE(p.base(slot) == lastBase[s]);
        REQUIRE(p.effective(slot) == clampTo(slot, lastBase[s] + lastMod[s]));
    }
}

namespace
{
// Counts spawns without allocating: the sink runs inside process().
struct SpawnCounter
{
    uint64_t spawns{0};

    static void record(void *ctx, const gg::GrainSpec &)
    {
        ++static_cast<SpawnCounter *>(ctx)->spawns;
    }
};
} // namespace

TEST_CASE("processor does not allocate", "[core][rt]")
{
    // A fresh processor's first process() call is armed: paramMeta builds its table on its first
    // call, which allocates, so the constructor must have made that call (ruling M1-q). Nothing
    // here calls paramMeta before the constructor does. maxBlockSize 256 makes the 1000-frame
    // calls oversized; mono, stereo, missing and four-channel buffers, 0- and 1-frame calls, and
    // events on every slot (Freeze and modulation included) all run armed.
    Processor p({kSampleRate, 256, 3, Mode::Fx});
    SpawnCounter counter;
    p.setEventSink(&SpawnCounter::record, &counter);

    constexpr uint32_t kFrames = 1000;
    std::vector<float> inLeft(kFrames), inRight(kFrames);
    for (std::size_t i = 0; i < kFrames; ++i)
    {
        inLeft[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * i / 48000.0));
        inRight[i] = -inLeft[i];
    }
    const float *in[2] = {inLeft.data(), inRight.data()};
    std::vector<float> o0(kFrames), o1(kFrames), o2(kFrames), o3(kFrames);
    float *out[4] = {o0.data(), o1.data(), o2.data(), o3.data()};

    // Events on every slot across a 1000-frame call: 256 grains/s of 62 ms, Freeze on and
    // off, and a modulation event on every slot.
    std::vector<ParamEvent> events;
    for (size_t s = 0; s < kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        const auto offset = static_cast<uint32_t>(s * 97);
        const double value =
            slot == ParamSlot::Density ? 8.0 : (slot == ParamSlot::Size ? -4.0 : 0.4);
        events.push_back({offset, slot, false, value});
        events.push_back({offset + 3, slot, true, 0.1});
    }
    events.push_back({990, ParamSlot::Freeze, false, 1.0});
    std::stable_sort(events.begin(), events.end(),
                     [](const ParamEvent &a, const ParamEvent &b) { return a.offset < b.offset; });
    const std::array<ParamEvent, 3> atStart{{{0, ParamSlot::Mix, false, 0.7},
                                             {0, ParamSlot::Freeze, false, 0.0},
                                             {0, ParamSlot::Level, true, -3.0}}};

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        p.process(in, 2, out, 2, kFrames, events); // the first call
        for (int i = 0; i < 20; ++i)
        {
            p.process(in, 2, out, 2, 0, atStart);
            p.process(in, 2, out, 2, 1, atStart);
            p.process(in, 1, out, 2, kFrames, events);
            p.process(nullptr, 0, out, 2, 100, atStart);
            p.process(in, 2, nullptr, 0, 100, atStart);
            p.process(in, 2, out, 4, kFrames, events);
            p.process(in, 2, out, 1, 100, atStart);
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    REQUIRE(counter.spawns > 0);
    REQUIRE(p.engine().activeGrains() > 0);
    REQUIRE(allocations == 0);
}

TEST_CASE("processor flushes denormals", "[core][denormal]")
{
    // The whole process() call, the dry/wet sum included, runs with denormals flushed (spec
    // §6.4), and the caller's floating-point mode comes back afterwards. Input of subnormals,
    // and of normals just above FLT_MIN that the Mix crossfade scales into the subnormal range.
    const auto flushing = [] {
        volatile float t = 1e-38f;
        volatile float r = t * 1e-3f;
        return r == 0.0f;
    };
    REQUIRE_FALSE(flushing());

    constexpr uint32_t kFrames = 4800;
    constexpr float kMin = std::numeric_limits<float>::min();
    std::vector<float> inLeft(kFrames), inRight(kFrames);
    for (std::size_t i = 0; i < kFrames; ++i)
    {
        const float sign = i % 2 == 0 ? 1.0f : -1.0f;
        inLeft[i] = sign * (i % 3 == 0 ? 1e-40f : 1e-45f); // subnormal
        inRight[i] = sign * 1.5f * kMin;                   // normal; × 0.5 or less is subnormal
    }
    REQUIRE(std::fpclassify(inLeft[0]) == FP_SUBNORMAL);
    const float *in[2] = {inLeft.data(), inRight.data()};

    for (const float mix : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        INFO("mix " << mix);
        Processor p({kSampleRate, 4096, 5, Mode::Fx});
        p.setBase(ParamSlot::Mix, mix);
        std::vector<float> left(kFrames, kNaN), right(kFrames, kNaN);
        float *out[2] = {left.data(), right.data()};
        p.process(in, 2, out, 2, kFrames, {});
        REQUIRE_FALSE(flushing()); // the caller's mode is back
        for (const auto *channel : {&left, &right})
        {
            for (const float x : *channel)
            {
                if (!(x == 0.0f || std::abs(x) >= kMin))
                {
                    INFO("x = " << x);
                    REQUIRE(std::fpclassify(x) != FP_SUBNORMAL);
                    REQUIRE(std::isfinite(x));
                }
            }
        }

        // Called with flushing already on, it stays on afterwards.
        {
            gg::ScopedDenormalsOff callerGuard;
            p.process(in, 2, out, 2, kFrames, {});
            REQUIRE(flushing());
        }
        REQUIRE_FALSE(flushing());
    }
}

TEST_CASE("processor output stays finite with non-finite input", "[core]")
{
    // Ruling M1-q: a non-finite dry sample counts as 0 and a finite one passes through
    // unchanged; the ring stores 0 for non-finite input (Review Focus 2). Bursts of NaN, +Inf
    // and -Inf, single samples and runs, at different places in each channel.
    constexpr uint32_t kFrames = 24000;
    Stereo input = noise(kFrames, 0.25f, 3);
    const auto burst = [](std::vector<float> &channel, std::size_t from, std::size_t to,
                          float value) {
        std::fill(channel.begin() + static_cast<std::ptrdiff_t>(from),
                  channel.begin() + static_cast<std::ptrdiff_t>(to), value);
    };
    burst(input.left, 3000, 3064, kNaN);
    burst(input.left, 5000, 5031, kInf);
    burst(input.left, 7001, 7002, -kInf);
    for (std::size_t i = 9000; i < 9500; i += 2)
    {
        input.left[i] = i % 4 == 0 ? kNaN : -kInf;
    }
    burst(input.right, 3032, 3200, -kInf);
    burst(input.right, 12000, 12001, kNaN);
    burst(input.right, 15000, 15500, kInf);
    for (std::size_t i = 9001; i < 9500; i += 3)
    {
        input.right[i] = kNaN;
    }

    const auto render = [&](float mix, std::span<const ParamEvent> events, uint32_t block) {
        Processor p({kSampleRate, 4096, 13, Mode::Fx});
        p.setBase(ParamSlot::Mix, mix);
        p.setBase(ParamSlot::Position, 0.0f);
        p.setBase(ParamSlot::Density, 7.0f);
        Stereo out{std::vector<float>(kFrames, kNaN), std::vector<float>(kFrames, kNaN)};
        std::vector<ParamEvent> blockEvents;
        blockEvents.reserve(events.size());
        std::size_t next = 0;
        for (uint32_t done = 0; done < kFrames; done += block)
        {
            const uint32_t n = std::min(block, kFrames - done);
            blockEvents.clear();
            for (; next < events.size() && events[next].offset < done + n; ++next)
            {
                ParamEvent e = events[next];
                e.offset -= done;
                blockEvents.push_back(e);
            }
            const float *in[2] = {input.left.data() + done, input.right.data() + done};
            float *o[2] = {out.left.data() + done, out.right.data() + done};
            p.process(in, 2, o, 2, n, blockEvents);
        }
        return out;
    };

    for (const float mix : {0.0f, 0.5f, 1.0f})
    {
        INFO("mix " << mix);
        const Stereo out = render(mix, {}, 256);
        REQUIRE(allFinite(out.left));
        REQUIRE(allFinite(out.right));
        if (mix == 1.0f)
        {
            REQUIRE(peak(out.left) > 0.01f); // the wet signal sounds
        }
        if (mix == 0.0f)
        {
            // Finite input passes through bit for bit; non-finite input is silence.
            for (const auto &[x, y] :
                 {std::pair{&input.left, &out.left}, std::pair{&input.right, &out.right}})
            {
                for (std::size_t i = 0; i < kFrames; ++i)
                {
                    const float expected = std::isfinite((*x)[i]) ? (*x)[i] : 0.0f;
                    if (std::memcmp(&expected, &(*y)[i], sizeof(float)) != 0)
                    {
                        INFO("i = " << i << ": " << (*x)[i] << " gave " << (*y)[i]);
                        REQUIRE((*y)[i] == expected);
                    }
                }
            }
        }
    }

    // Mix swept from 0 to 1 and back across the bursts, an event every 160 samples, so the ramps
    // run through them.
    std::vector<ParamEvent> sweep;
    for (uint32_t t = 0; t < kFrames; t += 160)
    {
        const double phase = static_cast<double>(t) / kFrames;
        sweep.push_back({t, ParamSlot::Mix, false, 1.0 - std::abs(2.0 * phase - 1.0)});
    }
    const Stereo swept = render(0.0f, sweep, 100);
    REQUIRE(allFinite(swept.left));
    REQUIRE(allFinite(swept.right));
}

TEST_CASE("processor configures the engine for its mode", "[core]")
{
    std::vector<float> o0(512, kNaN), o1(512, kNaN), o2(512, kNaN), o3(512, kNaN);
    float *out[4] = {o0.data(), o1.data(), o2.data(), o3.data()};
    const Stereo input = noise(512, 0.5f, 8);
    const float *in[2] = {input.left.data(), input.right.data()};

    SECTION("FX")
    {
        // The 16 s live ring, read in Live mode; channels beyond 2 are zeroed.
        Processor p({kSampleRate, 512, 1, Mode::Fx});
        REQUIRE(p.engine().liveFrames() == 16 * 48000);
        p.process(in, 2, out, 4, 512, {});
        REQUIRE(p.engine().sourceMode() == gg::SourceMode::Live);
        REQUIRE(p.engine().sampleClock() == 512);
        REQUIRE(allFinite(o0));
        REQUIRE(allFinite(o1));
        REQUIRE(peak(o0) > 0.1f); // the dry half of the default Mix
        REQUIRE(peak(o2) == 0.0f);
        REQUIRE(peak(o3) == 0.0f);
    }
    SECTION("instrument")
    {
        // No ring and no source in this slice: the wet signal alone, which is silence, whatever
        // the input.
        Processor p({kSampleRate, 512, 1, Mode::Instrument});
        REQUIRE(p.engine().liveFrames() == 0);
        p.process(in, 2, out, 4, 512, {});
        REQUIRE(p.engine().sourceMode() == gg::SourceMode::Sample);
        REQUIRE(p.engine().sampleClock() == 512);
        for (const auto *channel : {&o0, &o1, &o2, &o3})
        {
            REQUIRE(allFinite(*channel));
            REQUIRE(peak(*channel) == 0.0f);
        }
    }
}

TEST_CASE("processor base values, pending changes and reset", "[core]")
{
    Processor p({kSampleRate, 4096, 1, Mode::Fx});
    for (size_t s = 0; s < kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        INFO("slot " << s);
        REQUIRE(p.base(slot) == gg::core::paramMeta(slot).defaultVal);
        REQUIRE(p.effective(slot) == p.base(slot));
    }

    // setBase clamps to the range and ignores NaN.
    p.setBase(ParamSlot::Mix, 2.0f);
    REQUIRE(p.base(ParamSlot::Mix) == 1.0f);
    p.setBase(ParamSlot::Level, -kInf);
    REQUIRE(p.base(ParamSlot::Level) == -60.0f);
    p.setBase(ParamSlot::Mix, kNaN);
    REQUIRE(p.base(ParamSlot::Mix) == 1.0f);

    std::vector<float> left(64), right(64);
    float *out[2] = {left.data(), right.data()};

    // An event is pending until its boundary (32, past this 10-frame call); a NaN event is
    // ignored.
    const std::array<ParamEvent, 2> events{{{3, ParamSlot::Density, false, 6.0},
                                            {4, ParamSlot::Mix, false, static_cast<double>(kNaN)}}};
    p.process(nullptr, 0, out, 2, 10, events);
    REQUIRE(p.base(ParamSlot::Density) == 4.0f);
    // reset() applies it at once and restarts the clock.
    p.reset();
    REQUIRE(p.base(ParamSlot::Density) == 6.0f);
    REQUIRE(p.effective(ParamSlot::Density) == 6.0f);
    REQUIRE(p.base(ParamSlot::Mix) == 1.0f);
    REQUIRE(p.engine().sampleClock() == 0);

    // A pending event reaches its boundary in a later call.
    const ParamEvent density5{1, ParamSlot::Density, false, 5.0};
    p.process(nullptr, 0, out, 2, 10, std::span(&density5, 1));
    REQUIRE(p.base(ParamSlot::Density) == 6.0f);
    p.process(nullptr, 0, out, 2, 64, {});
    REQUIRE(p.base(ParamSlot::Density) == 5.0f);

    // setBase after an event, before its boundary, wins: the latest value.
    const ParamEvent density7{1, ParamSlot::Density, false, 7.0};
    p.process(nullptr, 0, out, 2, 10, std::span(&density7, 1)); // boundary 96
    p.setBase(ParamSlot::Density, 3.0f);
    REQUIRE(p.base(ParamSlot::Density) == 3.0f);
    p.process(nullptr, 0, out, 2, 64, {});
    REQUIRE(p.base(ParamSlot::Density) == 3.0f);

    // Modulation adds to the base, and the sum is clamped.
    const std::array<ParamEvent, 2> modulated{
        {{0, ParamSlot::Mix, false, 0.75}, {0, ParamSlot::Mix, true, 0.5}}};
    p.process(nullptr, 0, out, 2, 64, modulated);
    REQUIRE(p.base(ParamSlot::Mix) == 0.75f);
    REQUIRE(p.effective(ParamSlot::Mix) == 1.0f);
}
