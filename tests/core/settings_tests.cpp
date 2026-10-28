// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The per-machine settings (spec §6.7): gg-core's XML codec (gg/core/MachineSettings.h), and
// gg-media's settings file (gg/media/SettingsFile.h), tagged [media]. Every file test works in a
// folder of its own under GG_TEST_OUTPUT_DIR and never touches the user's settings file.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/MachineSettings.h"
#include "gg/media/SettingsFile.h"
#include "media/AudioFixtures.h"

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
