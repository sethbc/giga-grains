// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_LATESTVALUE_H
#define GG_PLUGIN_LATESTVALUE_H

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace gg::plugin
{
/// A lock-free, latest-wins hand-off of a value from one producer thread to one consumer
/// thread: a triple buffer. The plugin uses it to pass a state loaded on the main thread to the
/// audio thread (spec §5).
///
/// The producer writes into its own slot and swaps it with the shared middle one; the consumer
/// swaps its own slot with the middle one when that holds a value it hasn't taken. Neither ever
/// waits, and neither touches the other's slot. A value pushed before the consumer took the one
/// before it replaces that one: only the newest is ever handed over, and it is never lost. For
/// a state, that is the whole contract: a state superseded before the audio thread saw it never
/// needs applying.
///
/// One producer thread and one consumer thread at a time. Either role may move to another
/// thread only when the host orders the calls (for example, the main thread may take() while
/// it knows the audio thread is stopped).
template <typename T> class LatestValue
{
    static_assert(std::is_trivially_copyable_v<T>, "push() and take() copy T without allocating");

  public:
    /// Producer only. Hands `value` over, replacing one the consumer hasn't taken yet.
    void push(const T &value) noexcept
    {
        slots_[back_] = value;
        // Release: the consumer that takes this slot sees the value. Acquire: the slot we get
        // back is one the consumer has finished reading.
        back_ = middle_.exchange(static_cast<uint8_t>(back_ | kFresh), std::memory_order_acq_rel) &
                kIndex;
    }

    /// Consumer only. If a value was pushed since the last take(), copies the newest into `out`
    /// and returns true; otherwise returns false and leaves `out` alone.
    bool take(T &out) noexcept
    {
        // Only take() clears kFresh, so once seen it stays until the exchange below.
        if ((middle_.load(std::memory_order_relaxed) & kFresh) == 0)
        {
            return false;
        }
        front_ = middle_.exchange(front_, std::memory_order_acq_rel) & kIndex;
        out = slots_[front_];
        return true;
    }

  private:
    static constexpr uint8_t kIndex = 0x3;
    static constexpr uint8_t kFresh = 0x4;
    static_assert(std::atomic<uint8_t>::is_always_lock_free);

    std::array<T, 3> slots_{};
    std::atomic<uint8_t> middle_{1}; // a slot index, plus kFresh when it holds an untaken value
    uint8_t back_{0};                // the producer's slot
    uint8_t front_{2};               // the consumer's slot
};
} // namespace gg::plugin

#endif // GG_PLUGIN_LATESTVALUE_H
