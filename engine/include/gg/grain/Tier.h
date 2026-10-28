// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_TIER_H
#define GG_GRAIN_TIER_H

#include <cstdint>

namespace gg
{
/// A grain's interpolation tier (INTENT §14), latched at its onset from CloudParams::quality and
/// stored in the pool, so a change reaches only the grains that start after it (ruling 2). The
/// tier changes only how a grain reads its source, never which grains spawn or what they draw.
/// Until M2 only the engine and gg-render choose Eco; both plugins stay Normal.
enum class Tier : uint8_t
{
    Eco,    ///< Linear interpolation over taps floor(pos) and floor(pos) + 1.
    Normal, ///< The 4-point Hermite (Catmull-Rom) over taps floor(pos) − 1 .. floor(pos) + 2.
};
} // namespace gg

#endif // GG_GRAIN_TIER_H
