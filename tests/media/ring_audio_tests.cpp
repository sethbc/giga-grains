// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Rings as WAV (gg/media/RingAudio.h, spec §6.6): a ring's window encoded as float32 WAV bytes,
// and a saved ring restored into storage of the layer's Ring Length. Every test works in a folder
// of its own under GG_TEST_OUTPUT_DIR.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/SessionState.h"
#include "gg/media/Decode.h"
#include "gg/media/RingAudio.h"
#include "gg/media/Sha256.h"
#include "gg/media/WavBytes.h"
#include "gg/source/RingStorage.h"
#include "media/AudioFixtures.h"
#include "support/Utf8.h"

namespace
{
namespace fs = std::filesystem;
using gg::core::RingEntry;
using gg::media::encodeRing;
using gg::media::restoreRing;
using gg::media::ringToBuffer;

constexpr uint64_t kBound = gg::media::kDefaultDecodeBound;
constexpr uint32_t kBlock = 64;

fs::path freshDir(const char *name)
{
    const auto dir = fs::path(GG_TEST_OUTPUT_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

/// A ring's slots: capacity + 3 slots of 2 floats, frame f (absolute) at slot f mod capacity,
/// holding value(f, c). The window is `frames` frames from `oldest`.
std::vector<float> ringSlots(int64_t capacity, int64_t oldest, int64_t frames, double offset = 0.0)
{
    std::vector<float> slots(static_cast<std::size_t>(2 * (capacity + 3)), 0.0f);
    for (int64_t k = 0; k < frames; ++k)
    {
        const int64_t slot = (oldest + k) % capacity;
        slots[std::size_t(2 * slot)] = float(std::sin(0.01 * double(k) + offset));
        slots[std::size_t(2 * slot + 1)] = float(0.5 * std::cos(0.013 * double(k) + offset));
    }
    return slots;
}

bool sameBits(float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }

RingEntry entryOf(const gg::SampleBuffer &window, std::vector<int64_t> splices)
{
    RingEntry e;
    e.frames = window.frames();
    e.sampleRate = window.sampleRate();
    e.splices = std::move(splices);
    e.embed = true;
    return e;
}
} // namespace

TEST_CASE("a ring round-trips through WAV bytes", "[media]")
{
    constexpr int64_t kCapacity = 1000 + kBlock;
    auto slots = ringSlots(kCapacity, 1700, 800);       // the window wraps the slots
    slots[std::size_t(2 * (1700 % kCapacity))] = -0.0f; // the extremes survive
    slots[std::size_t(2 * (1701 % kCapacity) + 1)] = 3.5f;

    const auto window = ringToBuffer(slots.data(), kCapacity, 1700, 800, 48000.0);
    REQUIRE(window != nullptr);
    REQUIRE(window->channels() == 2);
    REQUIRE(window->frames() == 800);
    REQUIRE(window->sampleRate() == 48000.0);
    for (int64_t k = 0; k < 800; ++k)
    {
        const int64_t slot = (1700 + k) % kCapacity;
        REQUIRE(sameBits(window->channel(0)[k], slots[std::size_t(2 * slot)]));
        REQUIRE(sameBits(window->channel(1)[k], slots[std::size_t(2 * slot + 1)]));
    }

    const auto encoded = encodeRing(*window);
    REQUIRE(encoded.wavBytes == gg::media::encodeFloat32Wav(*window));
    REQUIRE(encoded.size == encoded.wavBytes.size());
    REQUIRE(encoded.sha256Hex == gg::media::sha256Hex(std::as_bytes(
                                     std::span(encoded.wavBytes.data(), encoded.wavBytes.size()))));

    const RingEntry entry = entryOf(*window, {100, 400});
    const auto restored = restoreRing(entry, encoded.wavBytes, 48000.0, 1000, kBlock, kBound);
    INFO(restored.error);
    REQUIRE(restored.error.empty());
    REQUIRE(restored.storage != nullptr);
    REQUIRE(restored.storage->frames() == 1000);
    REQUIRE(restored.frames == 800);
    REQUIRE(restored.splices == std::vector<int64_t>{100, 400});
    const float *out = restored.storage->slots();
    for (int64_t k = 0; k < 800; ++k)
    {
        REQUIRE(sameBits(out[2 * k], window->channel(0)[k]));
        REQUIRE(sameBits(out[2 * k + 1], window->channel(1)[k]));
    }
    // Frames no one wrote are silence.
    for (int64_t s = 800; s < restored.storage->capacity(); ++s)
    {
        REQUIRE(out[2 * s] == 0.0f);
        REQUIRE(out[2 * s + 1] == 0.0f);
    }
    // The guard slots equal slots 0 .. 2.
    const int64_t c = restored.storage->capacity();
    for (int64_t g = 0; g < gg::RingStorage::kGuardFrames; ++g)
    {
        REQUIRE(sameBits(out[2 * (c + g)], out[2 * g]));
        REQUIRE(sameBits(out[2 * (c + g) + 1], out[2 * g + 1]));
    }
}

TEST_CASE("a ring saved at another rate restores converted", "[media]")
{
    constexpr int64_t kCapacity = 4410 + kBlock;
    const auto slots = ringSlots(kCapacity, 0, 4410);
    const auto window = ringToBuffer(slots.data(), kCapacity, 0, 4410, 44100.0);
    const auto encoded = encodeRing(*window);
    const RingEntry entry = entryOf(*window, {1000, 2205, 4409});

    const auto restored = restoreRing(entry, encoded.wavBytes, 48000.0, 48000, kBlock, kBound);
    INFO(restored.error);
    REQUIRE(restored.error.empty());
    const int64_t expected = std::llround(4410.0 * 48000.0 / 44100.0);
    REQUIRE(restored.frames == expected);
    REQUIRE(restored.splices.size() == 3);
    REQUIRE(restored.splices[0] == std::llround(1000.0 * 48000.0 / 44100.0));
    REQUIRE(restored.splices[1] == std::llround(2205.0 * 48000.0 / 44100.0));
    REQUIRE(restored.splices[2] < restored.frames);
    // The converted audio is not silent and ends where it should.
    float peak = 0.0f;
    for (int64_t k = 0; k < restored.frames; ++k)
    {
        peak = std::max(peak, std::abs(restored.storage->slots()[2 * k]));
    }
    REQUIRE(peak > 0.1f);
}

TEST_CASE("a longer saved ring keeps its newest frames", "[media]")
{
    constexpr int64_t kCapacity = 1000 + kBlock;
    const auto slots = ringSlots(kCapacity, 0, 1000);
    const auto window = ringToBuffer(slots.data(), kCapacity, 0, 1000, 48000.0);
    const auto encoded = encodeRing(*window);
    const RingEntry entry = entryOf(*window, {100, 600, 700, 900});

    const auto restored = restoreRing(entry, encoded.wavBytes, 48000.0, 400, kBlock, kBound);
    INFO(restored.error);
    REQUIRE(restored.error.empty());
    REQUIRE(restored.storage->frames() == 400);
    REQUIRE(restored.frames == 400);
    for (int64_t k = 0; k < 400; ++k)
    {
        REQUIRE(sameBits(restored.storage->slots()[2 * k], window->channel(0)[600 + k]));
        REQUIRE(sameBits(restored.storage->slots()[2 * k + 1], window->channel(1)[600 + k]));
    }
    // Marks move with the frames: the one before the kept window (100) goes, and the one at its
    // first frame (600) becomes frame 0.
    REQUIRE(restored.splices == std::vector<int64_t>{0, 100, 300});
}

TEST_CASE("a shorter one leaves the rest silent", "[media]")
{
    constexpr int64_t kCapacity = 100 + kBlock;
    const auto slots = ringSlots(kCapacity, 0, 100);
    const auto window = ringToBuffer(slots.data(), kCapacity, 0, 100, 48000.0);
    const auto encoded = encodeRing(*window);
    const RingEntry entry = entryOf(*window, {50});

    const auto restored = restoreRing(entry, encoded.wavBytes, 48000.0, 400, kBlock, kBound);
    REQUIRE(restored.error.empty());
    REQUIRE(restored.storage->frames() == 400);
    REQUIRE(restored.frames == 100);
    REQUIRE(restored.splices == std::vector<int64_t>{50});
    for (int64_t k = 0; k < 100; ++k)
    {
        REQUIRE(sameBits(restored.storage->slots()[2 * k], window->channel(0)[k]));
    }
    for (int64_t s = 100; s < restored.storage->capacity(); ++s)
    {
        REQUIRE(restored.storage->slots()[2 * s] == 0.0f);
        REQUIRE(restored.storage->slots()[2 * s + 1] == 0.0f);
    }
}

TEST_CASE("a filed ring restores from its file, and a bad one gives an error", "[media]")
{
    const auto dir = freshDir("ring-audio-filed");
    constexpr int64_t kCapacity = 300 + kBlock;
    const auto slots = ringSlots(kCapacity, 0, 300);
    const auto window = ringToBuffer(slots.data(), kCapacity, 0, 300, 48000.0);
    const auto encoded = encodeRing(*window);
    const auto file = dir / "ring.wav";
    gg::test::writeFile(file, std::span(reinterpret_cast<const uint8_t *>(encoded.wavBytes.data()),
                                        encoded.wavBytes.size()));

    RingEntry entry = entryOf(*window, {});
    entry.embed = false;
    entry.absolutePath = gg::test::utf8(file);
    auto restored = restoreRing(entry, {}, 48000.0, 300, kBlock, kBound);
    INFO(restored.error);
    REQUIRE(restored.error.empty());
    REQUIRE(restored.frames == 300);

    entry.absolutePath = gg::test::utf8(dir / "gone.wav");
    restored = restoreRing(entry, {}, 48000.0, 300, kBlock, kBound);
    REQUIRE_FALSE(restored.error.empty());
    REQUIRE(restored.storage == nullptr);

    restored =
        restoreRing(entryOf(*window, {}), "not a wav file at all", 48000.0, 300, kBlock, kBound);
    REQUIRE_FALSE(restored.error.empty());
    REQUIRE(restored.storage == nullptr);

    // Audio that isn't the frames the manifest says is refused.
    RingEntry wrong = entryOf(*window, {});
    wrong.frames = 301;
    restored = restoreRing(wrong, encoded.wavBytes, 48000.0, 300, kBlock, kBound);
    REQUIRE_FALSE(restored.error.empty());

    // Over the decoder's bound.
    restored = restoreRing(entryOf(*window, {}), encoded.wavBytes, 48000.0, 300, kBlock, 16);
    REQUIRE_FALSE(restored.error.empty());
}

TEST_CASE("a ring saved at a higher rate restores converted down", "[media]")
{
    constexpr int64_t kCapacity = 4800 + kBlock;
    const auto slots = ringSlots(kCapacity, 0, 4800);
    const auto window = ringToBuffer(slots.data(), kCapacity, 0, 4800, 48000.0);
    const auto encoded = encodeRing(*window);
    const RingEntry entry = entryOf(*window, {2400, 4799});

    const auto restored = restoreRing(entry, encoded.wavBytes, 44100.0, 44100, kBlock, kBound);
    INFO(restored.error);
    REQUIRE(restored.error.empty());
    REQUIRE(restored.frames == std::llround(4800.0 * 44100.0 / 48000.0));
    REQUIRE(restored.splices.size() == 2);
    REQUIRE(restored.splices[0] == std::llround(2400.0 * 44100.0 / 48000.0));
    REQUIRE(restored.splices[1] == std::llround(4799.0 * 44100.0 / 48000.0));
    REQUIRE(restored.splices[1] < restored.frames);
}

TEST_CASE("a mark that rounds onto the frame count is dropped", "[media]")
{
    // Converting 44.1 kHz down to 32 kHz, the last mark (frames - 1) rounds to the new frame
    // count for some counts: it would sit past the end, so the ring leaves it out. Find one.
    constexpr double kRate = 32000.0;
    for (int64_t frames = 2; frames < 200; ++frames)
    {
        const int64_t scaledFrames = std::llround(double(frames) * kRate / 44100.0);
        const int64_t scaledMark = std::llround(double(frames - 1) * kRate / 44100.0);
        if (scaledMark < scaledFrames)
        {
            continue;
        }
        INFO("frames " << frames);
        const int64_t capacity = frames + kBlock;
        const auto slots = ringSlots(capacity, 0, frames);
        const auto window = ringToBuffer(slots.data(), capacity, 0, frames, 44100.0);
        const auto encoded = encodeRing(*window);
        const RingEntry entry = entryOf(*window, {0, frames - 1});
        const auto restored = restoreRing(entry, encoded.wavBytes, kRate, 1000, kBlock, kBound);
        REQUIRE(restored.error.empty());
        REQUIRE(restored.frames == scaledFrames);
        REQUIRE(restored.splices == std::vector<int64_t>{0});
        return;
    }
    FAIL("no frame count whose last mark rounds onto the count");
}
