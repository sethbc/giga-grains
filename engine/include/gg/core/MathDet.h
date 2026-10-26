// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_MATHDET_H
#define GG_CORE_MATHDET_H

#include <cmath>

namespace gg
{
namespace detail
{
/// ln(2)^k / k! for k = 0..16, each the nearest double: the Taylor series of 2^f = exp(f·ln2).
inline constexpr double kExp2Taylor[17] = {
    0x1.0p+0,              // k = 0
    0x1.62e42fefa39efp-1,  // k = 1, ln 2
    0x1.ebfbdff82c58fp-3,  // k = 2
    0x1.c6b08d704a0cp-5,   // k = 3
    0x1.3b2ab6fba4e77p-7,  // k = 4
    0x1.5d87fe78a6731p-10, // k = 5
    0x1.430912f86c787p-13, // k = 6
    0x1.ffcbfc588b0c7p-17, // k = 7
    0x1.62c0223a5c824p-20, // k = 8
    0x1.b5253d395e7c4p-24, // k = 9
    0x1.e4cf5158b8ecap-28, // k = 10
    0x1.e8cac7351bb25p-32, // k = 11
    0x1.c3bd650fc2986p-36, // k = 12
    0x1.816193166d0f9p-40, // k = 13
    0x1.314964d5878a9p-44, // k = 14
    0x1.c36e843b04022p-49, // k = 15
    0x1.38e89ae79f8b4p-53, // k = 16
};
} // namespace detail

/// 2^x, bit-identical on every platform. Use it for every value that reaches the grain-event
/// log, such as pitch ratios (INTENT §14): std::exp2 comes from the platform's libm, whose last
/// bits differ between platforms.
///
/// x = n + f with n = floor(x) and f = x - n in [0, 1] (f is 1 only when x - n rounds up, for
/// tiny negative x). 2^f is the degree-16 Taylor polynomial of exp(f·ln2) in Horner form, using
/// IEEE * and + only (truncation error below 6e-18), and 2^x = ldexp(2^f, n). floor and ldexp
/// are exact, so with the project's -ffp-contract=off the result is plain IEEE arithmetic.
/// NaN gives NaN; +inf and x >= 1024 give +inf; -inf and x <= -1075 give 0.
inline double exp2Det(double x) noexcept
{
    if (std::isnan(x))
    {
        return x;
    }
    // Beyond ±1100 ldexp has already saturated to inf or 0; clamping keeps the int cast defined.
    x = x < -1100.0 ? -1100.0 : (x > 1100.0 ? 1100.0 : x);
    const double n = std::floor(x);
    const double f = x - n;
    double p = detail::kExp2Taylor[16];
    for (int k = 15; k >= 0; --k)
    {
        p = p * f + detail::kExp2Taylor[k];
    }
    return std::ldexp(p, static_cast<int>(n));
}
} // namespace gg

#endif // GG_CORE_MATHDET_H
