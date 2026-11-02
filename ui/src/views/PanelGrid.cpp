// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "PanelGrid.h"

#include <algorithm>

namespace gg::ui
{
void layoutPanelGrid(juce::Viewport &viewport, juce::Component &content,
                     std::span<ParamPanel *const> panels, int margin)
{
    const int width = std::max(1, viewport.getMaximumVisibleWidth());
    juce::FlexBox box;
    box.flexDirection = juce::FlexBox::Direction::row;
    box.flexWrap = juce::FlexBox::Wrap::wrap;
    box.justifyContent = juce::FlexBox::JustifyContent::flexStart;
    box.alignItems = juce::FlexBox::AlignItems::flexStart;
    box.alignContent = juce::FlexBox::AlignContent::flexStart;
    const juce::FlexItem::Margin itemMargin(static_cast<float>(margin));
    for (ParamPanel *panel : panels)
    {
        const auto size = panel->preferredSize(width - 2 * margin);
        const bool shown = size.x != 0 && size.y != 0;
        panel->setVisible(shown);
        if (!shown)
        {
            continue;
        }
        box.items.add(juce::FlexItem(*panel)
                          .withWidth(static_cast<float>(size.x))
                          .withHeight(static_cast<float>(size.y))
                          .withFlex(0.0f, 0.0f)
                          .withMargin(itemMargin));
    }
    box.performLayout(juce::Rectangle<int>(0, 0, width, 100000));
    int bottom = 0;
    for (const ParamPanel *panel : panels)
    {
        if (panel->isVisible())
        {
            bottom = std::max(bottom, panel->getBottom() + margin);
        }
    }
    content.setSize(width, bottom);
}
} // namespace gg::ui
