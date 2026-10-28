// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_SOURCEEXCHANGE_H
#define GG_CORE_SOURCEEXCHANGE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <sst/cpputils/rtsan_support.h>

#include "gg/source/PreparedSource.h"

namespace gg::core
{
/// The main thread ⇄ audio thread hand-off of PreparedSources (spec §6.4; INTENT §14 "Buffers and
/// memory"): the main thread offers a source, the audio thread takes it and later hands it back,
/// and the main thread frees it. Lock-free on the audio side, which never allocates or frees.
///
/// **Ownership.** The exchange owns every source offered to it, as a shared_ptr held on the main
/// thread, until the audio side has handed it back (retire) and the main thread has collected it,
/// or until an untaken offer is replaced. The audio side only ever sees raw pointers. So every
/// release of a source happens on the main thread: in offer() (an untaken offer, which the audio
/// side never had), in collect(), or in the destructor.
///
/// **The two directions.**
/// - The mailbox, main → audio: a single slot, latest wins. An offer the audio side hasn't
///   polled when the next one comes is dropped, never handed over. nullptr is an offer too: of no
///   source, so that a restored session whose sample is missing goes silent (spec §6.4).
/// - The hand-back, audio → main: a bounded single-producer, single-consumer ring of raw
///   pointers, kRetireCapacity long. A source enters it once, after it was polled. Between two
///   collect() calls at most every source polled since the one before can enter it: with the
///   Processor's use (one source pending adoption, the engine's current and draining ones, and
///   one more offered) that is a handful, far below the capacity. Should the ring ever be full,
///   retire() drops the pointer: the exchange still holds the source and frees it when it is
///   destroyed, so nothing leaks past the exchange's life.
///
/// **Threads.** offer(), collect() and held() on the main thread; poll() and retire() on the audio
/// thread. Each role may move to another thread only when the host orders the calls, for example
/// the main thread may retire() while it knows the audio thread is stopped (as
/// Processor::setSourceNow does).
class SourceExchange
{
  public:
    /// The most retired sources awaiting collect().
    static constexpr std::size_t kRetireCapacity = 16;

    SourceExchange();
    /// Frees every source still held, on the thread that destroys the exchange: the main thread,
    /// with the audio thread stopped.
    ~SourceExchange();
    SourceExchange(const SourceExchange &) = delete;
    SourceExchange &operator=(const SourceExchange &) = delete;

    /// Main thread. Offers `s` (nullptr: no source) to the audio side, replacing an offer not yet
    /// polled, which is dropped and freed here. Then collects (collect()). Allocates.
    void offer(std::shared_ptr<const PreparedSource> s);

    /// Main thread. Frees the sources the audio side has retired since the last call.
    void collect();

    /// Main thread. The sources the exchange holds: offered and not yet freed, whether the audio
    /// side has them or not. For tests and diagnostics.
    std::size_t held() const noexcept { return owned_.size(); }

    /// Audio thread. If an offer came since the last poll, sets `source` to the newest one
    /// (nullptr for an offer of no source) and returns true; each offer is returned once.
    /// Otherwise returns false and leaves `source` alone. Never allocates, frees or waits.
    bool poll(const PreparedSource *&source) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Audio thread. Hands back `s`, a source poll() returned that no grain reads any more (or
    /// that was never adopted), for collect() to free. nullptr is ignored. Never allocates, frees
    /// or waits.
    void retire(const PreparedSource *s) noexcept SST_CPPUTILS_NONBLOCKING;

  private:
    // The mailbox: 0 when empty, else the offered pointer's bits | kFresh (a PreparedSource is
    // aligned to more than 1 byte, so its low bit is free; nullptr | kFresh is "no source").
    static constexpr uintptr_t kFresh = 1;
    static_assert(alignof(PreparedSource) > 1);
    static_assert(std::atomic<uintptr_t>::is_always_lock_free);
    std::atomic<uintptr_t> mailbox_{0};

    // The hand-back ring: the audio thread writes ring_[tail_ % capacity] then advances tail_;
    // the main thread reads up to tail_ and advances head_.
    std::array<const PreparedSource *, kRetireCapacity> ring_{};
    std::atomic<std::size_t> head_{0}; // written by the main thread
    std::atomic<std::size_t> tail_{0}; // written by the audio thread

    // Every source held (see Ownership). Main thread.
    std::vector<std::shared_ptr<const PreparedSource>> owned_;

    /// Frees the source `s` points to, if the exchange holds it. Main thread.
    void release(const PreparedSource *s);
};
} // namespace gg::core

#endif // GG_CORE_SOURCEEXCHANGE_H
