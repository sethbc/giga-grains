// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "PerformView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "../GrainCanvas.h"
#include "../LayerStrip.h"
#include "../LoaderStrip.h"
#include "../MacroRow.h"
#include "../MorphPad.h"
#include "ModView.h"
#include "gg/ui/LaneModel.h"

namespace gg::ui
{
namespace
{
constexpr int kLoaderHeight = 28;
constexpr int kBottomRowHeight = 104; // M1's macro row
constexpr int kGap = 6;
} // namespace

struct PerformView::Impl
{
    Impl(PerformView &o, const EditorContext &c)
        : owner(o), canvas(c), strip(c, LayerStrip::Layout::Column), pad(c), macros(c)
    {
        if (c.showSampleLoader)
        {
            loader = std::make_unique<LoaderStrip>(c);
            owner.addAndMakeVisible(*loader);
        }
        owner.addAndMakeVisible(canvas);
        owner.addAndMakeVisible(strip); // over the canvas's header column, which it leaves empty
        owner.addAndMakeVisible(pad);
        owner.addAndMakeVisible(macros);

        const auto select = [this](uint8_t layer) {
            owner.setSelectedLayer(layer);
            if (owner.onSelectLayer)
            {
                owner.onSelectLayer(layer);
            }
        };
        canvas.onSelectLayer = select;
        strip.onSelectLayer = select;
        const auto drop = [this, &c](gg::core::ParamSlot slot, gg::core::ModSource source) {
            dropSourceOn(owner, c, slot, source);
        };
        strip.setSourceDropHandler(drop);
        macros.setSourceDropHandler(drop);
    }

    /// The pad's ghost: the snapshot's effective morph, when it differs from the base.
    void refreshGhost()
    {
        const gg::core::UiSnapshot *s = canvas.latestSnapshot();
        std::optional<PadPoint> next;
        if (s != nullptr && (std::abs(s->effectiveMorphX - s->baseMorphX) > kGhostThreshold ||
                             std::abs(s->effectiveMorphY - s->baseMorphY) > kGhostThreshold))
        {
            next = PadPoint{s->effectiveMorphX, s->effectiveMorphY};
        }
        const bool same = next.has_value() == ghost.has_value() &&
                          (!next || (next->x == ghost->x && next->y == ghost->y));
        if (!same)
        {
            ghost = next;
            pad.setGhost(ghost);
        }
    }

    PerformView &owner;
    std::unique_ptr<LoaderStrip> loader; // the instrument's only
    GrainCanvas canvas;
    LayerStrip strip;
    MorphPad pad;
    MacroRow macros;
    std::optional<PadPoint> ghost;
};

PerformView::PerformView(const EditorContext &context)
    : impl_(std::make_unique<Impl>(*this, context))
{
    setComponentID("view.perform");
    setTitle("Perform");
}

PerformView::~PerformView() = default;

void PerformView::setSelectedLayer(uint8_t layer)
{
    impl_->canvas.setSelectedLayer(layer);
    impl_->strip.setSelectedLayer(layer);
}

void PerformView::refresh()
{
    Impl &m = *impl_;
    if (m.loader)
    {
        m.loader->refresh();
    }
    m.strip.refresh();
    m.pad.refresh();
    m.macros.refresh();
    m.refreshGhost();
}

GrainCanvas &PerformView::canvas() noexcept { return impl_->canvas; }

LoaderStrip *PerformView::loaderStrip() noexcept { return impl_->loader.get(); }

void PerformView::resized()
{
    Impl &m = *impl_;
    auto area = getLocalBounds();
    if (m.loader)
    {
        m.loader->setBounds(area.removeFromTop(kLoaderHeight));
        area.removeFromTop(kGap);
    }
    auto bottom = area.removeFromBottom(kBottomRowHeight);
    area.removeFromBottom(kGap);
    m.pad.setBounds(bottom.removeFromLeft(bottom.getHeight()));
    bottom.removeFromLeft(kGap);
    m.macros.setBounds(bottom);
    m.canvas.setBounds(area);

    // The strip is the canvas's header column, each layer's cell beside its lane's rows.
    const int column = std::min(GrainCanvas::kLaneHeaderWidth, area.getWidth());
    m.strip.setBounds(area.withWidth(column));
    std::array<juce::Rectangle<int>, 4> lanes;
    for (uint8_t layer = 0; layer < lanes.size(); ++layer)
    {
        const auto lane = m.canvas.laneBounds(layer);
        lanes[layer] = {0, lane.getY(), column, lane.getHeight()};
    }
    m.strip.setLaneBounds(lanes);
}
} // namespace gg::ui
