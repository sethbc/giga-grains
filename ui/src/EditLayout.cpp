// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/EditLayout.h"

#include <array>

namespace gg::ui
{
using gg::core::ParamSlot;

namespace
{
/// One row of the table: layer A's slot for a layer parameter or a per-layer global, the slot
/// itself for any other global.
struct Row
{
    ParamSlot slot;
    View view;
    Panel panel;
    uint8_t order;
    ParamSlot pairedSpread;
    bool breakBefore{false};
};

constexpr ParamSlot kNone = ParamSlot::Count;

constexpr Row edit(ParamSlot slot, Panel panel, uint8_t order, ParamSlot spread = kNone)
{
    return {slot, View::Edit, panel, order, spread};
}

/// An Edit row whose cell starts a new line in its panel.
constexpr Row editLine(ParamSlot slot, Panel panel, uint8_t order)
{
    return {slot, View::Edit, panel, order, kNone, true};
}

constexpr Row fx(ParamSlot slot, Panel panel, uint8_t order)
{
    return {slot, View::Fx, panel, order, kNone};
}

/// The pinned table (spec §6.5, §6.7, from INTENT §17 with the modulators added): one row per
/// LayerParam (by layer A's slot), per per-layer global and per other global, each panel's rows
/// in order.
constexpr std::array kRows{
    // Source: the kind (the sample strip is the Source panel's own, not a parameter's).
    edit(ParamSlot::Source, Panel::Source, 0),
    // Ring: M2 sub-project 2's ring parameters, in the Source panel from its merge (ruling SP-M7,
    // after VW-T19a) until PR 2 gave them a panel of their own.
    edit(ParamSlot::RingLength, Panel::Ring, 0),
    edit(ParamSlot::Loop, Panel::Ring, 1),
    edit(ParamSlot::LoopLength, Panel::Ring, 2),
    edit(ParamSlot::Overdub, Panel::Ring, 3),
    edit(ParamSlot::Splice, Panel::Ring, 4),
    // Scheduler.
    edit(ParamSlot::Density, Panel::Scheduler, 0),
    edit(ParamSlot::Distribution, Panel::Scheduler, 1),
    edit(ParamSlot::Freeze, Panel::Scheduler, 2),
    // Grain: Size and Size Spread, Position and Spray, Texture, then M2 sub-project 4's Smear
    // beside it (ruling VW-T19b, placed last until PR 2), then Quality (ruling VW-T19a).
    edit(ParamSlot::Size, Panel::Grain, 0, ParamSlot::SizeSpread),
    edit(ParamSlot::SizeSpread, Panel::Grain, 1),
    edit(ParamSlot::Position, Panel::Grain, 2, ParamSlot::Spray),
    edit(ParamSlot::Spray, Panel::Grain, 3),
    edit(ParamSlot::Texture, Panel::Grain, 4),
    edit(ParamSlot::Smear, Panel::Grain, 5),
    edit(ParamSlot::Quality, Panel::Grain, 6),
    // Pitch and Pitch Spread.
    edit(ParamSlot::Pitch, Panel::Pitch, 0, ParamSlot::PitchSpread),
    edit(ParamSlot::PitchSpread, Panel::Pitch, 1),
    // Space: the layer's diffuser, Feedback and Level (Level B to D through Level's row).
    edit(ParamSlot::Stereo, Panel::Space, 0),
    edit(ParamSlot::Diffusion, Panel::Space, 1),
    edit(ParamSlot::Feedback, Panel::Space, 2),
    edit(ParamSlot::Level, Panel::Space, 3),
    // Filter: the bus split, then each bus's filter on a line of its own (M2 sub-project 4;
    // ruling VW-T19b, one wrapped row until PR 2).
    edit(ParamSlot::BusSplit, Panel::Filter, 0),
    editLine(ParamSlot::Bus1Filter, Panel::Filter, 1),
    edit(ParamSlot::Bus1Cutoff, Panel::Filter, 2),
    edit(ParamSlot::Bus1Resonance, Panel::Filter, 3),
    editLine(ParamSlot::Bus2Filter, Panel::Filter, 4),
    edit(ParamSlot::Bus2Cutoff, Panel::Filter, 5),
    edit(ParamSlot::Bus2Resonance, Panel::Filter, 6),
    // The modulators.
    edit(ParamSlot::Lfo1Rate, Panel::Lfo1, 0),
    edit(ParamSlot::Lfo1Shape, Panel::Lfo1, 1),
    edit(ParamSlot::Lfo2Rate, Panel::Lfo2, 0),
    edit(ParamSlot::Lfo2Shape, Panel::Lfo2, 1),
    edit(ParamSlot::Env1Attack, Panel::Env1, 0),
    edit(ParamSlot::Env1Decay, Panel::Env1, 1),
    edit(ParamSlot::Env1Sustain, Panel::Env1, 2),
    edit(ParamSlot::Env1Release, Panel::Env1, 3),
    edit(ParamSlot::Env2Attack, Panel::Env2, 0),
    edit(ParamSlot::Env2Decay, Panel::Env2, 1),
    edit(ParamSlot::Env2Sustain, Panel::Env2, 2),
    edit(ParamSlot::Env2Release, Panel::Env2, 3),
    edit(ParamSlot::Random1Rate, Panel::Random1, 0),
    edit(ParamSlot::Random1Smooth, Panel::Random1, 1),
    edit(ParamSlot::Random2Rate, Panel::Random2, 0),
    edit(ParamSlot::Random2Smooth, Panel::Random2, 1),
    edit(ParamSlot::StepRate, Panel::Step, 0),
    edit(ParamSlot::StepLength, Panel::Step, 1),
    edit(ParamSlot::Step1, Panel::Step, 2),
    edit(ParamSlot::Step2, Panel::Step, 3),
    edit(ParamSlot::Step3, Panel::Step, 4),
    edit(ParamSlot::Step4, Panel::Step, 5),
    edit(ParamSlot::Step5, Panel::Step, 6),
    edit(ParamSlot::Step6, Panel::Step, 7),
    edit(ParamSlot::Step7, Panel::Step, 8),
    edit(ParamSlot::Step8, Panel::Step, 9),
    edit(ParamSlot::Step9, Panel::Step, 10),
    edit(ParamSlot::Step10, Panel::Step, 11),
    edit(ParamSlot::Step11, Panel::Step, 12),
    edit(ParamSlot::Step12, Panel::Step, 13),
    edit(ParamSlot::Step13, Panel::Step, 14),
    edit(ParamSlot::Step14, Panel::Step, 15),
    edit(ParamSlot::Step15, Panel::Step, 16),
    edit(ParamSlot::Step16, Panel::Step, 17),
    // The FX view (spec §6.7): Reverb; Output, Mix and the engine's Budget (ruling VW-T19a); and
    // M2 sub-project 4's global delay and limiter (in the Output panel by ruling VW-T19b, until
    // PR 2 gave them panels of their own). The view shows them in the signal chain's order.
    fx(ParamSlot::ReverbAmount, Panel::Reverb, 0),
    fx(ParamSlot::ReverbDecay, Panel::Reverb, 1),
    fx(ParamSlot::ReverbSize, Panel::Reverb, 2),
    fx(ParamSlot::ReverbDamping, Panel::Reverb, 3),
    fx(ParamSlot::ReverbPredelay, Panel::Reverb, 4),
    fx(ParamSlot::ReverbModulation, Panel::Reverb, 5),
    fx(ParamSlot::DryToReverb, Panel::Reverb, 6),
    fx(ParamSlot::Mix, Panel::Output, 0),
    fx(ParamSlot::Budget, Panel::Output, 1),
    fx(ParamSlot::DelayAmount, Panel::Delay, 0),
    fx(ParamSlot::DelayTime, Panel::Delay, 1),
    fx(ParamSlot::DelayFeedback, Panel::Delay, 2),
    fx(ParamSlot::DelayDamping, Panel::Delay, 3),
    fx(ParamSlot::DelayPingPong, Panel::Delay, 4),
    fx(ParamSlot::LimiterMode, Panel::Limiter, 0),
    fx(ParamSlot::LimiterCeiling, Panel::Limiter, 1),
    fx(ParamSlot::LimiterRelease, Panel::Limiter, 2),
    // The Perform view: the strip's On switches (Layer B On to D On through A's), the pad, the
    // macros.
    Row{ParamSlot::LayerAOn, View::Perform, Panel::Strip, 0, kNone},
    Row{ParamSlot::MorphX, View::Perform, Panel::Pad, 0, kNone},
    Row{ParamSlot::MorphY, View::Perform, Panel::Pad, 1, kNone},
    Row{ParamSlot::Macro1, View::Perform, Panel::Macros, 0, kNone},
    Row{ParamSlot::Macro2, View::Perform, Panel::Macros, 1, kNone},
    Row{ParamSlot::Macro3, View::Perform, Panel::Macros, 2, kNone},
    Row{ParamSlot::Macro4, View::Perform, Panel::Macros, 3, kNone},
    Row{ParamSlot::Macro5, View::Perform, Panel::Macros, 4, kNone},
    Row{ParamSlot::Macro6, View::Perform, Panel::Macros, 5, kNone},
    Row{ParamSlot::Macro7, View::Perform, Panel::Macros, 6, kNone},
    Row{ParamSlot::Macro8, View::Perform, Panel::Macros, 7, kNone},
};

// One row per layer parameter, per-layer global (Level, Layer On) and other global: 62 + 2 + 27.
static_assert(kRows.size() == gg::core::kLayerParamCount + 2 + 27);

/// A slot as the table keys it, with its layer: layer A's slot and the layer for a layer
/// parameter or a per-layer global; the slot itself and layer 0 for any other.
struct Keyed
{
    ParamSlot key;
    uint8_t layer;
};

constexpr Keyed keyOf(ParamSlot slot) noexcept
{
    if (const auto ref = gg::core::layerOf(slot))
    {
        return {gg::core::layerSlot(ref->param, 0), ref->layer};
    }
    if (const auto layer = gg::core::perLayerGlobalLayer(slot))
    {
        return {slot == gg::core::levelSlot(*layer) ? gg::core::levelSlot(0)
                                                    : gg::core::layerOnSlot(0),
                *layer};
    }
    return {slot, 0};
}

/// Layer `layer`'s slot of a table key (layer A's slot); the key itself for a global.
constexpr ParamSlot inLayer(ParamSlot key, uint8_t layer) noexcept
{
    if (key == kNone)
    {
        return kNone;
    }
    if (const auto ref = gg::core::layerOf(key))
    {
        return gg::core::layerSlot(ref->param, layer);
    }
    if (key == gg::core::levelSlot(0))
    {
        return gg::core::levelSlot(layer);
    }
    if (key == gg::core::layerOnSlot(0))
    {
        return gg::core::layerOnSlot(layer);
    }
    return key;
}

// Every row is keyed by its own slot: layer A's, for a layer parameter or a per-layer global.
static_assert([] {
    for (const Row &row : kRows)
    {
        if (keyOf(row.slot).key != row.slot || keyOf(row.slot).layer != 0)
        {
            return false;
        }
    }
    return true;
}());
} // namespace

std::optional<Placement> placementOf(ParamSlot slot) noexcept
{
    if (slot >= ParamSlot::Count)
    {
        return std::nullopt;
    }
    const Keyed keyed = keyOf(slot);
    for (const Row &row : kRows)
    {
        if (row.slot == keyed.key)
        {
            return Placement{row.view, row.panel, row.order, inLayer(row.pairedSpread, keyed.layer),
                             row.breakBefore};
        }
    }
    return std::nullopt;
}

std::vector<ParamSlot> slotsIn(Panel panel, uint8_t layer)
{
    std::vector<ParamSlot> slots;
    if (panel >= Panel::Count || layer >= gg::core::kLayers)
    {
        return slots;
    }
    // The rows are in each panel's order, so a panel's are already sorted.
    for (const Row &row : kRows)
    {
        if (row.panel == panel)
        {
            slots.push_back(inLayer(row.slot, layer));
        }
    }
    return slots;
}

const char *panelName(Panel panel) noexcept
{
    switch (panel)
    {
    case Panel::Source:
        return "Source";
    case Panel::Scheduler:
        return "Scheduler";
    case Panel::Grain:
        return "Grain";
    case Panel::Pitch:
        return "Pitch";
    case Panel::Space:
        return "Space";
    case Panel::Filter:
        return "Filter";
    case Panel::Lfo1:
        return "LFO 1";
    case Panel::Lfo2:
        return "LFO 2";
    case Panel::Env1:
        return "Env 1";
    case Panel::Env2:
        return "Env 2";
    case Panel::Random1:
        return "Random 1";
    case Panel::Random2:
        return "Random 2";
    case Panel::Step:
        return "Step";
    case Panel::Reverb:
        return "Reverb";
    case Panel::Output:
        return "Output";
    case Panel::Strip:
        return "Layers";
    case Panel::Pad:
        return "Morph";
    case Panel::Macros:
        return "Macros";
    case Panel::Ring:
        return "Ring";
    case Panel::Delay:
        return "Delay";
    case Panel::Limiter:
        return "Limiter";
    case Panel::Count:
        break;
    }
    return "";
}

bool hiddenIn(ParamSlot slot, gg::core::Mode mode) noexcept
{
    return mode == gg::core::Mode::Instrument &&
           (slot == ParamSlot::Mix || slot == ParamSlot::DryToReverb);
}
} // namespace gg::ui
