// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_TESTS_SUPPORT_PITCHESTIMATE_H
#define GG_TESTS_SUPPORT_PITCHESTIMATE_H

// Test support: a YIN f0 estimator (de Cheveigne and Kawahara, 2002) with a multi-period
// refinement. A parabolic fit at the first period alone misses 0.1 cent at 4 kHz, where a period
// is 12 samples, so the refinement fits the CMND's minimum near the largest multiple of the period
// that fits the lag window and divides by the multiple.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace gg::test
{
/// 1200 * log2(f / ref).
inline double centsBetween(double f, double ref) { return 1200.0 * std::log2(f / ref); }

/// The f0 of x in [minHz, maxHz], or 0 when no lag's CMND falls under 0.1.
inline double estimateF0(std::span<const float> x, double sampleRate, double minHz,
                         double maxHz) noexcept
{
    const std::size_t n = x.size();
    if (n < 8 || !(minHz > 0.0) || !(maxHz > minHz))
        return 0.0;
    constexpr std::size_t kMaxWindow = 4096;
    const auto lagLo = static_cast<std::size_t>(std::max(2.0, std::floor(sampleRate / maxHz)));
    const auto lagHi = static_cast<std::size_t>(std::ceil(sampleRate / minHz));
    const std::size_t tauMax = std::min(n / 2, std::max(lagHi + 2, kMaxWindow + 2));
    if (lagLo + 2 >= tauMax)
        return 0.0;
    const std::size_t window = std::min(kMaxWindow, n - tauMax);

    // Difference function and its cumulative mean normalization.
    std::vector<double> cmnd(tauMax + 1, 1.0);
    double running = 0.0;
    for (std::size_t tau = 1; tau <= tauMax; ++tau)
    {
        double d = 0.0;
        for (std::size_t j = 0; j < window; ++j)
        {
            const double e = static_cast<double>(x[j]) - static_cast<double>(x[j + tau]);
            d += e * e;
        }
        running += d;
        cmnd[tau] = running > 0.0 ? d * static_cast<double>(tau) / running : 1.0;
    }

    // First lag under the threshold, followed down to its local minimum.
    std::size_t tau0 = 0;
    for (std::size_t tau = lagLo; tau <= std::min(lagHi, tauMax - 1); ++tau)
    {
        if (cmnd[tau] < 0.1)
        {
            while (tau + 1 < tauMax && cmnd[tau + 1] < cmnd[tau])
                ++tau;
            tau0 = tau;
            break;
        }
    }
    if (tau0 == 0)
        return 0.0;

    // Multi-period refinement. The target multiple m is the largest that fits the lag window; the
    // search reaches it in doubling steps, each one centred on the previous estimate's multiple,
    // so the estimate's error times the multiple never leaves the +-2 sample search.
    const std::size_t reach = std::min(kMaxWindow, n / 2);
    std::size_t mTarget = std::max<std::size_t>(1, reach / tau0);
    while (mTarget > 1 && mTarget * tau0 + 4 > tauMax)
        --mTarget;
    auto fit = [&](std::size_t centre) {
        std::size_t best = std::max<std::size_t>(2, centre >= 2 ? centre - 2 : 2);
        const std::size_t hi = std::min(centre + 2, tauMax - 1);
        for (std::size_t t = best; t <= hi; ++t)
            if (cmnd[t] < cmnd[best])
                best = t;
        double r = static_cast<double>(best);
        const double a = cmnd[best - 1];
        const double b = cmnd[best];
        const double c = cmnd[best + 1];
        const double den = a - 2.0 * b + c;
        if (den > 0.0)
            r += 0.5 * (a - c) / den;
        return r;
    };
    double period = static_cast<double>(tau0);
    {
        const double refined = fit(tau0);
        if (std::abs(refined - period) <= 2.0)
            period = refined;
    }
    std::size_t m = 1;
    while (m < mTarget)
    {
        const std::size_t next = std::min(mTarget, m * 2);
        const auto centre =
            static_cast<std::size_t>(std::llround(period * static_cast<double>(next)));
        if (centre + 2 > tauMax)
            break;
        period = fit(centre) / static_cast<double>(next);
        m = next;
    }
    return sampleRate / period;
}
} // namespace gg::test

#endif
