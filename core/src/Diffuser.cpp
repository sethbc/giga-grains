// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/Diffuser.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace gg::core
{
namespace
{
bool isPrime(uint32_t n)
{
    if (n < 2)
        return false;
    for (uint32_t d = 2; d <= n / d; ++d)
        if (n % d == 0)
            return false;
    return true;
}

/// The smallest prime ≥ n.
uint32_t primeAtLeast(uint32_t n)
{
    while (!isPrime(n))
        ++n;
    return n;
}
} // namespace

void Diffuser::prepare(double sampleRate)
{
    assert(std::isfinite(sampleRate) && sampleRate > 0.0);
    for (size_t i = 0; i < stages_.size(); ++i)
    {
        const double nominal = kLengthsMs[i] * sampleRate / 1000.0;
        assert(nominal < 2147483648.0 && "a sample rate past any real one");
        auto len = primeAtLeast(static_cast<uint32_t>(std::llround(nominal)));
        // Distinct from every earlier length: the next prime up, as often as it takes.
        while (std::any_of(stages_.begin(), stages_.begin() + static_cast<std::ptrdiff_t>(i),
                           [len](const Allpass &a) { return a.line.size() == len; }))
            len = primeAtLeast(len + 1);
        stages_[i].line.assign(len, 0.0f);
        stages_[i].pos = 0;
    }
}

void Diffuser::reset() noexcept
{
    for (auto &a : stages_)
    {
        std::fill(a.line.begin(), a.line.end(), 0.0f);
        a.pos = 0;
    }
}

std::array<uint32_t, 2 * Diffuser::kStages> Diffuser::lengths() const noexcept
{
    std::array<uint32_t, 2 * kStages> out{};
    for (size_t i = 0; i < stages_.size(); ++i)
        out[i] = static_cast<uint32_t>(stages_[i].line.size());
    return out;
}
} // namespace gg::core
