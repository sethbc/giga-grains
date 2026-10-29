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
/// than 1 MiB, or isn't settings (fromSettingsXml). On Windows the file is opened sharing delete
/// access, so another instance's saveSettings can rename over it meanwhile.
core::MachineSettings loadSettings(const std::filesystem::path &file);

/// Writes `settings` to `file`, creating its folder if needed. The XML goes to a temporary file
/// beside it, unique to the save (ruling R16), which is flushed to the disk (fsync; F_FULLFSYNC
/// on macOS; FlushFileBuffers on Windows) and then renamed over `file`, and the folder is flushed
/// after it (on Windows NTFS journals the rename, so a crash leaves the old file or the new one
/// whole, though the rename is not necessarily durable when MoveFileExW returns): a failed
/// write, a crash or a power loss leaves the old settings or the new ones whole, never a partial
/// file. A rename that fails
/// because another program has `file` open (on Windows, which refuses to replace an open file)
/// is tried again, 20 times in all, 25 ms apart. Once saved, it removes the temporary files of
/// saves of `file` that crashed before their rename: those more than 10 minutes older than the
/// file it has just written, so another instance's save in progress is never touched. False if
/// a folder or the file can't be written, or if a string holds a NUL byte.
bool saveSettings(const std::filesystem::path &file, const core::MachineSettings &settings);
} // namespace gg::media

#endif // GG_MEDIA_SETTINGSFILE_H
