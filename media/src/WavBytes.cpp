// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/WavBytes.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace gg::media
{
namespace
{
constexpr uint16_t kFormatIeeeFloat = 3;
constexpr uint16_t kFormatExtensible = 0xfffe;
constexpr uint32_t kBitsPerSample = 32;
constexpr uint32_t kFrontLeftRight = 3; // SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT
// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 00000003-0000-0010-8000-00aa00389b71, as the file stores it.
constexpr std::string_view kIeeeFloatGuid{
    "\x03\x00\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71", 16};

void put(std::string &out, uint64_t v, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i)
    {
        out += static_cast<char>((v >> (8 * i)) & 0xffu);
    }
}
} // namespace

std::string encodeFloat32Wav(const SampleBuffer &audio)
{
    const uint32_t channels = audio.channels();
    assert(channels == 1 || channels == 2);
    const bool stereo = channels == 2;
    const uint32_t fmtBytes = stereo ? 40 : 16;
    const uint64_t headerBytes = 12 + 8 + fmtBytes + 8; // 44 mono, 68 stereo
    const auto frames = static_cast<uint64_t>(audio.frames());
    const uint64_t dataBytes = frames * channels * (kBitsPerSample / 8);
    // The RIFF size, the header and data less RIFF's own 8 bytes, must fit its u32.
    if (dataBytes > std::numeric_limits<uint32_t>::max() - (headerBytes - 8))
    {
        throw std::length_error("encodeFloat32Wav: too long for a WAV file");
    }
    const double rounded = std::round(audio.sampleRate());
    const auto rate = static_cast<uint32_t>(
        std::clamp(rounded, 1.0, double(std::numeric_limits<uint32_t>::max())));
    const uint32_t blockAlign = channels * (kBitsPerSample / 8);

    std::string out;
    out.reserve(static_cast<std::size_t>(headerBytes + dataBytes));
    out += "RIFF";
    put(out, headerBytes - 8 + dataBytes, 4);
    out += "WAVE";
    out += "fmt ";
    put(out, fmtBytes, 4);
    put(out, stereo ? kFormatExtensible : kFormatIeeeFloat, 2);
    put(out, channels, 2);
    put(out, rate, 4);
    put(out, uint64_t{rate} * blockAlign, 4);
    put(out, blockAlign, 2);
    put(out, kBitsPerSample, 2);
    if (stereo)
    {
        put(out, 22, 2);             // the extension's size
        put(out, kBitsPerSample, 2); // valid bits
        put(out, kFrontLeftRight, 4);
        out += kIeeeFloatGuid;
    }
    out += "data";
    put(out, dataBytes, 4);
    // Interleaved, each float as its bits, so -0, subnormals and NaNs are kept.
    for (uint64_t n = 0; n < frames; ++n)
    {
        for (uint32_t c = 0; c < channels; ++c)
        {
            put(out, std::bit_cast<uint32_t>(audio.channel(c)[n]), 4);
        }
    }
    return out;
}
} // namespace gg::media
