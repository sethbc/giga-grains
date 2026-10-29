// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/grain/GrainPool.h"

#include <algorithm>
#include <functional>

#include "gg/grain/Envelope.h"

namespace gg
{
// 2 × capacity is computed in uint32, so capacity must be below 2^31; GrainPool.h says so.
GrainPool::GrainPool(uint32_t capacity)
    : capacity_(capacity), slots_(2 * capacity), index_(slots_), onsetSample_(slots_),
      onsetFrac_(slots_), startPos_(slots_), rate_(slots_), durationSamples_(slots_), gain_(slots_),
      pan_(slots_), tier_(slots_), sourceSlot_(slots_), endSample_(slots_), invDuration_(slots_),
      gainL_(slots_), gainR_(slots_), texture_(slots_), envAttackEnd_(slots_),
      envReleaseStart_(slots_), envAttackScale_(slots_), envReleaseScale_(slots_),
      rows_{
          .index = index_.data(),
          .onsetSample = onsetSample_.data(),
          .onsetFrac = onsetFrac_.data(),
          .startPos = startPos_.data(),
          .rate = rate_.data(),
          .durationSamples = durationSamples_.data(),
          .gain = gain_.data(),
          .pan = pan_.data(),
          .tier = tier_.data(),
          .sourceSlot = sourceSlot_.data(),
          .endSample = endSample_.data(),
          .invDuration = invDuration_.data(),
          .gainL = gainL_.data(),
          .gainR = gainR_.data(),
          .texture = texture_.data(),
          .envAttackEnd = envAttackEnd_.data(),
          .envReleaseStart = envReleaseStart_.data(),
          .envAttackScale = envAttackScale_.data(),
          .envReleaseScale = envReleaseScale_.data(),
      },
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
    tier_[i] = g.tier;
    sourceSlot_[i] = g.sourceSlot;
    endSample_[i] = end;
    texture_[i] = g.texture;
    // The renderers' latched values (spec §3): one division and one channel gain per grain, and
    // the Texture envelope's shape (spec §4).
    const PanGains pan = panGains(g.pan);
    invDuration_[i] = 1.0 / g.durationSamples;
    gainL_[i] = g.gain * pan.left;
    gainR_[i] = g.gain * pan.right;
    const EnvelopeShape env = envelopeShape(g.texture, g.durationSamples);
    envAttackEnd_[i] = env.attackEnd;
    envReleaseStart_[i] = env.releaseStart;
    envAttackScale_[i] = env.attackScale;
    envReleaseScale_[i] = env.releaseScale;
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
    // Stable compaction by runs: each maximal run of grains that still sound moves down as one
    // block per array (std::copy of trivially copyable values, a memmove), so a block that
    // retires a few grains of thousands copies a few long runs rather than every field of every
    // grain one at a time.
    uint32_t kept = 0;
    int64_t earliest = std::numeric_limits<int64_t>::max();
    uint32_t i = 0;
    while (i < size_)
    {
        while (i < size_ && endSample_[i] <= nowSample)
        {
            ++i; // retired
        }
        const uint32_t run = i;
        while (i < size_ && endSample_[i] > nowSample)
        {
            earliest = endSample_[i] < earliest ? endSample_[i] : earliest;
            ++i;
        }
        const uint32_t n = i - run;
        if (n > 0 && kept != run)
        {
            moveRows(run, kept, n);
        }
        kept += n;
    }
    size_ = kept;
    earliestEnd_ = earliest;
}

void GrainPool::moveRows(uint32_t from, uint32_t to, uint32_t n) noexcept
{
    // to < from, so each copy runs forward over a range that starts below its source.
    const auto move = [from, to, n](auto &v) {
        std::copy(v.begin() + from, v.begin() + from + n, v.begin() + to);
    };
    move(index_);
    move(onsetSample_);
    move(onsetFrac_);
    move(startPos_);
    move(rate_);
    move(durationSamples_);
    move(gain_);
    move(pan_);
    move(tier_);
    move(sourceSlot_);
    move(endSample_);
    move(invDuration_);
    move(gainL_);
    move(gainR_);
    move(texture_);
    move(envAttackEnd_);
    move(envReleaseStart_);
    move(envAttackScale_);
    move(envReleaseScale_);
}

void GrainPool::clear() noexcept
{
    size_ = 0;
    earliestEnd_ = std::numeric_limits<int64_t>::max();
    heapSize_ = 0;
}

void GrainPool::assignSourceSlot(uint8_t slot) noexcept
{
    std::fill_n(sourceSlot_.begin(), size_, slot);
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
        .texture = texture_[i],
        .gain = gain_[i],
        .pan = pan_[i],
        .tier = tier_[i],
        .sourceSlot = sourceSlot_[i],
    };
}
} // namespace gg
