// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/Limiter.h"

#include <cassert>
#include <cmath>

namespace gg::core
{
void Limiter::prepare([[maybe_unused]] double sampleRate, uint32_t rampSamples)
{
    assert(std::isfinite(sampleRate) && sampleRate > 0.0);
    ramp_ = rampSamples;
    invRamp_ = rampSamples == 0 ? 0.0f : 1.0f / static_cast<float>(rampSamples);
    reset();
}

void Limiter::reset() noexcept
{
    gain_ = 1.0;
    settle();
}

void Limiter::jump(LimiterMode mode) noexcept
{
    mode_ = mode;
    gain_ = 1.0;
    settle();
}
} // namespace gg::core
