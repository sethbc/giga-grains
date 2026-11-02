// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_PRESETCOLOURS_H
#define GG_UI_PRESETCOLOURS_H

#include <juce_gui_basics/juce_gui_basics.h>

#include "Palette.h"

namespace gg::ui
{
/// The colors the preset strip, the browser and the Mutate panel give their JUCE controls (M2
/// sub-project 5, carry-over row 313). Each component calls these with paletteOf(*this) when it
/// is made, when its parent changes and on a look-and-feel change (the editor sends one on a
/// theme switch), as LoaderStrip does; what they paint themselves they read as they paint.

/// A button: the ground, the accent over it when on, the dim text off and the text on.
inline void colourButton(juce::TextButton &button, const Palette &palette)
{
    button.setColour(juce::TextButton::buttonColourId, palette.background);
    button.setColour(juce::TextButton::buttonOnColourId, palette.accent.withAlpha(0.35f));
    button.setColour(juce::TextButton::textColourOffId, palette.dimText);
    button.setColour(juce::TextButton::textColourOnId, palette.text);
    button.setColour(juce::ComboBox::outlineColourId, palette.outline);
}

/// A single-line text box on the palette's ground.
inline void colourEditor(juce::TextEditor &editor, const Palette &palette)
{
    editor.setColour(juce::TextEditor::backgroundColourId, palette.background);
    editor.setColour(juce::TextEditor::textColourId, palette.text);
    editor.setColour(juce::TextEditor::outlineColourId, palette.outline);
    editor.setColour(juce::TextEditor::focusedOutlineColourId, palette.accent);
    // The text shown while the box is empty is not a colour id: set it again, with its text.
    editor.setTextToShowWhenEmpty(editor.getTextToShowWhenEmpty(), palette.dimText);
}
} // namespace gg::ui

#endif // GG_UI_PRESETCOLOURS_H
