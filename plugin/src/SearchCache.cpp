// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "SearchCache.h"

#include <algorithm>
#include <optional>
#include <string>

#include "gg/media/SettingsFile.h"
#include "gg/media/Utf8Path.h"

namespace gg::plugin
{
SearchCache &SearchCache::process()
{
    static SearchCache cache;
    return cache;
}

SearchCache::Stamp SearchCache::stampOf(const std::filesystem::path &path) noexcept
{
    // A relative path (a Windows path read elsewhere) names nothing findSample would read.
    std::error_code error;
    const auto status =
        path.is_absolute() ? std::filesystem::status(path, error) : std::filesystem::file_status{};
    if (error || !std::filesystem::is_regular_file(status))
    {
        return {};
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error)
    {
        return {};
    }
    const auto written = std::filesystem::last_write_time(path, error);
    return {true, size, error ? std::filesystem::file_time_type{} : written, status.permissions()};
}

SearchCache::Stamps SearchCache::stampsOf(const core::MediaEntry &entry,
                                          const core::MachineSettings &settings,
                                          std::span<const std::filesystem::path> contentRoots)
{
    // As findSample makes them (gg/media/Search.h): a candidate it can't make, or one that isn't
    // absolute, stamps as no file.
    Stamps stamps;
    if (const auto saved = media::fromUtf8(entry.absolutePath))
    {
        stamps.saved = stampOf(*saved);
    }
    if (!entry.contentPath.empty())
    {
        const auto content = media::fromUtf8(entry.contentPath);
        for (const auto &root : contentRoots)
        {
            stamps.inRoots.push_back(content ? stampOf(root / *content) : Stamp{});
        }
    }
    if (!settings.sampleFolder.empty() && !entry.relativePath.empty())
    {
        const auto folder = media::fromUtf8(settings.sampleFolder);
        const auto relative = media::fromUtf8(entry.relativePath);
        if (folder && relative)
        {
            stamps.inFolder = stampOf(*folder / *relative);
        }
    }
    // The captures folder and the saved path's name, after either separator, as findSample
    // takes them.
    const auto separator = entry.absolutePath.find_last_of("/\\");
    const std::string name = separator == std::string::npos
                                 ? entry.absolutePath
                                 : entry.absolutePath.substr(separator + 1);
    // Plain values, not an optional built in a ternary (GCC 13's -Wmaybe-uninitialized).
    std::filesystem::path captures = media::defaultCapturesFolder();
    if (!settings.capturesFolder.empty())
    {
        captures = media::fromUtf8(settings.capturesFolder).value_or(std::filesystem::path());
    }
    const auto fileName = media::fromUtf8(name);
    if (!name.empty() && fileName && !captures.empty())
    {
        stamps.inCaptures = stampOf(captures / *fileName);
    }
    return stamps;
}

media::SearchResult SearchCache::find(const core::MediaEntry &entry,
                                      const core::MachineSettings &settings, Clock::time_point now,
                                      std::span<const std::filesystem::path> contentRoots)
{
    const Stamps stamps = stampsOf(entry, settings, contentRoots);
    std::lock_guard lock(mutex_);
    // Expired searches go first, so the list holds only the samples missing just now.
    std::erase_if(misses_, [now](const Miss &m) { return now - m.when >= kLifetime; });
    const auto known = std::find_if(misses_.begin(), misses_.end(), [&](const Miss &m) {
        return m.entry == entry && m.settings == settings &&
               std::equal(m.roots.begin(), m.roots.end(), contentRoots.begin(), contentRoots.end());
    });
    if (known != misses_.end())
    {
        if (known->stamps == stamps)
        {
            return known->result;
        }
        misses_.erase(known); // a candidate holds something else now: searched again
    }
    ++searches_;
    media::SearchResult result = media::findSample(entry, settings, contentRoots);
    if (result.outcome != media::SearchOutcome::Found)
    {
        misses_.push_back(
            {entry, settings, {contentRoots.begin(), contentRoots.end()}, stamps, result, now});
    }
    return result;
}

void SearchCache::clear()
{
    std::lock_guard lock(mutex_);
    misses_.clear();
}

uint64_t SearchCache::searches() const
{
    std::lock_guard lock(mutex_);
    return searches_;
}
} // namespace gg::plugin
