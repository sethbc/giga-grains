// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/LivePeaks.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

namespace gg::core
{
namespace
{
float magnitude(float x) noexcept { return std::isfinite(x) ? std::fabs(x) : 0.0f; }
} // namespace

void LivePeaks::prepare(int64_t ringFrames)
{
    assert(ringFrames >= 0);
    frames_ = std::max<int64_t>(ringFrames, 0);
    const int64_t count = (frames_ + kBucket - 1) / kBucket + 1;
    assert(count <= static_cast<int64_t>(UINT32_MAX));
    bucketCount_ = static_cast<uint32_t>(count);
    // Value-initialised: every slot starts at 0.
    peaks_ = std::make_unique<std::atomic<float>[]>(bucketCount_);
    lastBucket_ = -1;
}

void LivePeaks::write(const float *l, const float *r, int64_t t, uint32_t n) noexcept
{
    assert(t >= 0);
    if (bucketCount_ == 0 || n == 0)
    {
        return;
    }
    // One stretch of frames per bucket: at most n / kBucket + 2 of them.
    uint32_t i = 0;
    while (i < n)
    {
        const int64_t frame = t + i;
        const int64_t bucket = frame / kBucket;
        const auto inBucket = static_cast<uint32_t>(kBucket - frame % kBucket);
        const uint32_t end = n - i > inBucket ? i + inBucket : n;
        float peak = 0.0f;
        for (; i < end; ++i)
        {
            peak = std::max(peak, std::max(magnitude(l[i]), magnitude(r[i])));
        }
        std::atomic<float> &slot = peaks_[static_cast<size_t>(bucket % bucketCount_)];
        if (bucket == lastBucket_)
        {
            // Only the writer stores, so a load and a store need no read-modify-write.
            peak = std::max(peak, slot.load(std::memory_order_relaxed));
        }
        slot.store(peak, std::memory_order_relaxed);
        lastBucket_ = bucket;
    }
}

float LivePeaks::at(int64_t frame) const noexcept
{
    if (frame < 0 || bucketCount_ == 0)
    {
        return 0.0f;
    }
    return peaks_[static_cast<size_t>((frame / kBucket) % bucketCount_)].load(
        std::memory_order_relaxed);
}
} // namespace gg::core
