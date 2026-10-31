// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_PRESETCATALOG_H
#define GG_PLUGIN_PRESETCATALOG_H

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
    /// A preset of factory content: the Presets folder of GG_CONTENT_DIR or an install
    /// (gg::media::factoryPresetsFolder).
    Factory,
    /// A preset in the user presets folder (gg::media::userPresetsFolder): the only kind Delete
    /// removes.
    User,
    /// A preset in the Presets folder of the user's content folder ("Content folder…"): a pack of
    /// the user's own, neither factory content nor deletable here (fix wave M6).
    Content
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

/// The entry's favorites key (Favorites.h): "plugin:init" for INIT, else presetFileKey(file).
std::string presetKey(const PresetEntry &entry);
/// A preset file's favorites key: "file:" and its path as UTF-8 with `/` separators (empty after
/// "file:" if the path can't be converted). The discovery provider reports IS_FAVORITE by it.
std::string presetFileKey(const std::filesystem::path &file);

/// What PresetCatalog::save did.
struct SaveResult
{
    enum class Status
    {
        Saved,
        Exists, ///< a file of that name is there, and overwrite was false
        Failed,
    };
    Status status{Status::Failed};
    /// For the user: Exists's "a preset named <existingName> exists", or why the save failed.
    std::string error;
    /// Exists: the name the file there holds (presetInfoOf), or its file name when it can't be
    /// read, so a prompt names what an overwrite would replace (two names can make one file).
    std::string existingName;
    /// Saved: the file written.
    std::filesystem::path file;
    bool saved() const noexcept { return status == Status::Saved; }
};

/// `name` made a file name that stays in its folder on every platform: each of `/ \ : * ? " < >
/// |` and each control character (below U+0020, and U+007F) becomes `_`, as does a leading `.`
/// (no hidden file, no `.` or `..`); a name whose part before its first dot is one of Windows'
/// device names (CON, PRN, AUX, NUL, COM1 to COM9, LPT1 to LPT9, in any case) gets a `_` in
/// front; the result is cut to at most 100 bytes on a UTF-8 boundary, and then each trailing `.`
/// or space (which Windows drops) becomes `_`. Empty only when `name` is.
std::string safeFileName(std::string_view name);

/// The presets one product's browser lists (D9, option 1: its own product's alone), in order:
/// INIT; then each content location's presets (presetLocations' order), then the user folder's.
/// A folder is walked with its subfolders, as the hosts' crawl walks a declared location
/// (clap-wrapper's preset_discovery.cpp); its files whose extension is `.ggpreset` in any case are
/// sorted by their paths relative to the folder, UTF-8 with `/` separators, in byte order, and at
/// most kMaxFilesPerFolder of them are listed, before any is read (the rest are counted into
/// contentStatus); a listed file is kept when presetInfoOf reads it (within kMaxStateBytes) and
/// its product is the catalog's. A folder that can't be listed, a subfolder that can't be
/// entered, and a file that can't be read, are left out.
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
    /// Whether anything the last rescan read may have changed on disk (M4, the browser's 30 Hz
    /// poll): the modification time, or the existence, of the settings file, the favorites file,
    /// each content root's Presets folder or the user presets folder. Only stats them, reading no
    /// file. A change inside a subfolder alone doesn't move its folder's time: it shows at the next
    /// rescan (the browser's opening, a save, a delete).
    bool changedOnDisk() const;

    const std::vector<PresetEntry> &entries() const { return entries_; }

    /// The indices of the entries that match, in the list's order: `search`, when not empty, a
    /// substring of the name, the description or a tag, ignoring ASCII case; every one of `tags`
    /// among the entry's (exactly); and the entry a favorite when `favoritesOnly`.
    std::vector<std::size_t> filtered(std::string_view search, std::span<const std::string> tags,
                                      bool favoritesOnly) const;

    /// Stars (or unstars) entry `index` and saves the favorites file, keeping the keys it holds
    /// for other entries and products. False, and nothing changed, for an index out of range or
    /// a save that fails, or an entry whose path has no UTF-8 form (its key would match every
    /// such entry).
    bool setFavorite(std::size_t index, bool on);

    /// Save as…: writes `stateBytes` (the preset file, gg::core::encodePreset of `info`) to the
    /// user presets folder as `safeFileName(info.name)` + ".ggpreset", through a temporary file
    /// renamed over it, the folder created first, then rescans. Exists when the file is there and
    /// `overwrite` is false (see SaveResult); Failed with the error for the user ("the name is
    /// empty", or why the write failed).
    SaveResult save(const core::PresetInfo &info, const std::string &stateBytes, bool overwrite);

    /// Delete: removes a User entry's file (and its favorite), then rescans. False, and nothing
    /// changed, for INIT, a factory preset, an index out of range, or a file that can't be removed.
    bool remove(std::size_t index);

    /// The browser's bottom line: "Factory content <tag> in <root>" for the first factory
    /// location (the tag the second word of the first line of the root's content-version.txt),
    /// "Factory content in <root>" when that file has none, or, without one, "No factory content
    /// found: install it, or choose a Content folder" ("No factory content found" when a content
    /// folder is set); then "; Content folder: <folder>" when the settings name one (M6); then
    /// " (N more files not listed)" (" (1 more file not listed)") when folders held more than
    /// kMaxFilesPerFolder.
    std::string contentStatus() const { return status_; }

    /// The tags the listed presets carry, in the vocabulary's order (presetTagVocabulary).
    std::vector<std::string> tagsInUse() const;

  private:
    core::Mode mode_;
    std::filesystem::path settingsFile_;
    std::vector<PresetEntry> entries_;
    std::string status_;
    std::vector<std::pair<std::filesystem::path, std::optional<std::filesystem::file_time_type>>>
        watched_; // changedOnDisk's paths and their times at the last rescan
};
} // namespace gg::plugin

#endif // GG_PLUGIN_PRESETCATALOG_H
