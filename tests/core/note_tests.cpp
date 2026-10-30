// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Notes in the Processor (spec §6.1–§6.3): note events on the 32-sample control grid, in the
// same boundary groups as parameter events; the held set; the instrument's note gate; the FX's
// note freeze; and their real-time and determinism contracts.

#include <algorithm>
#include <array>
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
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/MathDet.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "gg/core/Rng.h"
#include "support/AllocTripwire.h"

using gg::core::Mode;
using gg::core::NoteEvent;
using gg::core::NoteEventType;
using gg::core::ParamEvent;
using gg::core::ParamSlot;
using gg::core::Processor;
using gg::test::AllocTripwire;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
// The freeze ramp at 48 kHz: 20 ms (LiveSource).
constexpr int64_t kFreezeRamp = 960;

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

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

double uniform(uint64_t seed, uint64_t index)
{
    return gg::rng::uniform01(gg::rng::hash({seed, 0, 0, gg::rng::Field::SignalNoise, index}));
}

// Stereo noise in ±amp from gg::rng, the two channels independent.
Stereo noise(int64_t frames, float amp, uint64_t seed)
{
    Stereo s{std::vector<float>(static_cast<std::size_t>(frames)),
             std::vector<float>(static_cast<std::size_t>(frames))};
    for (int64_t i = 0; i < frames; ++i)
    {
        const auto j = static_cast<std::size_t>(i);
        s.left[j] = amp * static_cast<float>(2.0 * uniform(seed, 2 * j) - 1.0);
        s.right[j] = amp * static_cast<float>(2.0 * uniform(seed, 2 * j + 1) - 1.0);
    }
    return s;
}

// A 4 s stereo sweep at 48 kHz, 100 Hz to 2 kHz, the right channel a quarter turn ahead.
std::shared_ptr<const gg::PreparedSource> sweepSource()
{
    constexpr int64_t kFrames = 4 * 48000;
    auto buf = std::make_shared<gg::SampleBuffer>(2, kFrames, kSampleRate);
    for (uint32_t c = 0; c < 2; ++c)
    {
        double phase = 0.25 * c;
        for (int64_t i = 0; i < kFrames; ++i)
        {
            const double f = 100.0 + 1900.0 * static_cast<double>(i) / kFrames;
            buf->channel(c)[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * phase));
            phase += f / kSampleRate;
        }
    }
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

// The peak of the samples [from, to) of v.
float peakIn(const std::vector<float> &v, int64_t from, int64_t to)
{
    float p = 0.0f;
    for (int64_t i = from; i < to; ++i)
    {
        p = std::max(p, std::abs(v[static_cast<std::size_t>(i)]));
    }
    return p;
}

float peak(const std::vector<float> &v) { return peakIn(v, 0, static_cast<int64_t>(v.size())); }

std::vector<uint8_t> keysOf(const Processor &p)
{
    const auto keys = p.heldKeys();
    return {keys.begin(), keys.end()};
}

NoteEvent on(uint32_t offset, int16_t key, int16_t channel = 0)
{
    return {offset, NoteEventType::On, channel, key};
}

NoteEvent off(uint32_t offset, int16_t key, int16_t channel = 0)
{
    return {offset, NoteEventType::Off, channel, key};
}

struct TimedNote
{
    int64_t time; // absolute sample
    NoteEventType type;
    int16_t channel;
    int16_t key;
};

struct TimedParam
{
    int64_t time; // absolute sample
    ParamSlot slot;
    bool isModulation;
    double value;
};

struct Render
{
    Stereo audio;
    GrainLog log;
    int64_t writeHead{0};
};

// Renders `frames` frames through p in process() calls of blockSizes in turn (cycling), each
// call given the events that fall inside it. A 0-frame call at sample `done` takes the events
// stamped `done`, at offset 0. `input`, if given, feeds the FX.
void renderNotes(Processor &p, int64_t frames, const std::vector<TimedParam> &params,
                 const std::vector<TimedNote> &notes, const std::vector<uint32_t> &blockSizes,
                 Render &r, const Stereo *input = nullptr)
{
    p.setEventSink(&GrainLog::record, &r.log);
    r.audio.left.assign(static_cast<std::size_t>(frames), kNaN);
    r.audio.right.assign(static_cast<std::size_t>(frames), kNaN);
    std::vector<ParamEvent> blockParams;
    std::vector<NoteEvent> blockNotes;
    std::size_t nextParam = 0;
    std::size_t nextNote = 0;
    int64_t done = 0;
    for (std::size_t next = 0; done < frames; ++next)
    {
        const uint32_t want = blockSizes[next % blockSizes.size()];
        const auto n = static_cast<uint32_t>(std::min<int64_t>(want, frames - done));
        const int64_t limit = done + std::max<int64_t>(n, 1);
        blockParams.clear();
        for (; nextParam < params.size() && params[nextParam].time < limit; ++nextParam)
        {
            const TimedParam &e = params[nextParam];
            blockParams.push_back(
                {static_cast<uint32_t>(e.time - done), e.slot, e.isModulation, e.value});
        }
        blockNotes.clear();
        for (; nextNote < notes.size() && notes[nextNote].time < limit; ++nextNote)
        {
            const TimedNote &e = notes[nextNote];
            blockNotes.push_back({static_cast<uint32_t>(e.time - done), e.type, e.channel, e.key});
        }
        const auto j = static_cast<std::size_t>(done);
        const float *in[2] = {nullptr, nullptr};
        if (input != nullptr)
        {
            in[0] = input->left.data() + j;
            in[1] = input->right.data() + j;
        }
        float *out[2] = {r.audio.left.data() + j, r.audio.right.data() + j};
        p.process(input != nullptr ? in : nullptr, input != nullptr ? 2 : 0, out, 2, n, blockParams,
                  blockNotes);
        done += n;
    }
    r.writeHead = p.engine().liveWriteHead();
}

// An instrument reading the sweep.
std::unique_ptr<Processor> instrument(uint64_t seed = 42, uint32_t maxBlock = 4096)
{
    auto p = std::make_unique<Processor>(
        gg::core::ProcessorConfig{kSampleRate, maxBlock, seed, Mode::Instrument});
    p->setSourceNow(sweepSource());
    return p;
}

std::unique_ptr<Processor> effect(uint64_t seed = 42, uint32_t maxBlock = 4096)
{
    return std::make_unique<Processor>(
        gg::core::ProcessorConfig{kSampleRate, maxBlock, seed, Mode::Fx});
}

// The grid boundary at or after the absolute sample a.
int64_t boundaryOf(int64_t a) { return (a + 31) / 32 * 32; }
} // namespace

TEST_CASE("an instrument with a sample and no notes is silent", "[core]")
{
    // Spec §6.2: the note gate. Without notes the instrument sounds as one without a sample.
    auto p = instrument();
    Render r;
    renderNotes(*p, 2 * 48000, {}, {}, {512}, r);
    REQUIRE(allFinite(r.audio.left));
    REQUIRE(allFinite(r.audio.right));
    CHECK(peak(r.audio.left) == 0.0f);
    CHECK(peak(r.audio.right) == 0.0f);
    CHECK(r.log.grains.empty());
    CHECK(p->heldKeys().empty());
}

TEST_CASE("setSourceNow keeps the source alive until it is replaced", "[core]")
{
    Processor p({kSampleRate, 512, 1, Mode::Instrument});
    auto a = sweepSource();
    const std::weak_ptr<const gg::PreparedSource> weakA = a;
    p.setSourceNow(std::move(a));
    CHECK_FALSE(weakA.expired());
    auto b = sweepSource();
    const std::weak_ptr<const gg::PreparedSource> weakB = b;
    p.setSourceNow(std::move(b));
    CHECK(weakA.expired()); // no grain read it: let go
    CHECK_FALSE(weakB.expired());

    // The new source sounds once a note is held.
    std::vector<float> left(4096), right(4096);
    float *out[2] = {left.data(), right.data()};
    const NoteEvent n = on(0, 60);
    p.process(nullptr, 0, out, 2, 4096, {}, std::span(&n, 1));
    CHECK(peak(left) > 0.01f);
}

TEST_CASE("a source given by setSourceNow is freed once an offer replaces it", "[core]")
{
    // Spec §6.4: the activation's source retires through sources() like an offered one, so it
    // doesn't stay in memory for the rest of the session once another has replaced it.
    Processor p({kSampleRate, 512, 1, Mode::Instrument});
    auto a = sweepSource();
    const std::weak_ptr<const gg::PreparedSource> weakA = a;
    p.setSourceNow(std::move(a));
    CHECK(p.sources().held() == 1);

    std::vector<float> left(512), right(512);
    float *out[2] = {left.data(), right.data()};
    const NoteEvent n = on(0, 60);
    p.process(nullptr, 0, out, 2, 512, {}, std::span(&n, 1)); // A's grains sound

    auto b = sweepSource();
    const std::weak_ptr<const gg::PreparedSource> weakB = b;
    p.sources().offer(std::move(b));
    for (int i = 0; i < 40; ++i) // about 0.43 s: B adopted, A's grains end, A retires
    {
        p.process(nullptr, 0, out, 2, 512, {});
    }
    CHECK_FALSE(weakA.expired()); // handed back, but only the main thread frees it
    p.sources().collect();
    CHECK(weakA.expired());
    CHECK_FALSE(weakB.expired());
    CHECK(p.sources().held() == 1);
}

TEST_CASE("an offer that keeps the source applies its values and leaves the source (R19)", "[core]")
{
    Processor p({kSampleRate, 512, 1, Mode::Instrument});
    auto a = sweepSource();
    const std::weak_ptr<const gg::PreparedSource> weakA = a;
    p.setSourceNow(std::move(a));
    gg::core::ParamValues values{};
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        values[s] = p.base(static_cast<ParamSlot>(s));
    }
    values[static_cast<std::size_t>(ParamSlot::Density)] = 3.0f;
    p.sources().offerValues(values);

    std::vector<float> left(4096), right(4096);
    float *out[2] = {left.data(), right.data()};
    const NoteEvent n = on(0, 60);
    p.process(nullptr, 0, out, 2, 4096, {}, std::span(&n, 1));
    CHECK(p.base(ParamSlot::Density) == 3.0f);
    CHECK(peak(left) > 0.01f); // still A: an offer of no source would have silenced the note
    p.sources().collect();
    CHECK_FALSE(weakA.expired());
    CHECK(p.sources().held() == 1);
}

TEST_CASE("an instrument plays a one-frame or an empty source safely", "[core]")
{
    // The shortest sources a load can give (convertRate keeps at least one frame of a file that
    // has any) and an empty buffer: a held note renders finite audio, whatever the grains read.
    for (const int64_t frames : {int64_t{1}, int64_t{0}})
    {
        INFO(frames << " frames");
        auto buf = std::make_shared<gg::SampleBuffer>(2, frames, kSampleRate);
        if (frames > 0)
        {
            buf->channel(0)[0] = 0.5f;
            buf->channel(1)[0] = -0.5f;
        }
        Processor p({kSampleRate, 512, 1, Mode::Instrument});
        p.setSourceNow(gg::PreparedSource::make(std::move(buf)));
        std::vector<float> left(48000), right(48000);
        float *out[2] = {left.data(), right.data()};
        const NoteEvent n = on(0, 60);
        p.process(nullptr, 0, out, 2, 48000, {}, std::span(&n, 1));
        CHECK(allFinite(left));
        CHECK(allFinite(right));
    }
}

TEST_CASE("a note-on starts the cloud at its boundary", "[core]")
{
    // Spec §6.2: the first grain spawns at the note's own boundary, 128 for offset 100.
    auto p = instrument();
    GrainLog log;
    p->setEventSink(&GrainLog::record, &log);
    std::vector<float> left(4096), right(4096);
    float *out[2] = {left.data(), right.data()};
    const NoteEvent n = on(100, 60);
    p->process(nullptr, 0, out, 2, 4096, {}, std::span(&n, 1));
    REQUIRE_FALSE(log.grains.empty());
    CHECK(log.grains[0].index == 0);
    CHECK(log.grains[0].onsetSample == 128);
    CHECK(log.grains[0].onsetFrac == 0.0);
    CHECK(keysOf(*p) == std::vector<uint8_t>{60});
    CHECK(peakIn(left, 0, 128) == 0.0f);
    CHECK(peakIn(left, 128, 4096) > 0.01f);
}

TEST_CASE("a note inside one grid cell does nothing", "[core]")
{
    // Spec §6.1: only the held set after a group counts.
    auto p = instrument();
    GrainLog log;
    p->setEventSink(&GrainLog::record, &log);
    std::vector<float> left(4800), right(4800);
    float *out[2] = {left.data(), right.data()};
    const std::array<NoteEvent, 4> notes{{on(33, 60), off(63, 60), on(70, 64), off(96, 64)}};
    p->process(nullptr, 0, out, 2, 4800, {}, notes);
    CHECK(log.grains.empty());
    CHECK(peak(left) == 0.0f);
    CHECK(p->heldKeys().empty());
}

TEST_CASE("releasing the last key stops new grains; the tail is Size long", "[core]")
{
    // Spec §6.2: no grain from the release's boundary on; grains already sounding play out their
    // window, so the output is exactly 0 from Size + 2 samples after the last onset.
    auto p = instrument();
    constexpr int64_t kFrames = 4800 + 3 * 7200;
    Render r;
    renderNotes(*p, kFrames, {}, {{0, NoteEventType::On, 0, 60}, {4800, NoteEventType::Off, 0, 60}},
                {512}, r);
    REQUIRE(allFinite(r.audio.left));
    REQUIRE(r.log.grains.size() >= 2);
    CHECK(r.log.grains[0].onsetSample == 0);
    for (const gg::GrainSpec &g : r.log.grains)
    {
        CHECK(g.onsetSample < 4800);
    }
    const gg::GrainSpec &last = r.log.grains.back();
    // Size's default, 150 ms.
    CHECK(std::abs(last.durationSamples - 0.15 * kSampleRate) < 1.0);
    const int64_t silentFrom =
        last.onsetSample + static_cast<int64_t>(std::ceil(last.durationSamples)) + 2;
    REQUIRE(silentFrom < kFrames);
    CHECK(peakIn(r.audio.left, 0, 4800) > 0.01f);
    CHECK(peakIn(r.audio.left, silentFrom, kFrames) == 0.0f);
    CHECK(peakIn(r.audio.right, silentFrom, kFrames) == 0.0f);
    CHECK(p->heldKeys().empty());
}

TEST_CASE("a chord picks its keys evenly", "[core]")
{
    // Spec §6.1: each grain picks one held key; its rate is that key's ratio from note 60.
    auto p = instrument();
    p->setBase(ParamSlot::Density, 7.0f); // 128 Hz
    Render r;
    renderNotes(*p, 5 * 48000, {},
                {{0, NoteEventType::On, 0, 60},
                 {0, NoteEventType::On, 0, 64},
                 {0, NoteEventType::On, 0, 67}},
                {512}, r);
    REQUIRE_FALSE(r.log.overflowed);
    const auto n = static_cast<double>(r.log.grains.size());
    REQUIRE(n > 500);
    CHECK(keysOf(*p) == std::vector<uint8_t>{60, 64, 67});
    std::array<int, 3> counts{};
    const std::array<int, 3> semitones{0, 4, 7};
    for (const gg::GrainSpec &g : r.log.grains)
    {
        bool matched = false;
        for (std::size_t i = 0; i < 3; ++i)
        {
            // The engine's own arithmetic (Engine::sampleGrain): the key's ratio times the
            // source's rate over the output's, both 48 kHz here. Written out the same way, the
            // comparison below can be exact.
            const double rate = gg::exp2Det(semitones[i] / 12.0) * kSampleRate / kSampleRate;
            if (g.rate == rate)
            {
                ++counts[i];
                matched = true;
            }
        }
        CHECK(matched);
    }
    for (std::size_t i = 0; i < 3; ++i)
    {
        INFO("key " << 60 + semitones[i] << ": " << counts[i] << " of " << n);
        CHECK(counts[i] / n > 0.27);
        CHECK(counts[i] / n < 0.40);
    }
}

TEST_CASE("the held set: channels, wildcards and all notes off", "[core]")
{
    // Spec §6.1: the held set is (channel, key) pairs; a key counts once however many channels
    // hold it, and is released when all are off. A wildcard (−1) Off matches every channel or
    // key; AllOff clears its channel, or everything for channel −1 (ruling R14).
    Processor p({kSampleRate, 512, 1, Mode::Instrument});
    std::vector<float> left(64), right(64);
    float *out[2] = {left.data(), right.data()};
    const auto run = [&](std::vector<NoteEvent> notes) {
        p.process(nullptr, 0, out, 2, 64, {}, notes);
        return keysOf(p);
    };
    using Keys = std::vector<uint8_t>;

    CHECK(run({on(0, 64), on(0, 60), on(0, 60, 1)}) == Keys{60, 64});
    CHECK(run({off(0, 60)}) == Keys{60, 64}); // still held on channel 1
    CHECK(run({off(0, 60, 1)}) == Keys{64});
    CHECK(run({{0, NoteEventType::AllOff, 0, 0}}) == Keys{});

    CHECK(run({on(0, 60, 3), on(0, 61, 4), on(0, 62, 15)}) == Keys{60, 61, 62});
    CHECK(run({off(0, -1, -1)}) == Keys{}); // the wildcard Off

    CHECK(run({on(0, 60, 0), on(0, 60, 1), on(0, 61, 0), on(0, 127, 2), on(0, 0, 2)}) ==
          Keys{0, 60, 61, 127});
    CHECK(run({off(0, 60, -1)}) == Keys{0, 61, 127}); // key 60 on any channel
    CHECK(run({off(0, -1, 2)}) == Keys{61});          // any key on channel 2

    // Out-of-range fields are ignored; a wildcard On means nothing.
    CHECK(run({on(0, 128), on(0, 60, 16), on(0, -1), on(0, 60, -1), off(0, 61, 16)}) == Keys{61});
    // An event past the call's end counts as at its end: it lands at the next boundary.
    CHECK(run({on(500, 70)}) == Keys{61});
    CHECK(run({}) == Keys{61, 70});

    // AllOff on a channel (MIDI CC 123 there) releases only that channel's keys (ruling R14);
    // channel −1 releases everything, and an out-of-range channel makes it ignored.
    CHECK(run({{0, NoteEventType::AllOff, -1, 0}}) == Keys{});
    CHECK(run({on(0, 60, 0), on(0, 60, 1), on(0, 61, 0), on(0, 62, 2)}) == Keys{60, 61, 62});
    CHECK(run({{0, NoteEventType::AllOff, 0, 0}}) == Keys{60, 62}); // 60 still on channel 1
    CHECK(run({{0, NoteEventType::AllOff, 2, -1}}) == Keys{60});
    CHECK(run({{0, NoteEventType::AllOff, 5, 0}}) == Keys{60}); // nothing on channel 5
    CHECK(run({{0, NoteEventType::AllOff, 16, 0}, {0, NoteEventType::AllOff, -2, 0}}) == Keys{60});
    CHECK(run({{0, NoteEventType::AllOff, 1, 0}}) == Keys{});
}

TEST_CASE("reset() releases every note", "[core]")
{
    // Review Focus 1: reset() clears the held set and the engine's keys, so the instrument is
    // silent after it.
    auto p = instrument();
    GrainLog log;
    p->setEventSink(&GrainLog::record, &log);
    std::vector<float> left(48000), right(48000);
    float *out[2] = {left.data(), right.data()};
    const std::array<NoteEvent, 2> chord{{on(0, 60), on(0, 64)}};
    p->process(nullptr, 0, out, 2, 4800, {}, chord);
    REQUIRE(keysOf(*p) == std::vector<uint8_t>{60, 64});
    REQUIRE(peakIn(left, 0, 4800) > 0.01f);

    // A note pending at a boundary past the call's end applies at the reset, and is cleared too.
    const NoteEvent pending = on(4799, 67);
    p->process(nullptr, 0, out, 2, 4800, {}, std::span(&pending, 1));
    p->reset();
    CHECK(p->heldKeys().empty());
    log.grains.clear();
    p->process(nullptr, 0, out, 2, 48000, {});
    CHECK(log.grains.empty());
    CHECK(peak(left) == 0.0f);
    CHECK(peak(right) == 0.0f);
}

TEST_CASE("FX: a held note freezes the ring", "[core]")
{
    // Spec §6.3: the head stops 20 ms after the note-on's boundary and resumes at the release's;
    // Freeze's value never changes. Grains spawned while the note is held take its pitch.
    constexpr int64_t kOn = 10000 + 7;
    constexpr int64_t kOff = 30000 + 1;
    constexpr int64_t kFrames = 40000;
    const Stereo input = noise(kFrames, 0.5f, 11);
    auto p = effect();
    Render r;
    renderNotes(*p, 20000, {}, {{kOn, NoteEventType::On, 0, 67}}, {512}, r, &input);
    CHECK(p->base(ParamSlot::Freeze) == 0.0f);
    CHECK(p->effective(ParamSlot::Freeze) == 0.0f);
    const int64_t engaged = boundaryOf(kOn);
    CHECK(r.writeHead == engaged + kFreezeRamp);
    bool sawHeld = false;
    for (const gg::GrainSpec &g : r.log.grains)
    {
        if (g.onsetSample >= engaged)
        {
            CHECK(g.rate == gg::exp2Det(7.0 / 12.0));
            sawHeld = true;
        }
        else
        {
            CHECK(g.rate == 1.0);
        }
    }
    CHECK(sawHeld);

    // Continue the same processor: the release unfreezes, and the head advances again.
    std::vector<float> left(10000), right(10000);
    float *out[2] = {left.data(), right.data()};
    const float *in20k[2] = {input.left.data() + 20000, input.right.data() + 20000};
    p->process(in20k, 2, out, 2, 10000, {}); // to 30000
    CHECK(p->engine().liveWriteHead() == engaged + kFreezeRamp);
    const NoteEvent release = off(static_cast<uint32_t>(kOff - 30000), 67);
    const float *in30k[2] = {input.left.data() + 30000, input.right.data() + 30000};
    p->process(in30k, 2, out, 2, 10000, {}, std::span(&release, 1)); // to 40000
    const int64_t released = boundaryOf(kOff);
    CHECK(p->engine().liveWriteHead() == engaged + kFreezeRamp + (kFrames - released));
    CHECK(p->base(ParamSlot::Freeze) == 0.0f);
    CHECK(p->effective(ParamSlot::Freeze) == 0.0f);
    CHECK(p->heldKeys().empty());
}

TEST_CASE("FX: no note event changes Freeze's value", "[core]")
{
    // Spec §6.3: a note freezes the ring without touching the Freeze parameter. Checked once
    // each note event has applied (at its grid boundary), over notes overlapping, legato,
    // detached and released by an AllOff, with Freeze off and on.
    using T = NoteEventType;
    const std::vector<TimedNote> notes{{20000 + 5, T::On, 0, 60}, {28800 - 7, T::On, 0, 67},
                                       {28800, T::Off, 0, 67},    {28800, T::On, 1, 67},
                                       {40000, T::Off, 0, 60},    {50000 + 11, T::Off, 1, 67},
                                       {52000, T::On, 0, 48},     {56000 + 1, T::Off, 0, 48},
                                       {60000, T::On, 0, 72},     {66000, T::AllOff, -1, 0}};
    constexpr int64_t kFrames = 70000;
    const Stereo input = noise(kFrames, 0.5f, 29);
    std::vector<float> left(kFrames), right(kFrames);
    for (const float freeze : {0.0f, 1.0f})
    {
        INFO("Freeze " << freeze);
        auto p = effect();
        p->setBase(ParamSlot::Freeze, freeze);
        int64_t done = 0;
        bool froze = false;
        for (std::size_t next = 0; next < notes.size();)
        {
            // The events that apply at one boundary, in one call that runs a grid step past it.
            const int64_t boundary = boundaryOf(notes[next].time);
            std::vector<NoteEvent> group;
            for (; next < notes.size() && boundaryOf(notes[next].time) == boundary; ++next)
            {
                const TimedNote &e = notes[next];
                group.push_back({static_cast<uint32_t>(e.time - done), e.type, e.channel, e.key});
            }
            const auto j = static_cast<std::size_t>(done);
            const float *in[2] = {input.left.data() + j, input.right.data() + j};
            float *out[2] = {left.data() + j, right.data() + j};
            p->process(in, 2, out, 2, static_cast<uint32_t>(boundary + 32 - done), {}, group);
            done = boundary + 32;
            INFO("after the events at boundary " << boundary);
            CHECK(p->base(ParamSlot::Freeze) == freeze);
            CHECK(p->effective(ParamSlot::Freeze) == freeze);
            froze = froze || p->engine().liveWriteHead() < done; // the notes did freeze the ring
        }
        CHECK(froze);
        CHECK(p->heldKeys().empty());
    }
}

TEST_CASE("FX: a note-on freezes without arming; Freeze restored on stays live", "[core]")
{
    // Ruling M1-ac: Freeze on at activation isn't armed, so the ring plays live. A note-on needs
    // no arming (spec §6.3); its release unfreezes, and the parameter alone still isn't armed.
    constexpr int64_t kOn = 10000 + 7;
    constexpr int64_t kOff = 30000 + 1;
    constexpr int64_t kFrames = 50000;
    const Stereo input = noise(kFrames, 0.5f, 13);
    auto p = effect();
    p->setBase(ParamSlot::Freeze, 1.0f);
    Render r;
    renderNotes(*p, 10000, {}, {}, {512}, r, &input);
    CHECK(r.writeHead == 10000); // live

    std::vector<float> left(20000), right(20000);
    float *out[2] = {left.data(), right.data()};
    const NoteEvent hold = on(static_cast<uint32_t>(kOn - 10000), 60);
    const float *in10k[2] = {input.left.data() + 10000, input.right.data() + 10000};
    p->process(in10k, 2, out, 2, 20000, {}, std::span(&hold, 1)); // to 30000
    const int64_t engaged = boundaryOf(kOn);
    CHECK(p->engine().liveWriteHead() == engaged + kFreezeRamp);
    const NoteEvent release = off(static_cast<uint32_t>(kOff - 30000), 60);
    const float *in30k[2] = {input.left.data() + 30000, input.right.data() + 30000};
    p->process(in30k, 2, out, 2, 20000, {}, std::span(&release, 1)); // to 50000
    CHECK(p->engine().liveWriteHead() ==
          engaged + kFreezeRamp + (kFrames - boundaryOf(kOff))); // live again
    CHECK(p->base(ParamSlot::Freeze) == 1.0f);
    CHECK(p->effective(ParamSlot::Freeze) == 1.0f);
}

TEST_CASE("FX: a note-on starts a grain at its boundary", "[core]")
{
    // Spec §6.3: at Density 0.25 Hz the next onset would be seconds away; the note moves it to
    // its own boundary.
    auto p = effect();
    p->setBase(ParamSlot::Density, -2.0f);
    GrainLog log;
    p->setEventSink(&GrainLog::record, &log);
    const Stereo input = noise(4096, 0.5f, 17);
    const float *in[2] = {input.left.data(), input.right.data()};
    std::vector<float> left(4096), right(4096);
    float *out[2] = {left.data(), right.data()};
    const NoteEvent n = on(1000, 60);
    p->process(in, 2, out, 2, 4096, {}, std::span(&n, 1));
    REQUIRE(log.grains.size() >= 2);
    CHECK(log.grains[0].onsetSample == 0); // the stream's first grain
    CHECK(log.grains[1].onsetSample == 1024);
    CHECK(log.grains[1].onsetFrac == 0.0);
    CHECK(log.grains[1].index == 1);
}

namespace
{
constexpr int64_t kDetFrames = 72000; // 1.5 s

// Block sizes drawn from gg::rng, enough to cover kDetFrames: a tenth of them 0-frame calls,
// three in ten multiples of 32 (so calls often end on a grid boundary), the rest anything in
// 1..4096.
std::vector<uint32_t> randomSplit(uint64_t seed)
{
    std::vector<uint32_t> sizes;
    int64_t total = 0;
    for (uint64_t i = 0; total < kDetFrames; ++i)
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

struct Split
{
    const char *name;
    uint32_t maxBlock;
    std::vector<uint32_t> blockSizes;
};

std::vector<Split> splits(int64_t boundary)
{
    const auto b = static_cast<uint32_t>(boundary);
    std::vector<Split> s{
        {"1-frame calls", 4096, {1}},
        {"32-frame calls", 4096, {32}},
        {"64-frame calls", 4096, {64}},
        {"511-frame calls", 4096, {511}},
        {"4096-frame calls", 4096, {4096}},
        {"maxBlockSize 64", 64, {4096}},
        {"a call ending on a boundary, then a 0-frame call with the offset-0 events",
         4096,
         {b, 0, 4096}},
    };
    for (uint64_t seed = 0; seed < 200; ++seed)
    {
        s.push_back({"random", 4096, randomSplit(1000 + seed)});
    }
    return s;
}

void requireSame(const Render &a, const Render &b)
{
    REQUIRE_FALSE(b.log.overflowed);
    REQUIRE(b.writeHead == a.writeHead);
    REQUIRE(b.log.grains == a.log.grains);
    REQUIRE(sameBits(a.audio.left, b.audio.left));
    REQUIRE(sameBits(a.audio.right, b.audio.right));
}
} // namespace

TEST_CASE("instrument notes are block-size invariant", "[core][determinism]")
{
    // The chord and releases, a note inside one group that straddles a call's end at kB, a
    // second onset after the set empties, Density and Pitch automation, and Texture and all
    // eight macros (the instrument's: Macro Feedback and Macro Level as 7 and 8) automated off the
    // grid, values and modulation. Macro Size stays at or below rest and Macro Texture below
    // its top quarter (no Diffusion) around the release at 41000, so the silence after it holds.
    constexpr int64_t kB = 32 * 700;
    using T = NoteEventType;
    const std::vector<TimedNote> notes{{1000 + 5, T::On, 0, 60},
                                       {8000, T::On, 0, 64},
                                       {8000 + 31, T::On, 1, 67},
                                       {kB - 3, T::On, 2, 72},
                                       {kB, T::Off, 2, 72},
                                       {25000, T::Off, 0, 64},
                                       {40000 + 1, T::Off, 0, 60},
                                       {41000, T::Off, 1, 67},
                                       {50000 + 9, T::On, 3, 55},
                                       {50000 + 9, T::On, 3, 79},
                                       {60000 + 20, T::AllOff, -1, 0},
                                       {65000, T::On, 0, 48},
                                       {70000 + 3, T::Off, -1, -1}};
    const std::vector<TimedParam> params{
        {5000 + 7, ParamSlot::Texture, false, 0.6},    {9000 + 13, ParamSlot::Macro1, false, 0.3},
        {12000 + 1, ParamSlot::Macro4, false, 0.8},    {15000 + 19, ParamSlot::Macro5, false, 0.6},
        {18000 + 5, ParamSlot::Macro3, false, 0.7},    {20000 + 3, ParamSlot::Density, false, 6.0},
        {21000 + 11, ParamSlot::Texture, true, -0.3},  {kB, ParamSlot::Pitch, false, -5.0},
        {kB, ParamSlot::Macro2, false, 0.6},           {26000 + 3, ParamSlot::Macro7, false, 0.9},
        {28000 + 29, ParamSlot::Macro8, false, 0.25},  {30000 + 17, ParamSlot::Pitch, false, 3.0},
        {33000 + 9, ParamSlot::Macro6, false, 1.0},    {36000 + 2, ParamSlot::Macro1, true, -0.1},
        {38000 + 17, ParamSlot::Macro6, false, 0.0},   {45000, ParamSlot::Pitch, true, 2.0},
        {47000 + 21, ParamSlot::Texture, false, 0.95}, {52000 + 6, ParamSlot::Macro8, true, 0.2},
        {55000 + 30, ParamSlot::Density, false, 3.0},  {58000 + 15, ParamSlot::Macro2, false, 0.4},
        {62000 + 30, ParamSlot::Macro5, false, 0.2},   {67000 + 1, ParamSlot::Macro3, true, -0.25}};

    const auto run = [&](const Split &split, const std::vector<TimedParam> &events, Render &r) {
        auto p = instrument(42, split.maxBlock);
        // 32 Hz of 62.5 ms grains: few overlap, so the 207 renders stay quick in Debug builds.
        p->setBase(ParamSlot::Density, 5.0f);
        p->setBase(ParamSlot::Size, -4.0f);
        renderNotes(*p, kDetFrames, events, notes, split.blockSizes, r);
    };
    const Split oneCall{"one call", 4096, {static_cast<uint32_t>(kDetFrames)}};
    Render ref;
    run(oneCall, params, ref);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(ref.log.grains.size() > 30);
    REQUIRE(peakIn(ref.audio.left, 1024, 40000) > 0.01f);
    // Texture reached the grains: some latched it, some didn't.
    REQUIRE(std::any_of(ref.log.grains.begin(), ref.log.grains.end(),
                        [](const gg::GrainSpec &g) { return g.texture == 0.0; }));
    REQUIRE(std::any_of(ref.log.grains.begin(), ref.log.grains.end(),
                        [](const gg::GrainSpec &g) { return g.texture == 1.0; }));
    // Silent before the first note, and between the release at 41000 and the next note's tail.
    REQUIRE(peakIn(ref.audio.left, 0, 1024) == 0.0f);
    REQUIRE(peakIn(ref.audio.left, 41024 + 3000 + 2, 50016) == 0.0f);
    // The macro events change the output: without them, the notes and the rest of the
    // automation render other bits.
    std::vector<TimedParam> withoutMacros;
    std::copy_if(params.begin(), params.end(), std::back_inserter(withoutMacros),
                 [](const TimedParam &e) { return !gg::core::isMacro(e.slot); });
    REQUIRE(withoutMacros.size() < params.size());
    Render plain;
    run(oneCall, withoutMacros, plain);
    REQUIRE_FALSE(sameBits(plain.audio.left, ref.audio.left));

    for (const Split &split : splits(kB))
    {
        INFO(split.name << ", maxBlockSize " << split.maxBlock << ", " << split.blockSizes.size()
                        << " sizes");
        Render r;
        run(split, params, r);
        requireSame(ref, r);
    }
}

TEST_CASE("FX note freeze is block-size invariant", "[core][determinism]")
{
    // Notes freeze and release the ring, overlapping (legato) and detached. The Freeze parameter
    // engages while a note holds and keeps the ring frozen after the notes are released; turned
    // off while another note holds, it leaves the ring frozen until that note's release.
    constexpr int64_t kB = 32 * 900;
    using T = NoteEventType;
    const std::vector<TimedNote> notes{{20000 + 5, T::On, 0, 60}, {kB - 7, T::On, 0, 67},
                                       {kB, T::Off, 0, 67},       {kB, T::On, 1, 67},
                                       {40000, T::Off, 0, 60},    {50000 + 11, T::Off, 1, 67},
                                       {52000, T::On, 0, 48},     {56000 + 1, T::Off, 0, 48},
                                       {60000, T::On, 0, 72},     {66000, T::AllOff, -1, 0}};
    const std::vector<TimedParam> params{{10000 + 3, ParamSlot::Density, false, 5.0},
                                         {45000, ParamSlot::Freeze, false, 1.0},
                                         {54000 + 2, ParamSlot::Freeze, false, 0.0},
                                         {58000, ParamSlot::Pitch, true, -7.0}};
    const Stereo input = noise(kDetFrames, 0.5f, 19);

    const auto run = [&](const Split &split, Render &r) {
        auto p = effect(42, split.maxBlock);
        p->setBase(ParamSlot::Mix, 1.0f);
        p->setBase(ParamSlot::Size, -4.0f);
        renderNotes(*p, kDetFrames, params, notes, split.blockSizes, r, &input);
    };
    Render ref;
    run({"one call", 4096, {static_cast<uint32_t>(kDetFrames)}}, ref);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(ref.log.grains.size() > 30);
    REQUIRE(peakIn(ref.audio.left, 24000, kDetFrames) > 0.05f);
    // The head stops 20 ms after the first note's boundary and resumes at the release of key 48
    // (56032); it stops 20 ms after the note at 60000 and resumes at the AllOff (66016).
    const int64_t stop1 = boundaryOf(20000 + 5) + kFreezeRamp;
    const int64_t stop2 = 60000 + kFreezeRamp;
    REQUIRE(ref.writeHead ==
            stop1 + (stop2 - boundaryOf(56000 + 1)) + (kDetFrames - boundaryOf(66000)));

    for (const Split &split : splits(kB))
    {
        INFO(split.name << ", maxBlockSize " << split.maxBlock << ", " << split.blockSizes.size()
                        << " sizes");
        Render r;
        run(split, r);
        requireSame(ref, r);
    }
}

TEST_CASE("process with notes does not allocate", "[core][rt]")
{
    // 64 note events and 64 parameter events in one call, interleaved by offset, on both
    // products, with grains sounding.
    for (const Mode mode : {Mode::Instrument, Mode::Fx})
    {
        INFO((mode == Mode::Fx ? "FX" : "instrument"));
        auto p = mode == Mode::Fx ? effect(3, 256) : instrument(3, 256);
        GrainLog log;
        p->setEventSink(&GrainLog::record, &log);
        constexpr uint32_t kFrames = 1024;
        const Stereo input = noise(kFrames, 0.5f, 23);
        const float *in[2] = {input.left.data(), input.right.data()};
        std::vector<float> left(kFrames), right(kFrames);
        float *out[2] = {left.data(), right.data()};

        std::vector<NoteEvent> notes;
        std::vector<ParamEvent> params;
        for (uint32_t i = 0; i < 64; ++i)
        {
            const uint32_t offset = i * 16;
            const auto key = static_cast<int16_t>(40 + (i * 7) % 48);
            const auto channel = static_cast<int16_t>(i % 16);
            if (i == 40)
            {
                notes.push_back({offset, NoteEventType::AllOff, -1, 0});
            }
            else if (i % 5 == 4)
            {
                notes.push_back(off(offset, i % 10 == 9 ? -1 : key, -1));
            }
            else
            {
                notes.push_back(on(offset, key, channel));
            }
            const auto slot = static_cast<ParamSlot>(i % gg::core::kParamCount);
            const double value =
                slot == ParamSlot::Density ? 7.0 : (slot == ParamSlot::Size ? -4.0 : 0.4);
            params.push_back({offset + 5, slot, i % 3 == 0, value});
        }
        const NoteEvent warm = on(0, 60);
        p->process(in, 2, out, 2, kFrames, {}, std::span(&warm, 1)); // grains sounding

        AllocTripwire::resetCount();
        {
            AllocTripwire::Scope armed;
            p->process(in, 2, out, 2, kFrames, params, notes);
            p->process(in, 2, out, 2, kFrames, params, notes);
            p->process(in, 2, out, 2, 0, {}, notes);
            p->process(in, 2, out, 2, 1, params, notes);
        }
        const uint64_t allocations = AllocTripwire::count();
        REQUIRE_NO_ALLOCATIONS(allocations);
        REQUIRE_FALSE(log.grains.empty());
        REQUIRE(allFinite(left));
    }
}
