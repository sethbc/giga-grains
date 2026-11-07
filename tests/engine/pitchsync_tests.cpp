// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The Pitch clock in the scheduler (M3 pitch-sync spec §4.2, S2 to S6, S9, SB2 to SB4): a voice
// at Clock Pitch spawns a grain every period of its note, N periods long, with every draw but the
// onset's held per refresh. Clock Free is sub-project 1's voice, bit for bit.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/VoiceFixture.h"
#include "gg/core/MathDet.h"
#include "gg/core/Rng.h"
#include "gg/engine/Engine.h"
#include "gg/grain/CloudScheduler.h"
#include "gg/grain/GrainPool.h"
#include "gg/source/SourceMode.h"

using namespace gg::test;

namespace
{
// FNV-1a 64 over raw bytes.
struct Fnv1a
{
    uint64_t h{14695981039346656037ull};

    void bytes(const void *p, std::size_t n)
    {
        const auto *b = static_cast<const unsigned char *>(p);
        for (std::size_t i = 0; i < n; ++i)
        {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }

    template <class T> void value(const T &v) { bytes(&v, sizeof v); }
};

// Every logged grain's fields, one at a time (no padding). Not the audio: its bytes depend on
// libm (the Hann table's cos, the sine sample's sin), which may differ in the last ulp between
// CI's legs, and the cross-platform gate is the grain-event log. No field hashed here comes from
// libm (rates from exp2Det, gains through IEEE sqrt). The audio at rest is the at-rest WAV
// check's.
uint64_t hashGrains(const GrainLog &log)
{
    Fnv1a f;
    for (const gg::GrainSpec &g : log.grains)
    {
        f.value(g.index);
        f.value(g.onsetSample);
        f.value(g.onsetFrac);
        f.value(g.startPos);
        f.value(g.rate);
        f.value(g.durationSamples);
        f.value(g.gain);
        f.value(g.pan);
        f.value(g.bus);
        f.value(g.voice);
    }
    return f.h;
}

// Pinned on the code before the Pitch clock (sub-project 3's Task 2, Step 1).
constexpr uint64_t kFreeVoicesHash = 0xa95e322c275c80d5;
constexpr uint64_t kLiveCloudHash = 0x29d40b6a21e262e2;
// Pinned on the code before the held anchor (sub-project 3's Task 3, Step 1): three Free voices
// over the live ring.
constexpr uint64_t kFreeRingVoicesHash = 0xdf9a47089ae1eeae;
} // namespace

TEST_CASE("free voices and the Cloud stream are unchanged by the clock", "[engine][pitchsync]")
{
    constexpr int64_t kFrames = 2 * 48000;
    constexpr uint32_t kBlock = 256;

    // Four Free voices of kParams over the sine sample, ordinals 0 to 3.
    GrainLog voiceLog;
    Audio voices = silentAudio(kFrames);
    {
        gg::Engine engine(config());
        engine.setSource(sineSource());
        engine.setEventSink(&GrainLog::record, &voiceLog);
        engine.setVoiceMode(true);
        for (uint32_t k = 0; k < 4; ++k)
        {
            engine.startVoice(k, k, voiceAt(kParams));
        }
        renderSpan(engine, voices, 0, kFrames, kBlock);
    }
    REQUIRE_FALSE(voiceLog.overflowed);
    REQUIRE(voiceLog.grains.size() > 200);

    // The Cloud stream at kParams over a live ring fed a 220 Hz sine.
    GrainLog liveLog;
    Audio live = silentAudio(kFrames);
    {
        gg::EngineConfig c = config();
        c.liveSeconds = 4.0;
        gg::Engine engine(c);
        engine.setSourceMode(gg::SourceMode::Live);
        engine.setEventSink(&GrainLog::record, &liveLog);
        engine.setCloudParams(kParams);
        std::vector<float> input(static_cast<std::size_t>(kFrames));
        for (int64_t i = 0; i < kFrames; ++i)
        {
            // No libm: the phase is exact and sineDet is plain IEEE arithmetic everywhere.
            const auto phase = static_cast<float>(static_cast<double>((i * 220) % 48000) / 48000.0);
            input[static_cast<std::size_t>(i)] = 0.5f * gg::sineDet(phase);
        }
        for (int64_t t = 0; t < kFrames; t += kBlock)
        {
            const float *in[1] = {input.data() + t};
            float *out[2] = {live.left.data() + t, live.right.data() + t};
            engine.process(in, 1, out, 2, kBlock);
        }
    }
    REQUIRE_FALSE(liveLog.overflowed);
    REQUIRE(liveLog.grains.size() > 50);

    CHECK(hashGrains(voiceLog) == kFreeVoicesHash);
    CHECK(hashGrains(liveLog) == kLiveCloudHash);
}

namespace
{
constexpr uint32_t kStream = 1; // a voice of ordinal 0

// One voice's controls at Clock Pitch: f0 in densityHz, Size's seconds, Distribution 0.
gg::CloudParams pitchAt(double f0, double sizeSec = 0.15)
{
    gg::CloudParams p;
    p.clock = gg::Clock::Pitch;
    p.densityHz = f0;
    p.distribution = 0.0;
    p.grainDurationSec = sizeSec;
    p.position = 0.4;
    p.gain = 0.5f;
    p.refreshHz = 16.0;
    return p;
}

double onsetTime(const gg::GrainDraw &d)
{
    return static_cast<double>(d.onsetSample) + d.onsetFrac;
}

double onsetTime(const gg::GrainSpec &g)
{
    return static_cast<double>(g.onsetSample) + g.onsetFrac;
}

// The Onset field's bipolar draw for grain k of the voice.
double onsetDraw(uint64_t k)
{
    return gg::rng::bipolar(gg::rng::hash({kSeed, kLayer, kStream, gg::rng::Field::Onset, k}));
}

// A voice's scheduler, at the given rate, with its first onset at 0.
gg::CloudScheduler voiceScheduler(const gg::CloudParams &p, double sampleRate = kSampleRate)
{
    gg::CloudScheduler s;
    s.prepare(sampleRate);
    s.setParams(p);
    s.forceNextOnset();
    return s;
}

// Every draw over [from, to), in blocks of `block`.
void drawSpan(gg::CloudScheduler &s, std::vector<gg::GrainDraw> &out, int64_t from, int64_t to,
              uint32_t block = 64)
{
    for (int64_t t = from; t < to;)
    {
        const auto n = static_cast<uint32_t>(std::min<int64_t>(block, to - t));
        s.schedule(t, n, kSeed, kLayer, kStream, [&](const gg::GrainDraw &d) { out.push_back(d); });
        t += n;
    }
}

// One voice at p over the sine sample, rendered for `frames`: its logged grains.
std::vector<gg::GrainSpec> voiceGrains(const gg::CloudParams &p, int64_t frames)
{
    gg::Engine engine(config());
    engine.setSource(sineSource());
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    engine.setVoiceMode(true);
    engine.startVoice(0, 0, voiceAt(p));
    Audio audio = silentAudio(frames);
    renderSpan(engine, audio, 0, frames, 256);
    REQUIRE_FALSE(log.overflowed);
    return log.grains;
}
} // namespace

TEST_CASE("pitch onsets fall at exactly k times P at distribution 0", "[engine][pitchsync]")
{
    const auto grains = voiceGrains(pitchAt(100.0), 200 * 480 + 100);
    REQUIRE(grains.size() >= 200);
    for (uint64_t k = 0; k < 200; ++k)
    {
        INFO("grain " << k);
        CHECK(grains[k].index == k);
        CHECK(grains[k].voice == 0);
        CHECK(grains[k].onsetSample == static_cast<int64_t>(k * 480));
        CHECK(grains[k].onsetFrac == 0.0);
    }
    // A period that isn't a whole number of samples: the onsets keep their fractions.
    const double period = 48000.0 / 110.0;
    const auto at110 = voiceGrains(pitchAt(110.0), 201 * 437);
    REQUIRE(at110.size() >= 200);
    double t = 0.0;
    bool fractional = false;
    for (uint64_t k = 0; k < 200; ++k)
    {
        INFO("grain " << k);
        CHECK(at110[k].onsetSample == static_cast<int64_t>(std::floor(t)));
        CHECK(onsetTime(at110[k]) == t);
        fractional = fractional || at110[k].onsetFrac != 0.0;
        t += period;
    }
    CHECK(fractional);
}

TEST_CASE("pitch grains are N periods long", "[engine][pitchsync]")
{
    const auto two = voiceGrains(pitchAt(100.0, 0.15), 4800);
    REQUIRE(two.size() >= 5);
    for (const gg::GrainSpec &g : two)
    {
        CHECK(g.durationSamples == 0.15 * gg::CloudScheduler::kPeriodsPerSizeSecond * 480.0);
    }
    // The clamps: 16 samples at the shortest Size and the top f0, 2 s at the longest and the
    // lowest.
    const auto shortest = voiceGrains(pitchAt(12000.0, 0.005), 480);
    REQUIRE(shortest.size() >= 100);
    for (const gg::GrainSpec &g : shortest)
    {
        CHECK(g.durationSamples == 16.0);
    }
    const auto longest = voiceGrains(pitchAt(1.0, 2.0), 2 * 48000);
    REQUIRE(longest.size() == 2);
    for (const gg::GrainSpec &g : longest)
    {
        CHECK(g.durationSamples == 96000.0);
    }
}

TEST_CASE("pitch f0 clamps to one hertz and a quarter of the sample rate", "[engine][pitchsync]")
{
    const auto low = voiceGrains(pitchAt(0.25), 2 * 48000 + 1000);
    REQUIRE(low.size() == 3);
    CHECK(low[0].onsetSample == 0);
    CHECK(low[1].onsetSample == 48000);
    CHECK(low[2].onsetSample == 96000);

    const auto high = voiceGrains(pitchAt(20000.0, 0.005), 400);
    REQUIRE(high.size() == 100);
    for (std::size_t k = 0; k < high.size(); ++k)
    {
        CHECK(high[k].onsetSample == static_cast<int64_t>(4 * k));
        CHECK(high[k].onsetFrac == 0.0);
    }

    CHECK(voiceGrains(pitchAt(0.0), 48000).empty());
}

TEST_CASE("the period jitter stays within half a period at distribution 1", "[engine][pitchsync]")
{
    for (const double d : {1.0, 0.25})
    {
        INFO("distribution " << d);
        gg::CloudParams p = pitchAt(100.0);
        p.distribution = d;
        gg::CloudScheduler s = voiceScheduler(p);
        std::vector<gg::GrainDraw> draws;
        drawSpan(s, draws, 0, 2001 * 720, 4096);
        REQUIRE(draws.size() >= 2001);
        const double bound = 0.5 * d * d * d * 480.0; // 0.5 P at 1, 0.0078125 P at 0.25
        double sum = 0.0;
        bool spread = false;
        for (uint64_t k = 0; k < 2000; ++k)
        {
            INFO("interval " << k);
            const double t = onsetTime(draws[k]);
            const double next = onsetTime(draws[k + 1]);
            // The law, bit for bit, with the grain's own onset draw (S5).
            REQUIRE(next == t + 480.0 * (1.0 + 0.5 * (d * d * d) * onsetDraw(k)));
            const double interval = next - t;
            CHECK(interval >= 480.0 - bound - 1e-9);
            CHECK(interval <= 480.0 + bound + 1e-9);
            spread = spread || std::abs(interval - 480.0) > 0.5 * bound;
            sum += interval;
        }
        CHECK(spread);
        if (d == 1.0)
        {
            CHECK(std::abs(sum / 2000.0 - 480.0) <= 0.01 * 480.0);
        }
    }
}

TEST_CASE("held draws repeat within a refresh", "[engine][pitchsync]")
{
    gg::CloudParams p = pitchAt(100.0);
    p.spray = 0.3;
    p.stereo = 0.8;
    p.busSplit = 0.5;
    p.pitchSpreadSemitones = 2.0;
    p.sizeSpreadOctaves = 0.5;
    p.smearJitterSec = 0.05;
    p.smearDetuneSemitones = 0.3;
    p.refreshHz = 16.0; // 3,000 samples
    gg::CloudScheduler s = voiceScheduler(p);
    std::vector<gg::GrainDraw> draws;
    drawSpan(s, draws, 0, 28 * 480);
    REQUIRE(draws.size() == 28);
    const auto same = [](const gg::GrainDraw &a, const gg::GrainDraw &b) {
        return a.positionDraw == b.positionDraw && a.sprayOffsetSec == b.sprayOffsetSec &&
               a.rate == b.rate && a.durationSamples == b.durationSamples && a.pan == b.pan &&
               a.bus == b.bus;
    };
    for (std::size_t group = 0; group < 4; ++group)
    {
        const gg::GrainDraw &refresh = draws[7 * group];
        INFO("group " << group);
        CHECK_FALSE(refresh.held);
        CHECK(refresh.index == 7 * group);
        for (std::size_t i = 7 * group + 1; i < 7 * group + 7; ++i)
        {
            INFO("grain " << i);
            CHECK(draws[i].held);
            CHECK(draws[i].index == i);
            CHECK(same(draws[i], refresh));
        }
        if (group > 0)
        {
            // The next group draws anew, every field.
            const gg::GrainDraw &before = draws[7 * (group - 1)];
            CHECK(refresh.positionDraw != before.positionDraw);
            CHECK(refresh.sprayOffsetSec != before.sprayOffsetSec);
            CHECK(refresh.rate != before.rate);
            CHECK(refresh.durationSamples != before.durationSamples);
            CHECK(refresh.pan != before.pan);
        }
    }
    // The bus is drawn too: across the four groups both buses appear.
    bool bus0 = false;
    bool bus1 = false;
    for (std::size_t group = 0; group < 4; ++group)
    {
        bus0 = bus0 || draws[7 * group].bus == 0;
        bus1 = bus1 || draws[7 * group].bus == 1;
    }
    CHECK(bus0);
    CHECK(bus1);
    // A refresh draws with its own index, as a Free grain would: k = 7's position.
    const double u = gg::rng::bipolar(
        gg::rng::hash({kSeed, kLayer, kStream, gg::rng::Field::PositionJitter, 7}));
    CHECK(draws[7].positionDraw == std::clamp(0.4 + 0.3 * u, 0.0, 1.0));
}

TEST_CASE("the refresh follows the rate in effect", "[engine][pitchsync]")
{
    gg::CloudScheduler s = voiceScheduler(pitchAt(100.0));
    std::vector<gg::GrainDraw> draws;
    drawSpan(s, draws, 0, 4800); // grains 0 to 9 at 16 Hz: refreshes at 0 and 7
    REQUIRE(draws.size() == 10);
    CHECK_FALSE(draws[0].held);
    CHECK(draws[1].held);
    CHECK_FALSE(draws[7].held);
    CHECK(draws[9].held);
    gg::CloudParams fast = pitchAt(100.0);
    fast.refreshHz = 400.0; // 120 samples, under a period
    s.setParams(fast);
    drawSpan(s, draws, 4800, 4800 + 50 * 480);
    REQUIRE(draws.size() == 60);
    for (std::size_t i = 10; i < draws.size(); ++i)
    {
        INFO("grain " << i);
        CHECK(draws[i].onsetSample == static_cast<int64_t>(480 * i)); // no rescale
        CHECK_FALSE(draws[i].held);
    }
}

TEST_CASE("a clock change forces an onset and a refresh", "[engine][pitchsync]")
{
    gg::CloudParams freeSlow = pitchAt(0.25);
    freeSlow.clock = gg::Clock::Free; // 0.25 Hz: the onset after 0 is pending 4 s away
    gg::CloudScheduler s = voiceScheduler(freeSlow);
    std::vector<gg::GrainDraw> draws;
    drawSpan(s, draws, 0, 3200);
    REQUIRE(draws.size() == 1);
    REQUIRE(s.pendingOnset() == 192000.0);

    s.setParams(pitchAt(100.0));
    drawSpan(s, draws, 3200, 6400);
    REQUIRE(draws.size() == 8); // 3200, 3680, ... 6560 is past the block
    CHECK(draws[1].onsetSample == 3200);
    CHECK(draws[1].onsetFrac == 0.0);
    CHECK(draws[1].index == 1);
    CHECK_FALSE(draws[1].held);
    CHECK(draws[2].onsetSample == 3680);
    CHECK(draws[2].held);

    // Back to Free: the onset pending at 6560 moves to the boundary, then 4 s on.
    s.setParams(freeSlow);
    drawSpan(s, draws, 6400, 9600);
    REQUIRE(draws.size() == 9);
    CHECK(draws[8].onsetSample == 6400);
    CHECK_FALSE(draws[8].held);
    CHECK(s.pendingOnset() == 6400.0 + 192000.0);

    // And to Pitch again: a fresh refresh, keyed by its own index.
    s.setParams(pitchAt(100.0));
    drawSpan(s, draws, 9600, 9600 + 480);
    REQUIRE(draws.size() == 10);
    CHECK(draws[9].onsetSample == 9600);
    CHECK_FALSE(draws[9].held);
}

TEST_CASE("a pitch slide rescales the pending onset", "[engine][pitchsync]")
{
    // At distribution 0.5 grain 2 can fall as late as 1020, so that slide is at 1100.
    struct Row
    {
        double d;
        int64_t boundary;
    };
    for (const Row row : {Row{0.0, 1000}, Row{0.5, 1100}})
    {
        const double d = row.d;
        INFO("distribution " << d);
        gg::CloudParams p = pitchAt(100.0);
        p.distribution = d;
        gg::CloudScheduler s = voiceScheduler(p);
        std::vector<gg::GrainDraw> draws;
        drawSpan(s, draws, 0, row.boundary, 1000);
        REQUIRE(draws.size() == 3); // 0, about 480 and about 960
        gg::CloudParams slid = p;
        slid.densityHz = 200.0; // P = 240
        s.setParams(slid);
        drawSpan(s, draws, row.boundary, 2000, 1000);
        REQUIRE(draws.size() >= 4);
        // Ruling M1-h with the Pitch law (SB3): from grain 2's onset, with grain 2's draw.
        const double expected =
            std::max(onsetTime(draws[2]) + 240.0 * (1.0 + 0.5 * (d * d * d) * onsetDraw(2)),
                     static_cast<double>(row.boundary));
        CHECK(onsetTime(draws[3]) == expected);
        if (d == 0.0)
        {
            CHECK(onsetTime(draws[3]) == 1200.0);
        }
        // Each grain is N periods of the f0 at its onset.
        CHECK(draws[2].durationSamples == 0.15 * gg::CloudScheduler::kPeriodsPerSizeSecond * 480.0);
        CHECK(draws[3].durationSamples == 0.15 * gg::CloudScheduler::kPeriodsPerSizeSecond * 240.0);
    }
}

TEST_CASE("pitchsync scheduler scales with the sample rate", "[engine][pitchsync]")
{
    // Review Focus 1: the f0 ceiling is sampleRate / 4, and the refresh period sampleRate / 16.
    // S2: 20 kHz clamps only at 44.1 and 48 kHz (ceilings 11,025 and 12,000 Hz; 24 and 48 kHz
    // at 96 and 192), so the clamp is tested with an f0 above every rate's ceiling, and 20 kHz
    // checks its own period, max(4, rate / 20000).
    struct Row
    {
        double rate;
        uint64_t firstRefresh; // the first k with 4 k >= rate / 16
        double periodAt20k;    // max(4, rate / 20000)
    };
    for (const Row row :
         {Row{44100.0, 690, 4.0}, Row{96000.0, 1500, 4.8}, Row{192000.0, 3000, 9.6}})
    {
        INFO("sample rate " << row.rate);
        gg::CloudParams at20k = pitchAt(20000.0, 0.005);
        gg::CloudScheduler u = voiceScheduler(at20k, row.rate);
        std::vector<gg::GrainDraw> lattice;
        drawSpan(u, lattice, 0, 100, 100);
        REQUIRE(lattice.size() >= 2);
        CHECK(onsetTime(lattice[1]) == row.periodAt20k);

        gg::CloudParams p = pitchAt(1.0e6, 0.005);
        gg::CloudScheduler s = voiceScheduler(p, row.rate);
        std::vector<gg::GrainDraw> draws;
        const auto frames = static_cast<int64_t>(4 * (row.firstRefresh + 10));
        drawSpan(s, draws, 0, frames, 512);
        REQUIRE(draws.size() == row.firstRefresh + 10);
        for (std::size_t k = 0; k < draws.size(); ++k)
        {
            INFO("grain " << k);
            REQUIRE(draws[k].onsetSample == static_cast<int64_t>(4 * k)); // P = 4 at every rate
            REQUIRE(draws[k].held == (k != 0 && k != row.firstRefresh));
        }
        // The length in periods scales too: 0.005 s x 40/3 x 4 samples is under 16.
        CHECK(draws[0].durationSamples == 16.0);
        gg::CloudParams two = pitchAt(100.0, 0.15);
        gg::CloudScheduler t = voiceScheduler(two, row.rate);
        std::vector<gg::GrainDraw> one;
        drawSpan(t, one, 0, 64, 64);
        REQUIRE(one.size() == 1);
        CHECK(one[0].durationSamples ==
              0.15 * gg::CloudScheduler::kPeriodsPerSizeSecond * (row.rate / 100.0));
    }
}

TEST_CASE("restartRefresh makes the next grain a refresh", "[engine][pitchsync]")
{
    gg::CloudParams p = pitchAt(100.0);
    p.spray = 0.3;
    p.stereo = 0.8;
    p.refreshHz = 0.25; // 4 s: every grain after the first is held
    gg::CloudScheduler s = voiceScheduler(p);
    std::vector<gg::GrainDraw> draws;
    drawSpan(s, draws, 0, 4800);
    REQUIRE(draws.size() == 10);
    for (std::size_t i = 1; i < 10; ++i)
    {
        CHECK(draws[i].held);
    }
    s.restartRefresh();
    drawSpan(s, draws, 4800, 9600);
    REQUIRE(draws.size() == 20);
    CHECK(draws[10].onsetSample == 4800); // the schedule doesn't move
    CHECK_FALSE(draws[10].held);
    CHECK(draws[10].positionDraw != draws[0].positionDraw);
    CHECK(draws[10].pan != draws[0].pan);
    for (std::size_t i = 11; i < 20; ++i)
    {
        CHECK(draws[i].held);
        CHECK(draws[i].positionDraw == draws[10].positionDraw);
    }
    // reset() does the same.
    s.reset();
    std::vector<gg::GrainDraw> again;
    drawSpan(s, again, 0, 960);
    REQUIRE(again.size() == 2);
    CHECK_FALSE(again[0].held);
    CHECK(again[0] == draws[0]);
    CHECK(again[1].held);
}

// The held anchor on the ring (M3 pitch-sync spec §4.6, S4, SB4, SB5, SB7, SB8): between
// refreshes a voice at Clock Pitch over a ring keeps reading the ring time its refresh started at.

namespace
{
constexpr int64_t kRingStart = 48000; // a second of input before the voice
constexpr double kM = static_cast<double>(gg::LiveSource::kHeadMargin);

// A 1 kHz stereo sine at 0.5 from sineDet (no libm), the right channel a quarter turn ahead.
struct RingInput
{
    std::vector<float> left;
    std::vector<float> right;
};

RingInput ringInput(int64_t frames)
{
    RingInput in{std::vector<float>(static_cast<std::size_t>(frames)),
                 std::vector<float>(static_cast<std::size_t>(frames))};
    for (int64_t i = 0; i < frames; ++i)
    {
        const auto at = static_cast<std::size_t>(i);
        const auto l = static_cast<float>(static_cast<double>((i * 1000) % 48000) / 48000.0);
        const auto r =
            static_cast<float>(static_cast<double>((i * 1000 + 12000) % 48000) / 48000.0);
        in.left[at] = 0.5f * gg::sineDet(l);
        in.right[at] = 0.5f * gg::sineDet(r);
    }
    return in;
}

// Each ring grain with ρ and its admission clamp [lo, hi] as Engine::liveGrain computes them, read
// from the ring inside the sink (the block's write is done by then).
struct RingLog
{
    static constexpr std::size_t kCapacity = 8192;
    const gg::Engine *engine{nullptr};
    std::vector<gg::GrainSpec> grains;
    std::vector<double> rho;
    std::vector<double> lo;
    std::vector<double> hi;
    bool overflowed{false};

    RingLog()
    {
        grains.reserve(kCapacity);
        rho.reserve(kCapacity);
        lo.reserve(kCapacity);
        hi.reserve(kCapacity);
    }

    static void record(void *ctx, uint32_t, const gg::GrainSpec &g)
    {
        auto *log = static_cast<RingLog *>(ctx);
        if (log->grains.size() >= kCapacity)
        {
            log->overflowed = true;
            return;
        }
        const gg::LiveSource &ring = g.sourceSlot == gg::Engine::kSpectralSlot
                                         ? log->engine->spectral()->ring()
                                         : log->engine->live();
        const int64_t t0 = g.onsetSample;
        const double run = ring.anchorRunAfter(t0);
        const double step = std::min(g.onsetFrac, run);
        const double rho = static_cast<double>(ring.anchorAt(t0)) + step;
        const double dur = g.durationSamples;
        const double span = std::min(dur, run - step);
        const double hi = rho - 3.0 - kM - std::max(0.0, dur * g.rate - span);
        double lo = rho - static_cast<double>(ring.windowFramesAt(t0)) + 1.0 + kM +
                    std::max(0.0, span * (1.0 - g.rate));
        const double runOldest = ring.oldestRunAfter(t0);
        if (runOldest != run)
        {
            const double stepOldest = std::min(g.onsetFrac, runOldest);
            const double spanOldest = std::min(dur, runOldest - stepOldest);
            lo = static_cast<double>(ring.windowOldestAt(t0)) + stepOldest + 1.0 + kM +
                 std::max(0.0, spanOldest * (1.0 - g.rate));
        }
        log->grains.push_back(g);
        log->rho.push_back(rho);
        log->lo.push_back(lo);
        log->hi.push_back(hi);
    }
};

// One engine over a ring (live, 4 s; the smear's too), in voice mode, fed ringInput.
struct RingRun
{
    static gg::EngineConfig ringConfig()
    {
        gg::EngineConfig c = config();
        c.liveSeconds = 4.0;
        c.spectral = true;
        return c;
    }

    gg::Engine engine{ringConfig()};
    RingInput input;
    Audio audio;
    RingLog log;
    int64_t done{0};

    RingRun(gg::SourceMode mode, int64_t frames)
        : input(ringInput(frames)), audio(silentAudio(frames))
    {
        log.engine = &engine;
        engine.setSourceMode(mode);
        engine.setEventSink(&RingLog::record, &log);
        engine.setVoiceMode(true);
    }

    void renderTo(int64_t to, uint32_t block = 256)
    {
        while (done < to)
        {
            const auto n = static_cast<uint32_t>(std::min<int64_t>(block, to - done));
            const auto at = static_cast<std::size_t>(done);
            const float *in[2] = {input.left.data() + at, input.right.data() + at};
            float *out[2] = {audio.left.data() + at, audio.right.data() + at};
            engine.process(in, 2, out, 2, n);
            done += n;
        }
    }
};

// A voice at Clock Pitch over a ring: f0 100 Hz (P = 480), 2 periods, Position 0.3 s, refreshing
// at 4 Hz (every 12,000 samples, 25 grains).
gg::CloudParams ringPitch(double spray, double refreshHz = 4.0)
{
    gg::CloudParams p = pitchAt(100.0);
    p.position = 0.3;
    p.spray = spray;
    p.stereo = 0.8;
    p.refreshHz = refreshHz;
    return p;
}

// The pan grain k of the voice of ordinal 0 draws with its own index.
float ownPan(uint64_t k, double stereo)
{
    const double p =
        stereo * gg::rng::bipolar(gg::rng::hash({kSeed, kLayer, kStream, gg::rng::Field::Pan, k}));
    return std::abs(p) < std::numeric_limits<float>::min() ? 0.0f : static_cast<float>(p);
}

// Grains in groups of `group` from 0: each refresh starts at ρ less its draw, each held grain
// at its refresh's start, all on `slot`.
void checkGroups(const RingLog &log, std::size_t group, uint8_t slot)
{
    for (std::size_t i = 0; i < log.grains.size(); ++i)
    {
        INFO("grain " << i);
        const gg::GrainSpec &g = log.grains[i];
        const gg::GrainSpec &refresh = log.grains[i - i % group];
        CHECK(g.sourceSlot == slot);
        CHECK(g.onsetSample == kRingStart + static_cast<int64_t>(480 * i));
        CHECK(g.pan == refresh.pan);
        CHECK(g.startPos == refresh.startPos);
        CHECK(g.startPos >= log.lo[i]);
        CHECK(g.startPos <= log.hi[i]);
    }
}
} // namespace

TEST_CASE("held grains keep the refresh's ring start", "[engine][pitchsync]")
{
    constexpr int64_t kFrames = kRingStart + 100 * 480;
    for (const double spray : {0.5, 0.0})
    {
        INFO("spray " << spray);
        RingRun run(gg::SourceMode::Live, kFrames);
        run.renderTo(kRingStart);
        run.engine.startVoice(0, 0, voiceAt(ringPitch(spray)));
        run.renderTo(kFrames);
        REQUIRE_FALSE(run.log.overflowed);
        REQUIRE(run.log.grains.size() == 100);
        REQUIRE(run.engine.droppedGrains() == 0);
        checkGroups(run.log, 25, gg::Engine::kLiveSlot);
        for (std::size_t j = 1; j < 4; ++j)
        {
            INFO("refresh " << j);
            const gg::GrainSpec &now = run.log.grains[25 * j];
            const gg::GrainSpec &before = run.log.grains[25 * (j - 1)];
            if (spray == 0.0)
            {
                // A refresh's start is the head's less its own draw, as a Free grain's.
                CHECK(now.startPos == run.log.rho[25 * j] - 0.3 * kSampleRate);
                CHECK(now.startPos == before.startPos + 12000.0);
            }
            else
            {
                CHECK(now.pan != before.pan);
                CHECK(now.startPos != before.startPos + 12000.0);
            }
        }
    }
}

TEST_CASE("a spectral voice holds its anchor on the smear's ring", "[engine][pitchsync]")
{
    constexpr int64_t kFrames = kRingStart + 100 * 480;
    RingRun run(gg::SourceMode::Spectral, kFrames);
    run.renderTo(kRingStart);
    run.engine.startVoice(0, 0, voiceAt(ringPitch(0.5)));
    run.renderTo(kFrames);
    REQUIRE_FALSE(run.log.overflowed);
    REQUIRE(run.log.grains.size() == 100);
    REQUIRE(run.engine.droppedGrains() == 0);
    checkGroups(run.log, 25, gg::Engine::kSpectralSlot);
    for (std::size_t j = 1; j < 4; ++j)
    {
        CHECK(run.log.grains[25 * j].startPos != run.log.grains[25 * (j - 1)].startPos + 12000.0);
    }
}

TEST_CASE("sample voices and free voices keep no anchor", "[engine][pitchsync]")
{
    // SB5: a Sample layer's held grains repeat their draws but read Position per grain.
    {
        gg::Engine engine(config());
        engine.setSource(sineSource());
        GrainLog log;
        engine.setEventSink(&GrainLog::record, &log);
        engine.setVoiceMode(true);
        gg::CloudParams p = pitchAt(100.0);
        p.spray = 0.1;
        p.refreshHz = 0.25; // every grain after the first is held
        engine.startVoice(0, 0, voiceAt(p));
        Audio audio = silentAudio(9600);
        renderSpan(engine, audio, 0, 4800, 256);
        p.position = 0.6;
        engine.setVoiceParams(0, voiceAt(p));
        CHECK(engine.voiceParams(0).cloud.position == 0.6);
        renderSpan(engine, audio, 4800, 9600, 256);
        REQUIRE(log.grains.size() == 20);
        for (std::size_t i = 1; i < 10; ++i)
        {
            CHECK(log.grains[i].startPos == log.grains[0].startPos);
        }
        CHECK(log.grains[10].startPos != log.grains[9].startPos);
        for (std::size_t i = 11; i < 20; ++i)
        {
            CHECK(log.grains[i].startPos == log.grains[10].startPos);
            CHECK(log.grains[i].pan == log.grains[0].pan);
        }
    }
    // Free voices over the ring: pinned on the code before the anchor (Task 3, Step 1).
    {
        constexpr int64_t kFrames = kRingStart + 48000;
        GrainLog log;
        RingRun run(gg::SourceMode::Live, kFrames);
        run.engine.setEventSink(&GrainLog::record, &log);
        run.renderTo(kRingStart);
        for (uint32_t k = 0; k < 3; ++k)
        {
            run.engine.startVoice(2 * k, k, voiceAt(kParams));
        }
        run.renderTo(kFrames);
        REQUIRE_FALSE(log.overflowed);
        REQUIRE(log.grains.size() > 60);
        CHECK(hashGrains(log) == kFreeRingVoicesHash);
    }
}

TEST_CASE("a source mode change drops the anchors", "[engine][pitchsync]")
{
    constexpr int64_t kSwitch = kRingStart + 10 * 480 + 100;
    constexpr int64_t kFrames = kRingStart + 20 * 480;
    RingRun run(gg::SourceMode::Live, kFrames);
    run.renderTo(kRingStart);
    run.engine.startVoice(0, 0, voiceAt(ringPitch(0.5, 0.25)));
    run.renderTo(kSwitch);
    run.engine.setSourceMode(gg::SourceMode::Spectral);
    run.renderTo(kFrames);
    REQUIRE(run.log.grains.size() == 20);
    const auto &grains = run.log.grains;
    for (std::size_t i = 0; i < 11; ++i)
    {
        INFO("grain " << i);
        CHECK(grains[i].sourceSlot == gg::Engine::kLiveSlot);
        CHECK(grains[i].pan == grains[0].pan);
        CHECK(grains[i].startPos == grains[0].startPos);
    }
    // Grain 11, the first after the switch, is a refresh with draws of its own.
    CHECK(grains[11].sourceSlot == gg::Engine::kSpectralSlot);
    CHECK(grains[11].pan == ownPan(11, 0.8));
    CHECK(grains[11].pan != grains[0].pan);
    for (std::size_t i = 12; i < 20; ++i)
    {
        INFO("grain " << i);
        CHECK(grains[i].sourceSlot == gg::Engine::kSpectralSlot);
        CHECK(grains[i].pan == grains[11].pan);
        CHECK(grains[i].startPos == grains[11].startPos);
    }
}

TEST_CASE("startVoice and reset drop the anchor", "[engine][pitchsync]")
{
    constexpr int64_t kRestart = kRingStart + 10 * 480;
    constexpr int64_t kFrames = kRestart + 10 * 480;
    gg::CloudParams first = ringPitch(0.0, 0.25);
    gg::CloudParams second = first;
    second.position = 0.5;

    RingRun run(gg::SourceMode::Live, kFrames);
    run.renderTo(kRingStart);
    run.engine.startVoice(3, 0, voiceAt(first));
    run.renderTo(kRestart);
    run.engine.startVoice(3, 0, voiceAt(second));
    run.renderTo(kFrames);
    REQUIRE(run.log.grains.size() == 20);
    const auto &grains = run.log.grains;
    for (std::size_t i = 0; i < 10; ++i)
    {
        CHECK(grains[i].startPos == run.log.rho[0] - 0.3 * kSampleRate);
    }
    // The restarted voice's first grain is a refresh from its own draw, not the old anchor.
    CHECK(grains[10].index == 0);
    CHECK(grains[10].onsetSample == kRestart);
    CHECK(grains[10].startPos == run.log.rho[10] - 0.5 * kSampleRate);
    for (std::size_t i = 11; i < 20; ++i)
    {
        CHECK(grains[i].startPos == grains[10].startPos);
    }

    // reset(): the same voice started again renders as on a fresh engine.
    RingRun fresh(gg::SourceMode::Live, kFrames);
    fresh.renderTo(kRingStart);
    fresh.engine.startVoice(3, 0, voiceAt(second));
    fresh.renderTo(kRestart);

    run.engine.reset();
    run.log.grains.clear();
    run.log.rho.clear();
    run.log.lo.clear();
    run.log.hi.clear();
    run.done = 0;
    run.renderTo(kRingStart);
    run.engine.startVoice(3, 0, voiceAt(second));
    run.renderTo(kRestart);
    REQUIRE(fresh.log.grains.size() == 10);
    CHECK(run.log.grains == fresh.log.grains);
}

TEST_CASE("a held anchor survives a ring shrink and a freeze toggle", "[engine][pitchsync]")
{
    // Review Focus 2: refreshing every 4 s, the first grain's anchor is held through a shrink to
    // a 1 s ring at 0.5 s, then a freeze from 1.5 s to 2 s; the hold outlives the window.
    constexpr int64_t kShrink = kRingStart + 24000;
    constexpr int64_t kFreezeOn = kRingStart + 72000;
    constexpr int64_t kFreezeOff = kRingStart + 96000;
    constexpr int64_t kFrames = kRingStart + 144000;
    RingRun run(gg::SourceMode::Live, kFrames);
    run.renderTo(kRingStart);
    run.engine.startVoice(0, 0, voiceAt(ringPitch(0.5, 0.25)));
    run.renderTo(kShrink, 100);
    run.engine.setRingLimit(48000);
    run.renderTo(kFreezeOn, 100);
    run.engine.setFreeze(true);
    run.renderTo(kFreezeOff, 100);
    run.engine.setFreeze(false);
    run.renderTo(kFrames, 100);
    REQUIRE_FALSE(run.log.overflowed);

    const auto &log = run.log;
    // Every draw is admitted inside its clamp, or dropped and counted.
    CHECK(log.grains.size() + run.engine.droppedGrains() == 300);
    std::size_t atAnchor = 0;
    std::size_t atLo = 0;
    for (std::size_t i = 0; i < log.grains.size(); ++i)
    {
        INFO("grain " << i << " at " << log.grains[i].onsetSample);
        const gg::GrainSpec &g = log.grains[i];
        CHECK(g.startPos >= log.lo[i]);
        CHECK(g.startPos <= log.hi[i]);
        CHECK(g.pan == log.grains[0].pan);
        if (g.onsetSample < kShrink)
        {
            CHECK(g.startPos == log.grains[0].startPos);
        }
        atAnchor += g.startPos == log.grains[0].startPos ? 1 : 0;
        atLo += g.startPos == log.lo[i] ? 1 : 0;
    }
    CHECK(atAnchor >= 50);
    CHECK(atLo > 50); // the hold outlived the 1 s window: the oldest admissible frame
    // Finite, and no click: a 0.5 sine at 1 kHz, gain 0.5, steps by at most about 0.033.
    float largest = 0.0f;
    for (const std::vector<float> *ch : {&run.audio.left, &run.audio.right})
    {
        for (std::size_t i = 1; i < ch->size(); ++i)
        {
            REQUIRE(std::isfinite((*ch)[i]));
            largest = std::max(largest, std::abs((*ch)[i] - (*ch)[i - 1]));
        }
    }
    CHECK(largest < 0.05f);
    CHECK(largest > 0.01f); // it sounds
}

namespace
{
// Counts admitted grains, the most sounding at once and whether an onset ever decreased. Every
// grain here is 16 samples at a whole onset.
struct DenseCount
{
    static constexpr std::size_t kRecent = 256;
    uint64_t count{0};
    int64_t lastOnset{std::numeric_limits<int64_t>::min()};
    bool decreased{false};
    uint32_t mostAtOnce{0};
    int64_t ends[kRecent]{};

    static void record(void *ctx, uint32_t, const gg::GrainSpec &g)
    {
        auto *c = static_cast<DenseCount *>(ctx);
        c->decreased = c->decreased || g.onsetSample < c->lastOnset;
        c->lastOnset = g.onsetSample;
        c->ends[c->count % kRecent] =
            g.onsetSample + static_cast<int64_t>(std::ceil(g.onsetFrac + g.durationSamples));
        ++c->count;
        uint32_t atOnce = 0;
        const auto recent = static_cast<std::size_t>(std::min<uint64_t>(c->count, kRecent));
        for (std::size_t i = 0; i < recent; ++i)
        {
            atOnce += c->ends[i] > g.onsetSample ? 1 : 0;
        }
        c->mostAtOnce = std::max(c->mostAtOnce, atOnce);
    }
};
} // namespace

TEST_CASE("top-note pitch voices fill the pool without overflow", "[engine][pitchsync]")
{
    gg::Engine engine(config());
    engine.setSource(sineSource());
    DenseCount count;
    engine.setEventSink(&DenseCount::record, &count);
    engine.setVoiceMode(true);
    const gg::CloudParams p = pitchAt(12000.0, 0.005); // P = 4, 16-sample grains, four deep
    for (uint32_t k = 0; k < gg::kMaxVoices; ++k)
    {
        engine.startVoice(k, k, voiceAt(p));
    }
    Audio audio = silentAudio(48000);
    renderSpan(engine, audio, 0, 48000, 256);
    CHECK(count.count >= 192000 - 16);
    CHECK(count.count <= 192000 + 16);
    CHECK(engine.droppedGrains() == 0);
    CHECK(count.mostAtOnce <= 64);
    CHECK(count.mostAtOnce >= 60);
    CHECK_FALSE(count.decreased);
}
