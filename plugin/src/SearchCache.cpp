// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "SearchCache.h"

#include <algorithm>

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
                                          const core::MachineSettings &settings)
{
    // As findSample makes them (gg/media/Search.h): a candidate it can't make, or one that isn't
    // absolute, stamps as no file.
    Stamps stamps;
    if (const auto saved = media::fromUtf8(entry.absolutePath))
    {
        stamps.saved = stampOf(*saved);
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
    return stamps;
}

media::SearchResult SearchCache::find(const core::MediaEntry &entry,
                                      const core::MachineSettings &settings, Clock::time_point now)
{
    const Stamps stamps = stampsOf(entry, settings);
    std::lock_guard lock(mutex_);
    // Expired searches go first, so the list holds only the samples missing just now.
    std::erase_if(misses_, [now](const Miss &m) { return now - m.when >= kLifetime; });
    const auto known = std::find_if(misses_.begin(), misses_.end(), [&](const Miss &m) {
        return m.entry == entry && m.settings == settings;
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
    media::SearchResult result = media::findSample(entry, settings);
    if (result.outcome != media::SearchOutcome::Found)
    {
        misses_.push_back({entry, settings, stamps, result, now});
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
