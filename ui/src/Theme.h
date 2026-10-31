// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_THEME_H
#define GG_UI_THEME_H

#include <juce_gui_basics/juce_gui_basics.h>

#include "Palette.h"

namespace gg::ui::theme
{
/// The fixed colors M2 sub-project 5's components (the preset strip, the browser and the Mutate
/// panel) set from when they are made: Palette::dark()'s, which are M1's. M2 sub-project 6
/// replaced M1's Theme.h with Palette; this header keeps those components' colors as sub-project
/// 5 built them, so they don't follow the light theme yet. Everything else reads paletteOf.
inline const juce::Colour kBackground = Palette::dark().background;
inline const juce::Colour kHeader = Palette::dark().header;
inline const juce::Colour kLane = Palette::dark().lane; ///< the canvas behind the grains
inline const juce::Colour kText = Palette::dark().text;
inline const juce::Colour kDimText = Palette::dark().dimText;
inline const juce::Colour kOutline = Palette::dark().outline;
inline const juce::Colour kAccent = Palette::dark().accent;
} // namespace gg::ui::theme

#endif // GG_UI_THEME_H
