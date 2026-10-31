// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/source/RingStorage.h"

#include <algorithm>
#include <stdexcept>

#include "gg/core/FpContract.h"

namespace gg
{
RingStorage::RingStorage(int64_t frames, uint32_t maxBlockSize)
{
    const auto block = static_cast<int64_t>(std::max<uint32_t>(1, maxBlockSize));
    if (frames < 1)
    {
        throw std::invalid_argument("RingStorage: frames must be >= 1");
    }
    if (frames > kMaxRingFrames - block)
    {
        throw std::invalid_argument("RingStorage: the ring would exceed 2^40 frames");
    }
    frames_ = frames;
    capacity_ = frames + block;
    slots_.assign(2 * static_cast<std::size_t>(capacity_ + kGuardFrames), 0.0f);
    levelCount_ = levelCountFor(frames);
    std::size_t floats = 0;
    for (uint32_t n = 1; n < levelCount_; ++n)
    {
        floats += 2 * static_cast<std::size_t>(mipLevelFrames(capacity_, n) + kGuardFrames);
    }
    levels_.assign(floats, 0.0f);
    levelSlots_[0] = slots_.data();
    levelCapacity_[0] = capacity_;
    std::size_t at = 0;
    for (uint32_t n = 1; n < levelCount_; ++n)
    {
        levelSlots_[n] = levels_.data() + at;
        levelCapacity_[n] = mipLevelFrames(capacity_, n);
        at += 2 * static_cast<std::size_t>(levelCapacity_[n] + kGuardFrames);
    }
}

void RingStorage::cascade(int64_t m) noexcept
{
    GG_FP_CONTRACT_OFF
    constexpr int64_t kCentre = kHalfbandHalf;
    for (uint32_t n = 1; n < levelCount_; ++n)
    {
        // Level n's frame m from level n − 1's frames 2m − 27 .. 2m + 27, all complete and still
        // stored: the level above holds at least 2 × kMinLevelFrames of them.
        const float *const src = levelSlots_[n - 1];
        const int64_t srcCapacity = levelCapacity_[n - 1];
        const int64_t first = 2 * m - kCentre; // the frame tap 0 reads
        const int64_t k0 = first < 0 ? -first : 0;
        int64_t s = (first + k0) % srcCapacity;
        float sumL = 0.0f;
        float sumR = 0.0f;
        // decimateHalfband's sum: the zero taps (odd offsets from the centre) and the frames
        // before 0 are skipped, the rest added in k order from +0.
        for (int64_t k = k0; k < kHalfbandTaps; ++k)
        {
            if ((k & 1) == 0 || k == kCentre)
            {
                const float h = kHalfband[static_cast<std::size_t>(k)];
                const float productL = h * src[2 * s];
                const float productR = h * src[2 * s + 1];
                sumL = sumL + productL;
                sumR = sumR + productR;
            }
            if (++s == srcCapacity)
            {
                s = 0;
            }
        }
        float *const dst = levelSlots_[n];
        const int64_t capacity = levelCapacity_[n];
        const int64_t slot = m % capacity;
        dst[2 * slot] = sumL;
        dst[2 * slot + 1] = sumR;
        if (slot < kGuardFrames)
        {
            dst[2 * (capacity + slot)] = sumL;
            dst[2 * (capacity + slot) + 1] = sumR;
        }
        // Level n + 1's frame m' completes with level n's frame 2m' + 27.
        if (m < kCentre || ((m - kCentre) & 1) != 0)
        {
            return;
        }
        m = (m - kCentre) >> 1;
    }
}

void fillLevels(RingStorage &storage, int64_t from, int64_t to) noexcept
{
    for (int64_t f = std::max<int64_t>(0, from); f < to; ++f)
    {
        storage.levelsAfter(f);
    }
}
} // namespace gg
