// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_ROUTEEDITS_H
#define GG_UI_ROUTEEDITS_H

#include <cstdint>
#include <string>
#include <utility>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"

namespace gg::ui
{
/// What a chip dropped on a control did (M2 sub-project 6's spec §6.6).
struct DropResult
{
    bool added;         ///< a new route went into `slot`
    uint8_t slot;       ///< 1 to 48: the new route's slot, or the existing route's; 0 for a refusal
    std::string notice; ///< "" unless the drop was refused: why, for the user
};

/// The route target for a control's slot, and the layer the control is on: layer A's slot for a
/// layer parameter, Level or Layer A On for a per-layer global; a global's own, with layer 0.
std::pair<gg::core::ParamSlot, uint8_t> routeTargetOf(gg::core::ParamSlot controlSlot) noexcept;

/// Drops `source` on `target` (as routeTargetOf names it) for `targetLayer` (ignored for a
/// global target) into the first empty slot: sourceLayer same, depth a quarter of `meta`'s range
/// (positive), curve Bipolar, or Unipolar for a stepped or on/off target.
///
/// - A route of the same source to the same target (and, for a layer target, layer) already
///   there (a layer target's route to every layer counts for each layer): nothing is added, `slot`
///   is its slot and the notice is empty.
/// - A full matrix: nothing changes, notice "the matrix is full: 48 routes".
/// - A macro or other invalid target (isValidTarget) or no source: nothing changes, with the
///   reason as the notice.
DropResult addRouteForDrop(gg::core::ModRoutes &routes, gg::core::ModSource source,
                           gg::core::ParamSlot target, uint8_t targetLayer,
                           const sst::basic_blocks::params::ParamMetaData &meta);

/// Sets slot `slot`'s (1 to 48) depth, as it is in the target's units. A slot out of range or a
/// depth that isn't finite changes nothing.
void setDepth(gg::core::ModRoutes &routes, uint8_t slot, float depth);

/// Empties slot `slot` (1 to 48): source None and every other field its default. A slot out of
/// range changes nothing.
void clearSlot(gg::core::ModRoutes &routes, uint8_t slot);
} // namespace gg::ui

#endif // GG_UI_ROUTEEDITS_H
