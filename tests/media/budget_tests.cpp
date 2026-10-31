// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The memory budget's accounting (gg/media/MemoryBudget.h, spec §6.5): what a sample source and a
// ring hold, and the cap's verdict and refusal.

#include <cstdint>
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "gg/media/MemoryBudget.h"
#include "gg/source/LiveSource.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/SampleBuffer.h"

namespace
{
constexpr uint64_t kMiB = uint64_t{1} << 20;

uint64_t ceilDiv(uint64_t a, uint64_t b) { return (a + b - 1) / b; }
} // namespace

TEST_CASE("sampleBytes counts the planar buffer, the pairs and the levels", "[media]")
{
    using gg::media::sampleBytes;
    // Planar, then the interleaved copy of a stereo source: twice the planar bytes.
    REQUIRE(sampleBytes(48000, 2, true, 1) == uint64_t{48000} * 2 * 4 * 2);
    REQUIRE(sampleBytes(48000, 2, false, 1) == uint64_t{48000} * 2 * 4);
    REQUIRE(sampleBytes(48000, 1, false, 1) == uint64_t{48000} * 4);

    // Levels 1 to 4 add ceil(frames / 2^n) frames of `channels` floats each.
    uint64_t levels = 0;
    for (uint32_t n = 1; n < 5; ++n)
    {
        levels += ceilDiv(48000, uint64_t{1} << n) * 8;
    }
    REQUIRE(sampleBytes(48000, 2, true, 5) == uint64_t{48000} * 2 * 4 * 2 + levels);
    // An odd count rounds each level up.
    REQUIRE(sampleBytes(7, 1, false, 3) == 7 * 4 + 4 * 4 + 2 * 4);
    REQUIRE(sampleBytes(0, 2, false, 1) == 0);
    REQUIRE(sampleBytes(-5, 2, true, 3) == 0);
}

TEST_CASE("sourceBytes derives a source's bytes from its buffer, pairs and levels", "[media]")
{
    const auto stereo = gg::PreparedSource::make(std::make_shared<gg::SampleBuffer>(2, 4800, 48e3));
    REQUIRE(gg::media::sourceBytes(*stereo) ==
            gg::media::sampleBytes(4800, 2, true, stereo->levelCount()));
    // Since M2 sub-project 3 a source carries its pyramid: levels 1 to 4 of 2,400, 1,200, 600
    // and 300 frames, each one copy of the channels.
    REQUIRE(stereo->levelCount() == 5);
    REQUIRE(gg::media::sourceBytes(*stereo) ==
            uint64_t{4800} * 2 * 4 * 2 + uint64_t{2400 + 1200 + 600 + 300} * 2 * 4);

    const auto mono = gg::PreparedSource::make(std::make_shared<gg::SampleBuffer>(1, 4800, 48e3));
    REQUIRE(gg::media::sourceBytes(*mono) == uint64_t{4800 + 2400 + 1200 + 600 + 300} * 4);
}

TEST_CASE("ringBytes is the ring storage's bytes", "[media]")
{
    // The ring's storage shape (LiveSource.h, RingStorage): C = N + maxBlockSize slots and 15
    // guard slots (the sinc's 16 taps, M2 sub-project 3), 2 floats each.
    const int64_t frames = 16 * 48000;
    gg::LiveSource ring;
    ring.prepare(48000.0, 16.0, 4096);
    REQUIRE(ring.frames() == frames);
    const uint64_t storage = uint64_t(ring.view().capacity + 15) * 2 * sizeof(float);
    REQUIRE(gg::media::ringBytes(frames, 4096, 1) == storage);
    REQUIRE(gg::media::ringBytes(frames, 4096, 1) == uint64_t(frames + 4096 + 15) * 8);

    // Each level n adds ceil(C / 2^n) + 15 slots.
    uint64_t levels = 0;
    for (uint32_t n = 1; n < 5; ++n)
    {
        levels += (ceilDiv(uint64_t(frames + 4096), uint64_t{1} << n) + 15) * 8;
    }
    REQUIRE(gg::media::ringBytes(frames, 4096, 5) == storage + levels);
    REQUIRE(gg::media::ringBytes(0, 4096, 1) == 0);

    // A ring's storage holds its own levels (M2 sub-project 2's spec §6.9): levels 1 to 3 for any
    // ring of 1 s or more, none for one too short for a level of 64 frames.
    for (const int64_t n : {frames, int64_t{48000}, int64_t{64}, int64_t{200}})
    {
        INFO("frames " << n);
        const gg::RingStorage ringStorage(n, 4096);
        REQUIRE(ringStorage.levelCount() == gg::RingStorage::levelCountFor(n));
        REQUIRE(ringStorage.bytes() == gg::media::ringBytes(n, 4096, ringStorage.levelCount()));
    }
    STATIC_REQUIRE(gg::RingStorage::levelCountFor(16 * 48000) == 4);
    STATIC_REQUIRE(gg::RingStorage::levelCountFor(200) == 2);
    STATIC_REQUIRE(gg::RingStorage::levelCountFor(64) == 1);
}

TEST_CASE("the memory budget fits what the cap leaves and refuses the rest", "[media]")
{
    gg::media::MemoryBudget budget{1024 * kMiB, 904 * kMiB};
    REQUIRE(budget.fits(0));
    REQUIRE(budget.fits(120 * kMiB));
    REQUIRE_FALSE(budget.fits(120 * kMiB + 1));
    REQUIRE_FALSE(budget.fits(UINT64_MAX)); // no overflow
    REQUIRE(budget.refusal(230 * kMiB) ==
            "over the memory cap: needs 230 MB, 120 MB free of 1024 MB");
    // The need rounds up and the free space down, so a refusal never reads as if it fits.
    REQUIRE(budget.refusal(120 * kMiB + 1) ==
            "over the memory cap: needs 121 MB, 120 MB free of 1024 MB");
    budget.usedBytes = 904 * kMiB + 1;
    REQUIRE(budget.refusal(120 * kMiB) ==
            "over the memory cap: needs 120 MB, 119 MB free of 1024 MB");

    // Used past the cap (a lowered cap): nothing fits, and nothing is free.
    budget.usedBytes = 2048 * kMiB;
    REQUIRE_FALSE(budget.fits(0));
    REQUIRE_FALSE(budget.fits(1));
    REQUIRE(budget.refusal(1) == "over the memory cap: needs 1 MB, 0 MB free of 1024 MB");
}
