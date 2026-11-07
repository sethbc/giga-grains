// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Voice streams in the engine (M3 Voice spec §4.3, V1, V4, B3): up to kMaxVoices streams a layer,
// each a CloudScheduler keyed by stream 1 + its ordinal, merged by onset (ties by ordinal) into
// the layer's one pool, each grain's gain scaled by its voice's amplitude at the grain's peak.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Rng.h"
#include "gg/engine/Engine.h"
#include "gg/grain/AmpEnvelope.h"
#include "gg/grain/Budget.h"
#include "gg/grain/CloudScheduler.h"
#include "gg/grain/GrainPool.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int64_t kSourceFrames = 4 * 48000; // 4 s
constexpr uint64_t kSeed = 11;
constexpr uint32_t kLayer = 2;

gg::EngineConfig config(uint32_t maxBlock = 4096)
{
    gg::EngineConfig c;
    c.sampleRate = kSampleRate;
    c.maxBlockSize = maxBlock;
    c.grainCapacity = 4096;
    c.seed = kSeed;
    c.layer = kLayer;
    return c;
}

// Exercises every draw a voice makes: spray, pitch spread, size spread, Smear's detune, the pan.
constexpr gg::CloudParams kParams{.densityHz = 41.0,
                                  .distribution = 0.6,
                                  .grainDurationSec = 0.06,
                                  .position = 0.4,
                                  .spray = 0.3,
                                  .pitchSemitones = 3.0,
                                  .stereo = 0.8,
                                  .gain = 0.6f,
                                  .pitchSpreadSemitones = 2.0,
                                  .sizeSpreadOctaves = 0.5,
                                  .smearDetuneSemitones = 0.3};

// An envelope that holds at 1: every voice grain keeps its draw's gain exactly.
gg::AmpEnvelope unityAmp()
{
    return {.at = 0,
            .level = 1.0,
            .stage = gg::AmpEnvelope::Stage::Sustain,
            .attackPerSample = 0.0,
            .decayPerSample = 0.0,
            .releasePerSample = 0.0,
            .sustain = 1.0};
}

gg::VoiceParams voiceAt(const gg::CloudParams &cloud)
{
    return {.cloud = cloud, .amp = unityAmp(), .gain = 1.0f};
}

// A mono 440 Hz sine at the output rate, so a grain's rate is its draw's.
std::shared_ptr<const gg::SampleBuffer> sineSource()
{
    auto buf = std::make_shared<gg::SampleBuffer>(1, kSourceFrames, kSampleRate);
    for (int64_t i = 0; i < kSourceFrames; ++i)
    {
        buf->channel(0)[i] =
            static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * i / kSampleRate));
    }
    return buf;
}

// Records spawned grains into preallocated storage: the sink runs inside process().
struct GrainLog
{
    static constexpr std::size_t kCapacity = 8192;
    std::vector<gg::GrainSpec> grains;
    bool overflowed{false};

    GrainLog() { grains.reserve(kCapacity); }

    static void record(void *ctx, uint32_t, const gg::GrainSpec &g)
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

struct Audio
{
    std::vector<float> left;
    std::vector<float> right;
};

// Renders [from, to) into r in calls of at most `block` frames.
void renderSpan(gg::Engine &engine, Audio &r, int64_t from, int64_t to, uint32_t block)
{
    for (int64_t t = from; t < to;)
    {
        const auto n = static_cast<uint32_t>(std::min<int64_t>(block, to - t));
        float *out[2] = {r.left.data() + t, r.right.data() + t};
        engine.process(out, 2, n);
        t += n;
    }
}

Audio silentAudio(int64_t frames)
{
    constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
    return {std::vector<float>(static_cast<std::size_t>(frames), kUnwritten),
            std::vector<float>(static_cast<std::size_t>(frames), kUnwritten)};
}

bool sameBits(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

float peak(const std::vector<float> &v, int64_t from, int64_t to)
{
    float p = 0.0f;
    for (int64_t i = from; i < to; ++i)
    {
        p = std::max(p, std::abs(v[static_cast<std::size_t>(i)]));
    }
    return p;
}

// Every grain a plain CloudScheduler at `stream` emits over [start, start + frames), with a
// forced onset at start: what a voice started at `start` should draw.
std::vector<gg::GrainDraw> cloudReference(const gg::CloudParams &params, uint32_t stream,
                                          int64_t start, int64_t frames)
{
    gg::CloudScheduler cloud;
    cloud.prepare(kSampleRate);
    cloud.setParams(params);
    cloud.forceNextOnset();
    std::vector<gg::GrainDraw> draws;
    cloud.schedule(start, static_cast<uint32_t>(frames), kSeed, kLayer, stream,
                   [&](const gg::GrainDraw &d) { draws.push_back(d); });
    return draws;
}

// The voice grain's gain (Engine.h): the draw's x the voice's gain x the amplitude at the peak
// x the headroom 1 / sqrt(n), n the layer's spawning voices (V7; n = 1 multiplies by exactly 1).
float voiceGain(const gg::GrainDraw &d, const gg::VoiceParams &v, uint32_t n = 1)
{
    const int64_t peakSample = d.onsetSample + static_cast<int64_t>(d.durationSamples * 0.5);
    return static_cast<float>(static_cast<double>(d.gain) * static_cast<double>(v.gain) *
                              v.amp.valueAt(peakSample) *
                              (1.0 / std::sqrt(static_cast<double>(n))));
}
} // namespace

TEST_CASE("the draw-at-a-time path reproduces schedule", "[engine][voice][cloud]")
{
    // beginRun / pendingOnset / takeNext are schedule() a grain at a time: the same draws, the
    // same forced onset and the same ruling M1-h rescale, here across a density change.
    gg::CloudScheduler a;
    gg::CloudScheduler b;
    for (gg::CloudScheduler *s : {&a, &b})
    {
        s->prepare(kSampleRate);
        s->setParams(kParams);
        s->forceNextOnset();
    }
    std::vector<gg::GrainDraw> viaSchedule;
    std::vector<gg::GrainDraw> viaTake;
    constexpr uint32_t kBlock = 300;
    for (int64_t t = 0; t < 48000; t += kBlock)
    {
        if (t == 24000)
        {
            gg::CloudParams p = kParams;
            p.densityHz = 130.0;
            a.setParams(p);
            b.setParams(p);
        }
        a.schedule(t, kBlock, kSeed, kLayer, 4,
                   [&](const gg::GrainDraw &d) { viaSchedule.push_back(d); });
        if (b.beginRun(t))
        {
            while (b.pendingOnset() < static_cast<double>(t + kBlock))
            {
                viaTake.push_back(b.takeNext(kSeed, kLayer, 4));
            }
        }
    }
    REQUIRE(viaSchedule.size() > 60);
    REQUIRE(viaTake == viaSchedule);
}

TEST_CASE("a voice draws with stream 1 plus its ordinal", "[engine][voice]")
{
    gg::Engine engine(config());
    engine.setSource(sineSource());
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    engine.setVoiceMode(true);
    constexpr int64_t kStart = 1000;
    constexpr int64_t kFrames = 3 * 48000;
    Audio audio = silentAudio(kFrames);
    renderSpan(engine, audio, 0, kStart, 256); // voice mode, no voice: nothing spawns
    REQUIRE(log.grains.empty());
    engine.startVoice(3, 5, voiceAt(kParams));
    REQUIRE(engine.voiceSpawning(3));
    renderSpan(engine, audio, kStart, kFrames, 256);
    REQUIRE_FALSE(log.overflowed);
    REQUIRE(log.grains.size() >= 50);

    const auto reference = cloudReference(kParams, 6, kStart, kFrames - kStart);
    REQUIRE(reference.size() >= 50);
    REQUIRE(reference.front().onsetSample == kStart); // the forced onset
    const double span = static_cast<double>(kSourceFrames - 1);
    for (std::size_t i = 0; i < 50; ++i)
    {
        INFO("grain " << i);
        const gg::GrainSpec &g = log.grains[i];
        const gg::GrainDraw &d = reference[i];
        CHECK(g.voice == 5);
        CHECK(g.index == d.index);
        CHECK(g.onsetSample == d.onsetSample);
        CHECK(g.onsetFrac == d.onsetFrac);
        CHECK(g.startPos == std::clamp(d.positionDraw * span, 0.0, span));
        CHECK(g.rate == d.rate * kSampleRate / kSampleRate);
        CHECK(g.durationSamples == d.durationSamples);
        CHECK(g.pan == d.pan);
        CHECK(g.gain == d.gain);
    }
    // Not vacuous: stream 1 (ordinal 0) and Cloud's stream 0 draw differently.
    for (const uint32_t other : {0u, 1u})
    {
        const auto elsewhere = cloudReference(kParams, other, kStart, kFrames - kStart);
        bool differs = false;
        for (std::size_t i = 0; i < 50 && i < elsewhere.size(); ++i)
        {
            differs = differs || elsewhere[i].positionDraw != reference[i].positionDraw ||
                      elsewhere[i].rate != reference[i].rate;
        }
        CHECK(differs);
    }
}

TEST_CASE("voices merge by onset, ties by ordinal", "[engine][voice]")
{
    gg::Engine engine(config());
    engine.setSource(sineSource());
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    engine.setVoiceMode(true);
    // Two periodic 100 Hz voices started together tie at every onset; a Poisson one interleaves.
    // The ordinals aren't in slot order, so a merge by slot would fail.
    gg::CloudParams periodic = kParams;
    periodic.densityHz = 100.0;
    periodic.distribution = 0.0;
    gg::CloudParams poisson = kParams;
    poisson.densityHz = 77.0;
    poisson.distribution = 1.0;
    engine.startVoice(0, 7, voiceAt(periodic));
    engine.startVoice(1, 3, voiceAt(poisson));
    engine.startVoice(2, 5, voiceAt(periodic));
    constexpr int64_t kFrames = 48000;
    Audio audio = silentAudio(kFrames);
    renderSpan(engine, audio, 0, kFrames, 777);
    REQUIRE_FALSE(log.overflowed);
    REQUIRE(log.grains.size() > 200);

    // The forced onsets all fall on frame 0: ordinals 3, 5, 7 in that order.
    REQUIRE(log.grains[0].voice == 3);
    REQUIRE(log.grains[1].voice == 5);
    REQUIRE(log.grains[2].voice == 7);
    int ties = 0;
    int counts[8]{};
    for (std::size_t i = 0; i < log.grains.size(); ++i)
    {
        const gg::GrainSpec &g = log.grains[i];
        REQUIRE((g.voice == 3 || g.voice == 5 || g.voice == 7));
        ++counts[g.voice];
        if (i == 0)
        {
            continue;
        }
        const gg::GrainSpec &prev = log.grains[i - 1];
        INFO("grain " << i);
        REQUIRE(g.onsetSample >= prev.onsetSample);
        if (g.onsetSample == prev.onsetSample && g.voice != prev.voice)
        {
            REQUIRE(g.voice > prev.voice);
            ++ties;
        }
    }
    CHECK(ties >= 99); // voices 5 and 7 tie at every one of their onsets
    CHECK(counts[3] > 50);
    CHECK(counts[5] == counts[7]);
}

TEST_CASE("a finished voice spawns nothing and its grains play out", "[engine][voice]")
{
    gg::Engine engine(config());
    engine.setSource(sineSource());
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    engine.setVoiceMode(true);
    gg::CloudParams p = kParams;
    p.sizeSpreadOctaves = 0.0;
    p.grainDurationSec = 0.05;
    p.densityHz = 40.0;
    engine.startVoice(4, 0, voiceAt(p));
    constexpr int64_t kFinish = 24000;
    constexpr int64_t kFrames = 36000;
    Audio audio = silentAudio(kFrames);
    renderSpan(engine, audio, 0, kFinish, 512);
    REQUIRE(engine.voiceSpawning(4));
    engine.finishVoice(4);
    REQUIRE_FALSE(engine.voiceSpawning(4));
    renderSpan(engine, audio, kFinish, kFrames, 512);

    REQUIRE(log.grains.size() > 10);
    int64_t lastEnd = 0;
    for (const gg::GrainSpec &g : log.grains)
    {
        REQUIRE(g.onsetSample < kFinish);
        lastEnd = std::max(lastEnd, gg::grainEndSample(g));
    }
    REQUIRE(lastEnd > kFinish);
    REQUIRE(lastEnd < kFrames);
    // The last grains sound past the finish, then the output is exactly 0.
    CHECK(peak(audio.left, kFinish, lastEnd) > 0.0f);
    CHECK(peak(audio.left, lastEnd, kFrames) == 0.0f);
    CHECK(peak(audio.right, lastEnd, kFrames) == 0.0f);
    CHECK(engine.activeGrains() == 0);
}

TEST_CASE("the amp envelope scales a grain at its peak", "[engine][voice]")
{
    gg::Engine engine(config());
    engine.setSource(sineSource());
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    engine.setVoiceMode(true);
    gg::CloudParams p = kParams;
    p.densityHz = 60.0;
    // Attack over 0.25 s from the start, decay over 0.25 s to 0.5; released at 0.75 s over 0.5 s.
    gg::VoiceParams v{.cloud = p,
                      .amp = {.at = 0,
                              .level = 0.0,
                              .stage = gg::AmpEnvelope::Stage::Attack,
                              .attackPerSample = 1.0 / 12000.0,
                              .decayPerSample = 0.5 / 12000.0,
                              .releasePerSample = 0.5 / 24000.0,
                              .sustain = 0.5},
                      .gain = 0.75f};
    engine.startVoice(0, 0, v);
    constexpr int64_t kRelease = 36000;
    constexpr int64_t kFrames = 72000;
    Audio audio = silentAudio(kFrames);
    renderSpan(engine, audio, 0, kRelease, 1000);
    gg::VoiceParams released = v;
    released.amp = {.at = kRelease,
                    .level = 0.5,
                    .stage = gg::AmpEnvelope::Stage::Release,
                    .attackPerSample = 1.0 / 12000.0,
                    .decayPerSample = 0.5 / 12000.0,
                    .releasePerSample = 0.5 / 24000.0,
                    .sustain = 0.5};
    engine.setVoiceParams(0, released);
    renderSpan(engine, audio, kRelease, kFrames, 1000);

    // The same voice's draws as a Cloud stream (stream 1), with the draw's gain.
    const auto reference = cloudReference(p, 1, 0, kFrames);
    REQUIRE(log.grains.size() > 30);
    REQUIRE(log.grains.size() <= reference.size());
    bool attacking = false;
    bool releasing = false;
    for (std::size_t i = 0; i < log.grains.size(); ++i)
    {
        INFO("grain " << i);
        const gg::GrainSpec &g = log.grains[i];
        const gg::GrainDraw &d = reference[i];
        REQUIRE(g.onsetSample == d.onsetSample);
        const gg::VoiceParams &at = g.onsetSample < kRelease ? v : released;
        CHECK(g.gain == voiceGain(d, at));
        CHECK(g.gain != d.gain);
        attacking = attacking || (g.onsetSample < 12000 && g.gain > 0.0f);
        releasing = releasing || (g.onsetSample > kRelease && g.gain > 0.0f &&
                                  g.gain < static_cast<float>(0.5 * 0.75) * d.gain);
    }
    // The forced grain at the strike speaks: its peak is past the onset.
    CHECK(log.grains.front().onsetSample == 0);
    CHECK(log.grains.front().gain > 0.0f);
    CHECK(attacking);
    CHECK(releasing);
}

TEST_CASE("AmpEnvelope valueAt is the linear ADSR", "[engine][voice]")
{
    using Stage = gg::AmpEnvelope::Stage;
    // Attack 1 / 100 per sample, decay 0.5 / 100 (1 to sustain 0.5 in 100 samples), release
    // 1 / 200 per sample.
    const auto env = [](Stage stage, double level, int64_t at = 1000) {
        return gg::AmpEnvelope{.at = at,
                               .level = level,
                               .stage = stage,
                               .attackPerSample = 0.01,
                               .decayPerSample = 0.005,
                               .releasePerSample = 0.005,
                               .sustain = 0.5};
    };
    struct Row
    {
        Stage stage;
        double level;
        int64_t t;
        double value;
    };
    const Row rows[] = {
        {Stage::Idle, 0.0, 1000, 0.0},
        {Stage::Idle, 0.7, 5000, 0.0},
        {Stage::Attack, 0.0, 1000, 0.0},
        {Stage::Attack, 0.0, 1050, 0.5},
        {Stage::Attack, 0.25, 1050, 0.75},
        {Stage::Attack, 0.0, 1100, 1.0},  // the attack-to-decay corner
        {Stage::Attack, 0.0, 1150, 0.75}, // half-way down the decay
        {Stage::Attack, 0.0, 1200, 0.5},  // the decay-to-sustain corner
        {Stage::Attack, 0.0, 99999, 0.5}, // held at sustain
        {Stage::Attack, 0.5, 1050, 1.0},  // from 0.5, the attack's corner is earlier
        {Stage::Attack, 0.5, 1100, 0.75},
        {Stage::Decay, 1.0, 1000, 1.0},
        {Stage::Decay, 0.9, 1020, 0.8},
        {Stage::Decay, 1.0, 1100, 0.5}, // the decay-to-sustain corner
        {Stage::Decay, 1.0, 1500, 0.5},
        {Stage::Sustain, 0.5, 1000, 0.5},
        {Stage::Sustain, 0.5, 9000000, 0.5},
        {Stage::Release, 0.5, 1000, 0.5},
        {Stage::Release, 0.5, 1050, 0.25},
        {Stage::Release, 0.5, 1100, 0.0}, // reaches 0
        {Stage::Release, 0.5, 5000, 0.0}, // and stays
        {Stage::Release, 0.5, 900, 0.5},  // before `at`: the level it starts from
    };
    for (const Row &row : rows)
    {
        INFO("stage " << static_cast<int>(row.stage) << ", level " << row.level << ", t " << row.t);
        const double value = env(row.stage, row.level).valueAt(row.t);
        CHECK(std::abs(value - row.value) <= 1e-12);
        CHECK(value >= 0.0);
        CHECK(value <= 1.0);
    }
    // Clamped to [0, 1] whatever it is given.
    CHECK(env(Stage::Sustain, 0.5).valueAt(1000) == 0.5);
    gg::AmpEnvelope wild = env(Stage::Attack, 3.0);
    CHECK(wild.valueAt(1000) <= 1.0);
    wild.sustain = -2.0;
    wild.stage = Stage::Sustain;
    CHECK(wild.valueAt(1000) == 0.0);
    gg::AmpEnvelope flat = env(Stage::Attack, 0.25);
    flat.attackPerSample = 0.0; // an attack that never rises holds its level
    CHECK(flat.valueAt(100000) == 0.25);
}

TEST_CASE("voice mode off schedules exactly the Cloud stream", "[engine][voice]")
{
    constexpr int64_t kFrames = 2 * 48000;
    const auto source = sineSource();
    const auto run = [&](bool touchVoices, GrainLog &log) {
        gg::Engine engine(config());
        engine.setSource(source);
        engine.setEventSink(&GrainLog::record, &log);
        engine.setCloudParams(kParams);
        if (touchVoices)
        {
            engine.setVoiceMode(false);
            engine.clearVoices();
        }
        Audio audio = silentAudio(kFrames);
        renderSpan(engine, audio, 0, kFrames, 512);
        return audio;
    };
    GrainLog plainLog;
    GrainLog voiceLog;
    const Audio plain = run(false, plainLog);
    const Audio off = run(true, voiceLog);
    REQUIRE(peak(plain.left, 0, kFrames) > 0.05f);
    REQUIRE(plainLog.grains.size() > 50);
    CHECK(sameBits(plain.left, off.left));
    CHECK(sameBits(plain.right, off.right));
    CHECK(voiceLog.grains == plainLog.grains);
    for (const gg::GrainSpec &g : plainLog.grains)
    {
        REQUIRE(g.voice == gg::kNoVoice);
    }
}

TEST_CASE("voice mode on stops the Cloud stream", "[engine][voice]")
{
    // B3: at Mode Voice the layer's Cloud stream doesn't schedule.
    gg::Engine engine(config());
    engine.setSource(sineSource());
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    engine.setCloudParams(kParams);
    constexpr int64_t kSwitch = 12000;
    constexpr int64_t kFrames = 30000;
    Audio audio = silentAudio(kFrames);
    renderSpan(engine, audio, 0, kSwitch, 512);
    const std::size_t cloudGrains = log.grains.size();
    REQUIRE(cloudGrains > 5);
    engine.setVoiceMode(true);
    renderSpan(engine, audio, kSwitch, kFrames, 512);
    CHECK(log.grains.size() == cloudGrains);
    CHECK(peak(audio.left, kFrames - 1000, kFrames) == 0.0f);

    // Back to Cloud: the stream starts again at the switch's frame.
    engine.setVoiceMode(false);
    Audio more = silentAudio(kFrames);
    float *out[2] = {more.left.data(), more.right.data()};
    engine.process(out, 2, 4800);
    REQUIRE(log.grains.size() > cloudGrains);
    CHECK(log.grains[cloudGrains].onsetSample == kFrames);
    CHECK(log.grains[cloudGrains].voice == gg::kNoVoice);
}

TEST_CASE("setEnabled false and reset clear the voices", "[engine][voice]")
{
    gg::Engine engine(config());
    engine.setSource(sineSource());
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    engine.setVoiceMode(true);
    const auto startAll = [&] {
        for (uint32_t slot = 0; slot < gg::kMaxVoices; ++slot)
        {
            engine.startVoice(slot, slot, voiceAt(kParams));
        }
    };
    const auto noneSpawning = [&] {
        for (uint32_t slot = 0; slot < gg::kMaxVoices; ++slot)
        {
            if (engine.voiceSpawning(slot))
            {
                return false;
            }
        }
        return true;
    };
    Audio audio = silentAudio(48000);
    float *out[2] = {audio.left.data(), audio.right.data()};
    startAll();
    REQUIRE_FALSE(noneSpawning());
    engine.process(out, 2, 2000);
    REQUIRE(engine.activeGrains() > 0);

    // The gate off clears the voices with the pool, at its frame.
    engine.setEnabled(false);
    engine.process(out, 2, 2000);
    CHECK(noneSpawning());
    CHECK(engine.activeGrains() == 0);
    const std::size_t afterOff = log.grains.size();
    engine.setEnabled(true);
    engine.process(out, 2, 4000);
    CHECK(log.grains.size() == afterOff); // nothing comes back
    CHECK(peak(audio.left, 0, 4000) == 0.0f);

    // reset() clears them too.
    startAll();
    engine.process(out, 2, 2000);
    REQUIRE(log.grains.size() > afterOff);
    engine.reset();
    CHECK(noneSpawning());
    CHECK(engine.activeGrains() == 0);
    const std::size_t afterReset = log.grains.size();
    engine.process(out, 2, 4000);
    CHECK(log.grains.size() == afterReset);

    // clearVoices() alone, between calls.
    startAll();
    REQUIRE_FALSE(noneSpawning());
    engine.clearVoices();
    CHECK(noneSpawning());
    // A slot out of range is ignored.
    engine.startVoice(gg::kMaxVoices, 0, voiceAt(kParams));
    CHECK_FALSE(engine.voiceSpawning(gg::kMaxVoices));
}

TEST_CASE("voice headroom is one over root n", "[engine][voice]")
{
    for (const uint32_t n : {1u, 2u, 4u, 16u})
    {
        INFO("n " << n);
        gg::Engine engine(config());
        engine.setSource(sineSource());
        GrainLog log;
        engine.setEventSink(&GrainLog::record, &log);
        engine.setVoiceMode(true);
        gg::CloudParams p = kParams;
        p.densityHz = 12.0;
        for (uint32_t k = 0; k < n; ++k)
        {
            engine.startVoice(k, k, voiceAt(p));
        }
        constexpr int64_t kFrames = 48000;
        Audio audio = silentAudio(kFrames);
        renderSpan(engine, audio, 0, kFrames, 256);
        REQUIRE_FALSE(log.overflowed);
        REQUIRE(log.grains.size() >= 8u * n);
        for (uint32_t k = 0; k < n; ++k)
        {
            const auto reference = cloudReference(p, 1 + k, 0, kFrames);
            std::size_t seen = 0;
            for (const gg::GrainSpec &g : log.grains)
            {
                if (g.voice != k)
                {
                    continue;
                }
                REQUIRE(g.index < reference.size());
                CHECK(g.gain == voiceGain(reference[g.index], voiceAt(p), n));
                ++seen;
            }
            CHECK(seen >= 8);
        }
    }
}

TEST_CASE("a finishing voice raises the others' later grains", "[engine][voice]")
{
    gg::Engine engine(config());
    engine.setSource(sineSource());
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    engine.setVoiceMode(true);
    gg::CloudParams p = kParams;
    p.densityHz = 30.0;
    for (uint32_t k = 0; k < 3; ++k)
    {
        engine.startVoice(k, k, voiceAt(p));
    }
    constexpr int64_t kFinish = 24000;
    constexpr int64_t kFrames = 48000;
    Audio audio = silentAudio(kFrames);
    renderSpan(engine, audio, 0, kFinish, 500);
    engine.finishVoice(1);
    renderSpan(engine, audio, kFinish, kFrames, 500);
    const auto reference = cloudReference(p, 1, 0, kFrames);
    bool before = false;
    bool after = false;
    for (const gg::GrainSpec &g : log.grains)
    {
        if (g.voice == 1)
        {
            CHECK(g.onsetSample < kFinish);
        }
        if (g.voice != 0)
        {
            continue;
        }
        const bool late = g.onsetSample >= kFinish;
        CHECK(g.gain == voiceGain(reference[g.index], voiceAt(p), late ? 2u : 3u));
        before = before || !late;
        after = after || late;
    }
    CHECK(before);
    CHECK(after);
}

TEST_CASE("voice headroom is block-size invariant as n changes", "[engine][voice][determinism]")
{
    // n goes 1, 2, 3, 2 at samples that every block size below divides.
    const auto render = [](uint32_t block) {
        gg::Engine engine(config(4096));
        engine.setSource(sineSource());
        engine.setVoiceMode(true);
        gg::CloudParams p = kParams;
        p.densityHz = 60.0;
        constexpr int64_t kFrames = 32768;
        Audio audio = silentAudio(kFrames);
        engine.startVoice(0, 0, voiceAt(p));
        renderSpan(engine, audio, 0, 8192, block);
        engine.startVoice(1, 1, voiceAt(p));
        renderSpan(engine, audio, 8192, 16384, block);
        engine.startVoice(2, 2, voiceAt(p));
        renderSpan(engine, audio, 16384, 24576, block);
        engine.finishVoice(1);
        renderSpan(engine, audio, 24576, kFrames, block);
        return audio;
    };
    const Audio reference = render(4096);
    for (const uint32_t block : {64u, 256u, 1024u})
    {
        INFO("block " << block);
        const Audio other = render(block);
        CHECK(sameBits(other.left, reference.left));
        CHECK(sameBits(other.right, reference.right));
    }
}

// ---------------------------------------------------------------------------------------------
// Voice mode's steal order (M3 expression spec E9, EB6, EB7; the director's D4, INTENT §23 #81,
// applied per voice). The cases below draw Eco grains from a mono source, so every grain weighs
// grainWeight(Eco, mono) = 2 and the ladder has no tier to lower to: a draw that doesn't fit goes
// straight to rung 3. Their streams are periodic, so a voice started at a call's first frame s
// spawns at s, s + 48000 / density, and so on, and every grain lasts 1 s (48,000 samples), so it
// passes D4's half-window test 24,000 samples after its onset.

namespace
{
constexpr uint64_t kEco = gg::grainWeight(gg::Tier::Eco, false);
static_assert(kEco == 2);

// Spawns and steals, in the order the engine reports them, into preallocated storage.
struct OrderLog
{
    struct Event
    {
        bool steal;
        uint64_t index;
        uint32_t voice; // a spawn's GrainSpec::voice, a steal's stolen grain's voice
        int64_t onset;  // a spawn's onset sample, a steal's onset (the draw's)
        uint8_t slot;   // a spawn's GrainSpec::voiceSlot
        int64_t end;    // a spawn's grainEndSample
    };
    static constexpr std::size_t kCapacity = 65536;
    std::vector<Event> events;
    bool overflowed{false};

    OrderLog() { events.reserve(kCapacity); }

    void push(const Event &e)
    {
        if (events.size() < kCapacity)
        {
            events.push_back(e);
        }
        else
        {
            overflowed = true;
        }
    }
    static void spawn(void *ctx, uint32_t, const gg::GrainSpec &g)
    {
        static_cast<OrderLog *>(ctx)->push(
            {false, g.index, g.voice, g.onsetSample, g.voiceSlot, gg::grainEndSample(g)});
    }
    static void steal(void *ctx, uint32_t, uint64_t index, uint32_t voice, int64_t onset)
    {
        static_cast<OrderLog *>(ctx)->push({true, index, voice, onset, 0, 0});
    }
    std::vector<Event> steals() const
    {
        std::vector<Event> out;
        std::copy_if(events.begin(), events.end(), std::back_inserter(out),
                     [](const Event &e) { return e.steal; });
        return out;
    }
};

// Eco, periodic, 1 s grains of a 440 Hz sine read from 0.25 s in (the read ends 1.25 s in, inside
// the 4 s source), centered.
gg::CloudParams periodic(double densityHz, double durationSec = 1.0)
{
    gg::CloudParams p;
    p.densityHz = densityHz;
    p.distribution = 0.0;
    p.grainDurationSec = durationSec;
    p.position = 0.0625;
    p.spray = 0.0;
    p.pitchSemitones = 0.0;
    p.stereo = 0.0;
    p.gain = 0.25f;
    p.quality = gg::Tier::Eco;
    return p;
}

gg::VoiceParams voiceOf(double densityHz, bool weak = false)
{
    gg::VoiceParams v = voiceAt(periodic(densityHz));
    v.weak = weak;
    return v;
}

// An engine in voice mode over the mono sine, logging into `log`.
struct StealRig
{
    gg::Engine engine{config(4096)};
    OrderLog log;
    Audio audio = silentAudio(4 * 48000);

    StealRig()
    {
        engine.setSource(sineSource());
        engine.setEventSink(&OrderLog::spawn, &log);
        engine.setStealSink(&OrderLog::steal, &log);
        engine.setVoiceMode(true);
    }
    void to(int64_t from, int64_t until) { renderSpan(engine, audio, from, until, 512); }
};

bool isSteal(const OrderLog::Event &e, uint64_t index, uint32_t voice, int64_t onset)
{
    return e.steal && e.index == index && e.voice == voice && e.onset == onset;
}
} // namespace

TEST_CASE("per slot weights sum to the ledger", "[engine][voice][pool][budget]")
{
    // 3,000 grains of weights 1 to 12 and lengths 16 to 6,000 samples, in random stream slots
    // (0 to 16), onsets advancing by 0 to 9 samples; from the 300th on, a random sounding grain
    // is stolen at some onsets, and the pool is retired at some (as the engine retires at block
    // ends). At every onset, after soundingWeight, the slots' weights sum to it, each slot's is
    // its own grains' (ends moved by steals, fading ones included), and oldestSoundingOf is the
    // first unstolen sounding row of its slot, as a scan of the rows says.
    constexpr uint32_t kGrains = 3000;
    gg::GrainPool pool(kGrains);
    struct Model
    {
        uint64_t index;
        uint8_t slot;
        uint8_t weight;
        int64_t end;
        bool stolen;
    };
    std::vector<Model> model;
    const auto draw = [](uint32_t field, uint64_t k) {
        return gg::rng::hash({23, 0, field, gg::rng::Field::SignalNoise, k});
    };
    int64_t onset = 0;
    uint32_t steals = 0;
    uint64_t largest = 0;
    for (uint64_t k = 0; k < kGrains; ++k)
    {
        onset += static_cast<int64_t>(draw(0, k) % 10);
        INFO("grain " << k << " at onset " << onset);
        const uint64_t load = pool.soundingWeight(onset);
        // The scan: each slot's weight of grains still sounding and its first unstolen one.
        uint64_t own[gg::kStreamSlots]{};
        std::size_t first[gg::kStreamSlots];
        std::fill(std::begin(first), std::end(first), model.size());
        for (std::size_t m = 0; m < model.size(); ++m)
        {
            if (model[m].end > onset)
            {
                own[model[m].slot] += model[m].weight;
                if (!model[m].stolen && first[model[m].slot] == model.size())
                {
                    first[model[m].slot] = m;
                }
            }
        }
        uint64_t sum = 0;
        for (uint8_t s = 0; s < gg::kStreamSlots; ++s)
        {
            INFO("slot " << int{s});
            REQUIRE(pool.slotWeight(s) == own[s]);
            sum += pool.slotWeight(s);
            uint32_t row = 0;
            const bool found = pool.oldestSoundingOf(s, onset, row);
            REQUIRE(found == (first[s] != model.size()));
            if (found)
            {
                REQUIRE(pool.rows().index[row] == model[first[s]].index);
                REQUIRE(pool.rows().voiceSlot[row] == s);
            }
        }
        REQUIRE(sum == load);
        REQUIRE(pool.slotWeight(gg::kStreamSlots) == 0);
        uint32_t ignored = 0;
        REQUIRE_FALSE(pool.oldestSoundingOf(gg::kStreamSlots, onset, ignored));
        largest = std::max(largest, load);

        if (k >= 300 && pool.size() > 0 && draw(1, k) % 3 == 0)
        {
            // Steal a random sounding, unstolen grain, by its row.
            const uint64_t pick = draw(2, k) % pool.size();
            for (uint32_t r = 0; r < pool.size(); ++r)
            {
                const uint32_t row = static_cast<uint32_t>((pick + r) % pool.size());
                Model *const it = &model[pool.rows().index[row]]; // model[k] is grain k
                if (it->end > onset && !it->stolen)
                {
                    pool.steal(row, onset);
                    it->stolen = true;
                    it->end = std::min(it->end, onset + gg::kStealFade);
                    ++steals;
                    break;
                }
            }
        }
        if (draw(3, k) % 7 == 0)
        {
            pool.retireFinished(onset);
        }
        gg::GrainSpec g{.index = k,
                        .onsetSample = onset,
                        .onsetFrac = 0.0,
                        .startPos = 0.0,
                        .rate = 1.0,
                        .durationSamples = 16.0 + static_cast<double>(draw(4, k) % 5985),
                        .gain = 0.5f,
                        .pan = 0.0f};
        g.weight = static_cast<uint8_t>(1 + draw(5, k) % 12);
        g.voiceSlot = static_cast<uint8_t>(draw(6, k) % gg::kStreamSlots);
        g.voice = g.voiceSlot == gg::kCloudStreamSlot ? gg::kNoVoice : 100u + g.voiceSlot;
        REQUIRE(pool.spawn(g));
        REQUIRE(pool.rows().voice[pool.size() - 1] == g.voice);
        REQUIRE(pool.spec(pool.size() - 1) == g);
        model.push_back({k, g.voiceSlot, g.weight, gg::grainEndSample(g), false});
    }
    REQUIRE(steals > 500);
    REQUIRE(largest > 1000); // many grains overlapped
    // A slot out of range is stored as the Cloud stream's; clear() forgets every slot's weight.
    gg::GrainSpec stray{.index = 0,
                        .onsetSample = onset,
                        .onsetFrac = 0.0,
                        .startPos = 0.0,
                        .rate = 1.0,
                        .durationSamples = 100.0,
                        .gain = 0.5f,
                        .pan = 0.0f};
    stray.voiceSlot = 200;
    pool.clear();
    REQUIRE(pool.spawn(stray));
    REQUIRE(pool.rows().voiceSlot[0] == gg::kCloudStreamSlot);
    REQUIRE(pool.slotWeight(gg::kCloudStreamSlot) == stray.weight);
    for (uint8_t s = 0; s < gg::kCloudStreamSlot; ++s)
    {
        REQUIRE(pool.slotWeight(s) == 0);
    }
}

TEST_CASE("an orphan slot's grain is stolen first", "[engine][voice][budget]")
{
    // H (slot 0) spawns at 0, O (slot 1) at 6,000 and finishes at 12,000, D (slot 2) at 12,000.
    // At 30,000, with the quota at the three grains' 6 units, E (slot 3) draws: O's grain, the
    // orphan's, has sounded exactly half its window and is stolen, though H's is older (the
    // shared rule's victim).
    StealRig rig;
    rig.engine.startVoice(0, 0, voiceOf(0.5));
    rig.to(0, 6000);
    rig.engine.startVoice(1, 1, voiceOf(0.5));
    rig.to(6000, 12000);
    rig.engine.finishVoice(1);
    rig.engine.startVoice(2, 2, voiceOf(0.5));
    rig.to(12000, 30000);
    rig.engine.setQuota(3 * kEco);
    rig.engine.startVoice(3, 3, voiceOf(0.5));
    rig.to(30000, 31000);

    REQUIRE_FALSE(rig.log.overflowed);
    const auto steals = rig.log.steals();
    REQUIRE(steals.size() == 1);
    CHECK(isSteal(steals[0], 0, 1, 30000));
    CHECK(rig.engine.droppedGrains() == 0);
    // The spawn it made room for follows it: E's first grain, in slot 3.
    const auto &events = rig.log.events;
    const auto at = std::find_if(events.begin(), events.end(),
                                 [](const OrderLog::Event &e) { return e.steal; });
    REQUIRE(at + 1 != events.end());
    CHECK((!at[1].steal && at[1].voice == 3 && at[1].slot == 3 && at[1].onset == 30000));
}

TEST_CASE("a reused slot's old grains count with its new voice", "[engine][voice][budget]")
{
    // EB6: A (slot 0, ordinal 0) spawns at 0 and finishes; B reuses slot 0 (ordinal 1) at 6,000.
    // A's grain is then B's slot's, not an orphan's. C (slot 1) spawns at 0, 12,000 and 24,000,
    // the heaviest held slot (6 units against slot 0's 4), so when D (slot 2) draws at 30,000
    // under a quota of the five grains' 10 units, C's oldest grain is stolen, not A's.
    StealRig rig;
    rig.engine.startVoice(0, 0, voiceOf(0.5));
    rig.engine.startVoice(1, 2, voiceOf(4.0));
    rig.to(0, 1000);
    rig.engine.finishVoice(0);
    rig.to(1000, 6000);
    rig.engine.startVoice(0, 1, voiceOf(0.5));
    rig.to(6000, 30000);
    rig.engine.setQuota(5 * kEco);
    rig.engine.startVoice(2, 3, voiceOf(0.5));
    rig.to(30000, 30001);

    REQUIRE_FALSE(rig.log.overflowed);
    const auto steals = rig.log.steals();
    REQUIRE(steals.size() == 1);
    CHECK(isSteal(steals[0], 0, 2, 30000));
}

TEST_CASE("the heaviest weak voice yields before a held one", "[engine][voice][budget]")
{
    // H (slot 0, held) spawns every 12,000 samples, W1 (slot 1) once at 0, W2 (slot 2) every
    // 24,000. At 30,000 W1 and W2 turn weak and the quota becomes the 12 units sounding at
    // 36,000, where H draws: no orphan, so the weak voices come first, the heavier W2 (4 units,
    // against W1's 2 and H's 6) before W1. W2's oldest grain is stolen.
    StealRig rig;
    rig.engine.startVoice(0, 0, voiceOf(4.0));
    rig.engine.startVoice(1, 1, voiceOf(0.5));
    rig.engine.startVoice(2, 2, voiceOf(2.0));
    rig.to(0, 30000);
    rig.engine.setVoiceParams(1, voiceOf(0.5, true));
    rig.engine.setVoiceParams(2, voiceOf(2.0, true));
    rig.engine.setQuota(6 * kEco);
    rig.to(30000, 36001);

    REQUIRE_FALSE(rig.log.overflowed);
    const auto steals = rig.log.steals();
    REQUIRE(steals.size() == 1);
    CHECK(isSteal(steals[0], 0, 2, 36000));

    SECTION("a tie goes to the lower ordinal")
    {
        // W1 (ordinal 4, slot 2) and W2 (ordinal 5, slot 1) weigh 4 units each; the lower
        // ordinal yields first, though its slot is the higher.
        StealRig tie;
        tie.engine.startVoice(0, 0, voiceOf(4.0));
        tie.engine.startVoice(2, 4, voiceOf(2.0));
        tie.engine.startVoice(1, 5, voiceOf(2.0));
        tie.to(0, 30000);
        tie.engine.setVoiceParams(1, voiceOf(2.0, true));
        tie.engine.setVoiceParams(2, voiceOf(2.0, true));
        tie.engine.setQuota(7 * kEco);
        tie.to(30000, 36001);
        REQUIRE_FALSE(tie.log.overflowed);
        const auto tied = tie.log.steals();
        REQUIRE(tied.size() == 1);
        CHECK(isSteal(tied[0], 0, 4, 36000));
    }
}

TEST_CASE("the heaviest held voice yields when none is weak", "[engine][voice][budget]")
{
    // H1 (slot 0) spawns at 0 and 24,000 (4 units), H2 (slot 1) at 0, 12,000 and 24,000 (6
    // units). At 30,000 the quota becomes their 10 units and H3 (slot 2) draws: no orphan, no weak
    // voice, so the heavier held voice, H2, yields its oldest grain.
    StealRig rig;
    rig.engine.startVoice(0, 0, voiceOf(2.0));
    rig.engine.startVoice(1, 1, voiceOf(4.0));
    rig.to(0, 30000);
    rig.engine.setQuota(5 * kEco);
    rig.engine.startVoice(2, 2, voiceOf(0.5));
    rig.to(30000, 30001);

    REQUIRE_FALSE(rig.log.overflowed);
    const auto steals = rig.log.steals();
    REQUIRE(steals.size() == 1);
    CHECK(isSteal(steals[0], 0, 1, 30000));
}

TEST_CASE("the drawing voice may yield its own grain", "[engine][voice][budget]")
{
    // H1 (slot 0) spawns once at 0 (2 units), H2 (slot 1) at 0, 12,000 and 24,000 (6 units).
    // At 36,000 H2 draws under a quota of the 8 units sounding: the heaviest held voice is the
    // drawer itself, and its own oldest grain makes room for its new one.
    StealRig rig;
    rig.engine.startVoice(0, 0, voiceOf(0.5));
    rig.engine.startVoice(1, 1, voiceOf(4.0));
    rig.to(0, 30000);
    rig.engine.setQuota(4 * kEco);
    rig.to(30000, 36001);

    REQUIRE_FALSE(rig.log.overflowed);
    const auto &events = rig.log.events;
    const auto at = std::find_if(events.begin(), events.end(),
                                 [](const OrderLog::Event &e) { return e.steal; });
    REQUIRE(at != events.end());
    CHECK(isSteal(*at, 0, 1, 36000));
    REQUIRE(at + 1 != events.end());
    CHECK((!at[1].steal && at[1].voice == 1 && at[1].index == 3 && at[1].onset == 36000));
    CHECK(rig.log.steals().size() == 1);
}

TEST_CASE("a candidate under half its window is passed over", "[engine][voice][budget]")
{
    // The Cloud stream spawns one 2 s grain at 0 (D4's half is 48,000 samples), then voice mode
    // turns on at 6,000 and H (slot 0) spawns a 1 s grain there. At 30,000, under a quota of the
    // two grains' 4 units, H2 (slot 1) draws. The first candidate, the Cloud stream's orphan
    // (kCloudStreamSlot), has sounded 30,000 samples, under half its window, and is passed over;
    // H's grain, at exactly half, is stolen. (The shared rule would test only the oldest grain,
    // the orphan, and drop the draw.)
    StealRig rig;
    rig.engine.setVoiceMode(false);
    rig.engine.setCloudParams(periodic(0.5, 2.0));
    rig.to(0, 6000);
    rig.engine.setVoiceMode(true);
    rig.engine.startVoice(0, 0, voiceOf(0.5));
    rig.to(6000, 30000);
    rig.engine.setQuota(2 * kEco);
    rig.engine.startVoice(1, 1, voiceOf(0.5));
    rig.to(30000, 30001);

    REQUIRE_FALSE(rig.log.overflowed);
    const auto &events = rig.log.events;
    REQUIRE(events.size() >= 2);
    REQUIRE((!events[0].steal && events[0].voice == gg::kNoVoice &&
             events[0].slot == gg::kCloudStreamSlot && events[0].onset == 0));
    const auto steals = rig.log.steals();
    REQUIRE(steals.size() == 1);
    CHECK(isSteal(steals[0], 0, 0, 30000));
    CHECK(rig.engine.droppedGrains() == 0);
}

TEST_CASE("no candidate past half drops the draw", "[engine][voice][budget]")
{
    // H (slot 0, held) spawns at 0, W (slot 1) at 6,000 and turns weak, O (slot 2) at 8,000 and
    // finishes. At 12,000, under a quota of their 6 units, X (slot 3) draws: the orphan, the
    // weak and the held voices' grains have sounded 4,000, 6,000 and 12,000 samples, each under
    // half its window, so nothing is stolen and the draw drops.
    StealRig rig;
    rig.engine.startVoice(0, 0, voiceOf(0.5));
    rig.to(0, 6000);
    rig.engine.startVoice(1, 1, voiceOf(0.5));
    rig.to(6000, 7000);
    rig.engine.setVoiceParams(1, voiceOf(0.5, true));
    rig.to(7000, 8000);
    rig.engine.startVoice(2, 2, voiceOf(0.5));
    rig.to(8000, 9000);
    rig.engine.finishVoice(2);
    rig.to(9000, 12000);
    rig.engine.setQuota(3 * kEco);
    rig.engine.startVoice(3, 3, voiceOf(0.5));
    rig.to(12000, 13000);

    REQUIRE_FALSE(rig.log.overflowed);
    CHECK(rig.log.steals().empty());
    CHECK(rig.engine.stolenGrains() == 0);
    CHECK(rig.engine.droppedGrains() == 1);
    CHECK(rig.log.events.size() == 3);
}

TEST_CASE("one voice steals as the shared rule did", "[engine][voice][budget]")
{
    // One voice (ordinal 5, slot 9) and no orphan: its slot's oldest grain is the layer's, so
    // each steal takes the smallest index still sounding and not yet stolen at its onset, as M2's
    // rule does (D4 exactly). Reconstructed from the spawn and steal events: a stolen grain ends at
    // its onset + kStealFade, or earlier at its own end. Normal grains of varied lengths
    // (sizeSpreadOctaves) at 300 Hz under a quota of 10 mono Normal grains.
    StealRig rig;
    gg::CloudParams p = kParams;
    p.densityHz = 300.0;
    p.grainDurationSec = 0.08;
    p.quality = gg::Tier::Normal;
    rig.engine.setQuota(10 * gg::grainWeight(gg::Tier::Normal, false));
    rig.engine.startVoice(9, 5, voiceAt(p));
    rig.to(0, 48000);

    REQUIRE_FALSE(rig.log.overflowed);
    struct Grain
    {
        uint64_t index;
        int64_t end;
        bool stolen;
    };
    std::vector<Grain> grains;
    uint32_t steals = 0;
    for (const OrderLog::Event &e : rig.log.events)
    {
        if (!e.steal)
        {
            REQUIRE(e.voice == 5);
            REQUIRE(e.slot == 9);
            grains.push_back({e.index, e.end, false});
            continue;
        }
        INFO("steal of " << e.index << " at " << e.onset);
        REQUIRE(e.voice == 5);
        const auto oldest = std::find_if(grains.begin(), grains.end(), [&](const Grain &g) {
            return !g.stolen && g.end > e.onset;
        });
        REQUIRE(oldest != grains.end());
        REQUIRE(oldest->index == e.index);
        oldest->stolen = true;
        oldest->end = std::min(oldest->end, e.onset + gg::kStealFade);
        ++steals;
    }
    CHECK(steals > 50);
    CHECK(rig.engine.droppedGrains() > 0);
    CHECK(rig.engine.stolenGrains() == steals);
}
