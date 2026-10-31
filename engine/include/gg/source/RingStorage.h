// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_SOURCE_RINGSTORAGE_H
#define GG_SOURCE_RINGSTORAGE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gg/source/Mipmap.h"

namespace gg
{
/// The most frames a ring may store, N + maxBlockSize: 2^40, 265 days at 48 kHz and far more
/// memory than any ring gets. It keeps every ring-time and slot computation far from int64
/// overflow.
inline constexpr int64_t kMaxRingFrames = int64_t{1} << 40;

/// A ring level's lag behind the head in ring frames, (2ⁿ − 1) × h with h = 27, the half-band's
/// half length (RingStorage::levelLag; spec §6.9).
inline constexpr int64_t ringLevelLag(uint32_t n) noexcept
{
    return ((int64_t{1} << n) - 1) * ((kHalfbandTaps - 1) / 2);
}

/// The most levels a ring builds, level 0 included (RingStorage::kMaxLevels): 1 + the largest
/// n < kMaxMipLevels whose lag is at most 256 ring frames, LiveSource's head margin M. 4 with the
/// 55-tap half-band: levels 1 .. 3, whose lags are 27, 81 and 189 (level 4's, 405, is past M).
inline constexpr uint32_t kRingMaxLevels = [] {
    uint32_t count = 1;
    while (count < kMaxMipLevels && ringLevelLag(count) <= 256)
    {
        ++count;
    }
    return count;
}();
static_assert(kRingMaxLevels == 4);

/// The frames of one live ring (M2 sub-project 2's spec §6.1, §6.5), allocated off the audio
/// thread and handed to a LiveSource, which points at it: prepare() allocates one the source owns,
/// and adoptStorage() and restoreStorage() swap in one the caller owns. It holds level 0, the
/// ring's frames, and the ring's pyramid, levels 1 .. levelCount() − 1 (spec §6.9).
///
/// The layout is LiveSource's (ruling M1-o): frame f >= 0 of a window of N = frames() frames lives
/// in slot f mod C, with C = capacity() = N + maxBlockSize, and a slot holds its two channels side
/// by side, slot s's channel c at slots()[2s + c]. Fifteen guard slots follow, C .. C + 14, which
/// the ring keeps equal to slots 0 .. 14 so that sixteen taps (the sinc's) from any slot are
/// contiguous.
///
/// **The levels** (spec §6.9). Level n >= 1 holds the ring lowpassed and decimated by 2ⁿ with
/// sub-project 3's half-band (Mipmap.h's kHalfband, decimateHalfband's sum): its frame m, at ring
/// frame m × 2ⁿ, is
///
///     Σ_{k=0}^{54} kHalfband[k] × level(n − 1)[2m + k − 27],
///
/// accumulated in float from 0 in k order, the zero taps and the frames before 0 skipped, exactly
/// as a file's level of the same frames (PreparedSource). Its frame m lives in slot m mod Cₙ,
/// Cₙ = levelCapacity(n) = ceil(C / 2ⁿ), 2 floats per slot as level 0, with 15 guard slots after
/// them mirroring slots 0 .. 14. Level n's frame m needs level n − 1's frame 2m + 27, so it is
/// complete once ring frame m × 2ⁿ + levelLag(n) is written: levelLag(n) = (2ⁿ − 1) × 27 ring
/// frames, 27, 81 and 189 for levels 1, 2 and 3. The ring builds the levels whose lag is at most
/// kRingLevelLagLimit = 256 (LiveSource's head margin M), levels 1 .. 3 with the 55-tap half-band
/// (level 4's lag, 405, is past it), and only those a file of N frames would get (mipLevelCount:
/// ceil(N / 2ⁿ) >= kMinLevelFrames), so a level always holds the 55 frames of the one above
/// that its newest frame reads. levelsAfter() advances them as each frame arrives.
///
/// Neither copied nor moved: a copy would allocate a whole ring, and a ring points at its storage,
/// which lives behind a pointer (std::unique_ptr) for as long as it does. Only the constructor
/// allocates; the accessors and levelsAfter() are real-time safe.
class RingStorage
{
  public:
    /// The guard slots past the capacity, C .. C + 14 (LiveSource's kGuardFrames), and past each
    /// level's.
    static constexpr int64_t kGuardFrames = 15;
    /// The half-band's half length, h: the frames its sum reaches on each side of its centre.
    static constexpr int64_t kHalfbandHalf = (kHalfbandTaps - 1) / 2;
    /// The most ring frames a level may lag the head by (spec §6.9): LiveSource::kHeadMargin.
    static constexpr int64_t kRingLevelLagLimit = 256;

    /// Level n's lag behind the head in ring frames, (2ⁿ − 1) × h (spec §6.9).
    static constexpr int64_t levelLag(uint32_t n) noexcept { return ringLevelLag(n); }
    /// The most levels a ring builds, level 0 included: kRingMaxLevels, 4.
    static constexpr uint32_t kMaxLevels = kRingMaxLevels;
    /// The levels a ring of `frames` frames builds: min(kMaxLevels, mipLevelCount(frames)).
    static constexpr uint32_t levelCountFor(int64_t frames) noexcept
    {
        const uint32_t file = mipLevelCount(frames);
        return file < kMaxLevels ? file : kMaxLevels;
    }
    /// How many of level n's frames are complete once the ring frames [0, head) are written: the
    /// frames m with m × 2ⁿ + levelLag(n) <= head − 1 (all of level 0's head).
    static constexpr int64_t completeLevelFrames(uint32_t n, int64_t head) noexcept
    {
        const int64_t newest = head - 1 - levelLag(n); // the newest complete frame's ring frame
        return newest < 0 ? 0 : (newest >> n) + 1;
    }

    /// N + maxBlockSize + 15 slots of 2 floats, and each level's ceil((N + maxBlockSize) / 2ⁿ) +
    /// 15, every one zeroed, so a frame no one wrote reads as silence. maxBlockSize 0 counts as 1.
    /// Throws std::invalid_argument when frames < 1 or when the storage, N + maxBlockSize frames,
    /// would exceed kMaxRingFrames.
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
    /// The bytes the slots hold, every level's and the guard slots included, for the memory
    /// budget (spec §6.5; media::ringBytes with levelCount()).
    std::size_t bytes() const noexcept { return (slots_.size() + levels_.size()) * sizeof(float); }

    /// The levels held, level 0 included: levelCountFor(frames()).
    uint32_t levelCount() const noexcept { return levelCount_; }
    /// Level n's slot count Cₙ: C for level 0, ceil(C / 2ⁿ) for a level n in 1 .. levelCount() − 1.
    int64_t levelCapacity(uint32_t n) const noexcept { return levelCapacity_[n]; }
    /// Level n's slots (slots() for level 0): slot s's channel c at 2s + c, s in 0 .. Cₙ + 14,
    /// Cₙ .. Cₙ + 14 mirroring 0 .. 14. n < levelCount().
    float *levelSlots(uint32_t n) noexcept { return levelSlots_[n]; }
    const float *levelSlots(uint32_t n) const noexcept { return levelSlots_[n]; }

    /// After ring frame f (>= 0) is written at its slot: the level frames f completes, level by
    /// level from 1, each from the level above as the class comment's sum (and its guard slot
    /// where it has one). Ring frames must arrive in order, as LiveSource::write() writes them;
    /// fillLevels() replays it. Real-time safe: at most one frame per level.
    void levelsAfter(int64_t f) noexcept
    {
        // Level 1's frame m completes at ring frame 2m + 27: an odd frame from 27 on.
        if (levelCount_ > 1 && f >= kHalfbandHalf && ((f - kHalfbandHalf) & 1) == 0)
        {
            cascade((f - kHalfbandHalf) >> 1);
        }
    }

  private:
    // Level 1's frame m, then level n + 1's frame m' wherever level n's frame m completes one.
    void cascade(int64_t m) noexcept;

    std::vector<float> slots_;
    std::vector<float> levels_; // levels 1 .. levelCount_ − 1, in order, guard slots included
    std::array<float *, kMaxLevels> levelSlots_{};
    std::array<int64_t, kMaxLevels> levelCapacity_{};
    int64_t frames_{0};
    int64_t capacity_{0};
    uint32_t levelCount_{1};
};

/// The levels of ring frames [from, to) of `storage`, whose level-0 slots already hold them (and
/// the frames before, as far as the levels reach): levelsAfter(f) for each f in order, the same
/// arithmetic in the same order as LiveSource::write(), so a ring restored or resized holds the
/// levels its frames would have got as they were written (spec §6.9). For the main thread's copy
/// of a resize's stable part and a restore's frames, and adoptStorage()'s bounded newest part.
/// Real-time safe, but its cost is the frames': (to − from) × about 51 multiplies. A level frame
/// whose reach (its ring frame ± levelLag(n)) crosses `from` reads whatever the slots before it
/// hold, as written: a restore's frames before 0 are silence, so its levels near the oldest frame
/// are the ones a ring that started there would have.
void fillLevels(RingStorage &storage, int64_t from, int64_t to) noexcept;

/// Copies frames [from, to) (each >= 0) of one level between two storages' layouts: frame f from
/// src's slot f mod srcCapacity to dst's slot f mod dstCapacity, both channels, in contiguous runs.
/// The guard slots are not written (mirrored when the storage is adopted). Real-time safe.
void copyRingFrames(const float *src, int64_t srcCapacity, float *dst, int64_t dstCapacity,
                    int64_t from, int64_t to) noexcept;

/// fillLevels' twin for a resize (spec §6.9): copies from `src` into `dst`, level by level, the
/// level frames that fillLevels(dst, from, to) would make, those whose completing ring frame
/// (m × 2ⁿ + levelLag(n)) is in [from, to). `src` must hold them complete (its head at `to` or
/// later, none of them overwritten since), so the copy is bit for bit what fillLevels would make
/// from the same frames, and those whose reach crosses `from` hold the old ring's values, not
/// values computed from what the new storage lacks. When dst has a level src lacks (a ring too
/// short for it), it is fillLevels(dst, from, to) instead. Real-time safe; its cost is the copy's.
void copyLevels(const RingStorage &src, RingStorage &dst, int64_t from, int64_t to) noexcept;
} // namespace gg

#endif // GG_SOURCE_RINGSTORAGE_H
