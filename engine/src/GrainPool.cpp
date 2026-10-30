// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/grain/GrainPool.h"

#include <algorithm>
#include <cassert>

#include "gg/grain/Budget.h"
#include "gg/grain/Envelope.h"

namespace gg
{
// 2 × capacity is computed in uint32, so capacity must be below 2^31; GrainPool.h says so.
GrainPool::GrainPool(uint32_t capacity)
    : capacity_(capacity), slots_(2 * capacity), index_(slots_), onsetSample_(slots_),
      onsetFrac_(slots_), startPos_(slots_), rate_(slots_), durationSamples_(slots_), gain_(slots_),
      pan_(slots_), tier_(slots_), sourceSlot_(slots_), endSample_(slots_), invDuration_(slots_),
      gainL_(slots_), gainR_(slots_), texture_(slots_), envAttackEnd_(slots_),
      envReleaseStart_(slots_), envAttackScale_(slots_), envReleaseScale_(slots_), weight_(slots_),
      level_(slots_), fadeStart_(slots_), invFade_(slots_), heapPos_(slots_, kNotInHeap),
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
          .weight = weight_.data(),
          .level = level_.data(),
          .fadeStart = fadeStart_.data(),
          .invFade = invFade_.data(),
      },
      heap_(slots_)
{
}

void GrainPool::place(uint32_t pos, const HeapEntry &e) noexcept
{
    heap_[pos] = e;
    heapPos_[e.row] = pos;
}

void GrainPool::siftUp(uint32_t pos) noexcept
{
    const HeapEntry e = heap_[pos];
    while (pos > 0)
    {
        const uint32_t parent = (pos - 1) / 2;
        if (heap_[parent].end <= e.end)
        {
            break;
        }
        place(pos, heap_[parent]);
        pos = parent;
    }
    place(pos, e);
}

void GrainPool::siftDown(uint32_t pos) noexcept
{
    const HeapEntry e = heap_[pos];
    for (;;)
    {
        uint32_t child = 2 * pos + 1; // below slots_ < 2^32, since heapSize_ <= slots_ <= 2^31
        if (child >= heapSize_)
        {
            break;
        }
        if (child + 1 < heapSize_ && heap_[child + 1].end < heap_[child].end)
        {
            ++child;
        }
        if (e.end <= heap_[child].end)
        {
            break;
        }
        place(pos, heap_[child]);
        pos = child;
    }
    place(pos, e);
}

void GrainPool::forgetEnds(int64_t t) noexcept
{
    // The heap's top is its smallest end. Ends <= t belong to grains that no longer sound, and
    // since onsets don't decrease they never will again.
    while (heapSize_ > 0 && heap_[0].end <= t)
    {
        heapWeight_ -= heap_[0].weight;
        if (fadeStart_[heap_[0].row] != kNeverStolen)
        {
            fadingWeight_ -= heap_[0].weight;
        }
        heapPos_[heap_[0].row] = kNotInHeap;
        if (--heapSize_ > 0)
        {
            place(0, heap_[heapSize_]);
            siftDown(0);
        }
    }
}

uint64_t GrainPool::soundingWeight(int64_t onset) noexcept
{
    forgetEnds(onset);
    return heapWeight_;
}

uint64_t GrainPool::fadingWeight(int64_t onset) noexcept
{
    forgetEnds(onset);
    return fadingWeight_;
}

uint32_t GrainPool::soundingCount(int64_t onset) noexcept
{
    forgetEnds(onset);
    return heapSize_;
}

bool GrainPool::oldestSounding(int64_t onset, uint32_t &row) noexcept
{
    // A row passed here has ended by this onset, so by every later one, or been stolen, which
    // it stays: neither is the answer again before retirement restarts the scan.
    while (cursor_ < size_ && (endSample_[cursor_] <= onset || fadeStart_[cursor_] != kNeverStolen))
    {
        ++cursor_;
    }
    if (cursor_ == size_)
    {
        return false;
    }
    row = cursor_;
    return true;
}

void GrainPool::steal(uint32_t row, int64_t onset) noexcept
{
    assert(row < size_ && endSample_[row] > onset);
    assert(fadeStart_[row] == kNeverStolen);
    // A grain that sounds at the onset still has its heap entry: only ends <= an onset or a
    // retirement time, never later than this onset, have been forgotten.
    assert(heapPos_[row] != kNotInHeap);
    const int64_t fade = std::min(kStealFade, endSample_[row] - onset);
    const int64_t end = onset + fade;
    endSample_[row] = end;
    fadeStart_[row] = onset;
    invFade_[row] = 1.0f / static_cast<float>(fade);
    // The key only decreases (or stays), so the entry can only move up.
    const uint32_t pos = heapPos_[row];
    fadingWeight_ += heap_[pos].weight;
    heap_[pos].end = end;
    siftUp(pos);
    earliestEnd_ = end < earliestEnd_ ? end : earliestEnd_;
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
    weight_[i] = g.weight;
    level_[i] = g.level;
    fadeStart_[i] = kNeverStolen;
    invFade_[i] = 0.0f;
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
    // The heap holds at most one entry per stored grain, and storage isn't full.
    const uint32_t pos = heapSize_++;
    place(pos, {end, g.weight, i});
    heapWeight_ += g.weight;
    siftUp(pos);
    return true;
}

void GrainPool::retireFinished(int64_t nowSample) noexcept
{
    if (earliestEnd_ > nowSample)
    {
        return; // nothing has finished
    }
    // The retired grains' ends are exactly the stored ends <= nowSample, so forgetting those
    // first leaves no heap entry on a retired row: every entry belongs to a row moveRows keeps.
    forgetEnds(nowSample);
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
    cursor_ = 0; // the rows moved
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
    move(weight_);
    move(level_);
    move(fadeStart_);
    move(invFade_);
    move(heapPos_);
    // Each moved row's heap entry, if it has one, now names its new row.
    for (uint32_t r = to; r < to + n; ++r)
    {
        if (heapPos_[r] != kNotInHeap)
        {
            heap_[heapPos_[r]].row = r;
        }
    }
}

void GrainPool::clear() noexcept
{
    size_ = 0;
    earliestEnd_ = std::numeric_limits<int64_t>::max();
    // The rows' heap positions go with them: spawn() sets each anew.
    heapSize_ = 0;
    heapWeight_ = 0;
    fadingWeight_ = 0;
    cursor_ = 0;
}

void GrainPool::assignSourceSlot(uint8_t slot, uint8_t below) noexcept
{
    std::replace_if(
        sourceSlot_.begin(), sourceSlot_.begin() + size_, [below](uint8_t s) { return s < below; },
        slot);
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
        .weight = weight_[i],
        .level = level_[i],
    };
}
} // namespace gg
