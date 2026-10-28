// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "gg/media/Decode.h"
#include "media/AudioFixtures.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace
{
namespace fs = std::filesystem;
using gg::media::decodeAudioFile;
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
    const auto u8name = file.filename().u8string();
    const std::string prefix = std::string(u8name.begin(), u8name.end()) + ": ";
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

    const auto result = decodeAudioFile(file);
    INFO(file.filename().string() << ": " << result.error);
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

    const auto result = decodeAudioFile(file);
    INFO(file.filename().string() << ": " << result.error);
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
    const auto result = decodeAudioFile(fixture("sine-1k-48k-mono.flac"));
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
    const auto result = decodeAudioFile(fixture("sine-1k-48k-mono.mp3"));
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

    const auto result = decodeAudioFile(file);
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
        requireError(decodeAudioFile(file), file, "truncated");
    }
    SECTION("an AIFF whose COMM chunk claims more frames than the file holds")
    {
        gg::test::Bytes b{gg::test::aiffBytes(gg::test::makeTestAudio(2, 48000, 600),
                                              SampleFormat::Int16, false)};
        b.patch(22, 1000, 4, true); // COMM's frame count
        const auto file = dir() / "truncated-data.aiff";
        gg::test::writeFile(file, b.data);
        requireError(decodeAudioFile(file), file, "truncated");
    }
    SECTION("the FLAC fixture cut in half")
    {
        auto bytes = gg::test::readFile(fixture("sine-1k-48k-mono.flac"));
        bytes.resize(bytes.size() / 2);
        const auto file = dir() / "truncated.flac";
        gg::test::writeFile(file, bytes);
        requireError(decodeAudioFile(file), file, "truncated");
    }
    SECTION("the MP3 fixture cut to its first 1000 bytes")
    {
        // Its Info tag counts 4800 frames; about two MP3 frames (2304) remain.
        auto bytes = gg::test::readFile(fixture("sine-1k-48k-mono.mp3"));
        bytes.resize(1000);
        const auto file = dir() / "truncated.mp3";
        gg::test::writeFile(file, bytes);
        requireError(decodeAudioFile(file), file, "truncated");
    }
}

TEST_CASE("decode never allocates for audio a header claims but the file lacks", "[media]")
{
    // Each file is tiny but claims up to 600 s, within every limit; allocating and zeroing that
    // much (1.8 GB for the 384 kHz stereo ones) would take far longer than the bound below.
    const auto requireQuickError = [](const fs::path &file, const std::string &reason) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = decodeAudioFile(file);
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

TEST_CASE("decode reads a FLAC whose STREAMINFO gives no length", "[media]")
{
    auto bytes = gg::test::readFile(fixture("sine-1k-48k-mono.flac"));
    setFlacTotalSamples(bytes, 0);
    const auto file = dir() / "unknown-length.flac";
    gg::test::writeFile(file, bytes);

    const auto result = decodeAudioFile(file);
    INFO(result.error);
    REQUIRE(result.audio.has_value());
    REQUIRE(result.audio->format == "flac");
    REQUIRE(result.audio->buffer->frames() == kFixtureFrames);
}

TEST_CASE("decode reads an MP3 named .wav as an MP3", "[media]")
{
    const auto file = dir() / "really-an-mp3.wav";
    fs::copy_file(fixture("sine-1k-48k-mono.mp3"), file, fs::copy_options::overwrite_existing);

    const auto result = decodeAudioFile(file);
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
        requireError(decodeAudioFile(file), file);
    }
    SECTION("a zero-length file")
    {
        const auto file = dir() / "empty.wav";
        gg::test::writeFile(file, {});
        requireError(decodeAudioFile(file), file);
    }
    SECTION("a truncated WAV header")
    {
        const auto bytes =
            gg::test::wavBytes(gg::test::makeTestAudio(1, 48000, 100), SampleFormat::Int16);
        const auto file = dir() / "truncated-header.wav";
        gg::test::writeFile(file, std::span(bytes).first(30)); // ends inside the fmt chunk
        requireError(decodeAudioFile(file), file);
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
        requireError(decodeAudioFile(file), file);
    }
    SECTION("a WAV with no frames")
    {
        const auto file = dir() / "no-frames.wav";
        gg::test::writeFile(
            file, gg::test::wavBytes(gg::test::makeTestAudio(1, 48000, 0), SampleFormat::Int16));
        requireError(decodeAudioFile(file), file);
    }
    SECTION("a WAV with no channels")
    {
        auto b = gg::test::wavHeader(1, 0, 48000, 16, 64);
        b.data.resize(b.data.size() + 64);
        const auto file = dir() / "no-channels.wav";
        gg::test::writeFile(file, b.data);
        requireError(decodeAudioFile(file), file);
    }
    SECTION("a WAV above 384 kHz, which dr_wav refuses")
    {
        const auto file = dir() / "800k.wav";
        gg::test::writeFile(
            file, gg::test::wavBytes(gg::test::makeTestAudio(1, 800000, 64), SampleFormat::Int16));
        requireError(decodeAudioFile(file), file);
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
        requireError(decodeAudioFile(file), file, "sample rate");
    }
    SECTION("a WAV in an encoding dr_wav can't decode")
    {
        auto b = gg::test::wavHeader(0x0055, 1, 48000, 16, 64); // MPEG Layer 3 in a WAV
        b.data.resize(b.data.size() + 64);
        const auto file = dir() / "mpeg-in-wav.wav";
        gg::test::writeFile(file, b.data);
        requireError(decodeAudioFile(file), file, "unsupported WAV encoding");
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
        const auto result = decodeAudioFile(file);
        fs::permissions(locked, fs::perms::owner_all, fs::perm_options::add);
        requireError(result, file, "can't be read");
    }
#endif
}

TEST_CASE("decode accepts 600 s and refuses a file just over", "[media]")
{
    // 8 kHz mono 16-bit, written sparsely: the header, then the file extended with zeros.
    const auto header =
        gg::test::wavBytes(gg::test::makeTestAudio(1, 8000, 0), SampleFormat::Int16);
    const auto write = [&](const fs::path &file, int64_t frames) {
        auto bytes = header;
        gg::test::Bytes b{std::move(bytes)};
        b.patch(40, uint64_t(frames) * 2, 4, false);             // the data chunk's size
        b.patch(4, 4 + 24 + 8 + uint64_t(frames) * 2, 4, false); // the RIFF size
        gg::test::writeFile(file, b.data);
        fs::resize_file(file, b.data.size() + uint64_t(frames) * 2);
    };

    const auto atLimit = dir() / "600s.wav";
    write(atLimit, 600 * 8000);
    const auto ok = decodeAudioFile(atLimit);
    INFO(ok.error);
    REQUIRE(ok.audio.has_value());
    REQUIRE(ok.audio->buffer->frames() == 600 * 8000);

    const auto over = dir() / "600.01s.wav";
    write(over, 600 * 8000 + 80); // 600.01 s
    const auto refused = decodeAudioFile(over);
    requireError(refused, over);
    REQUIRE(refused.error.find("600") != std::string::npos);
}

TEST_CASE("decode opens a UTF-8 path with non-ASCII characters", "[media]")
{
    const auto audio = gg::test::makeTestAudio(1, 48000, 64);
    const auto file = dir() / fs::path(u8"sämple ✓.wav");
    gg::test::writeFile(file, gg::test::wavBytes(audio, SampleFormat::Int16));

    const auto result = decodeAudioFile(file);
    INFO(result.error);
    REQUIRE(result.audio.has_value());
    REQUIRE(result.audio->buffer->frames() == 64);
    REQUIRE(maxError(*result.audio->buffer, audio, 1) <= gg::test::lsbOf(SampleFormat::Int16));
}
