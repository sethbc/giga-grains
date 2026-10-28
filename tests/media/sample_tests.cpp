// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// loadSample (gg/media/Sample.h): the decoded audio and the manifest entry of a file. Every test
// works in a folder of its own under GG_TEST_OUTPUT_DIR.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/MachineSettings.h"
#include "gg/core/SessionState.h"
#include "gg/media/Sample.h"
#include "gg/media/Sha256.h"
#include "media/AudioFixtures.h"

namespace
{
namespace fs = std::filesystem;
using gg::core::MachineSettings;
using gg::media::loadSample;
using gg::test::SampleFormat;

/// An empty folder of the test's own.
fs::path freshDir(const char *name)
{
    const auto dir = fs::path(GG_TEST_OUTPUT_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string utf8(const fs::path &path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
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
    const auto dir = freshDir("search-sample-entry");
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

TEST_CASE("loadSample records the channels the file stores, not the two it keeps", "[media]")
{
    const auto dir = freshDir("search-sample-channels");
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
    const auto dir = freshDir("search-sample-absolute");
    writeWav(dir / "a" / "kick.wav", 1, 48000, 10);
    // An absolute path with `.` and `..` in it comes back normal.
    const auto loaded = loadSample(dir / "a" / "." / ".." / "a" / "kick.wav", MachineSettings{});
    REQUIRE(loaded.decoded != nullptr);
    REQUIRE(loaded.entry.absolutePath == utf8(dir / "a" / "kick.wav"));
}

TEST_CASE("loadSample sets relativePath only inside the sample folder", "[media]")
{
    const auto dir = freshDir("search-sample-relative");
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
    const auto dir = freshDir("search-sample-symlink");
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
    const auto dir = freshDir("search-sample-error");
    const auto garbage = dir / "garbage.wav";
    const std::vector<uint8_t> bytes(4096, uint8_t{0x5a});
    gg::test::writeFile(garbage, bytes);

    for (const auto &file : {garbage, dir / "missing.wav", dir})
    {
        INFO(file.string());
        const auto loaded = loadSample(file, MachineSettings{utf8(dir), ""});
        REQUIRE(loaded.decoded == nullptr);
        REQUIRE_FALSE(loaded.error.empty());
        REQUIRE(loaded.entry == gg::core::MediaEntry{});
    }
}
