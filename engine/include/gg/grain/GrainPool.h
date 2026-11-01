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

#include "gg/core/FpContract.h"
#include "gg/grain/Tier.h"

namespace gg
{
/// GrainRows::fadeStart of a grain that never fades: neither stolen (spec §6.5) nor cut at its
/// source's edge (carry-over row 300). No output sample is at or after it, so no fade starts.
inline constexpr int64_t kNeverStolen = std::numeric_limits<int64_t>::max();

/// grainEdgeSample of a grain that never reads past its source's last frame while it sounds, and
/// GrainPool::spawn's `edge` for a grain that isn't cut: no output sample is at or after it.
inline constexpr int64_t kNoEdge = std::numeric_limits<int64_t>::max();

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
    /// The Texture envelope's control, [0, 1] (spec §4; Envelope.h): 0 is the plain Hann.
    double texture{0.0};
    float gain;
    float pan;               ///< -1 (left) .. 1 (right); see panGains().
    Tier tier{Tier::Normal}; ///< How the grain reads its source (ruling 2).
    /// The source the grain reads, latched at its onset so it finishes there whatever changes
    /// while it sounds: 0 or 1, one of the engine's two sample-source slots (the sample current
    /// at its onset, kept when another is adopted; Engine::adoptSource), or 2
    /// (Engine::kLiveSlot), the live ring (kept when the source mode changes;
    /// Engine::setSourceMode).
    uint8_t sourceSlot{0};
    /// The layer bus the grain renders into, 0 or 1 (extremes spec §6.3; GrainDraw::bus),
    /// latched at its onset.
    uint8_t bus{0};
    /// What the grain costs while it sounds, grainWeight(tier, stereo) (Budget.h): latched by
    /// Engine::admit with the tier the ladder chose. The pool's ledger sums it.
    uint8_t weight{4};
    /// The mipmap level the grain reads (M2 engine spec §6.3); 0, the full-rate source, until
    /// the engine sets it.
    uint8_t level{0};

    /// Every member, sourceSlot and bus included (as the plan asked): two grains that differ
    /// only in the slot they read, or their bus, compare unequal. Only tests compare GrainSpecs.
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
/// without a polynomial, and pan 0 gives exactly 1 and 1, so a centred grain's output 0 is the
/// one-output render's bit for bit (as is its output 1 with a mono source; from a stereo source
/// output 1 carries channel 1).
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

/// Where grain g, reading a sample source whose level 0 has `frames` frames, reaches the source's
/// edge (carry-over row 300; the director, 2026-10-30: fade the tail): the first output sample t
/// at which its read position, startPos + rate × e with e = (t − onsetSample) − onsetFrac in
/// level-0 frames, as the scalar reference computes it, is past the last frame, frames − 1, while
/// its window may still be open (e × (1 / durationSamples) < 1) and before grainEndSample(g).
/// kNoEdge when there is none: the grain ends inside the source, frames is 0 (nothing is read),
/// or rate isn't > 0. The engine passes it to GrainPool::spawn, which ends the grain there.
///
/// The edge is the last frame itself, the span Engine::sampleGrain clamps the start into, for
/// every tier and level: the same end whatever tier the ladder admits the grain at, and the
/// interpolators' zero taps past it are read only in the fade's last few samples. The start is
/// at or before the last frame and the rate positive, so the read only moves toward this edge: no
/// grain reads before frame 0 while its window is open (a start of 0 reads before it only at
/// e < 0, where the window is 0), so the start needs no edge.
///
/// e and the position are monotone in t, so the answer is the first n = t − onsetSample past
/// onsetFrac + (frames − 1 − startPos) / rate; the division's estimate is stepped (at most four
/// samples each way, which rounding never needs) to the exact test.
inline int64_t grainEdgeSample(const GrainSpec &g, int64_t frames) noexcept
{
    GG_FP_CONTRACT_OFF
    if (frames <= 0 || !(g.rate > 0.0))
    {
        return kNoEdge;
    }
    const auto last = static_cast<double>(frames - 1);
    const int64_t natural = grainEndSample(g);
    const auto past = [&g, last](int64_t t) noexcept {
        GG_FP_CONTRACT_OFF
        const double e = static_cast<double>(t - g.onsetSample) - g.onsetFrac;
        return g.startPos + g.rate * e > last;
    };
    // The first integer n > onsetFrac + (last − startPos) / rate, clamped into [0, natural −
    // onsetSample] (NaN to 0) before converting.
    const auto nMax = static_cast<double>(natural - g.onsetSample);
    double n = std::floor(g.onsetFrac + (last - g.startPos) / g.rate) + 1.0;
    if (!(n > 0.0))
    {
        n = 0.0;
    }
    if (!(n < nMax))
    {
        n = nMax;
    }
    int64_t t = g.onsetSample + static_cast<int64_t>(n);
    for (int k = 0; k < 4 && t > g.onsetSample && past(t - 1); ++k)
    {
        --t;
    }
    for (int k = 0; k < 4 && t < natural && !past(t); ++k)
    {
        ++t;
    }
    if (t >= natural || !past(t))
    {
        return kNoEdge;
    }
    const double e = static_cast<double>(t - g.onsetSample) - g.onsetFrac;
    return e * (1.0 / g.durationSamples) < 1.0 ? t : kNoEdge;
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
    const Tier *tier;
    const uint8_t *sourceSlot;
    const uint8_t *bus;        ///< GrainSpec::bus
    const int64_t *endSample;  ///< grainEndSample of each
    const double *invDuration; ///< 1.0 / durationSamples
    const float *gainL;        ///< gain × panGains(pan).left
    const float *gainR;        ///< gain × panGains(pan).right
    const double *texture;     ///< GrainSpec::texture
    /// envelopeShape(texture, durationSamples) (Envelope.h), one array per member, which every
    /// window site passes to warpPhase.
    const double *envAttackEnd;
    const double *envReleaseStart;
    const double *envAttackScale;
    const double *envReleaseScale;
    const uint8_t *weight; ///< GrainSpec::weight
    const uint8_t *level;  ///< GrainSpec::level
    /// The first output sample any fade of the grain applies at, where the SIMD runs end: the
    /// earlier of the output sample at which it was stolen (spec §6.5; GrainPool::steal) and,
    /// for a grain cut at its source's edge (carry-over row 300; GrainPool::spawn's `edge`), its
    /// edge curve's start, edgeEnd − L with L = min(kEdgeFade, edgeEnd − onsetSample).
    /// kNeverStolen for a grain neither stolen nor cut.
    const int64_t *fadeStart;
    /// The steal fade's 1 / kStealFade for a grain stolen with a fade; 0 for every other grain,
    /// those never stolen or stolen with at most kStealFade samples left, which don't fade
    /// (ruling ES-T6a).
    const float *invFade;
    /// A grain cut at its source's edge: the edge, the sample its curve ends at (its end, unless
    /// a steal ended it sooner), and the curve's 0.5 / L, its phase per sample (ruling EF-6). For
    /// every other grain kNoEdge and 0.
    const int64_t *edgeEnd;
    const double *edgeScale;
};

/// The grain pool: a structure of arrays (INTENT §14), one array per GrainSpec field plus each
/// grain's grainEndSample and the renderers' latched values (GrainRows), kept in spawn order.
/// The renderers (the scalar reference and the 4-wide SIMD kernels in GrainKernels.h) read
/// these arrays directly through rows(). Only the constructor allocates.
///
/// **Admission** (ruling T3-b) allows capacity() grains to sound at once: canAdmit(onset) is true
/// when fewer than capacity() of the grains spawned since the last clear() still sound at the
/// onset (endSample > onset). A min-heap of their ends answers that in O(log capacity) without
/// touching the arrays, so a grain that has finished but is still stored doesn't count.
///
/// **The ledger** (M2 engine spec §6.4; carry-over row 51). Each heap entry carries its grain's
/// weight and row, and the pool keeps the sum of the entries' weights, so soundingWeight(onset)
/// is exactly the weight of the grains still sounding at the onset. Each row keeps its entry's
/// position in the heap, moved by every sift and by moveRows, so steal() can move a grain's end
/// earlier and decrease its heap key in O(log capacity), lowering earliestEnd_ with it: the end
/// is mutable, and retirement and admission both see the moved end.
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
    /// the slot count is a uint32_t (the engine's default capacity is 4096, and it allows at most
    /// kMaxGrainCapacity).
    explicit GrainPool(uint32_t capacity);

    // rows() points into this pool's own arrays.
    GrainPool(const GrainPool &) = delete;
    GrainPool &operator=(const GrainPool &) = delete;

    /// The admission limit: the most grains that may sound at once.
    uint32_t capacity() const noexcept { return capacity_; }
    /// The stored grains, finished or not; at most 2 × capacity().
    uint32_t size() const noexcept { return size_; }

    /// The summed weight of the grains spawned since the last clear() whose end (endSample, as
    /// a steal may have moved it) is after the onset. First forgets every end <= onset.
    ///
    /// Requires the onsets passed here and to soundingCount, canAdmit, oldestSounding and steal,
    /// and the nowSample of every retireFinished() call before them, not to decrease between
    /// clear()s: an end forgotten at one onset is never counted again. Whatever restarts time
    /// (Engine::reset) must clear() the pool.
    uint64_t soundingWeight(int64_t onset) noexcept SST_CPPUTILS_NONBLOCKING;

    /// The part of soundingWeight(onset) that belongs to stolen grains still fading: the summed
    /// weight of the grains stolen with a fade (invFade isn't 0) whose end is after the onset
    /// (ruling ES-T2a; the overload ladder's rung 3 leaves it out of its room test). A grain
    /// stolen with at most kStealFade left doesn't fade, so its weight isn't here: it stays in
    /// the ordinary ledger until its end (ruling ES-T6a). Nor is a grain cut at its source's edge
    /// that wasn't stolen with a fade: it yields to nothing. First forgets every end <= onset,
    /// with soundingWeight's requirement.
    uint64_t fadingWeight(int64_t onset) noexcept SST_CPPUTILS_NONBLOCKING;

    /// How many of the grains spawned since the last clear() have an end after the onset. First
    /// forgets every end <= onset, with soundingWeight's requirement.
    uint32_t soundingCount(int64_t onset) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Whether a grain with this onset may spawn by count: soundingCount(onset) < capacity().
    bool canAdmit(int64_t onset) noexcept SST_CPPUTILS_NONBLOCKING
    {
        return soundingCount(onset) < capacity_;
    }

    /// The oldest grain that may be stolen at the onset (spec §6.5): the first stored row, in
    /// spawn order, whose end is after the onset and that hasn't been stolen (a grain cut at its
    /// source's edge may be, once, as any other). False when there
    /// is none. Its scan starts from a cursor that only advances between retireFinished() calls
    /// (a row it passes has ended or been stolen, so it is never the answer again) and restarts
    /// at each, so the work is bounded by the rows stored.
    bool oldestSounding(int64_t onset, uint32_t &row) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Steals the grain in `row`, which must sound at the onset and not have been stolen; it is
    /// never stolen again. fadeStart becomes min(fadeStart, onset): the onset, or a cut grain's
    /// edge curve start if that comes first. With more than kStealFade samples left, it fades:
    /// its end moves to onset + kStealFade, invFade becomes 1 / kStealFade, its heap key
    /// decreases to the new end and earliestEnd_ follows, and its weight stays in the ledger
    /// until the new end, counted in fadingWeight(). A cut grain keeps its edge curve, which the
    /// steal fade multiplies, so its gain starts from the curve's level at the onset and both
    /// reach 0 by the new end, before the edge. With at most kStealFade left it already ends no
    /// later than a fade would (ruling ES-T6a): its end, its window (invFade stays 0, and a cut
    /// grain's curve stays) and its ledger entry are left alone, and its weight stays in the
    /// ordinary ledger, not fadingWeight(), until that end.
    void steal(uint32_t row, int64_t onset) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Whether all 2 × capacity() slots are in use, so spawn() would fail.
    bool storageFull() const noexcept { return size_ == slots_; }

    /// Appends g after every stored grain. Returns false, and changes nothing, when storageFull().
    /// Doesn't check canAdmit(): the caller does.
    ///
    /// `edge` is where g reads past its source's last frame (grainEdgeSample; carry-over row
    /// 300), or kNoEdge. An edge before grainEndSample(g) cuts the grain there (ruling EF-6): its
    /// end and edgeEnd become max(edge, onsetSample + 1), and it fades out with a raised cosine
    /// over the L = min(kEdgeFade, end − onsetSample) samples before it: edgeScale 0.5 / L, and
    /// fadeStart end − L, where the SIMD runs end, so every fading sample goes through the scalar
    /// reference and every sample before the fade is the uncut grain's. Its weight stays in the
    /// ledger until that end, not as fading weight. An edge at or after the natural end changes
    /// nothing.
    bool spawn(const GrainSpec &g, int64_t edge = kNoEdge) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Removes every grain whose endSample is <= nowSample, keeping the rest in spawn order, and
    /// forgets their ends. nowSample must not exceed any onset later passed to soundingWeight()
    /// and the others before the next clear(). Restarts oldestSounding's cursor.
    void retireFinished(int64_t nowSample) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Removes every grain and forgets every end, so time may restart.
    void clear() noexcept SST_CPPUTILS_NONBLOCKING;

    /// Moves every stored grain whose source slot is below `below` to source slot `slot`, so it
    /// reads that slot's source from now on; the others keep theirs. Engine::setSource passes
    /// Engine::kLiveSlot as `below`: it replaces the sample every sample grain reads, and the
    /// grains reading the ring stay on it.
    void assignSourceSlot(uint8_t slot, uint8_t below) noexcept SST_CPPUTILS_NONBLOCKING;

    /// The stored grains' fields, in spawn order.
    const GrainRows &rows() const noexcept { return rows_; }

    /// The i-th stored grain in spawn order, reassembled from the arrays; i < size().
    GrainSpec spec(uint32_t i) const noexcept;

  private:
    /// Moves rows [from, from + n) of every array down to [to, to + n); to < from. The rows'
    /// heap entries follow them.
    void moveRows(uint32_t from, uint32_t to, uint32_t n) noexcept SST_CPPUTILS_NONBLOCKING;

    // One sounding grain's ledger entry: its end, its weight and its row.
    struct HeapEntry
    {
        int64_t end;
        uint32_t weight;
        uint32_t row;
    };
    // heapPos_ of a row that has no heap entry: its end has been forgotten.
    static constexpr uint32_t kNotInHeap = std::numeric_limits<uint32_t>::max();
    // Writes e at heap position pos and records the position in its row.
    void place(uint32_t pos, const HeapEntry &e) noexcept SST_CPPUTILS_NONBLOCKING;
    void siftUp(uint32_t pos) noexcept SST_CPPUTILS_NONBLOCKING;
    void siftDown(uint32_t pos) noexcept SST_CPPUTILS_NONBLOCKING;
    // Forgets every end <= t: pops them, subtracting their weights.
    void forgetEnds(int64_t t) noexcept SST_CPPUTILS_NONBLOCKING;

    uint32_t capacity_;
    uint32_t slots_; // 2 × capacity_

    // The per-grain arrays, slots_ long. Every array here is also moved by moveRows in
    // GrainPool.cpp, and the constructor points rows_ at it: add a new one to both.
    std::vector<uint64_t> index_;
    std::vector<int64_t> onsetSample_;
    std::vector<double> onsetFrac_;
    std::vector<double> startPos_;
    std::vector<double> rate_;
    std::vector<double> durationSamples_;
    std::vector<float> gain_;
    std::vector<float> pan_;
    std::vector<Tier> tier_;
    std::vector<uint8_t> sourceSlot_;
    std::vector<uint8_t> bus_;
    std::vector<int64_t> endSample_;
    std::vector<double> invDuration_;
    std::vector<float> gainL_;
    std::vector<float> gainR_;
    std::vector<double> texture_;
    std::vector<double> envAttackEnd_;
    std::vector<double> envReleaseStart_;
    std::vector<double> envAttackScale_;
    std::vector<double> envReleaseScale_;
    std::vector<uint8_t> weight_;
    std::vector<uint8_t> level_;
    std::vector<int64_t> fadeStart_;
    std::vector<float> invFade_;
    std::vector<int64_t> edgeEnd_;
    std::vector<double> edgeScale_;
    // Each row's steal: kUnstolen, kStolen (without a fade) or kStolenFading (with one, so its
    // weight is in fadingWeight_). Not in GrainRows: the renderers don't read it. A cut grain has
    // a fadeStart from its spawn, so fadeStart alone doesn't say whether it was stolen.
    static constexpr uint8_t kUnstolen = 0;
    static constexpr uint8_t kStolen = 1;
    static constexpr uint8_t kStolenFading = 2;
    std::vector<uint8_t> stolen_;
    // Each row's position in heap_, or kNotInHeap. Not in GrainRows: the renderers don't read it.
    std::vector<uint32_t> heapPos_;
    GrainRows rows_;
    uint32_t size_{0};
    // The smallest endSample_ among the stored grains, or INT64_MAX when empty: retireFinished
    // has nothing to do before it.
    int64_t earliestEnd_{std::numeric_limits<int64_t>::max()};

    // A min-heap by end in heap_[0, heapSize_) of the grains soundingWeight() still counts: one
    // entry per stored grain whose end hasn't been forgotten, so it never holds more than slots_.
    // heapWeight_ is the sum of the entries' weights, exactly, and fadingWeight_ the sum of the
    // fading ones' (an entry joins it at a steal that fades it and leaves it when its end is
    // forgotten).
    std::vector<HeapEntry> heap_;
    uint32_t heapSize_{0};
    uint64_t heapWeight_{0};
    uint64_t fadingWeight_{0};
    // oldestSounding's scan starts here: every row below it has ended or been stolen.
    uint32_t cursor_{0};
};
} // namespace gg

#endif // GG_GRAIN_GRAINPOOL_H
