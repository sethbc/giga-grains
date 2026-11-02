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
#include "gg/core/ParamMap.h"
#include "gg/ui/LayerColours.h"
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

/// The label a control carries in its panel: the parameter's name less the panel's own title
/// ("Rate" for "LFO 1 Rate" in LFO 1's panel, "Amount" for "Reverb Amount" in Reverb's), which
/// the panel's header already says. A name that is only the title stays whole.
juce::String shortLabel(const ParamControl &control, const char *panelTitle)
{
    juce::String name = control.shownLabel();
    // A per-layer global ("Level B", "Layer B On") sits in its own layer's panel: the panel is
    // the layer, so the letter goes, as it does from a layer parameter's name.
    if (const auto layer = gg::core::perLayerGlobalLayer(control.slot()))
    {
        const juce::String letter = juce::String::charToString(kLayerLetters[*layer]);
        name = name.replace(" " + letter + " ", " ");
        if (name.endsWith(" " + letter))
        {
            name = name.dropLastCharacters(2);
        }
    }
    const juce::String prefix = juce::String(panelTitle) + " ";
    return name.startsWith(prefix) && name.length() > prefix.length()
               ? name.substring(prefix.length())
               : name;
}

bool isStep(ParamSlot slot)
{
    const auto ref = gg::core::layerOf(slot);
    return ref && ref->param >= LayerParam::Step1 && ref->param <= LayerParam::Step16;
}

/// One cell: a control, its spread beside it, or a menu's caption above it. `lineStart`: the
/// cell starts a new line in its panel (Placement::breakBefore).
class Cell final : public juce::Component
{
  public:
    Cell(const EditorContext &context, ParamSlot slot, Kind kind, std::optional<ParamSlot> spread,
         const char *panelTitle, bool lineStart)
        : base_(std::make_unique<ParamControl>(context, slot, kind)), lineStart_(lineStart)
    {
        addAndMakeVisible(*base_);
        if (spread)
        {
            spread_ = std::make_unique<ParamControl>(context, *spread, Kind::Knob);
            addAndMakeVisible(*spread_);
        }
        for (ParamControl *control : {base_.get(), spread_.get()})
        {
            if (control != nullptr && control->getComponentID().isNotEmpty() &&
                kind != Kind::VSlider)
            {
                control->setShownLabel(shortLabel(*control, panelTitle));
            }
        }
        if (kind == Kind::Menu)
        {
            // As a knob's label: the panel's title the header already says is left off.
            caption_ = shortLabel(*base_, panelTitle);
            setName(caption_); // the tests' handle on what is drawn
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
    bool lineStart() const { return lineStart_; }

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
    const bool lineStart_;
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

    ~Impl() { endRingDrag(); }

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
            const auto placed = placementOf(slot);
            if (placed && placed->pairedSpread != ParamSlot::Count &&
                std::find(slots.begin(), slots.end(), placed->pairedSpread) != slots.end())
            {
                spread = placed->pairedSpread;
                consumed.push_back(*spread);
            }
            const Kind kind = isStep(slot)
                                  ? Kind::VSlider
                                  : ParamControl::kindFor(gg::core::paramMeta(slot, context.mode));
            auto cell = std::make_unique<Cell>(context, slot, kind, spread, panelName(panel),
                                               placed && placed->breakBefore);
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
                if (const auto ref = gg::core::layerOf(c->slot());
                    ref && ref->param == LayerParam::RingLength)
                {
                    // SP-M9: a Ring Length change is a ring resize, so it is sent once, on release.
                    c->setCommitOnRelease(true);
                    c->setTooltip("Sets on release: the ring resizes once, when you let go");
                }
                else if (c->slot() == ParamSlot::ReverbSize)
                {
                    // D10 (row 217): a Size move clicks, so the room changes once, on release.
                    c->setCommitOnRelease(true);
                    c->setTooltip("Sets on release: the room's size changes in one step");
                }
                c->onRingDrag = [this, controlSlot = c->slot()](uint8_t routeSlot, float depth01) {
                    dragRing(controlSlot, routeSlot, depth01);
                };
                c->onRingRelease = [this] { endRingDrag(); };
                c->onRingClick = [this](uint8_t routeSlot) {
                    if (owner.onRingClick)
                    {
                        owner.onRingClick(routeSlot);
                    }
                };
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
        refresh(context.routes ? context.routes() : gg::core::ModRoutes{});
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
            if (context.undo != nullptr)
            {
                context.undo->beginGroup(); // undo waits for the drag (UndoStack::busy)
            }
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
            context.undo->endGroup();
        }
        ringBefore.reset();
    }

    /// The spread pair's band on the base knob, in 0..1 of the base's range (spec §6.5): Size ±
    /// its octaves, Pitch ± its semitones, and Position ± Spray on a layer built as Sample, but
    /// Position to Position + Spray on one whose grains spray one way, behind a ring's head: built
    /// as Live or Spectral (gg::core::builtKind of the Source's base value, so Resample reads as
    /// Live and the unbuilt Oscillator as the product's default kind; the scheduler sprays
    /// Spectral one-sided, as Live). Position and Spray are both 0..1 values, taken as they are,
    /// as the lane's rectangle does.
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
            const ParamSlot source = gg::core::layerSlot(LayerParam::Source, e.layer);
            gg::core::ParamValues values{};
            values[static_cast<std::size_t>(source)] =
                context.baseValue ? context.baseValue(source)
                                  : gg::core::paramMeta(source, context.mode).defaultVal;
            const gg::SourceMode built =
                gg::core::builtKind(gg::core::sourceKindOf(values, e.layer), context.mode);
            if (built != gg::SourceMode::Sample)
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

    void refresh(const gg::core::ModRoutes &routes)
    {
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

    /// Lays the cells out in a `contentWidth` wide area at `origin`, moving them there when
    /// `place`; returns the extent they take. The cells fall into lines, a new one at each cell
    /// that starts one (Cell::lineStart), stacked top to bottom; each line wraps in the width.
    /// Measuring (place false) leaves the components alone.
    juce::Point<int> layoutCells(int contentWidth, juce::Point<int> origin, bool place)
    {
        int right = 0, bottom = 0;
        std::size_t first = 0;
        while (first < cells.size())
        {
            std::size_t end = first + 1;
            while (end < cells.size() && !cells[end]->lineStart())
            {
                ++end;
            }
            const int top = origin.y + bottom;
            juce::FlexBox box;
            box.flexDirection = juce::FlexBox::Direction::row;
            box.flexWrap = juce::FlexBox::Wrap::wrap;
            box.justifyContent = juce::FlexBox::JustifyContent::flexStart;
            box.alignItems = juce::FlexBox::AlignItems::flexStart;
            box.alignContent = juce::FlexBox::AlignContent::flexStart;
            for (std::size_t i = first; i < end; ++i)
            {
                box.items.add(juce::FlexItem(static_cast<float>(cells[i]->natural().x),
                                             static_cast<float>(cells[i]->natural().y))
                                  .withFlex(0.0f, 0.0f));
            }
            box.performLayout(juce::Rectangle<int>(origin.x, top, contentWidth, 100000));
            for (int i = 0; i < box.items.size(); ++i)
            {
                const auto bounds = box.items[i].currentBounds.toNearestInt();
                if (place)
                {
                    cells[first + static_cast<std::size_t>(i)]->setBounds(bounds);
                }
                right = std::max(right, bounds.getRight() - origin.x);
                bottom = std::max(bottom, bounds.getBottom() - origin.y);
            }
            first = end;
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

void ParamPanel::refresh(const gg::core::ModRoutes &routes) { impl_->refresh(routes); }

void ParamPanel::setModMode(bool on)
{
    impl_->endRingDrag(); // a drag under way ends with the mode
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
