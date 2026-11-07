// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The voice schedules (M3 sub-project 9, Task 1): fixed sequences of voice starts, parameter
// changes, finishes and clears over a stereo noise sample, rendered through the engine at any
// block size. They pin the engine's voice output bit for bit before the voice filter exists, so a
// later change that must leave Off alone can be checked against their hashes.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gg/engine/Engine.h"
#include "gg/grain/CloudScheduler.h"
#include "gg/grain/GrainPool.h"

namespace gg::test
{
enum class ScheduleForm : uint8_t
{
    TwoBus,
    FiveArgument,
    Mono
};

struct ScheduleEvent
{
    enum class Kind : uint8_t
    {
        Start,
        Params,
        Finish,
        Clear
    };
    int64_t at; ///< A grid sample: the event applies before the call that starts there.
    Kind kind;
    uint32_t slot;
    uint32_t ordinal;
    double pitchSemitones;
};

struct VoiceSchedule
{
    std::string name;
    ScheduleForm form;
    double busSplit;
    int64_t frames;
    std::vector<ScheduleEvent> events;
};

inline constexpr double kScheduleRate = 48000.0;
inline constexpr uint64_t kScheduleSeed = 11;
inline constexpr uint32_t kScheduleLayer = 2;

// The stream every voice runs: all the draws a voice makes (spray, spreads, Smear's detune, pan).
inline gg::CloudParams scheduleCloud(double pitchSemitones, double busSplit)
{
    return {.densityHz = 41.0,
            .distribution = 0.6,
            .grainDurationSec = 0.06,
            .position = 0.4,
            .spray = 0.3,
            .pitchSemitones = pitchSemitones,
            .stereo = 0.8,
            .gain = 0.6f,
            .pitchSpreadSemitones = 2.0,
            .sizeSpreadOctaves = 0.5,
            .smearDetuneSemitones = 0.3,
            .busSplit = busSplit};
}

inline std::vector<VoiceSchedule> voiceSchedules()
{
    using K = ScheduleEvent::Kind;
    constexpr int64_t kFrames = 2 * 48000;
    std::vector<VoiceSchedule> all;

    all.push_back({"chord",
                   ScheduleForm::FiveArgument,
                   0.0,
                   kFrames,
                   {{0, K::Start, 0, 0, 0.0},
                    {0, K::Start, 1, 1, 4.0},
                    {0, K::Start, 2, 2, 7.0},
                    {24000, K::Params, 1, 1, 5.0},
                    {48000, K::Finish, 0, 0, 0.0},
                    {72001, K::Finish, 1, 1, 0.0},
                    {72001, K::Finish, 2, 2, 0.0}}});

    // 17 starts over 16 slots: the 17th reuses slot 0.
    VoiceSchedule steal{"steal", ScheduleForm::FiveArgument, 0.0, kFrames, {}};
    for (uint32_t i = 0; i < 17; ++i)
    {
        steal.events.push_back({static_cast<int64_t>(i) * 1500, K::Start, i % 16, i,
                                static_cast<double>(i % 12) - 5.0});
    }
    all.push_back(std::move(steal));

    all.push_back({"dup",
                   ScheduleForm::FiveArgument,
                   0.0,
                   kFrames,
                   {{0, K::Start, 0, 0, 2.0},
                    {0, K::Start, 1, 1, 2.0},
                    {40000, K::Finish, 0, 0, 0.0},
                    {88000, K::Clear, 0, 0, 0.0}}});

    all.push_back({"bus-split",
                   ScheduleForm::TwoBus,
                   0.5,
                   kFrames,
                   {{0, K::Start, 0, 0, 0.0},
                    {0, K::Start, 3, 1, 7.0},
                    {60000, K::Params, 3, 1, -3.0},
                    {80000, K::Finish, 0, 0, 0.0}}});

    all.push_back(
        {"mono",
         ScheduleForm::Mono,
         0.0,
         kFrames,
         {{0, K::Start, 0, 0, 0.0}, {10000, K::Start, 1, 1, 12.0}, {50000, K::Finish, 1, 1, 0.0}}});

    all.push_back(
        {"five-argument",
         ScheduleForm::FiveArgument,
         0.0,
         kFrames,
         {{0, K::Start, 0, 0, -7.0}, {33333, K::Start, 5, 1, 3.0}, {66666, K::Finish, 0, 0, 0.0}}});
    return all;
}

struct ScheduleRender
{
    std::vector<float> out[2];
    std::vector<float> bus2[2];
    std::vector<std::string> events;
};

using BeforeCall = std::function<void(gg::Engine &, int64_t clock)>;

namespace detail
{
// A deterministic stereo noise sample (a 64-bit LCG: test data, not engine randomness).
inline std::shared_ptr<const gg::SampleBuffer> scheduleNoise()
{
    constexpr int64_t kFrames = 4 * 48000;
    auto buf = std::make_shared<gg::SampleBuffer>(2, kFrames, kScheduleRate);
    uint64_t s = 0x9E3779B97F4A7C15ull;
    for (int c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < kFrames; ++i)
        {
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            buf->channel(c)[i] =
                static_cast<float>(static_cast<double>(s >> 40) / 8388608.0 - 1.0) * 0.5f;
        }
    }
    return buf;
}

inline void recordGrain(void *ctx, uint32_t, const gg::GrainSpec &g)
{
    char line[320];
    std::snprintf(line, sizeof line,
                  "i=%llu voice=%u onset=%lld frac=%.17g pos=%.17g rate=%.17g dur=%.17g "
                  "gain=%.9g pan=%.9g bus=%u",
                  static_cast<unsigned long long>(g.index), g.voice,
                  static_cast<long long>(g.onsetSample), g.onsetFrac, g.startPos, g.rate,
                  g.durationSamples, static_cast<double>(g.gain), static_cast<double>(g.pan),
                  static_cast<unsigned>(g.bus));
    static_cast<std::vector<std::string> *>(ctx)->emplace_back(line);
}
} // namespace detail

inline ScheduleRender renderSchedule(const VoiceSchedule &s, uint32_t blockSize,
                                     const BeforeCall &before = {})
{
    gg::EngineConfig c;
    c.sampleRate = kScheduleRate;
    c.maxBlockSize = 4096;
    c.grainCapacity = 4096;
    c.seed = kScheduleSeed;
    c.layer = kScheduleLayer;
    gg::Engine engine(c);
    engine.setSource(detail::scheduleNoise());
    engine.setVoiceMode(true);

    ScheduleRender r;
    engine.setEventSink(&detail::recordGrain, &r.events);
    const auto n = static_cast<std::size_t>(s.frames);
    const bool mono = s.form == ScheduleForm::Mono;
    const bool twoBus = s.form == ScheduleForm::TwoBus;
    for (int ch = 0; ch < (mono ? 1 : 2); ++ch)
    {
        r.out[ch].assign(n, 0.0f);
        if (twoBus)
        {
            r.bus2[ch].assign(n, 0.0f);
        }
    }

    auto events = s.events;
    std::stable_sort(events.begin(), events.end(),
                     [](const ScheduleEvent &a, const ScheduleEvent &b) { return a.at < b.at; });
    std::size_t next = 0;
    int64_t t = 0;
    while (t < s.frames)
    {
        while (next < events.size() && events[next].at <= t)
        {
            const auto &e = events[next++];
            const gg::VoiceParams vp{.cloud = scheduleCloud(e.pitchSemitones, s.busSplit),
                                     .amp = {.at = 0,
                                             .level = 1.0,
                                             .stage = gg::AmpEnvelope::Stage::Sustain,
                                             .attackPerSample = 0.0,
                                             .decayPerSample = 0.0,
                                             .releasePerSample = 0.0,
                                             .sustain = 1.0},
                                     .gain = 1.0f};
            switch (e.kind)
            {
            case ScheduleEvent::Kind::Start:
                engine.startVoice(e.slot, e.ordinal, vp);
                break;
            case ScheduleEvent::Kind::Params:
                engine.setVoiceParams(e.slot, vp);
                break;
            case ScheduleEvent::Kind::Finish:
                engine.finishVoice(e.slot);
                break;
            case ScheduleEvent::Kind::Clear:
                engine.clearVoices();
                break;
            }
        }
        int64_t stop = std::min<int64_t>(s.frames, t + blockSize);
        if (next < events.size())
        {
            stop = std::min(stop, events[next].at);
        }
        const auto len = static_cast<uint32_t>(stop - t);
        if (before)
        {
            before(engine, t);
        }
        float *out[2] = {r.out[0].data() + t, mono ? nullptr : r.out[1].data() + t};
        if (twoBus)
        {
            float *b2[2] = {r.bus2[0].data() + t, r.bus2[1].data() + t};
            engine.process(nullptr, 0, out, 2, b2, len);
        }
        else
        {
            engine.process(nullptr, 0, out, mono ? 1 : 2, len);
        }
        t = stop;
    }
    return r;
}

inline uint64_t fnv1a(const ScheduleRender &r)
{
    uint64_t h = 0xcbf29ce484222325ull;
    const auto mix = [&h](const void *p, std::size_t bytes) {
        const auto *b = static_cast<const unsigned char *>(p);
        for (std::size_t i = 0; i < bytes; ++i)
        {
            h = (h ^ b[i]) * 0x100000001b3ull;
        }
    };
    for (const auto *v : {&r.out[0], &r.out[1], &r.bus2[0], &r.bus2[1]})
    {
        mix(v->data(), v->size() * sizeof(float));
    }
    for (const auto &e : r.events)
    {
        mix(e.data(), e.size());
    }
    return h;
}
} // namespace gg::test
