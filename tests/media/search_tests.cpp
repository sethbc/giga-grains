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
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/MachineSettings.h"
#include "gg/core/SessionState.h"
#include "gg/media/Search.h"
#include "TestHooks.h"
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

TEST_CASE("findSample's Changed path is normalized as a Found one is", "[media]")
{
    // The saved path has `.` and `..` components: a match is reported normalized, and so is a
    // file of other content at that path.
    const auto dir = freshDir("search-changed-normal");
    fs::create_directories(dir / "old" / "sub");
    const fs::path saved = dir / "old" / "." / "sub" / ".." / "kick.wav";
    put(dir / "old" / "kick.wav", kSameSize);
    const auto r = findSample(entryFor(saved), MachineSettings{});
    REQUIRE(r.outcome == SearchOutcome::Changed);
    REQUIRE(r.path == dir / "old" / "kick.wav");
    REQUIRE(r.path == saved.lexically_normal());
    REQUIRE_FALSE(r.moved);

    put(dir / "old" / "kick.wav", kContent);
    requireFound(findSample(entryFor(saved), MachineSettings{}), dir / "old" / "kick.wav", false);
}

namespace
{
// "café.wav" in Unicode's two normal forms: NFC composes é (U+00E9); NFD, as macOS's HFS+ stores
// names, spells it e and a combining acute accent (U+0065 U+0301).
const std::string kNfcName = "caf\xc3\xa9.wav";
const std::string kNfdName = "cafe\xcc\x81.wav";

fs::path pathOf(const std::string &text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

/// Found at the file `stored` names, by whichever spelling: a filesystem that ignores the
/// normal form (APFS, HFS+) opens either, one that doesn't (ext4, NTFS) only the stored one.
void requireFoundAt(const gg::media::SearchResult &r, const fs::path &stored, const fs::path &saved)
{
    INFO("path: " << r.path.string());
    REQUIRE(r.outcome == SearchOutcome::Found);
    REQUIRE(r.path.parent_path() == stored.parent_path());
    REQUIRE(fs::equivalent(r.path, stored));
    REQUIRE(r.moved == (r.path != saved));
}
} // namespace

TEST_CASE("findSample matches a name saved in NFC to its NFD spelling, and back", "[media]")
{
    for (const auto &[savedName, storedName] :
         {std::pair{kNfcName, kNfdName}, std::pair{kNfdName, kNfcName}})
    {
        INFO("saved " << (savedName == kNfcName ? "NFC" : "NFD") << ", stored "
                      << (storedName == kNfcName ? "NFC" : "NFD"));
        const auto dir = freshDir("search-unicode");
        const auto saved = dir / "old" / pathOf(savedName);
        const auto entry = entryFor(saved, "drums/" + savedName);

        // The scan lists the stored name, and compares it to the saved one.
        const auto lib = dir / "lib";
        const auto scanned = lib / "deep" / pathOf(storedName);
        put(scanned, kContent);
        requireFound(findSample(entry, MachineSettings{utf8(lib), ""}), scanned, true);
        fs::remove(scanned);

        // The last folder loaded from, the relative path and the saved path open the saved
        // spelling, and then the other.
        const auto last = dir / "last" / pathOf(storedName);
        put(last, kContent);
        requireFoundAt(findSample(entry, MachineSettings{"", utf8(dir / "last")}), last, saved);
        fs::remove(last);

        const auto relative = lib / "drums" / pathOf(storedName);
        put(relative, kContent);
        requireFoundAt(findSample(entry, MachineSettings{utf8(lib), ""}), relative, saved);
        fs::remove(relative);

        const auto atSaved = dir / "old" / pathOf(storedName);
        put(atSaved, kContent);
        requireFoundAt(findSample(entry, MachineSettings{}), atSaved, saved);

        // At the saved path in the other spelling, with other content: Changed, there.
        put(atSaved, kSameSize);
        const auto r = findSample(entry, MachineSettings{});
        REQUIRE(r.outcome == SearchOutcome::Changed);
        REQUIRE(fs::equivalent(r.path, atSaved));
    }
}

TEST_CASE("findSample's match by name is canonical equivalence, nothing looser", "[media]")
{
    // Without its accent, or in another case, the name is another name.
    const auto dir = freshDir("search-unicode-other");
    const auto lib = dir / "lib";
    put(lib / "cafe.wav", kContent);
    put(lib / "CAF\xc3\x89.wav", kContent);
    requireMissing(
        findSample(entryFor(dir / "old" / pathOf(kNfcName)), MachineSettings{utf8(lib), ""}));
}

#if defined(__linux__)
TEST_CASE("findSample's scan matches a name that isn't UTF-8 byte for byte", "[media]")
{
    // Linux names are bytes: one that isn't UTF-8 has no normal form, and matches only itself.
    const auto dir = freshDir("search-not-utf8");
    const auto lib = dir / "lib";
    put(lib / "deep" / "caf\xe9.wav", kContent); // Latin-1's é
    put(lib / "deep" / "caf\xe8.wav", kContent);
    MediaEntry entry = entryFor(dir);
    entry.absolutePath = utf8(dir / "old") + "/caf\xe9.wav";
    requireFound(findSample(entry, MachineSettings{utf8(lib), ""}), lib / "deep" / "caf\xe9.wav",
                 true);
}
#endif

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

    // Unlocked again however the test ends, a failed REQUIRE or SKIP included: the guard exists
    // before the lock does.
    struct Unlock
    {
        fs::path folder;
        ~Unlock()
        {
            std::error_code e;
            fs::permissions(folder, fs::perms::owner_all, e);
        }
    } unlock{locked};
    fs::permissions(locked, fs::perms::none);
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

namespace
{
using gg::media::detail::ScanStep;

/// The fault the scan hook gives: at `step` in `folder`, `error`, or (`remove`) the folder
/// removed and no error, so the scan meets a folder that has gone.
struct ScanFault
{
    ScanStep step{ScanStep::Open};
    fs::path folder;
    std::errc error{};
    bool remove{false};
    int hits{0};
};
ScanFault gScanFault;

std::error_code scanFaultHook(ScanStep step, const fs::path &folder)
{
    if (step != gScanFault.step || folder != gScanFault.folder)
    {
        return {};
    }
    ++gScanFault.hits;
    if (gScanFault.remove)
    {
        std::error_code ignored;
        fs::remove_all(folder, ignored);
        return {};
    }
    return std::make_error_code(gScanFault.error);
}

/// Sets the scan hook for its lifetime.
struct ScanHookSet
{
    explicit ScanHookSet(const ScanFault &fault)
    {
        gScanFault = fault;
        gg::media::detail::setScanHook(scanFaultHook);
    }
    ~ScanHookSet() { gg::media::detail::setScanHook(nullptr); }
    ScanHookSet(const ScanHookSet &) = delete;
    ScanHookSet &operator=(const ScanHookSet &) = delete;
};

/// Directory order is the filesystem's: lists `folder` as the scan does.
std::vector<fs::path> listed(const fs::path &folder)
{
    std::vector<fs::path> order;
    for (const auto &e : fs::directory_iterator(folder))
    {
        order.push_back(e.path());
    }
    return order;
}
} // namespace

TEST_CASE("findSample's scan skips a folder that fails to open or to list, and goes on", "[media]")
{
    // Eight folders of eight empty files. The sample is in the folder listed first, as the file
    // listed last there, and again in the folder listed last under the same name. Whatever the
    // fault in the first folder, before the sample is reached there, the scan finds the copy.
    const auto dir = freshDir("search-scan-faults");
    const auto lib = dir / "lib";
    for (int d = 0; d < 8; ++d)
    {
        for (int f = 0; f < 8; ++f)
        {
            put(lib / ("d" + std::to_string(d)) / ("n" + std::to_string(f)), "");
        }
    }
    const auto folders = listed(lib);
    REQUIRE(folders.size() == 8);
    const fs::path first = folders.front();
    const fs::path name = listed(first).back().filename();
    put(first / name, kContent);
    put(folders.back() / name, kContent);
    const auto entry = entryFor(dir / "old" / name);
    const MachineSettings settings{utf8(lib), ""};

    // With no fault the scan finds the first folder's.
    requireFound(findSample(entry, settings), first / name, true);

    ScanFault fault;
    fault.folder = first;
    SECTION("EPERM opening it") { fault.error = std::errc::operation_not_permitted; }
    SECTION("EACCES opening it") { fault.error = std::errc::permission_denied; }
    SECTION("EIO opening it") { fault.error = std::errc::io_error; }
    SECTION("EIO after its first entry")
    {
        fault.step = ScanStep::Next;
        fault.error = std::errc::io_error;
    }
    SECTION("removed before it is opened") { fault.remove = true; }
    SECTION("removed while it is listed")
    {
        fault.step = ScanStep::Next;
        fault.remove = true;
    }
    const ScanHookSet hook(fault);
    requireFound(findSample(entry, settings), folders.back() / name, true);
    REQUIRE(gScanFault.hits >= 1);
}

TEST_CASE("findSample's scan of a sample folder that can't be opened finds nothing", "[media]")
{
    const auto dir = freshDir("search-scan-root-fault");
    const auto lib = dir / "lib";
    put(lib / "kick.wav", kContent);
    ScanFault fault;
    fault.folder = lib;
    fault.error = std::errc::io_error;
    const ScanHookSet hook(fault);
    requireMissing(findSample(entryFor(dir / "old" / "kick.wav"), MachineSettings{utf8(lib), ""}));
    REQUIRE(gScanFault.hits == 1);
}

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
