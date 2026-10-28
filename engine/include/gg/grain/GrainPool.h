// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_GRAINPOOL_H
#define GG_GRAIN_GRAINPOOL_H

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <sst/cpputils/rtsan_support.h>

namespace gg
{
/// One grain's parameters, latched at its onset (INTENT §14). Times and positions are 64-bit.
struct GrainSpec
{
    uint64_t index;      ///< Grain index k, counted from the scheduler's reset().
    int64_t onsetSample; ///< floor(onset time), in output samples.
    double onsetFrac;    ///< onset time - onsetSample, in [0, 1).
    /// Read position at the onset: source frames in Sample mode, ring-time frames in Live mode.
    double startPos;
    double rate;            ///< Source (or ring) frames advanced per output sample.
    double durationSamples; ///< Window length in output samples, at least 16.
    float gain;
    float pan; ///< -1 (left) .. 1 (right); see panGains().

    friend bool operator==(const GrainSpec &, const GrainSpec &) = default;
};

/// A grain's channel gains.
struct PanGains
{
    float left;
    float right;
};

/// The equal-power pan law with unity at the centre: left = sqrt(1 - pan) and
/// right = sqrt(1 + pan), so left² + right² = 2 (spec §6.4). GrainPool::spawn applies it once
/// per grain, latching gain × each. sqrt is correctly rounded (IEEE 754), so it is deterministic
/// without a polynomial, and pan 0 gives exactly 1 and 1, so a centred grain's two outputs are
/// the one-output render's bit for bit.
inline PanGains panGains(float pan) noexcept
{
    return {std::sqrt(1.0f - pan), std::sqrt(1.0f + pan)};
}

/// One past the last output sample at which g can sound. At output sample t the grain's
/// elapsed time is e = (t - onsetSample) - onsetFrac, and its window is zero once
/// e >= durationSamples. Because onsetFrac < 1, that holds for every
/// t >= onsetSample + floor(durationSamples) + 2. Requires 0 <= durationSamples < 2^62.
constexpr int64_t grainEndSample(const GrainSpec &g) noexcept
{
    return g.onsetSample + static_cast<int64_t>(g.durationSamples) + 2;
}

/// Pointers to the pool's arrays, one per field (INTENT §14). Entries [0, GrainPool::size()) are
/// the stored grains in spawn order; row i of every array belongs to the same grain. The pointers
/// are fixed for the pool's life, but the entries move whenever the pool spawns, retires or
/// clears.
///
/// Besides each GrainSpec field, spawn() latches the values the renderers use (spec §3), each
/// computed once per grain so every renderer reads the same bits.
struct GrainRows
{
    const uint64_t *index;
    const int64_t *onsetSample;
    const double *onsetFrac;
    const double *startPos;
    const double *rate;
    const double *durationSamples;
    const float *gain;
    const float *pan;
    const int64_t *endSample;  ///< grainEndSample of each
    const double *invDuration; ///< 1.0 / durationSamples
    const float *gainL;        ///< gain × panGains(pan).left
    const float *gainR;        ///< gain × panGains(pan).right
};

/// The grain pool: a structure of arrays (INTENT §14), one array per GrainSpec field plus each
/// grain's grainEndSample and the renderers' latched values (GrainRows), kept in spawn order.
/// Only the constructor allocates.
///
/// **Admission** (ruling T3-b) allows capacity() grains to sound at once: canAdmit(onset) is true
/// when fewer than capacity() of the grains spawned since the last clear() still sound at the
/// onset (grainEndSample > onset). A min-heap of their end samples answers that in O(log
/// capacity) without touching the arrays, so a grain that has finished but is still stored
/// doesn't count.
///
/// **Storage** holds 2 × capacity() grains, finished or not, so the engine retires them once per
/// block, after rendering, rather than at each admission (carry-over [103]). At a block's end
/// at most capacity() grains still sound, so storage fills within a block only when more than
/// capacity() grains finish in it; the engine then retires early, which is amortized O(1) per
/// admission.
///
/// Retirement compacts every array stably (never swap-remove), so the grains are always visited
/// in spawn order and every output sample sums its grains in the same order however the host
/// splits blocks (INTENT §8.6, §14).
class GrainPool
{
  public:
    /// Allocates 2 × capacity slots per array, and the heap. capacity must be below 2^31, since
    /// the slot count is a uint32_t (the engine's default capacity is 4096).
    explicit GrainPool(uint32_t capacity);

    // rows() points into this pool's own arrays.
    GrainPool(const GrainPool &) = delete;
    GrainPool &operator=(const GrainPool &) = delete;

    /// The admission limit: the most grains that may sound at once.
    uint32_t capacity() const noexcept { return capacity_; }
    /// The stored grains, finished or not; at most 2 × capacity().
    uint32_t size() const noexcept { return size_; }

    /// Whether a grain with this onset may spawn: true when fewer than capacity() of the grains
    /// spawned since the last clear() have grainEndSample > onset. First forgets every end
    /// <= onset.
    ///
    /// Requires the onsets passed here, and the nowSample of every retireFinished() call before
    /// them, not to decrease between clear()s: an end forgotten at one onset is never counted
    /// again. Whatever restarts time (Engine::reset, a source-mode switch) must clear() the pool.
    bool canAdmit(int64_t onset) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Whether all 2 × capacity() slots are in use, so spawn() would fail.
    bool storageFull() const noexcept { return size_ == slots_; }

    /// Appends g after every stored grain. Returns false, and changes nothing, when storageFull().
    /// Doesn't check canAdmit(): the caller does.
    bool spawn(const GrainSpec &g) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Removes every grain whose grainEndSample is <= nowSample, keeping the rest in spawn order,
    /// and forgets their ends. nowSample must not exceed any onset later passed to canAdmit()
    /// before the next clear().
    void retireFinished(int64_t nowSample) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Removes every grain and forgets every end, so time may restart.
    void clear() noexcept SST_CPPUTILS_NONBLOCKING;

    /// The stored grains' fields, in spawn order.
    const GrainRows &rows() const noexcept { return rows_; }

    /// The i-th stored grain in spawn order, reassembled from the arrays; i < size().
    GrainSpec spec(uint32_t i) const noexcept;

  private:
    uint32_t capacity_;
    uint32_t slots_; // 2 × capacity_

    std::vector<uint64_t> index_;
    std::vector<int64_t> onsetSample_;
    std::vector<double> onsetFrac_;
    std::vector<double> startPos_;
    std::vector<double> rate_;
    std::vector<double> durationSamples_;
    std::vector<float> gain_;
    std::vector<float> pan_;
    std::vector<int64_t> endSample_;
    std::vector<double> invDuration_;
    std::vector<float> gainL_;
    std::vector<float> gainR_;
    GrainRows rows_;
    uint32_t size_{0};
    // The smallest endSample_ among the stored grains, or INT64_MAX when empty: retireFinished
    // has nothing to do before it.
    int64_t earliestEnd_{std::numeric_limits<int64_t>::max()};

    // A min-heap (std::greater) in heap_[0, heapSize_) of the ends canAdmit() still counts. Its
    // entries are a sub-multiset of the stored grains' ends, so it never holds more than slots_.
    std::vector<int64_t> heap_;
    uint32_t heapSize_{0};
};
} // namespace gg

#endif // GG_GRAIN_GRAINPOOL_H
