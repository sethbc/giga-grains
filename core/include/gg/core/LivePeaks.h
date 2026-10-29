// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_LIVEPEAKS_H
#define GG_CORE_LIVEPEAKS_H

#include <atomic>
#include <cstdint>
#include <memory>

#include <sst/cpputils/rtsan_support.h>

namespace gg::core
{
/// The peak strip of the live ring (spec §6.4), which the editor draws under the grains: the
/// peak, max(|L|, |R|), of every kBucket frames the ring receives, bucketed by absolute ring
/// time. The audio thread writes it as the ring fills; the UI thread reads it at any time.
///
/// Frame f belongs to bucket f / kBucket, stored at slot (f / kBucket) % buckets(). prepare()
/// makes ceil(frames / kBucket) + 1 slots, one more than the ring's frames span, so any
/// frames() consecutive frames sit in distinct slots: every frame among the last frames()
/// written reads its own bucket's peak. Older frames read whichever newer bucket took their slot.
///
/// Each slot is a std::atomic<float>, written and read relaxed: no locks, and a reader sees
/// each bucket's peak whole, either before or after a write. A bucket being filled shows the
/// peak so far.
///
/// One writer thread (write) and any number of readers (at, frames, buckets). prepare() is the
/// main thread's, while neither writes nor reads run.
class LivePeaks
{
    static_assert(std::atomic<float>::is_always_lock_free);

  public:
    static constexpr uint32_t kBucket = 64;

    /// Main thread; allocates. Sizes the strip for a ring of `ringFrames` frames (at least 0) and
    /// silences it: every bucket reads 0 until written.
    void prepare(int64_t ringFrames);

    /// Audio thread. Takes in frames t .. t + n − 1 of the ring (absolute ring time, t ≥ 0), from
    /// `l` and `r` (n values each; they may be the same buffer). A bucket that the previous
    /// write() didn't touch starts again from 0 at its first frame here; one it did touch keeps
    /// accumulating, so consecutive writes split anywhere give the buckets one write would. A
    /// non-finite sample counts as 0. Does nothing before prepare().
    void write(const float *l, const float *r, int64_t t,
               uint32_t n) noexcept SST_CPPUTILS_NONBLOCKING;

    /// The writer's, for a ring that restarts at time 0 (Processor::reset): silences every
    /// bucket, as prepare() leaves them, and forgets the last bucket written, so the next write()
    /// starts its first bucket from 0. Keeps the size. Real-time safe; bounded by buckets().
    void reset() noexcept SST_CPPUTILS_NONBLOCKING;

    /// Any thread. The peak of the bucket holding ring frame `frame`; 0 for a negative frame or
    /// before prepare().
    float at(int64_t frame) const noexcept;

    /// The ring's length in frames, as prepare() was given it.
    int64_t frames() const noexcept { return frames_; }

    /// The slots: ceil(frames() / kBucket) + 1, or 0 before prepare().
    uint32_t buckets() const noexcept { return bucketCount_; }

  private:
    std::unique_ptr<std::atomic<float>[]> peaks_;
    int64_t frames_{0};
    uint32_t bucketCount_{0};
    int64_t lastBucket_{-1}; // the bucket the last write() ended in; the writer's
};
} // namespace gg::core

#endif // GG_CORE_LIVEPEAKS_H
