// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/grain/SincTable.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace gg
{
namespace
{
// The modified Bessel function of the first kind, order 0, by its power series
// I0(x) = Σ_k ((x / 2)^k / k!)², summed until a term is below 1e-12 of the sum. Every term is
// positive, so the sum converges monotonically; for x <= 8 it takes about 30 terms.
double besselI0(double x)
{
    const double half = 0.5 * x;
    double term = 1.0;
    double sum = 1.0;
    for (int k = 1; k < 500; ++k)
    {
        const double q = half / k;
        term *= q * q;
        sum += term;
        if (term < 1e-12 * sum)
        {
            break;
        }
    }
    return sum;
}
} // namespace

SincTable::SincTable()
{
    const auto halfWidth = static_cast<double>(kTaps / 2);
    const double i0Beta = besselI0(kBeta);
    const auto centre = static_cast<int64_t>(kPoints / 2); // x = 0
    for (int64_t j = 0; j < static_cast<int64_t>(kPoints); ++j)
    {
        // x = |−8 + j / 512|, exact in double; the points at ±x get the same value.
        const double x = static_cast<double>(j < centre ? centre - j : j - centre) /
                         static_cast<double>(kPointsPerUnit);
        const double sinc =
            x == 0.0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
        const double r = x / halfWidth;
        const double window = besselI0(kBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0Beta;
        table_[static_cast<std::size_t>(j)] = static_cast<float>(sinc * window);
    }
}
} // namespace gg
