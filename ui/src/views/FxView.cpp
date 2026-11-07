// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "FxView.h"

#include <memory>
#include <vector>

#include "PanelGrid.h"
#include "ParamPanel.h"
#include "gg/core/ModMatrix.h"

namespace gg::ui
{
namespace
{
constexpr int kPanelMargin = 2;
/// The panels in the signal chain's order (core/src/Processor.cpp): the delay after the layers'
/// sum, then the reverb, then the limiter on the wet side, then the output's Mix (ruling VW-P2e).
constexpr Panel kPanels[] = {Panel::Delay, Panel::Reverb, Panel::Limiter, Panel::Output,
                             Panel::Midi};
} // namespace

struct FxView::Impl
{
    Impl(FxView &v, const EditorContext &c) : owner(v), context(c)
    {
        // In a vertical viewport, as the Edit view's panels are: the four can outgrow a small
        // window.
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        owner.addAndMakeVisible(viewport);
        for (const Panel p : kPanels)
        {
            panels.push_back(std::make_unique<ParamPanel>(context, p, 0)); // global: any layer
            raw.push_back(panels.back().get());
            content.addChildComponent(*panels.back()); // the grid shows those with controls
        }
    }

    FxView &owner;
    const EditorContext &context;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<ParamPanel>> panels;
    std::vector<ParamPanel *> raw; ///< the panels, for the grid
};

FxView::FxView(const EditorContext &context) : impl_(std::make_unique<Impl>(*this, context))
{
    setComponentID("view.fx");
}

FxView::~FxView() = default;

void FxView::refresh()
{
    const gg::core::ModRoutes routes =
        impl_->context.routes ? impl_->context.routes() : gg::core::ModRoutes{};
    for (const auto &panel : impl_->panels)
    {
        panel->refresh(routes);
    }
}

void FxView::resized()
{
    impl_->viewport.setBounds(getLocalBounds());
    layoutPanelGrid(impl_->viewport, impl_->content, impl_->raw, kPanelMargin);
}
} // namespace gg::ui
