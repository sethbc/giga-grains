// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The Pitch clock in the scheduler (M3 pitch-sync spec §4.2, S2 to S6, S9, SB2 to SB4): a voice
// at Clock Pitch spawns a grain every period of its note, N periods long, with every draw but the
// onset's held per refresh. Clock Free is sub-project 1's voice, bit for bit.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/VoiceFixture.h"
#include "gg/core/MathDet.h"
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

// The output's bytes, then every logged grain's fields, one at a time (no padding).
uint64_t hashRender(const Audio &audio, const GrainLog &log)
{
    Fnv1a f;
    f.bytes(audio.left.data(), audio.left.size() * sizeof(float));
    f.bytes(audio.right.data(), audio.right.size() * sizeof(float));
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
constexpr uint64_t kFreeVoicesHash = 0x1f4896d56eb8dab6;
constexpr uint64_t kLiveCloudHash = 0xfb04e5e01f0e8610;
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

    CHECK(hashRender(voices, voiceLog) == kFreeVoicesHash);
    CHECK(hashRender(live, liveLog) == kLiveCloudHash);
}
