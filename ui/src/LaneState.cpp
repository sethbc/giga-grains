// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/LaneState.h"

#include <algorithm>
#include <cmath>

namespace gg::ui
{
bool LaneClock::update(int64_t publishedClock, double wallSeconds, double sampleRate) noexcept
{
    if (!valid_ || publishedClock < published_)
    {
        const bool restarted = valid_;
        valid_ = true;
        published_ = publishedClock;
        changedAt_ = wallSeconds;
        now_ = publishedClock;
        return restarted;
    }
    if (publishedClock != published_)
    {
        published_ = publishedClock;
        changedAt_ = wallSeconds;
    }
    double elapsed = wallSeconds - changedAt_;
    elapsed = elapsed > 0.0 ? std::min(elapsed, kMaxExtrapolationSeconds) : 0.0; // NaN gives 0
    const double rate = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 0.0;
    const int64_t extrapolated = published_ + static_cast<int64_t>(std::llround(elapsed * rate));
    now_ = std::max(now_, extrapolated);
    return false;
}

void LaneClock::reset() noexcept
{
    valid_ = false;
    published_ = 0;
    changedAt_ = 0.0;
    now_ = 0;
}

int64_t headAtNow(int64_t writeHead, int64_t snapshotClock, int64_t now, bool frozen) noexcept
{
    if (frozen || now <= snapshotClock)
    {
        return writeHead;
    }
    return writeHead + (now - snapshotClock);
}

LiveGrains::LiveGrains() : slots_(kCapacity) {}

void LiveGrains::push(const LaneGrain &grain) noexcept
{
    if (size_ == kCapacity)
    {
        slots_[first_] = grain; // over the oldest, which the newest now follows
        first_ = (first_ + 1) % kCapacity;
        return;
    }
    slots_[(first_ + size_) % kCapacity] = grain;
    ++size_;
}

void LiveGrains::prune(int64_t now) noexcept
{
    // Compacts the survivors towards the oldest slot, in order.
    std::size_t kept = 0;
    for (std::size_t i = 0; i < size_; ++i)
    {
        const LaneGrain &g = slots_[(first_ + i) % kCapacity];
        const double duration = g.durationSamples;
        const bool playable = std::isfinite(duration) && duration > 0.0;
        // Unsigned difference, as grainCapsule's: no overflow for any two clocks.
        const bool ended = now >= g.onsetSample &&
                           !(static_cast<double>(static_cast<uint64_t>(now) -
                                                 static_cast<uint64_t>(g.onsetSample)) < duration);
        if (playable && !ended)
        {
            slots_[(first_ + kept) % kCapacity] = g;
            ++kept;
        }
    }
    size_ = kept;
}

void LiveGrains::clear() noexcept
{
    first_ = 0;
    size_ = 0;
}
} // namespace gg::ui
