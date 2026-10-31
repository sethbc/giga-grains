// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "UiSettings.h"

#include <cstdlib>
#include <filesystem>

#include "gg/core/MachineSettings.h"
#include "gg/media/SettingsFile.h"

namespace gg::plugin
{
namespace
{
/// The per-machine settings file: $GG_SETTINGS_FILE when set and not empty, else the user's
/// (gg::media::defaultSettingsPath), as GigaGrainsPlugin.cpp's settingsFile() finds it for the
/// loader. Empty when neither can be found.
std::filesystem::path settingsFile()
{
#if defined(_WIN32)
    if (const wchar_t *file = ::_wgetenv(L"GG_SETTINGS_FILE"); file != nullptr && *file != 0)
    {
        return std::filesystem::path(file);
    }
#else
    if (const char *file = std::getenv("GG_SETTINGS_FILE"); file != nullptr && *file != 0)
    {
        return std::filesystem::path(file);
    }
#endif
    return gg::media::defaultSettingsPath();
}

constexpr const char *kLight = "light";
constexpr const char *kDark = "dark";
} // namespace

ThemeName readTheme()
{
    try
    {
        const std::filesystem::path file = settingsFile();
        if (file.empty())
        {
            return ThemeName::Dark;
        }
        return media::loadSettings(file).theme == kLight ? ThemeName::Light : ThemeName::Dark;
    }
    catch (...) // an allocation that failed: the default theme
    {
        return ThemeName::Dark;
    }
}

bool writeTheme(ThemeName theme)
{
    try
    {
        const std::filesystem::path file = settingsFile();
        if (file.empty())
        {
            return false;
        }
        // Read, modify, write, as SampleLoader::setSampleFolder saves the folder (R16).
        core::MachineSettings settings = media::loadSettings(file);
        settings.theme = theme == ThemeName::Light ? kLight : kDark;
        return media::saveSettings(file, settings);
    }
    catch (...) // an allocation or a path conversion that failed: nothing saved
    {
        return false;
    }
}
} // namespace gg::plugin
