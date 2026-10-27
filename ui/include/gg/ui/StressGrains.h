// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_STRESSGRAINS_H
#define GG_UI_STRESSGRAINS_H

#include <cstdint>

namespace gg::ui
{
/// One grain as the canvas draws it: the arguments of SplatRenderer::add.
struct Splat
{
    float x, y, radius, intensity;
};

/// The M0 canvas stress workload, shared by the editor's stress panel and `gg-bench --canvas`
/// so both measure the same thing. It writes `count` synthetic grains for one animation frame,
/// on a width x height canvas (in physical pixels), to out[0 .. count).
///
/// Each grain slot lives for 20 to 59 frames, then respawns at a new place. A grain sits in one
/// of four lanes (the layers), at a random x and a pitch offset within its lane; it drifts in x
/// as it plays and fades in and out over its life. Its radius is 2 to 8 logical pixels, times
/// `pixelScale` (the canvas's physical pixels per logical pixel).
///
/// Everything is a pure function of (frame, slot), mixed with gg::rng::splitmix64. It never
/// touches the audio RNG's keyed hash or its fields (INTENT §17).
void stressGrains(uint64_t frame, uint32_t count, uint32_t width, uint32_t height, float pixelScale,
                  Splat *out) noexcept;
} // namespace gg::ui

#endif // GG_UI_STRESSGRAINS_H
