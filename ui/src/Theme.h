// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_THEME_H
#define GG_UI_THEME_H

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>
#include <sst/jucegui/style/StyleSheet.h>

#include "Palette.h"

namespace gg::ui::theme
{
/// The dark theme (sub-project 5's spec §6.5): sst-jucegui's built-in dark style sheet for its
/// controls, and these colours, Palette::dark()'s (M2 sub-project 6's spec §6.9), for what the
/// editor draws itself: a thin alias kept while the editor's components move to paletteOf. The
/// style sheets are set up on the first call; UI thread only.
inline sst::jucegui::style::StyleSheet::ptr_t darkStyle()
{
    // initializeStyleSheets is idempotent (it registers the classes once per process), but the
    // built-in sheet is rebuilt after JUCE's shutdown cleanup deletes it, so ask for it each
    // time rather than keeping it.
    sst::jucegui::style::StyleSheet::initializeStyleSheets([] {});
    return sst::jucegui::style::StyleSheet::getBuiltInStyleSheet(
        sst::jucegui::style::StyleSheet::DARK);
}

// Palette::dark()'s colours (see Palette for each), until every consumer reads its editor's
// palette (paletteOf) instead.
inline const juce::Colour kBackground{Palette::dark().background};
inline const juce::Colour kHeader{Palette::dark().header};
inline const juce::Colour kLane{Palette::dark().lane};
inline const juce::Colour kText{Palette::dark().text};
inline const juce::Colour kDimText{Palette::dark().dimText};
inline const juce::Colour kOutline{Palette::dark().outline};
inline const juce::Colour kAccent{Palette::dark().accent};
inline const juce::Colour kPeaks{Palette::dark().peaks};
inline const juce::Colour kWindow{Palette::dark().window};
inline const juce::Colour kGhost{Palette::dark().ghost};
inline const juce::Colour kFreeze{Palette::dark().freeze};
inline const juce::Colour kFeedback{Palette::dark().feedback};

/// The splats' 0xRRGGBB (amber, M1's), until the canvas tints each lane by its layer's colour.
inline constexpr uint32_t kGrainTint = 0xFFC46B;
} // namespace gg::ui::theme

#endif // GG_UI_THEME_H
