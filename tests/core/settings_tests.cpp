// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The per-machine settings (spec §6.7): gg-core's XML codec (gg/core/MachineSettings.h), and
// gg-media's settings file (gg/media/SettingsFile.h), tagged [media]. Every file test works in a
// folder of its own under GG_TEST_OUTPUT_DIR and never touches the user's settings file.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "XmlText.h"
#include "gg/core/MachineSettings.h"
#include "gg/core/Rng.h"
#include "gg/media/SettingsFile.h"
#include "media/AudioFixtures.h"
#include "support/DeepXml.h"

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
const MachineSettings kSome{"/Users/me/Samples & Loops/é", "/Volumes/Drum <Disk>/\"kits\""};
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
    const auto dir = freshDir("search-settings-defaults");
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
    const auto dir = freshDir("search-settings-save");
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
    REQUIRE(gg::media::loadSettings(file) == other);

    // A folder that can't be made: a file is in the way.
    REQUIRE_FALSE(gg::media::saveSettings(file / "settings.xml", kSome));
}

TEST_CASE("defaultSettingsPath is in the user's configuration folder", "[media]")
{
    const fs::path path = gg::media::defaultSettingsPath();
    INFO(path.string());
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
#endif
