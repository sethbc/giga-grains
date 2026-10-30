// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_PRESETCATALOG_H
#define GG_PLUGIN_PRESETCATALOG_H

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gg/core/ParamIds.h"
#include "gg/core/PresetInfo.h"

/// The preset browser's model (the presets spec §6.8): the list a product's browser shows, built
/// on the main thread from the folders the preset-discovery provider declares, never from
/// clap-wrapper's index, with the favorites merged in; and Save as…, Delete and the favorite star
/// on it. Main thread only: everything here reads or writes files.
namespace gg::plugin
{
enum class PresetKind
{
    /// The plugin's defaults (gg::core::initState), which have no file.
    Init,
    /// A preset in a content root's Presets folder (gg::media::factoryPresetsFolder).
    Factory,
    /// A preset in the user presets folder (gg::media::userPresetsFolder).
    User
};

struct PresetEntry
{
    PresetKind kind{PresetKind::Init};
    /// The preset file; empty for INIT.
    std::filesystem::path file;
    /// The file's metadata (gg::core::presetInfoOf). INIT's is the name "Init", the catalog's
    /// product and nothing else.
    core::PresetInfo info;
    bool favorite{false};
};

/// The entry's favorites key (Favorites.h): "plugin:init" for INIT, else "file:" and the file's
/// path as UTF-8 with `/` separators (empty after "file:" if the path can't be converted).
std::string presetKey(const PresetEntry &entry);

/// `name` made a file name that stays in its folder on every platform: each of `/ \ : * ? " < >
/// |` and each control character (below U+0020, and U+007F) becomes `_`, as does a leading `.`
/// (no hidden file, no `.` or `..`); the result is cut to at most 100 bytes on a UTF-8
/// boundary. Empty only when `name` is.
std::string safeFileName(std::string_view name);

/// The presets one product's browser lists (D9, option 1: its own product's alone), in order:
/// INIT; then each factory location's presets, each folder's by file name; then the user
/// folder's, likewise. A folder's `*.ggpreset` files (the extension in that case) are sorted by
/// their UTF-8 names in byte order and at most kMaxFilesPerFolder of them are listed, before any
/// is read (the rest are counted into contentStatus); a listed file is kept when presetInfoOf
/// reads it (within kMaxStateBytes) and its product is the catalog's. A folder that can't be
/// listed, and a file that can't be read, are left out.
class PresetCatalog
{
  public:
    static constexpr std::size_t kMaxFilesPerFolder = 5000;

    /// A catalog of `mode`'s presets, for the settings file `settingsFile` (which gives the
    /// content folder, the user presets folder and the favorites file; empty for none), scanned
    /// at once.
    PresetCatalog(core::Mode mode, std::filesystem::path settingsFile);

    /// Reads the settings, the folders and the favorites again.
    void rescan();

    const std::vector<PresetEntry> &entries() const { return entries_; }

    /// The indices of the entries that match, in the list's order: `search`, when not empty, a
    /// substring of the name, the description or a tag, ignoring ASCII case; every one of `tags`
    /// among the entry's (exactly); and the entry a favorite when `favoritesOnly`.
    std::vector<std::size_t> filtered(std::string_view search, std::span<const std::string> tags,
                                      bool favoritesOnly) const;

    /// Stars (or unstars) entry `index` and saves the favorites file, keeping the keys it holds
    /// for other entries and products. False, and nothing changed, for an index out of range or
    /// a save that fails.
    bool setFavorite(std::size_t index, bool on);

    /// Save as…: writes `stateBytes` (the preset file, gg::core::encodePreset of `info`) to the
    /// user presets folder as `safeFileName(info.name)` + ".ggpreset", through a temporary file
    /// renamed over it, the folder created first, then rescans. Returns "" on success, else the
    /// error for the user: "the name is empty", "a preset named <info.name> exists" when the file
    /// exists and `overwrite` is false, or why the write failed.
    std::string save(const core::PresetInfo &info, const std::string &stateBytes, bool overwrite);

    /// Delete: removes a User entry's file (and its favorite), then rescans. False, and nothing
    /// changed, for INIT, a factory preset, an index out of range, or a file that can't be removed.
    bool remove(std::size_t index);

    /// The browser's bottom line: "Factory content <tag> in <root>" for the first factory
    /// location (the tag the second word of the first line of the root's content-version.txt),
    /// "Factory content in <root>" when that file has none, or "No factory content found:
    /// install it, or choose a Content folder"; followed by " (N more files not listed)" (" (1
    /// more file not listed)") when folders held more than kMaxFilesPerFolder.
    std::string contentStatus() const { return status_; }

    /// The tags the listed presets carry, in the vocabulary's order (presetTagVocabulary).
    std::vector<std::string> tagsInUse() const;

  private:
    core::Mode mode_;
    std::filesystem::path settingsFile_;
    std::vector<PresetEntry> entries_;
    std::string status_;
};
} // namespace gg::plugin

#endif // GG_PLUGIN_PRESETCATALOG_H
