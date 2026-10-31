// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "SourceStrip.h"

#include <algorithm>

#include "../Palette.h"
#include "gg/ui/LayerColours.h"
#include "gg/ui/Rings.h"

namespace gg::ui
{
namespace
{
using gg::core::ModSource;

constexpr int kChipW = 68, kChipH = 22, kGap = 4, kGroupGap = 10;

/// The chip's short label.
const char *labelOf(ModSource s)
{
    switch (s)
    {
    case ModSource::Macro1:
        return "Macro 1";
    case ModSource::Macro2:
        return "Macro 2";
    case ModSource::Macro3:
        return "Macro 3";
    case ModSource::Macro4:
        return "Macro 4";
    case ModSource::Macro5:
        return "Macro 5";
    case ModSource::Macro6:
        return "Macro 6";
    case ModSource::Macro7:
        return "Macro 7";
    case ModSource::Macro8:
        return "Macro 8";
    case ModSource::Lfo1:
        return "LFO 1";
    case ModSource::Lfo2:
        return "LFO 2";
    case ModSource::Env1:
        return "Env 1";
    case ModSource::Env2:
        return "Env 2";
    case ModSource::Random1:
        return "Random 1";
    case ModSource::Random2:
        return "Random 2";
    case ModSource::Step:
        return "Step";
    case ModSource::ModWheel:
        return "Wheel";
    case ModSource::Aftertouch:
        return "Aftertouch";
    case ModSource::PitchBend:
        return "Bend";
    case ModSource::Velocity:
        return "Velocity";
    case ModSource::Key:
        return "Key";
    case ModSource::EnvFollower:
        return "Follower";
    case ModSource::TransportBeat:
        return "Beat";
    case ModSource::TransportBar:
        return "Bar";
    case ModSource::TransportPlaying:
        return "Playing";
    default:
        return "";
    }
}

/// Whether a group starts at `s`: the macros, the modulators, the MIDI sources, the follower and
/// the transport's.
bool startsGroup(ModSource s)
{
    return s == ModSource::Macro1 || s == ModSource::Lfo1 || s == ModSource::ModWheel ||
           s == ModSource::EnvFollower || s == ModSource::TransportBeat;
}
} // namespace

SourceChip::SourceChip(ModSource source) : source_(source), baseText_(labelOf(source))
{
    setComponentID("source." + juce::String(static_cast<int>(source)));
    setButtonText(baseText_);
    setTitle(gg::core::modSourceName(source));
    setTooltip(gg::core::modSourceName(source));
}

juce::String SourceChip::description() const
{
    return "modsource:" + juce::String(static_cast<int>(source_));
}

void SourceChip::setLayer(uint8_t layer)
{
    if (gg::core::isPerLayerSource(source_) && layer < kLayerLetters.size())
    {
        setButtonText(baseText_ + " " + juce::String::charToString(kLayerLetters[layer]));
    }
}

void SourceChip::paintButton(juce::Graphics &g, bool highlighted, bool down)
{
    const Palette &palette = paletteOf(*this);
    const juce::Colour family = palette.sourceFamilies[sourceFamily(source_)];
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(family.withAlpha(down ? 0.55f : highlighted ? 0.4f : 0.25f));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(family);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    g.setColour(palette.text);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
}

void SourceChip::mouseDrag(const juce::MouseEvent &e)
{
    juce::TextButton::mouseDrag(e);
    if (e.getDistanceFromDragStart() < 4)
    {
        return;
    }
    // The container is the Mod view's (or an editor's): none, no drag.
    auto *container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (container != nullptr && !container->isDragAndDropActive())
    {
        container->startDragging(description(), this);
    }
}

SourceStrip::SourceStrip()
{
    setComponentID("sources");
    for (std::size_t i = 0; i < chips_.size(); ++i)
    {
        chips_[i] = std::make_unique<SourceChip>(static_cast<ModSource>(i + 1));
        addAndMakeVisible(*chips_[i]);
    }
}

SourceStrip::~SourceStrip() = default;

void SourceStrip::setLayer(uint8_t layer)
{
    for (const auto &chip : chips_)
    {
        chip->setLayer(layer);
    }
    repaint();
}

int SourceStrip::layout(int width, bool place)
{
    int x = 0, y = 0;
    for (const auto &chip : chips_)
    {
        const int lead = startsGroup(chip->source()) && x > 0 ? kGroupGap : 0;
        if (x > 0 && x + lead + kChipW > width)
        {
            x = 0;
            y += kChipH + kGap;
        }
        else
        {
            x += lead;
        }
        if (place)
        {
            chip->setBounds(x, y, kChipW, kChipH);
        }
        x += kChipW + kGap;
    }
    return y + kChipH;
}

int SourceStrip::heightFor(int width) { return layout(std::max(width, kChipW), false); }

void SourceStrip::resized() { layout(std::max(getWidth(), kChipW), true); }
} // namespace gg::ui
