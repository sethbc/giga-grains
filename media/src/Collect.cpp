// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/Collect.h"

#include <atomic>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>

#include "TestHooks.h"
#include "gg/media/Search.h"
#include "gg/media/Sha256.h"
#include "gg/media/Utf8Path.h"

namespace gg::media
{
namespace fs = std::filesystem;

namespace
{
/// The collect hook (TestHooks.h); nullptr unless a test sets one.
std::atomic<detail::CollectHook> collectHook{nullptr};

/// One file to collect: the fingerprint to match, and where it was saved.
struct Wanted
{
    const std::string &sha256Hex;
    uint64_t size;
    const std::string &savedPath;
};

/// Whether the regular file at `path` has the wanted content.
bool hasContent(const fs::path &path, const Wanted &wanted)
{
    std::error_code error;
    if (!fs::is_regular_file(path, error))
    {
        return false;
    }
    const auto size = fs::file_size(path, error);
    if (error || size != wanted.size)
    {
        return false;
    }
    const auto hash = hashFile(path);
    return hash && hash->size == wanted.size && hash->sha256Hex == wanted.sha256Hex;
}

/// Puts a copy of `from` in `folder` and returns its path, or nullopt when that can't be done.
/// `copied` says whether a file was written: a file already there with the content is reused.
std::optional<fs::path> collectOne(const fs::path &from, const fs::path &folder,
                                   const Wanted &wanted, bool &copied)
{
    copied = false;
    const fs::path name = from.filename();
    std::error_code error;

    fs::path target = folder / name;
    if (fs::exists(target, error))
    {
        // Already there: this very file, or another with the content.
        if (hasContent(target, wanted))
        {
            return target;
        }
        // Another file of this name, which is never overwritten: ours takes the hash's prefix.
        target = folder /
                 (name.stem().native() + fs::path("-" + wanted.sha256Hex.substr(0, 8)).native() +
                  name.extension().native());
        if (fs::exists(target, error))
        {
            return hasContent(target, wanted) ? std::optional<fs::path>(target) : std::nullopt;
        }
    }
    fs::copy_file(from, target, fs::copy_options::none, error);
    if (error)
    {
        return std::nullopt;
    }
    if (const auto hook = collectHook.load(std::memory_order_relaxed))
    {
        hook(target);
    }
    if (!hasContent(target, wanted))
    {
        fs::remove(target, error);
        return std::nullopt;
    }
    copied = true;
    return target;
}
} // namespace

CollectResult collectMedia(const core::SessionState &state, const fs::path &folder,
                           const core::MachineSettings &settings,
                           std::span<const fs::path> contentRoots)
{
    CollectResult result;
    result.state = state;

    std::error_code error;
    fs::create_directories(folder, error);
    const bool folderReady = fs::is_directory(folder, error);

    std::set<std::string> listed; // the saved paths already in `missing`

    // Collects one entry's file: the copy's path (UTF-8), or nullopt, with the entry listed.
    const auto collect = [&](const SearchResult &found,
                             const Wanted &wanted) -> std::optional<std::string> {
        if (found.outcome == SearchOutcome::Found && folderReady)
        {
            bool copied = false;
            if (const auto target = collectOne(found.path, folder, wanted, copied))
            {
                if (const auto text = toUtf8(target->lexically_normal()))
                {
                    if (copied)
                    {
                        result.copied.push_back(*text);
                    }
                    return *text;
                }
            }
        }
        if (listed.insert(wanted.savedPath).second)
        {
            result.missing.push_back(wanted.savedPath);
        }
        return std::nullopt;
    };

    for (auto &slot : result.state.samples)
    {
        if (!slot || slot->embed)
        {
            continue;
        }
        core::MediaEntry &entry = *slot;
        const SearchResult found = findSample(entry, settings, contentRoots);
        const Wanted wanted{entry.sha256Hex, entry.fileSize, entry.absolutePath};
        if (const auto path = collect(found, wanted))
        {
            const auto name = fromUtf8(*path);
            entry.relativePath =
                name ? toGenericUtf8(name->filename()).value_or(std::string()) : std::string();
            entry.absolutePath = *path;
        }
    }
    for (auto &slot : result.state.rings)
    {
        if (!slot || slot->embed)
        {
            continue;
        }
        core::RingEntry &entry = *slot;
        const SearchResult found =
            findFiledAudio(entry.sha256Hex, entry.fileSize, entry.absolutePath, settings);
        const Wanted wanted{entry.sha256Hex, entry.fileSize, entry.absolutePath};
        if (const auto path = collect(found, wanted))
        {
            entry.absolutePath = *path;
        }
    }
    return result;
}

namespace detail
{
void setCollectHook(CollectHook hook) { collectHook.store(hook); }
} // namespace detail
} // namespace gg::media
