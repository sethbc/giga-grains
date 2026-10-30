// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_MODVIEW_H
#define GG_UI_MODVIEW_H

#include <cstdint>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/ui/Editor.h"

namespace gg::ui
{
/// The Mod view (M2 sub-project 6's spec §6.6; component ID "view.mod"): the compact layer
/// strip, the source strip of 24 chips, the selected layer's Edit panels in mod mode, and the
/// route list at the right (240 px).
///
/// The view is a drag-and-drop container for its chips. A chip dropped on a control routes it
/// (addRouteForDrop, with the control's layer): one setRoutes and one undo entry, or the context's
/// notice when the drop is refused; a route that is already there is selected in the list. A ring
/// drag sets the route's depth, one undo entry for the drag (ParamPanel).
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class ModView final : public juce::Component, public juce::DragAndDropContainer
{
  public:
    explicit ModView(const EditorContext &context);
    ~ModView() override;

    void setLayer(uint8_t layer);
    uint8_t layer() const noexcept;

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
