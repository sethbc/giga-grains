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

void SourceExchange::offer(std::shared_ptr<const PreparedSource> s)
{
    const PreparedSource *raw = s.get();
    if (s != nullptr)
    {
        owned_.push_back(std::move(s)); // before the audio side can see it
    }
    // Release: the audio side that polls this offer sees the source fully built. Acquire: an
    // offer we get back untaken is one the audio side never read.
    const uintptr_t old =
        mailbox_.exchange(reinterpret_cast<uintptr_t>(raw) | kFresh, std::memory_order_acq_rel);
    if (old != 0)
    {
        // Never polled, and now out of the mailbox: the audio side can't have it.
        release(reinterpret_cast<const PreparedSource *>(old & ~kFresh));
    }
    collect();
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

bool SourceExchange::poll(const PreparedSource *&source) noexcept
{
    // A relaxed look first, so an empty mailbox costs no read-modify-write.
    if (mailbox_.load(std::memory_order_relaxed) == 0)
    {
        return false;
    }
    const uintptr_t v = mailbox_.exchange(0, std::memory_order_acq_rel);
    if (v == 0)
    {
        return false;
    }
    source = reinterpret_cast<const PreparedSource *>(v & ~kFresh);
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
