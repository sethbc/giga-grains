// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_EDITLAYOUT_H
#define GG_UI_EDITLAYOUT_H

#include <cstdint>
#include <optional>
#include <vector>

#include "gg/core/ParamIds.h"

namespace gg::ui
{
/// The editor's views (M2 sub-project 6's spec §6.2), one visible at a time.
enum class View : uint8_t
{
    Perform,
    Edit,
    Mod,
    Fx
};

/// The panels a view lays its controls out in (spec §6.5, §6.7). Source to Step are the Edit
/// view's (and the Mod view's), one layer's at a time; Reverb and Output are the FX view's; Strip
/// (the layers' On switches), Pad (the morph) and Macros are the Perform view's.
enum class Panel : uint8_t
{
    Source,
    Scheduler,
    Grain,
    Pitch,
    Space,
    Filter,
    Lfo1,
    Lfo2,
    Env1,
    Env2,
    Random1,
    Random2,
    Step,
    Reverb,
    Output,
    Strip,
    Pad,
    Macros,
    Count
};

/// Where a parameter's control is: its view, its panel, its position in the panel (0 first), and
/// the spread drawn beside it as one picture (spec §6.5: Size with Size Spread, Position with
/// Spray, Pitch with Pitch Spread), ParamSlot::Count if none.
struct Placement
{
    View view;
    Panel panel;
    uint8_t order;
    gg::core::ParamSlot pairedSpread;
};

/// The pinned table's row for `slot` (spec §6.5): every parameter below ParamSlot::Count has one.
/// A layer parameter of layer B to D, or a per-layer global (Level B, Layer C On), places as
/// layer A's does, its pairedSpread in its own layer. Nothing for ParamSlot::Count.
std::optional<Placement> placementOf(gg::core::ParamSlot slot) noexcept;

/// The slots a panel draws, in order. A layer's panel (Source to Step, and Space's Level, and
/// the Strip's On switch) draws layer `layer`'s (0 to 3, A to D); a global panel (Reverb, Output,
/// Pad, Macros) ignores it. Empty for a panel with no rows yet (Filter, until PR 2) and for
/// Panel::Count.
std::vector<gg::core::ParamSlot> slotsIn(Panel panel, uint8_t layer);

/// The panel's title as the views show it ("LFO 1", "Random 2"); "" for Panel::Count.
const char *panelName(Panel panel) noexcept;

/// Whether `mode`'s product hides the slot's control: Mix and Dry to Reverb in the instrument,
/// which has no dry signal for them to act on (as its plugin hides them from the host).
bool hiddenIn(gg::core::ParamSlot slot, gg::core::Mode mode) noexcept;
} // namespace gg::ui

#endif // GG_UI_EDITLAYOUT_H
