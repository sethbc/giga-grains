// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_GRAINQUEUE_H
#define GG_CORE_GRAINQUEUE_H

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

#include <sst/cpputils/rtsan_support.h>

namespace gg::core
{
/// One grain the engine spawned, as the editor draws it (spec §6.4): one compact event per
/// spawn, from the audio thread to the UI thread.
struct GrainVis
{
    int64_t onsetSample{0};      ///< As GrainSpec::onsetSample: output samples, absolute time.
    double startPos{0.0};        ///< Where it starts reading: ring time (a grain reading the
                                 ///< ring) or source frames (a sample), as GrainSpec::startPos.
    float rate{0.0f};            ///< Source (or ring) frames advanced per output sample.
    float durationSamples{0.0f}; ///< Its length in output samples.
    float gain{0.0f};
    float pan{0.0f};  ///< -1 (left) .. 1 (right), as GrainSpec::pan.
    uint8_t layer{0}; ///< The layer that spawned it, 0 to 3 (A to D; M2 sub-project 1's spec §6.7).
};

/// A single-producer, single-consumer ring of N values of T, fixed in size: push never blocks,
/// waits or allocates, and a push to a full queue drops the value and counts it. The audio
/// thread publishes grain events to the editor through one (GrainQueue), and the editor sends
/// parameter changes to the audio thread through another.
///
/// One producer thread and one consumer thread at a time: push() is the producer's, pop() and
/// clear() are the consumer's, and dropped() may be read from any thread. The head and tail are
/// free-running 32-bit counters, so N must be a power of two (it then divides 2^32 and the
/// indices stay right across the counters' wrap).
template <class T, uint32_t N> class SpscQueue
{
    static_assert(std::is_trivially_copyable_v<T>, "push() and pop() copy T without allocating");
    static_assert(N > 0 && (N & (N - 1)) == 0, "the capacity must be a power of two");
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);

  public:
    static constexpr uint32_t kCapacity = N;

    /// Producer only. Appends `value` and returns true, or, when the queue holds kCapacity values,
    /// drops it, counts it in dropped() and returns false.
    bool push(const T &value) noexcept SST_CPPUTILS_NONBLOCKING
    {
        const uint32_t head = head_.load(std::memory_order_relaxed); // only we write it
        // Acquire: the slot we are about to overwrite is one the consumer has finished reading.
        if (head - tail_.load(std::memory_order_acquire) == N)
        {
            // Only the producer writes the count, so a load and a store need no read-modify-write.
            dropped_.store(dropped_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            return false;
        }
        slots_[head & (N - 1)] = value;
        // Release: the consumer that sees the new head sees the value.
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Consumer only. Moves the oldest value into `out` and returns true, or returns false and
    /// leaves `out` alone when the queue is empty.
    bool pop(T &out) noexcept
    {
        const uint32_t tail = tail_.load(std::memory_order_relaxed); // only we write it
        // Acquire: the value in the slot is the producer's, written before it moved the head.
        if (tail == head_.load(std::memory_order_acquire))
        {
            return false;
        }
        out = slots_[tail & (N - 1)];
        // Release: the producer that sees the new tail may reuse the slot.
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    /// The values push() has dropped since construction. Any thread; relaxed, so it may lag.
    uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

    /// Consumer only. Discards every value queued so far (the editor, between frames, when it
    /// doesn't want a backlog). Not a drop: dropped() doesn't change.
    void clear() noexcept
    {
        tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
    }

  private:
    // The producer's and the consumer's counters on cache lines of their own, so neither side's
    // writes slow the other's reads of its own.
    alignas(64) std::atomic<uint32_t> head_{0}; // the next slot push() writes; the producer's
    alignas(64) std::atomic<uint32_t> tail_{0}; // the next slot pop() reads; the consumer's
    alignas(64) std::atomic<uint64_t> dropped_{0};
    std::array<T, N> slots_{};
};

/// The audio thread → editor queue of grain events (spec §6.4). Lossy: when the editor falls
/// behind, the newest grains drop and dropped() counts them.
using GrainQueue = SpscQueue<GrainVis, 4096>;
} // namespace gg::core

#endif // GG_CORE_GRAINQUEUE_H
