// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_PARAMPANEL_H
#define GG_UI_PARAMPANEL_H

#include <cstdint>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include <sst/jucegui/components/NamedPanel.h>

#include "../ParamControl.h"
#include "gg/ui/Editor.h"
#include "gg/ui/EditLayout.h"

namespace gg::ui
{
/// One Panel of the placement table (M2 sub-project 6's spec §6.5) for one layer: a
/// ParamControl for each of slotsIn(panel, layer) the product shows, in a cell of its own, a
/// spread pair (Size with Size Spread, Position with Spray, Pitch with Pitch Spread) in one cell,
/// side by side. A Step panel's sixteen steps are 20 px wide vertical sliders. The cells wrap,
/// 72 × 92 for a knob with its label. The panel is hidden while it has no controls (the Filter
/// panel until PR 2).
///
/// **Rings** (spec §6.5, §6.6): each knob draws one arc per route targeting it on this layer,
/// from the context's routes, coloured by the source's family. Only knobs have rings, since
/// ParamControl draws them on knobs only; the Step sliders are not mod targets (the modulators'
/// own controls are refused as targets, isValidTarget), and a menu's or a toggle's ring waits for
/// a design of its own. In mod mode a ring drags its route's depth (the context's setRoutes).
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class ParamPanel final : public sst::jucegui::components::NamedPanel
{
  public:
    ParamPanel(const EditorContext &context, Panel panel, uint8_t layer);
    ~ParamPanel() override;

    Panel panel() const noexcept;
    /// Rebuilds the controls for another layer: the slots change.
    void setLayer(uint8_t layer);
    /// Follows the base values and the routes.
    void refresh();
    void setModMode(bool on);
    std::vector<ParamControl *> controls();

    /// The size that fits the cells in `maxWidth`, wrapping; (0, 0) when the panel has none.
    juce::Point<int> preferredSize(int maxWidth);

    void resized() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gg::ui

#endif // GG_UI_PARAMPANEL_H
