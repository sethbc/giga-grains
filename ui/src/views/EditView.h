// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_EDITVIEW_H
#define GG_UI_EDITVIEW_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ParamPanel.h"
#include "gg/ui/Editor.h"

namespace gg::ui
{
/// The Edit view (M2 sub-project 6's spec §6.5; component ID "view.edit"): fourteen panels,
/// Source, Ring, Scheduler, Grain, Pitch, Space, Filter, LFO 1, LFO 2, Env 1, Env 2, Random 1,
/// Random 2 and Step, for one layer at a time, wrapped and scrolled in the view's bounds by the
/// panel grid the FX view shares (layoutPanelGrid). The Mod view reuses it with setModMode.
///
/// The layer is set from outside (setLayer), or follows `selectedLayer` when one is given, which
/// refresh() polls. A layer change rebuilds the panels at once only while the view is shown (it
/// and its ancestors below the root visible): a hidden view rebuilds when it is shown, or at the
/// first refresh() that finds it shown (row 335), so a layer change builds one view's panels, not
/// the hidden ones' too. refresh() reads the context's routes once for all the panels (row 325).
/// With `withStrip` (the editor's Edit view, spec §6.2) the compact layer strip sits at the top,
/// and its letters select the layer (onSelectLayer); the Mod view, which has its own, builds the
/// view without.
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class EditView final : public juce::Component
{
  public:
    explicit EditView(const EditorContext &context, bool withStrip = false);
    ~EditView() override;

    void setLayer(uint8_t layer);
    uint8_t layer() const noexcept;
    /// The strip's letter selected `layer` (the view shows it already). The strip only.
    std::function<void(uint8_t)> onSelectLayer;
    /// The layer to show, polled by refresh(); empty (the default) leaves the layer to setLayer.
    void setSelectedLayer(std::function<uint8_t()> selectedLayer);

    /// 30 Hz: the controls and the rings from the routes.
    void refresh();
    /// The controls' own values are inert, their rings draggable.
    void setModMode(bool on);
    /// A source chip was dropped on the control of this slot (mod mode).
    std::function<void(gg::core::ParamSlot, gg::core::ModSource)> onSourceDrop;
    /// A ring was clicked without a drag, with its route's matrix slot (mod mode).
    std::function<void(uint8_t)> onRingClick;
    std::vector<ParamPanel *> panels();
    /// A test hook: true while a setLayer waits for the view to show.
    bool isStale() const noexcept;

    void visibilityChanged() override;
    void resized() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gg::ui

#endif // GG_UI_EDITVIEW_H
