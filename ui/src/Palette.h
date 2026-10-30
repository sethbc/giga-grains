// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_PALETTE_H
#define GG_UI_PALETTE_H

#include <array>
#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

namespace gg::ui
{
/// The colours the editor draws itself (M2 sub-project 6's spec §6.9), in two instances: dark()
/// (M1's colours, the default, D8) and light(). sst-jucegui's controls take their colours from
/// its own dark and light style sheets; these are for the editor's surfaces, text and canvas.
///
/// **The lanes stay dark in both** (D9): `lane`, `laneRule`, `laneText`, `peaks`, `window`,
/// `ghost`, `freeze` and `feedback` are the same in light() as in dark(), since the canvas is
/// additive light on a dark ground. The layers' colours (kLayerTints, Okabe–Ito's four) are the
/// same in both too.
struct Palette
{
    // The editor's surfaces and text.
    juce::Colour background, header;
    juce::Colour lane; ///< the canvas behind the grains
    juce::Colour text, dimText, outline;
    juce::Colour accent; ///< the view selected, a focused or selected control
    // The canvas.
    juce::Colour laneRule; ///< the rules between lanes and the canvas's frame
    juce::Colour laneText; ///< a hint drawn on a lane ("Load a sample, ...")
    juce::Colour peaks;    ///< the live ring's or the sample's outline
    juce::Colour window;   ///< the scan window at the base values
    juce::Colour ghost;    ///< where the effective values have it
    juce::Colour freeze;   ///< a frozen lane's tint
    juce::Colour feedback; ///< the regeneration marker at the head
    juce::Colour notice;   ///< a transient message's text (a refused drop, a failed save)
    /// The layers A to D: kLayerTints, opaque.
    std::array<juce::Colour, 4> layers;
    /// The Mod view's source families (spec §6.6, §6.9), in order: macros, modulators, MIDI,
    /// follower, transport. From Okabe–Ito's eight, apart from the layers' four: yellow (in
    /// light() darkened to an ochre, 0x8f7a00, since the yellow is about 1.2:1 on its ground),
    /// vermillion, blue, its black shown as grey for the follower, and, for the transport, its
    /// black in light() and the text's near-white in dark(), where black wouldn't show.
    std::array<juce::Colour, 5> sourceFamilies;

    static const Palette &dark();
    static const Palette &light();
};

/// The theme an editor shows (spec §6.9): the per-machine setting's value.
enum class ThemeChoice : uint8_t
{
    Dark,
    Light
};

/// What the editor's root implements, so that every component below it reads the current
/// palette rather than constants, and a theme switch restyles the tree in place.
struct PaletteOwner
{
    virtual ~PaletteOwner() = default;
    virtual const Palette &palette() const noexcept = 0;
    virtual ThemeChoice theme() const noexcept = 0;
};

/// The palette of the nearest PaletteOwner at or above `c`; Palette::dark() if there is none
/// (a component not yet in an editor, or built alone by a test). Message thread only.
const Palette &paletteOf(const juce::Component &c) noexcept;
} // namespace gg::ui

#endif // GG_UI_PALETTE_H
