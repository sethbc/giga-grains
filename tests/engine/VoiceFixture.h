// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The voice tests' shared fixture (voice_tests.cpp, pitchsync_tests.cpp): an engine config, a
// sine source, a grain log for the event sink, and rendering helpers.

#ifndef GG_TESTS_ENGINE_VOICEFIXTURE_H
#define GG_TESTS_ENGINE_VOICEFIXTURE_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

#include "gg/engine/Engine.h"
#include "gg/grain/AmpEnvelope.h"
#include "gg/grain/CloudScheduler.h"
#include "gg/grain/GrainPool.h"

namespace gg::test
{
inline constexpr double kSampleRate = 48000.0;
inline constexpr int64_t kSourceFrames = 4 * 48000; // 4 s
inline constexpr uint64_t kSeed = 11;
inline constexpr uint32_t kLayer = 2;

inline gg::EngineConfig config(uint32_t maxBlock = 4096)
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
inline constexpr gg::CloudParams kParams{.densityHz = 41.0,
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
inline gg::AmpEnvelope unityAmp()
{
    return {.at = 0,
            .level = 1.0,
            .stage = gg::AmpEnvelope::Stage::Sustain,
            .attackPerSample = 0.0,
            .decayPerSample = 0.0,
            .releasePerSample = 0.0,
            .sustain = 1.0};
}

inline gg::VoiceParams voiceAt(const gg::CloudParams &cloud)
{
    return {.cloud = cloud, .amp = unityAmp(), .gain = 1.0f};
}

// A mono 440 Hz sine at the output rate, so a grain's rate is its draw's.
inline std::shared_ptr<const gg::SampleBuffer> sineSource()
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
inline void renderSpan(gg::Engine &engine, Audio &r, int64_t from, int64_t to, uint32_t block)
{
    for (int64_t t = from; t < to;)
    {
        const auto n = static_cast<uint32_t>(std::min<int64_t>(block, to - t));
        float *out[2] = {r.left.data() + t, r.right.data() + t};
        engine.process(out, 2, n);
        t += n;
    }
}

inline Audio silentAudio(int64_t frames)
{
    constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
    return {std::vector<float>(static_cast<std::size_t>(frames), kUnwritten),
            std::vector<float>(static_cast<std::size_t>(frames), kUnwritten)};
}

inline bool sameBits(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}
} // namespace gg::test

#endif // GG_TESTS_ENGINE_VOICEFIXTURE_H
