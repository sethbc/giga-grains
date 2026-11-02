// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "EditView.h"

#include <algorithm>

#include "../LayerStrip.h"
#include "../LoaderStrip.h"
#include "PanelGrid.h"
#include "gg/ui/EditLayout.h"

namespace gg::ui
{
namespace
{
constexpr int kPanelMargin = 2;
constexpr int kStripHeight = 64; // as the Mod view's
constexpr int kStripGap = 4;
constexpr int kLoaderHeight = 28; // the compact sample strip, as Perform's
constexpr Panel kPanels[] = {Panel::Source,  Panel::Ring,  Panel::Scheduler, Panel::Grain,
                             Panel::Pitch,   Panel::Space, Panel::Filter,    Panel::Lfo1,
                             Panel::Lfo2,    Panel::Env1,  Panel::Env2,      Panel::Random1,
                             Panel::Random2, Panel::Step};

/// Whether `c` is shown in its window: it and every ancestor below the root visible. The root's
/// own flag (the editor's, or a view built alone) is its window's, not a view switch's.
bool shownInWindow(const juce::Component &c)
{
    for (const juce::Component *at = &c; at->getParentComponent() != nullptr;
         at = at->getParentComponent())
    {
        if (!at->isVisible())
        {
            return false;
        }
    }
    return true;
}
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
        if (withStrip && c.showSampleLoader)
        {
            loader = std::make_unique<LoaderStrip>(context, 0, Compact::Yes);
            owner.addAndMakeVisible(*loader);
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
            raw.push_back(panels.back().get());
            content.addChildComponent(*panels.back()); // layout() shows those with controls
        }
        layout();
    }

    /// The panels wrapped in the viewport's width (the grid the FX view shares).
    void layout() { layoutPanelGrid(viewport, content, raw, kPanelMargin); }

    /// Builds the panels for the layer set: a layer change, now or when the view shows.
    void rebuild()
    {
        stale = false;
        for (const auto &panel : panels)
        {
            panel->setLayer(layer);
        }
        layout();
    }

    /// Rebuilds if a setLayer waited and the view now shows; true if the view is still stale.
    bool rebuildIfShown()
    {
        if (stale && shownInWindow(owner))
        {
            rebuild();
        }
        return stale;
    }

    EditView &owner;
    const EditorContext &context;
    uint8_t layer{0};
    /// The panels show another layer than `layer`: a setLayer came while the view was hidden
    /// (row 335), so the panels are rebuilt when it shows rather than at every layer change.
    bool stale{false};
    std::function<uint8_t()> selectedLayer;
    std::unique_ptr<LayerStrip> strip;   // the editor's Edit view only
    std::unique_ptr<LoaderStrip> loader; // likewise: the compact sample strip, the layer's
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<ParamPanel>> panels;
    std::vector<ParamPanel *> raw; ///< the panels, for the grid
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
    if (m.loader)
    {
        m.loader->setLayer(layer);
    }
    m.stale = true;
    m.rebuildIfShown();
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
    if (m.loader)
    {
        m.loader->refresh();
    }
    if (m.rebuildIfShown())
    {
        return; // hidden: the panels follow when the view shows
    }
    // One copy of the routes for every panel this tick (row 325).
    const gg::core::ModRoutes routes =
        m.context.routes ? m.context.routes() : gg::core::ModRoutes{};
    for (const auto &panel : m.panels)
    {
        panel->refresh(routes);
    }
}

void EditView::setModMode(bool on)
{
    for (const auto &panel : impl_->panels)
    {
        panel->setModMode(on);
    }
}

std::vector<ParamPanel *> EditView::panels() { return impl_->raw; }

bool EditView::isStale() const noexcept { return impl_->stale; }

void EditView::visibilityChanged() { impl_->rebuildIfShown(); }

void EditView::resized()
{
    auto area = getLocalBounds();
    if (impl_->strip)
    {
        impl_->strip->setBounds(area.removeFromTop(kStripHeight));
        area.removeFromTop(kStripGap);
    }
    if (impl_->loader)
    {
        impl_->loader->setBounds(area.removeFromTop(kLoaderHeight));
        area.removeFromTop(kStripGap);
    }
    impl_->viewport.setBounds(area);
    impl_->layout();
}
} // namespace gg::ui
