// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_LAYERCOLOURS_H
#define GG_UI_LAYERCOLOURS_H

#include <array>
#include <cstdint>

namespace gg::ui
{
/// The layers' colours, A to D, as 0xRRGGBB (M2 sub-project 6's spec §6.9, D4): Okabe–Ito's four
/// most separable hues, which deuteranopes, protanopes and tritanopes tell apart: orange, sky
/// blue, bluish green and reddish purple. They are the grains' tints, the lane letters, the pad's
/// corners and the source chips' layer tags, the same in both themes. Colour is never the only
/// cue: everything tinted also carries its layer's letter (kLayerLetters). Framework-free, so the
/// canvas's resolve bands can take them without JUCE.
inline constexpr std::array<uint32_t, 4> kLayerTints{0xE69F00, 0x56B4E9, 0x009E73, 0xCC79A7};

/// The layers' letters, A to D.
inline constexpr std::array<char, 4> kLayerLetters{'A', 'B', 'C', 'D'};
} // namespace gg::ui

#endif // GG_UI_LAYERCOLOURS_H
