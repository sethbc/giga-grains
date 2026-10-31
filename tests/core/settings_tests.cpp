// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The per-machine settings (spec §6.7): gg-core's XML codec (gg/core/MachineSettings.h), and
// gg-media's settings file (gg/media/SettingsFile.h), tagged [media]. Every file test works in a
// folder of its own under GG_TEST_OUTPUT_DIR and never touches the user's settings file.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "TestHooks.h"
#include "XmlText.h"
#include "gg/core/MachineSettings.h"
#include "gg/core/Rng.h"
#include "gg/media/SettingsFile.h"
#include "media/AudioFixtures.h"
#include "support/DeepXml.h"
#include "support/Utf8.h"

using gg::core::fromSettingsXml;
using gg::core::MachineSettings;
using gg::core::toSettingsXml;

namespace
{
namespace fs = std::filesystem;

/// An empty folder of the test's own.
fs::path freshDir(const char *name)
{
    const auto dir = fs::path(GG_TEST_OUTPUT_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void writeText(const fs::path &file, std::string_view text)
{
    gg::test::writeFile(file,
                        std::span(reinterpret_cast<const uint8_t *>(text.data()), text.size()));
}

/// Settings that differ from the defaults and from each other.
const MachineSettings kSome{"/Users/me/Samples & Loops/é", "/Volumes/Drum <Disk>/\"kits\"",
                            "/Users/me/Music/Caps & <Takes>"};
} // namespace

TEST_CASE("settings XML round trips, whatever bytes the folders hold", "[core]")
{
    std::vector<MachineSettings> cases{{}, kSome, {"C:\\Users\\me\\Samples", ""}};
    // &#x41; must come back as text, not as "A".
    cases.push_back({"a&#x41;b&amp;c", "&"});
    // Each byte but NUL alone, and every pair of bytes from 0x80 up.
    for (int b = 1; b < 256; ++b)
    {
        cases.push_back(
            {std::string(1, static_cast<char>(b)), "a" + std::string(1, char(b)) + "z"});
    }
    std::string pairs;
    for (int a = 0x80; a < 0x100; ++a)
    {
        for (int b = 0x80; b < 0x100; ++b)
        {
            pairs += static_cast<char>(a);
            pairs += static_cast<char>(b);
            pairs += '/';
        }
    }
    cases.push_back({pairs, pairs});

    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        INFO("case " << i);
        const std::string xml = toSettingsXml(cases[i]);
        MachineSettings back{"untouched", "untouched"};
        REQUIRE(fromSettingsXml(xml, back));
        REQUIRE(back == cases[i]);
    }
}

TEST_CASE("settings XML round trips the captures folder, and an old file reads it empty", "[core]")
{
    MachineSettings some = kSome;
    some.capturesFolder = "/Users/me/Music/Giga Grains/Captures & <more>";
    const std::string xml = toSettingsXml(some);
    REQUIRE(xml.find("capturesFolder=\"") != std::string::npos);
    MachineSettings back;
    REQUIRE(fromSettingsXml(xml, back));
    REQUIRE(back == some);

    // A file from before the captures folder: the other folders, and the captures folder empty.
    MachineSettings old{"x", "y"};
    old.capturesFolder = "untouched";
    REQUIRE(
        fromSettingsXml(R"(<settings version="1" sampleFolder="/S" lastLoadFolder="/L" />)", old));
    REQUIRE(old.sampleFolder == "/S");
    REQUIRE(old.lastLoadFolder == "/L");
    REQUIRE(old.capturesFolder.empty());
}

TEST_CASE("settings XML is one <settings> element with a version", "[core]")
{
    const std::string xml = toSettingsXml(kSome);
    REQUIRE(xml.starts_with("<settings version=\"1\" "));
    REQUIRE(xml.find('\n') == std::string::npos);
}

TEST_CASE("settings XML reads missing attributes as empty and ignores unknown ones", "[core]")
{
    MachineSettings out{"x", "y"};
    REQUIRE(fromSettingsXml(R"(<settings version="1" />)", out));
    REQUIRE(out == MachineSettings{});

    // A newer build's file: a later version, more attributes and elements.
    REQUIRE(fromSettingsXml(R"(<settings version="7" sampleFolder="/S" colour="red">)"
                            R"(<recent path="/x" /></settings>)",
                            out));
    REQUIRE(out == MachineSettings{"/S", ""});
}

TEST_CASE("settings XML round trips the content folder, and an M1 file's reads as empty", "[core]")
{
    MachineSettings settings;
    settings.sampleFolder = "/Samples";
    settings.contentFolder = "/Packs & Things/\xc3\xa9 <\"x\">";
    MachineSettings back;
    REQUIRE(fromSettingsXml(toSettingsXml(settings), back));
    REQUIRE(back == settings);
    REQUIRE(back.contentFolder == "/Packs & Things/\xc3\xa9 <\"x\">");

    // The line M1's build wrote, with no contentFolder.
    MachineSettings m1;
    m1.contentFolder = "untouched";
    REQUIRE(
        fromSettingsXml(R"(<settings version="1" sampleFolder="/S" lastLoadFolder="/L" />)", m1));
    REQUIRE(m1.sampleFolder == "/S");
    REQUIRE(m1.lastLoadFolder == "/L");
    REQUIRE(m1.contentFolder.empty());
}

TEST_CASE("settings XML edited by hand loads, with a declaration and comments", "[core]")
{
    const std::string body = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                             "<!-- Giga Grains: my folders -->\n"
                             "<settings version=\"1\" sampleFolder=\"/Samples/\xc3\xa9\"\n"
                             "          lastLoadFolder=\"/Kicks\" />\n"
                             "<!-- the end -->\n";
    for (const std::string &xml : {body, "\xef\xbb\xbf" + body}) // without and with a BOM
    {
        INFO(xml);
        MachineSettings out;
        REQUIRE(fromSettingsXml(xml, out));
        REQUIRE(out == MachineSettings{"/Samples/\xc3\xa9", "/Kicks"});
    }
}

TEST_CASE("settings XML that isn't settings is rejected and changes nothing", "[core]")
{
    const std::vector<std::string> bad{
        "",
        "settings",
        R"(<settings version="1" sampleFolder="/S")",          // truncated
        R"(<settings sampleFolder="/S" />)",                   // no version
        R"(<settings version="0" sampleFolder="/S" />)",       // version 0
        R"(<settings version="one" sampleFolder="/S" />)",     // not an integer
        R"(<options version="1" sampleFolder="/S" />)",        // another root
        R"(<settings version="1" /><settings version="1" />)", // two roots
        R"(<settings version="1" /> trailing)",                // text after the root
        R"(<!DOCTYPE settings><settings version="1" />)",      // not a declaration or comment
        R"(<!-- only a comment -->)",                          // no root
        std::string(R"(<settings version="1" sampleFolder="/S)") + '\0' + R"(" />)", // a NUL
    };
    for (const auto &xml : bad)
    {
        INFO(xml);
        MachineSettings out = kSome;
        REQUIRE_FALSE(fromSettingsXml(xml, out));
        REQUIRE(out == kSome);
    }
}

TEST_CASE("settings XML nested deeper than 64 elements is rejected without parsing", "[core]")
{
    // tinyxml parses nested elements recursively: a file of a million nested elements would
    // overflow the stack. gg-core refuses nesting deeper than 64 before tinyxml sees it.
    const auto nested = [](std::size_t depth, std::string_view open, std::string_view close) {
        std::string xml = R"(<settings version="1" sampleFolder="/S">)";
        for (std::size_t i = 1; i < depth; ++i)
        {
            xml += open;
        }
        for (std::size_t i = 1; i < depth; ++i)
        {
            xml += close;
        }
        return xml + "</settings>";
    };
    MachineSettings out = kSome;
    SECTION("64 levels, the root's included, load")
    {
        REQUIRE(fromSettingsXml(nested(64, "<a>", "</a>"), out));
        REQUIRE(out.sampleFolder == "/S");
    }
    SECTION("65 levels are rejected")
    {
        REQUIRE_FALSE(fromSettingsXml(nested(65, "<a>", "</a>"), out));
        REQUIRE(out == kSome);
    }
    SECTION("a million levels are rejected, and nothing overflows")
    {
        REQUIRE_FALSE(fromSettingsXml(nested(1'000'000, "<a>", "</a>"), out));
        REQUIRE(out == kSome);
    }
    SECTION("a million levels whose attributes hold \"/>\" are rejected")
    {
        // Only a "/>" outside quotes closes an element at once.
        REQUIRE_FALSE(fromSettingsXml(nested(1'000'000, R"(<a x="/>">)", "</a>"), out));
        REQUIRE_FALSE(fromSettingsXml(nested(1'000'000, "<a x = '/>'>", "</a>"), out));
        REQUIRE(out == kSome);
    }
    SECTION("siblings, self-closed elements, comments and declarations don't nest")
    {
        std::string xml = R"(<?xml version="1.0"?><!-- <a><a> --><settings version="1">)";
        for (int i = 0; i < 1000; ++i)
        {
            xml += R"(<a x="<b>" /><![CDATA[<c><c>]]><b></b>)";
        }
        REQUIRE(fromSettingsXml(xml + "</settings>", out));
    }
}

TEST_CASE("settings XML that tinyxml would nest deeper than it looks is rejected", "[core]")
{
    // The shapes a scan of the bytes can miscount (tests/support/DeepXml.h): each would overflow
    // tinyxml's stack. A process killed here is the failure.
    for (const gg::test::DeepXml &shape : gg::test::deepXmlShapes())
        DYNAMIC_SECTION(shape.name)
        {
            MachineSettings out = kSome;
            REQUIRE_FALSE(fromSettingsXml(shape.head +
                                              R"(<settings version="1" sampleFolder="/S">)" +
                                              shape.body + "</settings>",
                                          out));
            REQUIRE(out == kSome);
        }
}

TEST_CASE("the XML nesting scan never passes what tinyxml nests deeper", "[core]")
{
    // gg-core's scan (core/src/XmlText.h), shared by the settings, PTCH and MEDI, against tinyxml
    // itself: 200,000 short documents strung from the tokens where the two readings could part
    // (quotes, declarations, unknown nodes, character references, UTF-8 lead bytes). Whatever
    // the scan passes at a depth, tinyxml must nest no deeper.
    static constexpr std::array<std::string_view, 44> kTokens{
        "<a>",         "</a>",        "<a/>",        "<a x=\"", "<a x='", "<a x=", "\"",
        "'",           ">",           "/>",          "/",       "=",      " ",     "\t",
        "a",           "<",           "</",          "<1",      "< a",    "<!--",  "-->",
        "<![CDATA[",   "]]>",         "<!X",         "<?xml ",  "<?xml",  "<?x",   "?>",
        " version=\"", " encoding='", " VERSION=\"", "&#x",     "&#",     "x;",    "1;",
        ";",           "&amp;",       "&#x41;",      "\xe0",    "\xc3",   "\xf0",  "\x80",
        "<\x80",       "\xef\xbb\xbf"};
    static constexpr std::array<std::string_view, 4> kHeads{"", "<?xml version=\"1.0\"?>",
                                                            "\xef\xbb\xbf", "<a>"};
    std::array<std::size_t, 4> passed{}; // per depth limit, of documents tinyxml nests at all
    for (uint64_t d = 0; d < 200'000; ++d)
    {
        const auto draw = [&](uint64_t field) {
            return gg::rng::hash(
                {0x7e57'0000'0000'0185ull + d, 0, 0, gg::rng::Field::SignalNoise, field});
        };
        std::string xml(kHeads[draw(0) % kHeads.size()]);
        const uint64_t count = 1 + draw(1) % 40;
        for (uint64_t t = 0; t < count; ++t)
        {
            xml += kTokens[draw(2 + t) % kTokens.size()];
        }
        // Parsed only when the scan passes it at some limit, as gg-core does: a debug build's
        // tinyxml asserts on some documents the scan refuses (a declaration cut short).
        if (!gg::core::xmlNestingWithin(xml, passed.size() - 1))
        {
            continue;
        }
        const std::size_t depth = gg::core::tinyxmlDepth(xml);
        for (std::size_t limit = 0; limit < passed.size(); ++limit)
        {
            if (gg::core::xmlNestingWithin(xml, limit))
            {
                if (depth > limit)
                {
                    INFO("document " << d << ": '" << xml << "'");
                    FAIL("the scan passes it at depth " << limit << "; tinyxml nests " << depth);
                }
                passed[limit] += depth > 0 ? 1 : 0;
            }
        }
    }
    // The check isn't empty: many documents that nest pass at each limit but 0.
    CHECK(passed[0] == 0);
    for (std::size_t limit = 1; limit < passed.size(); ++limit)
    {
        INFO("depth limit " << limit);
        CHECK(passed[limit] > 1000);
    }
}

TEST_CASE("the XML nesting scan refuses the shapes whose reading depends on the locale", "[core]")
{
    // Direct checks of what the differential test above (which runs in the C locale only) can't
    // see: a byte from 0x80 up where tinyxml may or may not skip white space, and a declaration
    // whose names tinyxml may or may not fold to the case it expects.
    using gg::core::xmlNestingWithin;
    const std::string nbsp = "<a x=\xa0\"v\"><b></b></a>"; // 0xA0: white space in some locales
    const std::string nel = "<a x=\x85\"v\"><b></b></a>";  // 0x85: likewise
    const std::string quotedGt = "<?XML version=\">\"?><a><b></b></a>"; // a quoted > hides the end
    const std::string foldedName = "<?xml VERS\xddON=\"1\"?><a><b></b></a>";
    REQUIRE_FALSE(xmlNestingWithin(nbsp));
    REQUIRE_FALSE(xmlNestingWithin(nel));
    REQUIRE_FALSE(xmlNestingWithin(quotedGt));
    REQUIRE_FALSE(xmlNestingWithin(foldedName));
    // The declaration alone, in upper case and with plain values, is read the same way by every
    // locale: it passes.
    REQUIRE(xmlNestingWithin("<?XML version=\"1.0\"?><a><b></b></a>"));
}

TEST_CASE("settings XML with a declaration after the first node is rejected", "[core]")
{
    // Only the first node may be a declaration (carry-over: "Search and settings minors").
    const std::vector<std::string> bad{
        R"(<settings version="1" sampleFolder="/S" /><?xml version="1.0"?>)",
        R"(<!-- c --><?xml version="1.0"?><settings version="1" sampleFolder="/S" />)",
        R"(<?xml version="1.0"?><?xml version="1.0"?><settings version="1" sampleFolder="/S" />)",
        R"(<settings version="1" sampleFolder="/S"><?xml version="1.0"?></settings>)",
        R"(<settings version="1" sampleFolder="/S"><?pi?></settings>)",
    };
    for (const auto &xml : bad)
    {
        INFO(xml);
        MachineSettings out = kSome;
        REQUIRE_FALSE(fromSettingsXml(xml, out));
        REQUIRE(out == kSome);
    }
    MachineSettings out;
    REQUIRE(
        fromSettingsXml(R"(<?xml version="1.0"?><settings version="1" sampleFolder="/S" />)", out));
    REQUIRE(out.sampleFolder == "/S");
}

TEST_CASE("loadSettings gives the defaults for a missing, corrupt or huge file", "[media]")
{
    const auto dir = freshDir("settings-defaults");
    REQUIRE(gg::media::loadSettings(dir / "none.xml") == MachineSettings{});
    REQUIRE(gg::media::loadSettings(dir) == MachineSettings{}); // a folder

    const auto corrupt = dir / "corrupt.xml";
    writeText(corrupt, "<settings version=\"1\" sampleFolder=\"/S");
    REQUIRE(gg::media::loadSettings(corrupt) == MachineSettings{});

    // Valid settings, padded past 1 MiB with whitespace.
    const auto huge = dir / "huge.xml";
    writeText(huge, toSettingsXml(kSome) + std::string((1u << 20) + 1, ' '));
    REQUIRE(gg::media::loadSettings(huge) == MachineSettings{});
}

TEST_CASE("saveSettings creates the folder, and loadSettings reads it back", "[media]")
{
    const auto dir = freshDir("settings-save");
    const auto file = dir / "deep" / "er" / "settings.xml";
    REQUIRE(gg::media::saveSettings(file, kSome));
    REQUIRE(fs::is_regular_file(file));
    REQUIRE(gg::media::loadSettings(file) == kSome);

    // Overwriting replaces, and leaves no temporary file behind.
    const MachineSettings other{"/Other", "/Last"};
    REQUIRE(gg::media::saveSettings(file, other));
    REQUIRE(gg::media::loadSettings(file) == other);
    std::size_t files = 0;
    for ([[maybe_unused]] const auto &e : fs::directory_iterator(file.parent_path()))
    {
        ++files;
    }
    REQUIRE(files == 1);

    // A NUL can't be written, and the old settings stay.
    REQUIRE_FALSE(gg::media::saveSettings(file, {std::string("/a\0b", 4), ""}));
    REQUIRE_FALSE(gg::media::saveSettings(file, {"", "", std::string("/c\0d", 4)}));
    REQUIRE(gg::media::loadSettings(file) == other);

    // A folder that can't be made: a file is in the way.
    REQUIRE_FALSE(gg::media::saveSettings(file / "settings.xml", kSome));
}

TEST_CASE("saveSettings removes the temporary files of saves that crashed, and nothing else",
          "[media]")
{
    // A crash between writing a save's temporary file and renaming it leaves the file behind.
    // The next save removes those of its own settings file more than 10 minutes older than the
    // file it has just written: a save takes milliseconds, so one that old has failed, while a
    // younger one may be another instance's, mid-save.
    const auto dir = freshDir("settings-stale-temporary");
    const auto file = dir / "settings.xml";
    const auto hourAgo = fs::file_time_type::clock::now() - std::chrono::hours(1);
    const auto make = [&](const char *name, bool old) {
        writeText(dir / name, "<settings version=\"1\"");
        if (old)
        {
            fs::last_write_time(dir / name, hourAgo);
        }
    };
    make("settings.xml.4242.0.tmp", true);     // a crashed save's: removed
    make("settings.xml.77.123456.tmp", true);  // another: removed
    make("settings.xml.4243.0.tmp", false);    // another instance's, mid-save: kept
    make("settings.xml.notes.tmp", true);      // not a save's
    make("settings.xml.4242.0.tmp.bak", true); // nor this
    make("settings.xml.4242..tmp", true);      // nor this
    make("other.xml.4242.0.tmp", true);        // another file's
    REQUIRE(gg::media::saveSettings(file, kSome));
    REQUIRE(gg::media::loadSettings(file) == kSome);

    std::vector<std::string> left;
    for (const auto &e : fs::directory_iterator(dir))
    {
        left.push_back(gg::test::utf8(e.path().filename()));
    }
    std::sort(left.begin(), left.end());
    REQUIRE(left == std::vector<std::string>{"other.xml.4242.0.tmp", "settings.xml",
                                             "settings.xml.4242..tmp",
                                             "settings.xml.4242.0.tmp.bak",
                                             "settings.xml.4243.0.tmp", "settings.xml.notes.tmp"});
}

TEST_CASE("saveSettings succeeds while other instances load and save the same file", "[media]")
{
    // Threads stand in for plugin instances in other processes: one loads the file over and over
    // while two save it. Every save succeeds, and every load reads one instance's settings whole.
    // On Windows a reader's open handle can block a rename for a moment; saveSettings retries.
    const auto dir = freshDir("settings-concurrent");
    const auto file = dir / "settings.xml";
    const MachineSettings a{"/A", "/a"};
    const MachineSettings b{"/B", "/b"};
    REQUIRE(gg::media::saveSettings(file, a));

    std::atomic<bool> stop{false};
    std::atomic<int> torn{0};
    std::thread reader([&] {
        while (!stop.load())
        {
            const auto got = gg::media::loadSettings(file);
            if (got != a && got != b)
            {
                torn.fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });
    std::atomic<int> failed{0};
    const auto saver = [&](const MachineSettings &settings) {
        for (int i = 0; i < 100; ++i)
        {
            if (!gg::media::saveSettings(file, settings))
            {
                failed.fetch_add(1);
            }
        }
    };
    std::thread other(saver, b);
    saver(a);
    other.join();
    stop.store(true);
    reader.join();
    REQUIRE(failed.load() == 0);
    REQUIRE(torn.load() == 0);
    std::size_t files = 0;
    for ([[maybe_unused]] const auto &e : fs::directory_iterator(dir))
    {
        ++files;
    }
    REQUIRE(files == 1);
}

TEST_CASE("saveSettings waits out another program holding the file for a moment", "[media]")
{
    // An ifstream opens a file without delete sharing on Windows, which blocks renaming over it,
    // as a virus scanner or an editor would. Released after 100 ms, the save goes through.
    // Elsewhere an open file never blocks a rename.
    const auto dir = freshDir("settings-held");
    const auto file = dir / "settings.xml";
    REQUIRE(gg::media::saveSettings(file, kSome));
    auto held = std::make_unique<std::ifstream>(file, std::ios::binary);
    REQUIRE(held->is_open());
    std::thread release([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        held.reset();
    });
    const MachineSettings other{"/Other", "/Last"};
    const bool saved = gg::media::saveSettings(file, other);
    release.join();
    REQUIRE(saved);
    REQUIRE(gg::media::loadSettings(file) == other);
}

#if defined(_WIN32)
TEST_CASE("saveSettings gives up on a file held open too long, leaving the old settings", "[media]")
{
    const auto dir = freshDir("settings-held-long");
    const auto file = dir / "settings.xml";
    REQUIRE(gg::media::saveSettings(file, kSome));
    {
        std::ifstream held(file, std::ios::binary);
        REQUIRE(held.is_open());
        REQUIRE_FALSE(gg::media::saveSettings(file, {"/Other", "/Last"}));
    }
    REQUIRE(gg::media::loadSettings(file) == kSome);
    std::size_t files = 0;
    for ([[maybe_unused]] const auto &e : fs::directory_iterator(dir))
    {
        ++files;
    }
    REQUIRE(files == 1);
}
#endif

namespace
{
using gg::media::detail::SaveStep;

/// What the save hook saw, and the faults it gives.
struct SaveRecord
{
    fs::path file;                      // the settings file being saved
    std::vector<SaveStep> steps;        // each step, in order
    std::vector<std::string> temporary; // the temporary file's content at each step
    std::vector<std::string> settings;  // the settings file's content at each step
    std::optional<SaveStep> failAt;     // the step to fail, with `error`
    std::error_code error;
    int renameFailures{0}; // fail this many Rename attempts, with `error`
};
SaveRecord gSave;

std::string contentOf(const fs::path &file)
{
    const auto bytes = gg::test::readFile(file);
    return {bytes.begin(), bytes.end()};
}

std::error_code saveHook(SaveStep step, const fs::path &temporary)
{
    gSave.steps.push_back(step);
    gSave.temporary.push_back(contentOf(temporary));
    gSave.settings.push_back(contentOf(gSave.file));
    if (step == SaveStep::Rename && gSave.renameFailures > 0)
    {
        --gSave.renameFailures;
        return gSave.error;
    }
    return gSave.failAt == step ? gSave.error : std::error_code();
}

/// Sets the save hook, recording `file`'s saves, for its lifetime.
struct SaveHookSet
{
    explicit SaveHookSet(const fs::path &file)
    {
        gSave = SaveRecord{};
        gSave.file = file;
        gg::media::detail::setSaveHook(saveHook);
    }
    ~SaveHookSet() { gg::media::detail::setSaveHook(nullptr); }
    SaveHookSet(const SaveHookSet &) = delete;
    SaveHookSet &operator=(const SaveHookSet &) = delete;
};

std::size_t filesIn(const fs::path &dir)
{
    std::size_t files = 0;
    for ([[maybe_unused]] const auto &e : fs::directory_iterator(dir))
    {
        ++files;
    }
    return files;
}
} // namespace

TEST_CASE("saveSettings flushes its temporary file to the disk before renaming it", "[media]")
{
    const auto dir = freshDir("settings-save-steps");
    const auto file = dir / "settings.xml";
    REQUIRE(gg::media::saveSettings(file, kSome));
    const std::string before = toSettingsXml(kSome);
    const MachineSettings other{"/Other", "/Last"};
    const std::string after = toSettingsXml(other);

    const SaveHookSet hook(file);
    REQUIRE(gg::media::saveSettings(file, other));
    // Written whole, flushed, then renamed, once; the settings file untouched until the rename.
    REQUIRE(gSave.steps == std::vector{SaveStep::Write, SaveStep::Sync, SaveStep::Rename});
    REQUIRE(gSave.temporary == std::vector{after, after, after});
    REQUIRE(gSave.settings == std::vector{before, before, before});
    REQUIRE(contentOf(file) == after);
    REQUIRE(filesIn(dir) == 1);
}

TEST_CASE("saveSettings whose write, flush or rename fails leaves the old settings whole",
          "[media]")
{
    const auto dir = freshDir("settings-save-fails");
    const auto file = dir / "settings.xml";
    REQUIRE(gg::media::saveSettings(file, kSome));
    const SaveHookSet hook(file);
    gSave.error = std::make_error_code(std::errc::io_error);
    SECTION("the write") { gSave.failAt = SaveStep::Write; }
    SECTION("the flush") { gSave.failAt = SaveStep::Sync; }
    SECTION("the rename") { gSave.failAt = SaveStep::Rename; }
    REQUIRE_FALSE(gg::media::saveSettings(file, {"/Other", "/Last"}));
    REQUIRE(gSave.steps.back() == *gSave.failAt);
    REQUIRE(gg::media::loadSettings(file) == kSome);
    REQUIRE(filesIn(dir) == 1); // no temporary file left
}

TEST_CASE("saveSettings retries a rename that fails while another program holds the file",
          "[media]")
{
    // On Windows a rename over a file another program has open fails with a sharing violation
    // until it closes the file. saveSettings tries 20 times, 25 ms apart.
    const auto dir = freshDir("settings-save-retries");
    const auto file = dir / "settings.xml";
    REQUIRE(gg::media::saveSettings(file, kSome));
    const MachineSettings other{"/Other", "/Last"};
    const SaveHookSet hook(file);
    gSave.error = gg::media::detail::transientRenameError();
    const auto renames = [] {
        return std::count(gSave.steps.begin(), gSave.steps.end(), SaveStep::Rename);
    };
    SECTION("3 failures, then success")
    {
        gSave.renameFailures = 3;
        REQUIRE(gg::media::saveSettings(file, other));
        REQUIRE(renames() == 4);
        REQUIRE(gg::media::loadSettings(file) == other);
    }
    SECTION("19 failures, then success on the last try")
    {
        gSave.renameFailures = 19;
        REQUIRE(gg::media::saveSettings(file, other));
        REQUIRE(renames() == 20);
        REQUIRE(gg::media::loadSettings(file) == other);
    }
    SECTION("20 failures: the save fails, and the old settings stay")
    {
        gSave.renameFailures = 20;
        REQUIRE_FALSE(gg::media::saveSettings(file, other));
        REQUIRE(renames() == 20);
        REQUIRE(gg::media::loadSettings(file) == kSome);
    }
    SECTION("an error that isn't transient isn't retried")
    {
        gSave.error = std::make_error_code(std::errc::permission_denied);
        gSave.renameFailures = 1;
        REQUIRE_FALSE(gg::media::saveSettings(file, other));
        REQUIRE(renames() == 1);
        REQUIRE(gg::media::loadSettings(file) == kSome);
    }
    REQUIRE(filesIn(dir) == 1);
}

TEST_CASE("defaultSettingsPath is in the user's configuration folder", "[media]")
{
    const fs::path path = gg::media::defaultSettingsPath();
    INFO(gg::test::utf8(path));
    REQUIRE(path.is_absolute());
    REQUIRE(path.filename() == "settings.xml");
#if defined(_WIN32)
    REQUIRE(path.parent_path().filename() == "Giga Grains");
    const wchar_t *appData = _wgetenv(L"APPDATA");
    REQUIRE(appData != nullptr);
    REQUIRE(path.parent_path().parent_path() == fs::path(appData));
#elif defined(__APPLE__)
    REQUIRE(path.parent_path().filename() == "Giga Grains");
    REQUIRE(path.parent_path().parent_path().filename() == "Application Support");
    const char *home = std::getenv("HOME");
    REQUIRE(home != nullptr);
    REQUIRE(path.parent_path().parent_path().parent_path() == fs::path(home) / "Library");
#else
    REQUIRE(path.parent_path().filename() == "giga-grains");
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    const char *home = std::getenv("HOME");
    if (xdg != nullptr && fs::path(xdg).is_absolute())
    {
        REQUIRE(path.parent_path().parent_path() == fs::path(xdg));
    }
    else
    {
        REQUIRE(home != nullptr);
        REQUIRE(path.parent_path().parent_path() == fs::path(home) / ".config");
    }
#endif
}

TEST_CASE("defaultCapturesFolder is in the user's Music folder", "[media]")
{
    const fs::path path = gg::media::defaultCapturesFolder();
    INFO(gg::test::utf8(path));
    REQUIRE(path.is_absolute());
    REQUIRE(path.filename() == "Captures");
    REQUIRE(path.parent_path().filename() == "Giga Grains");
    // It is a name: nothing is created.
#if defined(_WIN32)
    const wchar_t *profile = _wgetenv(L"USERPROFILE");
    REQUIRE(profile != nullptr);
    REQUIRE(path.parent_path().parent_path() == fs::path(profile) / L"Music");
#elif defined(__APPLE__)
    const char *home = std::getenv("HOME");
    REQUIRE(home != nullptr);
    REQUIRE(path.parent_path().parent_path() == fs::path(home) / "Music");
#else
    const char *music = std::getenv("XDG_MUSIC_DIR");
    const char *home = std::getenv("HOME");
    if (music != nullptr && fs::path(music).is_absolute())
    {
        REQUIRE(path.parent_path().parent_path() == fs::path(music));
    }
    else
    {
        REQUIRE(home != nullptr);
        REQUIRE(path.parent_path().parent_path() == fs::path(home) / "Music");
    }
#endif
}

#if !defined(_WIN32)
namespace
{
/// Sets (or, given nullptr, unsets) an environment variable for its lifetime.
class ScopedEnv
{
  public:
    ScopedEnv(const char *name, const char *value) : name_(name)
    {
        if (const char *old = std::getenv(name))
        {
            old_ = old;
        }
        set(value);
    }
    ~ScopedEnv() { set(old_ ? old_->c_str() : nullptr); }
    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

  private:
    void set(const char *value)
    {
        if (value != nullptr)
        {
            ::setenv(name_, value, 1);
        }
        else
        {
            ::unsetenv(name_);
        }
    }
    const char *name_;
    std::optional<std::string> old_;
};
} // namespace

TEST_CASE("defaultSettingsPath follows HOME and XDG_CONFIG_HOME", "[media]")
{
    const ScopedEnv home("HOME", "/home/gg-test");
#if defined(__APPLE__)
    REQUIRE(gg::media::defaultSettingsPath() ==
            fs::path("/home/gg-test/Library/Application Support/Giga Grains/settings.xml"));
#else
    {
        const ScopedEnv xdg("XDG_CONFIG_HOME", "/xdg/config");
        REQUIRE(gg::media::defaultSettingsPath() ==
                fs::path("/xdg/config/giga-grains/settings.xml"));
    }
    for (const char *ignored : {static_cast<const char *>(nullptr), "", "relative/config"})
    {
        const ScopedEnv xdg("XDG_CONFIG_HOME", ignored);
        REQUIRE(gg::media::defaultSettingsPath() ==
                fs::path("/home/gg-test/.config/giga-grains/settings.xml"));
    }
#endif
}

TEST_CASE("defaultCapturesFolder follows HOME and XDG_MUSIC_DIR, and creates nothing", "[media]")
{
    const fs::path home = fs::path(GG_TEST_OUTPUT_DIR) / "captures-home";
    fs::remove_all(home);
    const ScopedEnv homeEnv("HOME", gg::test::utf8(home).c_str());
#if defined(__APPLE__)
    REQUIRE(gg::media::defaultCapturesFolder() == home / "Music/Giga Grains/Captures");
#else
    {
        const ScopedEnv music("XDG_MUSIC_DIR", "/xdg/music");
        REQUIRE(gg::media::defaultCapturesFolder() == fs::path("/xdg/music/Giga Grains/Captures"));
    }
    for (const char *ignored : {static_cast<const char *>(nullptr), "", "relative/music"})
    {
        const ScopedEnv music("XDG_MUSIC_DIR", ignored);
        REQUIRE(gg::media::defaultCapturesFolder() == home / "Music/Giga Grains/Captures");
    }
#endif
    REQUIRE_FALSE(fs::exists(home));
}
#endif
