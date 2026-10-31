// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "ModView.h"

#include <algorithm>
#include <string>

#include "../LayerStrip.h"
#include "../NoticeSink.h"
#include "EditView.h"
#include "RouteList.h"
#include "SourceStrip.h"
#include "gg/ui/RouteEdits.h"
#include "gg/ui/UndoStack.h"

namespace gg::ui
{
namespace
{
constexpr int kListW = 240;
constexpr int kStripH = 64;
constexpr int kMargin = 4;
} // namespace

struct ModView::Impl
{
    Impl(ModView &o, const EditorContext &c)
        : owner(o), context(c), layers(c, LayerStrip::Layout::Row), edit(c), list(c)
    {
        owner.addAndMakeVisible(layers);
        owner.addAndMakeVisible(sources);
        owner.addAndMakeVisible(edit);
        owner.addAndMakeVisible(list);
        edit.setModMode(true);
        edit.onRingClick = [this](uint8_t slot) { list.select(slot); };
        sources.setLayer(0);
        edit.onSourceDrop = [this](gg::core::ParamSlot slot, gg::core::ModSource source) {
            owner.dropSource(slot, source);
        };
        layers.setSelectedLayer(0);
        layers.onSelectLayer = [this](uint8_t layer) {
            owner.setLayer(layer);
            if (owner.onSelectLayer)
            {
                owner.onSelectLayer(layer);
            }
        };
        layers.setSourceDropHandler([this](gg::core::ParamSlot slot, gg::core::ModSource source) {
            owner.dropSource(slot, source);
        });
    }

    ModView &owner;
    const EditorContext &context;
    uint8_t layer{0};
    LayerStrip layers;
    SourceStrip sources;
    EditView edit;
    RouteList list;
};

ModView::ModView(const EditorContext &context) : impl_(std::make_unique<Impl>(*this, context))
{
    setComponentID("view.mod");
}

ModView::~ModView() = default;

void ModView::setLayer(uint8_t layer)
{
    Impl &m = *impl_;
    m.layer = std::min<uint8_t>(layer, gg::core::kLayers - 1);
    m.layers.setSelectedLayer(m.layer);
    m.sources.setLayer(m.layer);
    m.edit.setLayer(m.layer);
}

uint8_t ModView::layer() const noexcept { return impl_->layer; }

DropResult dropSourceOn(juce::Component &target, const EditorContext &context,
                        gg::core::ParamSlot controlSlot, gg::core::ModSource source)
{
    if (!context.routes || !context.setRoutes)
    {
        return {false, 0, {}};
    }
    const auto [routeTarget, controlLayer] = routeTargetOf(controlSlot);
    const gg::core::ModRoutes before = context.routes();
    gg::core::ModRoutes after = before;
    DropResult result = addRouteForDrop(after, source, routeTarget, controlLayer,
                                        gg::core::paramMeta(controlSlot, context.mode));
    if (result.added)
    {
        context.setRoutes(after);
        if (context.undo != nullptr)
        {
            context.undo->pushRoutes(before, after, "route " + std::to_string(result.slot));
        }
    }
    else if (!result.notice.empty())
    {
        postNotice(target, context, result.notice);
    }
    return result;
}

void ModView::dropSource(gg::core::ParamSlot controlSlot, gg::core::ModSource source)
{
    Impl &m = *impl_;
    const DropResult result = dropSourceOn(*this, m.context, controlSlot, source);
    if (result.added)
    {
        m.list.refresh();
        m.edit.refresh();
    }
    m.list.select(result.slot);
}

void ModView::refresh()
{
    Impl &m = *impl_;
    m.layers.refresh();
    m.edit.refresh();
    m.list.refresh();
}

void ModView::resized()
{
    Impl &m = *impl_;
    auto area = getLocalBounds();
    m.list.setBounds(area.removeFromRight(kListW));
    m.layers.setBounds(area.removeFromTop(kStripH));
    area.removeFromTop(kMargin);
    m.sources.setBounds(area.removeFromTop(m.sources.heightFor(area.getWidth())));
    area.removeFromTop(kMargin);
    m.edit.setBounds(area);
}
} // namespace gg::ui
