// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Space in the Processor (sub-project 4's spec §6.1): the diffuser on the engine's output, the
// reverb on the grid with its Dry to Reverb send, and the return added after the wet chain. At
// its defaults Space leaves the output bit for bit as it was; at any setting the output is the
// same at every host block size, finite, bounded and rendered without allocating.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Processor.h"
#include "gg/core/Rng.h"
#include "gg/engine/Engine.h"
#include "gg/source/PreparedSource.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "support/AllocTripwire.h"

using gg::core::kParamCount;
using gg::core::Mode;
using gg::core::NoteEvent;
using gg::core::NoteEventType;
using gg::core::ParamEvent;
using gg::core::ParamSlot;
using gg::core::ParamValues;
using gg::core::Processor;
using gg::test::AllocTripwire;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

size_t at(ParamSlot s) { return static_cast<size_t>(s); }

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;

    explicit Stereo(std::size_t frames = 0, float fill = 0.0f)
        : left(frames, fill), right(frames, fill)
    {
    }
};

double uniform(uint64_t seed, uint64_t index)
{
    return gg::rng::uniform01(gg::rng::hash({seed, 0, 0, gg::rng::Field::SignalNoise, index}));
}

// Stereo noise in ±amp from gg::rng, the two channels independent.
Stereo noise(int64_t frames, float amp, uint64_t seed)
{
    Stereo s(static_cast<std::size_t>(frames));
    for (int64_t i = 0; i < frames; ++i)
    {
        const auto j = static_cast<std::size_t>(i);
        s.left[j] = amp * static_cast<float>(2.0 * uniform(seed, 2 * j) - 1.0);
        s.right[j] = amp * static_cast<float>(2.0 * uniform(seed, 2 * j + 1) - 1.0);
    }
    return s;
}

// A stereo sine, the right channel a quarter turn ahead.
Stereo sine(int64_t frames, double hz, float amp)
{
    Stereo s(static_cast<std::size_t>(frames));
    for (int64_t i = 0; i < frames; ++i)
    {
        const auto j = static_cast<std::size_t>(i);
        const double phase = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / kSampleRate;
        s.left[j] = amp * static_cast<float>(std::sin(phase));
        s.right[j] = amp * static_cast<float>(std::sin(phase + 0.5 * std::numbers::pi));
    }
    return s;
}

std::shared_ptr<const gg::PreparedSource> sourceOf(const Stereo &s)
{
    const auto frames = static_cast<int64_t>(s.left.size());
    auto buf = std::make_shared<gg::SampleBuffer>(2, frames, kSampleRate);
    std::copy(s.left.begin(), s.left.end(), buf->channel(0));
    std::copy(s.right.begin(), s.right.end(), buf->channel(1));
    return gg::PreparedSource::make(std::move(buf));
}

bool sameBits(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

bool allFinite(const std::vector<float> &v)
{
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

float peak(const std::vector<float> &v, std::size_t from = 0)
{
    float p = 0.0f;
    for (std::size_t i = from; i < v.size(); ++i)
    {
        p = std::max(p, std::abs(v[i]));
    }
    return p;
}

ParamValues defaults(Mode mode)
{
    ParamValues v{};
    for (size_t s = 0; s < kParamCount; ++s)
    {
        v[s] = gg::core::paramMeta(static_cast<ParamSlot>(s), mode).defaultVal;
    }
    return v;
}

constexpr NoteEvent kNoteOn60{0, NoteEventType::On, 0, 60};

// Renders `input` (the FX's input; ignored by the instrument) through `p` in calls of `block`
// frames, with `first` given to the first call.
Stereo renderBlocks(Processor &p, const Stereo &input, uint32_t numInChannels,
                    uint32_t numOutChannels, uint32_t block, std::span<const NoteEvent> first = {})
{
    const std::size_t frames = input.left.size();
    Stereo out(frames, kNaN);
    for (std::size_t done = 0; done < frames; done += block)
    {
        const auto n = static_cast<uint32_t>(std::min<std::size_t>(block, frames - done));
        const float *in[2] = {input.left.data() + done, input.right.data() + done};
        float *o[2] = {out.left.data() + done, out.right.data() + done};
        p.process(numInChannels > 0 ? in : nullptr, numInChannels, o, numOutChannels, n, {},
                  done == 0 ? first : std::span<const NoteEvent>());
    }
    return out;
}

// What the Processor rendered before Space (main at 5eefffc), computed from an Engine driven as
// the Processor drives it, with today's formula: wet = softLimit(w × g), out = d × (1 − m) +
// wet × m in the FX, wet in the instrument. Every parameter at its default but Mix; the
// instrument plays key 60 from the first sample.
Stereo oldChain(Mode mode, const Stereo &input, float mix,
                const std::shared_ptr<const gg::PreparedSource> &source)
{
    const bool fx = mode == Mode::Fx;
    gg::EngineConfig config;
    config.sampleRate = kSampleRate;
    config.maxBlockSize = 4096;
    config.seed = 42;
    config.liveSeconds = fx ? Processor::kLiveSeconds : 0.0;
    gg::Engine engine(config);
    ParamValues v = defaults(mode);
    v[at(ParamSlot::Mix)] = mix;
    engine.setCloudParams(gg::core::toCloudParams(v, mode));
    if (fx)
    {
        engine.setSourceMode(gg::SourceMode::Live);
    }
    else
    {
        engine.setNoteGate(true);
        engine.setPreparedSource(source);
        const uint8_t key = 60;
        engine.setHeldKeys(std::span(&key, 1));
    }
    const gg::core::WetParams w = gg::core::toWetParams(v);
    const std::size_t frames = input.left.size();
    Stereo out(frames);
    std::vector<float> wetL(512), wetR(512);
    float *wet[2] = {wetL.data(), wetR.data()};
    for (std::size_t done = 0; done < frames; done += 512)
    {
        const auto n = static_cast<uint32_t>(std::min<std::size_t>(512, frames - done));
        const float *in[2] = {input.left.data() + done, input.right.data() + done};
        engine.process(fx ? in : nullptr, fx ? 2 : 0, wet, 2, n);
        for (uint32_t i = 0; i < n; ++i)
        {
            const float l = gg::core::softLimit(wetL[i] * w.levelGain);
            const float r = gg::core::softLimit(wetR[i] * w.levelGain);
            const auto dry = [](float x) { return std::isfinite(x) ? x : 0.0f; };
            out.left[done + i] = fx ? dry(input.left[done + i]) * (1.0f - w.mix) + l * w.mix : l;
            out.right[done + i] = fx ? dry(input.right[done + i]) * (1.0f - w.mix) + r * w.mix : r;
        }
    }
    return out;
}

int negativeZeros(const std::vector<float> &v)
{
    return static_cast<int>(
        std::count_if(v.begin(), v.end(), [](float x) { return x == 0.0f && std::signbit(x); }));
}
} // namespace

TEST_CASE("space at defaults is bit-identical to the old chain", "[core][space]")
{
    // Global constraints: with every Space parameter at its default, the output is main's, bit
    // for bit, the sign of zero included. The FX's inputs carry −0 every 5th sample from 0.5 s,
    // so at Mix 0 the old chain gives −0 wherever the wet signal there is negative or −0: the
    // diffuser and the return must not add a +0 to those.
    constexpr int64_t kFrames = 2 * 48000;
    const Mode mode = GENERATE(Mode::Fx, Mode::Instrument);
    const bool useNoise = GENERATE(false, true);
    const float mix = GENERATE(0.0f, 0.5f, 1.0f);
    CAPTURE(mode == Mode::Fx, useNoise, mix);

    Stereo input = useNoise ? noise(kFrames, 0.5f, 3) : sine(kFrames, 220.0, 0.5f);
    for (std::size_t i = 24000; i < input.left.size(); i += 5)
    {
        input.left[i] = -0.0f;
        input.right[i] = -0.0f;
    }
    const auto source = sourceOf(input);
    const Stereo expected = oldChain(mode, input, mix, source);

    Processor p({kSampleRate, 4096, 42, mode});
    p.setBase(ParamSlot::Mix, mix);
    if (mode == Mode::Instrument)
    {
        p.setSourceNow(source);
    }
    // The instrument plays key 60; a note would freeze the FX (spec §6.3).
    const Stereo actual =
        renderBlocks(p, input, 2, 2, 512,
                     mode == Mode::Instrument ? std::span<const NoteEvent>(&kNoteOn60, 1)
                                              : std::span<const NoteEvent>());
    CHECK_FALSE(p.reverbActive());

    // Guards against a vacuous pass: the grains sound, and at Mix 0 there are −0s to keep.
    CHECK(peak(expected.left) > 0.05f);
    if (mode == Mode::Fx && mix == 0.0f)
    {
        CHECK(negativeZeros(expected.left) > 100);
        CHECK(negativeZeros(expected.right) > 100);
        CHECK(negativeZeros(actual.left) == negativeZeros(expected.left));
    }
    CHECK(sameBits(actual.left, expected.left));
    CHECK(sameBits(actual.right, expected.right));
}

namespace
{
constexpr int64_t kAutomationFrames = 3 * 48000;

struct TimedEvent
{
    int64_t time;
    ParamSlot slot;
    bool isModulation;
    double value;
};

struct TimedNote
{
    int64_t time;
    NoteEventType type;
    int16_t key;
};

// Every Space parameter automated over 3 s, with Mix and Level moving too:
// - Reverb Amount on a fixed schedule: on, off for 0.5 s (long enough for the idle clear to
//   finish), on, off just long enough for the ramp to reach 0, and on again a few chunks later,
//   while the idle clear is still under way;
// - Decay in and out of ∞ every 0.25 s, and modulation that pushes it across kDecayInfinite;
// - the other slots 30 events each at times from gg::rng, every fourth a modulation offset of
//   up to an eighth of the range, some on grid points.
std::vector<TimedEvent> spaceAutomation()
{
    constexpr int64_t s = 48000;
    std::vector<TimedEvent> events{
        {0, ParamSlot::ReverbAmount, false, 0.6},
        {s * 3 / 10, ParamSlot::ReverbAmount, false, 1.0},
        {s * 7 / 10, ParamSlot::ReverbAmount, true, -0.2},
        {s, ParamSlot::ReverbAmount, false, 0.0},
        {s, ParamSlot::ReverbAmount, true, 0.0},
        {s * 3 / 2 + 17, ParamSlot::ReverbAmount, false, 0.9},
        {2 * s, ParamSlot::ReverbAmount, false, 0.0},
        {2 * s + 480 + 100, ParamSlot::ReverbAmount, false, 0.7},
        {s * 5 / 2 + 3, ParamSlot::ReverbAmount, false, 0.4},
        // Finite at 5.8, then modulation takes it across kDecayInfinite and back.
        {s / 2 + 1000, ParamSlot::ReverbDecay, false, 5.8},
        {s / 2 + 3000, ParamSlot::ReverbDecay, true, 0.2},
        {s / 2 + 5000, ParamSlot::ReverbDecay, true, 0.0},
    };
    for (int k = 0; k < 12; ++k)
    {
        const double value = k % 2 == 0 ? static_cast<double>(gg::core::kDecayMax)
                                        : -2.0 + 5.0 * uniform(5, static_cast<uint64_t>(k));
        events.push_back({k * s / 4 + 11 * k, ParamSlot::ReverbDecay, false, value});
    }

    struct Range
    {
        ParamSlot slot;
        float lo, hi;
    };
    constexpr std::array<Range, 9> ranges{{
        {ParamSlot::Mix, 0.3f, 1.0f},
        {ParamSlot::Level, -6.0f, 6.0f},
        {ParamSlot::Diffusion, 0.0f, 1.0f},
        {ParamSlot::ReverbSize, 0.0f, 1.0f},
        {ParamSlot::ReverbDamping, 0.0f, 1.0f},
        {ParamSlot::ReverbPredelay, 0.0f, 0.5f},
        {ParamSlot::ReverbModulation, 0.0f, 1.0f},
        {ParamSlot::DryToReverb, 0.0f, 1.0f},
        {ParamSlot::Pitch, -12.0f, 12.0f},
    }};
    for (size_t r = 0; r < ranges.size(); ++r)
    {
        const Range range = ranges[r];
        for (int k = 0; k < 30; ++k)
        {
            const uint64_t key = 1000 * (r + 1) + static_cast<uint64_t>(k) * 2;
            auto time = static_cast<int64_t>(uniform(7, key) * kAutomationFrames);
            if (k % 10 == 3)
            {
                time -= time % 32; // on a grid point
            }
            const double u = uniform(7, key + 1);
            const bool isModulation = k % 4 == 1;
            const double value = isModulation ? (2.0 * u - 1.0) * (range.hi - range.lo) / 8.0
                                              : range.lo + (range.hi - range.lo) * u;
            events.push_back({time, range.slot, isModulation, value});
        }
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const TimedEvent &a, const TimedEvent &b) { return a.time < b.time; });
    return events;
}

// Block sizes drawn from gg::rng, enough to cover kAutomationFrames: a tenth of them 0-frame
// calls, three in ten multiples of 32, the rest anything in 1..4096.
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

struct SpaceRender
{
    Stereo audio;
    bool reverbRan{false}; // reverbActive() after some call
    bool wentIdle{false};  // and not after a later one
    bool sawInfinite{false};
};

// Renders `frames` frames through a fresh processor in calls of blockSizes in turn (cycling),
// each call given the events and notes that fall inside it, a 0-frame call at `done` taking
// those stamped `done`. The instrument plays `source`.
void renderSpace(Mode mode, const Stereo &input, const std::vector<TimedEvent> &events,
                 const std::vector<TimedNote> &notes, uint32_t maxBlock,
                 const std::vector<uint32_t> &blockSizes,
                 const std::shared_ptr<const gg::PreparedSource> &source, SpaceRender &r)
{
    const auto frames = static_cast<int64_t>(input.left.size());
    Processor p({kSampleRate, maxBlock, 42, mode});
    p.setBase(ParamSlot::Density, 6.0f); // 64 grains/s
    p.setBase(ParamSlot::Position, mode == Mode::Fx ? 0.1f : 0.2f);
    if (mode == Mode::Instrument)
    {
        p.setSourceNow(source);
    }
    r.audio = Stereo(static_cast<std::size_t>(frames), kNaN);
    std::vector<ParamEvent> blockEvents;
    std::vector<NoteEvent> blockNotes;
    std::size_t nextEvent = 0;
    std::size_t nextNote = 0;
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
            const TimedNote &e = notes[nextNote];
            blockNotes.push_back({static_cast<uint32_t>(e.time - done), e.type, 0, e.key});
        }
        const auto j = static_cast<std::size_t>(done);
        const float *in[2] = {input.left.data() + j, input.right.data() + j};
        float *out[2] = {r.audio.left.data() + j, r.audio.right.data() + j};
        p.process(in, 2, out, 2, n, blockEvents, blockNotes);
        r.wentIdle = r.wentIdle || (r.reverbRan && !p.reverbActive());
        r.reverbRan = r.reverbRan || p.reverbActive();
        r.sawInfinite =
            r.sawInfinite || p.effective(ParamSlot::ReverbDecay) >= gg::core::kDecayInfinite;
        done += n;
    }
}
} // namespace

TEST_CASE("space output is block-size invariant", "[core][space][determinism]")
{
    // Review Focus 2: the reverb's chunks sit on the absolute grid and carry across calls of any
    // size, maxBlockSize below 32 included, and 0-frame calls.
    const Mode mode = GENERATE(Mode::Fx, Mode::Instrument);
    CAPTURE(mode == Mode::Fx);
    const Stereo input = mode == Mode::Fx ? noise(kAutomationFrames, 0.5f, 31)
                                          : sine(kAutomationFrames, 330.0, 0.5f);
    const auto source = sourceOf(input);
    const auto events = spaceAutomation();
    // The instrument's notes; notes would freeze the FX (spec §6.3).
    std::vector<TimedNote> notes;
    if (mode == Mode::Instrument)
    {
        notes = {{0, NoteEventType::On, 60},
                 {52800, NoteEventType::On, 67},
                 {105605, NoteEventType::Off, 60}};
    }

    SpaceRender ref;
    renderSpace(mode, input, events, notes, 4096, {4096}, source, ref);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(peak(ref.audio.left) > 0.05f);
    REQUIRE_FALSE(sameBits(ref.audio.left, ref.audio.right));
    // Guards against a vacuous pass: the reverb ran, went idle and ran again, and Decay reached ∞.
    REQUIRE(ref.reverbRan);
    REQUIRE(ref.wentIdle);
    REQUIRE(ref.sawInfinite);

    struct Split
    {
        uint32_t maxBlock;
        std::vector<uint32_t> blockSizes;
    };
    std::vector<Split> splits{{4096, {1}}, {4096, {32}}, {4096, {64}}, {4096, {511}},
                              {1, {4096}}, {7, {4096}},  {7, {1}},     {64, {4096}}};
    for (const uint64_t seed : {99, 100, 101})
    {
        auto random = randomSplit(seed);
        REQUIRE(std::count(random.begin(), random.end(), 0u) > 0); // 0-frame calls
        splits.push_back({4096, std::move(random)});
    }
    for (const Split &split : splits)
    {
        INFO("maxBlockSize = " << split.maxBlock << ", first block size = "
                               << split.blockSizes.front() << " of " << split.blockSizes.size());
        SpaceRender r;
        renderSpace(mode, input, events, notes, split.maxBlock, split.blockSizes, source, r);
        REQUIRE(sameBits(ref.audio.left, r.audio.left));
        REQUIRE(sameBits(ref.audio.right, r.audio.right));
    }
}

TEST_CASE("dry to reverb at mix zero puts the input in the room", "[core][space]")
{
    // Spec §6.1: at Mix 0 the reverb hears s × d alone, and the output is d plus the return:
    // the input itself for the first chunk (nothing has run yet), the input plus its room
    // after, and the room alone once the input stops. With Amount 0, or with the send at 0,
    // nothing reaches the output but the input, bit for bit.
    constexpr int64_t kFrames = 48000;
    Stereo input = noise(kFrames, 0.5f, 11);
    std::fill(input.left.begin() + 12000, input.left.end(), 0.0f);
    std::fill(input.right.begin() + 12000, input.right.end(), 0.0f);
    const auto render = [&](float send, float amount) {
        Processor p({kSampleRate, 512, 42, Mode::Fx});
        p.setBase(ParamSlot::Mix, 0.0f);
        p.setBase(ParamSlot::DryToReverb, send);
        p.setBase(ParamSlot::ReverbAmount, amount);
        p.setBase(ParamSlot::Diffusion, 1.0f); // the wet side, which Mix 0 keeps out
        return renderBlocks(p, input, 2, 2, 512);
    };

    const Stereo room = render(1.0f, 1.0f);
    for (std::size_t i = 0; i < Processor::kControlGrid; ++i)
    {
        INFO("i = " << i);
        CHECK(room.left[i] == input.left[i]);
        CHECK(room.right[i] == input.right[i]);
    }
    std::size_t first = 0;
    int differ = 0;
    for (std::size_t i = 0; i < 12000; ++i)
    {
        if (room.left[i] != input.left[i])
        {
            first = first == 0 ? i : first;
            ++differ;
        }
    }
    WARN("the return first reaches the left output at sample " << first);
    CHECK(first >= Processor::kControlGrid);
    CHECK(differ > 12000 - static_cast<int>(first) - 100);
    CHECK(peak(room.left, 12000 + 4800) > 0.01f); // the room rings on after the input stops
    CHECK(peak(room.right, 12000 + 4800) > 0.01f);
    CHECK(allFinite(room.left));
    CHECK(allFinite(room.right));

    for (const auto &[send, amount] : {std::pair{1.0f, 0.0f}, std::pair{0.0f, 1.0f}})
    {
        CAPTURE(send, amount);
        const Stereo dry = render(send, amount);
        CHECK(sameBits(dry.left, input.left));
        CHECK(sameBits(dry.right, input.right));
    }
}

TEST_CASE("diffusion and reverb amount ramp like mix", "[core][space]")
{
    // An event at grid boundary b moves Diffusion and Amount linearly over the 10 ms ramp
    // (480 samples), as it does Mix: before b the output is the old setting's, bit for bit, from
    // b + 480 the new one's, and in between the crossfade by the ramp's value. Both the
    // diffuser's output and the reverb's return are the same whatever the setting (the diffuser
    // runs every sample; the reverb's input doesn't depend on Amount), so three renders show it.
    constexpr int64_t kFrames = 24000;
    constexpr int64_t kRamp = 480;
    const Stereo input = noise(kFrames, 0.2f, 29);
    const auto render = [&](ParamSlot slot, float before, float after, int64_t b, float mix) {
        Processor p({kSampleRate, 512, 42, Mode::Fx});
        p.setBase(ParamSlot::Position, 0.02f);
        p.setBase(ParamSlot::Density, 7.0f);
        p.setBase(ParamSlot::Mix, mix);
        p.setBase(ParamSlot::DryToReverb, 1.0f);
        p.setBase(slot, before);
        Stereo out(kFrames, kNaN);
        for (int64_t done = 0; done < kFrames; done += 512)
        {
            const auto j = static_cast<std::size_t>(done);
            const float *in[2] = {input.left.data() + j, input.right.data() + j};
            float *o[2] = {out.left.data() + j, out.right.data() + j};
            const ParamEvent e{static_cast<uint32_t>(b - done), slot, false, after};
            const auto n = static_cast<uint32_t>(std::min<int64_t>(512, kFrames - done));
            const bool here = b >= done && b < done + n;
            p.process(in, 2, o, 2, n, here ? std::span(&e, 1) : std::span<const ParamEvent>());
        }
        return out.left;
    };
    // Checks that `ramped` is `from` before b, `to` from b + kRamp, and from + u·(to − from)
    // between, u the ramp's value; returns how many samples between had to − from big enough
    // to show it.
    const auto checkRamp = [&](const std::vector<float> &from, const std::vector<float> &to,
                               const std::vector<float> &ramped, int64_t b) {
        const auto bi = static_cast<std::size_t>(b);
        const auto ei = static_cast<std::size_t>(b + kRamp);
        CHECK(std::equal(ramped.begin(), ramped.begin() + static_cast<std::ptrdiff_t>(bi),
                         from.begin()));
        CHECK(std::equal(ramped.begin() + static_cast<std::ptrdiff_t>(ei), ramped.end(),
                         to.begin() + static_cast<std::ptrdiff_t>(ei)));
        int shown = 0;
        for (std::size_t i = bi; i < ei; ++i)
        {
            const double u = static_cast<double>(i - bi) / static_cast<double>(kRamp);
            const double step = static_cast<double>(to[i]) - static_cast<double>(from[i]);
            const double expected = static_cast<double>(from[i]) + u * step;
            if (std::abs(step) > 1e-3)
            {
                ++shown;
                INFO("i = " << i);
                CHECK(std::abs(static_cast<double>(ramped[i]) - expected) < 1e-5);
            }
        }
        return shown;
    };

    SECTION("diffusion")
    {
        // Mix 100%, Level 0 dB and the wet signal below the limiter's knee: out = w'.
        constexpr int64_t b = 150 * 32;
        const auto plain = render(ParamSlot::Diffusion, 0.0f, 0.0f, -1, 1.0f);
        const auto full = render(ParamSlot::Diffusion, 1.0f, 1.0f, -1, 1.0f);
        const auto ramped = render(ParamSlot::Diffusion, 0.0f, 1.0f, b, 1.0f);
        REQUIRE(peak(plain) < 0.5f);
        REQUIRE(peak(full) < 0.5f);
        CHECK(checkRamp(plain, full, ramped, b) > 200);
    }
    SECTION("reverb amount")
    {
        // Mix 0 and the send at 100%: out = d + softLimit(a·r), r below the knee. From 20%
        // (active) to 100%, so the reverb runs the same in all three.
        constexpr int64_t b = 400 * 32;
        const auto low = render(ParamSlot::ReverbAmount, 0.2f, 0.2f, -1, 0.0f);
        const auto high = render(ParamSlot::ReverbAmount, 1.0f, 1.0f, -1, 0.0f);
        const auto ramped = render(ParamSlot::ReverbAmount, 0.2f, 1.0f, b, 0.0f);
        // The returns alone: 0.2·r, r, and a·r with a = 0.2 + 0.8·u = 0.2·r + u·(r − 0.2·r).
        std::vector<float> lowReturn(low.size()), highReturn(high.size()), rampReturn(low.size());
        for (std::size_t i = 0; i < low.size(); ++i)
        {
            lowReturn[i] = low[i] - input.left[i];
            highReturn[i] = high[i] - input.left[i];
            rampReturn[i] = ramped[i] - input.left[i];
        }
        REQUIRE(peak(highReturn) < 0.5f);
        REQUIRE(peak(highReturn) > 0.01f);
        CHECK(checkRamp(lowReturn, highReturn, rampReturn, b) > 200);
    }
}

TEST_CASE("the instrument ignores dry to reverb", "[core][space]")
{
    // The instrument has no dry signal: its reverb hears the wet signal, whatever Dry to Reverb
    // says, set or automated.
    constexpr int64_t kFrames = 48000;
    const Stereo sample = sine(kFrames, 440.0, 0.5f);
    const auto source = sourceOf(sample);
    const auto render = [&](float send, float amount, bool automate) {
        Processor p({kSampleRate, 512, 42, Mode::Instrument});
        p.setSourceNow(source);
        p.setBase(ParamSlot::ReverbAmount, amount);
        p.setBase(ParamSlot::DryToReverb, send);
        std::vector<ParamEvent> events;
        if (automate)
        {
            events = {{100, ParamSlot::DryToReverb, false, 0.3},
                      {200, ParamSlot::DryToReverb, true, 0.5},
                      {400, ParamSlot::DryToReverb, false, 0.0}};
        }
        Stereo out(kFrames, kNaN);
        for (std::size_t done = 0; done < out.left.size(); done += 512)
        {
            float *o[2] = {out.left.data() + done, out.right.data() + done};
            const auto n =
                static_cast<uint32_t>(std::min<std::size_t>(512, out.left.size() - done));
            p.process(nullptr, 0, o, 2, n, done == 0 ? events : std::vector<ParamEvent>(),
                      done == 0 ? std::span<const NoteEvent>(&kNoteOn60, 1)
                                : std::span<const NoteEvent>());
        }
        CHECK(p.reverbActive() == (amount > 0.0f));
        return out;
    };
    const Stereo plain = render(0.0f, 1.0f, false);
    CHECK(peak(plain.left) > 0.05f);
    // Guard against a vacuous pass: the return is there.
    CHECK_FALSE(sameBits(plain.left, render(0.0f, 0.0f, false).left));
    for (const bool automate : {false, true})
    {
        CAPTURE(automate);
        const Stereo sent = render(1.0f, 1.0f, automate);
        CHECK(sameBits(sent.left, plain.left));
        CHECK(sameBits(sent.right, plain.right));
    }
}

TEST_CASE("space handles mono and missing channels", "[core][space]")
{
    // Review Focus 3.
    constexpr int64_t kFrames = 24000;
    const Stereo input = noise(kFrames, 0.5f, 13);
    Stereo monoAsStereo = input;
    monoAsStereo.right = monoAsStereo.left;
    const auto processor = [](float mix) {
        auto p =
            std::make_unique<Processor>(gg::core::ProcessorConfig{kSampleRate, 512, 42, Mode::Fx});
        p->setBase(ParamSlot::Mix, mix);
        p->setBase(ParamSlot::DryToReverb, 1.0f);
        p->setBase(ParamSlot::ReverbAmount, 1.0f);
        p->setBase(ParamSlot::Diffusion, 0.7f);
        p->setBase(ParamSlot::Density, 6.0f);
        return p;
    };

    SECTION("one output channel gets the left return")
    {
        // At Mix 0 the wet side is out of both the output and the reverb's input, so a mono
        // output and a stereo one hear the same reverb, fed the mono input on both sides. The
        // mono output is the stereo output's left channel, whose right channel differs.
        const Stereo stereo = renderBlocks(*processor(0.0f), input, 1, 2, 512);
        const Stereo mono = renderBlocks(*processor(0.0f), input, 1, 1, 512);
        CHECK(sameBits(mono.left, stereo.left));
        CHECK_FALSE(sameBits(stereo.left, stereo.right));
        CHECK_FALSE(sameBits(stereo.left, input.left)); // the return is there
        CHECK(std::all_of(mono.right.begin(), mono.right.end(),
                          [](float x) { return std::isnan(x); })); // never written
    }
    SECTION("a mono input feeds both channels")
    {
        const Stereo mono = renderBlocks(*processor(0.5f), input, 1, 2, 512);
        const Stereo both = renderBlocks(*processor(0.5f), monoAsStereo, 2, 2, 512);
        CHECK(sameBits(mono.left, both.left));
        CHECK(sameBits(mono.right, both.right));
        CHECK(peak(mono.left) > 0.05f);
    }
    SECTION("a missing input is silence")
    {
        const Stereo missing = renderBlocks(*processor(0.5f), input, 0, 2, 512);
        const Stereo silent = renderBlocks(*processor(0.5f), Stereo(kFrames), 2, 2, 512);
        CHECK(allFinite(missing.left));
        CHECK(allFinite(missing.right));
        CHECK(sameBits(missing.left, silent.left));
        CHECK(sameBits(missing.right, silent.right));
    }
    SECTION("non-finite input samples count as 0")
    {
        Stereo dirty = input;
        Stereo clean = input;
        for (std::size_t i = 7; i < dirty.left.size(); i += 13)
        {
            dirty.left[i] = kNaN;
            dirty.right[i] = i % 2 == 0 ? kInf : -kInf;
            clean.left[i] = 0.0f;
            clean.right[i] = 0.0f;
        }
        const Stereo fromDirty = renderBlocks(*processor(0.5f), dirty, 2, 2, 512);
        const Stereo fromClean = renderBlocks(*processor(0.5f), clean, 2, 2, 512);
        CHECK(allFinite(fromDirty.left));
        CHECK(allFinite(fromDirty.right));
        CHECK(sameBits(fromDirty.left, fromClean.left));
        CHECK(sameBits(fromDirty.right, fromClean.right));
    }
}

TEST_CASE("space extreme settings stay bounded", "[core][space][rt]")
{
    // Review Focus 5: Decay flipping in and out of ∞ every 0.5 s, Damping 0, Size 100%,
    // Modulation 100%, Dry to Reverb 100%, Diffusion 100%, Amount 100%, Mix 100% and Level at
    // its top, on full-scale noise, for 60 s. Every sample is finite and |out| <= 2 + |dry|,
    // and nothing allocates.
    constexpr uint32_t kBlock = 512;
    constexpr int64_t kFrames = 60 * 48000;
    Processor p({kSampleRate, kBlock, 42, Mode::Fx});
    for (const auto &[slot, value] :
         {std::pair{ParamSlot::ReverbDamping, 0.0f}, std::pair{ParamSlot::ReverbSize, 1.0f},
          std::pair{ParamSlot::ReverbModulation, 1.0f}, std::pair{ParamSlot::DryToReverb, 1.0f},
          std::pair{ParamSlot::Diffusion, 1.0f}, std::pair{ParamSlot::ReverbAmount, 1.0f},
          std::pair{ParamSlot::Mix, 1.0f}, std::pair{ParamSlot::Level, 12.0f},
          std::pair{ParamSlot::Density, 7.0f}, std::pair{ParamSlot::Position, 0.05f}})
    {
        p.setBase(slot, value);
    }
    const Stereo input = noise(kBlock * 64, 1.0f, 17); // cycled
    Stereo out(kBlock);
    std::vector<ParamEvent> flip;
    flip.reserve(1);
    bool finite = true;
    bool bounded = true;
    float peakOut = 0.0f;
    int64_t sawInfinite = 0;

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int64_t done = 0; done < kFrames; done += kBlock)
        {
            // A Decay event every 0.5 s (46.875 blocks), alternating ∞ and 2 s, inside a block.
            const int64_t half = (done + kBlock - 1 - 100) / 24000; // the last flip before the end
            const int64_t flipAt = half * 24000 + 100;
            flip.clear();
            if (flipAt >= done && flipAt < done + kBlock)
            {
                flip.push_back({static_cast<uint32_t>(flipAt - done), ParamSlot::ReverbDecay, false,
                                half % 2 == 0 ? gg::core::kDecayMax : 1.0});
            }
            const auto j = static_cast<std::size_t>(done % (kBlock * 64));
            const float *in[2] = {input.left.data() + j, input.right.data() + j};
            float *o[2] = {out.left.data(), out.right.data()};
            p.process(in, 2, o, 2, kBlock, flip);
            sawInfinite += p.effective(ParamSlot::ReverbDecay) >= gg::core::kDecayInfinite;
            for (uint32_t i = 0; i < kBlock; ++i)
            {
                for (const auto &[x, d] :
                     {std::pair{out.left[i], in[0][i]}, std::pair{out.right[i], in[1][i]}})
                {
                    finite = finite && std::isfinite(x);
                    bounded = bounded && std::abs(x) <= 2.0f + std::abs(d);
                    peakOut = std::max(peakOut, std::abs(x));
                }
            }
        }
    }
    const uint64_t allocations = AllocTripwire::count();
    CAPTURE(peakOut);
    CHECK(finite);
    CHECK(bounded);
    CHECK(p.reverbActive());
    CHECK(sawInfinite > 2000);      // about half the blocks
    CHECK(peakOut > 1.0f);          // the return adds to a limited wet signal
    if (AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        CHECK(allocations == 0);
    }
}

TEST_CASE("space is allocation-free", "[core][space][rt]")
{
    // Every Space parameter automated, values and modulation, Decay into and out of ∞, Amount
    // to 0 and back (the idle clear and a re-activation), in calls of 0, 1, oversized and
    // ordinary lengths, mono, stereo, missing and four-channel buffers, both products. A fresh
    // processor's first call is armed.
    const Mode mode = GENERATE(Mode::Fx, Mode::Instrument);
    CAPTURE(mode == Mode::Fx);
    Processor p({kSampleRate, 256, 3, mode});
    if (mode == Mode::Instrument)
    {
        p.setSourceNow(sourceOf(sine(48000, 220.0, 0.5f)));
    }
    constexpr uint32_t kFrames = 1000;
    constexpr uint32_t kLong = 3000; // oversized: more than maxBlockSize
    const Stereo input = noise(kLong, 0.5f, 19);
    const float *in[2] = {input.left.data(), input.right.data()};
    std::vector<float> o0(kLong), o1(kLong), o2(kLong), o3(kLong);
    float *out[4] = {o0.data(), o1.data(), o2.data(), o3.data()};

    const auto spaceEvents = [](double amount, double decay) {
        std::vector<ParamEvent> events{
            {0, ParamSlot::ReverbAmount, false, amount},
            {40, ParamSlot::ReverbDecay, false, decay},
            {90, ParamSlot::Diffusion, false, 0.8},
            {130, ParamSlot::ReverbSize, false, 0.9},
            {170, ParamSlot::ReverbDamping, false, 0.0},
            {210, ParamSlot::ReverbPredelay, false, 0.25},
            {260, ParamSlot::ReverbModulation, false, 1.0},
            {300, ParamSlot::DryToReverb, false, 1.0},
            {350, ParamSlot::ReverbPredelay, true, -0.1},
            {400, ParamSlot::ReverbDecay, true, 0.5},
            {450, ParamSlot::Diffusion, true, -0.3},
            {500, ParamSlot::ReverbSize, true, -0.5},
            {600, ParamSlot::ReverbDamping, true, 0.4},
            {700, ParamSlot::ReverbModulation, true, -0.5},
            {800, ParamSlot::DryToReverb, true, -0.5},
            {900, ParamSlot::ReverbAmount, true, -0.25},
        };
        return events;
    };
    const auto on = spaceEvents(1.0, static_cast<double>(gg::core::kDecayMax));
    const auto finite = spaceEvents(0.7, 0.5);
    const auto off = spaceEvents(0.0, 3.0);
    bool ran = false;

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        p.process(in, 2, out, 2, kFrames, on, std::span(&kNoteOn60, 1)); // the first call
        for (int i = 0; i < 20; ++i)
        {
            p.process(in, 2, out, 2, 0, {});
            p.process(in, 2, out, 2, 1, {});
            p.process(in, 1, out, 2, kFrames, i % 3 == 2 ? off : finite);
            ran = ran || p.reverbActive();
            p.process(nullptr, 0, out, 2, 100, {});
            p.process(in, 2, nullptr, 0, 100, {});
            p.process(in, 2, out, 4, kFrames, on);
            p.process(in, 2, out, 1, 100, {});
            p.process(in, 2, out, 2, kLong, {}); // the idle clear runs after `off`
        }
    }
    const uint64_t allocations = AllocTripwire::count();
    CHECK(ran);
    CHECK(allFinite(o0));
    if (AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        CHECK(allocations == 0);
    }
}
