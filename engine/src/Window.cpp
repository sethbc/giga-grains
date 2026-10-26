// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/Window.h"

#include <cmath>
#include <numbers>

namespace gg
{
HannTable::HannTable()
{
    for (uint32_t i = 0; i <= kSize; ++i)
    {
        // At i = 0, kSize/2 and kSize the phase is exactly 0, pi and 2·pi as doubles, whose
        // cosines round to 1, -1 and 1, so the table is exactly 0, 1 and 0 there.
        const double phase = 2.0 * std::numbers::pi * i / kSize;
        table_[i] = static_cast<float>(0.5 - 0.5 * std::cos(phase));
    }
}
} // namespace gg
