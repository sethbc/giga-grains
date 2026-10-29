// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// loadSample (gg/media/Sample.h): the decoded audio and the manifest entry of a file; then prepare,
// the source the engine plays. Every test works in a folder of its own under GG_TEST_OUTPUT_DIR.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/MachineSettings.h"
#include "gg/core/SessionState.h"
#include "gg/media/Sample.h"
#include "gg/media/Sha256.h"
#include "media/AudioFixtures.h"
#include "support/Utf8.h"

namespace
{
namespace fs = std::filesystem;
using gg::core::MachineSettings;
using gg::media::loadSample;
using gg::test::SampleFormat;
using gg::test::utf8;

/// An empty folder of the test's own.
fs::path freshDir(const char *name)
{
    const auto dir = fs::path(GG_TEST_OUTPUT_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

/// Writes a 16-bit WAV of `channels` channels and `frames` frames at `rate` to `file`, creating
/// its folder.
void writeWav(const fs::path &file, uint32_t channels, uint32_t rate, int64_t frames)
{
    fs::create_directories(file.parent_path());
    gg::test::writeFile(file, gg::test::wavBytes(gg::test::makeTestAudio(channels, rate, frames),
                                                 SampleFormat::Int16));
}

/// The relative path loadSample records for `file` with `sampleFolder` as the sample folder.
std::string relativeOf(const fs::path &file, const fs::path &sampleFolder)
{
    const auto loaded = loadSample(file, MachineSettings{utf8(sampleFolder), ""});
    INFO(loaded.error);
    REQUIRE(loaded.decoded != nullptr);
    return loaded.entry.relativePath;
}
} // namespace

TEST_CASE("loadSample fills the entry from the file", "[media]")
{
    const auto dir = freshDir("sample-entry");
    // A non-ASCII name: the entry's paths are UTF-8.
    const auto file = dir / fs::path(u8"kick-é音.wav");
    writeWav(file, 2, 44100, 1234);

    const auto loaded = loadSample(file, MachineSettings{});
    INFO(loaded.error);
    REQUIRE(loaded.error.empty());
    REQUIRE(loaded.decoded != nullptr);
    REQUIRE(loaded.decoded->frames() == 1234);
    REQUIRE(loaded.decoded->channels() == 2);
    REQUIRE(loaded.decoded->sampleRate() == 44100.0);

    const auto hash = gg::media::hashFile(file);
    REQUIRE(hash.has_value());
    const auto &e = loaded.entry;
    REQUIRE(gg::core::isValidMediaEntry(e));
    REQUIRE(e.sha256Hex == hash->sha256Hex);
    REQUIRE(e.fileSize == hash->size);
    REQUIRE(e.fileSize == fs::file_size(file));
    REQUIRE(e.absolutePath == utf8(fs::absolute(file).lexically_normal()));
    REQUIRE(e.relativePath.empty());
    REQUIRE(e.format == "wav");
    REQUIRE(e.channels == 2);
    REQUIRE(e.sampleRate == 44100.0);
    REQUIRE(e.frames == 1234);
    REQUIRE_FALSE(e.embed);
}

TEST_CASE("loadSample takes a fingerprint it is given instead of hashing the file", "[media]")
{
    // A restore passes the fingerprint findSample just checked, so the file isn't read a third
    // time. A given one that isn't the file's shows it was taken as it is.
    const auto dir = freshDir("sample-known-hash");
    const auto file = dir / "known.wav";
    writeWav(file, 1, 48000, 100);
    const gg::media::FileHash given{std::string(64, 'a'), 12345};
    const auto loaded = loadSample(file, MachineSettings{}, given);
    INFO(loaded.error);
    REQUIRE(loaded.error.empty());
    REQUIRE(loaded.decoded != nullptr);
    CHECK(loaded.entry.sha256Hex == given.sha256Hex);
    CHECK(loaded.entry.fileSize == given.size);
    CHECK(loaded.decoded->frames() == 100);
}

TEST_CASE("loadSample records the channels the file stores, not the two it keeps", "[media]")
{
    const auto dir = freshDir("sample-channels");
    const auto file = dir / "quad.wav";
    writeWav(file, 4, 48000, 100);
    const auto loaded = loadSample(file, MachineSettings{});
    REQUIRE(loaded.decoded != nullptr);
    REQUIRE(loaded.decoded->channels() == 2);
    REQUIRE(loaded.entry.channels == 4);
    REQUIRE(loaded.entry.sampleRate == 48000.0);
    REQUIRE(loaded.entry.frames == 100);
}

TEST_CASE("loadSample records the absolute path lexically normal", "[media]")
{
    const auto dir = freshDir("sample-absolute");
    writeWav(dir / "a" / "kick.wav", 1, 48000, 10);
    // An absolute path with `.` and `..` in it comes back normal.
    const auto loaded = loadSample(dir / "a" / "." / ".." / "a" / "kick.wav", MachineSettings{});
    REQUIRE(loaded.decoded != nullptr);
    // Normal form on each platform: on Windows that is backslashes throughout, though
    // GG_TEST_OUTPUT_DIR comes with forward slashes.
    REQUIRE(loaded.entry.absolutePath == utf8((dir / "a" / "kick.wav").lexically_normal()));
}

TEST_CASE("loadSample sets relativePath only inside the sample folder", "[media]")
{
    const auto dir = freshDir("sample-relative");
    const auto lib = dir / "lib";
    writeWav(lib / "kick.wav", 1, 48000, 10);
    writeWav(lib / "drums" / "deep" / "snare.wav", 1, 48000, 10);
    writeWav(dir / "lib2" / "hat.wav", 1, 48000, 10); // a sibling sharing the folder's prefix
    writeWav(dir / "other.wav", 1, 48000, 10);

    REQUIRE(relativeOf(lib / "kick.wav", lib) == "kick.wav");
    // `/` separators, on every platform.
    REQUIRE(relativeOf(lib / "drums" / "deep" / "snare.wav", lib) == "drums/deep/snare.wav");
    // A folder given with a trailing separator, or with `..` in it.
    REQUIRE(relativeOf(lib / "kick.wav", fs::path(utf8(lib) + "/")) == "kick.wav");
    REQUIRE(relativeOf(lib / "kick.wav", lib / "drums" / "..") == "kick.wav");

    REQUIRE(relativeOf(dir / "lib2" / "hat.wav", lib).empty());
    REQUIRE(relativeOf(dir / "other.wav", lib).empty());
    REQUIRE(relativeOf(lib / "kick.wav", lib / "drums").empty());
    // No sample folder, a sample folder that doesn't exist.
    REQUIRE(relativeOf(lib / "kick.wav", fs::path()).empty());
    REQUIRE(relativeOf(lib / "kick.wav", dir / "nowhere").empty());
}

#if !defined(_WIN32)
TEST_CASE("loadSample sees through symlinks to the sample folder, as macOS's /var is", "[media]")
{
    // On macOS, /var is a symlink to /private/var, so a temporary folder has two spellings. A
    // symlink of our own makes the same case in the test's folder, on every POSIX system.
    const auto dir = freshDir("sample-symlink");
    const auto real = dir / "private" / "var" / "lib";
    writeWav(real / "sub" / "kick.wav", 1, 48000, 10);
    fs::create_directory_symlink(dir / "private" / "var", dir / "var");
    const auto viaLink = dir / "var" / "lib";

    // The folder through the link, the file by its real path, and the other way round.
    REQUIRE(relativeOf(real / "sub" / "kick.wav", viaLink) == "sub/kick.wav");
    REQUIRE(relativeOf(viaLink / "sub" / "kick.wav", real) == "sub/kick.wav");
    // The entry's absolute path keeps the spelling the file was given.
    const auto loaded = loadSample(viaLink / "sub" / "kick.wav", MachineSettings{});
    REQUIRE(loaded.entry.absolutePath == utf8(viaLink / "sub" / "kick.wav"));
}
#endif

TEST_CASE("loadSample of a file it can't decode gives an error and no audio", "[media]")
{
    const auto dir = freshDir("sample-error");
    const auto garbage = dir / "garbage.wav";
    const std::vector<uint8_t> bytes(4096, uint8_t{0x5a});
    gg::test::writeFile(garbage, bytes);

    for (const auto &file : {garbage, dir / "missing.wav", dir})
    {
        INFO(gg::test::utf8(file));
        const auto loaded = loadSample(file, MachineSettings{utf8(dir), ""});
        REQUIRE(loaded.decoded == nullptr);
        REQUIRE_FALSE(loaded.error.empty());
        REQUIRE(loaded.entry == gg::core::MediaEntry{});
    }
}

TEST_CASE("prepare at the file's rate keeps the decoded buffer, bit for bit", "[media]")
{
    const auto dir = freshDir("sample-prepare-same-rate");
    writeWav(dir / "kick.wav", 2, 48000, 480);
    const auto loaded = loadSample(dir / "kick.wav", MachineSettings{});
    REQUIRE(loaded.decoded != nullptr);
    const std::vector<float> left(loaded.decoded->channel(0),
                                  loaded.decoded->channel(0) + loaded.decoded->frames());

    const auto source = gg::media::prepare(loaded.decoded, 48000.0);
    REQUIRE(source != nullptr);
    REQUIRE(&source->buffer() == loaded.decoded.get()); // the same buffer, not a copy
    REQUIRE(std::memcmp(source->buffer().channel(0), left.data(), left.size() * sizeof(float)) ==
            0);
}

TEST_CASE("prepare converts 44.1 kHz to 48 kHz into a stereo source with its pairs", "[media]")
{
    const auto dir = freshDir("sample-prepare-convert");
    writeWav(dir / "kick.wav", 2, 44100, 4410);
    const auto loaded = loadSample(dir / "kick.wav", MachineSettings{});
    REQUIRE(loaded.decoded != nullptr);

    const auto source = gg::media::prepare(loaded.decoded, 48000.0);
    REQUIRE(source != nullptr);
    const gg::SampleBuffer &buffer = source->buffer();
    REQUIRE(buffer.sampleRate() == 48000.0);
    REQUIRE(buffer.channels() == 2);
    REQUIRE(buffer.frames() == 4800); // llround(4410 × 48000 / 44100)
    // The decoded buffer is left as it was, at the file's rate.
    REQUIRE(loaded.decoded->sampleRate() == 44100.0);
    REQUIRE(loaded.decoded->frames() == 4410);

    // The interleaved copy the SIMD kernel reads: channels 0 and 1, frame by frame.
    const auto &pairs = source->pairs();
    REQUIRE(pairs.data() != nullptr);
    REQUIRE(pairs.frames() == buffer.frames());
    for (int64_t n = 0; n < buffer.frames(); ++n)
    {
        INFO("frame " << n);
        REQUIRE(std::memcmp(&pairs.data()[2 * n], &buffer.channel(0)[n], sizeof(float)) == 0);
        REQUIRE(std::memcmp(&pairs.data()[2 * n + 1], &buffer.channel(1)[n], sizeof(float)) == 0);
    }
}

TEST_CASE("prepare refuses no buffer and a rate that isn't finite and positive", "[media]")
{
    const auto buffer = std::make_shared<gg::SampleBuffer>(2, 16, 48000.0);
    REQUIRE_THROWS_AS(gg::media::prepare(nullptr, 48000.0), std::invalid_argument);
    for (const double rate :
         {0.0, -48000.0, std::numeric_limits<double>::quiet_NaN(),
          std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
    {
        INFO("rate " << rate);
        REQUIRE_THROWS_AS(gg::media::prepare(buffer, rate), std::invalid_argument);
    }
}
