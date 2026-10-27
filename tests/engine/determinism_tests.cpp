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
#include <memory>
#include <numbers>
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

// 37 Hz, 0.083 s grains at position 0.4 with jitter 0.3, up 5 semitones, gain 0.7.
constexpr gg::StreamParams kParams{37.0, 0.083, 0.4, 0.3, 5.0, 0.7f};

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
// last call cut short to end at kFrames.
Render render(const std::shared_ptr<const gg::SampleBuffer> &source, uint64_t seed,
              const std::vector<uint32_t> &blockSizes)
{
    gg::Engine engine(config(seed));
    engine.setSource(source);
    engine.setStreamParams(kParams);
    Render r{std::vector<float>(static_cast<std::size_t>(kFrames), 1.0f),
             std::vector<float>(static_cast<std::size_t>(kFrames), 1.0f)};
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
    // Guard against a vacuous pass: the reference is audible and its channels differ.
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
    REQUIRE(peak(first.left) > 0.05f);
    REQUIRE(sameBits(first.left, second.left));
    REQUIRE(sameBits(first.right, second.right));
}

TEST_CASE("different seeds differ", "[determinism]")
{
    // The seed only reaches the output through the position jitter (0.3 in kParams).
    const auto source = sineSource();
    const auto one = render(source, 1, {kMaxBlock});
    const auto two = render(source, 2, {kMaxBlock});
    REQUIRE(peak(one.left) > 0.05f);
    REQUIRE(peak(two.left) > 0.05f);
    REQUIRE_FALSE(sameBits(one.left, two.left));
    REQUIRE_FALSE(sameBits(one.right, two.right));
}
