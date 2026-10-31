// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_UISNAPSHOT_H
#define GG_CORE_UISNAPSHOT_H

#include <array>
#include <cstdint>
#include <type_traits>

#include "gg/core/LatestValue.h"
#include "gg/core/ParamIds.h"

namespace gg::core
{
/// One layer's lane, as of the end of one processed block (the Views spec §6.4): what the editor
/// draws for it besides its grains.
struct LaneSnapshot
{
    int64_t writeHead{0};     ///< The layer's live ring's write head, in ring time.
    bool frozen{false};       ///< The freeze setting the layer's engine was given.
    bool on{true};            ///< The layer's on ramp's target or current value is above 0: on, or
                              ///< ramping either way.
    uint8_t builtKind{0};     ///< The gg::SourceMode the layer renders as.
    float basePosition{0.0f}; ///< Position, Spray, Pitch and Pitch Spread as set (base values,
    float baseSpray{0.0f};    ///< CLAP domain) ...
    float basePitch{0.0f};
    float basePitchSpread{0.0f};
    float effectivePosition{0.0f}; ///< ... and as in effect (modulation and macros included).
    float effectiveSpray{0.0f};
    float effectivePitch{0.0f};
    float effectivePitchSpread{0.0f};
    float feedback{0.0f}; ///< The layer's Feedback in effect.
};

/// What the editor draws besides the grains, as of the end of one processed block (spec §6.4):
/// published by the audio thread, latest wins.
struct UiSnapshot
{
    int64_t clock{0};         ///< Frames processed: the Processor's absolute time.
    int64_t writeHead{0};     ///< The live ring's write head, in ring time (FX).
    bool frozen{false};       ///< The freeze setting the engine was given (Freeze, or in the
                              ///< FX a held note): true from the call it engages, also during
                              ///< the 20 ms freeze ramp while the write head still advances.
    uint32_t activeGrains{0}; ///< Grains sounding.
    float basePosition{0.0f}; ///< Position and Spray as set (base values, CLAP domain) ...
    float baseSpray{0.0f};
    float effectivePosition{0.0f}; ///< ... and as in effect (modulation and macros included),
    float effectiveSpray{0.0f};    ///< for the ghost windows.
    float feedback{0.0f};          ///< The Feedback in effect (FX).
    // The Views (M2 sub-project 6's spec §6.4), after M1's fields: every layer's lane, A to D
    // (lanes[0] repeats M1's layer-A fields), and the morph pad as set and as in effect.
    std::array<LaneSnapshot, kLayers> lanes{};
    float baseMorphX{0.0f};
    float baseMorphY{0.0f};
    float effectiveMorphX{0.0f};
    float effectiveMorphY{0.0f};
};
static_assert(std::is_trivially_copyable_v<UiSnapshot>);

/// The audio thread → editor hand-off of UiSnapshots: a triple buffer, so the editor always takes
/// the newest whole snapshot and the audio thread never waits.
using SnapshotBuffer = LatestValue<UiSnapshot>;
} // namespace gg::core

#endif // GG_CORE_UISNAPSHOT_H
