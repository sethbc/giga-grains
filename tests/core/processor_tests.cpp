// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The Processor (spec §6.1, §6.4): parameter events on the 32-sample control grid, the Mix and
// Level ramps, the wet chain (× Level → soft limiter → Mix with dry), and its real-time,
// denormal and determinism contracts.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/LayersFixture.h"
#include "gg/core/DenormalGuard.h"
#include "gg/core/ModMatrix.h"
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
    const auto &meta = gg::core::paramMeta(s, Mode::Fx);
    return std::clamp(v, meta.minVal, meta.maxVal);
}

// Records spawned grains, with their layers, into preallocated storage: the sink runs inside
// process(). With more than one layer on, the order across layers follows the process() calls
// (each call renders layer A's piece, then B's, ...), so only each layer's own sequence is
// block-size invariant (layer()).
struct GrainLog
{
    static constexpr std::size_t kCapacity = 16384;
    std::vector<gg::GrainSpec> grains;
    std::vector<uint32_t> layers; // grains[i]'s layer
    bool overflowed{false};

    GrainLog()
    {
        grains.reserve(kCapacity);
        layers.reserve(kCapacity);
    }

    static void record(void *ctx, uint32_t layer, const gg::GrainSpec &g)
    {
        auto *log = static_cast<GrainLog *>(ctx);
        if (log->grains.size() < kCapacity)
        {
            log->grains.push_back(g);
            log->layers.push_back(layer);
        }
        else
        {
            log->overflowed = true;
        }
    }

    // Layer l's grains, in the order the sink saw them.
    std::vector<gg::GrainSpec> layer(uint32_t l) const
    {
        std::vector<gg::GrainSpec> out;
        for (std::size_t i = 0; i < grains.size(); ++i)
        {
            if (layers[i] == l)
            {
                out.push_back(grains[i]);
            }
        }
        return out;
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
                       gg::core::paramMeta(ParamSlot::Density, Mode::Fx).maxVal};
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
    maxDensity[at(ParamSlot::Density)] = gg::core::paramMeta(ParamSlot::Density, Mode::Fx).maxVal;
    const double newP = 8000.0 / gg::core::toCloudParams(maxDensity, Mode::Fx).densityHz;
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
        const double target = gg::core::toWetParams(minLevel, Mode::Fx).levelGain;
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

// The 28 M1 slots, which automation() covers. The other 190 (M2 sub-project 1's spec §6.9) are
// covered by everySlotAutomation(), with the layers they act on switched on.
constexpr size_t kM1Slots = static_cast<size_t>(ParamSlot::Source);
static_assert(kM1Slots == 28);

// 40 events on every M1 slot over 3 s: base values across a range that keeps the render light,
// and every fourth event a modulation offset of up to an eighth of that range. Every M1 slot has
// a range: Space's, Texture, Feedback and the macros (which move the cloud and Space through
// their recipes) are automated too. Some events land exactly on grid points, and some share a
// sample with the previous slot's. Times and values come from gg::rng.
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
    ranges[at(ParamSlot::Diffusion)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::ReverbAmount)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::ReverbDecay)] = {-2.0f, 4.0f}; // 0.25 to 16 s, never infinite
    ranges[at(ParamSlot::ReverbSize)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::ReverbDamping)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::ReverbPredelay)] = {0.0f, 0.5f};
    ranges[at(ParamSlot::ReverbModulation)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::DryToReverb)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::Texture)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::Feedback)] = {0.0f, 0.5f};
    ranges[at(ParamSlot::Macro1)] = {0.25f, 0.75f}; // Size ±1.5 octaves
    ranges[at(ParamSlot::Macro2)] = {0.3f, 0.6f};   // Density −1.6 to +0.8 octaves
    ranges[at(ParamSlot::Macro3)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::Macro4)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::Macro5)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::Macro6)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::Macro7)] = {0.0f, 1.0f};
    ranges[at(ParamSlot::Macro8)] = {0.0f, 1.0f};
    for (size_t s = 0; s < kM1Slots; ++s)
    {
        REQUIRE(ranges[s].hi > ranges[s].lo); // every slot covered
    }

    constexpr int kPerSlot = 40;
    std::vector<TimedEvent> events;
    std::array<int64_t, kPerSlot> previousSlotTimes{};
    for (size_t s = 0; s < kM1Slots; ++s)
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
            else if (slot == ParamSlot::Freeze || slot == ParamSlot::Macro6)
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
// calls, three in ten multiples of 32, the rest anything in 1..4096. A multiple of 32 ends on
// the grid only when the calls before it did, so few calls end on a grid boundary (1 to 3 of
// about 70); the dedicated test "events for one boundary apply as one group across a call's
// end" covers that case.
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
    // The live ring's frames 0 .. writeHead − 1 at the end, as stored; empty if some have left
    // its window (a render longer than the ring).
    Stereo ring;
};

// The ring's frames 0 .. head − 1, read from its storage as stored (the sign of zero included),
// or nothing once frame 0 has left the window.
Stereo ringFrames(const gg::LiveSource &live, int64_t head)
{
    const gg::LiveSource::RingView v = live.view();
    if (head > v.frames)
    {
        return {};
    }
    Stereo s{std::vector<float>(static_cast<std::size_t>(head)),
             std::vector<float>(static_cast<std::size_t>(head))};
    for (int64_t f = 0; f < head; ++f)
    {
        const auto slot = static_cast<std::size_t>(f % v.capacity);
        const auto j = static_cast<std::size_t>(f);
        s.left[j] = v.slots[2 * slot];
        s.right[j] = v.slots[2 * slot + 1];
    }
    return s;
}

// Renders the whole input through a fresh FX processor in process() calls of blockSizes in turn
// (cycling), each call given the events, notes and MIDI controls that fall inside it. A 0-frame
// call at sample `done` takes those stamped `done`, at offset 0. `setup`, if given, runs on the
// processor before the first call, after the event sink is set.
void renderAutomation(const Stereo &input, const std::vector<TimedEvent> &events, uint32_t maxBlock,
                      const std::vector<uint32_t> &blockSizes, AutomationRender &r,
                      const std::function<void(Processor &)> &setup = {},
                      const std::vector<gg::test::layers::TimedNote> &notes = {},
                      const std::vector<gg::test::layers::TimedControl> &controls = {})
{
    const auto frames = static_cast<int64_t>(input.left.size());
    Processor p({kSampleRate, maxBlock, 42, Mode::Fx});
    p.setEventSink(&GrainLog::record, &r.log);
    if (setup)
    {
        setup(p);
    }
    r.audio.left.assign(static_cast<std::size_t>(frames), kNaN);
    r.audio.right.assign(static_cast<std::size_t>(frames), kNaN);
    std::vector<ParamEvent> blockEvents;
    blockEvents.reserve(events.size());
    std::vector<gg::core::NoteEvent> blockNotes;
    blockNotes.reserve(notes.size());
    std::vector<gg::core::ControlEvent> blockControls;
    blockControls.reserve(controls.size());
    std::size_t nextEvent = 0;
    std::size_t nextNote = 0;
    std::size_t nextControl = 0;
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
        blockNotes.clear();
        for (; nextNote < notes.size() && notes[nextNote].time < limit; ++nextNote)
        {
            const auto &e = notes[nextNote];
            blockNotes.push_back(
                {static_cast<uint32_t>(e.time - done), e.type, e.channel, e.key, e.velocity});
        }
        blockControls.clear();
        for (; nextControl < controls.size() && controls[nextControl].time < limit; ++nextControl)
        {
            const auto &c = controls[nextControl];
            blockControls.push_back(
                {static_cast<uint32_t>(c.time - done), c.kind, c.channel, c.value});
        }
        const auto j = static_cast<std::size_t>(done);
        const float *in[2] = {input.left.data() + j, input.right.data() + j};
        float *out[2] = {r.audio.left.data() + j, r.audio.right.data() + j};
        p.process(in, 2, out, 2, n, blockEvents, blockNotes, blockControls);
        done += n;
    }
    r.writeHead = p.engine().liveWriteHead();
    r.ring = ringFrames(p.engine().live(), r.writeHead);
}
} // namespace

TEST_CASE("processor is block-size invariant with automation", "[core][determinism]")
{
    const Stereo input = noise(kAutomationFrames, 0.5f, 21);
    const auto events = automation();
    REQUIRE(events.size() == 40 * kM1Slots);

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

namespace
{
// automation(), with Texture, Feedback and the eight macros automated more densely on top of it
// (spec §9): 30 more events each, none on a grid point, every third a modulation offset of up
// to a quarter of the range, Feedback up to 0.9 (Macro Feedback can add 0.9 more, to the 0.95
// clamp). Times and values come from gg::rng, keyed apart from automation()'s.
std::vector<TimedEvent> performAutomation()
{
    struct Perform
    {
        ParamSlot slot;
        float lo, hi;
    };
    const std::array<Perform, 10> perform{{{ParamSlot::Texture, 0.0f, 1.0f},
                                           {ParamSlot::Feedback, 0.0f, 0.9f},
                                           {ParamSlot::Macro1, 0.25f, 0.75f},
                                           {ParamSlot::Macro2, 0.3f, 0.6f},
                                           {ParamSlot::Macro3, 0.0f, 1.0f},
                                           {ParamSlot::Macro4, 0.0f, 1.0f},
                                           {ParamSlot::Macro5, 0.0f, 1.0f},
                                           {ParamSlot::Macro6, 0.0f, 1.0f},
                                           {ParamSlot::Macro7, 0.0f, 1.0f},
                                           {ParamSlot::Macro8, 0.0f, 1.0f}}};
    std::vector<TimedEvent> events = automation();
    for (std::size_t m = 0; m < perform.size(); ++m)
    {
        const Perform &r = perform[m];
        for (int k = 0; k < 30; ++k)
        {
            const uint64_t key = 500000 + m * 1000 + static_cast<uint64_t>(k) * 4;
            auto time = static_cast<int64_t>(uniform(78, key) * kAutomationFrames);
            if (time % 32 == 0)
            {
                time += 1 + k % 31; // off the grid
            }
            time = std::min(time, kAutomationFrames - 1); // 143999, off the grid too
            const double u = uniform(78, key + 1);
            const bool isModulation = k % 3 == 2;
            double value = 0.0;
            if (isModulation)
            {
                value = (2.0 * u - 1.0) * (r.hi - r.lo) / 4.0;
            }
            else if (r.slot == ParamSlot::Macro6)
            {
                value = u < 0.5 ? 0.0 : 1.0;
            }
            else
            {
                value = r.lo + (r.hi - r.lo) * u;
            }
            events.push_back({time, r.slot, isModulation, value});
        }
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const TimedEvent &a, const TimedEvent &b) { return a.time < b.time; });
    return events;
}

// performAutomation(), with every slot past the 28 M1 ones automated too (M2 sub-project 1's spec
// §6.9; M2 sub-project 2's spec §6.3; M2 sub-project 3's Quality and Budget): layer A's 43 new
// parameters, layers B to D's 54 each, the nine layer globals and Budget, 10 events each across
// the slot's whole range in the FX's paramMeta, none on a grid point, every third a modulation
// offset of up to a quarter of the range, except on a stepped parameter (Freeze, Source, the LFO
// shapes, Step Length, Loop, Splice, Quality, the Layer On switches, Budget), whose values are
// whole. The Layer On
// switches are on three times in four, so layers go off for longer than the ramp and come back.
// Times and values come from gg::rng, keyed apart from the others'.
std::vector<TimedEvent> everySlotAutomation()
{
    constexpr int kPerSlot = 10;
    std::vector<TimedEvent> events = performAutomation();
    for (std::size_t s = kM1Slots; s < kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        const auto &meta = gg::core::paramMeta(slot, Mode::Fx);
        const double lo = meta.minVal;
        const double hi = meta.maxVal;
        REQUIRE(hi > lo);
        const bool stepped = meta.type != sst::basic_blocks::params::ParamMetaData::FLOAT;
        const auto layer = gg::core::perLayerGlobalLayer(slot);
        const bool layerOn = layer && slot == gg::core::layerOnSlot(*layer);
        for (int k = 0; k < kPerSlot; ++k)
        {
            const uint64_t key = 900000 + s * 1000 + static_cast<uint64_t>(k) * 4;
            auto time = static_cast<int64_t>(uniform(79, key) * kAutomationFrames);
            if (time % 32 == 0)
            {
                time += 1 + k % 31; // off the grid
            }
            time = std::min(time, kAutomationFrames - 1);
            const double u = uniform(79, key + 1);
            const bool isModulation = !stepped && k % 3 == 2;
            double value = 0.0;
            if (isModulation)
            {
                value = (2.0 * u - 1.0) * (hi - lo) / 4.0;
            }
            else if (layerOn)
            {
                value = u < 0.75 ? 1.0 : 0.0;
            }
            else if (stepped)
            {
                value = std::min(std::floor(lo + (hi - lo + 1.0) * u), hi);
            }
            else
            {
                value = lo + (hi - lo) * u;
            }
            events.push_back({time, slot, isModulation, value});
        }
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const TimedEvent &a, const TimedEvent &b) { return a.time < b.time; });
    return events;
}

// Layers B to D switched on, each layer given a sine sample for the times its Source is Sample, a
// route from every mod source (gg::test::layers::allSourceRoutes) and a playing transport.
void everyLayerSetup(Processor &p)
{
    const std::array<double, 4> hz{196.0, 293.66, 440.0, 659.25};
    for (uint8_t L = 0; L < gg::core::kLayers; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 1.0f);
        p.setSourceNow(L, gg::test::layers::sine(hz[L]));
    }
    p.setRoutes(gg::test::layers::allSourceRoutes(Mode::Fx));
    p.setTransport({true, 0.5, 132.0, true, 4});
}

// Two short notes with velocities (they freeze the rings for a while and gate the envelopes), and
// MIDI controls of every kind off the grid: the matrix's event-driven sources move.
std::vector<gg::test::layers::TimedNote> everyLayerNotes()
{
    using gg::core::NoteEventType;
    return {{30011, NoteEventType::On, 0, 64, 0.8f},
            {41017, NoteEventType::Off, 0, 64},
            {90007, NoteEventType::On, 1, 55, 0.3f},
            {99013, NoteEventType::Off, 1, 55}};
}

std::vector<gg::test::layers::TimedControl> everyLayerControls()
{
    std::vector<gg::test::layers::TimedControl> c;
    for (int k = 0; k < 24; ++k)
    {
        const auto key = static_cast<uint64_t>(3 * k);
        auto time = static_cast<int64_t>(uniform(80, key) * (kAutomationFrames - 1));
        if (time % 32 == 0)
        {
            time += 1; // off the grid
        }
        c.push_back({time, static_cast<gg::core::ControlKind>(k % 3),
                     static_cast<int16_t>(k % 2 == 0 ? -1 : 2),
                     static_cast<float>(uniform(80, key + 1))});
    }
    std::stable_sort(c.begin(), c.end(),
                     [](const auto &a, const auto &b) { return a.time < b.time; });
    return c;
}
} // namespace

TEST_CASE("the perform parameters are block-size invariant together", "[core][determinism]")
{
    // Every one of the 243 slots automated (automation() for the M1 slots, everySlotAutomation()
    // for the rest), Texture, Feedback and the macros densely, with modulation, layers B to D on,
    // a route from every mod source, a transport, notes and MIDI controls: the macro curves,
    // Texture's latched envelope, Feedback's ring write, the layers' switches, sources, spreads
    // and morph, and the matrix all render the same bits, grains, head and ring at any host block
    // size and maxBlockSize.
    const Stereo input = noise(kAutomationFrames, 0.5f, 31);
    const auto events = everySlotAutomation();
    REQUIRE(events.size() == 40 * kM1Slots + 300 + 10 * (kParamCount - kM1Slots));
    const auto notes = everyLayerNotes();
    const auto controls = everyLayerControls();
    const auto render = [&](const std::vector<TimedEvent> &ev, uint32_t maxBlock,
                            const std::vector<uint32_t> &blockSizes, AutomationRender &r) {
        renderAutomation(input, ev, maxBlock, blockSizes, r, everyLayerSetup, notes, controls);
    };

    AutomationRender ref;
    render(events, 4096, {4096}, ref);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(peak(ref.audio.left) > 0.05f);
    REQUIRE_FALSE(ref.log.overflowed);
    // Not vacuous: grains latched many Textures, Freeze stopped the head for a while, and Feedback
    // wrote into the ring (without its events, and Macro Feedback's, the ring differs).
    std::vector<double> textures;
    for (const gg::GrainSpec &g : ref.log.grains)
    {
        textures.push_back(g.texture);
    }
    std::sort(textures.begin(), textures.end());
    textures.erase(std::unique(textures.begin(), textures.end()), textures.end());
    REQUIRE(textures.size() > 10);
    for (uint8_t L = 0; L < gg::core::kLayers; ++L)
    {
        INFO("layer " << int{L});
        REQUIRE(ref.log.layer(L).size() > 3);
    }
    REQUIRE(ref.writeHead < kAutomationFrames - 4800);
    REQUIRE(ref.ring.left.size() == static_cast<std::size_t>(ref.writeHead));
    std::vector<TimedEvent> withoutFeedback;
    std::copy_if(events.begin(), events.end(), std::back_inserter(withoutFeedback),
                 [](const TimedEvent &e) {
                     return e.slot != ParamSlot::Feedback && e.slot != ParamSlot::Macro7;
                 });
    AutomationRender dry;
    render(withoutFeedback, 4096, {4096}, dry);
    REQUIRE(dry.writeHead == ref.writeHead);
    REQUIRE_FALSE(sameBits(dry.ring.left, ref.ring.left));
    // And the macro events change the output: without every macro slot's events (automation()'s
    // and the perform ones), the rest of the automation renders other bits.
    std::vector<TimedEvent> withoutMacros;
    std::copy_if(events.begin(), events.end(), std::back_inserter(withoutMacros),
                 [](const TimedEvent &e) { return !gg::core::isMacro(e.slot); });
    REQUIRE(withoutMacros.size() < events.size());
    AutomationRender plain;
    render(withoutMacros, 4096, {4096}, plain);
    REQUIRE_FALSE(sameBits(plain.audio.left, ref.audio.left));

    struct Split
    {
        uint32_t maxBlock;
        std::vector<uint32_t> blockSizes;
    };
    std::vector<Split> splits{{4096, {1}},    {4096, {7}},  {4096, {64}}, {4096, {511}},
                              {4096, {4096}}, {64, {4096}}, {1, {4096}},  {256, {4096}}};
    for (const uint64_t seed : {299, 300, 301})
    {
        auto random = randomSplit(seed);
        REQUIRE(std::count(random.begin(), random.end(), 0u) > 0); // 0-frame calls
        splits.push_back({4096, std::move(random)});
    }
    for (const Split &split : splits)
    {
        INFO("maxBlockSize = " << split.maxBlock << ", first block size = "
                               << split.blockSizes.front() << " of " << split.blockSizes.size());
        AutomationRender r;
        render(events, split.maxBlock, split.blockSizes, r);
        REQUIRE_FALSE(r.log.overflowed);
        for (uint32_t L = 0; L < gg::core::kLayers; ++L)
        {
            INFO("layer " << L);
            REQUIRE(r.log.layer(L) == ref.log.layer(L));
        }
        REQUIRE(r.writeHead == ref.writeHead);
        REQUIRE(sameBits(ref.audio.left, r.audio.left));
        REQUIRE(sameBits(ref.audio.right, r.audio.right));
        REQUIRE(sameBits(ref.ring.left, r.ring.left));
        REQUIRE(sameBits(ref.ring.right, r.ring.right));
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

namespace
{
// The peak of the samples [from, to) of v.
float peakIn(const std::vector<float> &v, int64_t from, int64_t to)
{
    return peak(std::vector<float>(v.begin() + from, v.begin() + to));
}
} // namespace

TEST_CASE("a Freeze that is on at activation or reset plays live", "[core]")
{
    // Review finding I1, the director's option (b), "live until toggled": the ring isn't saved
    // (an M2 non-goal), so a Freeze restored on would freeze a fresh ring's first 20 ms, usually
    // silence, and hold it. Freeze engages only on an off→on transition after an activation (the
    // constructor) or a reset(); until then the wet signal is live. Mix 1: the output is the wet
    // signal alone, which grains read 0.25 s behind the head.
    constexpr int64_t kFrames = 94 * 512; // about 1 s
    const Stereo input = noise(kFrames, 0.5f, 31);
    Processor p({kSampleRate, 512, 5, Mode::Fx});
    p.setBase(ParamSlot::Mix, 1.0f);
    p.setBase(ParamSlot::Freeze, 1.0f);
    REQUIRE(p.base(ParamSlot::Freeze) == 1.0f); // the value still reads On

    const auto render = [&] {
        Stereo out{std::vector<float>(kFrames, kNaN), std::vector<float>(kFrames, kNaN)};
        for (int64_t done = 0; done < kFrames; done += 512)
        {
            const auto j = static_cast<std::size_t>(done);
            const float *in[2] = {input.left.data() + j, input.right.data() + j};
            float *o[2] = {out.left.data() + j, out.right.data() + j};
            p.process(in, 2, o, 2, 512, {});
        }
        return out;
    };

    const Stereo activated = render();
    REQUIRE(allFinite(activated.left));
    CHECK(p.engine().liveWriteHead() == kFrames); // the head never stopped
    CHECK(peakIn(activated.left, kFrames / 2, kFrames) > 0.05f);
    CHECK(peakIn(activated.right, kFrames / 2, kFrames) > 0.05f);

    p.reset();
    REQUIRE(p.base(ParamSlot::Freeze) == 1.0f);
    const Stereo afterReset = render();
    REQUIRE(allFinite(afterReset.left));
    CHECK(p.engine().liveWriteHead() == kFrames);
    CHECK(peakIn(afterReset.left, kFrames / 2, kFrames) > 0.05f);
    // A reset restarts the same render: a fresh ring, the same seed and the same parameters.
    CHECK(sameBits(activated.left, afterReset.left));
    CHECK(sameBits(activated.right, afterReset.right));
}

TEST_CASE("Freeze toggled off and on after a reset freezes as before", "[core]")
{
    // An off→on transition engages Freeze exactly as it did before option (b): the 20 ms ramp,
    // then the head stops (ruling M1-p's admission clamp included). A processor that starts with
    // Freeze on, sees it off at t1 and on again at t2 renders bit for bit what one that starts
    // with Freeze off and sees it on at t2 renders: until t1 the first is live, as the second is.
    // Both the activation path (a fresh processor) and the reset path (a processor that has
    // already run, then reset() with Freeze on) are checked.
    constexpr int64_t kFrames = 2 * 48000;
    constexpr int64_t kT1 = 30000 + 5;
    constexpr int64_t kT2 = 60000 + 11;
    const Stereo input = noise(kFrames, 0.5f, 37);
    const auto mixAll = [](Processor &p) { p.setBase(ParamSlot::Mix, 1.0f); };

    AutomationRender fromOff;
    renderAutomation(input, {{kT2, ParamSlot::Freeze, false, 1.0}}, 4096, {512}, fromOff, mixAll);
    REQUIRE(allFinite(fromOff.audio.left));
    REQUIRE(peakIn(fromOff.audio.left, 24000, kT1) > 0.05f);
    // Engaged at the boundary after t2: the head stops 20 ms (960 samples) later.
    const int64_t engaged = (kT2 + 31) / 32 * 32;
    REQUIRE(fromOff.writeHead == engaged + 960);
    REQUIRE(peakIn(fromOff.audio.left, engaged + 4800, kFrames) > 0.05f); // the held ring sounds

    const std::vector<TimedEvent> toggled{{kT1, ParamSlot::Freeze, false, 0.0},
                                          {kT2, ParamSlot::Freeze, false, 1.0}};
    SECTION("activated with Freeze on")
    {
        AutomationRender r;
        renderAutomation(input, toggled, 4096, {512}, r, [&](Processor &p) {
            mixAll(p);
            p.setBase(ParamSlot::Freeze, 1.0f);
        });
        CHECK(r.writeHead == fromOff.writeHead);
        CHECK(r.log.grains == fromOff.log.grains);
        CHECK(sameBits(r.audio.left, fromOff.audio.left));
        CHECK(sameBits(r.audio.right, fromOff.audio.right));
    }
    SECTION("reset with Freeze on")
    {
        AutomationRender r;
        renderAutomation(input, toggled, 4096, {512}, r, [&](Processor &p) {
            // Run a while with Freeze off, then turn it on and reset, as a host does on a
            // transport stop.
            mixAll(p);
            std::vector<float> scratch(4096);
            float *o[2] = {scratch.data(), scratch.data()};
            const float *in[2] = {input.left.data(), input.right.data()};
            p.process(in, 2, o, 1, 4096, {});
            p.setBase(ParamSlot::Freeze, 1.0f);
            p.reset();
            r.log.grains.clear(); // the grains before the reset
        });
        CHECK(r.writeHead == fromOff.writeHead);
        CHECK(r.log.grains == fromOff.log.grains);
        CHECK(sameBits(r.audio.left, fromOff.audio.left));
        CHECK(sameBits(r.audio.right, fromOff.audio.right));
    }
}

TEST_CASE("a Freeze that starts on is block-size invariant", "[core][determinism]")
{
    // Freeze on from the start, so not engaged. At the boundary kB, one group whose events
    // straddle a call's end turns Freeze off and back on: it applies once, with Freeze on, so
    // nothing engages (ruling M1-u). Freeze turns off at t1 and on at t2, which engages it. The
    // splits put a 0-frame call first, end a call on kB with the offset-0 events in the next,
    // and cut everywhere else.
    constexpr int64_t kFrames = 2 * 48000;
    constexpr int64_t kB = 40 * 512;
    constexpr int64_t kT1 = 48000 + 7;
    constexpr int64_t kT2 = 60000 + 13;
    const Stereo input = noise(kFrames, 0.5f, 41);
    const std::vector<TimedEvent> events{
        {kB - 10, ParamSlot::Freeze, false, 0.0}, // boundary kB
        {kB, ParamSlot::Freeze, false, 1.0},      // boundary kB: on again, in the same group
        {kT1, ParamSlot::Freeze, false, 0.0},
        {kT2, ParamSlot::Freeze, false, 1.0},
    };
    const auto setup = [](Processor &p) {
        p.setBase(ParamSlot::Mix, 1.0f);
        p.setBase(ParamSlot::Freeze, 1.0f);
    };

    AutomationRender ref;
    renderAutomation(input, events, 4096, {static_cast<uint32_t>(kFrames)}, ref, setup);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    // Live up to t1, kB included; frozen 20 ms after the boundary after t2.
    REQUIRE(peakIn(ref.audio.left, 12000, kB) > 0.05f);
    REQUIRE(peakIn(ref.audio.left, kB, kT1) > 0.05f);
    REQUIRE(ref.writeHead == (kT2 + 31) / 32 * 32 + 960);

    struct Split
    {
        const char *name;
        uint32_t maxBlock;
        std::vector<uint32_t> blockSizes;
    };
    std::vector<Split> splits{
        {"1-frame calls", 4096, {1}},
        {"7-frame calls", 4096, {7}},
        {"32-frame calls", 4096, {32}},
        {"512-frame calls", 4096, {512}},
        {"a 0-frame call first", 4096, {0, 4096}},
        {"a call ending on kB, then a 0-frame call with the offset-0 events",
         4096,
         {static_cast<uint32_t>(kB), 0, static_cast<uint32_t>(kFrames - kB)}},
        {"a call ending on kB", 4096, {static_cast<uint32_t>(kB), 4096}},
        {"maxBlockSize 64", 64, {4096}},
    };
    for (const uint64_t seed : {102, 103})
    {
        splits.push_back({"random", 4096, randomSplit(seed)});
    }
    for (const Split &split : splits)
    {
        INFO(split.name << ", maxBlockSize " << split.maxBlock);
        AutomationRender r;
        renderAutomation(input, events, split.maxBlock, split.blockSizes, r, setup);
        CHECK(r.writeHead == ref.writeHead);
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
    const float g = gg::core::toWetParams(plus12, Mode::Fx).levelGain;
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
        const auto &meta = gg::core::paramMeta(slot, Mode::Fx);
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
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }

    REQUIRE(allFinite(left));
    REQUIRE(allFinite(right));
    // Mix is a crossfade, the wet signal is limited below 1, and so is the reverb's return
    // (the storm sets Reverb Amount too): the output is never louder than the input plus 2.
    REQUIRE(peak(left) < peak(input.left) + 2.0f);
    REQUIRE(peak(right) < peak(input.right) + 2.0f);
    // The effective values: the macros' clamp(base + mod), the factory routes' offsets from them
    // (the storm moves the macros too), then one clamp of base + mod + offset.
    std::array<gg::core::ModSourceValues, gg::core::kLayers> sources{};
    for (size_t m = 0; m < gg::core::kMacroSlots.size(); ++m)
    {
        const ParamSlot macro = gg::core::kMacroSlots[m];
        for (auto &layer : sources)
        {
            layer.v[static_cast<size_t>(gg::core::ModSource::Macro1) + m] =
                clampTo(macro, lastBase[at(macro)] + lastMod[at(macro)]);
        }
    }
    gg::core::ParamValues offsets{};
    gg::core::applyRoutes(p.routes(), sources, offsets);
    REQUIRE(p.routes() == gg::core::factoryRoutes(Mode::Fx));
    for (size_t s = 0; s < kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        INFO("slot " << s);
        REQUIRE(p.base(slot) == lastBase[s]);
        REQUIRE(p.effective(slot) == clampTo(slot, lastBase[s] + lastMod[s] + offsets[s]));
    }
}

namespace
{
// Counts spawns without allocating: the sink runs inside process().
struct SpawnCounter
{
    uint64_t spawns{0};

    static void record(void *ctx, uint32_t, const gg::GrainSpec &)
    {
        ++static_cast<SpawnCounter *>(ctx)->spawns;
    }
};
} // namespace

TEST_CASE("processor does not allocate", "[core][rt]")
{
    // A fresh processor's first process() call is armed: paramMeta builds its table on its first
    // call, which allocates, so the constructor must have made that call. Nothing
    // here calls paramMeta before the constructor does. maxBlockSize 256 makes the 1000-frame
    // calls oversized; mono, stereo, missing and four-channel buffers, 0- and 1-frame calls, and
    // events on every slot (Freeze and modulation included) all run armed.
    Processor p({kSampleRate, 256, 3, Mode::Fx});
    SpawnCounter counter;
    p.setEventSink(&SpawnCounter::record, &counter);
    // Feedback regenerates into the ring from the first call (sub-project 5's spec §5): the ring
    // input is built in preallocated buffers. The events below move Feedback and Mix again.
    p.setBase(ParamSlot::Feedback, 0.5f);
    p.setBase(ParamSlot::Mix, 1.0f);

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
        // Every layer switched on, so the call ends with grains sounding (M2 sub-project 1's spec
        // §6.3).
        const bool layerOn = gg::core::perLayerGlobalLayer(slot) &&
                             slot == gg::core::layerOnSlot(*gg::core::perLayerGlobalLayer(slot));
        const double value = slot == ParamSlot::Density ? 8.0
                             : slot == ParamSlot::Size  ? -4.0
                             : layerOn                  ? 1.0
                                                        : 0.4;
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
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("publishing does not allocate", "[core][publish][rt]")
{
    // The publish path (spec §6.4) under the tripwire: grains into the queue, which the UI never
    // drains here, so it fills and then drops (Review Focus 4); a snapshot per call; the peaks
    // of the ring input, Feedback's included, and of a missing input (silence). 4096-frame calls
    // at Density 8 (256 Hz) spawn about 22 grains each, so 200 of them overflow the 4096-entry
    // queue; 0- and 1-frame calls publish too.
    constexpr uint32_t kFrames = 4096;
    Processor p({kSampleRate, kFrames, 5, Mode::Fx});
    SpawnCounter counter;
    p.setEventSink(&SpawnCounter::record, &counter);
    p.setBase(ParamSlot::Density, 8.0f);
    p.setBase(ParamSlot::Feedback, 0.5f);
    p.setBase(ParamSlot::Mix, 1.0f);
    p.setPublishing(true);
    const Stereo input = noise(kFrames, 0.5f, 43);
    const float *in[2] = {input.left.data(), input.right.data()};
    std::vector<float> o0(kFrames), o1(kFrames);
    float *out[2] = {o0.data(), o1.data()};

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int i = 0; i < 200; ++i)
        {
            p.process(i % 50 == 49 ? nullptr : in, 2, out, 2, kFrames, {});
            p.process(in, 2, out, 2, 0, {});
            p.process(in, 1, out, 2, 1, {});
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    REQUIRE(counter.spawns > gg::core::GrainQueue::kCapacity);
    REQUIRE(p.grainQueue().dropped() == counter.spawns - gg::core::GrainQueue::kCapacity);
    gg::core::UiSnapshot snap;
    REQUIRE(p.snapshot().take(snap));
    REQUIRE(snap.clock == p.engine().sampleClock());
    REQUIRE(p.publishedClock().load() == p.engine().sampleClock());
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
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
        // No source in this slice: the wet signal alone, which is silence, whatever the input.
        // The input feeds the 16 s rings (M2 sub-project 1's spec §6.2), which layer A, reading
        // a sample, doesn't read.
        Processor p({kSampleRate, 512, 1, Mode::Instrument});
        REQUIRE(p.engine().liveFrames() == 16 * 48000);
        // The instrument's defaults: Position at the start of the file (spec §6.8).
        for (size_t s = 0; s < kParamCount; ++s)
        {
            const auto slot = static_cast<ParamSlot>(s);
            INFO("slot " << s);
            REQUIRE(p.base(slot) == gg::core::paramMeta(slot, Mode::Instrument).defaultVal);
        }
        REQUIRE(p.base(ParamSlot::Position) == 0.0f);
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
        REQUIRE(p.base(slot) == gg::core::paramMeta(slot, Mode::Fx).defaultVal);
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

// The macros (sub-project 5's spec §3): effective = clamp(base + mod + Σ macro curves), on the
// control grid like any parameter.

TEST_CASE("macro offsets add to base and modulation before one clamp", "[core]")
{
    const auto &size = gg::core::paramMeta(ParamSlot::Size, Mode::Fx);
    const float density = gg::core::paramMeta(ParamSlot::Density, Mode::Fx).defaultVal;
    std::vector<float> left(64), right(64);
    float *out[2] = {left.data(), right.data()};

    SECTION("above the range")
    {
        Processor p({kSampleRate, 512, 3, Mode::Fx});
        p.setBase(ParamSlot::Size, size.maxVal - 1.0f);
        const std::array<ParamEvent, 2> events{
            {{0, ParamSlot::Size, true, 1.0}, {0, ParamSlot::Macro1, false, 1.0}}};
        p.process(nullptr, 0, out, 2, 64, events);
        REQUIRE(p.base(ParamSlot::Size) == size.maxVal - 1.0f);
        REQUIRE(p.effective(ParamSlot::Macro1) == 1.0f);
        REQUIRE(p.effective(ParamSlot::Size) == size.maxVal); // max − 1 + 1 + 3, clamped
        REQUIRE(p.effective(ParamSlot::Density) == density - 1.5f);
        REQUIRE(p.base(ParamSlot::Density) == density);
    }

    SECTION("one clamp, not two")
    {
        // base + mod is above the range and the macro brings it back: one clamp of the whole
        // sum gives max + 1 − 3; clamping base + mod first would give max − 3.
        Processor p({kSampleRate, 512, 3, Mode::Fx});
        p.setBase(ParamSlot::Size, size.maxVal);
        const std::array<ParamEvent, 2> events{
            {{0, ParamSlot::Size, true, 1.0}, {0, ParamSlot::Macro1, false, 0.0}}};
        p.process(nullptr, 0, out, 2, 64, events);
        REQUIRE(p.effective(ParamSlot::Size) == (size.maxVal + 1.0f) - 3.0f);
        REQUIRE(p.effective(ParamSlot::Density) == density + 1.5f);
    }

    SECTION("a macro is clamped before its curve")
    {
        // Macro 3 at base 1 with modulation +1: its effective value is 1, so Pitch moves +24,
        // not +72 (clamped to its range either way, so read the macro itself).
        Processor p({kSampleRate, 512, 3, Mode::Fx});
        p.setBase(ParamSlot::Pitch, -24.0f);
        const std::array<ParamEvent, 2> events{
            {{0, ParamSlot::Macro3, false, 1.0}, {0, ParamSlot::Macro3, true, 1.0}}};
        p.process(nullptr, 0, out, 2, 64, events);
        REQUIRE(p.effective(ParamSlot::Macro3) == 1.0f);
        REQUIRE(p.effective(ParamSlot::Pitch) == 0.0f); // −24 + 24, not −24 + 72 → 24
    }
}

namespace
{
// Every macro at its rest value (spec §3.1: 0.5 for a bipolar macro, 0 for a unipolar one; Macro
// Feedback is unipolar in both products), each event at its own sample after 0.5 s, off the grid.
std::vector<TimedEvent> macrosAtRest(Mode mode)
{
    std::vector<TimedEvent> events;
    int64_t time = 24000 + 3;
    for (const ParamSlot slot : gg::core::kMacroSlots)
    {
        const bool unipolar =
            slot == ParamSlot::Macro5 || slot == ParamSlot::Macro6 || slot == ParamSlot::Macro7;
        const double rest = unipolar ? 0.0 : 0.5;
        REQUIRE(gg::core::paramMeta(slot, mode).defaultVal == static_cast<float>(rest));
        events.push_back({time, slot, false, rest});
        time += 37;
    }
    return events;
}

// An instrument over 2 s of a mono noise sample, one note held from sample 0, rendered in
// calls of up to 512 frames, with `events` at their absolute samples.
AutomationRender renderInstrument(const std::vector<TimedEvent> &events)
{
    constexpr int64_t kFrames = 2 * 48000;
    const Stereo sample = noise(kFrames, 0.5f, 29);
    auto buffer = std::make_shared<gg::SampleBuffer>(1, kFrames, kSampleRate);
    std::copy(sample.left.begin(), sample.left.end(), buffer->channel(0));
    Processor p({kSampleRate, 512, 42, Mode::Instrument});
    p.setSourceNow(gg::PreparedSource::make(std::move(buffer)));
    AutomationRender r;
    p.setEventSink(&GrainLog::record, &r.log);
    r.audio.left.assign(static_cast<std::size_t>(kFrames), kNaN);
    r.audio.right.assign(static_cast<std::size_t>(kFrames), kNaN);
    const gg::core::NoteEvent on{0, gg::core::NoteEventType::On, 0, 60};
    std::vector<ParamEvent> blockEvents;
    std::size_t next = 0;
    for (int64_t done = 0; done < kFrames;)
    {
        const auto n = static_cast<uint32_t>(std::min<int64_t>(512, kFrames - done));
        blockEvents.clear();
        for (; next < events.size() && events[next].time < done + n; ++next)
        {
            const TimedEvent &e = events[next];
            blockEvents.push_back(
                {static_cast<uint32_t>(e.time - done), e.slot, e.isModulation, e.value});
        }
        const auto j = static_cast<std::size_t>(done);
        float *out[2] = {r.audio.left.data() + j, r.audio.right.data() + j};
        p.process(nullptr, 0, out, 2, n, blockEvents,
                  done == 0 ? std::span(&on, 1) : std::span<const gg::core::NoteEvent>{});
        done += n;
    }
    return r;
}
} // namespace

TEST_CASE("a resting preset renders bit-identically with the macros present", "[core]")
{
    // Spec §3.1: every macro set to its rest value by an event after 0.5 s changes nothing, in
    // either product.
    SECTION("FX")
    {
        const Stereo input = noise(2 * 48000, 0.5f, 23);
        AutomationRender plain;
        renderAutomation(input, {}, 512, {512}, plain);
        AutomationRender rested;
        renderAutomation(input, macrosAtRest(Mode::Fx), 512, {512}, rested);
        REQUIRE(peak(plain.audio.left) > 0.05f);
        REQUIRE(plain.log.grains.size() > 10);
        REQUIRE(plain.ring.left.size() == input.left.size());
        REQUIRE(rested.log.grains == plain.log.grains);
        REQUIRE(sameBits(rested.audio.left, plain.audio.left));
        REQUIRE(sameBits(rested.audio.right, plain.audio.right));
        REQUIRE(sameBits(rested.ring.left, plain.ring.left));
        REQUIRE(sameBits(rested.ring.right, plain.ring.right));
    }

    SECTION("instrument")
    {
        const AutomationRender plain = renderInstrument({});
        const AutomationRender rested = renderInstrument(macrosAtRest(Mode::Instrument));
        REQUIRE(allFinite(plain.audio.left));
        REQUIRE(peak(plain.audio.left) > 0.05f);
        REQUIRE(plain.log.grains.size() > 10);
        REQUIRE(rested.log.grains == plain.log.grains);
        REQUIRE(sameBits(rested.audio.left, plain.audio.left));
        REQUIRE(sameBits(rested.audio.right, plain.audio.right));
    }
}

TEST_CASE("macro events apply on the grid", "[core]")
{
    // A Macro 3 event at sample 5 joins the boundary at 32: Pitch moves from there, in 1-sample
    // calls as in any others.
    Processor p({kSampleRate, 512, 3, Mode::Fx});
    float left = 0.0f;
    float right = 0.0f;
    float *out[2] = {&left, &right};
    const ParamEvent macro3{0, ParamSlot::Macro3, false, 0.75};
    for (int64_t t = 0; t < 64; ++t)
    {
        INFO("after sample " << t);
        p.process(nullptr, 0, out, 2, 1,
                  t == 5 ? std::span(&macro3, 1) : std::span<const ParamEvent>{});
        if (t < 32)
        {
            REQUIRE(p.effective(ParamSlot::Pitch) == 0.0f);
            REQUIRE(p.base(ParamSlot::Macro3) == 0.5f);
        }
        else
        {
            REQUIRE(p.effective(ParamSlot::Pitch) == 12.0f); // 24 × (2 × 0.75 − 1)
            REQUIRE(p.base(ParamSlot::Macro3) == 0.75f);
        }
    }
}

TEST_CASE("macro freeze engages freeze", "[core]")
{
    constexpr uint32_t kBlock = 512;
    std::vector<float> left(kBlock), right(kBlock);
    float *out[2] = {left.data(), right.data()};
    const Stereo input = noise(kBlock, 0.5f, 29);
    const float *in[2] = {input.left.data(), input.right.data()};
    // 20 blocks, about 0.2 s: well past the freeze's 20 ms ramp.
    const auto run = [&](Processor &p, const ParamEvent *first) {
        for (int k = 0; k < 20; ++k)
        {
            p.process(in, 2, out, 2, kBlock,
                      k == 0 && first != nullptr ? std::span(first, 1)
                                                 : std::span<const ParamEvent>{});
        }
    };

    SECTION("by an event")
    {
        Processor p({kSampleRate, kBlock, 3, Mode::Fx});
        run(p, nullptr); // Freeze off seen: armed
        REQUIRE(p.engine().liveWriteHead() == 20 * kBlock);
        const ParamEvent macro6{0, ParamSlot::Macro6, false, 1.0};
        run(p, &macro6);
        REQUIRE(p.base(ParamSlot::Freeze) == 0.0f);
        REQUIRE(p.effective(ParamSlot::Freeze) == 1.0f);
        const int64_t head = p.engine().liveWriteHead();
        REQUIRE(head > 20 * kBlock);
        REQUIRE(head < 40 * kBlock); // stopped
        run(p, nullptr);
        REQUIRE(p.engine().liveWriteHead() == head);

        // Macro 6 off again: the ring runs.
        const ParamEvent off{0, ParamSlot::Macro6, false, 0.0};
        run(p, &off);
        REQUIRE(p.effective(ParamSlot::Freeze) == 0.0f);
        REQUIRE(p.engine().liveWriteHead() > head);
    }

    SECTION("by setBase, as Freeze itself")
    {
        Processor p({kSampleRate, kBlock, 3, Mode::Fx});
        run(p, nullptr);
        p.setBase(ParamSlot::Macro6, 1.0f);
        REQUIRE(p.effective(ParamSlot::Freeze) == 1.0f);
        run(p, nullptr);
        const int64_t head = p.engine().liveWriteHead();
        REQUIRE(head < 40 * kBlock);
        run(p, nullptr);
        REQUIRE(p.engine().liveWriteHead() == head);
    }

    SECTION("live until toggled, as Freeze is")
    {
        // On before the first call: not armed (ruling M1-ac), so the ring keeps running.
        Processor p({kSampleRate, kBlock, 3, Mode::Fx});
        p.setBase(ParamSlot::Macro6, 1.0f);
        run(p, nullptr);
        REQUIRE(p.effective(ParamSlot::Freeze) == 1.0f);
        REQUIRE(p.engine().liveWriteHead() == 20 * kBlock);
    }
}

TEST_CASE("instrument macros 7 and 8 drive Feedback and Level", "[core]")
{
    std::vector<float> left(64), right(64);
    float *out[2] = {left.data(), right.data()};
    const std::array<ParamEvent, 2> events{
        {{0, ParamSlot::Macro7, false, 1.0}, {0, ParamSlot::Macro8, false, 1.0}}};

    // Macro 7 is Macro Feedback in both products (M2 sub-project 1's spec §6.9): in the
    // instrument it moves Feedback, which does nothing there yet, and Stereo stays at its base.
    Processor inst({kSampleRate, 512, 3, Mode::Instrument});
    inst.process(nullptr, 0, out, 2, 64, events);
    CHECK(inst.effective(ParamSlot::Stereo) == 0.5f);
    CHECK(inst.effective(ParamSlot::Level) == 0.0f + 12.0f);
    CHECK(inst.effective(ParamSlot::Feedback) == 0.9f);
    CHECK(inst.feedbackAt(64) == 0.0f); // the instrument's Feedback is 0 (toWetParams)
    CHECK(inst.effective(ParamSlot::Mix) == 0.5f);

    // The FX's are Feedback (+0.9u²) and Mix (+0.5b).
    Processor fx({kSampleRate, 512, 3, Mode::Fx});
    fx.process(nullptr, 0, out, 2, 64, events);
    CHECK(fx.effective(ParamSlot::Feedback) == 0.9f);
    CHECK(fx.effective(ParamSlot::Mix) == 1.0f);
    CHECK(fx.effective(ParamSlot::Stereo) == 0.5f);
    CHECK(fx.effective(ParamSlot::Level) == 0.0f);
}

TEST_CASE("setBase of a macro jumps the ramped values it moves", "[core]")
{
    // A state load or params.flush that sets Macro 8 (Mix) and Macro 5 (Texture, and Diffusion
    // in its top quarter) at their tops sounds as setting Mix, Texture and Diffusion directly:
    // the Mix and Diffusion ramps jump as setBase of Mix or Diffusion makes them. Macro 7 at its
    // top sounds as Feedback 0.9 set directly: its ramp jumps too.
    const Stereo input = noise(48000 / 2, 0.5f, 37);
    AutomationRender direct;
    renderAutomation(input, {}, 512, {512}, direct, [](Processor &p) {
        p.setBase(ParamSlot::Mix, 1.0f);
        p.setBase(ParamSlot::Texture, 1.0f);
        p.setBase(ParamSlot::Diffusion, 1.0f);
        p.setBase(ParamSlot::Feedback, 0.9f);
    });
    AutomationRender macros;
    renderAutomation(input, {}, 512, {512}, macros, [](Processor &p) {
        p.setBase(ParamSlot::Macro8, 1.0f);
        p.setBase(ParamSlot::Macro5, 1.0f);
        p.setBase(ParamSlot::Macro7, 1.0f); // Feedback +0.9, whose ramp jumps too
        REQUIRE(p.effective(ParamSlot::Mix) == 1.0f);
        REQUIRE(p.effective(ParamSlot::Texture) == 1.0f);
        REQUIRE(p.effective(ParamSlot::Diffusion) == 1.0f);
        REQUIRE(p.effective(ParamSlot::Feedback) == 0.9f);
        REQUIRE(p.feedbackAt(0) == 0.9f);
    });
    REQUIRE(peak(direct.audio.left) > 0.05f);
    REQUIRE(macros.log.grains == direct.log.grains);
    REQUIRE(sameBits(macros.audio.left, direct.audio.left));
    REQUIRE(sameBits(macros.audio.right, direct.audio.right));
}

namespace
{
// The RMS of both channels over the frames [from, to).
double rmsOf(const Stereo &s, int64_t from, int64_t to)
{
    double sum = 0.0;
    for (int64_t i = from; i < to; ++i)
    {
        const auto j = static_cast<std::size_t>(i);
        sum += static_cast<double>(s.left[j]) * s.left[j] +
               static_cast<double>(s.right[j]) * s.right[j];
    }
    return std::sqrt(sum / static_cast<double>(2 * (to - from)));
}

double dbOf(double ratio) { return 20.0 * std::log10(ratio); }
} // namespace

TEST_CASE("feedback writes the limited wet signal 256 samples late into the ring", "[core]")
{
    // Sub-project 5's spec §5: ring[t] = sanitize(in[t]) + fb · limited[t − 256]. With Mix 1 the
    // output is limited itself (d × 0 + limited), so the ring's frame t + 256 must hold
    // in[t + 256] + 0.5 × out[t], bit for bit, and its first 256 frames the input alone.
    static_assert(Processor::kFeedbackDelay == 256);
    constexpr uint32_t kFrames = 1025; // readLinear's second tap of frame 1023 is frame 1024
    const Stereo input = noise(kFrames, 0.5f, 41);
    Processor p({kSampleRate, 4096, 5, Mode::Fx});
    p.setBase(ParamSlot::Mix, 1.0f);
    p.setBase(ParamSlot::Level, 0.0f);
    p.setBase(ParamSlot::Diffusion, 0.0f);
    p.setBase(ParamSlot::Density, 6.0f);
    p.setBase(ParamSlot::Size, -2.0f);
    p.setBase(ParamSlot::Position, 0.0f); // right behind the head, so the wet starts at once
    p.setBase(ParamSlot::Spray, 0.0f);
    p.setBase(ParamSlot::Feedback, 0.5f);
    REQUIRE(p.feedbackAt(0) == 0.5f);
    Stereo out{std::vector<float>(kFrames, kNaN), std::vector<float>(kFrames, kNaN)};
    const float *in[2] = {input.left.data(), input.right.data()};
    float *o[2] = {out.left.data(), out.right.data()};
    p.process(in, 2, o, 2, kFrames, {});

    const gg::LiveSource &ring = p.engine().live();
    const int64_t head = p.engine().liveWriteHead();
    REQUIRE(head == kFrames);
    gg::LiveSource::Cursor cursor;
    int sounding = 0;
    for (int64_t f = 0; f < 1024; ++f)
    {
        float l = kNaN;
        float r = kNaN;
        REQUIRE(ring.readLinear(static_cast<double>(f), head, cursor, l, r));
        const auto j = static_cast<std::size_t>(f);
        float expectedL = input.left[j];
        float expectedR = input.right[j];
        if (f >= 256)
        {
            const auto k = static_cast<std::size_t>(f - 256);
            expectedL = input.left[j] + 0.5f * out.left[k];
            expectedR = input.right[j] + 0.5f * out.right[k];
            sounding += out.left[k] != 0.0f ? 1 : 0;
        }
        INFO("frame " << f);
        REQUIRE(std::bit_cast<uint32_t>(l) == std::bit_cast<uint32_t>(expectedL));
        REQUIRE(std::bit_cast<uint32_t>(r) == std::bit_cast<uint32_t>(expectedR));
    }
    REQUIRE(sounding > 200); // the wet signal was there to feed back
}

namespace
{
// 2.5 s of Feedback automation (spec §5; Review Focus 3): 0 → 0.9 at 0.3 s, → 0.2 at 1.1 s, a
// modulation of +0.3 at 1.7 s, and back to 0 (base and modulation) just after 2.2 s, at the
// boundary kFeedbackOff: the 10 ms ramp down reaches exactly 0 at kFeedbackZero.
constexpr int64_t kFeedbackFrames = 48000 * 5 / 2;
constexpr int64_t kFeedbackOff = 105632;              // the boundary of the events at 105605
constexpr int64_t kFeedbackZero = kFeedbackOff + 480; // the first sample with fb(t) == 0
// The input is -0.0 over [kNegativeZeroFrom, kNegativeZeroTo): through the end of the ramp and
// past it, so the ring keeps the sign of zero only if a sample whose fb(t) is 0 writes the input
// alone, inside a piece that started on the ramp as much as in one that started after it.
constexpr int64_t kNegativeZeroFrom = kFeedbackZero - 200;
constexpr int64_t kNegativeZeroTo = kFeedbackZero + 300;

std::vector<TimedEvent> feedbackAutomation()
{
    return {{14400, ParamSlot::Feedback, false, 0.9},
            {52800, ParamSlot::Feedback, false, 0.2},
            {81600, ParamSlot::Feedback, true, 0.3},
            {105605, ParamSlot::Feedback, false, 0.0},
            {105605, ParamSlot::Feedback, true, 0.0}};
}

Stereo feedbackInput()
{
    Stereo input = noise(kFeedbackFrames, 0.5f, 43);
    std::fill(input.left.begin() + kNegativeZeroFrom, input.left.begin() + kNegativeZeroTo, -0.0f);
    std::fill(input.right.begin() + kNegativeZeroFrom, input.right.begin() + kNegativeZeroTo,
              -0.0f);
    return input;
}

void feedbackSetup(Processor &p)
{
    p.setBase(ParamSlot::Mix, 1.0f);
    p.setBase(ParamSlot::Density, 6.0f);
}
} // namespace

TEST_CASE("feedback is block-size invariant", "[core][determinism]")
{
    // The history is indexed by absolute time and the FX's pieces are at most 256 samples, so a
    // 1-sample maxBlockSize, 0-frame calls and any split render what one 4096 call does, and
    // write the same ring, bit for bit.
    const Stereo input = feedbackInput();
    const auto events = feedbackAutomation();

    AutomationRender ref;
    renderAutomation(input, events, 4096, {4096}, ref, feedbackSetup);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(peak(ref.audio.left) > 0.05f);
    REQUIRE_FALSE(ref.log.overflowed);
    // Not vacuous: the same render without the events (Feedback 0 throughout) differs.
    AutomationRender none;
    renderAutomation(input, {}, 4096, {4096}, none, feedbackSetup);
    REQUIRE(none.log.grains == ref.log.grains); // feedback changes the ring, not the schedule
    REQUIRE_FALSE(sameBits(none.audio.left, ref.audio.left));
    // The ramp down to 0 still feeds back: the ring differs from the input on it.
    REQUIRE(ref.writeHead == kFeedbackFrames);
    REQUIRE(ref.ring.left.size() == static_cast<std::size_t>(kFeedbackFrames));
    int fedOnRamp = 0;
    for (int64_t t = kFeedbackOff; t < kFeedbackZero; ++t)
    {
        const auto j = static_cast<std::size_t>(t);
        if (std::bit_cast<uint32_t>(ref.ring.left[j]) != std::bit_cast<uint32_t>(input.left[j]))
        {
            ++fedOnRamp;
        }
    }
    REQUIRE(fedOnRamp > 400);
    // From the ramp's end the ring is the input, bit for bit: the -0.0 samples included.
    for (int64_t t = kFeedbackZero; t < kFeedbackFrames; ++t)
    {
        const auto j = static_cast<std::size_t>(t);
        INFO("frame " << t);
        REQUIRE(std::bit_cast<uint32_t>(ref.ring.left[j]) ==
                std::bit_cast<uint32_t>(input.left[j]));
        REQUIRE(std::bit_cast<uint32_t>(ref.ring.right[j]) ==
                std::bit_cast<uint32_t>(input.right[j]));
    }
    REQUIRE(std::signbit(ref.ring.left[static_cast<std::size_t>(kFeedbackZero)]));

    struct Split
    {
        uint32_t maxBlock;
        std::vector<uint32_t> blockSizes;
    };
    std::vector<Split> splits{{4096, {1}},  {4096, {7}},   {4096, {511}}, {1, {4096}},
                              {64, {4096}}, {256, {4096}}, {300, {4096}}};
    for (const uint64_t seed : {199, 200, 201})
    {
        auto random = randomSplit(seed);
        REQUIRE(std::count(random.begin(), random.end(), 0u) > 0); // 0-frame calls
        splits.push_back({4096, std::move(random)});
    }
    for (const Split &split : splits)
    {
        INFO("maxBlockSize = " << split.maxBlock << ", first block size = "
                               << split.blockSizes.front() << " of " << split.blockSizes.size());
        AutomationRender r;
        renderAutomation(input, events, split.maxBlock, split.blockSizes, r, feedbackSetup);
        REQUIRE_FALSE(r.log.overflowed);
        REQUIRE(r.log.grains == ref.log.grains);
        REQUIRE(r.writeHead == ref.writeHead);
        REQUIRE(sameBits(ref.audio.left, r.audio.left));
        REQUIRE(sameBits(ref.audio.right, r.audio.right));
        REQUIRE(sameBits(ref.ring.left, r.ring.left));
        REQUIRE(sameBits(ref.ring.right, r.ring.right));
    }
}

TEST_CASE("feedback stays bounded for 60 s", "[core][slow]")
{
    // Spec §5's property test: Feedback 0.95 with dense, loud settings (128 grains/s of 1 s,
    // Level +12 dB, Stereo 100%) over 2 s of loud stereo noise, then silence, for 60 s. With Mix
    // 1 the output is the limited wet signal, so |out| < 1 is the limiter's bound; and the
    // regenerated level doesn't run away: the last 10 s are no louder than seconds 16 to 26
    // (the first 10 s after the 16 s ring has filled), within 1 dB.
    constexpr int64_t kFrames = 60 * 48000;
    Stereo input = noise(kFrames, 0.5f, 47);
    std::fill(input.left.begin() + 2 * 48000, input.left.end(), 0.0f);
    std::fill(input.right.begin() + 2 * 48000, input.right.end(), 0.0f);
    AutomationRender r;
    renderAutomation(input, {}, 512, {512}, r, [](Processor &p) {
        p.setBase(ParamSlot::Feedback, gg::core::kFeedbackMax);
        p.setBase(ParamSlot::Density, 7.0f);
        p.setBase(ParamSlot::Size, 0.0f);
        p.setBase(ParamSlot::Mix, 1.0f);
        p.setBase(ParamSlot::Level, 12.0f);
        p.setBase(ParamSlot::Stereo, 1.0f);
    });
    REQUIRE(allFinite(r.audio.left));
    REQUIRE(allFinite(r.audio.right));
    REQUIRE(peak(r.audio.left) < 1.0f);
    REQUIRE(peak(r.audio.right) < 1.0f);
    const double settled = rmsOf(r.audio, 16 * 48000, 26 * 48000);
    const double last = rmsOf(r.audio, 50 * 48000, 60 * 48000);
    INFO("RMS over 16-26 s " << dbOf(settled) << " dBFS, over 50-60 s " << dbOf(last) << " dBFS");
    REQUIRE(settled > 0.0);
    REQUIRE(dbOf(last / settled) <= 1.0);
}

TEST_CASE("freeze stops feedback writes", "[core]")
{
    // Spec §5: Freeze wins. Frozen from 0.5 s, the ring's write gain falls to 0 over 20 ms and
    // the head stops, so neither the input nor the feedback is written and the frozen buffer
    // holds: with Feedback 0.9 acting from the start, every frame of the ring is the same, bit
    // for bit, at 3 s as at 1 s, and the output holds its level.
    constexpr int64_t kFrames = 3 * 48000;
    constexpr int64_t kFreezeAt = 24000;
    const Stereo input = noise(kFrames, 0.5f, 53);

    SECTION("the frozen ring holds, bit for bit")
    {
        Processor p({kSampleRate, 4096, 9, Mode::Fx});
        p.setBase(ParamSlot::Mix, 1.0f);
        p.setBase(ParamSlot::Feedback, 0.9f);
        Stereo out{std::vector<float>(kFrames, kNaN), std::vector<float>(kFrames, kNaN)};
        const auto renderTo = [&](int64_t from, int64_t to) {
            for (int64_t done = from; done < to;)
            {
                const auto n = static_cast<uint32_t>(std::min<int64_t>(4096, to - done));
                const auto j = static_cast<std::size_t>(done);
                const float *in[2] = {input.left.data() + j, input.right.data() + j};
                float *o[2] = {out.left.data() + j, out.right.data() + j};
                std::vector<ParamEvent> events;
                if (kFreezeAt >= done && kFreezeAt < done + n)
                {
                    events.push_back(
                        {static_cast<uint32_t>(kFreezeAt - done), ParamSlot::Freeze, false, 1.0});
                }
                p.process(in, 2, o, 2, n, events);
                done += n;
            }
        };
        // Every frame readLinear can reach: frames 0 .. head − 2.
        const auto snapshot = [&p] {
            const int64_t head = p.engine().liveWriteHead();
            Stereo frames{std::vector<float>(static_cast<std::size_t>(head - 1)),
                          std::vector<float>(static_cast<std::size_t>(head - 1))};
            gg::LiveSource::Cursor cursor;
            bool ok = true;
            for (int64_t f = 0; f + 1 < head; ++f)
            {
                const auto j = static_cast<std::size_t>(f);
                ok = ok && p.engine().live().readLinear(static_cast<double>(f), head, cursor,
                                                        frames.left[j], frames.right[j]);
            }
            REQUIRE(ok);
            return frames;
        };
        renderTo(0, 48000);
        const int64_t head = p.engine().liveWriteHead();
        // The freeze engages at its boundary, and the head stops after the 960-sample ramp.
        REQUIRE(head > kFreezeAt);
        REQUIRE(head <= kFreezeAt + 960 + 32);
        const Stereo atOne = snapshot();
        REQUIRE(peak(atOne.left) > 0.1f);
        renderTo(48000, kFrames);
        REQUIRE(p.engine().liveWriteHead() == head);
        REQUIRE(p.feedbackAt(kFrames) == 0.9f); // Feedback still on: Freeze is what stops it
        const Stereo atThree = snapshot();
        REQUIRE(sameBits(atOne.left, atThree.left));
        REQUIRE(sameBits(atOne.right, atThree.right));
        // The grains go on reading the frozen buffer at a steady level.
        const double second = rmsOf(out, 48000, 2 * 48000);
        const double third = rmsOf(out, 2 * 48000, kFrames);
        INFO("RMS over 1-2 s " << dbOf(second) << " dBFS, over 2-3 s " << dbOf(third) << " dBFS");
        REQUIRE(second > 0.01);
        REQUIRE(std::abs(dbOf(third / second)) <= 1.0);
    }

    SECTION("frozen, Feedback 0.9 sounds as Feedback 0")
    {
        // Feedback raised to 0.9 at the freeze's own boundary, so the two rings match up to it
        // (Feedback acting before the freeze would put its regenerated signal in the frozen
        // buffer, a legitimately different sound): after 1 s the output is as loud as with
        // Feedback 0 and the same Freeze, within 0.5 dB.
        const auto render = [&](double feedback) {
            const std::vector<TimedEvent> events{{kFreezeAt, ParamSlot::Freeze, false, 1.0},
                                                 {kFreezeAt, ParamSlot::Feedback, false, feedback}};
            AutomationRender r;
            renderAutomation(input, events, 4096, {4096}, r,
                             [](Processor &p) { p.setBase(ParamSlot::Mix, 1.0f); });
            return r;
        };
        const AutomationRender fed = render(0.9);
        const AutomationRender plain = render(0.0);
        REQUIRE(fed.writeHead == plain.writeHead);
        REQUIRE(fed.writeHead <= kFreezeAt + 960 + 32);
        const double fedRms = rmsOf(fed.audio, 48000, kFrames);
        const double plainRms = rmsOf(plain.audio, 48000, kFrames);
        INFO("after 1 s: Feedback 0.9 " << dbOf(fedRms) << " dBFS, Feedback 0 " << dbOf(plainRms)
                                        << " dBFS");
        REQUIRE(plainRms > 0.01);
        REQUIRE(std::abs(dbOf(fedRms / plainRms)) <= 0.5);
    }
}
