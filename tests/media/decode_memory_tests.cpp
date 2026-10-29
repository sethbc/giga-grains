// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The decoder's memory at its peak (carry-over row 187): a compressed file's audio is held once,
// in the buffer it's decoded into, when its header vouches for its length; and the truncation
// checks for ADPCM and for a RIFF data size of 0. The FLAC, MP3 and IMA ADPCM files are written
// here, byte by byte, as AudioFixtures.h writes WAV and AIFF.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "gg/media/Decode.h"
#include "media/AudioFixtures.h"
#include "support/AllocTripwire.h"

namespace
{
namespace fs = std::filesystem;
using gg::media::decodeAudioFile;
using gg::test::Bytes;

fs::path dir() { return gg::test::scratchDir("media-decode-memory"); }

/// FLAC's frame-header CRC: polynomial x^8 + x^2 + x + 1, initial 0, most significant bit first.
uint8_t crc8(const std::vector<uint8_t> &bytes, size_t from)
{
    uint8_t crc = 0;
    for (size_t i = from; i < bytes.size(); ++i)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = static_cast<uint8_t>((crc & 0x80u) != 0 ? (crc << 1) ^ 0x07u : crc << 1);
        }
    }
    return crc;
}

/// FLAC's frame CRC: polynomial x^16 + x^15 + x^2 + 1, initial 0, most significant bit first.
uint16_t crc16(const std::vector<uint8_t> &bytes, size_t from)
{
    uint16_t crc = 0;
    for (size_t i = from; i < bytes.size(); ++i)
    {
        crc ^= static_cast<uint16_t>(bytes[i] << 8);
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = static_cast<uint16_t>((crc & 0x8000u) != 0 ? (crc << 1) ^ 0x8005u : crc << 1);
        }
    }
    return crc;
}

enum class Subframes
{
    Verbatim, ///< every sample stored: about 2 bytes a sample, as an ordinary FLAC compresses
    Constant, ///< one sample a block: a few bytes for 4096 frames, as silence compresses
};

/// A 16-bit 48 kHz FLAC of `audio` (mono or stereo), in fixed blocks of 4096 frames (the last may
/// be shorter), each channel an independent subframe. Constant subframes hold each block's first
/// sample. STREAMINFO gives the frame count; its MD5 is zero (unchecked).
std::vector<uint8_t> flacBytes(const gg::test::TestAudio &audio, Subframes subframes)
{
    constexpr uint32_t kBlock = 4096;
    const auto frames = static_cast<uint64_t>(audio.frames());
    Bytes b;
    b.raw("fLaC");
    b.u8(0x80); // the last metadata block, STREAMINFO
    b.be(34, 3);
    b.be(kBlock, 2);
    b.be(kBlock, 2);
    b.be(0, 3); // frame sizes unknown
    b.be(0, 3);
    // 20 bits of rate, 3 of channels − 1, 5 of bits − 1, 36 of frame count.
    b.be((uint64_t{48000} << 44) | (uint64_t{audio.channels - 1} << 41) | (uint64_t{15} << 36) |
             frames,
         8);
    b.be(0, 8); // MD5
    b.be(0, 8);

    for (uint64_t start = 0, index = 0; start < frames; start += kBlock, ++index)
    {
        const auto count = static_cast<uint32_t>(std::min<uint64_t>(kBlock, frames - start));
        const size_t frameStart = b.data.size();
        b.be(0xFFF8, 2); // sync, fixed block size
        // Block size 4096 (code 12) or, for a shorter last block, "16 bits follow" (7); 48 kHz.
        b.u8((count == kBlock ? 0xC0u : 0x70u) | 0x0Au);
        b.u8(((audio.channels - 1) << 4) | 0x08u); // independent channels, 16 bits
        // The frame number, UTF-8 coded.
        if (index < 0x80)
        {
            b.u8(static_cast<uint32_t>(index));
        }
        else if (index < 0x800)
        {
            b.u8(0xC0u | static_cast<uint32_t>(index >> 6));
            b.u8(0x80u | static_cast<uint32_t>(index & 0x3F));
        }
        else
        {
            REQUIRE(index < 0x10000);
            b.u8(0xE0u | static_cast<uint32_t>(index >> 12));
            b.u8(0x80u | static_cast<uint32_t>((index >> 6) & 0x3F));
            b.u8(0x80u | static_cast<uint32_t>(index & 0x3F));
        }
        if (count != kBlock)
        {
            b.be(count - 1, 2);
        }
        b.u8(crc8(b.data, frameStart));
        for (uint32_t c = 0; c < audio.channels; ++c)
        {
            const auto sample = [&](uint64_t n) {
                return static_cast<uint16_t>(gg::test::quantize(audio.at(int64_t(n), c), 16));
            };
            if (subframes == Subframes::Constant)
            {
                b.u8(0x00);
                b.be(sample(start), 2);
            }
            else
            {
                b.u8(0x02);
                for (uint64_t n = start; n < start + count; ++n)
                {
                    b.be(sample(n), 2);
                }
            }
        }
        b.be(crc16(b.data, frameStart), 2);
    }
    return std::move(b.data);
}

/// An MP3 of `frames` silent MPEG-1 Layer III frames, 44.1 kHz, 128 kb/s, stereo, no tag: each
/// frame a header, zeroed side information (no main data) and zero padding, 417 bytes, decoding
/// to 1152 frames of silence.
std::vector<uint8_t> mp3Bytes(uint32_t frames)
{
    std::vector<uint8_t> out;
    for (uint32_t f = 0; f < frames; ++f)
    {
        const uint8_t header[4] = {0xFF, 0xFB, 0x90, 0x00};
        out.insert(out.end(), std::begin(header), std::end(header));
        out.resize(out.size() + 417 - 4, 0);
    }
    return out;
}

/// The frames one IMA ADPCM block holds: the header's sample, then two a byte after the headers.
uint32_t imaFramesPerBlock(uint32_t channels, uint32_t blockAlign)
{
    return (blockAlign - 4 * channels) * 2 / channels + 1;
}

/// A RIFF/WAVE IMA ADPCM file (format 0x11) of `blocks` blocks of `blockAlign` bytes, with no
/// `fact` chunk. The blocks decode to a constant: each channel's header predictor is 1000 · (c +
/// 1), its step index 0, and every nibble 0. The data size is the blocks' unless `claimedBytes`
/// overrides it.
std::vector<uint8_t> imaAdpcmBytes(uint32_t channels, uint32_t blocks, uint32_t blockAlign,
                                   int64_t claimedBytes = -1)
{
    const uint64_t dataBytes = uint64_t(blocks) * blockAlign;
    const uint64_t claimed = claimedBytes >= 0 ? uint64_t(claimedBytes) : dataBytes;
    const uint32_t rate = 44100;
    Bytes b;
    b.raw("RIFF");
    b.le(4 + 28 + 8 + claimed, 4);
    b.raw("WAVE");
    b.raw("fmt ");
    b.le(20, 4);
    b.le(0x11, 2);
    b.le(channels, 2);
    b.le(rate, 4);
    b.le(uint64_t(rate) * blockAlign / imaFramesPerBlock(channels, blockAlign), 4);
    b.le(blockAlign, 2);
    b.le(4, 2); // bits per sample
    b.le(2, 2); // extra bytes: samples per block
    b.le(imaFramesPerBlock(channels, blockAlign), 2);
    b.raw("data");
    b.le(claimed, 4);
    for (uint32_t block = 0; block < blocks; ++block)
    {
        for (uint32_t c = 0; c < channels; ++c)
        {
            b.le(1000 * (c + 1), 2);
            b.le(0, 2);
        }
        b.data.resize(b.data.size() + blockAlign - 4 * channels, 0);
    }
    return std::move(b.data);
}

/// Sets the 36-bit frame count of flacBytes' STREAMINFO: the low nibble of byte 8 + 13, then
/// bytes 8 + 14 to 8 + 17.
void setFlacFrames(std::vector<uint8_t> &flac, uint64_t frames)
{
    flac[8 + 13] = static_cast<uint8_t>((flac[8 + 13] & 0xf0u) | ((frames >> 32) & 0x0fu));
    for (int i = 0; i < 4; ++i)
    {
        flac[size_t(8 + 14 + i)] = static_cast<uint8_t>(frames >> (24 - 8 * i));
    }
}

/// Whether every decoded sample is within one 16-bit LSB of `audio`'s, in every channel.
bool matches16(const gg::SampleBuffer &buffer, const gg::test::TestAudio &audio)
{
    if (buffer.frames() != audio.frames() || buffer.channels() != audio.channels)
    {
        return false;
    }
    const double lsb = gg::test::lsbOf(gg::test::SampleFormat::Int16);
    for (uint32_t c = 0; c < buffer.channels(); ++c)
    {
        for (int64_t n = 0; n < buffer.frames(); ++n)
        {
            if (std::abs(double(buffer.channel(c)[n]) - audio.at(n, c)) > lsb)
            {
                return false;
            }
        }
    }
    return true;
}

/// What decoding `file` held at its peak, measured by AllocPeak.
struct Measured
{
    gg::media::DecodeResult result;
    uint64_t peakBytes{};
    bool complete{};
};

Measured decodeMeasured(const fs::path &file)
{
    Measured m;
    {
        const gg::test::AllocPeak peak;
        m.result = decodeAudioFile(file);
        m.peakBytes = peak.peakBytes();
        m.complete = peak.complete();
    }
    return m;
}

/// The decoded audio's own bytes: its channels × frames floats.
uint64_t audioBytes(const gg::media::DecodedAudio &audio)
{
    return uint64_t(audio.buffer->channels()) * uint64_t(audio.buffer->frames()) * sizeof(float);
}

/// What a decode may hold beyond its audio, at most: its 32 KiB scratch buffer, dr_mp3's decoder
/// state (about 10 KiB, which the decoder allocates with new) and small change.
constexpr uint64_t kOverheadBytes = 128 * 1024;
} // namespace

TEST_CASE("decode holds a compressed file's audio once at its peak", "[media]")
{
    if (!gg::test::AllocPeak::available())
    {
        SKIP("this build doesn't measure allocations (GG_SANITIZE)");
    }
    const std::string codec = GENERATE("flac", "mp3", "adpcm");
    fs::path file;
    int64_t expectedFrames = 0;
    if (codec == "flac")
    {
        // 409,600 stereo frames, 3.3 MB decoded, from 1.6 MB.
        const auto audio = gg::test::makeTestAudio(2, 48000, 100 * 4096);
        file = dir() / "peak.flac";
        gg::test::writeFile(file, flacBytes(audio, Subframes::Verbatim));
        expectedFrames = audio.frames();
    }
    else if (codec == "mp3")
    {
        // 400 frames: 460,800 stereo frames, 3.7 MB decoded, from 167 KB.
        file = dir() / "peak.mp3";
        gg::test::writeFile(file, mp3Bytes(400));
        expectedFrames = 400 * 1152;
    }
    else
    {
        // 440 blocks of 1017 stereo frames: 447,480 frames, 3.6 MB decoded, from 451 KB.
        file = dir() / "peak-ima.wav";
        gg::test::writeFile(file, imaAdpcmBytes(2, 440, 1024));
        expectedFrames = 440 * imaFramesPerBlock(2, 1024);
    }

    const auto m = decodeMeasured(file);
    INFO(codec << ": " << m.result.error);
    REQUIRE(m.result.audio.has_value());
    REQUIRE(m.complete);
    const auto &audio = *m.result.audio;
    REQUIRE(audio.buffer->channels() == 2);
    // MP3: within a frame of the count (the decoder may hold back its first frame's samples).
    REQUIRE(std::abs(audio.buffer->frames() - expectedFrames) <= (codec == "mp3" ? 1152 : 0));
    const uint64_t bytes = audioBytes(audio);
    INFO("audio " << bytes << " bytes, peak " << m.peakBytes << " bytes ("
                  << double(m.peakBytes) / double(bytes) << "x)");
    REQUIRE(m.peakBytes >= bytes);
    REQUIRE(m.peakBytes <= bytes + kOverheadBytes);
}

TEST_CASE("decode holds at most twice the audio when the header can't vouch for it", "[media]")
{
    if (!gg::test::AllocPeak::available())
    {
        SKIP("this build doesn't measure allocations (GG_SANITIZE)");
    }
    // A FLAC of constant subframes decodes to thousands of times its size, and a STREAMINFO
    // without a count gives none: the audio grows in blocks as it arrives, and is then copied
    // into one buffer, so the peak is the audio twice and a block.
    const auto audio = gg::test::makeTestAudio(2, 48000, 100 * 4096);
    const bool noCount = GENERATE(false, true);
    auto bytes = flacBytes(audio, noCount ? Subframes::Verbatim : Subframes::Constant);
    if (noCount)
    {
        setFlacFrames(bytes, 0);
    }
    const auto file = dir() / (noCount ? "no-count.flac" : "constant.flac");
    gg::test::writeFile(file, bytes);

    const auto m = decodeMeasured(file);
    INFO(m.result.error);
    REQUIRE(m.result.audio.has_value());
    REQUIRE(m.complete);
    REQUIRE(m.result.audio->buffer->frames() == audio.frames());
    const uint64_t decoded = audioBytes(*m.result.audio);
    INFO("audio " << decoded << " bytes, peak " << m.peakBytes << " bytes ("
                  << double(m.peakBytes) / double(decoded) << "x)");
    REQUIRE(m.peakBytes <= 2 * decoded + 2 * 65536 * sizeof(float) + kOverheadBytes);
    if (noCount)
    {
        // The blocks put back together: every sample where it belongs.
        REQUIRE(matches16(*m.result.audio->buffer, audio));
    }
}

TEST_CASE("decode trusts a compressed header up to 64 bytes of audio per byte of the file",
          "[media]")
{
    if (!gg::test::AllocPeak::available())
    {
        SKIP("this build doesn't measure allocations (GG_SANITIZE)");
    }
    // Ruling R7 as amended (INTENT §23 #54). A mono verbatim FLAC of 40,960 frames (about 82 KB)
    // whose STREAMINFO lies: just under the ratio, its claim is allocated whole before the decode
    // proves the file truncated, and costs at most 64 times the file; just over, the audio grows
    // in blocks as it decodes and the claim costs nothing.
    const auto audio = gg::test::makeTestAudio(1, 48000, 10 * 4096);
    auto bytes = flacBytes(audio, Subframes::Verbatim);
    const uint64_t fileBytes = bytes.size();
    const bool over = GENERATE(false, true);
    // Mono: 4 bytes a frame, so 16 frames a byte of the file is exactly 64 bytes a byte.
    setFlacFrames(bytes, 16 * (over ? fileBytes + 1 : fileBytes));
    const auto file = dir() / (over ? "claim-over-64x.flac" : "claim-under-64x.flac");
    gg::test::writeFile(file, bytes);

    const auto m = decodeMeasured(file);
    INFO("file " << fileBytes << " bytes, peak " << m.peakBytes << " bytes ("
                 << double(m.peakBytes) / double(fileBytes) << "x the file)");
    REQUIRE_FALSE(m.result.audio.has_value());
    REQUIRE(m.result.error.find("truncated") != std::string::npos);
    REQUIRE(m.complete);
    if (over)
    {
        // One block of 65,536 frames held the 40,960 that came.
        REQUIRE(m.peakBytes <= 65536 * sizeof(float) + kOverheadBytes);
    }
    else
    {
        REQUIRE(m.peakBytes >= 64 * fileBytes);
        REQUIRE(m.peakBytes <= 64 * fileBytes + kOverheadBytes);
    }
}

TEST_CASE("decode carries a decode past its header's count from the buffer into blocks", "[media]")
{
    // A stereo verbatim FLAC of 409,600 frames whose STREAMINFO claims 1000 fewer: the first
    // 408,600 go into the buffer allocated at the claim, and a 4096-frame read straddles its end,
    // the rest going into a block. Every sample must come out where it belongs.
    const auto audio = gg::test::makeTestAudio(2, 48000, 100 * 4096);
    auto bytes = flacBytes(audio, Subframes::Verbatim);
    setFlacFrames(bytes, uint64_t(audio.frames()) - 1000);
    const auto file = dir() / "beyond-count.flac";
    gg::test::writeFile(file, bytes);

    const auto result = decodeAudioFile(file);
    INFO(result.error);
    REQUIRE(result.audio.has_value());
    REQUIRE(result.audio->buffer->frames() == audio.frames());
    REQUIRE(matches16(*result.audio->buffer, audio));
}

TEST_CASE("decode reads the FLAC and IMA ADPCM files it is tested with", "[media]")
{
    // The writers above, checked: the verbatim FLAC is lossless, the ADPCM decodes to its
    // predictors.
    SECTION("FLAC, mono, with a short last block")
    {
        const auto audio = gg::test::makeTestAudio(1, 48000, 3 * 4096 + 100);
        const auto file = dir() / "writer-check.flac";
        gg::test::writeFile(file, flacBytes(audio, Subframes::Verbatim));
        const auto result = decodeAudioFile(file);
        INFO(result.error);
        REQUIRE(result.audio.has_value());
        REQUIRE(result.audio->format == "flac");
        REQUIRE(result.audio->buffer->frames() == audio.frames());
        double worst = 0.0;
        for (int64_t n = 0; n < audio.frames(); ++n)
        {
            worst = std::max(
                worst, std::abs(double(result.audio->buffer->channel(0)[n]) - audio.at(n, 0)));
        }
        REQUIRE(worst <= gg::test::lsbOf(gg::test::SampleFormat::Int16));
    }
    SECTION("IMA ADPCM, stereo")
    {
        const auto file = dir() / "writer-check-ima.wav";
        gg::test::writeFile(file, imaAdpcmBytes(2, 3, 256));
        const auto result = decodeAudioFile(file);
        INFO(result.error);
        REQUIRE(result.audio.has_value());
        REQUIRE(result.audio->format == "wav");
        REQUIRE(result.audio->buffer->frames() == 3 * imaFramesPerBlock(2, 256));
        REQUIRE(result.audio->buffer->channel(0)[10] == 1000.0f / 32768.0f);
        REQUIRE(result.audio->buffer->channel(1)[10] == 2000.0f / 32768.0f);
    }
}

TEST_CASE("decode reads an ADPCM WAV whose last block is short with its channels intact", "[media]")
{
    // dr_wav counts frames for the whole of a last block that is only partly there, and decodes
    // fewer: the decode ends within its one block of shortfall, and the stereo buffer that was
    // allocated at the count closes up around the frames that came.
    auto bytes = imaAdpcmBytes(2, 9, 256);
    const size_t dataBytes = 8 * 256 + 100;
    bytes.resize(bytes.size() - (256 - 100));
    Bytes b{std::move(bytes)};
    b.patch(4, 4 + 28 + 8 + dataBytes, 4, false);         // the RIFF size
    b.patch(4 + 4 + 4 + 8 + 20 + 4, dataBytes, 4, false); // the data size
    const auto file = dir() / "ima-short-last-block.wav";
    gg::test::writeFile(file, b.data);

    const auto result = decodeAudioFile(file);
    INFO(result.error);
    REQUIRE(result.audio.has_value());
    const auto &buffer = *result.audio->buffer;
    REQUIRE(buffer.channels() == 2);
    REQUIRE(buffer.frames() > 8 * int64_t(imaFramesPerBlock(2, 256)));
    REQUIRE(buffer.frames() < 9 * int64_t(imaFramesPerBlock(2, 256)));
    bool intact = true;
    for (int64_t n = 0; n < buffer.frames(); ++n)
    {
        intact = intact && buffer.channel(0)[n] == 1000.0f / 32768.0f &&
                 buffer.channel(1)[n] == 2000.0f / 32768.0f;
    }
    REQUIRE(intact);
}

TEST_CASE("decode refuses an ADPCM WAV without a fact chunk whose data runs past the file",
          "[media]")
{
    const auto requireTruncated = [](const fs::path &file) {
        const auto result = decodeAudioFile(file);
        INFO("error: " << result.error);
        REQUIRE_FALSE(result.audio.has_value());
        REQUIRE(result.error.find("truncated") != std::string::npos);
    };
    SECTION("the data size claims twice the blocks")
    {
        const auto file = dir() / "ima-truncated.wav";
        gg::test::writeFile(file, imaAdpcmBytes(2, 8, 256, 16 * 256));
        requireTruncated(file);
    }
    SECTION("the data size claims a block and a byte more")
    {
        const auto file = dir() / "ima-block-and-byte.wav";
        gg::test::writeFile(file, imaAdpcmBytes(1, 8, 256, 9 * 256 + 1));
        requireTruncated(file);
    }
    SECTION("up to one block more reads, as the one block an ADPCM decode may fall short")
    {
        const auto file = dir() / "ima-one-block-short.wav";
        gg::test::writeFile(file, imaAdpcmBytes(1, 8, 256, 9 * 256));
        const auto result = decodeAudioFile(file);
        INFO("error: " << result.error);
        REQUIRE(result.audio.has_value());
        REQUIRE(result.audio->buffer->frames() == 8 * imaFramesPerBlock(1, 256));
    }
}

TEST_CASE("decode reads a RIFF data size of 0 with audio after it as truncated", "[media]")
{
    SECTION("audio follows: a recording never finalized")
    {
        const auto audio = gg::test::makeTestAudio(2, 48000, 600);
        const auto file = dir() / "data-size-0.wav";
        gg::test::writeFile(file, gg::test::wavBytes(audio, gg::test::SampleFormat::Int16, 0));
        const auto result = decodeAudioFile(file);
        INFO("error: " << result.error);
        REQUIRE_FALSE(result.audio.has_value());
        REQUIRE(result.error.find("truncated") != std::string::npos);
    }
    SECTION("an IMA ADPCM file: dr_wav refuses it, but it is truncated just the same")
    {
        const auto file = dir() / "ima-data-size-0.wav";
        gg::test::writeFile(file, imaAdpcmBytes(2, 8, 256, 0));
        const auto result = decodeAudioFile(file);
        INFO("error: " << result.error);
        REQUIRE_FALSE(result.audio.has_value());
        REQUIRE(result.error.find("truncated") != std::string::npos);
    }
    SECTION("a chunk that fits follows: an empty file, no audio")
    {
        auto bytes =
            gg::test::wavBytes(gg::test::makeTestAudio(2, 48000, 0), gg::test::SampleFormat::Int16);
        Bytes b{std::move(bytes)};
        b.raw("LIST");
        b.le(4, 4);
        b.raw("INFO");
        b.patch(4, b.data.size() - 8, 4, false); // the RIFF size
        const auto file = dir() / "data-size-0-then-list.wav";
        gg::test::writeFile(file, b.data);
        const auto result = decodeAudioFile(file);
        INFO("error: " << result.error);
        REQUIRE_FALSE(result.audio.has_value());
        REQUIRE(result.error.find("no audio") != std::string::npos);
    }
    SECTION("RIFX: the next chunk's size is read big-endian")
    {
        // A big-endian 16-bit stereo WAVE with a data size of 0, then another chunk.
        const auto rifx = [](uint32_t nextSize) {
            std::vector<uint8_t> v;
            const auto text = [&](const char *s) { v.insert(v.end(), s, s + 4); };
            const auto be = [&](uint64_t x, int n) {
                for (int i = n - 1; i >= 0; --i)
                {
                    v.push_back(uint8_t(x >> (8 * i)));
                }
            };
            text("RIFX");
            be(4 + 24 + 8 + 12, 4);
            text("WAVE");
            text("fmt ");
            be(16, 4);
            be(1, 2); // PCM
            be(2, 2); // channels
            be(48000, 4);
            be(48000 * 4, 4);
            be(4, 2);
            be(16, 2);
            text("data");
            be(0, 4);
            text("LIST");
            be(nextSize, 4);
            text("INFO");
            return v;
        };
        const auto fitting = dir() / "rifx-data-size-0-then-list.wav";
        gg::test::writeFile(fitting, rifx(4)); // 4 bytes follow: fits (read little-endian: 2^26)
        auto result = decodeAudioFile(fitting);
        INFO("error: " << result.error);
        REQUIRE_FALSE(result.audio.has_value());
        REQUIRE(result.error.find("no audio") != std::string::npos);
        const auto garbage = dir() / "rifx-data-size-0-then-garbage.wav";
        gg::test::writeFile(garbage, rifx(0x7ffffff0u)); // runs past the end of the file
        result = decodeAudioFile(garbage);
        INFO("error: " << result.error);
        REQUIRE_FALSE(result.audio.has_value());
        REQUIRE(result.error.find("truncated") != std::string::npos);
    }
    SECTION("nothing follows: no audio")
    {
        const auto file = dir() / "data-size-0-empty.wav";
        gg::test::writeFile(file, gg::test::wavBytes(gg::test::makeTestAudio(2, 48000, 0),
                                                     gg::test::SampleFormat::Int16));
        const auto result = decodeAudioFile(file);
        INFO("error: " << result.error);
        REQUIRE_FALSE(result.audio.has_value());
        REQUIRE(result.error.find("no audio") != std::string::npos);
    }
}
