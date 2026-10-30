// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "RouteList.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include <sst/basic-blocks/params/ParamMetadata.h>

#include "../Palette.h"
#include "gg/ui/EditLayout.h"
#include "gg/ui/LayerColours.h"
#include "gg/ui/RouteEdits.h"
#include "gg/ui/UndoStack.h"

namespace gg::ui
{
namespace
{
using gg::core::ModRoute;
using gg::core::ModRoutes;
using gg::core::ParamSlot;

constexpr int kRowH = 96, kLineH = 20, kPad = 4;
constexpr float kBoxFontSize = 12.0f; ///< the boxes' text: the 240 px list is narrow
constexpr int kArrowW = 16;           ///< a combo box's arrow zone
constexpr int kNoTarget = static_cast<int>(gg::core::kParamCount) + 1; ///< the "—" target's id

const juce::String kEmpty = juce::String::fromUTF8("\xe2\x80\x94"); // an em dash

/// A target's menu text: its name, less "A " for layer A's slot of a layer parameter.
juce::String targetName(ParamSlot slot, gg::core::Mode mode)
{
    std::string name = gg::core::paramMeta(slot, mode).name;
    if (slot == ParamSlot::LayerAOn)
    {
        return "Layer On";
    }
    if (gg::core::layerOf(slot) && name.starts_with("A "))
    {
        name.erase(0, 2);
    }
    return juce::String::fromUTF8(name.c_str());
}

/// A curve's name as the list shows it: the saved CamelCase one with spaces ("Unipolar Squared").
juce::String curveText(gg::core::ModCurve curve)
{
    const juce::String saved(gg::core::modCurveName(curve));
    juce::String text;
    for (int i = 0; i < saved.length(); ++i)
    {
        if (i > 0 && juce::CharacterFunctions::isUpperCase(saved[i]))
        {
            text << ' ';
        }
        text << saved[i];
    }
    return text;
}

/// The depth slider's range: the target's span either way, 1 for none.
double depthRange(ParamSlot target, gg::core::Mode mode)
{
    if (static_cast<std::size_t>(target) >= gg::core::kParamCount)
    {
        return 1.0;
    }
    const auto &meta = gg::core::paramMeta(target, mode);
    const double range = static_cast<double>(meta.maxVal) - static_cast<double>(meta.minVal);
    return range > 0.0 && std::isfinite(range) ? range : 1.0;
}

/// The list's boxes: a smaller font than JUCE's default and a narrower arrow, so that the 240 px
/// list's names fit whole.
struct BoxLookAndFeel final : juce::LookAndFeel_V4
{
    juce::Font getComboBoxFont(juce::ComboBox &) override
    {
        return juce::Font(juce::FontOptions(kBoxFontSize));
    }
    juce::Font getPopupMenuFont() override
    {
        return juce::Font(juce::FontOptions(kBoxFontSize + 1.0f));
    }

    void positionComboBoxText(juce::ComboBox &box, juce::Label &label) override
    {
        label.setBounds(1, 1, box.getWidth() - kArrowW - 1, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

    void drawComboBox(juce::Graphics &g, int width, int height, bool, int, int, int, int,
                      juce::ComboBox &box) override
    {
        const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width),
                                                   static_cast<float>(height));
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
        const auto arrow =
            juce::Rectangle<float>(static_cast<float>(width - kArrowW), 0.0f,
                                   static_cast<float>(kArrowW), static_cast<float>(height))
                .withSizeKeepingCentre(8.0f, 4.0f);
        juce::Path path;
        path.addTriangle(arrow.getX(), arrow.getY(), arrow.getRight(), arrow.getY(),
                         arrow.getCentreX(), arrow.getBottom());
        g.setColour(
            box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 1.0f : 0.3f));
        g.fillPath(path);
    }
};
} // namespace

struct RouteList::Impl
{
    struct Row
    {
        juce::Label number;
        juce::ComboBox source, sourceLayer, target, targetLayer, curve;
        juce::Slider depth{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
        juce::TextButton clear{"x"};
    };

    /// The rows' parent: it holds them and draws the selection.
    struct Content final : juce::Component
    {
        explicit Content(Impl &i) : impl(i) {}
        void paint(juce::Graphics &g) override
        {
            if (impl.selected >= 1)
            {
                g.setColour(paletteOf(*this).accent);
                g.drawRoundedRectangle(
                    juce::Rectangle<float>(
                        1.0f, static_cast<float>((impl.selected - 1) * kRowH) + 1.0f,
                        static_cast<float>(getWidth()) - 2.0f, static_cast<float>(kRowH) - 2.0f),
                    4.0f, 2.0f);
            }
        }
        Impl &impl;
    };

    Impl(RouteList &o, const EditorContext &c) : owner(o), context(c), content(*this)
    {
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        owner.addAndMakeVisible(viewport);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            rows[i] = std::make_unique<Row>();
            build(static_cast<uint8_t>(i + 1), *rows[i]);
        }
        content.setSize(240, static_cast<int>(rows.size()) * kRowH);
        applyColours();
    }

    void build(uint8_t slot, Row &row)
    {
        for (juce::ComboBox *box :
             {&row.source, &row.sourceLayer, &row.target, &row.targetLayer, &row.curve})
        {
            box->setLookAndFeel(&boxLook);
        }
        const juce::String id = "route." + juce::String(static_cast<int>(slot)) + ".";
        row.number.setText(juce::String(static_cast<int>(slot)), juce::dontSendNotification);
        row.number.setFont(juce::FontOptions(11.0f));
        row.number.setBorderSize(juce::BorderSize<int>(0, 0, 0, 2)); // "13" in 18 px
        row.number.setJustificationType(juce::Justification::centredLeft);
        row.number.setInterceptsMouseClicks(false, false);

        // Source: "—" (None) then the 24, by ModSource index + 1.
        row.source.setComponentID(id + "source");
        for (int s = 0; s < static_cast<int>(gg::core::ModSource::Count); ++s)
        {
            row.source.addItem(
                s == 0 ? kEmpty
                       : juce::String(gg::core::modSourceName(static_cast<gg::core::ModSource>(s))),
                s + 1);
        }
        row.sourceLayer.setComponentID(id + "sourceLayer");
        row.sourceLayer.addItem("same", 1);
        row.targetLayer.setComponentID(id + "targetLayer");
        for (int l = 0; l < gg::core::kLayers; ++l)
        {
            row.sourceLayer.addItem(juce::String::charToString(kLayerLetters[l]), l + 2);
            row.targetLayer.addItem(juce::String::charToString(kLayerLetters[l]), l + 1);
        }
        row.targetLayer.addItem("all", gg::core::kLayers + 1);

        // Target: "—", then every valid target the product shows, by slot index + 1.
        row.target.setComponentID(id + "target");
        row.target.addItem(kEmpty, kNoTarget);
        for (std::size_t t = 0; t < gg::core::kParamCount; ++t)
        {
            const auto target = static_cast<ParamSlot>(t);
            if (gg::core::isValidTarget(target) && !hiddenIn(target, context.mode))
            {
                row.target.addItem(targetName(target, context.mode), static_cast<int>(t) + 1);
            }
        }

        row.curve.setComponentID(id + "curve");
        for (int c = 0; c < static_cast<int>(gg::core::ModCurve::Count); ++c)
        {
            row.curve.addItem(curveText(static_cast<gg::core::ModCurve>(c)), c + 1);
        }

        row.depth.setComponentID(id + "depth");
        row.depth.setRange(-1.0, 1.0, 0.0);
        row.depth.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, kLineH);
        row.depth.setNumDecimalPlacesToDisplay(3);

        row.clear.setComponentID(id + "clear");
        row.clear.setTooltip("Clear this route");

        row.source.onChange = [this, slot, &row] {
            const auto source = static_cast<gg::core::ModSource>(row.source.getSelectedId() - 1);
            edit(slot, [slot, source](ModRoutes &r) {
                if (source == gg::core::ModSource::None)
                {
                    clearSlot(r, slot); // as the clear button does
                }
                else
                {
                    r[slot - 1u].source = source;
                }
            });
        };
        row.sourceLayer.onChange = [this, slot, &row] {
            const int sel = row.sourceLayer.getSelectedId();
            edit(slot, [slot, sel](ModRoutes &r) {
                r[slot - 1u].sourceLayer =
                    sel <= 1 ? gg::core::kSameLayer : static_cast<uint8_t>(sel - 2);
            });
        };
        row.target.onChange = [this, slot, &row] {
            const int sel = row.target.getSelectedId();
            edit(slot, [slot, sel](ModRoutes &r) {
                r[slot - 1u].target =
                    sel == kNoTarget ? ParamSlot::Count : static_cast<ParamSlot>(sel - 1);
            });
        };
        row.targetLayer.onChange = [this, slot, &row] {
            const int sel = row.targetLayer.getSelectedId();
            edit(slot, [slot, sel](ModRoutes &r) {
                r[slot - 1u].targetLayer =
                    sel > gg::core::kLayers ? gg::core::kAllLayers : static_cast<uint8_t>(sel - 1);
            });
        };
        row.curve.onChange = [this, slot, &row] {
            const auto curve = static_cast<gg::core::ModCurve>(row.curve.getSelectedId() - 1);
            edit(slot, [slot, curve](ModRoutes &r) { r[slot - 1u].curve = curve; });
        };
        row.clear.onClick = [this, slot] {
            edit(slot, [slot](ModRoutes &r) { clearSlot(r, slot); });
        };
        // A depth drag is one undo entry: the routes at its start, and as it ends.
        row.depth.onDragStart = [this, slot] {
            depthSlot = slot;
            depthBefore = routes();
            depthAfter = depthBefore;
        };
        row.depth.onValueChange = [this, slot, &row] {
            const float depth = static_cast<float>(row.depth.getValue());
            if (depthSlot == slot)
            {
                depthAfter = routes();
                setDepth(depthAfter, slot, depth);
                if (context.setRoutes)
                {
                    context.setRoutes(depthAfter);
                }
            }
            else
            {
                edit(slot, [slot, depth](ModRoutes &r) { setDepth(r, slot, depth); });
            }
        };
        row.depth.onDragEnd = [this, slot] {
            if (depthSlot == slot && context.undo != nullptr)
            {
                context.undo->pushRoutes(depthBefore, depthAfter, "route " + std::to_string(slot));
            }
            depthSlot = 0;
        };

        for (juce::Component *c : std::initializer_list<juce::Component *>{
                 &row.number, &row.source, &row.sourceLayer, &row.target, &row.targetLayer,
                 &row.curve, &row.depth, &row.clear})
        {
            content.addAndMakeVisible(*c);
        }
    }

    ~Impl()
    {
        for (auto &row : rows)
        {
            for (juce::ComboBox *box :
                 {&row->source, &row->sourceLayer, &row->target, &row->targetLayer, &row->curve})
            {
                box->setLookAndFeel(nullptr);
            }
            row->depth.setLookAndFeel(nullptr);
            row->clear.setLookAndFeel(nullptr);
        }
    }

    /// The boxes, the depth sliders and the clear buttons take the theme's colors (the editor's
    /// palette, found above the list), so a light theme has no dark box in it.
    void applyColours()
    {
        const Palette &pal = paletteOf(owner);
        const juce::Colour box = pal.header, text = pal.text;
        for (auto &row : rows)
        {
            for (juce::ComboBox *combo :
                 {&row->source, &row->sourceLayer, &row->target, &row->targetLayer, &row->curve})
            {
                combo->setColour(juce::ComboBox::backgroundColourId, box);
                combo->setColour(juce::ComboBox::textColourId, text);
                combo->setColour(juce::ComboBox::outlineColourId, pal.outline);
                combo->setColour(juce::ComboBox::arrowColourId, text);
                combo->setColour(juce::ComboBox::focusedOutlineColourId, pal.accent);
                combo->setColour(juce::PopupMenu::backgroundColourId, box);
                combo->setColour(juce::PopupMenu::textColourId, text);
                combo->setColour(juce::PopupMenu::highlightedBackgroundColourId,
                                 pal.accent.withAlpha(0.35f));
                combo->setColour(juce::PopupMenu::highlightedTextColourId, text);
                combo->repaint();
            }
            row->number.setColour(juce::Label::textColourId, pal.dimText);
            row->depth.setColour(juce::Slider::textBoxBackgroundColourId, box);
            row->depth.setColour(juce::Slider::textBoxTextColourId, text);
            row->depth.setColour(juce::Slider::textBoxOutlineColourId, pal.outline);
            row->depth.setColour(juce::Slider::textBoxHighlightColourId,
                                 pal.accent.withAlpha(0.35f));
            row->depth.setColour(juce::Slider::backgroundColourId, pal.outline);
            row->depth.setColour(juce::Slider::trackColourId, pal.accent);
            row->depth.setColour(juce::Slider::thumbColourId, pal.accent);
            row->clear.setColour(juce::TextButton::buttonColourId, box);
            row->clear.setColour(juce::TextButton::textColourOffId, text);
            row->clear.setColour(juce::TextButton::textColourOnId, text);
        }
    }

    ModRoutes routes() const { return context.routes ? context.routes() : ModRoutes{}; }

    /// Changes the matrix by `change`: one setRoutes, one undo entry.
    void edit(uint8_t slot, const std::function<void(ModRoutes &)> &change)
    {
        if (!context.routes || !context.setRoutes || refreshing)
        {
            return;
        }
        const ModRoutes before = context.routes();
        ModRoutes after = before;
        change(after);
        if (after == before)
        {
            return;
        }
        context.setRoutes(after);
        if (context.undo != nullptr)
        {
            context.undo->pushRoutes(before, after, "route " + std::to_string(slot));
        }
    }

    void refresh()
    {
        const ModRoutes now = routes();
        if (shown && *shown == now)
        {
            return;
        }
        // A route that appeared since the last look (a drop, a redo, the host's patch) comes
        // into view: the newest one, outlined.
        uint8_t added = 0;
        if (shown)
        {
            for (std::size_t i = 0; i < now.size(); ++i)
            {
                if (now[i].source != gg::core::ModSource::None &&
                    (*shown)[i].source == gg::core::ModSource::None)
                {
                    added = static_cast<uint8_t>(i + 1);
                }
            }
        }
        shown = now;
        refreshing = true;
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            Row &row = *rows[i];
            const ModRoute &r = now[i];
            const bool active = r.source != gg::core::ModSource::None;
            row.source.setSelectedId(static_cast<int>(r.source) + 1, juce::dontSendNotification);
            row.sourceLayer.setSelectedId(r.sourceLayer < gg::core::kLayers ? r.sourceLayer + 2 : 1,
                                          juce::dontSendNotification);
            row.target.setSelectedId(static_cast<std::size_t>(r.target) < gg::core::kParamCount
                                         ? static_cast<int>(r.target) + 1
                                         : kNoTarget,
                                     juce::dontSendNotification);
            row.targetLayer.setSelectedId(r.targetLayer < gg::core::kLayers ? r.targetLayer + 1
                                                                            : gg::core::kLayers + 1,
                                          juce::dontSendNotification);
            row.curve.setSelectedId(static_cast<int>(r.curve) + 1, juce::dontSendNotification);
            if (depthSlot != i + 1) // the slider being dragged shows itself
            {
                const double range = depthRange(r.target, context.mode);
                row.depth.setRange(-range, range, 0.0);
                row.depth.setValue(static_cast<double>(r.depth), juce::dontSendNotification);
            }
            row.sourceLayer.setEnabled(active && gg::core::isPerLayerSource(r.source));
            row.targetLayer.setEnabled(active && gg::core::isLayerTarget(r.target));
            row.curve.setEnabled(active);
            row.depth.setEnabled(active);
            row.clear.setEnabled(active);
        }
        refreshing = false;
        if (added != 0)
        {
            owner.select(added);
        }
    }

    void layoutRows(int width)
    {
        content.setSize(width, static_cast<int>(rows.size()) * kRowH);
        const int inner = width - 2 * kPad;
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            Row &row = *rows[i];
            auto area = juce::Rectangle<int>(kPad, static_cast<int>(i) * kRowH + kPad, inner,
                                             kRowH - 2 * kPad);
            // Four lines: the source and its layer; the target and its layer; the curve; the
            // depth (its slider, its value).
            auto line1 = area.removeFromTop(kLineH);
            row.number.setBounds(line1.removeFromLeft(18));
            row.clear.setBounds(line1.removeFromRight(24));
            row.sourceLayer.setBounds(line1.removeFromRight(56).reduced(1, 0));
            row.source.setBounds(line1.reduced(1, 0));
            area.removeFromTop(2);
            auto line2 = area.removeFromTop(kLineH);
            line2.removeFromLeft(18);
            row.targetLayer.setBounds(line2.removeFromRight(56).reduced(1, 0));
            row.target.setBounds(line2.reduced(1, 0));
            area.removeFromTop(2);
            auto line3 = area.removeFromTop(kLineH);
            line3.removeFromLeft(18);
            row.curve.setBounds(line3.reduced(1, 0));
            area.removeFromTop(2);
            row.depth.setBounds(area.removeFromTop(kLineH));
        }
    }

    RouteList &owner;
    const EditorContext &context;
    BoxLookAndFeel boxLook;
    juce::Viewport viewport;
    Content content;
    std::array<std::unique_ptr<Row>, gg::core::kModSlots> rows;
    std::optional<ModRoutes> shown; ///< the routes the rows show
    bool refreshing{false};         ///< refresh() sets the controls: their callbacks edit nothing
    uint8_t selected{0};
    uint8_t depthSlot{0}; ///< the slot whose depth slider is being dragged
    ModRoutes depthBefore{}, depthAfter{};
};

RouteList::RouteList(const EditorContext &context) : impl_(std::make_unique<Impl>(*this, context))
{
    setComponentID("routes");
    impl_->refresh();
}

RouteList::~RouteList() = default;

void RouteList::refresh() { impl_->refresh(); }

void RouteList::parentHierarchyChanged() { impl_->applyColours(); }

void RouteList::lookAndFeelChanged() { impl_->applyColours(); }

void RouteList::select(uint8_t slot)
{
    Impl &m = *impl_;
    m.selected = slot <= gg::core::kModSlots ? slot : 0;
    m.content.repaint();
    if (m.selected >= 1)
    {
        const int y = (m.selected - 1) * kRowH;
        const int top = m.viewport.getViewPositionY();
        if (y < top || y + kRowH > top + m.viewport.getViewHeight())
        {
            m.viewport.setViewPosition(0, y);
        }
    }
}

uint8_t RouteList::selected() const noexcept { return impl_->selected; }

void RouteList::resized()
{
    impl_->viewport.setBounds(getLocalBounds());
    impl_->layoutRows(std::max(1, impl_->viewport.getMaximumVisibleWidth()));
}
} // namespace gg::ui
