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

#include "gg/core/MathDet.h"
#include "gg/core/Rng.h"
#include "gg/engine/Engine.h"
#include "gg/grain/Budget.h"
#include "gg/grain/Tier.h"
#include "gg/source/PreparedSource.h"

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

// Renders kFrames with a fresh engine at kParams and the given quality, calling process() with
// blockSizes in turn (cycling), the last call cut short to end at kFrames. The buffers start as
// NaN, so a sample process() didn't write fails allFinite(), and two renders that both wrote
// nothing can't pass sameBits().
Render render(const std::shared_ptr<const gg::SampleBuffer> &source, uint64_t seed,
              const std::vector<uint32_t> &blockSizes, gg::Tier quality = gg::Tier::Normal)
{
    gg::Engine engine(config(seed));
    engine.setSource(source);
    gg::CloudParams p = kParams;
    p.quality = quality;
    engine.setCloudParams(p);
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

// Density and distribution changes during a render, as segments: 37 Hz, then slower, then
// faster, then Poisson at the same density, then off, then back on, periodic. Each change is
// made between process() calls at the segment's first frame, so every block split below sees it
// at the same absolute sample.
struct Segment
{
    int64_t end; // the first frame after the segment
    double densityHz;
    double distribution;
};
constexpr int64_t kDensityFrames = 48000;
constexpr Segment kDensitySegments[] = {
    {9000, 37.0, 0.5},   {20000, 11.0, 0.5}, {30000, 160.0, 0.5},
    {33000, 160.0, 1.0}, {36000, 0.0, 1.0},  {kDensityFrames, 160.0, 0.0},
};

struct DensityRender
{
    Render audio;
    GrainLog log;
};

// Renders kDensitySegments in process() calls of blockSizes in turn (cycling across segments),
// each segment's last call cut short at its end, with an engine whose maxBlockSize is maxBlock.
void renderDensityChanges(const std::shared_ptr<const gg::SampleBuffer> &source, uint32_t maxBlock,
                          const std::vector<uint32_t> &blockSizes, DensityRender &r)
{
    gg::Engine engine({kSampleRate, maxBlock, 4096, 42});
    engine.setSource(source);
    engine.setEventSink(&GrainLog::record, &r.log);
    constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
    r.audio.left.assign(static_cast<std::size_t>(kDensityFrames), kUnwritten);
    r.audio.right.assign(static_cast<std::size_t>(kDensityFrames), kUnwritten);
    int64_t done = 0;
    std::size_t next = 0;
    for (const Segment &segment : kDensitySegments)
    {
        gg::CloudParams p = kParams;
        p.densityHz = segment.densityHz;
        p.distribution = segment.distribution;
        engine.setCloudParams(p);
        while (done < segment.end)
        {
            const uint32_t want = blockSizes[next++ % blockSizes.size()];
            const auto n = static_cast<uint32_t>(std::min<int64_t>(want, segment.end - done));
            float *out[2] = {r.audio.left.data() + done, r.audio.right.data() + done};
            engine.process(out, 2, n);
            done += n;
        }
    }
}
} // namespace

TEST_CASE("density and distribution changes are block-size invariant", "[determinism]")
{
    // Ruling M1-h: a density or distribution change at g rescales the pending onset. Ruling
    // M1-j: a stream turned back on starts at the first block after the change. With the changes
    // at fixed samples, neither may depend on how the host splits blocks (Engine.h, INTENT
    // §8.6).
    const auto source = sineSource();
    DensityRender ref;
    renderDensityChanges(source, kMaxBlock, {kMaxBlock}, ref);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(peak(ref.audio.left) > 0.05f);
    REQUIRE_FALSE(ref.log.overflowed);

    // Guard against a vacuous pass: every segment that is on spawns grains, and none spawn
    // while the stream is off.
    int64_t segmentStart = 0;
    for (const Segment &segment : kDensitySegments)
    {
        const auto inSegment = std::count_if(
            ref.log.grains.begin(), ref.log.grains.end(), [&](const gg::GrainSpec &g) {
                return g.onsetSample >= segmentStart && g.onsetSample < segment.end;
            });
        INFO("segment [" << segmentStart << ", " << segment.end << ") at " << segment.densityHz
                         << " Hz");
        if (segment.densityHz > 0.0)
        {
            REQUIRE(inSegment > 0);
        }
        else
        {
            REQUIRE(inSegment == 0);
        }
        segmentStart = segment.end;
    }

    // Ruling M1-h through the engine: at every change that keeps the stream on, the first grain
    // at or after the change is at max(t(k−1) + Δ_new(k−1), g), from the last grain before it
    // and its own onset draw (seed 42, layer 0, stream 0); and the change did move it.
    int moved = 0;
    for (std::size_t s = 1; s < std::size(kDensitySegments); ++s)
    {
        const Segment &before = kDensitySegments[s - 1];
        const Segment &after = kDensitySegments[s];
        if (!(before.densityHz > 0.0 && after.densityHz > 0.0))
        {
            continue;
        }
        const int64_t g = before.end;
        INFO("change at " << g);
        const auto next = std::find_if(ref.log.grains.begin(), ref.log.grains.end(),
                                       [&](const gg::GrainSpec &x) { return x.onsetSample >= g; });
        REQUIRE(next != ref.log.grains.begin());
        REQUIRE(next != ref.log.grains.end());
        const gg::GrainSpec &last = *(next - 1);
        REQUIRE(next->index == last.index + 1);
        const double e = -gg::logDet(
            1.0 - gg::rng::uniform01(gg::rng::hash({42, 0, 0, gg::rng::Field::Onset, last.index})));
        const auto onsetAfter = [&](const Segment &params) {
            const double period = kSampleRate / params.densityHz;
            const double d = params.distribution;
            return static_cast<double>(last.onsetSample) + last.onsetFrac +
                   std::max(period * ((1.0 - d) + d * e), 1.0 / 16.0);
        };
        const double expected = std::max(onsetAfter(after), static_cast<double>(g));
        REQUIRE(static_cast<double>(next->onsetSample) + next->onsetFrac == expected);
        moved += onsetAfter(before) != expected ? 1 : 0;
    }
    REQUIRE(moved == 3);

    struct Split
    {
        uint32_t maxBlock;
        std::vector<uint32_t> blockSizes;
    };
    const auto random = randomSplit();
    REQUIRE(random.size() > 2);
    for (const Split &split :
         {Split{kMaxBlock, {1}}, Split{kMaxBlock, {7}}, Split{kMaxBlock, {64}},
          Split{kMaxBlock, {511}}, Split{64, {4096}}, Split{kMaxBlock, random}})
    {
        INFO("maxBlockSize = " << split.maxBlock << ", first block size = "
                               << split.blockSizes.front() << " of " << split.blockSizes.size());
        DensityRender r;
        renderDensityChanges(source, split.maxBlock, split.blockSizes, r);
        REQUIRE_FALSE(r.log.overflowed);
        REQUIRE(r.log.grains == ref.log.grains);
        REQUIRE(sameBits(ref.audio.left, r.audio.left));
        REQUIRE(sameBits(ref.audio.right, r.audio.right));
    }
}

namespace
{
// Live mode (spec §6.3): 3 s of stereo noise through the 16 s ring, with Freeze on at 1 s and off
// at 2 s. The freeze changes are made between process() calls at their samples (both on the
// 32-sample grid, where the Processor applies them), so every block split sees them at the same
// absolute sample.
constexpr int64_t kLiveFrames = 3 * 48000;
constexpr int64_t kFreezeOn = 48000;
constexpr int64_t kFreezeOff = 96000;

struct LiveInput
{
    std::vector<float> left;
    std::vector<float> right;
};

LiveInput liveNoise()
{
    LiveInput in{std::vector<float>(static_cast<std::size_t>(kLiveFrames)),
                 std::vector<float>(static_cast<std::size_t>(kLiveFrames))};
    for (int64_t i = 0; i < kLiveFrames; ++i)
    {
        using gg::rng::Field;
        const auto k = static_cast<uint64_t>(i);
        const auto at = static_cast<std::size_t>(i);
        in.left[at] =
            0.5f *
            static_cast<float>(gg::rng::bipolar(gg::rng::hash({21, 0, 0, Field::SignalNoise, k})));
        in.right[at] =
            0.5f *
            static_cast<float>(gg::rng::bipolar(gg::rng::hash({21, 0, 1, Field::SignalNoise, k})));
    }
    return in;
}

struct LiveRender
{
    Render audio;
    GrainLog log;
    int64_t writeHead{0};
};

void renderLive(const LiveInput &input, uint32_t maxBlock, const std::vector<uint32_t> &blockSizes,
                LiveRender &r, gg::Tier quality = gg::Tier::Normal)
{
    gg::EngineConfig c{kSampleRate, maxBlock, 4096, 42};
    c.liveSeconds = 16.0;
    gg::Engine engine(c);
    engine.setSourceMode(gg::SourceMode::Live);
    // kParams with Position and Spray in seconds: 0.3 s behind the head, sprayed over 0.2 s.
    gg::CloudParams p = kParams;
    p.position = 0.3;
    p.spray = 0.2;
    p.quality = quality;
    engine.setCloudParams(p);
    engine.setEventSink(&GrainLog::record, &r.log);
    constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
    r.audio.left.assign(static_cast<std::size_t>(kLiveFrames), kUnwritten);
    r.audio.right.assign(static_cast<std::size_t>(kLiveFrames), kUnwritten);
    int64_t done = 0;
    std::size_t next = 0;
    for (const int64_t end : {kFreezeOn, kFreezeOff, kLiveFrames})
    {
        while (done < end)
        {
            const uint32_t want = blockSizes[next++ % blockSizes.size()];
            const auto n = static_cast<uint32_t>(std::min<int64_t>(want, end - done));
            const float *in[2] = {input.left.data() + done, input.right.data() + done};
            float *out[2] = {r.audio.left.data() + done, r.audio.right.data() + done};
            engine.process(in, 2, out, 2, n);
            done += n;
        }
        engine.setFreeze(end == kFreezeOn);
    }
    r.writeHead = engine.liveWriteHead();
}
} // namespace

TEST_CASE("live mode is block-size invariant", "[determinism]")
{
    const auto input = liveNoise();
    LiveRender ref;
    renderLive(input, kMaxBlock, {kMaxBlock}, ref);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(peak(ref.audio.left) > 0.05f);
    REQUIRE_FALSE(sameBits(ref.audio.left, ref.audio.right));
    REQUIRE_FALSE(ref.log.overflowed);
    // Guard against a vacuous pass: the head stopped 20 ms (960 samples) into the freeze and
    // resumed after it, grains spawned before, during and after it, and the frozen second sounds.
    REQUIRE(ref.writeHead == kLiveFrames - (kFreezeOff - kFreezeOn) + 960);
    for (const auto &[from, to] :
         {std::pair{int64_t{0}, kFreezeOn}, std::pair{kFreezeOn, kFreezeOff},
          std::pair{kFreezeOff, kLiveFrames}})
    {
        INFO("grains in [" << from << ", " << to << ")");
        REQUIRE(std::count_if(ref.log.grains.begin(), ref.log.grains.end(),
                              [&](const gg::GrainSpec &g) {
                                  return g.onsetSample >= from && g.onsetSample < to;
                              }) > 20);
    }
    REQUIRE(peak(std::vector<float>(ref.audio.left.begin() + kFreezeOn + 24000,
                                    ref.audio.left.begin() + kFreezeOff)) > 0.05f);

    struct Split
    {
        uint32_t maxBlock;
        std::vector<uint32_t> blockSizes;
    };
    const auto random = randomSplit();
    for (const Split &split : {Split{kMaxBlock, {1}}, Split{kMaxBlock, {7}}, Split{kMaxBlock, {64}},
                               Split{kMaxBlock, {511}}, Split{kMaxBlock, {4096}}, Split{64, {4096}},
                               Split{kMaxBlock, random}})
    {
        INFO("maxBlockSize = " << split.maxBlock << ", first block size = "
                               << split.blockSizes.front() << " of " << split.blockSizes.size());
        LiveRender r;
        renderLive(input, split.maxBlock, split.blockSizes, r);
        REQUIRE_FALSE(r.log.overflowed);
        REQUIRE(r.log.grains == ref.log.grains);
        REQUIRE(r.writeHead == ref.writeHead);
        REQUIRE(sameBits(ref.audio.left, r.audio.left));
        REQUIRE(sameBits(ref.audio.right, r.audio.right));
    }
}

TEST_CASE("eco render is identical across block sizes", "[determinism]")
{
    // The Eco tier (ruling 2), over the sample source and over the live ring with its freeze.
    using gg::Tier;
    const auto random = randomSplit();

    SECTION("sample source")
    {
        const auto source = sineSource();
        const auto ref = render(source, 42, {kMaxBlock}, Tier::Eco);
        REQUIRE(allFinite(ref.left));
        REQUIRE(allFinite(ref.right));
        REQUIRE(peak(ref.left) > 0.05f);
        REQUIRE_FALSE(sameBits(ref.left, ref.right));
        // Not the Normal render: the tier reaches the output.
        const auto normal = render(source, 42, {kMaxBlock});
        REQUIRE_FALSE(sameBits(ref.left, normal.left));
        REQUIRE_FALSE(sameBits(ref.right, normal.right));

        for (const uint32_t blockSize : {1u, 7u, 32u, 64u, 511u, 4096u})
        {
            INFO("block size = " << blockSize);
            const auto r = render(source, 42, {blockSize}, Tier::Eco);
            REQUIRE(sameBits(ref.left, r.left));
            REQUIRE(sameBits(ref.right, r.right));
        }
        INFO("random split into " << random.size() << " blocks");
        const auto r = render(source, 42, random, Tier::Eco);
        REQUIRE(sameBits(ref.left, r.left));
        REQUIRE(sameBits(ref.right, r.right));
    }

    SECTION("live ring")
    {
        const auto input = liveNoise();
        LiveRender ref;
        renderLive(input, kMaxBlock, {kMaxBlock}, ref, Tier::Eco);
        REQUIRE(allFinite(ref.audio.left));
        REQUIRE(allFinite(ref.audio.right));
        REQUIRE(peak(ref.audio.left) > 0.05f);
        REQUIRE_FALSE(ref.log.overflowed);
        REQUIRE(std::all_of(ref.log.grains.begin(), ref.log.grains.end(),
                            [](const gg::GrainSpec &g) { return g.tier == Tier::Eco; }));
        LiveRender normal;
        renderLive(input, kMaxBlock, {kMaxBlock}, normal);
        REQUIRE_FALSE(sameBits(ref.audio.left, normal.audio.left));

        for (const std::vector<uint32_t> &blockSizes :
             {std::vector<uint32_t>{1}, {7}, {32}, {64}, {511}, {4096}, random})
        {
            INFO("first block size = " << blockSizes.front() << " of " << blockSizes.size());
            LiveRender r;
            renderLive(input, kMaxBlock, blockSizes, r, Tier::Eco);
            REQUIRE_FALSE(r.log.overflowed);
            REQUIRE(r.log.grains == ref.log.grains);
            REQUIRE(sameBits(ref.audio.left, r.audio.left));
            REQUIRE(sameBits(ref.audio.right, r.audio.right));
        }
    }

    SECTION("a quality change reaches only the grains that start after it")
    {
        // Eco, then Normal from 1 s: the grains started before 1 s keep reading linearly until
        // they end, at most 0.083 s later (3,986 samples). So the render is the Eco one before
        // 1 s, neither just after, and the Normal one once the last Eco grain has ended; the
        // change moves no onset, since the density and the distribution stay.
        constexpr int64_t kChange = 48000;
        constexpr int64_t kEcoGone = kChange + 4000;
        const auto source = sineSource();
        const auto eco = render(source, 42, {kMaxBlock}, Tier::Eco);
        const auto normal = render(source, 42, {kMaxBlock});
        const auto same = [](const std::vector<float> &a, const std::vector<float> &b, int64_t from,
                             int64_t to) {
            return std::memcmp(a.data() + from, b.data() + from,
                               static_cast<std::size_t>(to - from) * sizeof(float)) == 0;
        };
        for (const uint32_t blockSize : {1u, 64u, 511u, 4096u})
        {
            INFO("block size = " << blockSize);
            gg::Engine engine(config(42));
            engine.setSource(source);
            gg::CloudParams p = kParams;
            p.quality = Tier::Eco;
            engine.setCloudParams(p);
            constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
            Render r{std::vector<float>(static_cast<std::size_t>(kFrames), kUnwritten),
                     std::vector<float>(static_cast<std::size_t>(kFrames), kUnwritten)};
            for (int64_t done = 0; done < kFrames;)
            {
                if (done == kChange)
                {
                    p.quality = Tier::Normal;
                    engine.setCloudParams(p);
                }
                const int64_t end = done < kChange ? kChange : kFrames;
                const auto n = static_cast<uint32_t>(std::min<int64_t>(blockSize, end - done));
                float *out[2] = {r.left.data() + done, r.right.data() + done};
                engine.process(out, 2, n);
                done += n;
            }
            REQUIRE(allFinite(r.left));
            REQUIRE(same(r.left, eco.left, 0, kChange));
            REQUIRE(same(r.right, eco.right, 0, kChange));
            REQUIRE_FALSE(same(r.left, eco.left, kChange, kEcoGone));
            REQUIRE_FALSE(same(r.left, normal.left, kChange, kEcoGone));
            REQUIRE(same(r.left, normal.left, kEcoGone, kFrames));
            REQUIRE(same(r.right, normal.right, kEcoGone, kFrames));
        }
    }
}

namespace
{
// A saturated pool (carry-over [103]): capacity 64 against 4 kHz of 50 ms grains, about 200
// overlapping wanted, for 1 s. At saturation a grain is admitted only when one ends, about one
// every 38 samples, so a 4096-frame block admits more than the 64 that storage holds beyond the
// sounding grains: storage fills mid-block and admit() renders and retires early.
constexpr uint32_t kSaturatedCapacity = 64;
constexpr int64_t kSaturatedFrames = 48000;

struct SaturatedRender
{
    Render audio;
    GrainLog log;
    uint64_t dropped{0};
};

void renderSaturated(const std::shared_ptr<const gg::SampleBuffer> &source,
                     const std::vector<uint32_t> &blockSizes, SaturatedRender &r)
{
    gg::Engine engine({kSampleRate, kMaxBlock, kSaturatedCapacity, 42});
    engine.setSource(source);
    gg::CloudParams p = kParams;
    p.densityHz = 4000.0;
    p.grainDurationSec = 0.05;
    engine.setCloudParams(p);
    engine.setEventSink(&GrainLog::record, &r.log);
    constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
    r.audio.left.assign(static_cast<std::size_t>(kSaturatedFrames), kUnwritten);
    r.audio.right.assign(static_cast<std::size_t>(kSaturatedFrames), kUnwritten);
    int64_t done = 0;
    for (std::size_t next = 0; done < kSaturatedFrames; ++next)
    {
        const uint32_t want = blockSizes[next % blockSizes.size()];
        const auto n = static_cast<uint32_t>(std::min<int64_t>(want, kSaturatedFrames - done));
        float *out[2] = {r.audio.left.data() + done, r.audio.right.data() + done};
        engine.process(out, 2, n);
        done += n;
    }
    r.dropped = engine.droppedGrains();
}

// The most grains the pool stores at once when every block is blockSize frames and retirement
// waits for the block end: those carried in from earlier blocks (still sounding at the block's
// start) plus those spawned in it. Above 2 × capacity, storage filled and admit() compacted early.
std::size_t peakStoredWithoutEarlyRetirement(const std::vector<gg::GrainSpec> &grains,
                                             int64_t blockSize)
{
    std::size_t peak = 0;
    for (int64_t start = 0; start < kSaturatedFrames; start += blockSize)
    {
        std::size_t stored = 0;
        for (const gg::GrainSpec &g : grains)
        {
            const bool carried = g.onsetSample < start && gg::grainEndSample(g) > start;
            const bool spawned = g.onsetSample >= start && g.onsetSample < start + blockSize;
            stored += carried || spawned ? 1 : 0;
        }
        peak = std::max(peak, stored);
    }
    return peak;
}
} // namespace

namespace
{
// A stereo exponential sweep from 40 Hz to 8 kHz over kSourceFrames at 48 kHz, amplitude 0.8; the
// right channel a quarter turn ahead.
std::shared_ptr<const gg::SampleBuffer> sweepSource()
{
    auto buf = std::make_shared<gg::SampleBuffer>(2, kSourceFrames, kSampleRate);
    const double seconds = static_cast<double>(kSourceFrames) / kSampleRate;
    const double k = std::log(8000.0 / 40.0) / seconds;
    for (uint32_t c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < kSourceFrames; ++i)
        {
            const double t = static_cast<double>(i) / kSampleRate;
            const double turns = 40.0 * (std::exp(k * t) - 1.0) / k + 0.25 * c;
            buf->channel(c)[i] = static_cast<float>(0.8 * std::sin(2.0 * std::numbers::pi * turns));
        }
    }
    return buf;
}
} // namespace

TEST_CASE("hq renders are identical across block sizes", "[determinism][mip]")
{
    // The HQ tier (spec §6.2) at pitch +19, rate 2^(19/12) (about 3.0), over a stereo sweep: every
    // grain latches mip level 1 (spec §6.3) and reads it at half its positions, stretched by the
    // residual rate's 1 / 1.5.
    const auto source = gg::PreparedSource::make(sweepSource());
    REQUIRE(source->levelCount() == 5);
    const auto run = [&](const std::vector<uint32_t> &blockSizes, GrainLog &log) {
        gg::Engine engine(config(42));
        engine.setPreparedSource(source);
        gg::CloudParams p = kParams;
        p.pitchSemitones = 19.0;
        p.quality = gg::Tier::HQ;
        engine.setCloudParams(p);
        engine.setEventSink(&GrainLog::record, &log);
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
    };
    GrainLog refLog;
    const Render ref = run({kMaxBlock}, refLog);
    REQUIRE(allFinite(ref.left));
    REQUIRE(allFinite(ref.right));
    REQUIRE(peak(ref.left) > 0.05f);
    REQUIRE_FALSE(sameBits(ref.left, ref.right));
    REQUIRE_FALSE(refLog.overflowed);
    REQUIRE(refLog.grains.size() > 100);
    REQUIRE(std::all_of(refLog.grains.begin(), refLog.grains.end(), [](const gg::GrainSpec &g) {
        return g.tier == gg::Tier::HQ && g.level == 1 && g.rate > 2.9 && g.rate < 3.1;
    }));

    const auto random = randomSplit();
    for (const std::vector<uint32_t> &blockSizes :
         {std::vector<uint32_t>{1}, {32}, {64}, {511}, {4096}, random})
    {
        INFO("first block size = " << blockSizes.front() << " of " << blockSizes.size());
        GrainLog log;
        const Render r = run(blockSizes, log);
        REQUIRE(log.grains == refLog.grains);
        REQUIRE(sameBits(ref.left, r.left));
        REQUIRE(sameBits(ref.right, r.right));
    }
}

TEST_CASE("saturated pool is block-size invariant", "[determinism]")
{
    const auto source = sineSource();
    SaturatedRender ref;
    renderSaturated(source, {kMaxBlock}, ref);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(peak(ref.audio.left) > 0.05f);
    REQUIRE_FALSE(ref.log.overflowed);
    // Guard against a vacuous pass: the pool saturated, and in the reference's 4096-frame blocks
    // storage filled, so admit() took its early render-and-retire path.
    REQUIRE(ref.dropped > 0);
    REQUIRE(ref.log.grains.size() > 2 * kSaturatedCapacity);
    REQUIRE(peakStoredWithoutEarlyRetirement(ref.log.grains, kMaxBlock) > 2 * kSaturatedCapacity);

    const auto random = randomSplit();
    for (const auto &blockSizes :
         {std::vector<uint32_t>{1}, std::vector<uint32_t>{7}, std::vector<uint32_t>{64},
          std::vector<uint32_t>{511}, std::vector<uint32_t>{4096}, random})
    {
        INFO("first block size = " << blockSizes.front() << " of " << blockSizes.size());
        SaturatedRender r;
        renderSaturated(source, blockSizes, r);
        REQUIRE_FALSE(r.log.overflowed);
        REQUIRE(r.log.grains == ref.log.grains);
        REQUIRE(r.dropped == ref.dropped);
        REQUIRE(sameBits(ref.audio.left, r.audio.left));
        REQUIRE(sameBits(ref.audio.right, r.audio.right));
    }
}

namespace
{
// A stereo 330 Hz sine at half the amplitude of sineSource, for the swap below.
std::shared_ptr<const gg::SampleBuffer> otherSource()
{
    auto buf = std::make_shared<gg::SampleBuffer>(2, kSourceFrames, kSampleRate);
    for (uint32_t c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < kSourceFrames; ++i)
        {
            const double phase = 2.0 * std::numbers::pi * (330.0 * i / kSampleRate + 0.25 * c);
            buf->channel(c)[i] = static_cast<float>(0.5 * std::sin(phase));
        }
    }
    return buf;
}

// Swaps 1.5 s in, at no multiple of any block size below.
constexpr int64_t kSwapAt = 72077;

struct SwapRender
{
    Render audio;
    GrainLog log;
};

// Renders kFrames at kParams from `first`, adopting `second` between the process() calls at
// kSwapAt, with blockSizes in turn (cycling), the call before the swap cut short to end there.
void renderSwap(const gg::PreparedSource &first, const gg::PreparedSource &second,
                const std::vector<uint32_t> &blockSizes, SwapRender &r)
{
    gg::Engine engine(config(42));
    REQUIRE(engine.adoptSource(&first));
    engine.setCloudParams(kParams);
    engine.setEventSink(&GrainLog::record, &r.log);
    constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
    r.audio.left.assign(static_cast<std::size_t>(kFrames), kUnwritten);
    r.audio.right.assign(static_cast<std::size_t>(kFrames), kUnwritten);
    int64_t done = 0;
    std::size_t next = 0;
    for (const int64_t end : {kSwapAt, kFrames})
    {
        while (done < end)
        {
            const uint32_t want = blockSizes[next++ % blockSizes.size()];
            const auto n = static_cast<uint32_t>(std::min<int64_t>(want, end - done));
            float *out[2] = {r.audio.left.data() + done, r.audio.right.data() + done};
            engine.process(out, 2, n);
            done += n;
        }
        if (end == kSwapAt)
        {
            REQUIRE(engine.adoptSource(&second));
        }
    }
}
} // namespace

TEST_CASE("a source swap is block-size invariant", "[determinism]")
{
    const auto bufferA = sineSource();
    const auto a = gg::PreparedSource::make(bufferA);
    const auto b = gg::PreparedSource::make(otherSource());
    SwapRender ref;
    renderSwap(*a, *b, {kMaxBlock}, ref);
    REQUIRE_FALSE(ref.log.overflowed);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(peak(ref.audio.left) > 0.05f);
    // Grains of both sources, and old ones sounding past the swap.
    REQUIRE(std::any_of(ref.log.grains.begin(), ref.log.grains.end(), [](const gg::GrainSpec &g) {
        return g.onsetSample < kSwapAt && gg::grainEndSample(g) > kSwapAt + 100;
    }));
    REQUIRE(std::any_of(ref.log.grains.begin(), ref.log.grains.end(),
                        [](const gg::GrainSpec &g) { return g.onsetSample >= kSwapAt; }));

    // Not vacuous: up to the swap it is setSource's render of A, and after it it isn't.
    const auto unswapped = render(bufferA, 42, {kMaxBlock});
    const auto same = [](const std::vector<float> &x, const std::vector<float> &y, int64_t from,
                         int64_t to) {
        return std::memcmp(x.data() + from, y.data() + from,
                           static_cast<std::size_t>(to - from) * sizeof(float)) == 0;
    };
    REQUIRE(same(ref.audio.left, unswapped.left, 0, kSwapAt));
    REQUIRE(same(ref.audio.right, unswapped.right, 0, kSwapAt));
    REQUIRE_FALSE(same(ref.audio.left, unswapped.left, kSwapAt, kFrames));

    const auto random = randomSplit();
    for (const std::vector<uint32_t> &blockSizes :
         {std::vector<uint32_t>{1}, {32}, {64}, {511}, {4096}, random})
    {
        INFO("first block size = " << blockSizes.front() << " of " << blockSizes.size());
        SwapRender r;
        renderSwap(*a, *b, blockSizes, r);
        REQUIRE_FALSE(r.log.overflowed);
        REQUIRE(r.log.grains == ref.log.grains);
        REQUIRE(sameBits(ref.audio.left, r.audio.left));
        REQUIRE(sameBits(ref.audio.right, r.audio.right));
    }
}

namespace
{
// Reseeds 1.2 s in, at no multiple of any block size below.
constexpr int64_t kReseedAt = 57611;

// Renders kFrames at kParams over sineSource with seed 42, calling setSeed(newSeed) between the
// process() calls at kReseedAt (none when newSeed is 42), with blockSizes in turn (cycling), the
// call before the reseed cut short to end there.
void renderReseed(const std::shared_ptr<const gg::SampleBuffer> &source, uint64_t newSeed,
                  const std::vector<uint32_t> &blockSizes, SwapRender &r)
{
    gg::Engine engine(config(42));
    engine.setSource(source);
    engine.setCloudParams(kParams);
    engine.setEventSink(&GrainLog::record, &r.log);
    constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
    r.audio.left.assign(static_cast<std::size_t>(kFrames), kUnwritten);
    r.audio.right.assign(static_cast<std::size_t>(kFrames), kUnwritten);
    int64_t done = 0;
    std::size_t next = 0;
    for (const int64_t end : {kReseedAt, kFrames})
    {
        while (done < end)
        {
            const uint32_t want = blockSizes[next++ % blockSizes.size()];
            const auto n = static_cast<uint32_t>(std::min<int64_t>(want, end - done));
            float *out[2] = {r.audio.left.data() + done, r.audio.right.data() + done};
            engine.process(out, 2, n);
            done += n;
        }
        if (end == kReseedAt && newSeed != 42)
        {
            engine.setSeed(newSeed);
        }
    }
}
} // namespace

TEST_CASE("setSeed changes the next grains only", "[determinism]")
{
    // A seed set between two process() calls keys the grains whose onsets come after it (a
    // loaded state's seed, sub-project 5's spec §7); the grains already sounding keep their
    // draws, and the grain index k keeps counting, so the render after it is not a fresh
    // engine's at the new seed. Pinned: up to the reseed it is seed 42's render bit for bit,
    // after it it differs, and it is the same at every block size.
    const auto source = sineSource();
    SwapRender unseeded;
    renderReseed(source, 42, {kMaxBlock}, unseeded);
    SwapRender ref;
    renderReseed(source, 5, {kMaxBlock}, ref);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE_FALSE(ref.log.overflowed);

    const auto same = [](const std::vector<float> &x, const std::vector<float> &y, int64_t from,
                         int64_t to) {
        return std::memcmp(x.data() + from, y.data() + from,
                           static_cast<std::size_t>(to - from) * sizeof(float)) == 0;
    };
    REQUIRE(same(ref.audio.left, unseeded.audio.left, 0, kReseedAt));
    REQUIRE(same(ref.audio.right, unseeded.audio.right, 0, kReseedAt));
    REQUIRE_FALSE(same(ref.audio.left, unseeded.audio.left, kReseedAt, kFrames));
    REQUIRE_FALSE(same(ref.audio.right, unseeded.audio.right, kReseedAt, kFrames));
    // The grain log agrees: the grains before the reseed are seed 42's, the index runs on.
    REQUIRE_FALSE(ref.log.grains == unseeded.log.grains);
    for (std::size_t i = 0; i < ref.log.grains.size() && i < unseeded.log.grains.size(); ++i)
    {
        if (unseeded.log.grains[i].onsetSample >= kReseedAt)
        {
            break;
        }
        REQUIRE(ref.log.grains[i] == unseeded.log.grains[i]);
    }

    const auto random = randomSplit();
    for (const std::vector<uint32_t> &blockSizes :
         {std::vector<uint32_t>{kMaxBlock}, {1}, {64}, {511}, random})
    {
        INFO("first block size = " << blockSizes.front() << " of " << blockSizes.size());
        SwapRender r;
        renderReseed(source, 5, blockSizes, r);
        REQUIRE_FALSE(r.log.overflowed);
        REQUIRE(r.log.grains == ref.log.grains);
        REQUIRE(sameBits(ref.audio.left, r.audio.left));
        REQUIRE(sameBits(ref.audio.right, r.audio.right));
    }
}

namespace
{
// A source-kind switch (spec §6.4): Sample to Live at 0.5 s and back at 1 s, between the
// process() calls at those frames, over liveNoise() into the 16 s ring. 0.3 s grains, so grains
// of the old kind sound across each switch.
constexpr int64_t kToLive = 24000;
constexpr int64_t kToSample = 48000;

struct KindRender
{
    Render audio;
    GrainLog log;
};

void renderKindSwitch(const LiveInput &input, const std::shared_ptr<const gg::SampleBuffer> &source,
                      const std::vector<uint32_t> &blockSizes, KindRender &r)
{
    gg::EngineConfig c = config(42);
    c.liveSeconds = 16.0;
    gg::Engine engine(c);
    engine.setSource(source);
    gg::CloudParams p = kParams;
    p.grainDurationSec = 0.3;
    engine.setCloudParams(p);
    engine.setEventSink(&GrainLog::record, &r.log);
    constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
    r.audio.left.assign(static_cast<std::size_t>(kLiveFrames), kUnwritten);
    r.audio.right.assign(static_cast<std::size_t>(kLiveFrames), kUnwritten);
    int64_t done = 0;
    std::size_t next = 0;
    for (const int64_t end : {kToLive, kToSample, kLiveFrames})
    {
        while (done < end)
        {
            const uint32_t want = blockSizes[next++ % blockSizes.size()];
            const auto n = static_cast<uint32_t>(std::min<int64_t>(want, end - done));
            const float *in[2] = {input.left.data() + done, input.right.data() + done};
            float *out[2] = {r.audio.left.data() + done, r.audio.right.data() + done};
            engine.process(in, 2, out, 2, n);
            done += n;
        }
        engine.setSourceMode(end == kToLive ? gg::SourceMode::Live : gg::SourceMode::Sample);
    }
}
} // namespace

TEST_CASE("a source kind switch is block-size invariant", "[determinism]")
{
    const auto input = liveNoise();
    const auto source = sineSource();
    KindRender ref;
    renderKindSwitch(input, source, {kMaxBlock}, ref);
    REQUIRE_FALSE(ref.log.overflowed);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(peak(ref.audio.left) > 0.05f);
    // Not vacuous: each kind's grains in its span, and the old kind's sounding past each switch.
    const auto any = [&](auto &&pred) {
        return std::any_of(ref.log.grains.begin(), ref.log.grains.end(), pred);
    };
    const auto isLive = [](const gg::GrainSpec &g) {
        return g.sourceSlot == gg::Engine::kLiveSlot;
    };
    REQUIRE(any([&](const gg::GrainSpec &g) {
        return !isLive(g) && g.onsetSample < kToLive && gg::grainEndSample(g) > kToLive + 4800;
    }));
    REQUIRE(any([&](const gg::GrainSpec &g) {
        return isLive(g) && g.onsetSample < kToSample && gg::grainEndSample(g) > kToSample + 4800;
    }));
    bool kinds = true;
    for (const gg::GrainSpec &g : ref.log.grains)
    {
        kinds = kinds && isLive(g) == (g.onsetSample >= kToLive && g.onsetSample < kToSample);
    }
    REQUIRE(kinds);
    REQUIRE(any([&](const gg::GrainSpec &g) { return g.onsetSample >= kToSample; }));

    const auto random = randomSplit();
    for (const std::vector<uint32_t> &blockSizes :
         {std::vector<uint32_t>{1}, {32}, {64}, {511}, {4096}, random})
    {
        INFO("first block size = " << blockSizes.front() << " of " << blockSizes.size());
        KindRender r;
        renderKindSwitch(input, source, blockSizes, r);
        REQUIRE_FALSE(r.log.overflowed);
        REQUIRE(r.log.grains == ref.log.grains);
        REQUIRE(sameBits(ref.audio.left, r.audio.left));
        REQUIRE(sameBits(ref.audio.right, r.audio.right));
    }
}

namespace
{
// An overload case: periodic onsets of grains of one length into one engine of the given
// capacity and quota, over the stereo sine.
struct OverloadCase
{
    int64_t frames;
    double densityHz;
    double grainDurationSec;
    uint32_t capacity;
    uint64_t quota;
    bool drops; ///< whether the quota fills before the oldest grain is half-way, so some drop
};

// The overload ladder's exit case (spec §6.5): 16,384 one-second grains a second into one engine
// of capacity 16,384 at a quota of 1,024 Normal stereo grains (4,096 units), for 1.5 s. The
// quota fills within the first 3,000 samples; the grains drawn after it drop until the oldest is
// half-way, at 0.5 s, and from then on each draw steals one grain while the oldest unstolen one
// is half-way (ruling ES-T2a).
constexpr OverloadCase kExitCase{72000, 16384.0, 1.0, gg::kMaxGrainCapacity, 4096, true};
// The same at a small size, which always runs (ruling ES-T2b): 4,096 grains of 100 ms a second
// into capacity 4,096 at a quota of 256 grains (1,024 units), for 0.5 s. The quota fills at
// 62.5 ms, after the oldest grain is half-way (50 ms), so every draw past it steals the oldest
// unstolen grain, which is then 62.5 ms old: none drops.
constexpr OverloadCase kSmallCase{24000, 4096.0, 0.1, 4096, 1024, false};

struct Steal
{
    uint64_t index;
    int64_t onset;
    friend bool operator==(const Steal &, const Steal &) = default;
};

struct OverloadRender
{
    Render audio;
    std::vector<Steal> steals;
    bool overflowed{false};
    uint64_t spawned{0};
    uint64_t dropped{0};
    uint64_t lowered{0};
    uint64_t stolen{0};

    OverloadRender() { steals.reserve(16384); }

    static void spawn(void *ctx, uint32_t, const gg::GrainSpec &)
    {
        ++static_cast<OverloadRender *>(ctx)->spawned;
    }
    static void steal(void *ctx, uint32_t, uint64_t index, int64_t onset)
    {
        auto *r = static_cast<OverloadRender *>(ctx);
        if (r->steals.size() < r->steals.capacity())
        {
            r->steals.push_back({index, onset});
        }
        else
        {
            r->overflowed = true;
        }
    }
};

void renderOverload(const OverloadCase &c, const std::shared_ptr<const gg::SampleBuffer> &source,
                    const std::vector<uint32_t> &blockSizes, OverloadRender &r)
{
    gg::Engine engine({kSampleRate, kMaxBlock, c.capacity, 42});
    engine.setSource(source);
    gg::CloudParams p = kParams;
    p.densityHz = c.densityHz; // periodic: 48,000 / 2^n is exact
    p.distribution = 0.0;
    p.grainDurationSec = c.grainDurationSec;
    engine.setCloudParams(p);
    engine.setQuota(c.quota);
    engine.setEventSink(&OverloadRender::spawn, &r);
    engine.setStealSink(&OverloadRender::steal, &r);
    constexpr float kUnwritten = std::numeric_limits<float>::quiet_NaN();
    r.audio.left.assign(static_cast<std::size_t>(c.frames), kUnwritten);
    r.audio.right.assign(static_cast<std::size_t>(c.frames), kUnwritten);
    int64_t done = 0;
    for (std::size_t next = 0; done < c.frames; ++next)
    {
        const uint32_t want = blockSizes[next % blockSizes.size()];
        const auto n = static_cast<uint32_t>(std::min<int64_t>(want, c.frames - done));
        float *out[2] = {r.audio.left.data() + done, r.audio.right.data() + done};
        engine.process(out, 2, n);
        done += n;
    }
    r.dropped = engine.droppedGrains();
    r.lowered = engine.loweredGrains();
    r.stolen = engine.stolenGrains();
}

// Renders the case at 4096-frame blocks, the reference, then at 1, 32, 64 and 511 and a random
// split: identical bytes, counters and steal lists. The reference isn't rendered twice.
void checkOverloadInvariance(const OverloadCase &c)
{
    const auto source = sineSource();
    OverloadRender ref;
    renderOverload(c, source, {kMaxBlock}, ref);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(peak(ref.audio.left) > 0.05f);
    REQUIRE_FALSE(ref.overflowed);
    // Guard against a vacuous pass: the steal was taken, and the drop where the case reaches it.
    REQUIRE((ref.dropped > 0) == c.drops);
    REQUIRE(ref.stolen > 0);
    REQUIRE(ref.steals.size() == ref.stolen);
    // Every onset in the render, periodic: frames × density / rate of them.
    const auto onsets =
        static_cast<uint64_t>(std::ceil(static_cast<double>(c.frames) * c.densityHz / kSampleRate));
    REQUIRE(ref.spawned + ref.dropped == onsets);
    // Nothing steals before the oldest grain is half-way.
    REQUIRE(ref.steals.front().onset >=
            static_cast<int64_t>(0.5 * c.grainDurationSec * kSampleRate));

    const auto random = randomSplit();
    for (const auto &blockSizes : {std::vector<uint32_t>{1}, std::vector<uint32_t>{32},
                                   std::vector<uint32_t>{64}, std::vector<uint32_t>{511}, random})
    {
        INFO("first block size = " << blockSizes.front() << " of " << blockSizes.size());
        OverloadRender r;
        renderOverload(c, source, blockSizes, r);
        REQUIRE_FALSE(r.overflowed);
        REQUIRE(r.spawned == ref.spawned);
        REQUIRE(r.dropped == ref.dropped);
        REQUIRE(r.lowered == ref.lowered);
        REQUIRE(r.stolen == ref.stolen);
        REQUIRE(r.steals == ref.steals);
        REQUIRE(sameBits(ref.audio.left, r.audio.left));
        REQUIRE(sameBits(ref.audio.right, r.audio.right));
    }
}
} // namespace

// Rendering over a thousand grains for 1.5 s at six block splits takes minutes in Debug, so it
// is slow (ruling ES-T2b); its small sibling below always runs.
TEST_CASE("the overload ladder is block-size invariant", "[determinism][budget][slow]")
{
    checkOverloadInvariance(kExitCase);
}

TEST_CASE("the overload ladder is block-size invariant at a small size", "[determinism][budget]")
{
    checkOverloadInvariance(kSmallCase);
}
