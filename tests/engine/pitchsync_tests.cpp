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
