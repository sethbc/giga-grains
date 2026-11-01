// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "FxView.h"

#include <algorithm>

#include "ParamPanel.h"
#include "gg/core/ParamIds.h"

namespace gg::ui
{
namespace
{
constexpr int kPanelMargin = 2;
} // namespace

struct FxView::Impl
{
    Impl(FxView &v, const EditorContext &c)
        : owner(v), reverb(c, Panel::Reverb, 0), output(c, Panel::Output, 0)
    {
        for (ParamControl *control : reverb.controls())
        {
            if (control->slot() == gg::core::ParamSlot::ReverbSize)
            {
                control->setCommitOnRelease(true); // D10: set once, at construction
                control->setTooltip("Sets on release: the room's size changes in one step");
            }
        }
        // In a vertical viewport, as the Edit view's panels are: since M2 sub-project 4's delay
        // and limiter joined the Output panel (ruling VW-T19b), the two panels can outgrow a
        // small window.
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        owner.addAndMakeVisible(viewport);
        content.addChildComponent(reverb);
        content.addChildComponent(output);
    }

    /// The panels side by side, wrapping to a second row when the width is short; a panel is
    /// hidden while it has no controls. The content is sized to hold them, and scrolls.
    void layout()
    {
        viewport.setBounds(owner.getLocalBounds());
        const int width = std::max(1, viewport.getMaximumVisibleWidth());
        const auto r = reverb.preferredSize(width - 2 * kPanelMargin);
        reverb.setVisible(r.x != 0);
        const auto o = output.preferredSize(width - 2 * kPanelMargin);
        output.setVisible(o.x != 0);
        int x = kPanelMargin;
        int y = kPanelMargin;
        int rowHeight = 0;
        int bottom = 0;
        if (r.x != 0)
        {
            reverb.setBounds(x, y, r.x, r.y);
            x += r.x + kPanelMargin;
            rowHeight = r.y;
            bottom = reverb.getBottom() + kPanelMargin;
        }
        if (o.x != 0)
        {
            if (x + o.x + kPanelMargin > width && x > kPanelMargin)
            {
                x = kPanelMargin;
                y += rowHeight + kPanelMargin;
            }
            output.setBounds(x, y, o.x, o.y);
            bottom = std::max(bottom, output.getBottom() + kPanelMargin);
        }
        content.setSize(width, bottom);
    }

    FxView &owner;
    juce::Viewport viewport;
    juce::Component content;
    ParamPanel reverb;
    ParamPanel output;
};

FxView::FxView(const EditorContext &context) : impl_(std::make_unique<Impl>(*this, context))
{
    setComponentID("view.fx");
}

FxView::~FxView() = default;

void FxView::refresh()
{
    impl_->reverb.refresh();
    impl_->output.refresh();
}

void FxView::resized() { impl_->layout(); }
} // namespace gg::ui
