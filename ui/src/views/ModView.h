// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_MODVIEW_H
#define GG_UI_MODVIEW_H

#include <cstdint>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/ui/Editor.h"
#include "gg/ui/RouteEdits.h"

namespace gg::ui
{
/// Routes `source` onto the control of `controlSlot` (spec §6.6), for any view's drop target:
/// addRouteForDrop on the context's routes, then, for a new route, one setRoutes and one undo
/// entry ("route <slot>"); a refusal's notice is posted from `target` (postNotice). Nothing, and
/// slot 0, without the context's routes.
DropResult dropSourceOn(juce::Component &target, const EditorContext &context,
                        gg::core::ParamSlot controlSlot, gg::core::ModSource source);

/// The Mod view (M2 sub-project 6's spec §6.6; component ID "view.mod"): the compact layer
/// strip, the source strip of 24 chips, the selected layer's Edit panels in mod mode, and the
/// route list at the right (240 px).
///
/// The view is a drag-and-drop container for its chips. A chip dropped on a control routes it
/// (dropSourceOn, with the control's layer): one setRoutes and one undo entry, or a notice when
/// the drop is refused; a route that is already there is selected in the list. The strip's
/// controls take drops too. A ring drag sets the route's depth, one undo entry for the drag
/// (ParamPanel).
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class ModView final : public juce::Component, public juce::DragAndDropContainer
{
  public:
    explicit ModView(const EditorContext &context);
    ~ModView() override;

    void setLayer(uint8_t layer);
    uint8_t layer() const noexcept;
    /// The strip's letter selected `layer` (the view shows it already).
    std::function<void(uint8_t)> onSelectLayer;

    /// A source chip dropped on the control of `controlSlot`: routes it, or says why not.
    void dropSource(gg::core::ParamSlot controlSlot, gg::core::ModSource source);

    /// 30 Hz: the strip, the panels' controls and rings, and the list.
    void refresh();

    void resized() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gg::ui

#endif // GG_UI_MODVIEW_H
