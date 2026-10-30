// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_MEDIA_MEMORYBUDGET_H
#define GG_MEDIA_MEMORYBUDGET_H

#include <cstdint>
#include <string>

#include "gg/source/PreparedSource.h"

/// The memory cap's accounting (spec §6.5): what a sample source and a ring hold, and whether a
/// need fits what the cap leaves. Pure arithmetic, deterministic: the accounting is of what an
/// instance references, never of what the OS has. Any thread but the audio thread (refusal
/// allocates).
namespace gg::media
{
/// The bytes a sample source of `frames` frames and `channels` channels holds: its planar float
/// buffer (frames × channels × 4), the interleaved copy of a stereo source's pairs when
/// `interleavedCopy` (frames × 2 × 4), and for each pyramid level n in 1 .. levels − 1,
/// ceil(frames / 2ⁿ) × channels × 4 (one copy per level: pairs for stereo, planar for mono).
/// 0 for frames <= 0; `levels` 0 counts as 1.
uint64_t sampleBytes(int64_t frames, uint32_t channels, bool interleavedCopy,
                     uint32_t levels) noexcept;

/// sampleBytes of `source`: its buffer's frames and channels, whether it has pairs, and its
/// levelCount().
uint64_t sourceBytes(const PreparedSource &source) noexcept;

/// The bytes a ring's storage holds (RingStorage; LiveSource's shape): level 0's
/// (frames + maxBlockSize + 3) slots of 2 floats, the 3 being the guard that mirrors the first
/// slots, plus, for each level n in 1 .. levels − 1, (ceil((frames + maxBlockSize) / 2ⁿ) + 3)
/// slots of 2 floats. 0 for frames <= 0 (no ring); `levels` 0 counts as 1.
uint64_t ringBytes(int64_t frames, uint32_t maxBlockSize, uint32_t levels) noexcept;

/// An instance's cap and what it holds against it, in bytes.
struct MemoryBudget
{
    uint64_t capBytes;
    uint64_t usedBytes{0};

    /// Whether `need` more fits: usedBytes + need <= capBytes, without overflowing.
    bool fits(uint64_t need) const noexcept;
    /// The status for a need that doesn't fit: "over the memory cap: needs N MB, F MB free of
    /// C MB", in whole MiB: the need rounded up, the free space (capBytes − usedBytes, 0 when
    /// used exceeds the cap) rounded down, so the need always reads as the larger, and the cap
    /// rounded up.
    std::string refusal(uint64_t need) const;
};
} // namespace gg::media

#endif // GG_MEDIA_MEMORYBUDGET_H
