// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Engine::process end to end against the scalar reference (spec §3, ruling 1). The kernel tests
// hold each SIMD kernel to its reference grain by grain; these hold the whole engine to it: what
// process() renders through the SIMD kernels, with each grain's latched tier, must be what the
// reference renderers give for the grains it reported, bit for bit. The grains come from the
// event sink, are rebuilt into a GrainPool in spawn order (only its public API), and are rendered
// by renderSampleRef or renderLiveRef, each with the reader its tier names.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Rng.h"
#include "gg/core/Window.h"
#include "gg/engine/Engine.h"
#include "gg/grain/GrainKernels.h"
#include "gg/grain/GrainPool.h"
#include "gg/grain/SincTable.h"
#include "gg/source/LiveSource.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr uint32_t kMaxBlock = 256;
// The process() call sizes, cycled: odd sizes, one frame, and one above kMaxBlock, which the
// engine splits.
constexpr uint32_t kBlocks[] = {64, 1, 37, 256, 300, 7, 128};
constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();

// Records spawned grains into preallocated storage: the sink runs inside process().
struct SinkLog
{
    static constexpr std::size_t kCapacity = 4096;
    std::vector<gg::GrainSpec> grains;
    bool overflowed{false};

    SinkLog() { grains.reserve(kCapacity); }

    static void record(void *ctx, uint32_t, const gg::GrainSpec &g)
    {
        auto *log = static_cast<SinkLog *>(ctx);
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

// Noise in [-amp, amp) from gg::rng; channel c of a buffer is stream c, so channels differ.
float noiseAt(uint64_t seed, uint32_t channel, int64_t i, float amp)
{
    using gg::rng::Field;
    return amp * static_cast<float>(gg::rng::bipolar(gg::rng::hash(
                     {seed, 0, channel, Field::SignalNoise, static_cast<uint64_t>(i)})));
}

std::shared_ptr<const gg::SampleBuffer> makeNoise(uint32_t channels, int64_t frames, uint64_t seed)
{
    auto buf = std::make_shared<gg::SampleBuffer>(channels, frames, kSampleRate);
    for (uint32_t c = 0; c < channels; ++c)
    {
        for (int64_t i = 0; i < frames; ++i)
        {
            buf->channel(c)[i] = noiseAt(seed, c, i, 0.5f);
        }
    }
    return buf;
}

// A change made between process() calls at sample `at`.
struct Change
{
    int64_t at;
    bool eco{false};                                  // switch the quality to Eco
    bool setFreeze{false};                            // make a freeze change...
    bool freeze{false};                               // ...to this setting
    std::shared_ptr<const gg::SampleBuffer> source{}; // install this source
};

struct Channels
{
    std::vector<float> ch[2];
};

// end, cut short at the first change after done: no process() call runs across a change.
int64_t nextCut(const std::vector<Change> &changes, int64_t done, int64_t end)
{
    for (const Change &c : changes)
    {
        if (c.at > done)
        {
            end = std::min(end, c.at);
        }
    }
    return end;
}

// The engine's render: process() calls cycling through kBlocks, each cut short at the next
// change. `input` (two channels) feeds the live ring when there is one. The output buffers start
// as NaN, so a sample process() didn't write can't match.
Channels renderEngine(gg::Engine &engine, const gg::CloudParams &base,
                      const std::vector<Change> &changes, const Channels *input, uint32_t outputs,
                      int64_t frames)
{
    Channels out;
    for (auto &c : out.ch)
    {
        c.assign(static_cast<std::size_t>(frames), kUnwritten);
    }
    gg::CloudParams params = base;
    std::size_t call = 0;
    for (int64_t done = 0; done < frames;)
    {
        for (const Change &c : changes)
        {
            if (c.at != done)
            {
                continue;
            }
            if (c.eco)
            {
                params.quality = gg::Tier::Eco;
                engine.setCloudParams(params);
            }
            if (c.setFreeze)
            {
                engine.setFreeze(c.freeze);
            }
            if (c.source)
            {
                engine.setSource(c.source);
            }
        }
        const int64_t end = nextCut(
            changes, done, std::min<int64_t>(done + kBlocks[call++ % std::size(kBlocks)], frames));
        const auto n = static_cast<uint32_t>(end - done);
        const auto at = static_cast<std::size_t>(done);
        float *o[2] = {out.ch[0].data() + at, out.ch[1].data() + at};
        if (input != nullptr)
        {
            const float *in[2] = {input->ch[0].data() + at, input->ch[1].data() + at};
            engine.process(in, 2, o, outputs, n);
        }
        else
        {
            engine.process(o, outputs, n);
        }
        done = end;
    }
    return out;
}

// The grains, in spawn order, in a pool of their own: the rows the reference reads, latched by
// GrainPool::spawn as the engine's were.
std::unique_ptr<gg::GrainPool> poolOf(const std::vector<gg::GrainSpec> &grains)
{
    auto pool = std::make_unique<gg::GrainPool>(
        static_cast<uint32_t>(std::max<std::size_t>(1, grains.size())));
    for (const gg::GrainSpec &g : grains)
    {
        REQUIRE(pool->spawn(g));
    }
    REQUIRE(pool->size() == grains.size());
    return pool;
}

// Output samples [from, to) of every grain, in spawn order, read from `source` with the reader
// each grain's tier names.
void referenceSample(const gg::GrainPool &pool, const gg::SampleBuffer &source,
                     const gg::kernels::Output &out, int64_t from, int64_t to,
                     const gg::kernels::Tables &tables)
{
    const bool mono = source.channels() == 1;
    const gg::kernels::SampleSource src{{source.channel(0), source.channel(mono ? 0 : 1)},
                                        source.frames(),
                                        mono,
                                        nullptr}; // the reference reads the planar channels
    for (uint32_t i = 0; i < pool.size(); ++i)
    {
        if (pool.rows().tier[i] == gg::Tier::Eco)
        {
            gg::kernels::renderSampleRef<gg::kernels::Linear>(pool.rows(), i, src, out, from, to,
                                                              tables);
        }
        else if (pool.rows().tier[i] == gg::Tier::HQ)
        {
            gg::kernels::renderSampleRef<gg::kernels::Sinc>(pool.rows(), i, src, out, from, to,
                                                            tables);
        }
        else
        {
            gg::kernels::renderSampleRef<gg::kernels::Hermite>(pool.rows(), i, src, out, from, to,
                                                               tables);
        }
    }
}

// Silence, plus the reference's render of each source's grains over the samples it was installed
// for: from its change's sample to the next source change (the first source is installed at 0).
Channels renderSampleReference(const std::vector<gg::GrainSpec> &grains,
                               const std::vector<Change> &changes, uint32_t outputs, int64_t frames)
{
    Channels ref;
    for (auto &c : ref.ch)
    {
        c.assign(static_cast<std::size_t>(frames), 0.0f);
    }
    const auto pool = poolOf(grains);
    const gg::HannTable hann;
    const gg::SincTable sinc;
    const gg::kernels::Tables tables{hann, sinc};
    const gg::kernels::Output out{{ref.ch[0].data(), outputs == 2 ? ref.ch[1].data() : nullptr}, 0};
    std::vector<const Change *> sources;
    for (const Change &c : changes)
    {
        if (c.source)
        {
            sources.push_back(&c);
        }
    }
    REQUIRE(!sources.empty());
    REQUIRE(sources.front()->at == 0);
    for (std::size_t k = 0; k < sources.size(); ++k)
    {
        const int64_t to = k + 1 < sources.size() ? sources[k + 1]->at : frames;
        referenceSample(*pool, *sources[k]->source, out, sources[k]->at, to, tables);
    }
    return ref;
}

// Silence, plus the reference's render of the grains over the live ring: a LiveSource of the
// engine's size, written block by block as the engine's is (each process() call split at
// kMaxBlock, with the freeze setting of the call), every grain rendered over each block in spawn
// order with renderLiveRef, which reads the head of that block's write.
Channels renderLiveReference(const std::vector<gg::GrainSpec> &grains,
                             const std::vector<Change> &changes, const Channels &input,
                             double liveSeconds, uint32_t outputs, int64_t frames)
{
    Channels ref;
    for (auto &c : ref.ch)
    {
        c.assign(static_cast<std::size_t>(frames), 0.0f);
    }
    const auto pool = poolOf(grains);
    const gg::GrainRows &rows = pool->rows();
    const gg::HannTable hann;
    const gg::SincTable sinc;
    const gg::kernels::Tables tables{hann, sinc};
    const gg::kernels::Output out{{ref.ch[0].data(), outputs == 2 ? ref.ch[1].data() : nullptr}, 0};
    gg::LiveSource ring;
    ring.prepare(kSampleRate, liveSeconds, kMaxBlock);
    bool frozen = false;
    std::size_t call = 0;
    for (int64_t done = 0; done < frames;)
    {
        for (const Change &c : changes)
        {
            if (c.at == done && c.setFreeze)
            {
                frozen = c.freeze;
            }
        }
        const int64_t end = nextCut(
            changes, done, std::min<int64_t>(done + kBlocks[call++ % std::size(kBlocks)], frames));
        ring.setFrozen(frozen);
        for (int64_t blockStart = done; blockStart < end; blockStart += kMaxBlock)
        {
            const int64_t blockEnd = std::min<int64_t>(blockStart + kMaxBlock, end);
            const auto at = static_cast<std::size_t>(blockStart);
            const float *in[2] = {input.ch[0].data() + at, input.ch[1].data() + at};
            ring.write(in, 2, 0, blockStart, static_cast<uint32_t>(blockEnd - blockStart));
            for (uint32_t i = 0; i < pool->size(); ++i)
            {
                if (rows.tier[i] == gg::Tier::Eco)
                {
                    gg::kernels::renderLiveRef<gg::kernels::Linear>(rows, i, ring, 1u, out,
                                                                    blockStart, blockEnd, tables);
                }
                else if (rows.tier[i] == gg::Tier::HQ)
                {
                    gg::kernels::renderLiveRef<gg::kernels::Sinc>(rows, i, ring, 1u, out,
                                                                  blockStart, blockEnd, tables);
                }
                else
                {
                    gg::kernels::renderLiveRef<gg::kernels::Hermite>(rows, i, ring, 1u, out,
                                                                     blockStart, blockEnd, tables);
                }
            }
        }
        done = end;
    }
    return ref;
}

bool sameBits(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
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

// Both tiers sound, and the grains that do aren't too few to mean anything.
void requireBothTiers(const std::vector<gg::GrainSpec> &grains)
{
    const auto eco = std::count_if(grains.begin(), grains.end(),
                                   [](const gg::GrainSpec &g) { return g.tier == gg::Tier::Eco; });
    const auto total = static_cast<std::ptrdiff_t>(grains.size());
    REQUIRE(eco > 20);
    REQUIRE(total - eco > 20);
}

void requireSame(const Channels &engine, const Channels &ref, uint32_t outputs)
{
    REQUIRE(peak(ref.ch[0]) > 0.05f);
    REQUIRE(sameBits(engine.ch[0], ref.ch[0]));
    if (outputs == 2)
    {
        REQUIRE(peak(ref.ch[1]) > 0.05f);
        REQUIRE(sameBits(engine.ch[1], ref.ch[1]));
    }
}

// Sample mode: Poisson onsets, spread positions and pan, a fifth up, so the grains overlap,
// cross the source's ends and land at every fraction.
gg::CloudParams sampleParams()
{
    return {.densityHz = 400.0,
            .distribution = 0.5,
            .grainDurationSec = 0.03,
            .position = 0.5,
            .spray = 0.6,
            .pitchSemitones = 7.0,
            .stereo = 0.8,
            .gain = 0.5f};
}
} // namespace

TEST_CASE("engine output is the scalar reference's bit for bit", "[engine]")
{
    constexpr int64_t kFrames = 24000;
    // Halfway through, the quality switches from Normal to Eco, so grains of both tiers overlap
    // there and each must be read with its own.
    const Change toEco{.at = kFrames / 2, .eco = true};

    SECTION("sample mode, mono and stereo sources, one and two outputs")
    {
        for (const uint32_t channels : {1u, 2u})
        {
            for (const uint32_t outputs : {1u, 2u})
            {
                INFO("source channels " << channels << ", outputs " << outputs);
                const std::vector<Change> changes{
                    {.at = 0, .source = makeNoise(channels, 30000, 3 + channels)}, toEco};
                gg::EngineConfig config;
                config.maxBlockSize = kMaxBlock;
                config.seed = 31;
                gg::Engine engine(config);
                SinkLog log;
                engine.setEventSink(&SinkLog::record, &log);
                engine.setCloudParams(sampleParams());
                const Channels got =
                    renderEngine(engine, sampleParams(), changes, nullptr, outputs, kFrames);
                REQUIRE_FALSE(log.overflowed);
                requireBothTiers(log.grains);
                requireSame(got, renderSampleReference(log.grains, changes, outputs, kFrames),
                            outputs);
            }
        }
    }

    SECTION("sample mode, a stereo source replaced by a shorter stereo one")
    {
        // A third of the way in, a source of 30,000 frames gives way to one of 2,000. setSource
        // keeps the sounding grains, which go on at their old positions, mostly past the new
        // source's end, where the reference reads 0: the SIMD kernel must read the new
        // interleaved copy, within its bounds, not the old one.
        for (const uint32_t outputs : {1u, 2u})
        {
            INFO("outputs " << outputs);
            const std::vector<Change> changes{{.at = 0, .source = makeNoise(2, 30000, 7)},
                                              {.at = kFrames / 3, .source = makeNoise(2, 2000, 8)},
                                              toEco};
            gg::EngineConfig config;
            config.maxBlockSize = kMaxBlock;
            config.seed = 37;
            gg::Engine engine(config);
            SinkLog log;
            engine.setEventSink(&SinkLog::record, &log);
            engine.setCloudParams(sampleParams());
            const Channels got =
                renderEngine(engine, sampleParams(), changes, nullptr, outputs, kFrames);
            REQUIRE_FALSE(log.overflowed);
            requireBothTiers(log.grains);
            // Some grains spawned from the first source still sound after the switch.
            REQUIRE(std::any_of(log.grains.begin(), log.grains.end(), [](const gg::GrainSpec &g) {
                return g.onsetSample < kFrames / 3 && gg::grainEndSample(g) > kFrames / 3 + 100;
            }));
            requireSame(got, renderSampleReference(log.grains, changes, outputs, kFrames), outputs);
        }
    }

    SECTION("live mode, through a freeze and the ring's wrap, one and two outputs")
    {
        // A 0.25 s ring (N = 12,000 frames in 12,256 slots), frozen from 20,000 to 26,000 (the
        // head stops 960 samples in) and again from 40,000 to the end: it wraps twice, grains
        // read across a stopped head and through the fade, and some calls straddle the kink.
        constexpr int64_t kLiveFrames = 48000;
        constexpr double kRingSeconds = 0.25;
        Channels input;
        for (uint32_t c = 0; c < 2; ++c)
        {
            input.ch[c].resize(static_cast<std::size_t>(kLiveFrames));
            for (int64_t i = 0; i < kLiveFrames; ++i)
            {
                input.ch[c][static_cast<std::size_t>(i)] = noiseAt(41, c, i, 0.5f);
            }
        }
        const std::vector<Change> changes{
            {.at = 20000, .setFreeze = true, .freeze = true},
            {.at = 26000, .setFreeze = true, .freeze = false},
            {.at = kLiveFrames / 2, .eco = true},
            {.at = 40000, .setFreeze = true, .freeze = true},
        };
        const gg::CloudParams params{.densityHz = 300.0,
                                     .distribution = 0.5,
                                     .grainDurationSec = 0.04,
                                     .position = 0.05,
                                     .spray = 0.15,
                                     .pitchSemitones = 5.0,
                                     .stereo = 0.8,
                                     .gain = 0.5f};
        for (const uint32_t outputs : {1u, 2u})
        {
            INFO("outputs " << outputs);
            gg::EngineConfig config;
            config.maxBlockSize = kMaxBlock;
            config.seed = 43;
            config.liveSeconds = kRingSeconds;
            gg::Engine engine(config);
            engine.setSourceMode(gg::SourceMode::Live);
            SinkLog log;
            engine.setEventSink(&SinkLog::record, &log);
            engine.setCloudParams(params);
            const Channels got =
                renderEngine(engine, params, changes, &input, outputs, kLiveFrames);
            REQUIRE(engine.sourceMode() == gg::SourceMode::Live);
            REQUIRE(engine.liveWriteHead() > 2 * (engine.liveFrames() + kMaxBlock));
            REQUIRE_FALSE(log.overflowed);
            requireBothTiers(log.grains);
            requireSame(
                got,
                renderLiveReference(log.grains, changes, input, kRingSeconds, outputs, kLiveFrames),
                outputs);
        }
    }
}

TEST_CASE("textured engine output is the scalar reference's bit for bit", "[engine]")
{
    // Texture 0.6 (spec §4): a sustain between a short attack and release. Every grain latches
    // it, and the SIMD interior's warped window must give the reference's bits, in Sample and
    // Live mode, at both tiers.
    constexpr double kTexture = 0.6;
    const auto requireTextured = [](const std::vector<gg::GrainSpec> &grains) {
        REQUIRE(!grains.empty());
        for (const gg::GrainSpec &g : grains)
        {
            REQUIRE(g.texture == kTexture);
        }
    };

    SECTION("sample mode, stereo source, one and two outputs")
    {
        constexpr int64_t kFrames = 24000;
        gg::CloudParams params = sampleParams();
        params.texture = kTexture;
        for (const uint32_t outputs : {1u, 2u})
        {
            INFO("outputs " << outputs);
            const std::vector<Change> changes{{.at = 0, .source = makeNoise(2, 30000, 51)},
                                              {.at = kFrames / 2, .eco = true}};
            gg::EngineConfig config;
            config.maxBlockSize = kMaxBlock;
            config.seed = 53;
            gg::Engine engine(config);
            SinkLog log;
            engine.setEventSink(&SinkLog::record, &log);
            engine.setCloudParams(params);
            const Channels got = renderEngine(engine, params, changes, nullptr, outputs, kFrames);
            REQUIRE_FALSE(log.overflowed);
            requireBothTiers(log.grains);
            requireTextured(log.grains);
            requireSame(got, renderSampleReference(log.grains, changes, outputs, kFrames), outputs);
        }
    }

    SECTION("live mode, through a freeze, one and two outputs")
    {
        constexpr int64_t kLiveFrames = 36000;
        constexpr double kRingSeconds = 0.25;
        Channels input;
        for (uint32_t c = 0; c < 2; ++c)
        {
            input.ch[c].resize(static_cast<std::size_t>(kLiveFrames));
            for (int64_t i = 0; i < kLiveFrames; ++i)
            {
                input.ch[c][static_cast<std::size_t>(i)] = noiseAt(57, c, i, 0.5f);
            }
        }
        const std::vector<Change> changes{
            {.at = 20000, .setFreeze = true, .freeze = true},
            {.at = 26000, .setFreeze = true, .freeze = false},
            {.at = kLiveFrames / 2, .eco = true},
        };
        const gg::CloudParams params{.densityHz = 300.0,
                                     .distribution = 0.5,
                                     .grainDurationSec = 0.04,
                                     .position = 0.05,
                                     .spray = 0.15,
                                     .pitchSemitones = 5.0,
                                     .stereo = 0.8,
                                     .gain = 0.5f,
                                     .texture = kTexture};
        for (const uint32_t outputs : {1u, 2u})
        {
            INFO("outputs " << outputs);
            gg::EngineConfig config;
            config.maxBlockSize = kMaxBlock;
            config.seed = 59;
            config.liveSeconds = kRingSeconds;
            gg::Engine engine(config);
            engine.setSourceMode(gg::SourceMode::Live);
            SinkLog log;
            engine.setEventSink(&SinkLog::record, &log);
            engine.setCloudParams(params);
            const Channels got =
                renderEngine(engine, params, changes, &input, outputs, kLiveFrames);
            REQUIRE_FALSE(log.overflowed);
            requireBothTiers(log.grains);
            requireTextured(log.grains);
            requireSame(
                got,
                renderLiveReference(log.grains, changes, input, kRingSeconds, outputs, kLiveFrames),
                outputs);
        }
    }
}
