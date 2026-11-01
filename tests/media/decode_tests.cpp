// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "gg/media/Decode.h"
#include "media/AudioFixtures.h"
#include "support/AllocTripwire.h"
#include "support/Utf8.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace
{
namespace fs = std::filesystem;
using gg::media::decodeAudioFile;
/// The bound every decode here passes: the memory cap's ceiling, so nothing is refused for its
/// size.
constexpr uint64_t kBound = gg::media::kDefaultDecodeBound;
using gg::test::SampleFormat;

fs::path dir() { return gg::test::scratchDir("media-decode"); }

/// The largest difference between the decoded buffer and `expected`'s first `channels` channels.
double maxError(const gg::SampleBuffer &buffer, const gg::test::TestAudio &expected,
                uint32_t channels)
{
    double worst = 0.0;
    for (uint32_t c = 0; c < channels; ++c)
    {
        const float *decoded = buffer.channel(c);
        for (int64_t n = 0; n < buffer.frames(); ++n)
        {
            worst = std::max(worst, std::abs(double(decoded[n]) - expected.at(n, c)));
        }
    }
    return worst;
}

double rms(const gg::SampleBuffer &buffer)
{
    double sum = 0.0;
    const float *x = buffer.channel(0);
    for (int64_t n = 0; n < buffer.frames(); ++n)
    {
        sum += double(x[n]) * double(x[n]);
    }
    return std::sqrt(sum / double(buffer.frames()));
}

const char *name(SampleFormat format)
{
    return format == SampleFormat::Int16 ? "16" : (format == SampleFormat::Int24 ? "24" : "f32");
}

/// The error a failed decode of `file` must give: no audio, and a message naming the file and
/// containing `reason`.
void requireError(const gg::media::DecodeResult &result, const fs::path &file,
                  const std::string &reason = {})
{
    INFO("error: " << result.error);
    REQUIRE_FALSE(result.audio.has_value());
    REQUIRE_FALSE(result.error.empty());
    // "<file name>: <reason>", the reason searched apart, since names like truncated.flac
    // contain the words a reason does.
    const std::string prefix = gg::test::utf8(file.filename()) + ": ";
    REQUIRE(result.error.starts_with(prefix));
    REQUIRE(result.error.find(reason, prefix.size()) != std::string::npos);
}

fs::path fixture(const char *name) { return fs::path(GG_AUDIO_FIXTURE_DIR) / name; }

/// Sets the 36-bit total-samples field of a FLAC file's STREAMINFO, which starts at byte 8 (after
/// "fLaC" and its block header): the low nibble of its byte 13, then bytes 14 to 17.
void setFlacTotalSamples(std::vector<uint8_t> &flac, uint64_t samples)
{
    REQUIRE(flac.size() > 26);
    REQUIRE(std::string(flac.begin(), flac.begin() + 4) == "fLaC");
    flac[8 + 13] = static_cast<uint8_t>((flac[8 + 13] & 0xf0u) | ((samples >> 32) & 0x0fu));
    for (int i = 0; i < 4; ++i)
    {
        flac[size_t(8 + 14 + i)] = static_cast<uint8_t>(samples >> (24 - 8 * i));
    }
}

// The fixtures' signal (tests/fixtures/audio/README.md): 0.1 s of 0.5 · 32767 · sin(2π · 1 kHz ·
// n / 48 kHz), rounded to 16 bits, scaled to [-1, 1] by 1/32768.
constexpr double kFixtureRate = 48000.0;
constexpr int64_t kFixtureFrames = 4800;
double fixtureSample(int64_t n)
{
    return 0.5 * 32767.0 / 32768.0 *
           std::sin(2.0 * std::numbers::pi * 1000.0 * double(n) / kFixtureRate);
}
const double kFixtureRms = 0.5 * 32767.0 / 32768.0 / std::numbers::sqrt2;
} // namespace

TEST_CASE("decode reads 16-bit, 24-bit and float WAV, mono and stereo", "[media]")
{
    const auto format = GENERATE(SampleFormat::Int16, SampleFormat::Int24, SampleFormat::Float32);
    const uint32_t channels = GENERATE(1u, 2u);
    const uint32_t rate = 44100;
    const auto audio = gg::test::makeTestAudio(channels, rate, 1001);
    const auto file =
        dir() / (std::string("wav-") + name(format) + "-" + std::to_string(channels) + "ch.wav");
    gg::test::writeFile(file, gg::test::wavBytes(audio, format));

    const auto result = decodeAudioFile(file, kBound);
    INFO(gg::test::utf8(file.filename()) << ": " << result.error);
    REQUIRE(result.audio.has_value());
    REQUIRE(result.error.empty());
    const auto &decoded = *result.audio;
    REQUIRE(decoded.format == "wav");
    REQUIRE(decoded.fileChannels == channels);
    REQUIRE(decoded.buffer);
    REQUIRE(decoded.buffer->channels() == channels);
    REQUIRE(decoded.buffer->frames() == 1001);
    REQUIRE(decoded.buffer->sampleRate() == double(rate));
    REQUIRE(maxError(*decoded.buffer, audio, channels) <= gg::test::lsbOf(format));
}

TEST_CASE("decode reads AIFF and AIFC", "[media]")
{
    const bool aifc = GENERATE(false, true);
    // AIFF: big-endian 16-bit stereo. AIFC: `sowt` (little-endian) 24-bit mono.
    const auto format = aifc ? SampleFormat::Int24 : SampleFormat::Int16;
    const uint32_t channels = aifc ? 1u : 2u;
    const uint32_t rate = aifc ? 96000u : 48000u;
    const auto audio = gg::test::makeTestAudio(channels, rate, 777);
    const auto file = dir() / (aifc ? "sowt-24-mono.aifc" : "be-16-stereo.aiff");
    gg::test::writeFile(file, gg::test::aiffBytes(audio, format, aifc));

    const auto result = decodeAudioFile(file, kBound);
    INFO(gg::test::utf8(file.filename()) << ": " << result.error);
    REQUIRE(result.audio.has_value());
    const auto &decoded = *result.audio;
    REQUIRE(decoded.format == "aiff");
    REQUIRE(decoded.fileChannels == channels);
    REQUIRE(decoded.buffer->channels() == channels);
    REQUIRE(decoded.buffer->frames() == 777);
    REQUIRE(decoded.buffer->sampleRate() == double(rate));
    REQUIRE(maxError(*decoded.buffer, audio, channels) <= gg::test::lsbOf(format));
}

TEST_CASE("decode reads the FLAC fixture losslessly", "[media]")
{
    const auto result = decodeAudioFile(fixture("sine-1k-48k-mono.flac"), kBound);
    INFO(result.error);
    REQUIRE(result.audio.has_value());
    const auto &decoded = *result.audio;
    REQUIRE(decoded.format == "flac");
    REQUIRE(decoded.fileChannels == 1);
    const auto &buffer = *decoded.buffer;
    REQUIRE(buffer.channels() == 1);
    REQUIRE(buffer.sampleRate() == kFixtureRate);
    REQUIRE(buffer.frames() == kFixtureFrames);
    double worst = 0.0;
    for (int64_t n = 0; n < buffer.frames(); ++n)
    {
        worst = std::max(worst, std::abs(double(buffer.channel(0)[n]) - fixtureSample(n)));
    }
    REQUIRE(worst <= 1.0 / 32768.0);
    REQUIRE(std::abs(20.0 * std::log10(rms(buffer) / kFixtureRms)) < 0.01);
}

TEST_CASE("decode reads the MP3 fixture", "[media]")
{
    const auto result = decodeAudioFile(fixture("sine-1k-48k-mono.mp3"), kBound);
    INFO(result.error);
    REQUIRE(result.audio.has_value());
    const auto &decoded = *result.audio;
    REQUIRE(decoded.format == "mp3");
    REQUIRE(decoded.fileChannels == 1);
    const auto &buffer = *decoded.buffer;
    REQUIRE(buffer.channels() == 1);
    REQUIRE(buffer.sampleRate() == kFixtureRate);
    // About 0.1 s: within one MP3 frame (1152 samples) of the encoded length.
    REQUIRE(std::abs(buffer.frames() - kFixtureFrames) <= 1152);
    REQUIRE(std::abs(20.0 * std::log10(rms(buffer) / kFixtureRms)) < 1.0);
}

TEST_CASE("decode keeps the first two of four channels", "[media]")
{
    const auto audio = gg::test::makeTestAudio(4, 48000, 500);
    const auto file = dir() / "four-channels.wav";
    gg::test::writeFile(file, gg::test::wavBytes(audio, SampleFormat::Int16));

    const auto result = decodeAudioFile(file, kBound);
    INFO(result.error);
    REQUIRE(result.audio.has_value());
    REQUIRE(result.audio->fileChannels == 4);
    REQUIRE(result.audio->buffer->channels() == 2);
    REQUIRE(result.audio->buffer->frames() == 500);
    REQUIRE(maxError(*result.audio->buffer, audio, 2) <= gg::test::lsbOf(SampleFormat::Int16));
}

TEST_CASE("decode refuses a file that ends before its header says, as truncated", "[media]")
{
    SECTION("a WAV whose data chunk claims more than the file holds")
    {
        const auto audio = gg::test::makeTestAudio(2, 48000, 600);
        const auto file = dir() / "truncated-data.wav";
        // The header claims 1000 frames; the file holds 600.
        gg::test::writeFile(file, gg::test::wavBytes(audio, SampleFormat::Int16, 1000 * 4));
        requireError(decodeAudioFile(file, kBound), file, "truncated");
    }
    SECTION("a WAV never finalized, whose data size is a placeholder over the memory cap")
    {
        // A recorder that stopped before rewriting its header: 0x7FFFFFF0 bytes of 44.1 kHz
        // 16-bit stereo would be 12,173 s, 4.3 GB decoded, but the file holds 600 frames. It is
        // truncated, not over a 64 MiB cap.
        const auto audio = gg::test::makeTestAudio(2, 44100, 600);
        const auto file = dir() / "unfinalized.wav";
        gg::test::writeFile(file, gg::test::wavBytes(audio, SampleFormat::Int16, 0x7FFFFFF0));
        requireError(decodeAudioFile(file, uint64_t{64} << 20), file, "truncated");
    }
    SECTION("an AIFF whose COMM chunk claims more frames than the file holds")
    {
        gg::test::Bytes b{gg::test::aiffBytes(gg::test::makeTestAudio(2, 48000, 600),
                                              SampleFormat::Int16, false)};
        b.patch(22, 1000, 4, true); // COMM's frame count
        const auto file = dir() / "truncated-data.aiff";
        gg::test::writeFile(file, b.data);
        requireError(decodeAudioFile(file, kBound), file, "truncated");
    }
    SECTION("the FLAC fixture cut in half")
    {
        auto bytes = gg::test::readFile(fixture("sine-1k-48k-mono.flac"));
        bytes.resize(bytes.size() / 2);
        const auto file = dir() / "truncated.flac";
        gg::test::writeFile(file, bytes);
        requireError(decodeAudioFile(file, kBound), file, "truncated");
    }
    SECTION("the MP3 fixture cut to its first 1000 bytes")
    {
        // Its Info tag counts 4800 frames; about two MP3 frames (2304) remain.
        auto bytes = gg::test::readFile(fixture("sine-1k-48k-mono.mp3"));
        bytes.resize(1000);
        const auto file = dir() / "truncated.mp3";
        gg::test::writeFile(file, bytes);
        requireError(decodeAudioFile(file, kBound), file, "truncated");
    }
}

TEST_CASE("decode tolerates a data size up to 1 KiB beyond the file (ruling R10)", "[media]")
{
    // 600 stereo 16-bit frames: 2400 bytes of audio.
    const auto audio = gg::test::makeTestAudio(2, 48000, 600);
    SECTION("100 bytes beyond: the frames present decode")
    {
        const auto file = dir() / "overstated-100.wav";
        gg::test::writeFile(file, gg::test::wavBytes(audio, SampleFormat::Int16, 2400 + 100));
        const auto result = decodeAudioFile(file, kBound);
        INFO("error: " << result.error);
        REQUIRE(result.audio.has_value());
        REQUIRE(result.audio->buffer->channels() == 2);
        REQUIRE(result.audio->buffer->frames() == 600);
        REQUIRE(maxError(*result.audio->buffer, audio, 2) <= gg::test::lsbOf(SampleFormat::Int16));
    }
    SECTION("exactly 1024 bytes beyond decodes; 1028 is truncated")
    {
        const auto atLimit = dir() / "overstated-1024.wav";
        gg::test::writeFile(atLimit, gg::test::wavBytes(audio, SampleFormat::Int16, 2400 + 1024));
        const auto result = decodeAudioFile(atLimit, kBound);
        INFO("error: " << result.error);
        REQUIRE(result.audio.has_value());
        REQUIRE(result.audio->buffer->frames() == 600);

        const auto over = dir() / "overstated-1028.wav";
        gg::test::writeFile(over, gg::test::wavBytes(audio, SampleFormat::Int16, 2400 + 1028));
        requireError(decodeAudioFile(over, kBound), over, "truncated");
    }
    SECTION("4096 bytes beyond: truncated")
    {
        const auto file = dir() / "overstated-4096.wav";
        gg::test::writeFile(file, gg::test::wavBytes(audio, SampleFormat::Int16, 2400 + 4096));
        requireError(decodeAudioFile(file, kBound), file, "truncated");
    }
}

TEST_CASE("decode tolerates an AIFF frame count up to 1 KiB beyond the file (ruling R10)",
          "[media]")
{
    // AIFF declares its length in COMM's frame count, not a data size: the same 1 KiB of slack
    // applies to the bytes those frames would take. AIFF, big-endian 16-bit stereo, 4 bytes a
    // frame (COMM's count at byte 22); AIFC `sowt`, 24-bit mono, 3 bytes a frame (byte 34, after
    // FVER).
    struct Case
    {
        bool aifc;
        SampleFormat format;
        uint32_t channels;
        uint32_t bytesPerFrame;
        size_t countOffset;
    };
    for (const Case c :
         {Case{false, SampleFormat::Int16, 2, 4, 22}, Case{true, SampleFormat::Int24, 1, 3, 34}})
    {
        INFO((c.aifc ? "AIFC" : "AIFF"));
        const auto audio = gg::test::makeTestAudio(c.channels, 48000, 600);
        const auto claiming = [&](uint64_t frames, const char *name) {
            gg::test::Bytes b{gg::test::aiffBytes(audio, c.format, c.aifc)};
            const auto file = dir() / (std::string(name) + (c.aifc ? ".aifc" : ".aiff"));
            b.patch(c.countOffset, frames, 4, true);
            gg::test::writeFile(file, b.data);
            return file;
        };
        // The fixture's own count, read back where the test patches it.
        {
            const auto bytes = gg::test::aiffBytes(audio, c.format, c.aifc);
            REQUIRE(std::string(bytes.begin() + int(c.countOffset) - 10,
                                bytes.begin() + int(c.countOffset) - 6) == "COMM");
            uint32_t count = 0;
            for (int i = 0; i < 4; ++i)
            {
                count = count << 8 | bytes[c.countOffset + size_t(i)];
            }
            REQUIRE(count == 600);
        }

        const uint64_t slackFrames = 1024 / c.bytesPerFrame; // 256 or 341
        for (const uint64_t extra : {uint64_t{25}, slackFrames})
        {
            INFO("frames beyond: " << extra);
            const auto file = claiming(600 + extra, "overstated-within");
            const auto result = decodeAudioFile(file, kBound);
            INFO("error: " << result.error);
            REQUIRE(result.audio.has_value());
            REQUIRE(result.audio->format == "aiff");
            REQUIRE(result.audio->buffer->channels() == c.channels);
            REQUIRE(result.audio->buffer->frames() == 600);
            REQUIRE(maxError(*result.audio->buffer, audio, c.channels) <=
                    gg::test::lsbOf(c.format));
        }
        // One frame more is past the slack (1028 or 1026 bytes): truncated.
        const auto over = claiming(600 + slackFrames + 1, "overstated-past");
        requireError(decodeAudioFile(over, kBound), over, "truncated");
    }
}

TEST_CASE("decode allocates no more than the file vouches for when a header claims audio it lacks",
          "[media][serial]")
{
    // Each file is tiny but claims up to 600 s, within the memory cap's ceiling; allocating and
    // zeroing that much (1.8 GB for the 384 kHz stereo ones) would take far longer than the bound
    // below. Ruling R7 as amended (INTENT §23 #54): an uncompressed file's buffer never exceeds
    // what its bytes hold, and a compressed header is trusted only up to 64 bytes of decoded audio
    // per byte of the file, which these tiny files are far beyond. decode_memory_tests.cpp pins
    // that bound.
    const auto requireQuickError = [](const fs::path &file, const std::string &reason) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = decodeAudioFile(file, kBound);
        const std::chrono::duration<double> took = std::chrono::steady_clock::now() - start;
        requireError(result, file, reason);
        REQUIRE(took.count() < 0.5);
    };
    SECTION("a 44-byte WAV header claiming 921,600,000 bytes at 768 kHz, 8-bit stereo")
    {
        // dr_wav itself refuses rates above 384 kHz (DRWAV_MAX_SAMPLE_RATE), so this one never
        // reaches the size check.
        const auto file = dir() / "huge-claim-768k.wav";
        gg::test::writeFile(file, gg::test::wavHeader(1, 2, 768000, 8, 921600000).data);
        REQUIRE(fs::file_size(file) == 44);
        requireQuickError(file, "");
    }
    SECTION("a 44-byte WAV header claiming 460,800,000 bytes (600 s) at 384 kHz, 8-bit stereo")
    {
        const auto file = dir() / "huge-claim-384k.wav";
        gg::test::writeFile(file, gg::test::wavHeader(1, 2, 384000, 8, 460800000).data);
        REQUIRE(fs::file_size(file) == 44);
        requireQuickError(file, "truncated");
    }
    SECTION("an AIFF claiming 230,400,000 frames (600 s) at 384 kHz, 16-bit stereo")
    {
        gg::test::Bytes b{
            gg::test::aiffBytes(gg::test::makeTestAudio(2, 384000, 2), SampleFormat::Int16, false)};
        b.patch(22, 230400000, 4, true); // COMM's frame count, which dr_wav takes as given
        const auto file = dir() / "huge-claim.aiff";
        gg::test::writeFile(file, b.data);
        requireQuickError(file, "truncated");
    }
    SECTION("the FLAC fixture with STREAMINFO claiming 600 s")
    {
        auto bytes = gg::test::readFile(fixture("sine-1k-48k-mono.flac"));
        setFlacTotalSamples(bytes, 600 * 48000);
        const auto file = dir() / "huge-claim.flac";
        gg::test::writeFile(file, bytes);
        requireQuickError(file, "truncated");
    }
}

TEST_CASE("decode fails at once on a chunk size that would seek past the file", "[media][serial]")
{
    // dr_wav skips a chunk by seeking forward in steps of at most 2^31 − 1 bytes. A chunk size
    // that wraps below its header's (an RF64 ds64 chunk under 24 bytes, a W64 chunk under 24) or
    // is simply huge would take billions of steps if every seek past the file succeeded, as
    // fseek's do: decoding must refuse such a seek and fail within the bound.
    const auto requireQuickError = [](const fs::path &file) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = decodeAudioFile(file, kBound);
        const std::chrono::duration<double> took = std::chrono::steady_clock::now() - start;
        requireError(result, file);
        REQUIRE(took.count() < 0.5);
    };
    // The bytes given, then zeros up to `size`.
    const auto craft = [](const char *name, std::vector<uint8_t> bytes, std::size_t size) {
        bytes.resize(size, 0);
        const auto file = dir() / name;
        gg::test::writeFile(file, bytes);
        return file;
    };
    // A W64 file whose junk chunk declares `junkSize` bytes (its 24-byte header included).
    const auto w64 = [](uint64_t junkSize) {
        std::vector<uint8_t> bytes{
            // riff GUID, size 1000, wave GUID
            0x72, 0x69, 0x66, 0x66, 0x2E, 0x91, 0xCF, 0x11, 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1,
            0x00, 0x00, 0xE8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x77, 0x61, 0x76, 0x65,
            0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A,
            // fmt GUID, size 40: PCM, mono, 48 kHz, 16-bit
            0x66, 0x6D, 0x74, 0x20, 0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E,
            0xDB, 0x8A, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
            0x80, 0xBB, 0x00, 0x00, 0x00, 0x77, 0x01, 0x00, 0x02, 0x00, 0x10, 0x00,
            // junk GUID, then its size (below)
            0x6A, 0x75, 0x6E, 0x6B, 0xF3, 0xAC, 0xD3, 0x11, 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E,
            0xDB, 0x8A};
        for (int i = 0; i < 8; ++i)
        {
            bytes.push_back(static_cast<uint8_t>(junkSize >> (8 * i)));
        }
        // data GUID, size 124
        const uint8_t data[] = {0x64, 0x61, 0x74, 0x61, 0xF3, 0xAC, 0xD3, 0x11,
                                0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A,
                                0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        bytes.insert(bytes.end(), std::begin(data), std::end(data));
        return bytes;
    };
    SECTION("an RF64 file whose ds64 chunk declares 0 bytes")
    {
        requireQuickError(
            craft("rf64-ds64-0.wav",
                  {// RF64, size −1, WAVE; ds64 of size 0, then what its body would hold
                   0x52, 0x46, 0x36, 0x34, 0xFF, 0xFF, 0xFF, 0xFF, 0x57, 0x41, 0x56, 0x45, 0x64,
                   0x73, 0x36, 0x34, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
                   0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00,
                   0x00, 0x00, 0x00, 0x00, 0x00,
                   // fmt: PCM, mono, 48 kHz, 16-bit; data of size −1
                   0x66, 0x6D, 0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80,
                   0xBB, 0x00, 0x00, 0x00, 0x77, 0x01, 0x00, 0x02, 0x00, 0x10, 0x00, 0x64, 0x61,
                   0x74, 0x61, 0xFF, 0xFF, 0xFF, 0xFF},
                  176));
    }
    SECTION("a W64 file whose junk chunk declares 2^63 - 256 bytes")
    {
        requireQuickError(craft("w64-hugejunk.wav", w64(0x7FFFFFFFFFFFFF00ull), 228));
    }
    SECTION("a W64 file whose junk chunk declares 8 bytes, fewer than its header")
    {
        requireQuickError(craft("w64-junk-8.wav", w64(8), 228));
    }
}

TEST_CASE("decode reads a FLAC whose STREAMINFO gives no length", "[media]")
{
    auto bytes = gg::test::readFile(fixture("sine-1k-48k-mono.flac"));
    setFlacTotalSamples(bytes, 0);
    const auto file = dir() / "unknown-length.flac";
    gg::test::writeFile(file, bytes);

    const auto result = decodeAudioFile(file, kBound);
    INFO(result.error);
    REQUIRE(result.audio.has_value());
    REQUIRE(result.audio->format == "flac");
    REQUIRE(result.audio->buffer->frames() == kFixtureFrames);
}

TEST_CASE("decode reads an MP3 named .wav as an MP3", "[media]")
{
    const auto file = dir() / "really-an-mp3.wav";
    fs::copy_file(fixture("sine-1k-48k-mono.mp3"), file, fs::copy_options::overwrite_existing);

    const auto result = decodeAudioFile(file, kBound);
    INFO(result.error);
    REQUIRE(result.audio.has_value());
    REQUIRE(result.audio->format == "mp3");
    REQUIRE(result.audio->buffer->sampleRate() == kFixtureRate);
}

TEST_CASE("decode refuses what it can't read, naming the file", "[media]")
{
    SECTION("a missing file")
    {
        const auto file = dir() / "no-such-file.wav";
        fs::remove(file);
        requireError(decodeAudioFile(file, kBound), file);
    }
    SECTION("a zero-length file")
    {
        const auto file = dir() / "empty.wav";
        gg::test::writeFile(file, {});
        requireError(decodeAudioFile(file, kBound), file);
    }
    SECTION("a truncated WAV header")
    {
        const auto bytes =
            gg::test::wavBytes(gg::test::makeTestAudio(1, 48000, 100), SampleFormat::Int16);
        const auto file = dir() / "truncated-header.wav";
        gg::test::writeFile(file, std::span(bytes).first(30)); // ends inside the fmt chunk
        requireError(decodeAudioFile(file, kBound), file);
    }
    SECTION("random bytes named .wav")
    {
        std::vector<uint8_t> bytes(4096);
        uint64_t state = 0x9E3779B97F4A7C15ull; // a fixed splitmix64 stream
        for (auto &byte : bytes)
        {
            state += 0x9E3779B97F4A7C15ull;
            uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            byte = static_cast<uint8_t>(z ^ (z >> 31));
        }
        const auto file = dir() / "random.wav";
        gg::test::writeFile(file, bytes);
        requireError(decodeAudioFile(file, kBound), file);
    }
    SECTION("a WAV with no frames")
    {
        const auto file = dir() / "no-frames.wav";
        gg::test::writeFile(
            file, gg::test::wavBytes(gg::test::makeTestAudio(1, 48000, 0), SampleFormat::Int16));
        requireError(decodeAudioFile(file, kBound), file);
    }
    SECTION("a WAV with no channels")
    {
        auto b = gg::test::wavHeader(1, 0, 48000, 16, 64);
        b.data.resize(b.data.size() + 64);
        const auto file = dir() / "no-channels.wav";
        gg::test::writeFile(file, b.data);
        requireError(decodeAudioFile(file, kBound), file);
    }
    SECTION("a WAV above 384 kHz, which dr_wav refuses")
    {
        const auto file = dir() / "800k.wav";
        gg::test::writeFile(
            file, gg::test::wavBytes(gg::test::makeTestAudio(1, 800000, 64), SampleFormat::Int16));
        requireError(decodeAudioFile(file, kBound), file);
    }
    SECTION("a FLAC above 768 kHz")
    {
        // STREAMINFO's 20-bit sample rate: bytes 10 and 11 and the top nibble of 12.
        auto bytes = gg::test::readFile(fixture("sine-1k-48k-mono.flac"));
        const uint32_t rate = 800000;
        bytes[8 + 10] = static_cast<uint8_t>(rate >> 12);
        bytes[8 + 11] = static_cast<uint8_t>(rate >> 4);
        bytes[8 + 12] = static_cast<uint8_t>(((rate & 0xfu) << 4) | (bytes[8 + 12] & 0x0fu));
        const auto file = dir() / "800k.flac";
        gg::test::writeFile(file, bytes);
        requireError(decodeAudioFile(file, kBound), file, "sample rate");
    }
    SECTION("a WAV in an encoding dr_wav can't decode")
    {
        auto b = gg::test::wavHeader(0x0055, 1, 48000, 16, 64); // MPEG Layer 3 in a WAV
        b.data.resize(b.data.size() + 64);
        const auto file = dir() / "mpeg-in-wav.wav";
        gg::test::writeFile(file, b.data);
        requireError(decodeAudioFile(file, kBound), file, "unsupported WAV encoding");
    }
#if !defined(_WIN32)
    SECTION("a file in a directory that can't be searched")
    {
        if (geteuid() == 0)
        {
            SKIP("root ignores directory permissions");
        }
        const auto locked = dir() / "locked";
        fs::create_directories(locked);
        const auto file = locked / "inside.wav";
        gg::test::writeFile(
            file, gg::test::wavBytes(gg::test::makeTestAudio(1, 48000, 8), SampleFormat::Int16));
        fs::permissions(locked, fs::perms::owner_all, fs::perm_options::remove);
        const auto result = decodeAudioFile(file, kBound);
        fs::permissions(locked, fs::perms::owner_all, fs::perm_options::add);
        requireError(result, file, "can't be read");
    }
#endif
}

TEST_CASE("decode refuses, before allocating, a file over its bound in bytes", "[media][serial]")
{
    // 20 minutes of 16 kHz mono 16-bit, written sparsely: the header, then the file extended with
    // zeros (38.4 MB). It decodes to 19,200,000 floats, 76.8 MB (73.24 MiB): within a 2 GiB
    // bound, over a 64 MiB one. The bound replaces M1's 600 s cap (spec §6.5).
    const int64_t frames = int64_t{20} * 60 * 16000;
    const auto file = dir() / "twenty-minutes.wav";
    {
        gg::test::Bytes b{
            gg::test::wavBytes(gg::test::makeTestAudio(1, 16000, 0), SampleFormat::Int16)};
        b.patch(40, uint64_t(frames) * 2, 4, false);             // the data chunk's size
        b.patch(4, 4 + 24 + 8 + uint64_t(frames) * 2, 4, false); // the RIFF size
        gg::test::writeFile(file, b.data);
        fs::resize_file(file, b.data.size() + uint64_t(frames) * 2);
    }

    const auto ok = decodeAudioFile(file, uint64_t{2} << 30);
    INFO(ok.error);
    REQUIRE(ok.audio.has_value());
    REQUIRE(ok.audio->buffer->frames() == frames);

    gg::media::DecodeResult refused;
    uint64_t peakBytes = 0;
    bool complete = false;
    {
        const gg::test::AllocPeak peak;
        refused = decodeAudioFile(file, uint64_t{64} << 20);
        peakBytes = peak.peakBytes();
        complete = peak.complete();
    }
    requireError(refused, file, "needs 74 MB, the memory cap allows 64 MB");
    REQUIRE(refused.error == "twenty-minutes.wav: needs 74 MB, the memory cap allows 64 MB");
    // Refused from the header: nothing the size of the audio was allocated, only the message and
    // small change.
    if (gg::test::AllocPeak::available())
    {
        REQUIRE(complete);
        REQUIRE(peakBytes < 64 * 1024);
    }

    // At exactly the decoded size it fits; one byte less and it doesn't.
    const uint64_t exact = uint64_t(frames) * sizeof(float);
    REQUIRE(decodeAudioFile(file, exact).audio.has_value());
    requireError(decodeAudioFile(file, exact - 1), file,
                 "needs 74 MB, the memory cap allows 73 MB");
}

TEST_CASE("decode bounds what a compressed file decodes to, whatever its header says", "[media]")
{
    // The FLAC fixture (4800 mono frames, 19,200 bytes decoded) with STREAMINFO giving no length:
    // the decode itself has to prove it within the bound.
    auto bytes = gg::test::readFile(fixture("sine-1k-48k-mono.flac"));
    setFlacTotalSamples(bytes, 0);
    const auto file = dir() / "unbounded.flac";
    gg::test::writeFile(file, bytes);
    REQUIRE(decodeAudioFile(file, 19200).audio.has_value());
    requireError(decodeAudioFile(file, 19199), file, "the memory cap allows");

    // With its length, the header is refused before decoding.
    requireError(decodeAudioFile(fixture("sine-1k-48k-mono.flac"), 19199),
                 fixture("sine-1k-48k-mono.flac"), "needs 1 MB, the memory cap allows 0 MB");
}

TEST_CASE("decodeAudioBytes bounds what a compressed payload decodes to", "[media]")
{
    // decodeAudioBytes' overrun path (an embedded payload, spec §6.6): the FLAC fixture with
    // STREAMINFO giving no length passes the header's check, so the decode itself proves it past
    // the bound, as decodeAudioFile's does, with the reason alone.
    auto bytes = gg::test::readFile(fixture("sine-1k-48k-mono.flac"));
    setFlacTotalSamples(bytes, 0);
    const std::string_view view(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    const auto fits = gg::media::decodeAudioBytes(view, 19200);
    REQUIRE(fits.audio.has_value());
    CHECK(fits.audio->buffer->frames() == 4800);
    const auto over = gg::media::decodeAudioBytes(view, 19199);
    REQUIRE_FALSE(over.audio.has_value());
    CHECK(over.error == "decodes to more than the memory cap allows (0 MB)");
    const auto file = dir() / "unbounded-bytes.flac";
    gg::test::writeFile(file, bytes);
    CHECK(decodeAudioFile(file, 19199).error == "unbounded-bytes.flac: " + over.error);
}

TEST_CASE("decode opens a UTF-8 path with non-ASCII characters", "[media]")
{
    const auto audio = gg::test::makeTestAudio(1, 48000, 64);
    const auto file = dir() / fs::path(u8"sämple ✓.wav");
    gg::test::writeFile(file, gg::test::wavBytes(audio, SampleFormat::Int16));

    const auto result = decodeAudioFile(file, kBound);
    INFO(result.error);
    REQUIRE(result.audio.has_value());
    REQUIRE(result.audio->buffer->frames() == 64);
    REQUIRE(maxError(*result.audio->buffer, audio, 1) <= gg::test::lsbOf(SampleFormat::Int16));
}

TEST_CASE("decode stops once its cancel flag is set", "[media]")
{
    // The sample loader's prompt stop (carry-over [194]): the flag is read once per chunk, so a
    // set flag stops a decode under way and gives no audio. A flag never set changes nothing.
    const auto wav = dir() / "cancel.wav";
    gg::test::writeFile(
        wav, gg::test::wavBytes(gg::test::makeTestAudio(2, 48000, 48000), SampleFormat::Int16));
    const fs::path file =
        GENERATE_COPY(wav, fixture("sine-1k-48k-mono.flac"), fixture("sine-1k-48k-mono.mp3"));
    INFO(gg::test::utf8(file.filename()));
    std::atomic<bool> cancel{false};
    const auto plain = decodeAudioFile(file, kBound);
    const auto unset = decodeAudioFile(file, kBound, &cancel);
    REQUIRE(plain.audio.has_value());
    REQUIRE(unset.audio.has_value());
    const auto &a = *plain.audio->buffer;
    const auto &b = *unset.audio->buffer;
    REQUIRE(a.channels() == b.channels());
    REQUIRE(a.frames() == b.frames());
    for (uint32_t c = 0; c < a.channels(); ++c)
    {
        CHECK(std::memcmp(a.channel(c), b.channel(c), size_t(a.frames()) * sizeof(float)) == 0);
    }

    cancel = true;
    const auto cancelled = decodeAudioFile(file, kBound, &cancel);
    CHECK_FALSE(cancelled.audio.has_value());
    CHECK(cancelled.error.find("cancelled") != std::string::npos);
}

TEST_CASE("decodeAudioBytes decodes what decodeAudioFile decodes", "[media]")
{
    const auto wav = dir() / "bytes.wav";
    gg::test::writeFile(
        wav, gg::test::wavBytes(gg::test::makeTestAudio(2, 44100, 4410), SampleFormat::Float32));
    const fs::path file =
        GENERATE_COPY(wav, fixture("sine-1k-48k-mono.flac"), fixture("sine-1k-48k-mono.mp3"));
    INFO(gg::test::utf8(file.filename()));
    const auto content = gg::test::readFile(file);
    const std::string_view view(reinterpret_cast<const char *>(content.data()), content.size());

    const auto fromFile = decodeAudioFile(file, kBound);
    const auto fromBytes = gg::media::decodeAudioBytes(view, kBound);
    INFO(fromFile.error << " / " << fromBytes.error);
    REQUIRE(fromFile.audio.has_value());
    REQUIRE(fromBytes.audio.has_value());
    REQUIRE(fromBytes.audio->format == fromFile.audio->format);
    REQUIRE(fromBytes.audio->fileChannels == fromFile.audio->fileChannels);
    const auto &a = *fromFile.audio->buffer;
    const auto &b = *fromBytes.audio->buffer;
    REQUIRE(a.channels() == b.channels());
    REQUIRE(a.frames() == b.frames());
    REQUIRE(a.sampleRate() == b.sampleRate());
    for (uint32_t c = 0; c < a.channels(); ++c)
    {
        CHECK(std::memcmp(a.channel(c), b.channel(c), size_t(a.frames()) * sizeof(float)) == 0);
    }

    // The same bound, from the header.
    const auto refused = gg::media::decodeAudioBytes(view, 1024);
    REQUIRE_FALSE(refused.audio.has_value());
    REQUIRE(refused.error.find("the memory cap allows 0 MB") != std::string::npos);
}

TEST_CASE("decodeAudioBytes refuses a truncated file", "[media]")
{
    // decodeAudioFile's message without the file's name, which the bytes don't have.
    const auto audio = gg::test::makeTestAudio(2, 48000, 600);
    const auto file = dir() / "bytes-truncated.wav";
    const auto content = gg::test::wavBytes(audio, SampleFormat::Float32, 1000 * 8);
    gg::test::writeFile(file, content);
    const auto fromFile = decodeAudioFile(file, kBound);
    requireError(fromFile, file, "truncated");

    const std::string_view view(reinterpret_cast<const char *>(content.data()), content.size());
    const auto fromBytes = gg::media::decodeAudioBytes(view, kBound);
    REQUIRE_FALSE(fromBytes.audio.has_value());
    REQUIRE(fromFile.error == "bytes-truncated.wav: " + fromBytes.error);

    // Cut short anywhere in its header or audio, the bytes are refused, never read past.
    for (const size_t keep :
         {size_t{0}, size_t{4}, size_t{12}, size_t{40}, size_t{44}, content.size() - 1500})
    {
        INFO("keep " << keep);
        const auto cut = gg::media::decodeAudioBytes(view.substr(0, keep), kBound);
        REQUIRE_FALSE(cut.audio.has_value());
        REQUIRE_FALSE(cut.error.empty());
    }
}
