// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "ParamPanel.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include <sst/basic-blocks/params/ParamMetadata.h>

#include "../Palette.h"
#include "gg/ui/RouteEdits.h"
#include "gg/ui/Rings.h"
#include "gg/ui/UndoStack.h"

namespace gg::ui
{
namespace
{
using gg::core::LayerParam;
using gg::core::ParamSlot;
using Kind = ParamControl::Kind;

// The cells (spec §6.5): a knob with its label, its spread beside it smaller, a menu or toggle
// in a row of its own, a step's slider.
constexpr int kKnobW = 72, kKnobH = 92;
constexpr int kSpreadW = 56, kSpreadH = 80, kSpreadY = 10;
constexpr int kMenuW = 88, kCaptionH = 16, kMenuH = 28;
constexpr int kStepW = 20, kStepH = kKnobH;
// NamedPanel's content area is its bounds less 2 + 2 on each side and its 20 px header.
constexpr int kPadW = 2 * (2 + 2), kPadH = 2 * (2 + 2) + 20;

bool isStep(ParamSlot slot)
{
    const auto ref = gg::core::layerOf(slot);
    return ref && ref->param >= LayerParam::Step1 && ref->param <= LayerParam::Step16;
}

/// One cell: a control, its spread beside it, or a menu's caption above it.
class Cell final : public juce::Component
{
  public:
    Cell(const EditorContext &context, ParamSlot slot, Kind kind, std::optional<ParamSlot> spread)
        : base_(std::make_unique<ParamControl>(context, slot, kind))
    {
        addAndMakeVisible(*base_);
        if (spread)
        {
            spread_ = std::make_unique<ParamControl>(context, *spread, Kind::Knob);
            addAndMakeVisible(*spread_);
        }
        if (kind == Kind::Menu)
        {
            caption_ = base_->getTitle();
        }
        const bool knobLike = kind == Kind::Knob;
        const int w = spread_                 ? kKnobW + kSpreadW
                      : kind == Kind::VSlider ? kStepW
                      : knobLike              ? kKnobW
                                              : kMenuW;
        const int h = kind == Kind::Menu      ? kCaptionH + kMenuH
                      : kind == Kind::Toggle  ? kMenuH
                      : kind == Kind::VSlider ? kStepH
                                              : kKnobH;
        natural_ = {w, h};
        setSize(w, h);
    }

    /// The size the cell wants, whatever a layout has left it at.
    juce::Point<int> natural() const { return natural_; }

    ParamControl &base() { return *base_; }
    ParamControl *spread() { return spread_.get(); }

    void resized() override
    {
        if (spread_)
        {
            base_->setBounds(0, 0, kKnobW, kKnobH);
            spread_->setBounds(kKnobW, kSpreadY, kSpreadW, kSpreadH);
        }
        else if (caption_.isNotEmpty())
        {
            base_->setBounds(getLocalBounds().withTrimmedTop(kCaptionH));
        }
        else
        {
            base_->setBounds(getLocalBounds());
        }
    }

    void paint(juce::Graphics &g) override
    {
        if (caption_.isNotEmpty())
        {
            g.setColour(paletteOf(*this).dimText);
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(caption_, getLocalBounds().removeFromTop(kCaptionH),
                       juce::Justification::centredLeft);
        }
    }

  private:
    std::unique_ptr<ParamControl> base_, spread_;
    juce::String caption_;
    juce::Point<int> natural_;
};
} // namespace

struct ParamPanel::Impl
{
    /// A cell's control, as refresh() sees it.
    struct Entry
    {
        ParamControl *control{nullptr};
        ParamControl *spread{nullptr};
        bool step{false};
        uint8_t layer{0};
        std::optional<std::pair<float, float>> band;
    };

    Impl(ParamPanel &p, const EditorContext &c, Panel pn, uint8_t l)
        : owner(p), context(c), panel(pn), layer(l)
    {
    }

    void build()
    {
        endRingDrag(); // the controls the drag came from go below
        entries.clear();
        cells.clear();
        std::vector<ParamSlot> consumed;
        const std::vector<ParamSlot> slots = slotsIn(panel, layer);
        for (const ParamSlot slot : slots)
        {
            if (hiddenIn(slot, context.mode) ||
                std::find(consumed.begin(), consumed.end(), slot) != consumed.end())
            {
                continue;
            }
            std::optional<ParamSlot> spread;
            if (const auto placed = placementOf(slot);
                placed && placed->pairedSpread != ParamSlot::Count &&
                std::find(slots.begin(), slots.end(), placed->pairedSpread) != slots.end())
            {
                spread = placed->pairedSpread;
                consumed.push_back(*spread);
            }
            const Kind kind = isStep(slot)
                                  ? Kind::VSlider
                                  : ParamControl::kindFor(gg::core::paramMeta(slot, context.mode));
            auto cell = std::make_unique<Cell>(context, slot, kind, spread);
            Entry entry;
            entry.control = &cell->base();
            entry.spread = cell->spread();
            entry.step = kind == Kind::VSlider;
            entry.layer = routeTargetOf(slot).second;
            for (ParamControl *c : {entry.control, entry.spread})
            {
                if (c == nullptr)
                {
                    continue;
                }
                c->setModMode(modMode);
                c->setSourceDropEnabled(modMode);
                c->onRingDrag = [this, controlSlot = c->slot()](uint8_t routeSlot, float depth01) {
                    dragRing(controlSlot, routeSlot, depth01);
                };
                c->onRingRelease = [this] { endRingDrag(); };
                c->onSourceDrop = [this, controlSlot = c->slot()](gg::core::ModSource source) {
                    if (owner.onSourceDrop)
                    {
                        owner.onSourceDrop(controlSlot, source);
                    }
                };
            }
            entries.push_back(entry);
            owner.addAndMakeVisible(*cell);
            cells.push_back(std::move(cell));
        }
        owner.setVisible(!cells.empty());
        refresh();
    }

    /// A ring dragged to a depth: the route's, in the parameter's own units.
    void dragRing(ParamSlot controlSlot, uint8_t routeSlot, float depth01)
    {
        if (!context.routes || !context.setRoutes || routeSlot < 1 ||
            routeSlot > gg::core::kModSlots)
        {
            return;
        }
        const auto &meta = gg::core::paramMeta(controlSlot, context.mode);
        gg::core::ModRoutes routes = context.routes();
        if (!ringBefore)
        {
            ringBefore = routes;
            ringSlot = routeSlot;
        }
        setDepth(routes, routeSlot, depth01 * (meta.maxVal - meta.minVal));
        context.setRoutes(routes);
        ringAfter = routes;
    }

    /// The drag is over: its edits are one undo entry.
    void endRingDrag()
    {
        if (ringBefore && context.undo != nullptr)
        {
            context.undo->pushRoutes(*ringBefore, ringAfter, "route " + std::to_string(ringSlot));
        }
        ringBefore.reset();
    }

    /// The spread pair's band on the base knob, in 0..1 of the base's range (spec §6.5): Size ±
    /// its octaves, Pitch ± its semitones, Position ± Spray on a Sample layer and Position to
    /// Position + Spray on a Live one (Position and Spray are both 0..1 values, taken as they
    /// are, as the lane's rectangle does).
    std::optional<std::pair<float, float>> bandOf(const Entry &e) const
    {
        const auto ref = gg::core::layerOf(e.control->slot());
        const float spread = e.spread->shownValue();
        if (!ref || !(spread > 0.0f) || !std::isfinite(spread))
        {
            return std::nullopt;
        }
        const float value = e.control->shownValue();
        const auto &meta = gg::core::paramMeta(e.control->slot(), context.mode);
        float low = value - spread, high = value + spread;
        if (ref->param == LayerParam::Position)
        {
            const float live =
                context.baseValue
                    ? context.baseValue(gg::core::layerSlot(LayerParam::Source, e.layer))
                    : 0.0f;
            if (std::lround(live) == 1)
            {
                low = value;
            }
        }
        const float range = meta.maxVal - meta.minVal;
        if (!(range > 0.0f))
        {
            return std::nullopt;
        }
        const auto to01 = [&](float v) {
            return std::clamp((v - meta.minVal) / range, 0.0f, 1.0f);
        };
        return std::pair{to01(low), to01(high)};
    }

    void refresh()
    {
        const gg::core::ModRoutes routes =
            context.routes ? context.routes() : gg::core::ModRoutes{};
        const Palette &palette = paletteOf(owner);
        for (Entry &e : entries)
        {
            e.control->refresh();
            if (e.spread != nullptr)
            {
                e.spread->refresh();
                const auto band = bandOf(e);
                if (band != e.band)
                {
                    e.band = band;
                    e.control->setSpreadBand(band);
                }
            }
            for (ParamControl *c : {e.control, e.spread})
            {
                if (c != nullptr && !e.step) // the Step sliders are no targets
                {
                    refreshRings(*c, routes, palette);
                }
            }
        }
    }

    void refreshRings(ParamControl &c, const gg::core::ModRoutes &routes, const Palette &palette)
    {
        const auto [target, ringLayer] = routeTargetOf(c.slot());
        const auto &meta = gg::core::paramMeta(c.slot(), context.mode);
        std::vector<ParamControl::Ring> rings;
        for (const RingSpec &r : ringsFor(routes, target, ringLayer, meta.minVal, meta.maxVal))
        {
            rings.push_back({r.slot, r.depth01, r.bipolar, palette.sourceFamilies[r.family]});
        }
        if (!sameRings(rings, c.rings()))
        {
            c.setRings(std::move(rings));
        }
    }

    static bool sameRings(const std::vector<ParamControl::Ring> &a,
                          const std::vector<ParamControl::Ring> &b)
    {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                          [](const ParamControl::Ring &x, const ParamControl::Ring &y) {
                              return x.slot == y.slot && x.depth01 == y.depth01 &&
                                     x.bipolar == y.bipolar && x.colour == y.colour;
                          });
    }

    /// Wraps the cells in a `contentWidth` wide area at `origin`, moving them there when `place`;
    /// returns the extent they take. Measuring (place false) leaves the components alone.
    juce::Point<int> layoutCells(int contentWidth, juce::Point<int> origin, bool place)
    {
        juce::FlexBox box;
        box.flexDirection = juce::FlexBox::Direction::row;
        box.flexWrap = juce::FlexBox::Wrap::wrap;
        box.justifyContent = juce::FlexBox::JustifyContent::flexStart;
        box.alignItems = juce::FlexBox::AlignItems::flexStart;
        box.alignContent = juce::FlexBox::AlignContent::flexStart;
        for (const auto &cell : cells)
        {
            box.items.add(juce::FlexItem(static_cast<float>(cell->natural().x),
                                         static_cast<float>(cell->natural().y))
                              .withFlex(0.0f, 0.0f));
        }
        box.performLayout(juce::Rectangle<int>(origin.x, origin.y, contentWidth, 100000));
        int right = 0, bottom = 0;
        for (int i = 0; i < box.items.size(); ++i)
        {
            const auto bounds = box.items[i].currentBounds.toNearestInt();
            if (place)
            {
                cells[static_cast<std::size_t>(i)]->setBounds(bounds);
            }
            right = std::max(right, bounds.getRight() - origin.x);
            bottom = std::max(bottom, bounds.getBottom() - origin.y);
        }
        return {right, bottom};
    }

    ParamPanel &owner;
    const EditorContext &context;
    const Panel panel;
    uint8_t layer;
    bool modMode{false};
    std::vector<std::unique_ptr<Cell>> cells;
    std::vector<Entry> entries;
    std::optional<gg::core::ModRoutes> ringBefore; ///< the routes as a ring drag found them
    gg::core::ModRoutes ringAfter{};
    uint8_t ringSlot{0};
};

ParamPanel::ParamPanel(const EditorContext &context, Panel panel, uint8_t layer)
    : sst::jucegui::components::NamedPanel(panelName(panel)),
      impl_(std::make_unique<Impl>(*this, context, panel, layer))
{
    impl_->build();
}

ParamPanel::~ParamPanel() = default;

Panel ParamPanel::panel() const noexcept { return impl_->panel; }

void ParamPanel::setLayer(uint8_t layer)
{
    // The cells (and their controls, which end a gesture they hold) go before the new ones.
    impl_->layer = layer;
    impl_->build();
    resized();
}

void ParamPanel::refresh() { impl_->refresh(); }

void ParamPanel::setModMode(bool on)
{
    impl_->modMode = on;
    for (const auto &e : impl_->entries)
    {
        for (ParamControl *c : {e.control, e.spread})
        {
            if (c != nullptr)
            {
                c->setModMode(on);
                c->setSourceDropEnabled(on);
            }
        }
    }
}

std::vector<ParamControl *> ParamPanel::controls()
{
    std::vector<ParamControl *> out;
    for (const auto &e : impl_->entries)
    {
        out.push_back(e.control);
        if (e.spread != nullptr)
        {
            out.push_back(e.spread);
        }
    }
    return out;
}

juce::Point<int> ParamPanel::preferredSize(int maxWidth)
{
    if (impl_->cells.empty())
    {
        return {0, 0};
    }
    int widest = 0;
    for (const auto &cell : impl_->cells)
    {
        widest = std::max(widest, cell->natural().x);
    }
    const auto extent = impl_->layoutCells(std::max(widest, maxWidth - kPadW), {0, 0}, false);
    return {extent.x + kPadW, extent.y + kPadH};
}

void ParamPanel::resized()
{
    sst::jucegui::components::NamedPanel::resized();
    const auto area = getContentArea();
    impl_->layoutCells(area.getWidth(), area.getPosition(), true);
}
} // namespace gg::ui
