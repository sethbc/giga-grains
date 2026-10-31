// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_SAMPLEPOOL_H
#define GG_MEDIA_SAMPLEPOOL_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "gg/source/PreparedSource.h"

namespace gg::media
{
/// What identifies a prepared source: its file's fingerprint (SHA-256 and size, as in its
/// manifest entry) and the rate it was converted to. Two keys are the same when the hex digest
/// and size are equal and the rate's bits are (so 48000 and the next double up are two rates).
struct PoolKey
{
    std::string sha256Hex;
    uint64_t fileSize;
    double rate;
};

/// The prepared sources the process's instances share (spec §6.5), so ten instances of one
/// project hold one copy of a shared sample's planar buffer, interleaved copy and levels.
///
/// It holds them weakly: it keeps nothing alive, and the last instance to let go of a source
/// frees it. Entries whose source has gone are pruned as find() passes them. One per process,
/// as SearchCache is; main and loader threads, under one mutex, never the audio thread.
class SamplePool
{
  public:
    /// The process's pool, which every instance shares.
    static SamplePool &process();

    /// The live source of `key`, or nullptr when none is alive. Prunes the entries whose
    /// source has gone.
    std::shared_ptr<const PreparedSource> find(const PoolKey &key);
    /// Remembers `source` (weakly) under `key` unless a live source already holds the key, and
    /// returns the one the pool holds: `source` itself, or the live one it found, which is not
    /// replaced. Find and insert are one step under one lock, so two loaders that both built a
    /// source of one key end up sharing the first to arrive. An entry whose source has gone is
    /// replaced. Throws std::bad_alloc if memory runs out.
    std::shared_ptr<const PreparedSource>
    insert(const PoolKey &key, const std::shared_ptr<const PreparedSource> &source);
    /// The entries whose source is alive (tests and diagnostics).
    std::size_t alive() const;
    /// Forgets every entry (tests).
    void clear();

  private:
    struct Entry
    {
        PoolKey key;
        std::weak_ptr<const PreparedSource> source;
    };

    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
};
} // namespace gg::media

#endif // GG_MEDIA_SAMPLEPOOL_H
