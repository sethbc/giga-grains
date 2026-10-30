// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// collectMedia (gg/media/Collect.h, spec §6.7): the files a state references, gathered into one
// folder. The files are plain bytes: collecting never decodes. Every test works in a folder of its
// own under GG_TEST_OUTPUT_DIR.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "TestHooks.h"
#include "gg/core/MachineSettings.h"
#include "gg/core/SessionState.h"
#include "gg/media/Collect.h"
#include "gg/media/Sha256.h"
#include "media/AudioFixtures.h"
#include "support/Utf8.h"

namespace
{
namespace fs = std::filesystem;
using gg::core::MachineSettings;
using gg::core::MediaEntry;
using gg::core::RingEntry;
using gg::core::SessionState;
using gg::test::utf8;

fs::path freshDir(const char *name)
{
    const auto dir = fs::path(GG_TEST_OUTPUT_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void put(const fs::path &file, std::string_view text)
{
    fs::create_directories(file.parent_path());
    gg::test::writeFile(file,
                        std::span(reinterpret_cast<const uint8_t *>(text.data()), text.size()));
}

std::string slurp(const fs::path &file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string hashOf(std::string_view text)
{
    return gg::media::sha256Hex(std::as_bytes(std::span(text.data(), text.size())));
}

/// A collected entry's path as collectMedia saves it: the copy's path in its normal form, as the
/// loader saves a path (so with `\` separators on Windows, wherever the folder's path had `/`).
std::string savedPath(const fs::path &path) { return utf8(path.lexically_normal()); }

MediaEntry sampleEntry(const fs::path &path, std::string_view content, uint8_t layer)
{
    MediaEntry e;
    e.sha256Hex = hashOf(content);
    e.fileSize = content.size();
    e.absolutePath = utf8(path);
    e.relativePath = "old/relative.wav";
    e.format = "wav";
    e.channels = 2;
    e.sampleRate = 48000.0;
    e.frames = 100;
    e.layer = layer;
    return e;
}

RingEntry ringEntry(const fs::path &path, std::string_view content, uint8_t layer)
{
    RingEntry e;
    e.layer = layer;
    e.frames = 100;
    e.sampleRate = 48000.0;
    e.sha256Hex = hashOf(content);
    e.fileSize = content.size();
    e.absolutePath = utf8(path);
    return e;
}
} // namespace

TEST_CASE("collectMedia copies the files it finds and points the entries at them", "[media]")
{
    const auto dir = freshDir("collect-copies");
    const auto out = dir / "project" / "media";
    put(dir / "a" / "kick.wav", "kick bytes");
    put(dir / "lib" / "deep" / "snare.wav", "snare bytes, a different size");
    const auto captures = dir / "Captures";
    put(captures / "ring-file.wav", "ring wav bytes");

    SessionState state;
    state.samples[0] = sampleEntry(dir / "a" / "kick.wav", "kick bytes", 0);
    // Moved: only the sample folder's scan finds it.
    state.samples[1] = sampleEntry(dir / "gone" / "snare.wav", "snare bytes, a different size", 1);
    // Filed ring, found in the captures folder.
    state.rings[2] = ringEntry(dir / "elsewhere" / "ring-file.wav", "ring wav bytes", 2);
    MachineSettings settings{utf8(dir / "lib"), ""};
    settings.capturesFolder = utf8(captures);

    const SessionState before = state;
    const auto result = gg::media::collectMedia(state, out, settings, {});
    REQUIRE(state.samples == before.samples); // the input is untouched
    REQUIRE(state.rings == before.rings);
    REQUIRE(result.missing.empty());
    REQUIRE(result.copied.size() == 3);

    REQUIRE(slurp(out / "kick.wav") == "kick bytes");
    REQUIRE(slurp(out / "snare.wav") == "snare bytes, a different size");
    REQUIRE(slurp(out / "ring-file.wav") == "ring wav bytes");

    REQUIRE(result.state.samples[0]->absolutePath == savedPath(out / "kick.wav"));
    REQUIRE(result.state.samples[0]->relativePath == "kick.wav");
    REQUIRE(result.state.samples[1]->absolutePath == savedPath(out / "snare.wav"));
    REQUIRE(result.state.rings[2]->absolutePath == savedPath(out / "ring-file.wav"));
    // Everything else about the entries is as it was.
    auto expected = *state.samples[0];
    expected.absolutePath = savedPath(out / "kick.wav");
    expected.relativePath = "kick.wav";
    REQUIRE(*result.state.samples[0] == expected);

    // The collected state finds its files at the saved paths.
    auto again = gg::media::collectMedia(result.state, out, settings, {});
    REQUIRE(again.missing.empty());
    REQUIRE(again.copied.empty());
    REQUIRE(again.state.samples == result.state.samples);
    REQUIRE(again.state.rings == result.state.rings);
}

TEST_CASE("collectMedia renames a file whose name clashes with other content", "[media]")
{
    const auto dir = freshDir("collect-clash");
    const auto out = dir / "out";
    put(dir / "a" / "kick.wav", "first kick");
    put(dir / "b" / "kick.wav", "second kick, longer");
    put(out / "snare.wav", "somebody else's snare");
    put(dir / "c" / "snare.wav", "my snare");

    SessionState state;
    state.samples[0] = sampleEntry(dir / "a" / "kick.wav", "first kick", 0);
    state.samples[1] = sampleEntry(dir / "b" / "kick.wav", "second kick, longer", 1);
    state.samples[2] = sampleEntry(dir / "c" / "snare.wav", "my snare", 2);
    // The same file again in layer 3: collected once.
    state.samples[3] = sampleEntry(dir / "a" / "kick.wav", "first kick", 3);

    const auto result = gg::media::collectMedia(state, out, MachineSettings{}, {});
    REQUIRE(result.missing.empty());
    REQUIRE(result.copied.size() == 3);
    REQUIRE(result.state.samples[0]->absolutePath == savedPath(out / "kick.wav"));
    REQUIRE(result.state.samples[3]->absolutePath == savedPath(out / "kick.wav"));
    const auto second = hashOf("second kick, longer").substr(0, 8);
    const auto snare = hashOf("my snare").substr(0, 8);
    REQUIRE(result.state.samples[1]->absolutePath == savedPath(out / ("kick-" + second + ".wav")));
    REQUIRE(result.state.samples[2]->absolutePath == savedPath(out / ("snare-" + snare + ".wav")));
    REQUIRE(slurp(out / "kick.wav") == "first kick");
    REQUIRE(slurp(out / ("kick-" + second + ".wav")) == "second kick, longer");
    REQUIRE(slurp(out / "snare.wav") == "somebody else's snare"); // never overwritten
    REQUIRE(slurp(out / ("snare-" + snare + ".wav")) == "my snare");
    REQUIRE(result.state.samples[1]->relativePath == "kick-" + second + ".wav");
}

TEST_CASE("collectMedia saves the copy's path in its normal form", "[media]")
{
    // The folder is named with "." and ".." components, and the file has a non-ASCII name: the
    // saved path is the copy's path normalized, as the loader saves one, on every platform (on
    // Windows that also makes its separators `\`), and it names the copy.
    const auto dir = freshDir("collect-normal");
    const auto out = dir / "x" / ".." / "." / "out";
    const fs::path name(u8"café été.wav");
    put(dir / "a" / name, "kick bytes");

    SessionState state;
    state.samples[0] = sampleEntry(dir / "a" / name, "kick bytes", 0);
    const auto result = gg::media::collectMedia(state, out, MachineSettings{}, {});
    REQUIRE(result.missing.empty());
    REQUIRE(result.copied == std::vector<std::string>{savedPath(dir / "out" / name)});
    const std::string saved = result.state.samples[0]->absolutePath;
    REQUIRE(saved == savedPath(dir / "out" / name));
    REQUIRE(saved != utf8(out / name)); // not the folder's path as given
    REQUIRE(gg::test::pathOf(saved) == (dir / "out" / name).lexically_normal());
    REQUIRE(result.state.samples[0]->relativePath == utf8(name));
    REQUIRE(slurp(gg::test::pathOf(saved)) == "kick bytes");
}

TEST_CASE("collectMedia lists what it can't find and leaves embedded entries alone", "[media]")
{
    const auto dir = freshDir("collect-missing");
    const auto out = dir / "out";
    put(dir / "a" / "kick.wav", "kick bytes");
    put(dir / "b" / "changed.wav", "other content than saved");

    SessionState state;
    state.samples[0] = sampleEntry(dir / "a" / "kick.wav", "kick bytes", 0);
    state.samples[1] = sampleEntry(dir / "nowhere" / "lost.wav", "lost bytes", 1);
    state.samples[2] = sampleEntry(dir / "b" / "changed.wav", "the saved content", 2);
    MediaEntry embedded = sampleEntry({}, "embedded", 3);
    embedded.absolutePath.clear();
    embedded.relativePath.clear();
    embedded.embed = true;
    embedded.audioId = 1;
    state.samples[3] = embedded;
    state.audio.push_back({1, "embedded"});
    state.rings[0] = ringEntry(dir / "nowhere" / "ring.wav", "ring bytes", 0);

    const auto result = gg::media::collectMedia(state, out, MachineSettings{}, {});
    REQUIRE(result.copied.size() == 1);
    REQUIRE(result.missing.size() == 3);
    REQUIRE(std::find(result.missing.begin(), result.missing.end(),
                      utf8(dir / "nowhere" / "lost.wav")) != result.missing.end());
    REQUIRE(std::find(result.missing.begin(), result.missing.end(),
                      utf8(dir / "b" / "changed.wav")) != result.missing.end());
    REQUIRE(std::find(result.missing.begin(), result.missing.end(),
                      utf8(dir / "nowhere" / "ring.wav")) != result.missing.end());
    // A missing entry stays as it was; so does the embedded one and its payload.
    REQUIRE(result.state.samples[1] == state.samples[1]);
    REQUIRE(result.state.samples[2] == state.samples[2]);
    REQUIRE(result.state.rings[0] == state.rings[0]);
    REQUIRE(result.state.samples[3] == state.samples[3]);
    REQUIRE(result.state.audio == state.audio);
    REQUIRE_FALSE(fs::exists(out / "lost.wav"));
}

TEST_CASE("collectMedia finds a sample through a content root", "[media]")
{
    const auto dir = freshDir("collect-content-root");
    const auto out = dir / "out";
    put(dir / "root" / "packs" / "kick.wav", "pack kick");
    SessionState state;
    state.samples[0] = sampleEntry(dir / "gone" / "kick.wav", "pack kick", 0);
    state.samples[0]->contentPath = "packs/kick.wav";

    const std::vector<fs::path> roots{dir / "root"};
    const auto result = gg::media::collectMedia(state, out, MachineSettings{}, roots);
    REQUIRE(result.missing.empty());
    REQUIRE(slurp(out / "kick.wav") == "pack kick");
    REQUIRE(result.state.samples[0]->contentPath == "packs/kick.wav");
}

TEST_CASE("collectMedia verifies each copy's hash and drops one that fails", "[media]")
{
    const auto dir = freshDir("collect-verify");
    const auto out = dir / "out";
    put(dir / "a" / "kick.wav", "kick bytes");

    SessionState state;
    state.samples[0] = sampleEntry(dir / "a" / "kick.wav", "kick bytes", 0);
    // The same file in a second layer: its failure is listed once.
    state.samples[1] = sampleEntry(dir / "a" / "kick.wav", "kick bytes", 1);
    const SessionState before = state;

    // A disk that writes other bytes than it was given.
    gg::media::detail::setCollectHook([](const fs::path &copy) { put(copy, "kick bytez"); });
    const auto result = gg::media::collectMedia(state, out, MachineSettings{}, {});
    gg::media::detail::setCollectHook(nullptr);

    REQUIRE(result.copied.empty());
    REQUIRE(result.missing == std::vector<std::string>{utf8(dir / "a" / "kick.wav")});
    REQUIRE_FALSE(fs::exists(out / "kick.wav"));
    REQUIRE(result.state.samples == before.samples);
    REQUIRE(state.samples == before.samples);
}
