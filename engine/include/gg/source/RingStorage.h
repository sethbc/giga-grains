// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_SOURCE_RINGSTORAGE_H
#define GG_SOURCE_RINGSTORAGE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gg
{
/// The most frames a ring may store, N + maxBlockSize: 2^40, 265 days at 48 kHz and far more
/// memory than any ring gets. It keeps every ring-time and slot computation far from int64
/// overflow.
inline constexpr int64_t kMaxRingFrames = int64_t{1} << 40;

/// The frames of one live ring (M2 sub-project 2's spec §6.1, §6.5), allocated off the audio
/// thread and handed to a LiveSource, which points at it: prepare() allocates one the source owns,
/// and adoptStorage() and restoreStorage() swap in one the caller owns. Level 0 alone until the
/// ring's pyramid (spec §6.9).
///
/// The layout is LiveSource's (ruling M1-o): frame f >= 0 of a window of N = frames() frames lives
/// in slot f mod C, with C = capacity() = N + maxBlockSize, and a slot holds its two channels side
/// by side, slot s's channel c at slots()[2s + c]. Fifteen guard slots follow, C .. C + 14, which
/// the ring keeps equal to slots 0 .. 14 so that sixteen taps (the sinc's) from any slot are
/// contiguous.
///
/// Neither copied nor moved: a copy would allocate a whole ring, and a ring points at its storage,
/// which lives behind a pointer (std::unique_ptr) for as long as it does. Only the constructor
/// allocates; the accessors are real-time safe.
class RingStorage
{
  public:
    /// N + maxBlockSize + 15 slots of 2 floats, every one zeroed, so a frame no one wrote reads as
    /// silence. maxBlockSize 0 counts as 1. Throws std::invalid_argument when frames < 1 or when
    /// the storage, N + maxBlockSize frames, would exceed kMaxRingFrames.
    RingStorage(int64_t frames, uint32_t maxBlockSize);

    RingStorage(const RingStorage &) = delete;
    RingStorage &operator=(const RingStorage &) = delete;
    RingStorage(RingStorage &&) = delete;
    RingStorage &operator=(RingStorage &&) = delete;
    ~RingStorage() = default;

    /// N, the window's length in frames.
    int64_t frames() const noexcept { return frames_; }
    /// C = N + maxBlockSize, the slots a frame index wraps at (the guard slots come after them).
    int64_t capacity() const noexcept { return capacity_; }
    /// The slots: slot s's channel c at 2s + c, for s in 0 .. C + 14.
    float *slots() noexcept { return slots_.data(); }
    const float *slots() const noexcept { return slots_.data(); }
    /// The bytes the slots hold, guard slots included, for the memory budget (spec §6.5).
    std::size_t bytes() const noexcept { return slots_.size() * sizeof(float); }

    /// The guard slots past the capacity, C .. C + 14 (LiveSource's kGuardFrames).
    static constexpr int64_t kGuardFrames = 15;

  private:
    std::vector<float> slots_;
    int64_t frames_{0};
    int64_t capacity_{0};
};
} // namespace gg

#endif // GG_SOURCE_RINGSTORAGE_H
