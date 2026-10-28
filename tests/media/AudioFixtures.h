// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_TESTS_MEDIA_AUDIOFIXTURES_H
#define GG_TESTS_MEDIA_AUDIOFIXTURES_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

/// Test helpers that write WAV, AIFF and AIFC files byte by byte, independently of dr_wav (which
/// can't write AIFF), so the decoder is checked against files it didn't write itself.
namespace gg::test
{
enum class SampleFormat
{
    Int16,
    Int24,
    Float32,
};

/// Interleaved audio in [-1, 1].
struct TestAudio
{
    uint32_t channels{};
    uint32_t sampleRate{};
    std::vector<double> samples; ///< frames × channels, interleaved

    int64_t frames() const { return static_cast<int64_t>(samples.size() / channels); }
    double at(int64_t frame, uint32_t channel) const
    {
        return samples[static_cast<size_t>(frame) * channels + channel];
    }
};

/// `frames` frames of a distinct sine per channel (channel c at 220 · (c + 1) Hz, 0.8 peak),
/// except that frame 0 is -1 and frame 1 is +1 in every channel, so the extremes are covered.
inline TestAudio makeTestAudio(uint32_t channels, uint32_t sampleRate, int64_t frames)
{
    TestAudio audio{channels, sampleRate, {}};
    audio.samples.resize(static_cast<size_t>(frames) * channels);
    for (int64_t n = 0; n < frames; ++n)
    {
        for (uint32_t c = 0; c < channels; ++c)
        {
            const double phase = 2.0 * std::numbers::pi * 220.0 * (c + 1) * static_cast<double>(n) /
                                 static_cast<double>(sampleRate);
            double x = 0.8 * std::sin(phase);
            if (n == 0)
            {
                x = -1.0;
            }
            else if (n == 1)
            {
                x = 1.0;
            }
            audio.samples[static_cast<size_t>(n) * channels + c] = x;
        }
    }
    return audio;
}

inline uint32_t bitsOf(SampleFormat format)
{
    return format == SampleFormat::Int16 ? 16u : (format == SampleFormat::Int24 ? 24u : 32u);
}

/// One least significant bit of `format` in [-1, 1] units (for float, a tolerance near float
/// rounding).
inline double lsbOf(SampleFormat format)
{
    return format == SampleFormat::Float32 ? 1e-7 : std::ldexp(1.0, -int(bitsOf(format) - 1));
}

/// x quantized to a signed integer of `bits` bits: round(x · 2^(bits−1)), clamped.
inline int32_t quantize(double x, uint32_t bits)
{
    const double scale = std::ldexp(1.0, int(bits - 1));
    const double q = std::floor(x * scale + 0.5);
    return static_cast<int32_t>(std::clamp(q, -scale, scale - 1.0));
}

class Bytes
{
  public:
    std::vector<uint8_t> data;

    void raw(const char *fourcc) { data.insert(data.end(), fourcc, fourcc + 4); }
    void u8(uint32_t v) { data.push_back(static_cast<uint8_t>(v)); }
    void le(uint64_t v, int bytes)
    {
        for (int i = 0; i < bytes; ++i)
        {
            u8(static_cast<uint32_t>(v >> (8 * i)) & 0xffu);
        }
    }
    void be(uint64_t v, int bytes)
    {
        for (int i = bytes - 1; i >= 0; --i)
        {
            u8(static_cast<uint32_t>(v >> (8 * i)) & 0xffu);
        }
    }
    /// Overwrites `bytes` bytes at `offset`, little- or big-endian.
    void patch(size_t offset, uint64_t v, int bytes, bool bigEndian)
    {
        for (int i = 0; i < bytes; ++i)
        {
            const int shift = 8 * (bigEndian ? bytes - 1 - i : i);
            data[offset + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> shift);
        }
    }
};

/// Appends one sample of `format`, little- or big-endian.
inline void putSample(Bytes &out, double x, SampleFormat format, bool bigEndian)
{
    uint32_t bits = 0;
    int bytes = 0;
    if (format == SampleFormat::Float32)
    {
        const float f = static_cast<float>(x);
        static_assert(sizeof(float) == sizeof(uint32_t));
        std::memcpy(&bits, &f, sizeof bits);
        bytes = 4;
    }
    else
    {
        const uint32_t width = bitsOf(format);
        bits = static_cast<uint32_t>(quantize(x, width));
        bytes = static_cast<int>(width / 8);
    }
    if (bigEndian)
    {
        out.be(bits, bytes);
    }
    else
    {
        out.le(bits, bytes);
    }
}

/// The 44-byte header of a RIFF/WAVE file: a 16-byte `fmt ` chunk with these fields and the
/// header of a `data` chunk declaring `dataBytes`. The RIFF size covers that data, padded.
inline Bytes wavHeader(uint32_t formatTag, uint32_t channels, uint32_t sampleRate, uint32_t bits,
                       uint64_t dataBytes)
{
    const uint32_t blockAlign = channels * bits / 8;
    Bytes b;
    b.raw("RIFF");
    b.le(4 + 24 + 8 + dataBytes + (dataBytes % 2), 4);
    b.raw("WAVE");
    b.raw("fmt ");
    b.le(16, 4);
    b.le(formatTag, 2);
    b.le(channels, 2);
    b.le(sampleRate, 4);
    b.le(uint64_t{sampleRate} * blockAlign, 4);
    b.le(blockAlign, 2);
    b.le(bits, 2);
    b.raw("data");
    b.le(dataBytes, 4);
    return b;
}

/// A canonical RIFF/WAVE file: wavHeader (format tag 1, PCM, or 3, IEEE float) and the samples.
/// The `data` size is the samples' unless `claimedDataBytes` overrides it.
inline std::vector<uint8_t> wavBytes(const TestAudio &audio, SampleFormat format,
                                     int64_t claimedDataBytes = -1)
{
    const uint32_t bits = bitsOf(format);
    const uint64_t dataBytes = static_cast<uint64_t>(audio.frames()) * audio.channels * bits / 8;
    const uint64_t claimed = claimedDataBytes >= 0 ? uint64_t(claimedDataBytes) : dataBytes;
    Bytes b = wavHeader(format == SampleFormat::Float32 ? 3 : 1, audio.channels, audio.sampleRate,
                        bits, claimed);
    for (double x : audio.samples)
    {
        putSample(b, x, format, false);
    }
    if (dataBytes % 2 != 0)
    {
        b.u8(0);
    }
    return std::move(b.data);
}

/// IEEE 754 80-bit extended, big-endian, as AIFF's COMM chunk stores the sample rate.
inline void putExtended(Bytes &out, uint32_t rate)
{
    if (rate == 0)
    {
        out.be(0, 2);
        out.be(0, 8);
        return;
    }
    int exponent = 31;
    while ((rate >> exponent) == 0)
    {
        --exponent;
    }
    out.be(static_cast<uint64_t>(16383 + exponent), 2);
    out.be(static_cast<uint64_t>(rate) << (63 - exponent), 8);
}

/// An AIFF file (`aifc` false: big-endian PCM) or an AIFC file (`aifc` true: compression type
/// `sowt`, little-endian PCM, as macOS writes them), with COMM and SSND chunks. Int16 or Int24.
inline std::vector<uint8_t> aiffBytes(const TestAudio &audio, SampleFormat format, bool aifc)
{
    if (format == SampleFormat::Float32)
    {
        throw std::invalid_argument("aiffBytes: PCM formats only");
    }
    const uint32_t bits = bitsOf(format);
    Bytes b;
    b.raw("FORM");
    b.be(0, 4); // patched below
    b.raw(aifc ? "AIFC" : "AIFF");
    if (aifc)
    {
        b.raw("FVER");
        b.be(4, 4);
        b.be(0xA2805140u, 4);
    }
    b.raw("COMM");
    b.be(aifc ? 18 + 4 + 2 : 18, 4);
    b.be(audio.channels, 2);
    b.be(static_cast<uint64_t>(audio.frames()), 4);
    b.be(bits, 2);
    putExtended(b, audio.sampleRate);
    if (aifc)
    {
        b.raw("sowt");
        b.u8(0); // an empty Pascal string for the compression name, padded to an even length
        b.u8(0);
    }
    const uint64_t dataBytes = static_cast<uint64_t>(audio.frames()) * audio.channels * bits / 8;
    b.raw("SSND");
    b.be(8 + dataBytes, 4);
    b.be(0, 4); // offset
    b.be(0, 4); // block size
    for (double x : audio.samples)
    {
        putSample(b, x, format, !aifc);
    }
    if (dataBytes % 2 != 0)
    {
        b.u8(0);
    }
    b.patch(4, b.data.size() - 8, 4, true);
    return std::move(b.data);
}

inline void writeFile(const std::filesystem::path &path, std::span<const uint8_t> bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out)
    {
        throw std::runtime_error("writeFile: can't write " + path.string());
    }
}

inline std::vector<uint8_t> readFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        throw std::runtime_error("readFile: can't open " + path.string());
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The directory a test's scratch files go in, created if needed.
inline std::filesystem::path scratchDir(const char *name)
{
    const auto dir = std::filesystem::path(GG_TEST_OUTPUT_DIR) / name;
    std::filesystem::create_directories(dir);
    return dir;
}
} // namespace gg::test

#endif // GG_TESTS_MEDIA_AUDIOFIXTURES_H
