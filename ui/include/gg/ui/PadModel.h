// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_PADMODEL_H
#define GG_UI_PADMODEL_H

#include <array>

#include "gg/core/ParamIds.h"

namespace gg::ui
{
/// The XY morph pad's geometry and shading (M2 Views' spec §6.3): pure functions, framework-free,
/// for the UI thread only, as LaneModel's are.
///
/// The pad is a side × side square in pixels, (0, 0) at its top-left and y down, as the canvas's
/// coordinates are. Its values are Morph X and Morph Y with Y up, so the morph's corners sit at
/// A bottom-left (X 0, Y 0), B bottom-right (1, 0), C top-left (0, 1) and D top-right (1, 1).

/// Morph X and Morph Y's CLAP values, each 0..1, Y up.
struct PadPoint
{
    float x, y;
};

/// The values at pixel (px, py) of a side × side pad: x = px / side, y = 1 − py / side, each
/// clamped to 0..1, so a drag past the pad pins the handle to its edge. A NaN coordinate gives
/// 0.5 for its value, and a side that isn't positive and finite gives the centre, (0.5, 0.5).
PadPoint padValueAt(float px, float py, float side) noexcept;

/// The pixel (x, y) of `value` on a side × side pad, padValueAt's inverse: where the handle is
/// drawn. Each value is clamped to 0..1 first, NaN to 0.5; a side that isn't positive and finite
/// gives (0, 0).
std::array<float, 2> padPixelOf(PadPoint value, float side) noexcept;

/// The layers' morph weights at pad position (x, y) with the Layer On switches `on`, as the
/// engine computes them once its on/off ramps settle (gg-core's Processor, D1's law; for the
/// picture only, never the audio): x and y are clamped to [0.001, 0.999] as the engine clamps
/// them (NaN to 0.5), the corners' bilinear weights are b = {(1−x)(1−y), x(1−y), (1−x)y, xy},
/// and with r_L = on[L] ? 1 : 0 and s = Σ r_K b_K, weight_L = s > 0 ? (r_L b_L / s) × min(1,
/// Σ r_K) : 0. So the on layers' weights sum to 1, an off layer's is 0, and none on gives all 0.
std::array<float, gg::core::kLayers>
morphWeights(float x, float y, const std::array<bool, gg::core::kLayers> &on) noexcept;
} // namespace gg::ui

#endif // GG_UI_PADMODEL_H
