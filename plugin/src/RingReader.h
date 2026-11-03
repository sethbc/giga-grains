// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_RINGREADER_H
#define GG_PLUGIN_RINGREADER_H

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "PluginTestHooks.h"
#include "gg/core/Processor.h"
#include "gg/source/RingStorage.h"
#include "gg/source/SampleBuffer.h"

namespace gg::plugin
{
/// A copy of part of a layer's ring, read on the main thread without stopping the audio thread
/// (readRing).
struct RingCopy
{
    std::shared_ptr<SampleBuffer> window; ///< stereo, at the rate given, oldest frame first
    int64_t oldest{0};                    ///< the ring time of window's first frame
    int64_t head{0};                      ///< R0: the head the copy is the window as of
    std::vector<int64_t> marks;           ///< the splice marks inside the copy, as ring times
};

/// The span of a layer's ring a read takes (readRing), by ring time, as of the head R0 with N the
/// storage's window:
/// - the window: [max(0, R0 − N), R0), and with Loop on its newest min(L, N) frames, L the Loop
///   Length in frames (llround(seconds × rate), at least 1, as the Processor sets it), the Splice
///   selection aside (Task 8's ruling: a saved ring is this window, Splice reselects on restore);
/// - `splice` k >= 1 (a capture's): the span from the k-th mark inside the window, oldest first
///   (D6), to the next mark or R0; k past the count is the last; with no mark inside, the window.
struct RingSpanRequest
{
    bool loop{false};
    int64_t loopFrames{0};
    uint32_t splice{0};
};

/// Reads layer `layer`'s span `request` (RingSpanRequest) through core::RingRead's protocol (spec
/// §6.4, as Processor.h documents it): the epoch, the storage, the head R0 with acquire order, the
/// copy, an acquire fence, then the head R1 and the epoch again. A frame f of the copy is the one
/// written for it when the epoch held and R1 − f <= N (the documented check, frame by frame): so a
/// copy whose oldest frames the head lapped while it copied is still good from max(from, R1 − N)
/// on. A read is tried up to 8 times for the whole span (a frozen ring passes at once, a running
/// one whose head moved during the copy fails the whole-span check), and a try whose epoch held is
/// kept with its lapped front trimmed (a capture's; a save keeps the window's length and writes
/// silence over that front instead, MediaSession::save) when it is the last, or when its span is
/// longer than N − (R1 − R0), so that at that write rate every try would lap it (a whole window of
/// a running ring: its first try); a try the head lapped whole is never kept. nullopt with no
/// ring, an empty span, the storage replaced under every try, or the copy lapped whole on every
/// try. Main thread only: the storage may be read only on the thread that frees it
/// (Processor::ringRead). Consumes the layer's RingMeta (Processor::ringMeta, which has one
/// consumer), for the marks.
std::optional<RingCopy> readRing(const core::Processor &processor, uint8_t layer,
                                 const RingSpanRequest &request, double rate);

namespace detail
{
/// The tries of one read (readRing's comment).
inline constexpr int kRingReadTries = 8;

/// The ring reader's hook (PluginTestHooks.h, setRingReadHook); nullptr unless a test sets one.
RingReadHook loadRingReadHook() noexcept;

/// The span `request` names as of the head `r0` over a window of `n` frames, and the marks inside
/// the window (`meta`'s, by ring time).
struct Span
{
    int64_t from{0};
    int64_t to{0};
};

Span spanOf(const RingSpanRequest &request, const core::RingMeta &meta, int64_t r0, int64_t n,
            std::vector<int64_t> &marks);

/// The one ring reader (core::RingRead's protocol; readRing's comment). `copy(storage, from, to)`
/// copies frames [from, to) wherever the caller wants them, each try. On success, `from`, `valid`
/// (the first frame of the copy known good) and `to`, the head R0 and the marks inside the window.
struct ReadDone
{
    int64_t from{0};
    int64_t valid{0};
    int64_t to{0};
    int64_t head{0};
    uint32_t epoch{0};  // e0, the storage epoch the copy is of
    bool frozen{false}; // the meta's: the engine was given its freeze
    bool cached{false}; // the probe said the caller holds this copy: nothing was copied
    std::vector<int64_t> marks;
};

/// `probe(meta, e0, r0, span)`, before the copy, may say the caller already holds that copy
/// (the save's cache of a frozen ring): the read then ends there, `cached`, without copying.
/// `replaced`, when given, is set when the read failed with a ring there (the storage replaced
/// under every try, or the whole copy lapped on the last), as against no ring or an empty span.
template <class Probe, class Copy>
std::optional<ReadDone> readWith(const core::Processor &processor, uint8_t layer,
                                 const RingSpanRequest &request, int64_t maxFrames, Probe &&probe,
                                 Copy &&copy, bool *replaced = nullptr)
{
    if (replaced != nullptr)
    {
        *replaced = false;
    }
    ReadDone done;
    for (int attempt = 0; attempt < kRingReadTries; ++attempt)
    {
        const bool last = attempt + 1 == kRingReadTries;
        // 1. The marks (and, for a splice, the span) from the latest meta.
        const core::RingMeta meta = processor.ringMeta(layer);
        // 2. The epoch: odd while an adoption swaps the storage.
        const std::atomic<uint32_t> *epoch = processor.ringRead(layer).storageEpoch;
        const uint32_t e0 = epoch->load(std::memory_order_acquire);
        if ((e0 & 1u) != 0)
        {
            std::this_thread::yield();
            continue;
        }
        // 3. The storage as of e0 or later.
        const core::RingRead read = processor.ringRead(layer);
        if (read.storage == nullptr)
        {
            return std::nullopt;
        }
        const int64_t n = read.storage->frames();
        // 4. The head R0.
        const int64_t r0 = read.head->load(std::memory_order_acquire);
        Span span = spanOf(request, meta, r0, n, done.marks);
        if (maxFrames > 0)
        {
            span.from = std::max(span.from, span.to - maxFrames);
        }
        if (span.to <= span.from)
        {
            return std::nullopt;
        }
        done.frozen = meta.frozen;
        if (probe(meta, e0, r0, span))
        {
            done.from = span.from;
            done.valid = span.from;
            done.to = span.to;
            done.head = r0;
            done.epoch = e0;
            done.cached = true;
            return done;
        }
        // 5. The copy.
        copy(*read.storage, span.from, span.to);
        if (const auto hook = loadRingReadHook())
        {
            hook(layer, attempt);
        }
        // 6. No frame load completes after what follows.
        std::atomic_thread_fence(std::memory_order_acquire);
        // 7. R1 and the epoch again.
        const int64_t r1 = read.head->load(std::memory_order_relaxed);
        const uint32_t e1 = read.storageEpoch->load(std::memory_order_relaxed);
        if (e1 != e0)
        {
            continue;
        }
        // Frame f is good when R1 − f <= N: none written meanwhile shares its slot.
        const int64_t valid = std::max(span.from, r1 - n);
        // A span longer than N − (R1 − R0), what the copy's own writes leave of the window,
        // would be lapped by every try at this write rate: a try again would copy (and
        // allocate) it once more for nothing (eight times 115 MB for a running 300 s ring), so
        // this try is kept as the last one is.
        const bool lapsEveryTry = span.to - span.from > n - (r1 - r0);
        if (valid == span.from || last || lapsEveryTry)
        {
            if (valid >= span.to)
            {
                // The head lapped the whole copy (carry-over row 353): no frame of it is good,
                // so a try is left to make, unless this was the last.
                if (last)
                {
                    break;
                }
                continue;
            }
            done.from = span.from;
            done.valid = valid;
            done.to = span.to;
            done.head = r0;
            done.epoch = e0;
            return done;
        }
    }
    if (replaced != nullptr)
    {
        *replaced = true;
    }
    return std::nullopt;
}

/// readWith with no cache to probe.
template <class Copy>
std::optional<ReadDone> readWith(const core::Processor &processor, uint8_t layer,
                                 const RingSpanRequest &request, int64_t maxFrames, Copy &&copy)
{
    return readWith(
        processor, layer, request, maxFrames,
        [](const core::RingMeta &, uint32_t, int64_t, const Span &) { return false; },
        std::forward<Copy>(copy));
}
} // namespace detail
} // namespace gg::plugin

#endif // GG_PLUGIN_RINGREADER_H
