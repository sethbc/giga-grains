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

#include "gg/core/ParamIds.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/RingStorage.h"

namespace gg::core
{
/// A layer's ring from the main thread (M2 sub-project 2's spec §6.5, §6.6), which the audio
/// thread adopts at a call's start: a resize, whose storage's slots hold the window's frames below
/// copiedTo (LiveSource::adoptStorage), or a restore, whose storage holds frames [0, frames)
/// (LiveSource::restoreStorage) with its marks, held from the first sample when `frozen` (the
/// layer's loaded Freeze is on; ruling R1-3).
struct RingOffer
{
    RingStorage *storage{nullptr}; ///< The exchange's, set by offerRing() (and setRingNow).
    int64_t copiedTo{0};           ///< A resize: the head R0 as of the main thread's copy.
    bool restore{false};           ///< A restore, not a resize.
    int64_t frames{0};             ///< A restore: the frames it holds, the restored head.
    bool frozen{false};            ///< A restore: held from the first sample.
    uint32_t spliceCount{0};       ///< A restore: the marks, ascending ring times, below frames.
    std::array<int64_t, kMaxSplices> splices{};
};

/// One hand-off from the main thread to the audio thread: a source (nullptr: no source) and,
/// for a session restored while active (D8), the parameter values that go with it, so that both
/// reach the audio thread as one unit. An offer may instead keep the source the audio side has
/// (`keepSource`, ruling R19): a session restored whose sample is the one already in memory
/// brings only its values, and `source` is then nullptr and ignored.
struct SourceOffer
{
    const PreparedSource *source{nullptr};
    bool keepSource{false};
    bool hasValues{false};
    ParamValues values{};
    /// A ring offer (offerRing), with the serial the audio side reports adopting
    /// (markRingAdopted).
    bool hasRing{false};
    RingOffer ring{};
    uint32_t ringSerial{0};
};

/// The main thread ⇄ audio thread hand-off of PreparedSources (spec §6.4; INTENT §14 "Buffers and
/// memory"): the main thread offers a source, the audio thread takes it and later hands it back,
/// and the main thread frees it. Lock-free on the audio side, which never allocates or frees.
///
/// **Ownership.** The exchange owns every source offered or held through it, as a shared_ptr
/// kept on the main thread, until the audio side has handed it back (retire) and the main thread
/// has collected it, or until an untaken offer is replaced. The audio side only ever sees raw
/// pointers. So every release of a source happens on the main thread: in offer() (an untaken
/// offer, which the audio side never had), in collect(), or in the destructor.
///
/// **The two directions.**
/// - The mailbox, main → audio: a triple buffer of SourceOffer, latest wins. An offer the audio
///   side hasn't polled when the next one comes is replaced, and its source freed, never handed
///   over; but its values, if the newer offer has none, are carried forward into it, so a
///   restored session's values are never lost to a load offered before the audio side took
///   them. An offer of no source is an offer too, so a restored session whose sample is missing
///   goes silent (spec §6.4). A keep-source offer (offerValues) that replaces an untaken offer
///   takes over that offer's source, or its keep, so a source offered but not yet taken still
///   reaches the audio side.
/// - The hand-back, audio → main: a single-producer, single-consumer ring of raw pointers,
///   kRetireCapacity long. A source enters it once. Between two collect() calls only sources the
///   exchange holds can enter it, and with the Processor's use it holds a handful (the engine's
///   draining and playing sources, one waiting for adoption, one offered), far below the
///   capacity. Should the ring ever be full, retire() asserts and keeps the pointer out: the
///   exchange still holds the source and frees it when it is destroyed.
///
/// **Rings** (M2 sub-project 2's spec §6.5). An offer may also carry a RingStorage (offerRing),
/// owned by the exchange as a source is, and handed back once no grain reads it (retireRing):
/// the storage an adoption replaced, or a declined one. One ring is in flight per layer: a ring
/// offer replacing one the audio side hasn't taken frees it. A ring rides the mailbox with the
/// rest of an offer, so a restore's ring, sample and values land in one call: an untaken ring is
/// carried forward into a newer offer of a source or values, and a ring offer replacing an
/// untaken offer takes over its source (or keep) and its values. The audio side adopts or declines
/// it at once and says which (markRingAdopted, markRingDeclined); a declined ring's offer stays
/// pending (ringAdoptionPending) for the main thread to remake, and the declines are counted
/// (ringsDeclined).
///
/// **Threads.** offer(), offerRing(), hold(), holdRing(), collect(), held(), ringsHeld() and
/// ringAdoptionPending() on the main thread; poll(), retire(), retireRing(), markRingAdopted()
/// and markRingDeclined() on the audio thread; awaitingCollect() and ringsDeclined() on either.
/// Each role may move to another thread only when the host orders the calls, for example the main
/// thread may poll() and retire() while it knows the audio thread is stopped (as
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

    /// Main thread. Offers `s` (nullptr: no source) to the audio side, with `values` if given,
    /// replacing an offer not yet polled, whose source is freed here. Without `values`, the
    /// replaced offer's values, if any, go with `s` (the class comment). Then collects
    /// (collect()). Allocates, unless reserve() was called since the last offer or hold.
    void offer(std::shared_ptr<const PreparedSource> s, const ParamValues *values = nullptr);

    /// Main thread. Offers `values` with no change of source (SourceOffer::keepSource, ruling
    /// R19): the audio side applies them and keeps the source it has, or the one an untaken
    /// offer this replaces would have given it (the class comment). Then collects (collect()).
    /// Never allocates.
    void offerValues(const ParamValues &values);

    /// Main thread. Makes room for one more source, so that the next offer() or hold() doesn't
    /// allocate: an all-or-nothing caller allocates here, before it changes anything. Throws
    /// std::bad_alloc.
    void reserve();

    /// Main thread. Holds `s` without offering it, for a caller that gives it to the engine
    /// itself while the audio side is stopped (Processor::setSourceNow), and returns its raw
    /// pointer (nullptr for nullptr). Like an offered source, it must come back through retire().
    /// Allocates.
    const PreparedSource *hold(std::shared_ptr<const PreparedSource> s);

    /// Main thread. Frees the sources and the rings the audio side has retired since the last
    /// call.
    void collect();

    /// Main thread. Offers `storage` to the audio side as `how` describes (its `storage` field is
    /// ignored and set to this one), keeping the source and values an untaken offer had and
    /// freeing an untaken offer's ring (the class comment). Then collects (collect()). Allocates
    /// a little, unless the exchange holds no more rings than it has held before.
    void offerRing(std::unique_ptr<RingStorage> storage, const RingOffer &how);

    /// Main thread. Holds `storage` without offering it, for a caller that gives it to the engine
    /// itself while the audio side is stopped (Processor::setRingNow), and returns its raw pointer
    /// (nullptr for nullptr). Like an offered ring, it must come back through retireRing().
    RingStorage *holdRing(std::unique_ptr<RingStorage> storage);

    /// Main thread. The rings the exchange holds: offered or held, and not yet freed. For tests,
    /// diagnostics and the memory budget's accounting.
    std::size_t ringsHeld() const noexcept { return rings_.size(); }

    /// Main thread. Whether the newest ring offered hasn't been adopted: not yet taken, or taken
    /// and declined (to be remade), until a ring offered since is adopted.
    bool ringAdoptionPending() const noexcept;

    /// Either thread. How many ring offers the audio side has declined since construction.
    uint32_t ringsDeclined() const noexcept;

    /// Main thread. The sources the exchange holds: offered or held, and not yet freed, whether
    /// the audio side has them or not. For tests and diagnostics.
    std::size_t held() const noexcept { return owned_.size(); }

    /// Either thread. Whether a retired source awaits collect(). Never allocates or waits.
    bool awaitingCollect() const noexcept SST_CPPUTILS_NONBLOCKING;

    /// Audio thread. If an offer came since the last poll, copies the newest into `offer` and
    /// returns true; each offer is returned once. Otherwise returns false and leaves `offer`
    /// alone. Never allocates, frees or waits.
    bool poll(SourceOffer &offer) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Audio thread. Hands back `s`, a source poll() returned (or hold() held) that no grain reads
    /// any more, or that was never adopted, for collect() to free. nullptr is ignored. Never
    /// allocates, frees or waits.
    void retire(const PreparedSource *s) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Audio thread. Hands back `storage`, a ring the engine no longer reads (one an adoption
    /// replaced, or one declined), for collect() to free. nullptr is ignored. Never allocates,
    /// frees or waits.
    void retireRing(RingStorage *storage) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Audio thread. The ring of the offer whose ringSerial is `serial` was adopted.
    void markRingAdopted(uint32_t serial) noexcept SST_CPPUTILS_NONBLOCKING;
    /// Audio thread. A ring offer was declined (its storage retired).
    void markRingDeclined() noexcept SST_CPPUTILS_NONBLOCKING;

  private:
    // The mailbox, a triple buffer (as LatestValue): the main thread writes its own slot
    // and swaps it with the middle one; the audio thread swaps its own with the middle one when
    // that holds an offer it hasn't taken.
    static constexpr uint8_t kIndex = 0x3;
    static constexpr uint8_t kFresh = 0x4;
    static_assert(std::atomic<uint8_t>::is_always_lock_free);
    std::array<SourceOffer, 3> slots_{};
    std::atomic<uint8_t> middle_{1}; // a slot index, plus kFresh when it holds an untaken offer
    uint8_t back_{0};                // the main thread's slot
    uint8_t front_{2};               // the audio thread's slot
    // The newest offer published: its values, if it had any, carried forward while it stays
    // untaken, and its source or keep, which a keep-source offer replacing it takes over. Main
    // thread.
    bool lastHadValues_{false};
    ParamValues lastValues_{};
    const PreparedSource *lastSource_{nullptr};
    bool lastKeep_{false};
    // And its ring, carried forward while it stays untaken. Main thread.
    bool lastHasRing_{false};
    RingOffer lastRing_{};
    uint32_t lastRingSerial_{0};

    // The hand-back ring: the audio thread writes ring_[tail_ % capacity] then advances tail_;
    // the main thread reads up to tail_ and advances head_.
    std::array<const PreparedSource *, kRetireCapacity> ring_{};
    std::atomic<std::size_t> head_{0}; // written by the main thread
    std::atomic<std::size_t> tail_{0}; // written by the audio thread

    // Every source held (see Ownership). Main thread.
    std::vector<std::shared_ptr<const PreparedSource>> owned_;

    // The rings (the class comment): every one held, the hand-back ring of retired ones (as
    // ring_), the newest serial offered (main thread) and the newest adopted, and the declines.
    std::vector<std::unique_ptr<RingStorage>> rings_;
    std::array<RingStorage *, kRetireCapacity> ringBack_{};
    std::atomic<std::size_t> ringHead_{0}; // written by the main thread
    std::atomic<std::size_t> ringTail_{0}; // written by the audio thread
    uint32_t ringSerial_{0};
    std::atomic<uint32_t> ringAdopted_{0};
    std::atomic<uint32_t> ringsDeclined_{0};
    static_assert(std::atomic<uint32_t>::is_always_lock_free);

    /// Frees the source `s` points to, if the exchange holds it. Main thread.
    void release(const PreparedSource *s);
    /// Frees the ring `r` points to, if the exchange holds it. Main thread.
    void releaseRing(const RingStorage *r);
    /// offer(), offerValues() and offerRing(): publishes `raw` (already held), or a keep, with
    /// `values` and `ring` (already held, with its serial) when given.
    void publish(const PreparedSource *raw, bool keep, const ParamValues *values,
                 const RingOffer *ring = nullptr, uint32_t serial = 0);
};
} // namespace gg::core

#endif // GG_CORE_SOURCEEXCHANGE_H
