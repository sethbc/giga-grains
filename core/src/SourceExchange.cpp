// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/SourceExchange.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace gg::core
{
SourceExchange::SourceExchange()
{
    // The Processor's use holds a handful at most (see the class comment); reserving keeps
    // offer() from reallocating in the usual case.
    owned_.reserve(8);
}

SourceExchange::~SourceExchange() = default;

void SourceExchange::offer(std::shared_ptr<const PreparedSource> s, const ParamValues *values)
{
    const PreparedSource *raw = hold(std::move(s)); // before the audio side can see it
    SourceOffer &mine = slots_[back_];
    mine.source = raw;
    mine.hasValues = values != nullptr;
    mine.values = values != nullptr ? *values : ParamValues{};
    // Release: the audio side that takes this slot sees the offer and the source fully built.
    // Acquire: the slot we get back is one the audio side has finished reading.
    const uint8_t old =
        middle_.exchange(static_cast<uint8_t>(back_ | kFresh), std::memory_order_acq_rel);
    back_ = old & kIndex;
    if ((old & kFresh) != 0)
    {
        // Never taken, and now ours again: the audio side never had its source.
        release(slots_[back_].source);
    }
    collect();
}

const PreparedSource *SourceExchange::hold(std::shared_ptr<const PreparedSource> s)
{
    const PreparedSource *raw = s.get();
    if (s != nullptr)
    {
        owned_.push_back(std::move(s));
    }
    return raw;
}

void SourceExchange::collect()
{
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    std::size_t head = head_.load(std::memory_order_relaxed);
    for (; head != tail; ++head)
    {
        release(ring_[head % kRetireCapacity]);
    }
    head_.store(head, std::memory_order_release);
}

bool SourceExchange::awaitingCollect() const noexcept
{
    return tail_.load(std::memory_order_acquire) != head_.load(std::memory_order_acquire);
}

bool SourceExchange::poll(SourceOffer &offer) noexcept
{
    // Only poll() clears kFresh, so once seen it stays until the exchange below.
    if ((middle_.load(std::memory_order_relaxed) & kFresh) == 0)
    {
        return false;
    }
    front_ = middle_.exchange(front_, std::memory_order_acq_rel) & kIndex;
    offer = slots_[front_];
    return true;
}

void SourceExchange::retire(const PreparedSource *s) noexcept
{
    if (s == nullptr)
    {
        return;
    }
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail - head_.load(std::memory_order_acquire) == kRetireCapacity)
    {
        // Full, which the class comment's bound rules out: the source stays held until the
        // exchange is destroyed rather than being freed here.
        assert(false && "SourceExchange: hand-back ring full");
        return;
    }
    ring_[tail % kRetireCapacity] = s;
    tail_.store(tail + 1, std::memory_order_release);
}

void SourceExchange::release(const PreparedSource *s)
{
    if (s == nullptr)
    {
        return;
    }
    const auto it = std::find_if(owned_.begin(), owned_.end(),
                                 [s](const auto &owned) { return owned.get() == s; });
    if (it != owned_.end())
    {
        owned_.erase(it);
    }
}
} // namespace gg::core
