// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Determinism (INTENT §8.6, §14): the same configuration renders the same bits, however the host
// splits the blocks.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Rng.h"
#include "gg/engine/Engine.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int64_t kSourceFrames = 4 * 48000; // 4 s
constexpr int64_t kFrames = 3 * 48000;       // 3 s of output
constexpr uint32_t kMaxBlock = 4096;

gg::EngineConfig config(uint64_t seed) { return {kSampleRate, kMaxBlock, 4096, seed}; }

// 37 Hz halfway between periodic and Poisson onsets, 0.083 s grains at position 0.4 with spray
// 0.3, up 5 semitones, panned across 0.7 of the stereo field, gain 0.7.
constexpr gg::CloudParams kParams{.densityHz = 37.0,
                                  .distribution = 0.5,
                                  .grainDurationSec = 0.083,
                                  .position = 0.4,
                                  .spray = 0.3,
                                  .pitchSemitones = 5.0,
                                  .stereo = 0.7,
                                  .gain = 0.7f};

// A stereo 440 Hz sine; the right channel is a quarter turn ahead, so the outputs differ.
std::shared_ptr<const gg::SampleBuffer> sineSource()
{
    auto buf = std::make_shared<gg::SampleBuffer>(2, kSourceFrames, kSampleRate);
    for (uint32_t c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < kSourceFrames; ++i)
        {
            const double phase = 2.0 * std::numbers::pi * (440.0 * i / kSampleRate + 0.25 * c);
            buf->channel(c)[i] = static_cast<float>(std::sin(phase));
        }
    }
    return buf;
}

struct Render
{
    std::vector<float> left;
    std::vector<float> right;
};

// Renders kFrames with a fresh engine, calling process() with blockSizes in turn (cycling), the
// last call cut short to end at kFrames. The buffers start as NaN, so a sample process() didn't
// write fails allFinite(), and two renders that both wrote nothing can't pass sameBits().
Render render(const std::shared_ptr<const gg::SampleBuffer> &source, uint64_t seed,
              const std::vector<uint32_t> &blockSizes)
{
    gg::Engine engine(config(seed));
    engine.setSource(source);
    engine.setCloudParams(kParams);
    constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
    Render r{std::vector<float>(static_cast<std::size_t>(kFrames), kUnwritten),
             std::vector<float>(static_cast<std::size_t>(kFrames), kUnwritten)};
    int64_t done = 0;
    for (std::size_t next = 0; done < kFrames; ++next)
    {
        const uint32_t want = blockSizes[next % blockSizes.size()];
        const auto n = static_cast<uint32_t>(std::min<int64_t>(want, kFrames - done));
        float *out[2] = {r.left.data() + done, r.right.data() + done};
        engine.process(out, 2, n);
        done += n;
    }
    return r;
}

// Block sizes in 1..kMaxBlock drawn from gg::rng (seed 99), enough to cover kFrames.
std::vector<uint32_t> randomSplit()
{
    std::vector<uint32_t> sizes;
    int64_t total = 0;
    for (uint64_t i = 0; total < kFrames; ++i)
    {
        const double u =
            gg::rng::uniform01(gg::rng::hash({99, 0, 0, gg::rng::Field::SignalNoise, i}));
        const uint32_t n = 1 + static_cast<uint32_t>(u * kMaxBlock);
        sizes.push_back(n);
        total += n;
    }
    return sizes;
}

bool sameBits(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

bool allFinite(const std::vector<float> &v)
{
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
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
} // namespace

TEST_CASE("render is identical across block sizes", "[determinism]")
{
    const auto source = sineSource();
    const auto ref = render(source, 42, {kMaxBlock});
    // Guard against a vacuous pass: the reference is fully written, audible, and its channels
    // differ.
    REQUIRE(allFinite(ref.left));
    REQUIRE(allFinite(ref.right));
    REQUIRE(peak(ref.left) > 0.05f);
    REQUIRE_FALSE(sameBits(ref.left, ref.right));

    for (const uint32_t blockSize : {1u, 32u, 64u, 511u, 4096u})
    {
        INFO("block size = " << blockSize);
        const auto r = render(source, 42, {blockSize});
        REQUIRE(sameBits(ref.left, r.left));
        REQUIRE(sameBits(ref.right, r.right));
    }

    const auto split = randomSplit();
    REQUIRE(*std::min_element(split.begin(), split.end()) >= 1u);
    REQUIRE(*std::max_element(split.begin(), split.end()) <= kMaxBlock);
    INFO("random split into " << split.size() << " blocks");
    const auto r = render(source, 42, split);
    REQUIRE(sameBits(ref.left, r.left));
    REQUIRE(sameBits(ref.right, r.right));
}

TEST_CASE("render twice is identical", "[determinism]")
{
    const auto source = sineSource();
    const auto first = render(source, 42, {512});
    const auto second = render(source, 42, {512});
    REQUIRE(allFinite(first.left));
    REQUIRE(allFinite(first.right));
    REQUIRE(peak(first.left) > 0.05f);
    REQUIRE(sameBits(first.left, second.left));
    REQUIRE(sameBits(first.right, second.right));
}

TEST_CASE("different seeds differ", "[determinism]")
{
    // The seed reaches the output through the onsets, the spray and the pan.
    const auto source = sineSource();
    const auto one = render(source, 1, {kMaxBlock});
    const auto two = render(source, 2, {kMaxBlock});
    REQUIRE(allFinite(one.left));
    REQUIRE(allFinite(two.left));
    REQUIRE(peak(one.left) > 0.05f);
    REQUIRE(peak(two.left) > 0.05f);
    REQUIRE_FALSE(sameBits(one.left, two.left));
    REQUIRE_FALSE(sameBits(one.right, two.right));
}

namespace
{
// Records spawned grains into preallocated storage: the sink runs inside process().
struct GrainLog
{
    static constexpr std::size_t kCapacity = 4096;
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

// Density changes during a render, as (end frame, density) segments: 37 Hz, then slower, then
// faster, then off, then back on. Each change is made between process() calls at the segment's
// first frame, so every block split below sees it at the same sample.
constexpr int64_t kDensityFrames = 48000;
constexpr std::pair<int64_t, double> kDensitySegments[] = {
    {9000, 37.0}, {20000, 11.0}, {30000, 160.0}, {36000, 0.0}, {kDensityFrames, 160.0},
};

struct DensityRender
{
    Render audio;
    GrainLog log;
};

// Renders kDensitySegments in process() calls of blockSize frames, each segment's last call cut
// short at its end, with an engine whose maxBlockSize is maxBlock.
void renderDensityChanges(const std::shared_ptr<const gg::SampleBuffer> &source, uint32_t maxBlock,
                          uint32_t blockSize, DensityRender &r)
{
    gg::Engine engine({kSampleRate, maxBlock, 4096, 42});
    engine.setSource(source);
    engine.setEventSink(&GrainLog::record, &r.log);
    constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
    r.audio.left.assign(static_cast<std::size_t>(kDensityFrames), kUnwritten);
    r.audio.right.assign(static_cast<std::size_t>(kDensityFrames), kUnwritten);
    int64_t done = 0;
    for (const auto &[end, densityHz] : kDensitySegments)
    {
        gg::CloudParams p = kParams;
        p.densityHz = densityHz;
        engine.setCloudParams(p);
        while (done < end)
        {
            const auto n = static_cast<uint32_t>(std::min<int64_t>(blockSize, end - done));
            float *out[2] = {r.audio.left.data() + done, r.audio.right.data() + done};
            engine.process(out, 2, n);
            done += n;
        }
    }
}
} // namespace

TEST_CASE("density changes are block-size invariant", "[determinism]")
{
    // Spec §6.2: a density change applies from the next onset computed after it, and a stream
    // turned back on starts at the first block after the change. With the change at a fixed
    // sample, neither may depend on how the host splits blocks (Engine.h, INTENT §8.6).
    const auto source = sineSource();
    DensityRender ref;
    renderDensityChanges(source, kMaxBlock, kMaxBlock, ref);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(peak(ref.audio.left) > 0.05f);
    REQUIRE_FALSE(ref.log.overflowed);

    // Guard against a vacuous pass: every segment that is on spawns grains, and none spawn
    // while the stream is off.
    int64_t segmentStart = 0;
    for (const auto &[end, densityHz] : kDensitySegments)
    {
        const auto inSegment = std::count_if(
            ref.log.grains.begin(), ref.log.grains.end(), [&](const gg::GrainSpec &g) {
                return g.onsetSample >= segmentStart && g.onsetSample < end;
            });
        INFO("segment [" << segmentStart << ", " << end << ") at " << densityHz << " Hz");
        if (densityHz > 0.0)
        {
            REQUIRE(inSegment > 0);
        }
        else
        {
            REQUIRE(inSegment == 0);
        }
        segmentStart = end;
    }

    struct Split
    {
        uint32_t maxBlock;
        uint32_t blockSize;
    };
    for (const Split split : {Split{kMaxBlock, 1}, Split{kMaxBlock, 7}, Split{kMaxBlock, 64},
                              Split{kMaxBlock, 511}, Split{64, 4096}})
    {
        INFO("maxBlockSize = " << split.maxBlock << ", block size = " << split.blockSize);
        DensityRender r;
        renderDensityChanges(source, split.maxBlock, split.blockSize, r);
        REQUIRE_FALSE(r.log.overflowed);
        REQUIRE(r.log.grains == ref.log.grains);
        REQUIRE(sameBits(ref.audio.left, r.audio.left));
        REQUIRE(sameBits(ref.audio.right, r.audio.right));
    }
}
