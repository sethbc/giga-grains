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
    uint64_t index;         ///< Grain index k, counted from the scheduler's reset().
    int64_t onsetSample;    ///< floor(onset time), in output samples.
    double onsetFrac;       ///< onset time - onsetSample, in [0, 1).
    double startPos;        ///< Source read position at the onset, in source frames.
    double rate;            ///< Source frames advanced per output sample.
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
/// right = sqrt(1 + pan), so left² + right² = 2 (spec §6.4). The renderer applies it once per
/// grain. sqrt is correctly rounded (IEEE 754), so it is deterministic without a polynomial,
/// and pan 0 gives exactly 1 and 1, which leaves an unpanned grain bit-identical to M0's.
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

/// A fixed-capacity set of live grains, kept in spawn order. Only the constructor allocates.
///
/// Retirement compacts the dense array stably (never swap-remove), so the grains are always
/// visited in spawn order and every output sample sums its grains in the same order however
/// the host splits blocks (INTENT §8.6, §14).
///
/// In M0 the pool is an array of GrainSpec structs (AoS, ruling T3-c), which is what at()
/// returns a reference into. INTENT §14's structure-of-arrays layout is still the target; it
/// arrives with the M1 SIMD kernels, which are what need it.
class GrainPool
{
  public:
    explicit GrainPool(uint32_t capacity);

    uint32_t capacity() const noexcept { return static_cast<uint32_t>(grains_.size()); }
    uint32_t size() const noexcept { return size_; }

    /// Appends g after every live grain. Returns false, and changes nothing, when full.
    bool spawn(const GrainSpec &g) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Removes every grain whose grainEndSample is <= nowSample, keeping the rest in order.
    void retireFinished(int64_t nowSample) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Removes every grain.
    void clear() noexcept SST_CPPUTILS_NONBLOCKING;

    /// The smallest grainEndSample among the live grains, or INT64_MAX when empty: no grain
    /// retires before this sample.
    int64_t earliestEnd() const noexcept { return earliestEnd_; }

    /// The i-th live grain in spawn order; i < size().
    const GrainSpec &at(uint32_t i) const noexcept { return grains_[i]; }

  private:
    std::vector<GrainSpec> grains_; // capacity() slots; [0, size_) are live
    uint32_t size_{0};
    int64_t earliestEnd_{std::numeric_limits<int64_t>::max()};
};
} // namespace gg

#endif // GG_GRAIN_GRAINPOOL_H
