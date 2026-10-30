// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_ROUTELIST_H
#define GG_UI_ROUTELIST_H

#include <cstdint>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/ui/Editor.h"

namespace gg::ui
{
/// The mod matrix's 48 rows (M2 sub-project 6's spec §6.6; component ID "routes"), scrolled. Row N
/// (1 to 48) holds, by component ID, its source ("route.N.source"), the source's layer
/// ("route.N.sourceLayer": same or A to D), its target ("route.N.target"), the target's layer
/// ("route.N.targetLayer": A to D or all), its depth in the target's units ("route.N.depth", a
/// slider over ± the target's range), its curve ("route.N.curve") and a clear button
/// ("route.N.clear"). An empty slot reads "—".
///
/// Every edit is one context.setRoutes of the whole matrix and one undo entry ("route N"); a
/// depth slider's drag is one entry. refresh() shows the routes as they are now.
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class RouteList final : public juce::Component
{
  public:
    explicit RouteList(const EditorContext &context);
    ~RouteList() override;

    /// Shows the routes as they are now.
    void refresh();
    /// Outlines row `slot` (1 to 48) and scrolls it into view; 0 or out of range clears it.
    void select(uint8_t slot);
    uint8_t selected() const noexcept;

    void resized() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gg::ui

#endif // GG_UI_ROUTELIST_H
