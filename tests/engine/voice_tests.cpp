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
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/engine/Engine.h"
#include "gg/grain/AmpEnvelope.h"
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
