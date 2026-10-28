// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_INTERPOLATE_H
#define GG_CORE_INTERPOLATE_H

#include <cmath>
#include <cstdint>

#include "gg/core/FpContract.h"

/// Fractional-position readers for one channel of `frames` samples. Taps outside [0, frames)
/// read as zero, so no position (including NaN and ±inf) reads outside the buffer, and `data`
/// may be null when `frames` is 0.
namespace gg::interp
{
namespace detail
{
inline float tap(const float *data, int64_t frames, int64_t i) noexcept
{
    GG_FP_CONTRACT_OFF
    return (i >= 0 && i < frames) ? data[i] : 0.0f;
}
} // namespace detail

/// Linear interpolation between taps floor(pos) and floor(pos) + 1.
inline float linear(const float *data, int64_t frames, double pos) noexcept
{
    GG_FP_CONTRACT_OFF
    // Outside (-1, frames) both taps are zero. This check also rejects NaN and ±inf before the
    // integer conversion.
    if (!(pos > -1.0 && pos < static_cast<double>(frames)))
    {
        return 0.0f;
    }
    const double base = std::floor(pos);
    const auto i = static_cast<int64_t>(base);
    const auto t = static_cast<float>(pos - base);
    const float a = detail::tap(data, frames, i);
    const float b = detail::tap(data, frames, i + 1);
    return a + (b - a) * t;
}

/// The 4-point Catmull-Rom curve through taps p0 .. p3 at t in [0, 1) between p1 and p2. Every
/// Hermite reader (hermite4, the live ring) goes through this one expression, so they agree bit
/// for bit.
inline float catmullRom(float p0, float p1, float p2, float p3, float t) noexcept
{
    GG_FP_CONTRACT_OFF
    const float c1 = 0.5f * (p2 - p0);
    const float c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    const float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
    return ((c3 * t + c2) * t + c1) * t + p1;
}

/// 4-point Catmull-Rom over taps floor(pos) - 1 .. floor(pos) + 2. Reproduces polynomials up to
/// degree 2 exactly (up to float rounding).
inline float hermite4(const float *data, int64_t frames, double pos) noexcept
{
    GG_FP_CONTRACT_OFF
    // Outside (-2, frames + 1) all four taps are zero (at exactly -2 the curve passes through
    // tap -2, which is zero). This check also rejects NaN and ±inf before the integer conversion.
    if (!(pos > -2.0 && pos < static_cast<double>(frames) + 1.0))
    {
        return 0.0f;
    }
    const double base = std::floor(pos);
    const auto i = static_cast<int64_t>(base);
    const auto t = static_cast<float>(pos - base);
    return catmullRom(detail::tap(data, frames, i - 1), detail::tap(data, frames, i),
                      detail::tap(data, frames, i + 1), detail::tap(data, frames, i + 2), t);
}
} // namespace gg::interp

#endif // GG_CORE_INTERPOLATE_H
