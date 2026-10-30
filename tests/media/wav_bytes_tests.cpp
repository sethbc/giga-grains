// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The float32 WAV bytes of embedded and filed captures (gg/media/WavBytes.h, spec §5, §6.6):
// exact through gg-media's own decoder, deterministic, and with a canonical header.

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/media/Decode.h"
#include "gg/media/WavBytes.h"
#include "gg/source/SampleBuffer.h"
#include "media/AudioFixtures.h"

namespace
{
namespace fs = std::filesystem;
using gg::SampleBuffer;
using gg::media::encodeFloat32Wav;

/// `frames` frames of `channels` channels at `rate`, channel c's frame n a distinct float, with
/// the values a ring may hold that a careless codec loses: -0, ±2^20 (the ring's clamp), the
/// smallest subnormal, the largest float below 1 and a value no PCM width holds.
SampleBuffer testBuffer(uint32_t channels, int64_t frames, double rate)
{
    SampleBuffer b(channels, frames, rate);
    const std::vector<float> special{-0.0f,
                                     0x1p20f,
                                     -0x1p20f,
                                     std::numeric_limits<float>::denorm_min(),
                                     std::nextafter(1.0f, 0.0f),
                                     1.0f / 3.0f,
                                     -1.0f,
                                     1.0f};
    for (uint32_t c = 0; c < channels; ++c)
    {
        float *x = b.channel(c);
        for (int64_t n = 0; n < frames; ++n)
        {
            const auto k = static_cast<std::size_t>(n) + c;
            x[n] = k < special.size() ? special[k]
                                      : 0.75f * std::sin(0.01f * float(n) * float(c + 1)) - 0.1f;
        }
    }
    return b;
}

uint64_t le(std::string_view bytes, std::size_t at, std::size_t count)
{
    uint64_t v = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        v |= uint64_t{static_cast<unsigned char>(bytes[at + i])} << (8 * i);
    }
    return v;
}

fs::path writeScratch(const std::string &name, const std::string &bytes)
{
    const auto path = gg::test::scratchDir("media-wav-bytes") / name;
    const std::span<const uint8_t> view(reinterpret_cast<const uint8_t *>(bytes.data()),
                                        bytes.size());
    gg::test::writeFile(path, view);
    return path;
}
} // namespace

TEST_CASE("encodeFloat32Wav decodes to the same floats", "[media]")
{
    for (const uint32_t channels : {1u, 2u})
    {
        INFO(channels << " channels");
        const SampleBuffer in = testBuffer(channels, 1000, 48000.0);
        const std::string bytes = encodeFloat32Wav(in);
        const auto path = writeScratch("round-trip-" + std::to_string(channels) + ".wav", bytes);

        const auto result = gg::media::decodeAudioFile(path);
        INFO(result.error);
        REQUIRE(result.audio.has_value());
        CHECK(result.audio->format == "wav");
        CHECK(result.audio->fileChannels == channels);
        const SampleBuffer &out = *result.audio->buffer;
        REQUIRE(out.channels() == channels);
        REQUIRE(out.frames() == in.frames());
        CHECK(out.sampleRate() == 48000.0);
        // Every float with its bits: -0 stays -0, ±2^20 and the subnormal are kept.
        std::size_t differing = 0;
        for (uint32_t c = 0; c < channels; ++c)
        {
            for (int64_t n = 0; n < in.frames(); ++n)
            {
                differing += std::bit_cast<uint32_t>(in.channel(c)[n]) !=
                             std::bit_cast<uint32_t>(out.channel(c)[n]);
            }
        }
        CHECK(differing == 0);
        CHECK(std::bit_cast<uint32_t>(out.channel(0)[0]) == 0x80000000u);
        CHECK(out.channel(0)[1] == 0x1p20f);
        CHECK(out.channel(0)[2] == -0x1p20f);
    }
}

TEST_CASE("the same audio gives the same bytes", "[media]")
{
    for (const uint32_t channels : {1u, 2u})
    {
        INFO(channels << " channels");
        const SampleBuffer a = testBuffer(channels, 777, 44100.0);
        const SampleBuffer b = testBuffer(channels, 777, 44100.0);
        const std::string bytes = encodeFloat32Wav(a);
        CHECK(encodeFloat32Wav(a) == bytes);
        CHECK(encodeFloat32Wav(b) == bytes);
        // One float's bits changed changes the bytes, there and only there.
        SampleBuffer c = testBuffer(channels, 777, 44100.0);
        c.channel(channels - 1)[500] = std::nextafter(c.channel(channels - 1)[500], 2.0f);
        const std::string changed = encodeFloat32Wav(c);
        REQUIRE(changed.size() == bytes.size());
        std::size_t differing = 0;
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            differing += bytes[i] != changed[i];
        }
        CHECK(differing >= 1);
        CHECK(differing <= 4);
    }
}

TEST_CASE("the header is canonical", "[media]")
{
    SECTION("mono: 44 bytes, format 3")
    {
        const SampleBuffer in = testBuffer(1, 10, 48000.0);
        const std::string w = encodeFloat32Wav(in);
        REQUIRE(w.size() == 44 + 10 * 4);
        CHECK(w.substr(0, 4) == "RIFF");
        CHECK(le(w, 4, 4) == w.size() - 8);
        CHECK(w.substr(8, 4) == "WAVE");
        CHECK(w.substr(12, 4) == "fmt ");
        CHECK(le(w, 16, 4) == 16);        // the fmt chunk's size
        CHECK(le(w, 20, 2) == 3);         // WAVE_FORMAT_IEEE_FLOAT
        CHECK(le(w, 22, 2) == 1);         // channels
        CHECK(le(w, 24, 4) == 48000);     // rate
        CHECK(le(w, 28, 4) == 48000 * 4); // bytes a second
        CHECK(le(w, 32, 2) == 4);         // block align
        CHECK(le(w, 34, 2) == 32);        // bits
        CHECK(w.substr(36, 4) == "data");
        CHECK(le(w, 40, 4) == 10 * 4);
        // The samples follow, as their bits.
        CHECK(le(w, 44, 4) == 0x80000000u);
        CHECK(le(w, 48, 4) == std::bit_cast<uint32_t>(0x1p20f));
    }
    SECTION("stereo: 68 bytes, WAVE_FORMAT_EXTENSIBLE")
    {
        const SampleBuffer in = testBuffer(2, 10, 44100.0);
        const std::string w = encodeFloat32Wav(in);
        REQUIRE(w.size() == 68 + 10 * 8);
        CHECK(w.substr(0, 4) == "RIFF");
        CHECK(le(w, 4, 4) == w.size() - 8);
        CHECK(w.substr(8, 4) == "WAVE");
        CHECK(w.substr(12, 4) == "fmt ");
        CHECK(le(w, 16, 4) == 40);        // the fmt chunk's size
        CHECK(le(w, 20, 2) == 0xfffe);    // WAVE_FORMAT_EXTENSIBLE
        CHECK(le(w, 22, 2) == 2);         // channels
        CHECK(le(w, 24, 4) == 44100);     // rate
        CHECK(le(w, 28, 4) == 44100 * 8); // bytes a second
        CHECK(le(w, 32, 2) == 8);         // block align
        CHECK(le(w, 34, 2) == 32);        // bits
        CHECK(le(w, 36, 2) == 22);        // the extension's size
        CHECK(le(w, 38, 2) == 32);        // valid bits
        CHECK(le(w, 40, 4) == 3);         // channel mask: front left and right
        // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 00000003-0000-0010-8000-00aa00389b71.
        CHECK(w.substr(44, 16) ==
              std::string_view("\x03\x00\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71",
                               16));
        CHECK(w.substr(60, 4) == "data");
        CHECK(le(w, 64, 4) == 10 * 8);
        // Interleaved, L then R.
        CHECK(le(w, 68, 4) == std::bit_cast<uint32_t>(in.channel(0)[0]));
        CHECK(le(w, 72, 4) == std::bit_cast<uint32_t>(in.channel(1)[0]));
        CHECK(le(w, 76, 4) == std::bit_cast<uint32_t>(in.channel(0)[1]));
    }
    SECTION("no frames: the header alone, its data chunk empty")
    {
        const std::string w = encodeFloat32Wav(SampleBuffer(2, 0, 48000.0));
        REQUIRE(w.size() == 68);
        CHECK(le(w, 4, 4) == 60);
        CHECK(le(w, 64, 4) == 0);
    }
    SECTION("the rate is rounded to an integer")
    {
        const std::string w = encodeFloat32Wav(SampleBuffer(1, 1, 44100.4));
        CHECK(le(w, 24, 4) == 44100);
        CHECK(le(encodeFloat32Wav(SampleBuffer(1, 1, 44100.5)), 24, 4) == 44101);
    }
}
