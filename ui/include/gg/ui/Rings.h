// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_RINGS_H
#define GG_UI_RINGS_H

#include <cstdint>
#include <vector>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"

namespace gg::ui
{
/// One route's ring on a control (M2 sub-project 6's spec §6.5): framework-free, so the Edit view
/// turns it into a ParamControl::Ring with its palette's colour.
struct RingSpec
{
    uint8_t slot;  ///< the route's matrix slot, 1 to 48
    float depth01; ///< the depth as a fraction of the parameter's range (negative lowers)
    bool bipolar;  ///< the curve swings both ways about the base value (ModCurve::Bipolar)
    uint8_t
        family; ///< the source's family: 0 macros, 1 modulators, 2 MIDI, 3 follower, 4 transport
};

/// A source's family, as RingSpec::family numbers them: 0 macros, 1 modulators, 2 MIDI, 3
/// follower, 4 transport (None counts as a macro's).
uint8_t sourceFamily(gg::core::ModSource source) noexcept;

/// The routes that target a control, in slot order. `target` is the route's target: layer A's
/// slot for a layer parameter, Level or Layer A On, else the global's own; `layer` is the layer
/// the control belongs to (ignored for a global target). A route counts when its source isn't
/// None and its targetLayer is kAllLayers or `layer`. `depth01` is depth / (rangeMax − rangeMin);
/// a range that isn't positive and finite gives none.
std::vector<RingSpec> ringsFor(const gg::core::ModRoutes &routes, gg::core::ParamSlot target,
                               uint8_t layer, float rangeMin, float rangeMax) noexcept;
} // namespace gg::ui

#endif // GG_UI_RINGS_H
