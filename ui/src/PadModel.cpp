// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/PadModel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

// UI-thread display code, as LaneModel's: nothing here reaches the audio.

namespace gg::ui
{
namespace
{
/// v clamped to 0..1, with NaN mapped to the centre.
float clampValue(float v) { return std::isnan(v) ? 0.5f : std::clamp(v, 0.0f, 1.0f); }

bool usableSide(float side) { return std::isfinite(side) && side > 0.0f; }
} // namespace

PadPoint padValueAt(float px, float py, float side) noexcept
{
    if (!usableSide(side))
    {
        return {0.5f, 0.5f};
    }
    return {clampValue(px / side), clampValue(1.0f - py / side)};
}

std::array<float, 2> padPixelOf(PadPoint value, float side) noexcept
{
    if (!usableSide(side))
    {
        return {0.0f, 0.0f};
    }
    return {clampValue(value.x) * side, (1.0f - clampValue(value.y)) * side};
}

std::array<float, gg::core::kLayers>
morphWeights(float x, float y, const std::array<bool, gg::core::kLayers> &on) noexcept
{
    // gg-core's Processor::weightsAt with every ramp settled at 0 or 1, in the same float
    // operations, so the pad's shading is the engine's weights.
    static_assert(gg::core::kLayers == 4);
    const float cx = std::clamp(std::isnan(x) ? 0.5f : x, 0.001f, 0.999f);
    const float cy = std::clamp(std::isnan(y) ? 0.5f : y, 0.001f, 0.999f);
    const float ix = 1.0f - cx;
    const float iy = 1.0f - cy;
    const std::array<float, gg::core::kLayers> corner{ix * iy, cx * iy, ix * cy, cx * cy};
    std::array<float, gg::core::kLayers> rb{};
    float sum = 0.0f;
    float ramps = 0.0f;
    for (std::size_t L = 0; L < gg::core::kLayers; ++L)
    {
        const float r = on[L] ? 1.0f : 0.0f;
        rb[L] = r * corner[L];
        sum += rb[L];
        ramps += r;
    }
    const float fade = std::min(1.0f, ramps);
    std::array<float, gg::core::kLayers> weights{};
    for (std::size_t L = 0; L < gg::core::kLayers; ++L)
    {
        weights[L] = sum > 0.0f ? rb[L] / sum * fade : 0.0f;
    }
    return weights;
}
} // namespace gg::ui
