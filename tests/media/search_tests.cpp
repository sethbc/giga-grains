// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// findSample (gg/media/Search.h): the search for a manifest's sample when a state loads
// (spec §6.7). Each search step is shown finding the file when only it can, and every non-match
// is shown rejected. The files are plain bytes: the search never decodes. Every test works in a
// folder of its own under GG_TEST_OUTPUT_DIR.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/MachineSettings.h"
#include "gg/core/SessionState.h"
#include "gg/media/Search.h"
#include "gg/media/Sha256.h"
#include "media/AudioFixtures.h"

namespace
{
namespace fs = std::filesystem;
using gg::core::MachineSettings;
using gg::core::MediaEntry;
using gg::media::findSample;
using gg::media::SearchOutcome;

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

/// Writes `text` to `file`, creating its folder.
void put(const fs::path &file, std::string_view text)
{
    fs::create_directories(file.parent_path());
    gg::test::writeFile(file,
                        std::span(reinterpret_cast<const uint8_t *>(text.data()), text.size()));
}

constexpr std::string_view kContent = "the sample's bytes";
constexpr std::string_view kOther = "other bytes, longer than the sample's"; // another size
constexpr std::string_view kSameSize = "the sample'S bytes";                 // its size, not it
static_assert(kSameSize.size() == kContent.size() && kSameSize != kContent);

/// The entry of a file of kContent saved at `path` (which needn't exist now), with `relative`.
MediaEntry entryFor(const fs::path &path, const std::string &relative = {})
{
    const auto bytes = std::as_bytes(std::span(kContent.data(), kContent.size()));
    MediaEntry e;
    e.sha256Hex = gg::media::sha256Hex(bytes);
    e.fileSize = kContent.size();
    e.absolutePath = utf8(path);
    e.relativePath = relative;
    e.format = "wav";
    e.channels = 2;
    e.sampleRate = 48000.0;
    e.frames = 100;
    return e;
}

void requireFound(const gg::media::SearchResult &r, const fs::path &where, bool moved)
{
    INFO("path: " << r.path.string());
    REQUIRE(r.outcome == SearchOutcome::Found);
    REQUIRE(r.path == where);
    REQUIRE(r.moved == moved);
}

void requireMissing(const gg::media::SearchResult &r)
{
    INFO("path: " << r.path.string());
    REQUIRE(r.outcome == SearchOutcome::Missing);
    REQUIRE(r.path.empty());
    REQUIRE_FALSE(r.moved);
}
} // namespace

TEST_CASE("findSample finds the file at its saved path", "[media]")
{
    const auto dir = freshDir("search-absolute");
    const auto file = dir / "old" / "kick.wav";
    put(file, kContent);
    requireFound(findSample(entryFor(file), MachineSettings{}), file, false);
    // With folders set too, the saved path still comes first.
    put(dir / "last" / "kick.wav", kContent);
    requireFound(findSample(entryFor(file), MachineSettings{utf8(dir), utf8(dir / "last")}), file,
                 false);
}

TEST_CASE("findSample finds a file through the sample folder and its relative path", "[media]")
{
    // The file was renamed as well as moved, so no search by name can find it: only step 2.
    const auto dir = freshDir("search-relative");
    const auto lib = dir / "new-drive" / "Samples";
    put(lib / "drums" / "renamed.wav", kContent);
    const auto entry =
        entryFor(dir / "old-drive" / "Samples" / "drums" / "kick.wav", "drums/renamed.wav");

    requireFound(findSample(entry, MachineSettings{utf8(lib), ""}), lib / "drums" / "renamed.wav",
                 true);
    // Without the sample folder, or without the relative path, it isn't found.
    requireMissing(findSample(entry, MachineSettings{"", utf8(lib / "drums")}));
    requireMissing(findSample(entryFor(dir / "old-drive" / "Samples" / "drums" / "kick.wav"),
                              MachineSettings{utf8(lib), ""}));
}

TEST_CASE("findSample finds a file by name in the last folder loaded from", "[media]")
{
    const auto dir = freshDir("search-last-folder");
    put(dir / "last" / "kick.wav", kContent);
    const auto entry = entryFor(dir / "old" / "kick.wav");
    requireFound(findSample(entry, MachineSettings{"", utf8(dir / "last")}),
                 dir / "last" / "kick.wav", true);
    // Not recursively: the last folder is looked in, not under.
    requireMissing(findSample(entry, MachineSettings{"", utf8(dir)}));
}

TEST_CASE("findSample finds a file by name anywhere under the sample folder", "[media]")
{
    const auto dir = freshDir("search-scan");
    const auto lib = dir / "lib";
    put(lib / "a" / "b" / "c" / "kick.wav", kContent);
    put(lib / "a" / "snare.wav", kContent); // the content, under another name
    requireFound(findSample(entryFor(dir / "old" / "kick.wav"), MachineSettings{utf8(lib), ""}),
                 lib / "a" / "b" / "c" / "kick.wav", true);
    // The sample folder itself, as a file name: not the scan's job.
    requireMissing(findSample(entryFor(dir / "old" / "lib"), MachineSettings{utf8(lib), ""}));
}

TEST_CASE("findSample's steps go in order: saved path, relative path, last folder, scan", "[media]")
{
    const auto dir = freshDir("search-order");
    const auto lib = dir / "lib";
    const auto saved = dir / "old" / "kick.wav";
    const auto relative = lib / "kick.wav";
    const auto last = dir / "last" / "kick.wav";
    const auto scanned = lib / "deep" / "kick.wav";
    for (const auto &f : {saved, relative, last, scanned})
    {
        put(f, kContent);
    }
    const auto entry = entryFor(saved, "kick.wav");
    const MachineSettings settings{utf8(lib), utf8(last.parent_path())};

    requireFound(findSample(entry, settings), saved, false);
    fs::remove(saved);
    requireFound(findSample(entry, settings), relative, true);
    fs::remove(relative);
    requireFound(findSample(entry, settings), last, true);
    fs::remove(last);
    requireFound(findSample(entry, settings), scanned, true);
    fs::remove(scanned);
    requireMissing(findSample(entry, settings));
}

TEST_CASE("findSample takes no file with other content, by size or by hash", "[media]")
{
    const auto dir = freshDir("search-other-content");
    const auto lib = dir / "lib";
    const auto saved = dir / "old" / "kick.wav";
    const auto entry = entryFor(saved, "kick.wav");
    const MachineSettings settings{utf8(lib), utf8(dir / "last")};

    for (const auto content : {kOther, kSameSize})
    {
        INFO(content);
        // Same-named files with other content, wherever the search looks: no match.
        put(lib / "kick.wav", content);
        put(dir / "last" / "kick.wav", content);
        put(lib / "deep" / "kick.wav", content);
        requireMissing(findSample(entry, settings));

        // At the saved path too: the file changed.
        put(saved, content);
        const auto r = findSample(entry, settings);
        REQUIRE(r.outcome == SearchOutcome::Changed);
        REQUIRE(r.path == saved);
        REQUIRE_FALSE(r.moved);

        // A true copy elsewhere still wins over the changed file.
        put(lib / "deep" / "kick.wav", kContent);
        requireFound(findSample(entry, settings), lib / "deep" / "kick.wav", true);
        fs::remove(saved);
    }
}

TEST_CASE("findSample of a saved path that is a folder is missing, not changed", "[media]")
{
    const auto dir = freshDir("search-folder-at-path");
    fs::create_directories(dir / "kick.wav");
    requireMissing(findSample(entryFor(dir / "kick.wav"), MachineSettings{}));
}

TEST_CASE("findSample finds a file saved with Windows separators by its name", "[media]")
{
    // On Windows this is an absolute path on a drive that needn't exist; elsewhere it's one
    // relative file name, never looked up against the working directory. Either way the file is
    // found by its name, kick.wav.
    const auto dir = freshDir("search-windows-path");
    put(dir / "last" / "kick.wav", kContent);
    MediaEntry entry = entryFor(dir);
    entry.absolutePath = "Q:\\gg-nowhere\\Samples\\kick.wav";
    requireFound(findSample(entry, MachineSettings{"", utf8(dir / "last")}),
                 dir / "last" / "kick.wav", true);
}

#if !defined(_WIN32)
TEST_CASE("findSample's scan follows symlinked files but not symlinked folders", "[media]")
{
    const auto dir = freshDir("search-symlinks");
    const auto lib = dir / "lib";
    put(dir / "outside" / "kick.wav", kContent);
    fs::create_directories(lib);
    fs::create_directory_symlink(dir / "outside", lib / "linked-folder");
    const auto entry = entryFor(dir / "old" / "kick.wav");
    requireMissing(findSample(entry, MachineSettings{utf8(lib), ""}));

    fs::create_symlink(dir / "outside" / "kick.wav", lib / "kick.wav");
    requireFound(findSample(entry, MachineSettings{utf8(lib), ""}), lib / "kick.wav", true);
}
#endif

#if !defined(_WIN32)
TEST_CASE("findSample's scan skips a folder it can't open and goes on", "[media]")
{
    // Eight folders; the one listed first is locked (mode 000) and the sample is in the one
    // listed last, so the scan meets the locked folder before the sample, whatever the order.
    const auto dir = fs::path(GG_TEST_OUTPUT_DIR) / "search-locked-folder";
    const auto lib = dir / "lib";
    std::error_code ignored;
    for (int i = 0; i < 8; ++i) // unlock what a failed earlier run left, so it can be removed
    {
        fs::permissions(lib / ("d" + std::to_string(i)), fs::perms::owner_all, ignored);
    }
    freshDir("search-locked-folder");
    for (int i = 0; i < 8; ++i)
    {
        put(lib / ("d" + std::to_string(i)) / "inside" / "x.wav", kOther);
    }
    std::vector<fs::path> order;
    for (const auto &e : fs::directory_iterator(lib))
    {
        order.push_back(e.path());
    }
    REQUIRE(order.size() == 8);
    const auto locked = order.front();
    const auto target = order.back() / "kick.wav";
    put(target, kContent);

    fs::permissions(locked, fs::perms::none);
    struct Unlock
    {
        fs::path folder;
        ~Unlock()
        {
            std::error_code e;
            fs::permissions(folder, fs::perms::owner_all, e);
        }
    } unlock{locked};
    std::error_code openError;
    fs::directory_iterator probe(locked, openError);
    if (!openError)
    {
        SKIP("the locked folder still opens (running as root?)");
    }

    requireFound(findSample(entryFor(dir / "old" / "kick.wav"), MachineSettings{utf8(lib), ""}),
                 target, true);
}
#endif

TEST_CASE("findSample's scan stops after kMaxScanEntries entries", "[media][slow]")
{
    // Directory order is the filesystem's, so the test learns it: it fills the sample folder with
    // kMaxScanEntries + 1 empty files, lists them as the scan does, and puts the sample's content
    // in the file listed last (one past the limit: missing), then in the one listed at the limit
    // (found), each time naming the saved path after it.
    const auto dir = freshDir("search-scan-limit");
    const auto lib = dir / "lib";
    fs::create_directories(lib);
    constexpr std::size_t kFiles = gg::media::kMaxScanEntries + 1;
    for (std::size_t i = 0; i < kFiles; ++i)
    {
        std::ofstream(lib / ("f" + std::to_string(i)), std::ios::binary);
    }
    std::vector<fs::path> order;
    for (const auto &e : fs::directory_iterator(lib))
    {
        order.push_back(e.path());
    }
    REQUIRE(order.size() == kFiles);
    const auto pastLimit = order.back();
    const auto atLimit = order[gg::media::kMaxScanEntries - 1];

    const MachineSettings settings{utf8(lib), ""};
    put(pastLimit, kContent);
    requireMissing(findSample(entryFor(dir / "old" / pastLimit.filename()), settings));

    put(pastLimit, "");
    put(atLimit, kContent);
    requireFound(findSample(entryFor(dir / "old" / atLimit.filename()), settings), atLimit, true);
}
