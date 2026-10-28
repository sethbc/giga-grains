// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/SettingsFile.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace gg::media
{
namespace fs = std::filesystem;

namespace
{
/// The most loadSettings reads: the settings are a few hundred bytes.
constexpr std::size_t kMaxSettingsBytes = std::size_t{1} << 20;

#if !defined(_WIN32)
/// $HOME if it is an absolute path, else the user's home directory from the password database;
/// empty if neither is.
fs::path homeFolder()
{
    if (const char *home = std::getenv("HOME"); home != nullptr && fs::path(home).is_absolute())
    {
        return home;
    }
    if (const passwd *user = ::getpwuid(::getuid());
        user != nullptr && user->pw_dir != nullptr && fs::path(user->pw_dir).is_absolute())
    {
        return user->pw_dir;
    }
    return {};
}
#endif

int processId()
{
#if defined(_WIN32)
    return ::_getpid();
#else
    return static_cast<int>(::getpid());
#endif
}
} // namespace

fs::path defaultSettingsPath()
{
#if defined(_WIN32)
    const wchar_t *appData = ::_wgetenv(L"APPDATA");
    if (appData == nullptr || !fs::path(appData).is_absolute())
    {
        return {};
    }
    return fs::path(appData) / L"Giga Grains" / L"settings.xml";
#elif defined(__APPLE__)
    const fs::path home = homeFolder();
    if (home.empty())
    {
        return {};
    }
    return home / "Library" / "Application Support" / "Giga Grains" / "settings.xml";
#else
    // The XDG base directory spec: a relative XDG_CONFIG_HOME is invalid and ignored.
    fs::path config;
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME");
        xdg != nullptr && fs::path(xdg).is_absolute())
    {
        config = xdg;
    }
    else if (const fs::path home = homeFolder(); !home.empty())
    {
        config = home / ".config";
    }
    else
    {
        return {};
    }
    return config / "giga-grains" / "settings.xml";
#endif
}

core::MachineSettings loadSettings(const fs::path &file)
{
    std::error_code error;
    if (!fs::is_regular_file(file, error))
    {
        return {};
    }
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        return {};
    }
    // One byte past the cap, to tell a file at the cap from a longer one.
    std::string text(kMaxSettingsBytes + 1, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    const auto got = static_cast<std::size_t>(in.gcount());
    if (in.bad() || got > kMaxSettingsBytes)
    {
        return {};
    }
    text.resize(got);
    core::MachineSettings settings;
    if (!core::fromSettingsXml(text, settings))
    {
        return {};
    }
    return settings;
}

bool saveSettings(const fs::path &file, const core::MachineSettings &settings)
{
    if (settings.sampleFolder.find('\0') != std::string::npos ||
        settings.lastLoadFolder.find('\0') != std::string::npos)
    {
        return false;
    }
    std::error_code error;
    if (file.has_parent_path())
    {
        fs::create_directories(file.parent_path(), error);
        if (error)
        {
            return false;
        }
    }
    // A temporary file of this save's own beside `file`, renamed over it once written whole. The
    // process ID and a process-wide count make its name unique, though every plugin instance in
    // a host shares the process: a save in another process or instance writes its own, and the
    // last rename wins.
    static std::atomic<uint64_t> saves{0};
    fs::path temporary = file;
    temporary +=
        "." + std::to_string(processId()) + "." + std::to_string(saves.fetch_add(1)) + ".tmp";
    const std::string xml = core::toSettingsXml(settings);
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(xml.data(), static_cast<std::streamsize>(xml.size()));
        out.close();
        if (!out)
        {
            fs::remove(temporary, error);
            return false;
        }
    }
    fs::rename(temporary, file, error);
    if (error)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    return true;
}
} // namespace gg::media
