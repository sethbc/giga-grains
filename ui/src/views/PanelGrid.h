// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_PANELGRID_H
#define GG_UI_PANELGRID_H

#include <span>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ParamPanel.h"

namespace gg::ui
{
/// The panel grid the Edit and FX views share (M2 sub-project 6's spec §6.5, §6.7): `panels`, in
/// order, each at its preferredSize for the viewport's visible width, wrapped left to right and
/// top to bottom with `margin` around each, in `content`, the viewport's viewed component, whose
/// size is set to the viewport's visible width and the lowest panel's bottom (its margin
/// included), so the viewport scrolls to every panel. A panel whose preferredSize is {0, 0} (no
/// control the product shows) is hidden; every other is shown. Message thread only.
void layoutPanelGrid(juce::Viewport &viewport, juce::Component &content,
                     std::span<ParamPanel *const> panels, int margin);
} // namespace gg::ui

#endif // GG_UI_PANELGRID_H
