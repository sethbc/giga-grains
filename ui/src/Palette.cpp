// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "Palette.h"

#include <cstddef>

#include "gg/ui/LayerColours.h"

namespace gg::ui
{
namespace
{
/// The layers' colours, opaque.
std::array<juce::Colour, 4> layerColours()
{
    std::array<juce::Colour, 4> colours;
    for (std::size_t i = 0; i < colours.size(); ++i)
    {
        colours[i] = juce::Colour(0xff000000u | kLayerTints[i]);
    }
    return colours;
}

Palette makeDark()
{
    Palette p;
    p.background = juce::Colour(0xff15171c);
    p.header = juce::Colour(0xff1c1f26);
    p.lane = juce::Colour(0xff0b0c0f);
    p.text = juce::Colour(0xffe8e8ec);
    p.dimText = juce::Colour(0xff9aa0ab);
    p.outline = juce::Colour(0xff2c313b);
    p.accent = juce::Colour(0xff6fc3df);
    p.laneRule = juce::Colour(0xff2c313b);
    p.laneText = juce::Colour(0xff9aa0ab);
    p.peaks = juce::Colour(0xff3b4150);
    p.window = juce::Colour(0xff6fc3df);
    p.ghost = juce::Colour(0xffe8e8ec);
    p.freeze = juce::Colour(0xff7fb4ff);
    p.feedback = juce::Colour(0xffff6b8b);
    p.notice = juce::Colour(0xfff0e442); // Okabe–Ito's yellow
    p.layers = layerColours();
    p.sourceFamilies = {juce::Colour(0xfff0e442), juce::Colour(0xffd55e00),
                        juce::Colour(0xff0072b2), juce::Colour(0xff999999), p.text};
    return p;
}

Palette makeLight()
{
    // sst-jucegui's light sheet's surfaces; the canvas's colours are the dark palette's (D9).
    Palette p = makeDark();
    p.background = juce::Colour(0xfff2f3f5);
    p.header = juce::Colour(0xffe6e8ec);
    p.text = juce::Colour(0xff15171c);
    p.dimText = juce::Colour(0xff5c6370);
    p.outline = juce::Colour(0xffc9cdd6);
    p.accent = juce::Colour(0xff1f7a99);
    p.notice = juce::Colour(0xffd55e00); // Okabe–Ito's vermillion, legible on the light ground
    // Okabe–Ito's yellow is about 1.2:1 on the light ground: the macros take a darkened ochre
    // (about 4:1), the transport Okabe–Ito's black.
    p.sourceFamilies[0] = juce::Colour(0xff8f7a00);
    p.sourceFamilies[4] = juce::Colour(0xff000000);
    return p;
}
} // namespace

const Palette &Palette::dark()
{
    static const Palette palette = makeDark();
    return palette;
}

const Palette &Palette::light()
{
    static const Palette palette = makeLight();
    return palette;
}

const Palette &paletteOf(const juce::Component &c) noexcept
{
    for (const juce::Component *at = &c; at != nullptr; at = at->getParentComponent())
    {
        if (const auto *owner = dynamic_cast<const PaletteOwner *>(at))
        {
            return owner->palette();
        }
    }
    return Palette::dark();
}
} // namespace gg::ui
