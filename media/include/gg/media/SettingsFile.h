// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_SETTINGSFILE_H
#define GG_MEDIA_SETTINGSFILE_H

#include <filesystem>

#include "gg/core/MachineSettings.h"

/// The per-machine settings file (spec §6.7): gg-core's settings XML
/// (gg/core/MachineSettings.h), read and written here. loadSettings and saveSettings are main
/// thread only (ruling R16): the plugin reads and saves the user's file from its main thread,
/// never from the loader. Every function but defaultSettingsPath takes the file explicitly, so
/// tests never touch the user's.
namespace gg::media
{
/// The settings file in the user's configuration folder:
/// - macOS: ~/Library/Application Support/Giga Grains/settings.xml;
/// - Windows: %APPDATA%\Giga Grains\settings.xml;
/// - Linux: $XDG_CONFIG_HOME/giga-grains/settings.xml, or ~/.config/giga-grains/settings.xml when
///   XDG_CONFIG_HOME is unset, empty or relative (as the XDG base directory spec says).
/// `~` is $HOME, or the user's home directory from the password database when HOME is unset or
/// empty. Empty when none of these can be found. Reads the environment, nothing else.
std::filesystem::path defaultSettingsPath();

/// The settings in `file`, or the defaults (both folders empty) if it can't be read, is larger
/// than 1 MiB, or isn't settings (fromSettingsXml).
core::MachineSettings loadSettings(const std::filesystem::path &file);

/// Writes `settings` to `file`, creating its folder if needed. The XML goes to a temporary file
/// beside it, which is then renamed over `file`, so a failed write leaves the old settings
/// whole. False if a folder or the file can't be written, or if a string holds a NUL byte.
bool saveSettings(const std::filesystem::path &file, const core::MachineSettings &settings);
} // namespace gg::media

#endif // GG_MEDIA_SETTINGSFILE_H
