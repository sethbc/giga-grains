// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "LayerStrip.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

#include <sst/jucegui/components/TextPushButton.h>

#include "Palette.h"
#include "ParamControl.h"
#include "gg/core/ParamIds.h"
#include "gg/ui/LayerColours.h"

namespace gg::ui
{
namespace
{
using gg::core::LayerParam;
using gg::core::ParamSlot;
namespace jcmp = sst::jucegui::components;

constexpr int kRowHeight = 22; // the letter, the toggles, Source and Level

/// The layer's letter in its colour: a filled button, the letter dark on it.
struct LetterButton final : jcmp::TextPushButton
{
    LetterButton(uint8_t layer) : layer_(layer) { setLabel(std::string(1, kLayerLetters[layer])); }

    void paint(juce::Graphics &g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(paletteOf(*this).layers[layer_]);
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.setFont(juce::FontOptions(area.getHeight() * 0.7f, juce::Font::bold));
        g.drawText(getLabel(), getLocalBounds(), juce::Justification::centred);
    }

    uint8_t layer_;
};

ParamControl::Kind kindOf(const EditorContext &context, ParamSlot slot)
{
    return ParamControl::kindFor(gg::core::paramMeta(slot, context.mode));
}
} // namespace

struct LayerStrip::Impl
{
    struct Cell
    {
        std::unique_ptr<LetterButton> letter;
        std::unique_ptr<ParamControl> on, level, source, freeze;
    };

    Layout layout;
    std::array<Cell, gg::core::kLayers> cells;
    std::optional<std::array<juce::Rectangle<int>, 4>> lanes;
    uint8_t selected{0};

    /// Layer `layer`'s cell, in the strip's coordinates.
    juce::Rectangle<int> cellBounds(const juce::Rectangle<int> &local, std::size_t layer) const
    {
        if (layout == Layout::Column && lanes)
        {
            return (*lanes)[layer];
        }
        const int n = static_cast<int>(gg::core::kLayers);
        const int i = static_cast<int>(layer);
        if (layout == Layout::Column)
        {
            return {local.getX(), local.getY() + local.getHeight() * i / n, local.getWidth(),
                    local.getHeight() * (i + 1) / n - local.getHeight() * i / n};
        }
        return {local.getX() + local.getWidth() * i / n, local.getY(),
                local.getWidth() * (i + 1) / n - local.getWidth() * i / n, local.getHeight()};
    }
};

LayerStrip::LayerStrip(const EditorContext &context, Layout layout)
    : impl_(std::make_unique<Impl>())
{
    setComponentID("strip");
    setTitle("Layers");
    impl_->layout = layout;
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        Impl::Cell &cell = impl_->cells[layer];
        cell.letter = std::make_unique<LetterButton>(layer);
        cell.letter->setComponentID("strip." + juce::String(static_cast<int>(layer)) + ".letter");
        cell.letter->setTitle(std::string("Layer ") + kLayerLetters[layer]);
        cell.letter->setOnCallback([this, layer] {
            setSelectedLayer(layer);
            if (onSelectLayer)
            {
                onSelectLayer(layer);
            }
        });
        addAndMakeVisible(*cell.letter);

        const auto make = [&](ParamSlot slot) {
            auto control = std::make_unique<ParamControl>(context, slot, kindOf(context, slot));
            addAndMakeVisible(*control);
            return control;
        };
        cell.on = make(gg::core::layerOnSlot(layer));
        // A horizontal slider (spec §6.3), which fits a lane's height where a knob wouldn't.
        cell.level = std::make_unique<ParamControl>(context, gg::core::levelSlot(layer),
                                                    ParamControl::Kind::HSlider);
        addAndMakeVisible(*cell.level);
        cell.source = make(gg::core::layerSlot(LayerParam::Source, layer));
        cell.freeze = make(gg::core::layerSlot(LayerParam::Freeze, layer));
    }
}

LayerStrip::~LayerStrip() = default;

void LayerStrip::setLaneBounds(const std::array<juce::Rectangle<int>, 4> &lanes)
{
    impl_->lanes = lanes;
    resized();
    repaint();
}

void LayerStrip::setSelectedLayer(uint8_t layer)
{
    if (layer < gg::core::kLayers && layer != impl_->selected)
    {
        impl_->selected = layer;
        repaint();
    }
}

uint8_t LayerStrip::selectedLayer() const noexcept { return impl_->selected; }

void LayerStrip::refresh()
{
    for (Impl::Cell &cell : impl_->cells)
    {
        cell.on->refresh();
        cell.level->refresh();
        cell.source->refresh();
        cell.freeze->refresh();
    }
}

void LayerStrip::setSourceDropHandler(
    std::function<void(gg::core::ParamSlot, gg::core::ModSource)> handler)
{
    for (Impl::Cell &cell : impl_->cells)
    {
        for (ParamControl *control :
             {cell.on.get(), cell.level.get(), cell.source.get(), cell.freeze.get()})
        {
            control->setSourceDropEnabled(handler != nullptr);
            control->onSourceDrop = [control, handler](gg::core::ModSource source) {
                if (handler)
                {
                    handler(control->slot(), source);
                }
            };
        }
    }
}

void LayerStrip::resized()
{
    for (std::size_t layer = 0; layer < impl_->cells.size(); ++layer)
    {
        Impl::Cell &cell = impl_->cells[layer];
        auto area = impl_->cellBounds(getLocalBounds(), layer).reduced(3);
        // Three rows when they fit: the letter, On and Freeze; Source; Level. In a shorter cell
        // (a lane at the editor's smallest), two: Source and Level share the second, halved.
        const bool threeRows = area.getHeight() >= 3 * kRowHeight;
        const int row = std::min(kRowHeight, area.getHeight() / (threeRows ? 3 : 2));
        auto header = area.removeFromTop(row);
        cell.letter->setBounds(header.removeFromLeft(std::min(kRowHeight, header.getWidth())));
        const int half = header.getWidth() / 2;
        cell.on->setBounds(header.removeFromLeft(half));
        cell.freeze->setBounds(header);
        auto rest = area.removeFromTop(threeRows ? 2 * row : row);
        if (threeRows)
        {
            cell.source->setBounds(rest.removeFromTop(row));
            cell.level->setBounds(rest);
        }
        else
        {
            cell.source->setBounds(rest.removeFromLeft(rest.getWidth() / 2));
            cell.level->setBounds(rest);
        }
    }
}

void LayerStrip::paintOverChildren(juce::Graphics &g)
{
    const Palette &palette = paletteOf(*this);
    const std::size_t layer = impl_->selected;
    g.setColour(palette.layers[layer]);
    g.drawRoundedRectangle(impl_->cellBounds(getLocalBounds(), layer).toFloat().reduced(1.5f), 4.0f,
                           2.0f);
}
} // namespace gg::ui
