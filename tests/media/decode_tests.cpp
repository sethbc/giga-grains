// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

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

/// The error a failed decode of `file` must give: no audio, and a message naming the file.
void requireError(const gg::media::DecodeResult &result, const fs::path &file)
{
    INFO("error: " << result.error);
    REQUIRE_FALSE(result.audio.has_value());
    REQUIRE_FALSE(result.error.empty());
    const auto u8name = file.filename().u8string();
    REQUIRE(result.error.find(std::string(u8name.begin(), u8name.end())) != std::string::npos);
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
    const auto result = decodeAudioFile(fs::path(GG_AUDIO_FIXTURE_DIR) / "sine-1k-48k-mono.flac");
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
    const auto result = decodeAudioFile(fs::path(GG_AUDIO_FIXTURE_DIR) / "sine-1k-48k-mono.mp3");
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

TEST_CASE("decode keeps the frames of a file that ends before its header says", "[media]")
{
    const auto audio = gg::test::makeTestAudio(2, 48000, 600);
    const auto file = dir() / "truncated-data.wav";
    // The header claims 1000 frames; the file holds 600.
    gg::test::writeFile(file, gg::test::wavBytes(audio, SampleFormat::Int16, 1000 * 4));

    const auto result = decodeAudioFile(file);
    INFO(result.error);
    REQUIRE(result.audio.has_value());
    REQUIRE(result.audio->buffer->frames() == 600);
    REQUIRE(maxError(*result.audio->buffer, audio, 2) <= gg::test::lsbOf(SampleFormat::Int16));
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
