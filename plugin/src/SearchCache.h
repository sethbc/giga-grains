// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_SEARCHCACHE_H
#define GG_PLUGIN_SEARCHCACHE_H

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <vector>

#include "gg/core/MachineSettings.h"
#include "gg/core/SessionState.h"
#include "gg/media/Search.h"

namespace gg::plugin
{
/// The searches for a restored session's sample that found nothing (gg::media::findSample, spec
/// §6.7), remembered per process (carry-over [192]). A project of many instances whose sample is
/// missing would otherwise scan the sample folder, up to kMaxScanEntries entries, once per
/// instance as it opens.
///
/// A search that finds nothing (Missing or Changed) is remembered by its entry, the settings'
/// folders (the sample folder, the last load folder and the captures folder) and the content
/// roots it was given (M2 sub-project 2's spec §6.7), with what the search's direct candidates
/// held then: the saved path, each content root joined with the entry's content path, the sample
/// folder joined with the entry's relative path, and the captures folder joined with the saved
/// path's name (whether a file was there, its size, modification time and permissions; each path
/// as saved, not in its other Unicode spellings). A search of the same entry with the same
/// folders and roots gets its result again, without searching,
/// for kLifetime after it, as long as both hold what they held: a file put back at either, or
/// changed there, is searched for again at once. A search that finds the file is never
/// remembered. clear() forgets every one: SampleLoader calls it after a load or relink applies
/// and after the sample folder is set.
///
/// The lifetime bounds how long a file that comes back elsewhere (into the sample folder, say)
/// goes unseen: a project reopened in the same host process, or an instance reactivated, finds
/// it once the lifetime is over. It is long enough to span a project's restores (which, for
/// every instance of one missing sample, take no scan after the first).
///
/// Main thread, from any instance; a mutex covers the remembered searches, and is held through a
/// search so that instances restoring at once on different threads still scan once.
class SearchCache
{
  public:
    using Clock = std::chrono::steady_clock;
    /// How long a search that found nothing is remembered.
    static constexpr Clock::duration kLifetime = std::chrono::seconds(30);

    /// The process's cache, which every instance shares.
    static SearchCache &process();

    /// gg::media::findSample(entry, settings, contentRoots), or, when a search of `entry` with
    /// the same folders and roots found nothing less than kLifetime before `now`, and its direct
    /// candidates hold what they held then, its result. Throws only what findSample throws
    /// (std::bad_alloc).
    media::SearchResult find(const core::MediaEntry &entry, const core::MachineSettings &settings,
                             Clock::time_point now,
                             std::span<const std::filesystem::path> contentRoots = {});
    /// Forgets every search remembered.
    void clear();
    /// The searches made (findSample calls), for tests and diagnostics.
    uint64_t searches() const;

  private:
    /// What a path held: a regular file's size, modification time and permissions, or nothing.
    struct Stamp
    {
        bool file{false};
        uint64_t size{0};
        std::filesystem::file_time_type written{};
        std::filesystem::perms perms{std::filesystem::perms::unknown};
        friend bool operator==(const Stamp &, const Stamp &) = default;
    };
    /// The search's direct candidates: the saved path, each content root and the content path,
    /// the sample folder and relative path, and the captures folder and the saved name.
    struct Stamps
    {
        Stamp saved;
        std::vector<Stamp> inRoots;
        Stamp inFolder;
        Stamp inCaptures;
        friend bool operator==(const Stamps &, const Stamps &) = default;
    };
    static Stamp stampOf(const std::filesystem::path &path) noexcept;
    static Stamps stampsOf(const core::MediaEntry &entry, const core::MachineSettings &settings,
                           std::span<const std::filesystem::path> contentRoots);

    struct Miss
    {
        core::MediaEntry entry;
        core::MachineSettings settings;
        std::vector<std::filesystem::path> roots;
        Stamps stamps; // when the search was made
        media::SearchResult result;
        Clock::time_point when;
    };

    mutable std::mutex mutex_;
    std::vector<Miss> misses_;
    uint64_t searches_{0};
};
} // namespace gg::plugin

#endif // GG_PLUGIN_SEARCHCACHE_H
