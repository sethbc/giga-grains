// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/source/InterleavedStereo.h"
#include "gg/source/SampleBuffer.h"

TEST_CASE("sample buffer holds separate zeroed channels", "[source]")
{
    gg::SampleBuffer buf(2, 100, 44100.0);
    REQUIRE(buf.channels() == 2);
    REQUIRE(buf.frames() == 100);
    REQUIRE(buf.sampleRate() == 44100.0);

    bool allZero = true;
    for (uint32_t c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < 100; ++i)
        {
            allZero = allZero && buf.channel(c)[i] == 0.0f;
        }
    }
    REQUIRE(allZero);

    for (int64_t i = 0; i < 100; ++i)
    {
        buf.channel(0)[i] = 1.0f;
    }
    bool rightUntouched = true;
    for (int64_t i = 0; i < 100; ++i)
    {
        rightUntouched = rightUntouched && buf.channel(1)[i] == 0.0f;
    }
    REQUIRE(rightUntouched);

    const gg::SampleBuffer &cbuf = buf;
    REQUIRE(cbuf.channel(0) == buf.channel(0));
    REQUIRE(cbuf.channel(0)[99] == 1.0f);
    REQUIRE(buf.channel(2) == nullptr);
    REQUIRE(cbuf.channel(2) == nullptr);
}

TEST_CASE("sample buffer accepts empty shapes and rejects invalid ones", "[source]")
{
    const gg::SampleBuffer empty(2, 0, 48000.0);
    REQUIRE(empty.channels() == 2);
    REQUIRE(empty.frames() == 0);

    const gg::SampleBuffer noChannels(0, 10, 48000.0);
    REQUIRE(noChannels.channel(0) == nullptr);

    REQUIRE_THROWS_AS(gg::SampleBuffer(1, -1, 48000.0), std::invalid_argument);
    REQUIRE_THROWS_AS(gg::SampleBuffer(1, 10, 0.0), std::invalid_argument);
    REQUIRE_THROWS_AS(gg::SampleBuffer(1, 10, -48000.0), std::invalid_argument);
    REQUIRE_THROWS_AS(gg::SampleBuffer(1, 10, std::numeric_limits<double>::quiet_NaN()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(gg::SampleBuffer(1, 10, std::numeric_limits<double>::infinity()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(gg::SampleBuffer(std::numeric_limits<uint32_t>::max(),
                                       std::numeric_limits<int64_t>::max(), 48000.0),
                      std::length_error);
}

TEST_CASE("sample buffer is move-only and a moved-from buffer is empty", "[source]")
{
    STATIC_REQUIRE(!std::is_copy_constructible_v<gg::SampleBuffer>);
    STATIC_REQUIRE(!std::is_copy_assignable_v<gg::SampleBuffer>);
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<gg::SampleBuffer>);
    STATIC_REQUIRE(std::is_nothrow_move_assignable_v<gg::SampleBuffer>);

    gg::SampleBuffer a(2, 100, 44100.0);
    a.channel(1)[99] = 0.5f;
    const float *samples = a.channel(0);

    gg::SampleBuffer b = std::move(a);
    REQUIRE(a.channels() == 0);
    REQUIRE(a.frames() == 0);
    REQUIRE(a.channel(0) == nullptr);
    REQUIRE(a.sampleRate() == 44100.0);
    REQUIRE(b.channels() == 2);
    REQUIRE(b.frames() == 100);
    REQUIRE(b.channel(0) == samples); // the samples moved, not copied
    REQUIRE(b.channel(1)[99] == 0.5f);

    // Move assignment empties its source the same way.
    gg::SampleBuffer c(1, 10, 48000.0);
    c = std::move(b);
    REQUIRE(b.channels() == 0);
    REQUIRE(b.frames() == 0);
    REQUIRE(b.channel(0) == nullptr);
    REQUIRE(c.channels() == 2);
    REQUIRE(c.frames() == 100);
    REQUIRE(c.sampleRate() == 44100.0);
    REQUIRE(c.channel(0) == samples);
}

TEST_CASE("sample buffer adopts a vector's samples without copying them", "[source]")
{
    // Channel c's frames at c x frames, as the buffer lays them out.
    std::vector<float> samples(2 * 100);
    samples[0] = 0.25f;
    samples[100] = 0.5f;
    samples[199] = 0.75f;
    const float *data = samples.data();

    const gg::SampleBuffer buf(2, 100, 48000.0, std::move(samples));
    REQUIRE(buf.channels() == 2);
    REQUIRE(buf.frames() == 100);
    REQUIRE(buf.sampleRate() == 48000.0);
    REQUIRE(buf.channel(0) == data); // taken, not copied
    REQUIRE(buf.channel(1) == data + 100);
    REQUIRE(buf.channel(0)[0] == 0.25f);
    REQUIRE(buf.channel(1)[0] == 0.5f);
    REQUIRE(buf.channel(1)[99] == 0.75f);

    // The shape must match the samples, and is checked as the other constructor checks it.
    REQUIRE_THROWS_AS(gg::SampleBuffer(2, 100, 48000.0, std::vector<float>(199)),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(gg::SampleBuffer(2, 100, 48000.0, std::vector<float>(201)),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(gg::SampleBuffer(1, 10, 0.0, std::vector<float>(10)), std::invalid_argument);
    REQUIRE_THROWS_AS(gg::SampleBuffer(1, -1, 48000.0, std::vector<float>()),
                      std::invalid_argument);
    const gg::SampleBuffer empty(2, 0, 48000.0, std::vector<float>());
    REQUIRE(empty.frames() == 0);
}

namespace
{
constexpr float kZero = 0.0f; // +0, bit for bit
} // namespace

TEST_CASE("interleaved stereo holds L, R per frame between zero guards", "[source]")
{
    // §16 lever 2: the SIMD sample kernel reads these floats in place of the planar channels, so
    // each must be the planar one's exact bits, a NaN's payload and a −0 included.
    constexpr int64_t kFrames = 37;
    constexpr int64_t kGuard = gg::InterleavedStereo::kGuardFrames;
    REQUIRE(kGuard == 3);
    std::vector<float> left(kFrames);
    std::vector<float> right(kFrames);
    for (int64_t f = 0; f < kFrames; ++f)
    {
        left[static_cast<std::size_t>(f)] = 1.0f + static_cast<float>(f);
        right[static_cast<std::size_t>(f)] = -0.5f - static_cast<float>(f);
    }
    left[5] = std::numeric_limits<float>::quiet_NaN();
    right[6] = -0.0f;

    gg::InterleavedStereo pairs;
    REQUIRE(pairs.data() == nullptr);
    REQUIRE(pairs.frames() == 0);
    pairs.assign(left.data(), right.data(), kFrames);
    REQUIRE(pairs.data() != nullptr);
    REQUIRE(pairs.frames() == kFrames);
    bool same = true;
    for (int64_t f = 0; f < kFrames; ++f)
    {
        const auto j = static_cast<std::size_t>(f);
        same = same && std::memcmp(&pairs.data()[2 * f], &left[j], sizeof(float)) == 0 &&
               std::memcmp(&pairs.data()[2 * f + 1], &right[j], sizeof(float)) == 0;
    }
    REQUIRE(same);
    bool guardsZero = true;
    for (int64_t k = 1; k <= kGuard; ++k)
    {
        for (int64_t c = 0; c < 2; ++c)
        {
            const float before = pairs.data()[-2 * k + c];
            const float after = pairs.data()[2 * (kFrames - 1 + k) + c];
            guardsZero = guardsZero && std::memcmp(&before, &kZero, sizeof(float)) == 0 &&
                         std::memcmp(&after, &kZero, sizeof(float)) == 0;
        }
    }
    REQUIRE(guardsZero);

    // A copy (or a copy assignment) reads its own floats, which outlive the original's.
    {
        gg::InterleavedStereo copy = pairs;
        gg::InterleavedStereo assigned;
        assigned = pairs;
        REQUIRE(copy.data() != pairs.data());
        REQUIRE(assigned.data() != pairs.data());
        gg::InterleavedStereo original = pairs;
        original.clear();
        REQUIRE(original.data() == nullptr);
        REQUIRE(copy.frames() == kFrames);
        REQUIRE(std::memcmp(copy.data() - 2 * kGuard, pairs.data() - 2 * kGuard,
                            static_cast<std::size_t>(2 * (kFrames + 2 * kGuard)) * sizeof(float)) ==
                0);
        REQUIRE(std::memcmp(assigned.data() - 2 * kGuard, pairs.data() - 2 * kGuard,
                            static_cast<std::size_t>(2 * (kFrames + 2 * kGuard)) * sizeof(float)) ==
                0);
    }

    // A shorter source replaces the copy; an empty one clears it.
    pairs.assign(right.data(), left.data(), 2);
    REQUIRE(pairs.frames() == 2);
    REQUIRE(pairs.data()[0] == right[0]);
    REQUIRE(pairs.data()[3] == left[1]);
    REQUIRE(pairs.data()[4] == 0.0f);
    pairs.assign(left.data(), right.data(), 0);
    REQUIRE(pairs.data() == nullptr);
    REQUIRE(pairs.frames() == 0);
}
