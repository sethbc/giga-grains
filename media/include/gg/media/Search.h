// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_SEARCH_H
#define GG_MEDIA_SEARCH_H

#include <cstddef>
#include <filesystem>

#include "gg/core/MachineSettings.h"
#include "gg/core/SessionState.h"

/// Finding a manifest's sample when a state loads (spec §6.7). Not real-time safe: it lists
/// folders and hashes files, on the main thread, inside stateLoad.
namespace gg::media
{
enum class SearchOutcome
{
    Found,   // `path` holds the file's content
    Missing, // nothing matched
    Changed, // nothing matched, and the file at the saved path has other content
};

struct SearchResult
{
    SearchOutcome outcome;
    std::filesystem::path path; // Found: the match; Changed: the saved path; Missing: empty.
                                // Lexically normal.
    bool moved;                 // Found away from the saved path: the manifest needs updating
};

/// The most directory entries findSample's recursive scan of the sample folder visits.
inline constexpr std::size_t kMaxScanEntries = 50'000;

/// Looks for `entry`'s file. Each candidate must be a regular file (or a symlink to one) whose
/// size, then SHA-256, are the entry's; the first one that is is Found. The candidates, in order:
/// 1. `entry.absolutePath`;
/// 2. `settings.sampleFolder / entry.relativePath`, when both are non-empty;
/// 3. a file of the saved path's name (its last component, after either `/` or `\`, so a path
///    saved on Windows still gives its name elsewhere):
///    a. in the saved path's folder;
///    b. in `settings.lastLoadFolder`;
///    c. anywhere under `settings.sampleFolder`, recursively. The scan stops after visiting
///       kMaxScanEntries directory entries (files and folders alike, the match included), never
///       follows a symlinked folder, and skips any folder it can't open or finish listing
///       (permissions, a folder removed mid-scan, a failing volume), going on with the rest.
/// Names are Unicode text, compared up to canonical equivalence: a name saved in NFC is found
/// under its NFD spelling (as macOS's HFS+ stores names) and the other way round. So steps 1,
/// 2, 3a and 3b try the saved spelling of what they add (the whole path in 1, the relative path
/// in 2, the name in 3a and 3b), then its NFC and its NFD spellings, and the scan compares each
/// name it lists with the saved one in NFC. A name that isn't valid UTF-8 matches only itself.
/// A candidate that isn't an absolute path (a Windows path read on macOS, say) is skipped, since
/// it would depend on the working directory, and one already checked isn't checked again. So 3a,
/// whose candidate is 1's path or a relative one, adds nothing: it is kept for the spec's order
/// (ruling R13). `moved` is whether the match's path differs from the saved one. When nothing
/// matches, the outcome is Changed if a spelling of the saved path holds a regular file whose
/// fingerprint (read in full) differs, and Missing otherwise. A Found or Changed path is
/// lexically normal (no `.` or `..` components). Paths are UTF-8; filesystem errors count as
/// non-matches, and nothing throws but std::bad_alloc.
SearchResult findSample(const core::MediaEntry &entry, const core::MachineSettings &settings);
} // namespace gg::media

#endif // GG_MEDIA_SEARCH_H
