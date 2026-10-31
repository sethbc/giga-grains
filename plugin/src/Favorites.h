// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_FAVORITES_H
#define GG_PLUGIN_FAVORITES_H

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

/// The per-machine favorites file (the presets spec §6.4; gg::media::favoritesFile names it): one
/// key per line, UTF-8, LF. A key names a preset: "plugin:init" for INIT, or "file:" and the
/// preset file's path as UTF-8 with `/` separators (presetKey in PresetCatalog.h makes them).
/// Both products share the file, so a key either doesn't know is kept as it is. Main thread
/// only: these read and write files.
namespace gg::plugin
{
/// The keys in `file`, in its order: each line without a CR before its LF, blank lines (white
/// space alone) dropped. Empty when the file is missing or can't be read.
std::vector<std::string> loadFavorites(const std::filesystem::path &file);

/// Writes `keys` to `file`, one per line, each ended by LF (an empty key is left out), through a
/// temporary file beside it (`<file>.tmp-<process ID>-<count>`) renamed over it, so a reader
/// sees the old file or the new one whole; the file's folder is created first. False, and the
/// file untouched, when `file` is empty, a key holds a CR or an LF (it would read back as two),
/// or the folder, the temporary file or the rename fails.
bool saveFavorites(const std::filesystem::path &file, std::span<const std::string> keys);

/// Writes `bytes` to `file` (whose folder exists) as saveFavorites does, through a temporary file
/// beside it (`<file>.tmp-<process ID>-<count>`, which never ends in a preset's extension)
/// renamed over it, and removes the temporary file when anything fails. Shared with the
/// catalog's Save as… (PresetCatalog::save). The error, or none.
std::error_code writeReplacing(const std::filesystem::path &file, std::string_view bytes);
} // namespace gg::plugin

#endif // GG_PLUGIN_FAVORITES_H
