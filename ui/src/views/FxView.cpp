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
        owner.addChildComponent(reverb);
        owner.addChildComponent(output);
    }

    /// The panels side by side, wrapping to a second row when the width is short; the Output
    /// panel is hidden while it has no controls.
    void layout()
    {
        const int width = std::max(1, owner.getWidth());
        const auto r = reverb.preferredSize(width - 2 * kPanelMargin);
        reverb.setVisible(r.x != 0);
        const auto o = output.preferredSize(width - 2 * kPanelMargin);
        output.setVisible(o.x != 0);
        int x = kPanelMargin;
        int y = kPanelMargin;
        int rowHeight = 0;
        if (r.x != 0)
        {
            reverb.setBounds(x, y, r.x, r.y);
            x += r.x + kPanelMargin;
            rowHeight = r.y;
        }
        if (o.x != 0)
        {
            if (x + o.x + kPanelMargin > width && x > kPanelMargin)
            {
                x = kPanelMargin;
                y += rowHeight + kPanelMargin;
            }
            output.setBounds(x, y, o.x, o.y);
        }
    }

    FxView &owner;
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
