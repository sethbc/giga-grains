// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_THEME_H
#define GG_UI_THEME_H

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>
#include <sst/jucegui/style/StyleSheet.h>

namespace gg::ui::theme
{
/// The one dark theme (sub-project 5's spec §6.5): sst-jucegui's built-in dark style sheet for
/// its controls, and these colours for what the editor draws itself. The style sheets are set up
/// on the first call; UI thread only.
inline sst::jucegui::style::StyleSheet::ptr_t darkStyle()
{
    // initializeStyleSheets is idempotent (it registers the classes once per process), but the
    // built-in sheet is rebuilt after JUCE's shutdown cleanup deletes it, so ask for it each
    // time rather than keeping it.
    sst::jucegui::style::StyleSheet::initializeStyleSheets([] {});
    return sst::jucegui::style::StyleSheet::getBuiltInStyleSheet(
        sst::jucegui::style::StyleSheet::DARK);
}

// The editor's own surfaces and text.
inline const juce::Colour kBackground{0xff15171c};
inline const juce::Colour kHeader{0xff1c1f26};
inline const juce::Colour kLane{0xff0b0c0f}; ///< the canvas behind the grains
inline const juce::Colour kText{0xffe8e8ec};
inline const juce::Colour kDimText{0xff9aa0ab};
inline const juce::Colour kOutline{0xff2c313b};
inline const juce::Colour kAccent{0xff6fc3df}; ///< Perform, selected

// The canvas.
inline constexpr uint32_t kGrainTint = 0xFFC46B; ///< the splats' 0xRRGGBB (amber)
inline const juce::Colour kPeaks{0xff3b4150};    ///< the live ring's or the sample's outline
inline const juce::Colour kWindow{0xff6fc3df};   ///< the scan window at the base values
inline const juce::Colour kGhost{0xffe8e8ec};    ///< where the effective values have it
inline const juce::Colour kFreeze{0xff7fb4ff};   ///< the frozen lane's tint
inline const juce::Colour kFeedback{0xffff6b8b}; ///< the regeneration marker at the head
} // namespace gg::ui::theme

#endif // GG_UI_THEME_H
