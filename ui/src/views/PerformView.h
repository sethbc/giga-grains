// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_PERFORMVIEW_H
#define GG_UI_PERFORMVIEW_H

#include <cstdint>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/ui/Editor.h"

namespace gg::ui
{
class GrainCanvas;
class LoaderStrip;

/// The Perform view (M2 sub-project 6's spec §6.3; component ID "view.perform"), top to bottom:
/// the instrument's sample loader strip (showSampleLoader), the grain canvas's four lanes with
/// the layer strip in Column layout as their header column, each layer's cell beside its lane,
/// and the bottom row, the XY pad (a square of the row's height) and the eight macros.
///
/// The selected layer is set from outside (setSelectedLayer) and shown by the canvas and the
/// strip; a press in a lane or on a strip's letter selects one (onSelectLayer). The strip's
/// controls take the Mod view's source chips (dropSourceOn), and the macros refuse them with a
/// notice. refresh(), the editor's 30 Hz timer, follows the base values, the sample's status and
/// the pad's ghost (the effective Morph X and Y of the canvas's latest snapshot).
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class PerformView final : public juce::Component
{
  public:
    explicit PerformView(const EditorContext &context);
    ~PerformView() override;

    void setSelectedLayer(uint8_t layer);
    /// A press in a lane or on a strip's letter selected `layer` (the view shows it already).
    std::function<void(uint8_t)> onSelectLayer;

    /// 30 Hz: the strip, the pad, the macros and the loader strip.
    void refresh();

    GrainCanvas &canvas() noexcept;
    /// The instrument's loader strip; null in the FX.
    LoaderStrip *loaderStrip() noexcept;

    void resized() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gg::ui

#endif // GG_UI_PERFORMVIEW_H
