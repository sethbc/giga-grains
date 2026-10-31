// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/source/RingStorage.h"

#include <algorithm>
#include <cstring>
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
    // levelCountFor never passes kMaxLevels; the min lets GCC 13 see the arrays' bound.
    levelCount_ = std::min(levelCountFor(frames), kMaxLevels);
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
    const uint32_t count = std::min(levelCount_, kMaxLevels); // the arrays' bound, for GCC 13
    for (uint32_t n = 1; n < count; ++n)
    {
        // Level n's frame m from level n − 1's frames 2m − 27 .. 2m + 27, all complete and still
        // stored: the level above holds at least 2 × kMinLevelFrames of them.
        const float *const src = levelSlots_[n - 1];
        const int64_t srcCapacity = levelCapacity_[n - 1];
        // decimateHalfband's sum, in its order: the taps at even k ascending, the centre tap
        // (k = 27, the frame 2m itself) between k = 26 and k = 28, every other tap zero and
        // skipped, as are the frames before 0 (k below 27 − 2m); added from +0.
        const int64_t centreSlot = (2 * m) % srcCapacity;
        float sumL = 0.0f;
        float sumR = 0.0f;
        const auto add = [&](int64_t k) noexcept {
            int64_t s = centreSlot + (k - kCentre);
            s += s < 0 ? srcCapacity : (s >= srcCapacity ? -srcCapacity : 0);
            const float h = kHalfband[static_cast<std::size_t>(k)];
            const float productL = h * src[2 * s];
            const float productR = h * src[2 * s + 1];
            sumL = sumL + productL;
            sumR = sumR + productR;
        };
        const int64_t firstEven = std::max<int64_t>(0, kCentre - 2 * m + 1); // even, frame >= 0
        for (int64_t k = firstEven; k < kCentre; k += 2)
        {
            add(k);
        }
        add(kCentre);
        for (int64_t k = kCentre + 1; k < kHalfbandTaps; k += 2)
        {
            add(k);
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
void copyRingFrames(const float *src, int64_t srcCapacity, float *dst, int64_t dstCapacity,
                    int64_t from, int64_t to) noexcept
{
    for (int64_t f = std::max<int64_t>(0, from); f < to;)
    {
        const int64_t s = f % srcCapacity;
        const int64_t d = f % dstCapacity;
        const int64_t run = std::min({to - f, srcCapacity - s, dstCapacity - d});
        std::memcpy(dst + 2 * d, src + 2 * s, static_cast<std::size_t>(run) * 2 * sizeof(float));
        f += run;
    }
}

void copyLevels(const RingStorage &src, RingStorage &dst, int64_t from, int64_t to) noexcept
{
    if (dst.levelCount() > src.levelCount())
    {
        fillLevels(dst, from, to);
        return;
    }
    const int64_t start = std::max<int64_t>(0, from);
    for (uint32_t n = 1; n < dst.levelCount(); ++n)
    {
        // The level frames completing at ring frames [start, to).
        copyRingFrames(src.levelSlots(n), src.levelCapacity(n), dst.levelSlots(n),
                       dst.levelCapacity(n), RingStorage::completeLevelFrames(n, start),
                       RingStorage::completeLevelFrames(n, to));
    }
}
} // namespace gg
