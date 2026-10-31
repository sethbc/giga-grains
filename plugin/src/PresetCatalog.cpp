// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "PresetCatalog.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>

#include "gg/core/MachineSettings.h"
#include "gg/core/Preset.h"
#include "gg/core/SessionState.h"
#include "gg/media/ContentPaths.h"
#include "gg/media/SettingsFile.h"
#include "gg/media/Utf8Path.h"

#include "Favorites.h"
#include "PresetDiscovery.h"

namespace gg::plugin
{
namespace
{
namespace fs = std::filesystem;

/// `path` as UTF-8; empty if it can't be converted (a wide path that isn't valid UTF-16).
std::string utf8(const fs::path &path) { return media::toUtf8(path).value_or(std::string()); }

/// `text` with ASCII letters lowercased.
std::string lowered(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

/// Whether lowercased `needle` is in `text`, ignoring ASCII case.
bool contains(std::string_view text, const std::string &needle)
{
    return lowered(text).find(needle) != std::string::npos;
}

/// Whether `name` ends in the preset extension, in any ASCII case (the hosts' crawl matches it
/// so), with something before it.
bool hasPresetExtension(std::string_view name)
{
    const auto ext = core::kPresetExtension;
    return name.size() > ext.size() && lowered(name.substr(name.size() - ext.size())) == ext;
}

/// The preset files under `folder`, its subfolders included, sorted by their paths relative to
/// it (UTF-8, `/` separators) in byte order, at most PresetCatalog::kMaxFilesPerFolder of them;
/// the rest are added to `unlisted`. Only names are read, never a file. Nothing when the folder
/// is empty or can't be listed; a subfolder that can't be entered is skipped.
std::vector<fs::path> presetFiles(const fs::path &folder, std::size_t &unlisted)
{
    std::vector<std::pair<std::string, fs::path>> files;
    if (folder.empty())
    {
        return {};
    }
    std::error_code error;
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied,
                                             error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error))
    {
        std::error_code typeError;
        if (!it->is_regular_file(typeError))
        {
            continue;
        }
        const auto name = media::toGenericUtf8(it->path().filename());
        if (!name || !hasPresetExtension(*name))
        {
            continue;
        }
        auto relative = media::toGenericUtf8(it->path().lexically_relative(folder));
        if (!relative)
        {
            continue;
        }
        files.emplace_back(std::move(*relative), it->path());
    }
    // std::string compares its chars as unsigned char: byte order.
    std::sort(files.begin(), files.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    if (files.size() > PresetCatalog::kMaxFilesPerFolder)
    {
        unlisted += files.size() - PresetCatalog::kMaxFilesPerFolder;
        files.resize(PresetCatalog::kMaxFilesPerFolder);
    }
    std::vector<fs::path> paths;
    paths.reserve(files.size());
    for (auto &file : files)
    {
        paths.push_back(std::move(file.second));
    }
    return paths;
}

/// The metadata of the preset file `file`, read whole when it is at most kMaxStateBytes long.
bool readInfo(const fs::path &file, core::PresetInfo &info)
{
    std::error_code error;
    const auto size = fs::file_size(file, error);
    if (error || size > core::kMaxStateBytes)
    {
        return false;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        return false;
    }
    const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return bytes.size() <= core::kMaxStateBytes && core::presetInfoOf(bytes, info);
}

/// The content release's tag: the second word of the first line of the root's
/// content-version.txt (`giga-grains-content <tag> <sha256>`); empty without one.
std::string contentVersion(const fs::path &root)
{
    std::ifstream in(media::contentVersionFile(root), std::ios::binary);
    std::string line;
    if (!in || !std::getline(in, line))
    {
        return {};
    }
    std::istringstream words(line);
    std::string product;
    std::string tag;
    words >> product >> tag;
    return tag;
}

/// `path`'s modification time; nullopt when it doesn't exist or can't be read. One stat call.
std::optional<fs::file_time_type> modified(const fs::path &path)
{
    std::error_code error;
    const auto time = fs::last_write_time(path, error);
    return error ? std::nullopt : std::optional<fs::file_time_type>(time);
}

/// Removes every copy of `key` from the favorites file `file`; false if the save fails.
bool unfavorite(const fs::path &file, const std::string &key)
{
    auto keys = loadFavorites(file);
    std::erase(keys, key);
    return saveFavorites(file, keys);
}
} // namespace

std::string presetKey(const PresetEntry &entry)
{
    if (entry.kind == PresetKind::Init)
    {
        return "plugin:" + std::string(core::kInitLoadKey);
    }
    return presetFileKey(entry.file);
}

std::string presetFileKey(const fs::path &file)
{
    return "file:" + media::toGenericUtf8(file.lexically_normal()).value_or(std::string());
}

std::string safeFileName(std::string_view name)
{
    constexpr std::size_t kMaxBytes = 100;
    constexpr std::string_view kReserved = "/\\:*?\"<>|";
    std::string out(name);
    for (char &c : out)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7f || kReserved.find(c) != std::string_view::npos)
        {
            c = '_';
        }
    }
    if (!out.empty() && out.front() == '.')
    {
        out.front() = '_';
    }
    // Windows' device names, by the part before the first dot, in any case (M1): CON.ggpreset
    // would open the console there.
    {
        const std::string stem = lowered(std::string_view(out).substr(0, out.find('.')));
        const bool device =
            stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
             stem[3] >= '1' && stem[3] <= '9');
        if (device)
        {
            out.insert(out.begin(), '_');
        }
    }
    if (out.size() > kMaxBytes)
    {
        // Back up over continuation bytes (10xxxxxx) so the cut falls before a character.
        std::size_t cut = kMaxBytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xc0) == 0x80)
        {
            --cut;
        }
        out.resize(cut);
    }
    // Windows drops a trailing dot or space from a name: keep them as `_`.
    for (auto it = out.rbegin(); it != out.rend() && (*it == '.' || *it == ' '); ++it)
    {
        *it = '_';
    }
    return out;
}

PresetCatalog::PresetCatalog(core::Mode mode, std::filesystem::path settingsFile)
    : mode_(mode), settingsFile_(std::move(settingsFile))
{
    rescan();
}

void PresetCatalog::rescan()
{
    entries_.clear();
    PresetEntry init;
    init.kind = PresetKind::Init;
    init.info.name = "Init";
    init.info.product = mode_;
    entries_.push_back(std::move(init));

    const auto settings =
        settingsFile_.empty() ? core::MachineSettings{} : media::loadSettings(settingsFile_);
    std::optional<fs::path> factoryRoot;
    std::size_t unlisted = 0;
    for (const auto &location : presetLocations(settings, settingsFile_))
    {
        if (location.factory && !factoryRoot)
        {
            factoryRoot = location.root;
        }
        const PresetKind kind = location.factory         ? PresetKind::Factory
                                : location.contentFolder ? PresetKind::Content
                                                         : PresetKind::User;
        for (auto &file : presetFiles(location.folder, unlisted))
        {
            PresetEntry entry;
            if (readInfo(file, entry.info) && entry.info.product == mode_)
            {
                entry.kind = kind;
                entry.file = std::move(file);
                entries_.push_back(std::move(entry));
            }
        }
    }

    // What changedOnDisk watches (M4): the settings and favorites files, every content root's
    // Presets folder (one installed later included) and the user folder, by modification time.
    watched_.clear();
    const auto watch = [this](fs::path path) {
        if (!path.empty())
        {
            watched_.emplace_back(path, modified(path));
        }
    };
    watch(settingsFile_);
    watch(media::favoritesFile(settingsFile_));
    for (const auto &root : media::contentRoots(settings))
    {
        watch(media::factoryPresetsFolder(root));
    }
    watch(media::userPresetsFolder(settingsFile_));

    const auto keys = loadFavorites(media::favoritesFile(settingsFile_));
    const std::unordered_set<std::string> favorites(keys.begin(), keys.end());
    for (auto &entry : entries_)
    {
        entry.favorite = favorites.contains(presetKey(entry));
    }

    if (factoryRoot)
    {
        const std::string version = contentVersion(*factoryRoot);
        status_ = "Factory content " + (version.empty() ? std::string() : version + " ") + "in " +
                  utf8(*factoryRoot);
    }
    else if (!settings.contentFolder.empty())
    {
        status_ = "No factory content found";
    }
    else
    {
        status_ = "No factory content found: install it, or choose a Content folder";
    }
    if (!settings.contentFolder.empty())
    {
        status_ += "; Content folder: " + settings.contentFolder; // UTF-8 already
    }
    if (unlisted > 0)
    {
        status_ += " (" + std::to_string(unlisted) +
                   (unlisted == 1 ? " more file not listed)" : " more files not listed)");
    }
}

bool PresetCatalog::changedOnDisk() const
{
    return std::any_of(watched_.begin(), watched_.end(),
                       [](const auto &w) { return modified(w.first) != w.second; });
}

std::vector<std::size_t> PresetCatalog::filtered(std::string_view search,
                                                 std::span<const std::string> tags,
                                                 bool favoritesOnly) const
{
    const std::string needle = lowered(search);
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < entries_.size(); ++i)
    {
        const auto &entry = entries_[i];
        if (favoritesOnly && !entry.favorite)
        {
            continue;
        }
        const auto &own = entry.info.tags;
        if (!std::all_of(tags.begin(), tags.end(), [&](const std::string &tag) {
                return std::find(own.begin(), own.end(), tag) != own.end();
            }))
        {
            continue;
        }
        if (!needle.empty() && !contains(entry.info.name, needle) &&
            !contains(entry.info.description, needle) &&
            std::none_of(own.begin(), own.end(),
                         [&](const std::string &tag) { return contains(tag, needle); }))
        {
            continue;
        }
        out.push_back(i);
    }
    return out;
}

bool PresetCatalog::setFavorite(std::size_t index, bool on)
{
    if (index >= entries_.size())
    {
        return false;
    }
    const fs::path file = media::favoritesFile(settingsFile_);
    const std::string key = presetKey(entries_[index]);
    if (key == "file:")
    {
        return false; // a path with no UTF-8 form: its key would match every such entry
    }
    auto keys = loadFavorites(file);
    const bool had = std::find(keys.begin(), keys.end(), key) != keys.end();
    if (on && !had)
    {
        keys.push_back(key);
    }
    else if (!on)
    {
        std::erase(keys, key);
    }
    if (!saveFavorites(file, keys))
    {
        return false;
    }
    entries_[index].favorite = on;
    return true;
}

SaveResult PresetCatalog::save(const core::PresetInfo &info, const std::string &stateBytes,
                               bool overwrite)
{
    const auto fail = [](std::string error) {
        return SaveResult{SaveResult::Status::Failed, std::move(error), {}, {}};
    };
    const std::string base = safeFileName(info.name);
    if (base.empty())
    {
        return fail("the name is empty");
    }
    const std::string failed = "can't save " + info.name + ": ";
    const fs::path folder = media::userPresetsFolder(settingsFile_);
    if (folder.empty())
    {
        return fail(failed + "there is no user presets folder");
    }
    const auto name = media::fromUtf8(base + std::string(core::kPresetExtension));
    if (!name)
    {
        return fail(failed + "the name isn't valid UTF-8");
    }
    const fs::path file = folder / *name;
    std::error_code error;
    fs::create_directories(folder, error);
    if (error)
    {
        return fail(failed + "can't create the folder " + utf8(folder) + ": " + error.message());
    }
    const bool exists = fs::exists(file, error);
    if (error)
    {
        return fail(failed + "can't check for " + utf8(file) + ": " + error.message());
    }
    if (!overwrite && exists)
    {
        // Named by what the file holds (M2): "a/b" and "a:b" make one file.
        core::PresetInfo there;
        std::string existing = readInfo(file, there) ? there.name : media::displayName(file);
        return SaveResult{SaveResult::Status::Exists,
                          "a preset named " + existing + " exists",
                          std::move(existing),
                          {}};
    }
    if (const auto written = writeReplacing(file, stateBytes))
    {
        return fail(failed + written.message());
    }
    rescan();
    return SaveResult{SaveResult::Status::Saved, {}, {}, file};
}

bool PresetCatalog::remove(std::size_t index)
{
    if (index >= entries_.size() || entries_[index].kind != PresetKind::User)
    {
        return false;
    }
    std::error_code error;
    if (!fs::remove(entries_[index].file, error) || error)
    {
        return false;
    }
    if (entries_[index].favorite)
    {
        // Best effort: the preset is gone either way, and a stale key matches nothing.
        (void)unfavorite(media::favoritesFile(settingsFile_), presetKey(entries_[index]));
    }
    rescan();
    return true;
}

std::vector<std::string> PresetCatalog::tagsInUse() const
{
    std::vector<std::string> out;
    for (const auto word : core::presetTagVocabulary())
    {
        const bool used = std::any_of(entries_.begin(), entries_.end(), [&](const auto &entry) {
            const auto &tags = entry.info.tags;
            return std::find(tags.begin(), tags.end(), word) != tags.end();
        });
        if (used)
        {
            out.emplace_back(word);
        }
    }
    return out;
}
} // namespace gg::plugin
