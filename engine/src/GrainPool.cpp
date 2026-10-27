// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/grain/GrainPool.h"

namespace gg
{
GrainPool::GrainPool(uint32_t capacity) : grains_(capacity) {}

bool GrainPool::spawn(const GrainSpec &g) noexcept
{
    if (size_ == grains_.size())
    {
        return false;
    }
    grains_[size_++] = g;
    const int64_t end = grainEndSample(g);
    earliestEnd_ = end < earliestEnd_ ? end : earliestEnd_;
    return true;
}

void GrainPool::retireFinished(int64_t nowSample) noexcept
{
    if (earliestEnd_ > nowSample)
    {
        return; // nothing has finished
    }
    uint32_t kept = 0;
    int64_t earliest = std::numeric_limits<int64_t>::max();
    for (uint32_t i = 0; i < size_; ++i)
    {
        const int64_t end = grainEndSample(grains_[i]);
        if (end > nowSample)
        {
            grains_[kept++] = grains_[i];
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
}
} // namespace gg
