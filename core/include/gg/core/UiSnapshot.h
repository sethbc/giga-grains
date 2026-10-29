// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_UISNAPSHOT_H
#define GG_CORE_UISNAPSHOT_H

#include <cstdint>
#include <type_traits>

#include "gg/core/LatestValue.h"

namespace gg::core
{
/// What the editor draws besides the grains, as of the end of one processed block (spec §6.4):
/// published by the audio thread, latest wins.
struct UiSnapshot
{
    int64_t clock{0};         ///< Frames processed: the Processor's absolute time.
    int64_t writeHead{0};     ///< The live ring's write head, in ring time (FX).
    bool frozen{false};       ///< Whether Freeze is in effect.
    uint32_t activeGrains{0}; ///< Grains sounding.
    float basePosition{0.0f}; ///< Position and Spray as set (base values, CLAP domain) ...
    float baseSpray{0.0f};
    float effectivePosition{0.0f}; ///< ... and as in effect (modulation and macros included),
    float effectiveSpray{0.0f};    ///< for the ghost windows.
    float feedback{0.0f};          ///< The Feedback in effect (FX).
};
static_assert(std::is_trivially_copyable_v<UiSnapshot>);

/// The audio thread → editor hand-off of UiSnapshots: a triple buffer, so the editor always takes
/// the newest whole snapshot and the audio thread never waits.
using SnapshotBuffer = LatestValue<UiSnapshot>;
} // namespace gg::core

#endif // GG_CORE_UISNAPSHOT_H
