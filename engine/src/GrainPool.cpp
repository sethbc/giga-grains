// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/grain/GrainPool.h"

#include <algorithm>
#include <functional>

namespace gg
{
GrainPool::GrainPool(uint32_t capacity)
    : capacity_(capacity), slots_(2 * capacity), index_(slots_), onsetSample_(slots_),
      onsetFrac_(slots_), startPos_(slots_), rate_(slots_), durationSamples_(slots_), gain_(slots_),
      pan_(slots_), endSample_(slots_),
      rows_{index_.data(),    onsetSample_.data(), onsetFrac_.data(),
            startPos_.data(), rate_.data(),        durationSamples_.data(),
            gain_.data(),     pan_.data(),         endSample_.data()},
      heap_(slots_)
{
}

bool GrainPool::canAdmit(int64_t onset) noexcept
{
    // The heap's top is its smallest end. Ends <= onset belong to grains that no longer sound,
    // and since onsets don't decrease they never will again.
    while (heapSize_ > 0 && heap_[0] <= onset)
    {
        std::pop_heap(heap_.begin(), heap_.begin() + heapSize_, std::greater<>{});
        --heapSize_;
    }
    return heapSize_ < capacity_;
}

bool GrainPool::spawn(const GrainSpec &g) noexcept
{
    if (storageFull())
    {
        return false;
    }
    const uint32_t i = size_++;
    const int64_t end = grainEndSample(g);
    index_[i] = g.index;
    onsetSample_[i] = g.onsetSample;
    onsetFrac_[i] = g.onsetFrac;
    startPos_[i] = g.startPos;
    rate_[i] = g.rate;
    durationSamples_[i] = g.durationSamples;
    gain_[i] = g.gain;
    pan_[i] = g.pan;
    endSample_[i] = end;
    earliestEnd_ = end < earliestEnd_ ? end : earliestEnd_;
    // The heap holds at most the stored grains' ends (one per slot), and storage isn't full.
    heap_[heapSize_++] = end;
    std::push_heap(heap_.begin(), heap_.begin() + heapSize_, std::greater<>{});
    return true;
}

void GrainPool::retireFinished(int64_t nowSample) noexcept
{
    if (earliestEnd_ > nowSample)
    {
        return; // nothing has finished
    }
    // The retired grains' ends are exactly the stored ends <= nowSample, so popping those keeps
    // the heap's entries a sub-multiset of the stored grains' ends.
    while (heapSize_ > 0 && heap_[0] <= nowSample)
    {
        std::pop_heap(heap_.begin(), heap_.begin() + heapSize_, std::greater<>{});
        --heapSize_;
    }
    uint32_t kept = 0;
    int64_t earliest = std::numeric_limits<int64_t>::max();
    for (uint32_t i = 0; i < size_; ++i)
    {
        const int64_t end = endSample_[i];
        if (end > nowSample)
        {
            if (kept != i)
            {
                index_[kept] = index_[i];
                onsetSample_[kept] = onsetSample_[i];
                onsetFrac_[kept] = onsetFrac_[i];
                startPos_[kept] = startPos_[i];
                rate_[kept] = rate_[i];
                durationSamples_[kept] = durationSamples_[i];
                gain_[kept] = gain_[i];
                pan_[kept] = pan_[i];
                endSample_[kept] = end;
            }
            ++kept;
            earliest = end < earliest ? end : earliest;
        }
    }
    size_ = kept;
    earliestEnd_ = earliest;
}

void GrainPool::clear() noexcept
{
    size_ = 0;
    earliestEnd_ = std::numeric_limits<int64_t>::max();
    heapSize_ = 0;
}

GrainSpec GrainPool::spec(uint32_t i) const noexcept
{
    return {
        .index = index_[i],
        .onsetSample = onsetSample_[i],
        .onsetFrac = onsetFrac_[i],
        .startPos = startPos_[i],
        .rate = rate_[i],
        .durationSamples = durationSamples_[i],
        .gain = gain_[i],
        .pan = pan_[i],
    };
}
} // namespace gg
