// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/Delay.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace gg::core
{
void Delay::prepare(double sampleRate)
{
    assert(std::isfinite(sampleRate) && sampleRate > 0.0);
    maxDelay_ = std::max(1.0, kMaxSeconds * sampleRate);
    const double frames = std::ceil(maxDelay_) + 1.0;
    assert(frames < 2147483648.0 && "a sample rate past any real one");
    lineL_.assign(static_cast<size_t>(frames), 0.0f);
    lineR_.assign(static_cast<size_t>(frames), 0.0f);
    target_ = 1.0;
    reset();
}

void Delay::reset() noexcept
{
    std::fill(lineL_.begin(), lineL_.end(), 0.0f);
    std::fill(lineR_.begin(), lineR_.end(), 0.0f);
    pos_ = 0;
    dampL_ = 0.0f;
    dampR_ = 0.0f;
    delay_ = target_;
    ran_ = false;
}
} // namespace gg::core
