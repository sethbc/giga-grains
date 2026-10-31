// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_SINCTABLE_H
#define GG_GRAIN_SINCTABLE_H

#include <algorithm>
#include <array>
#include <cstdint>

#include "gg/core/FpContract.h"

namespace gg
{
/// The HQ tier's windowed sinc (spec §6.2): h(x) = sinc(x) × w(x / 8) for |x| <= 8, with sinc(x)
/// = sin(πx) / (πx) and w the Kaiser window w(r) = I0(β √(1 − r²)) / I0(β), tabulated at
/// kPointsPerUnit points per unit, x = −8 + j / 512 for j in [0, kPoints), and read with linear
/// interpolation. The constructor computes the table in double (std::sin, and I0 by its power
/// series to a relative 1e-12) and stores floats; at() is real-time safe. The points are
/// symmetric bit for bit: each is computed at |x|.
///
/// kernels::Sinc reads a source at pos through taps floor(pos) − kBefore .. floor(pos) + kAfter,
/// weighting tap k by at((k − frac) × stretch).
class SincTable
{
  public:
    static constexpr int64_t kTaps = 16;
    static constexpr int64_t kBefore = kTaps / 2 - 1; ///< 7
    static constexpr int64_t kAfter = kTaps / 2;      ///< 8
    static constexpr uint32_t kPointsPerUnit = 512;
    /// x in [−8, 8]: 16 × 512 + 1 points, 32 KiB of floats.
    static constexpr uint32_t kPoints = static_cast<uint32_t>(kTaps) * kPointsPerUnit + 1;
    /// The Kaiser window's β. At stretch 1 it puts the passband within 0.1 dB to 0.3 fs and the
    /// images at least 70 dB down from 0.7 fs (sinc_table_tests.cpp; measured 0.0007 dB and
    /// −81.8 dB).
    static constexpr double kBeta = 8.0;

    SincTable();

    /// h(x), or 0 if x is outside [−8, 8] or NaN: with u = (x + 8) × 512, j = min(uint32(u),
    /// kPoints − 2) and t = u − j, points j and j + 1 lerped as p[j] + (p[j + 1] − p[j]) × t, in
    /// float, in that order (the SIMD kernels' reads must keep it).
    float at(float x) const noexcept
    {
        GG_FP_CONTRACT_OFF
        if (!(x >= -8.0f && x <= 8.0f))
        {
            return 0.0f;
        }
        const float u = (x + 8.0f) * static_cast<float>(kPointsPerUnit); // exact: a power of two
        // x == 8 is the end of the last segment.
        const uint32_t j = std::min(static_cast<uint32_t>(u), kPoints - 2);
        const float t = u - static_cast<float>(j);
        return table_[j] + (table_[j + 1] - table_[j]) * t;
    }

    /// The kPoints tabulated points, which at() interpolates between.
    const float *points() const noexcept { return table_.data(); }

  private:
    std::array<float, kPoints> table_{};
};
} // namespace gg

#endif // GG_GRAIN_SINCTABLE_H
