// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The content search path (gg/media/ContentPaths.h; the presets spec §6.4): the content roots in
// their order, a content-relative reference resolved against them, and the user presets folder
// and favorites file beside the settings file. Every file test works in a folder of its own
// under GG_TEST_OUTPUT_DIR; the environment variables a test sets are restored when it ends, and
// each test case runs in a process of its own.

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
#include "gg/media/ContentPaths.h"
#include "gg/media/SettingsFile.h"
#include "media/AudioFixtures.h"
#include "support/Utf8.h"

namespace
{
namespace fs = std::filesystem;
using gg::core::MachineSettings;
using gg::media::contentRoots;
using gg::media::resolveContentPath;
using gg::test::pathOf;
using gg::test::utf8;

/// An empty folder of the test's own.
fs::path freshDir(const char *name)
{
    const auto dir = fs::path(GG_TEST_OUTPUT_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

/// Writes a few bytes to `file`, creating its folder.
void put(const fs::path &file)
{
    fs::create_directories(file.parent_path());
    constexpr std::string_view kBytes = "content";
    gg::test::writeFile(file,
                        std::span(reinterpret_cast<const uint8_t *>(kBytes.data()), kBytes.size()));
}

/// Sets (or, given nullptr, unsets) an environment variable for its lifetime. Names and values
/// are ASCII.
class ScopedEnv
{
  public:
    ScopedEnv(const char *name, const char *value) : name_(name)
    {
        if (const auto old = get(); old)
        {
            old_ = old;
        }
        set(value);
    }
    ~ScopedEnv() { set(old_ ? old_->c_str() : nullptr); }
    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

  private:
#if defined(_WIN32)
    static std::wstring wide(std::string_view text)
    {
        return std::wstring(text.begin(), text.end());
    }
    std::optional<std::string> get() const
    {
        const wchar_t *value = ::_wgetenv(wide(name_).c_str());
        if (value == nullptr)
        {
            return std::nullopt;
        }
        return utf8(fs::path(value));
    }
    void set(const char *value) const
    {
        // An empty value removes the variable: Windows can't hold an empty one.
        (void)::_wputenv_s(wide(name_).c_str(), value != nullptr ? pathOf(value).c_str() : L"");
    }
#else
    std::optional<std::string> get() const
    {
        const char *value = std::getenv(name_);
        return value != nullptr ? std::optional<std::string>(value) : std::nullopt;
    }
    void set(const char *value) const
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
#endif
    const char *name_;
    std::optional<std::string> old_;
};

/// Each root as UTF-8 with `/` separators.
std::vector<std::string> generic(const std::vector<fs::path> &roots)
{
    std::vector<std::string> out;
    for (const auto &root : roots)
    {
        out.push_back(gg::media::toGenericUtf8(root).value());
    }
    return out;
}

/// The platform's install roots, per-user then system, for the environment each test sets.
#if defined(_WIN32)
constexpr const char *kLocalAppData = "C:/gg-test/Local";
constexpr const char *kProgramData = "C:/gg-test/ProgramData";
const std::vector<std::string> kInstallRoots{"C:/gg-test/Local/sethbc/Giga Grains",
                                             "C:/gg-test/ProgramData/sethbc/Giga Grains"};
#elif defined(__APPLE__)
const std::vector<std::string> kInstallRoots{
    "/home/gg-test/Library/Application Support/sethbc/Giga Grains",
    "/Library/Application Support/sethbc/Giga Grains"};
#else
const std::vector<std::string> kInstallRoots{
    "/xdg/data/sethbc/Giga Grains", "/xdg/a/sethbc/Giga Grains", "/xdg/b/sethbc/Giga Grains"};
#endif

/// Sets the environment kInstallRoots names.
struct InstallEnv
{
#if defined(_WIN32)
    ScopedEnv local{"LOCALAPPDATA", kLocalAppData};
    ScopedEnv program{"ProgramData", kProgramData};
#else
    ScopedEnv home{"HOME", "/home/gg-test"};
#if !defined(__APPLE__)
    ScopedEnv dataHome{"XDG_DATA_HOME", "/xdg/data"};
    ScopedEnv dataDirs{"XDG_DATA_DIRS", "/xdg/a:/xdg/b"};
#endif
#endif
};
} // namespace

TEST_CASE("the content roots are GG_CONTENT_DIR, the content folder, then the installs",
          "[media][content]")
{
    const InstallEnv install;
    const ScopedEnv dev("GG_CONTENT_DIR", "/dev/tree/content");
    MachineSettings settings;
    settings.contentFolder = "/Users/me/My Pack \xc3\xa9"; // UTF-8
    std::vector<std::string> expected{"/dev/tree/content", "/Users/me/My Pack \xc3\xa9"};
    expected.insert(expected.end(), kInstallRoots.begin(), kInstallRoots.end());
    REQUIRE(generic(contentRoots(settings)) == expected);
}

TEST_CASE("the content roots leave out an unset or empty GG_CONTENT_DIR and content folder",
          "[media][content]")
{
    const InstallEnv install;
    for (const char *dev : {static_cast<const char *>(nullptr), ""})
    {
        INFO((dev != nullptr ? "empty" : "unset"));
        const ScopedEnv env("GG_CONTENT_DIR", dev);
        REQUIRE(generic(contentRoots(MachineSettings{})) == kInstallRoots);
    }
    // Each alone.
    {
        const ScopedEnv env("GG_CONTENT_DIR", "/dev/content");
        auto expected = kInstallRoots;
        expected.insert(expected.begin(), "/dev/content");
        REQUIRE(generic(contentRoots(MachineSettings{})) == expected);
    }
    {
        const ScopedEnv env("GG_CONTENT_DIR", nullptr);
        MachineSettings settings;
        settings.contentFolder = "/pack";
        auto expected = kInstallRoots;
        expected.insert(expected.begin(), "/pack");
        REQUIRE(generic(contentRoots(settings)) == expected);
    }
}

#if !defined(_WIN32) && !defined(__APPLE__)
TEST_CASE("the content roots default to the XDG data folders", "[media][content]")
{
    const ScopedEnv home("HOME", "/home/gg-test");
    const ScopedEnv dev("GG_CONTENT_DIR", nullptr);
    const std::vector<std::string> defaults{"/home/gg-test/.local/share/sethbc/Giga Grains",
                                            "/usr/local/share/sethbc/Giga Grains",
                                            "/usr/share/sethbc/Giga Grains"};
    for (const char *unset : {static_cast<const char *>(nullptr), ""})
    {
        const ScopedEnv dataHome("XDG_DATA_HOME", unset);
        const ScopedEnv dataDirs("XDG_DATA_DIRS", unset);
        REQUIRE(generic(contentRoots(MachineSettings{})) == defaults);
    }
    // The XDG base directory spec: relative entries and empty ones are ignored.
    const ScopedEnv dataHome("XDG_DATA_HOME", "relative/data");
    const ScopedEnv dataDirs("XDG_DATA_DIRS", "::relative:/xdg/a:");
    REQUIRE(generic(contentRoots(MachineSettings{})) ==
            std::vector<std::string>{"/home/gg-test/.local/share/sethbc/Giga Grains",
                                     "/xdg/a/sethbc/Giga Grains"});
}
#endif

TEST_CASE("a reference resolves against the first root that holds the file", "[media][content]")
{
    const auto dir = freshDir("content_paths_resolve");
    const std::vector<fs::path> roots{dir / "first", dir / "second", dir / "third"};
    const std::string reference = "Samples/Bowed/cello \xc3\xa9.flac";
    const fs::path relative = pathOf(reference);
    fs::create_directories(roots[0]);

    REQUIRE(resolveContentPath(reference, roots) == std::nullopt); // in none of them

    put(roots[1] / relative);
    put(roots[2] / relative);
    REQUIRE(resolveContentPath(reference, roots) == roots[1] / relative); // the first lacks it

    // A folder of that name isn't the file.
    fs::create_directories(roots[0] / relative);
    REQUIRE(resolveContentPath(reference, roots) == roots[1] / relative);

    fs::remove_all(roots[0] / relative);
    put(roots[0] / relative);
    REQUIRE(resolveContentPath(reference, roots) == roots[0] / relative);

    REQUIRE(resolveContentPath(reference, std::span<const fs::path>()) == std::nullopt);
}

TEST_CASE("a reference that escapes a root is refused", "[media][content]")
{
    const auto dir = freshDir("content_paths_escape");
    const fs::path root = dir / "root";
    put(dir / "x.flac");  // beside the root
    put(root / "x.flac"); // in it
    put(root / "Samples/x.flac");
#if !defined(_WIN32)
    put(root / "a\\b.flac"); // a backslash is a file name's character here, and still refused
#endif
    const std::vector<fs::path> roots{root};
    REQUIRE(resolveContentPath("x.flac", roots) == root / "x.flac");
    REQUIRE(resolveContentPath("Samples/x.flac", roots) == root / "Samples" / "x.flac");

    const std::string absolute = gg::media::toGenericUtf8(fs::absolute(dir / "x.flac")).value();
    const std::vector<std::string> refused{
        "",
        "../x.flac",
        "Samples/../x.flac",
        "Samples/../../x.flac",
        "..",
        "/abs/x.flac",
        absolute,
        "a\\b.flac",
        "Samples\\x.flac",
        std::string("x.flac\0", 7),
        std::string("x.flac\0.wav", 11),
#if defined(_WIN32)
        "C:x.flac",
        "C:/x.flac",
        "//server/share/x.flac",
#endif
    };
    for (const auto &reference : refused)
    {
        INFO(reference);
        REQUIRE(resolveContentPath(reference, roots) == std::nullopt);
    }
}

TEST_CASE("the user presets folder and favorites sit beside a GG_SETTINGS_FILE settings file",
          "[media][content]")
{
    const fs::path settingsFile = fs::path(GG_TEST_OUTPUT_DIR) / "content_paths_settings" /
                                  pathOf("r\xc3\xa9glages") / "settings.xml";
    REQUIRE(gg::media::userPresetsFolder(settingsFile) == settingsFile.parent_path() / "Presets");
    REQUIRE(gg::media::favoritesFile(settingsFile) == settingsFile.parent_path() / "favorites.txt");
    // No settings file (no home folder to put it in): no folder, never a relative one.
    REQUIRE(gg::media::userPresetsFolder(fs::path()).empty());
    REQUIRE(gg::media::favoritesFile(fs::path()).empty());
}

TEST_CASE("the factory presets folder and content version file are in a content root",
          "[media][content]")
{
    const fs::path root = pathOf("/content/Giga Grains \xc3\xa9");
    REQUIRE(gg::media::factoryPresetsFolder(root) == root / "Presets");
    REQUIRE(gg::media::contentVersionFile(root) == root / "content-version.txt");
}

TEST_CASE("the user presets folder beside the default settings file", "[media][content]")
{
#if defined(_WIN32)
    const ScopedEnv appData("APPDATA", "C:/gg-test/Roaming");
    const fs::path settingsFile = gg::media::defaultSettingsPath();
    REQUIRE(gg::media::toGenericUtf8(gg::media::userPresetsFolder(settingsFile)) ==
            "C:/gg-test/Roaming/Giga Grains/Presets");
    REQUIRE(gg::media::toGenericUtf8(gg::media::favoritesFile(settingsFile)) ==
            "C:/gg-test/Roaming/Giga Grains/favorites.txt");
#elif defined(__APPLE__)
    const ScopedEnv home("HOME", "/home/gg-test");
    const fs::path settingsFile = gg::media::defaultSettingsPath();
    REQUIRE(gg::media::userPresetsFolder(settingsFile) ==
            fs::path("/home/gg-test/Library/Application Support/Giga Grains/Presets"));
    REQUIRE(gg::media::favoritesFile(settingsFile) ==
            fs::path("/home/gg-test/Library/Application Support/Giga Grains/favorites.txt"));
#else
    // On Linux the default settings file is configuration, and the presets are data.
    const ScopedEnv home("HOME", "/home/gg-test");
    const ScopedEnv config("XDG_CONFIG_HOME", "/xdg/config");
    {
        const ScopedEnv dataHome("XDG_DATA_HOME", "/xdg/data");
        REQUIRE(gg::media::userPresetsFolder(gg::media::defaultSettingsPath()) ==
                fs::path("/xdg/data/giga-grains/presets"));
    }
    for (const char *unset : {static_cast<const char *>(nullptr), "", "relative/data"})
    {
        const ScopedEnv dataHome("XDG_DATA_HOME", unset);
        REQUIRE(gg::media::userPresetsFolder(gg::media::defaultSettingsPath()) ==
                fs::path("/home/gg-test/.local/share/giga-grains/presets"));
    }
    // The favorites stay beside the settings file, and another settings file keeps its own.
    REQUIRE(gg::media::favoritesFile(gg::media::defaultSettingsPath()) ==
            fs::path("/xdg/config/giga-grains/favorites.txt"));
    REQUIRE(gg::media::userPresetsFolder("/elsewhere/settings.xml") ==
            fs::path("/elsewhere/Presets"));
#endif
}
