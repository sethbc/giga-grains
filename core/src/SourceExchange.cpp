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
    publish(raw, false, values);
}

void SourceExchange::offerValues(const ParamValues &values) { publish(nullptr, true, &values); }

void SourceExchange::publish(const PreparedSource *raw, bool keep, const ParamValues *values)
{
    // What the audio side may not have taken yet: the newest offer published, when it is still
    // untaken. An offer without values replacing it carries its values forward, so a restored
    // session's values are never lost to a load that comes before the audio thread takes them;
    // newer values replace them. A keep-source offer replacing it takes over its source (or its
    // keep), so that source still reaches the audio side.
    const uint8_t observed = middle_.load(std::memory_order_acquire);
    const auto fill = [&](bool replacing) {
        SourceOffer &mine = slots_[back_];
        const bool inherit = keep && replacing;
        mine.source = inherit ? lastSource_ : raw;
        mine.keepSource = inherit ? lastKeep_ : keep;
        const bool carry = values == nullptr && replacing && lastHadValues_;
        mine.hasValues = values != nullptr || carry;
        mine.values = values != nullptr ? *values : (carry ? lastValues_ : ParamValues{});
    };
    fill((observed & kFresh) != 0);
    const auto published = static_cast<uint8_t>(back_ | kFresh);
    // Release: the audio side that takes this slot sees the offer and the source fully built.
    // Acquire: the slot we get back is one the audio side has finished reading.
    uint8_t old = observed;
    if (!middle_.compare_exchange_strong(old, published, std::memory_order_acq_rel,
                                         std::memory_order_acquire))
    {
        // The audio side took the untaken offer in between, values, source and all: nothing to
        // carry or take over. Only poll() changes the middle slot besides us, and only to an
        // untaken-free one, so it can't change again before this exchange.
        fill(false);
        old = middle_.exchange(published, std::memory_order_acq_rel);
    }
    // What the audio side may not have taken yet now: this offer.
    const SourceOffer &mine = slots_[back_]; // only the main thread writes slots: no race
    lastHadValues_ = mine.hasValues;
    lastValues_ = mine.values;
    lastSource_ = mine.source;
    lastKeep_ = mine.keepSource;
    back_ = old & kIndex;
    // Never taken, and now ours again: the audio side never had its source. Unless this offer
    // took it over (a keep-source offer replacing it), it is freed.
    if ((old & kFresh) != 0 && !keep)
    {
        release(slots_[back_].source);
    }
    collect();
}

void SourceExchange::reserve() { owned_.reserve(owned_.size() + 1); }

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
