// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_LAYERSTRIP_H
#define GG_UI_LAYERSTRIP_H

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/ui/Editor.h"

namespace gg::ui
{
/// The layers' header controls (M2 sub-project 6's spec §6.3, §6.5), component ID "strip": per
/// layer L (0 to 3, A to D) a letter button in the layer's colour ("strip.<L>.letter"), which
/// selects the layer, and the ParamControls of its On switch, Level (a horizontal slider), Source
/// and Freeze (IDs "param.<id>"). The letters take keyboard focus.
///
/// **Layout.** Column (the Perform view): the four layers' cells run down the strip, each
/// spanning its lane's rows, so a layer's controls sit beside its lane (setLaneBounds). Row (the
/// Edit and Mod views): the cells sit side by side in one compact row.
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class LayerStrip final : public juce::Component
{
  public:
    enum class Layout
    {
        Column,
        Row
    };

    LayerStrip(const EditorContext &context, Layout layout);
    ~LayerStrip() override;

    /// Column layout: each layer's cell, in the strip's own coordinates. Until set, and in Row
    /// layout, the cells are the strip's four equal quarters.
    void setLaneBounds(const std::array<juce::Rectangle<int>, 4> &lanes);

    /// The layer outlined as selected (0 to 3; larger values are ignored).
    void setSelectedLayer(uint8_t layer);
    uint8_t selectedLayer() const noexcept;

    /// A letter button was clicked: the layer, already selected here.
    std::function<void(uint8_t)> onSelectLayer;

    /// Shows the base values as they are now.
    void refresh();

    /// Makes every control a drop target for the Mod view's source chips (spec §6.6): a chip
    /// dropped on one calls `handler` with the control's slot and the chip's source. Null turns
    /// the targets off (the default).
    void
    setSourceDropHandler(std::function<void(gg::core::ParamSlot, gg::core::ModSource)> handler);

    void resized() override;
    void paintOverChildren(juce::Graphics &g) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gg::ui

#endif // GG_UI_LAYERSTRIP_H
