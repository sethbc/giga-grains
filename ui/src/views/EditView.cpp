// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "EditView.h"

#include <algorithm>

#include "../LayerStrip.h"
#include "gg/ui/EditLayout.h"

namespace gg::ui
{
namespace
{
constexpr int kPanelMargin = 2;
constexpr int kStripHeight = 64; // as the Mod view's
constexpr int kStripGap = 4;
constexpr Panel kPanels[] = {Panel::Source, Panel::Scheduler, Panel::Grain,   Panel::Pitch,
                             Panel::Space,  Panel::Filter,    Panel::Lfo1,    Panel::Lfo2,
                             Panel::Env1,   Panel::Env2,      Panel::Random1, Panel::Random2,
                             Panel::Step};
} // namespace

struct EditView::Impl
{
    Impl(EditView &v, const EditorContext &c, bool withStrip) : owner(v), context(c)
    {
        if (withStrip)
        {
            strip = std::make_unique<LayerStrip>(context, LayerStrip::Layout::Row);
            strip->onSelectLayer = [this](uint8_t selected) {
                owner.setLayer(selected);
                if (owner.onSelectLayer)
                {
                    owner.onSelectLayer(selected);
                }
            };
            owner.addAndMakeVisible(*strip);
        }
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        owner.addAndMakeVisible(viewport);
        for (const Panel p : kPanels)
        {
            panels.push_back(std::make_unique<ParamPanel>(context, p, layer));
            panels.back()->onSourceDrop = [this](gg::core::ParamSlot slot,
                                                 gg::core::ModSource source) {
                if (owner.onSourceDrop)
                {
                    owner.onSourceDrop(slot, source);
                }
            };
            panels.back()->onRingClick = [this](uint8_t slot) {
                if (owner.onRingClick)
                {
                    owner.onRingClick(slot);
                }
            };
            content.addChildComponent(*panels.back()); // layout() shows those with controls
        }
        layout();
    }

    /// Wraps the visible panels in the viewport's width and sizes the content to hold them.
    void layout()
    {
        const int width = std::max(1, viewport.getMaximumVisibleWidth());
        juce::FlexBox box;
        box.flexDirection = juce::FlexBox::Direction::row;
        box.flexWrap = juce::FlexBox::Wrap::wrap;
        box.justifyContent = juce::FlexBox::JustifyContent::flexStart;
        box.alignItems = juce::FlexBox::AlignItems::flexStart;
        box.alignContent = juce::FlexBox::AlignContent::flexStart;
        const juce::FlexItem::Margin margin(static_cast<float>(kPanelMargin));
        for (const auto &panel : panels)
        {
            const auto size = panel->preferredSize(width - 2 * kPanelMargin);
            panel->setVisible(size.x != 0); // hidden while empty
            if (size.x == 0)
            {
                continue;
            }
            box.items.add(juce::FlexItem(*panel)
                              .withWidth(static_cast<float>(size.x))
                              .withHeight(static_cast<float>(size.y))
                              .withFlex(0.0f, 0.0f)
                              .withMargin(margin));
        }
        box.performLayout(juce::Rectangle<int>(0, 0, width, 100000));
        int bottom = 0;
        for (const auto &panel : panels)
        {
            if (panel->isVisible())
            {
                bottom = std::max(bottom, panel->getBottom() + kPanelMargin);
            }
        }
        content.setSize(width, bottom);
    }

    EditView &owner;
    const EditorContext &context;
    uint8_t layer{0};
    std::function<uint8_t()> selectedLayer;
    std::unique_ptr<LayerStrip> strip; // the editor's Edit view only
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<ParamPanel>> panels;
};

EditView::EditView(const EditorContext &context, bool withStrip)
    : impl_(std::make_unique<Impl>(*this, context, withStrip))
{
    setComponentID("view.edit");
}

EditView::~EditView() = default;

void EditView::setLayer(uint8_t layer)
{
    Impl &m = *impl_;
    layer = std::min<uint8_t>(layer, gg::core::kLayers - 1);
    if (layer == m.layer)
    {
        return;
    }
    m.layer = layer;
    if (m.strip)
    {
        m.strip->setSelectedLayer(layer);
    }
    for (const auto &panel : m.panels)
    {
        panel->setLayer(layer);
    }
    m.layout();
}

uint8_t EditView::layer() const noexcept { return impl_->layer; }

void EditView::setSelectedLayer(std::function<uint8_t()> selectedLayer)
{
    impl_->selectedLayer = std::move(selectedLayer);
}

void EditView::refresh()
{
    Impl &m = *impl_;
    if (m.selectedLayer)
    {
        setLayer(m.selectedLayer());
    }
    if (m.strip)
    {
        m.strip->refresh();
    }
    for (const auto &panel : m.panels)
    {
        panel->refresh();
    }
}

void EditView::setModMode(bool on)
{
    for (const auto &panel : impl_->panels)
    {
        panel->setModMode(on);
    }
}

std::vector<ParamPanel *> EditView::panels()
{
    std::vector<ParamPanel *> out;
    for (const auto &panel : impl_->panels)
    {
        out.push_back(panel.get());
    }
    return out;
}

void EditView::resized()
{
    auto area = getLocalBounds();
    if (impl_->strip)
    {
        impl_->strip->setBounds(area.removeFromTop(kStripHeight));
        area.removeFromTop(kStripGap);
    }
    impl_->viewport.setBounds(area);
    impl_->layout();
}
} // namespace gg::ui
