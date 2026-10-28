// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_MATHDET_H
#define GG_CORE_MATHDET_H

#include <cmath>
#include <limits>

#include "gg/core/FpContract.h"

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

/// 1/(2k+1) for k = 0..12, each the nearest double: the series atanh(s) = Σ s^(2k+1)/(2k+1).
inline constexpr double kLogAtanh[13] = {
    0x1.0p+0,             // k = 0, 1/1
    0x1.5555555555555p-2, // k = 1, 1/3
    0x1.999999999999ap-3, // k = 2, 1/5
    0x1.2492492492492p-3, // k = 3, 1/7
    0x1.c71c71c71c71cp-4, // k = 4, 1/9
    0x1.745d1745d1746p-4, // k = 5, 1/11
    0x1.3b13b13b13b14p-4, // k = 6, 1/13
    0x1.1111111111111p-4, // k = 7, 1/15
    0x1.e1e1e1e1e1e1ep-5, // k = 8, 1/17
    0x1.af286bca1af28p-5, // k = 9, 1/19
    0x1.8618618618618p-5, // k = 10, 1/21
    0x1.642c8590b2164p-5, // k = 11, 1/23
    0x1.47ae147ae147bp-5, // k = 12, 1/25
};

/// ln 2 split in two (fdlibm's split). kLn2Hi has 32 significant bits, so e·kLn2Hi is exact for
/// every binary exponent e of a double; kLn2Hi + kLn2Lo is within 1.2e-26 of ln 2.
inline constexpr double kLn2Hi = 0x1.62e42fee00000p-1;
inline constexpr double kLn2Lo = 0x1.a39ef35793c76p-33;

/// √½, rounded to nearest: logDet's reduced mantissa lies in [kSqrtHalf, 2·kSqrtHalf).
inline constexpr double kSqrtHalf = 0x1.6a09e667f3bcdp-1;
} // namespace detail

/// 2^x, bit-identical on every platform whenever the result is in the normal range
/// (x >= -1022). Use it for every value that reaches the grain-event log, such as pitch ratios
/// (INTENT §14): std::exp2 comes from the platform's libm, whose last bits differ between
/// platforms.
///
/// x = n + f with n = floor(x) and f = x - n in [0, 1] (f is 1 only when x - n rounds up, for
/// tiny negative x). 2^f is the degree-16 Taylor polynomial of exp(f·ln2) in Horner form, using
/// IEEE * and + only (truncation error below 6e-18), and 2^x = ldexp(2^f, n). floor is exact,
/// and so is ldexp while its result is normal, so with the project's -ffp-contract=off the
/// result is plain IEEE arithmetic.
///
/// Subnormal results (x < -1022) are not guaranteed to match across platforms: ldexp has to
/// round them, which is up to the platform's libm, and the FPU's flush-to-zero and
/// denormals-are-zero modes (ScopedDenormalsOff) can turn them into 0.
/// NaN gives NaN; +inf and x >= 1024 give +inf; -inf and x <= -1075 give 0.
inline double exp2Det(double x) noexcept
{
    GG_FP_CONTRACT_OFF
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

/// The natural log of x, bit-identical on every platform for every normal x > 0. Use it for
/// every value that reaches the grain-event log, such as the Cloud scheduler's Poisson intervals
/// (INTENT §14): std::log comes from the platform's libm, whose last bits differ between
/// platforms.
///
/// frexp splits x = m·2^e exactly, and m is moved into [√½, √2) by doubling it (also exact).
/// Then log x = e·ln2 + log m, and log m = 2·atanh(s) with s = (m - 1)/(m + 1), where m - 1 is
/// exact (Sterbenz). atanh(s) is its odd Taylor series s + s³/3 + ... + s²⁵/25: 13 terms, degree
/// 25, in Horner form in s². |s| <= 3 - 2√2 < 0.1716, so s² < 0.02944, and every term past s²⁵
/// together is below 5e-22 relative to s, far under one rounding (1.1e-16). What's left is
/// rounding error: under 3 ulp measured, 1/45 of the bound |logDet(x) - log(x)| <=
/// 1e-14·max(1, |log x|).
/// e·ln2 is e·kLn2Hi, which is exact, plus e·kLn2Lo. Only IEEE + - * / and the exact frexp are
/// used, so with the project's -ffp-contract=off the result is plain IEEE arithmetic.
/// logDet(1) is exactly 0.
///
/// Subnormal x (below 2^-1022) is not guaranteed to match across platforms: under the FPU's
/// denormals-are-zero mode (ScopedDenormalsOff), the x == 0.0 comparison treats it as 0, so
/// logDet returns -inf; without that mode frexp splits it exactly.
/// NaN and x < 0 give NaN; ±0 gives -inf; +inf gives +inf.
inline double logDet(double x) noexcept
{
    GG_FP_CONTRACT_OFF
    if (std::isnan(x))
    {
        return x;
    }
    if (x < 0.0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (x == 0.0)
    {
        return -std::numeric_limits<double>::infinity();
    }
    if (std::isinf(x))
    {
        return x;
    }
    int e = 0;
    double m = std::frexp(x, &e); // x = m·2^e, m in [0.5, 1)
    if (m < detail::kSqrtHalf)
    {
        m = m + m;
        --e;
    }
    const double s = (m - 1.0) / (m + 1.0);
    const double z = s * s;
    double p = detail::kLogAtanh[12];
    for (int k = 11; k >= 0; --k)
    {
        p = p * z + detail::kLogAtanh[k];
    }
    const double logM = (s + s) * p; // 2·atanh(s)
    const double de = static_cast<double>(e);
    return de * detail::kLn2Hi + (de * detail::kLn2Lo + logM);
}
} // namespace gg

#endif // GG_CORE_MATHDET_H
