// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_SETTINGSPATH_H
#define GG_PLUGIN_SETTINGSPATH_H

#include <cstdlib>
#include <filesystem>

#include "gg/media/SettingsFile.h"

namespace gg::plugin
{
/// The per-machine settings file: $GG_SETTINGS_FILE when set and not empty, else the user's
/// (gg::media::defaultSettingsPath). See GigaGrainsPlugin's class comment.
inline std::filesystem::path settingsFile()
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
} // namespace gg::plugin

#endif // GG_PLUGIN_SETTINGSPATH_H
