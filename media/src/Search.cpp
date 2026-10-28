// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/media/Search.h"

#include <algorithm>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "Utf8Path.h"
#include "gg/media/Sha256.h"

namespace gg::media
{
namespace fs = std::filesystem;

namespace
{
/// What checking one candidate found.
enum class Check
{
    Skipped, // relative, or already checked
    NoFile,  // no regular file there, or one that can't be read
    Differs, // a regular file of another size or SHA-256
    Matches, // the entry's file
};

/// The candidates one search has checked, and the check itself.
class Candidates
{
  public:
    explicit Candidates(const core::MediaEntry &entry) : entry_(entry) {}

    Check check(const fs::path &candidate)
    {
        if (!candidate.is_absolute())
        {
            return Check::Skipped;
        }
        const fs::path key = candidate.lexically_normal();
        if (std::find(checked_.begin(), checked_.end(), key) != checked_.end())
        {
            return Check::Skipped;
        }
        checked_.push_back(key);

        std::error_code error;
        if (!fs::is_regular_file(candidate, error))
        {
            return Check::NoFile;
        }
        const auto size = fs::file_size(candidate, error);
        if (error)
        {
            return Check::NoFile;
        }
        if (size != entry_.fileSize)
        {
            return Check::Differs;
        }
        const auto hash = hashFile(candidate);
        if (!hash)
        {
            return Check::NoFile;
        }
        return hash->size == entry_.fileSize && hash->sha256Hex == entry_.sha256Hex
                   ? Check::Matches
                   : Check::Differs;
    }

  private:
    const core::MediaEntry &entry_;
    std::vector<fs::path> checked_;
};

/// The index of the saved path's last separator, `/` or `\` (a path saved on Windows), or npos.
std::size_t lastSeparator(const std::string &path) { return path.find_last_of("/\\"); }
} // namespace

SearchResult findSample(const core::MediaEntry &entry, const core::MachineSettings &settings)
{
    Candidates candidates(entry);
    const fs::path saved = detail::fromUtf8(entry.absolutePath).value_or(fs::path());
    const auto found = [&](const fs::path &candidate) {
        const fs::path where = candidate.lexically_normal();
        return SearchResult{SearchOutcome::Found, where, where != saved.lexically_normal()};
    };

    // 1. The saved path.
    const Check atSaved = saved.empty() ? Check::Skipped : candidates.check(saved);
    if (atSaved == Check::Matches)
    {
        return found(saved);
    }

    // 2. The sample folder and the relative path.
    const auto sampleFolder =
        settings.sampleFolder.empty() ? std::nullopt : detail::fromUtf8(settings.sampleFolder);
    if (sampleFolder && !entry.relativePath.empty())
    {
        if (const auto relative = detail::fromUtf8(entry.relativePath))
        {
            const fs::path candidate = *sampleFolder / *relative;
            if (candidates.check(candidate) == Check::Matches)
            {
                return found(candidate);
            }
        }
    }

    // 3. By name: in the saved path's folder, the last folder loaded from, then the scan.
    const std::size_t separator = lastSeparator(entry.absolutePath);
    const auto name =
        detail::fromUtf8(separator == std::string::npos ? entry.absolutePath
                                                        : entry.absolutePath.substr(separator + 1));
    if (name && !name->empty() && *name != "." && *name != "..")
    {
        if (separator != std::string::npos)
        {
            if (const auto folder = detail::fromUtf8(entry.absolutePath.substr(0, separator + 1)))
            {
                const fs::path candidate = *folder / *name;
                if (candidates.check(candidate) == Check::Matches)
                {
                    return found(candidate);
                }
            }
        }
        if (!settings.lastLoadFolder.empty())
        {
            if (const auto folder = detail::fromUtf8(settings.lastLoadFolder))
            {
                const fs::path candidate = *folder / *name;
                if (candidates.check(candidate) == Check::Matches)
                {
                    return found(candidate);
                }
            }
        }
        if (sampleFolder && sampleFolder->is_absolute())
        {
            // The default options don't follow symlinked folders.
            std::error_code error;
            fs::recursive_directory_iterator it(
                *sampleFolder, fs::directory_options::skip_permission_denied, error);
            std::size_t visited = 0;
            for (; !error && it != fs::recursive_directory_iterator(); it.increment(error))
            {
                if (++visited > kMaxScanEntries)
                {
                    break;
                }
                const fs::path &candidate = it->path();
                if (candidate.filename() == *name && candidates.check(candidate) == Check::Matches)
                {
                    return found(candidate);
                }
            }
        }
    }

    if (atSaved == Check::Differs)
    {
        return {SearchOutcome::Changed, saved, false};
    }
    return {SearchOutcome::Missing, {}, false};
}
} // namespace gg::media
