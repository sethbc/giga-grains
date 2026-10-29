// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_SPLATRENDERER_H
#define GG_UI_SPLATRENDERER_H

#include <cstdint>
#include <vector>

namespace gg::ui
{
/// The CPU additive "splat" renderer behind the grain canvas (INTENT §17). It is framework-free:
/// grains are splatted into a float accumulator, then resolved to 32-bit ARGB pixels that any
/// toolkit can draw.
///
/// Coordinates are in pixels, and pixel (x, y) has its centre at (x, y). A splat adds
/// intensity * max(0, 1 - d^2/r^2) to every pixel whose centre lies within the radius r of the
/// splat's centre, so splats sum: brightness accumulates where grains overlap. Resolving maps
/// each accumulated value a to v = 1 - exp(-a), which saturates smoothly instead of clipping.
///
/// Not thread-safe; the UI thread owns it.
class SplatRenderer
{
  public:
    /// A width x height canvas, cleared. Allocates width * height floats.
    SplatRenderer(uint32_t width, uint32_t height);

    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }

    /// Sets every pixel back to 0.
    void clear() noexcept;

    /// Adds one splat centred at (x, y). The parts outside the canvas are clipped. A splat with
    /// a non-finite argument or a radius that isn't positive adds nothing.
    void add(float x, float y, float radius, float intensity) noexcept;

    /// Adds one capsule: the splat's falloff, intensity * max(0, 1 - d^2/r^2), with d measured
    /// from the segment (x0, y) - (x1, y), so it is flat at full intensity along that segment.
    /// The ends may come in either order, and x0 == x1 adds exactly what add(x0, y, ...) does.
    /// Clipping and the non-finite and radius rules are add's.
    void addCapsule(float x0, float x1, float y, float radius, float intensity) noexcept;

    /// Writes every pixel to dst as 0xAARRGGBB: alpha 255 and each channel of rgbTint (0xRRGGBB)
    /// scaled by v = 1 - exp(-a), to within one 8-bit step. Row y starts at dst + y * strideU32;
    /// the strideU32 - width words that pad each row are left alone. Writes nothing if dst is
    /// null or strideU32 is less than the width.
    void resolveToArgb(uint32_t *dst, uint32_t strideU32, uint32_t rgbTint) const noexcept;

    /// The accumulated value at pixel (x, y); 0 outside the canvas.
    float accumAt(uint32_t x, uint32_t y) const noexcept;

  private:
    uint32_t width_;
    uint32_t height_;
    std::vector<float> accum_;
};
} // namespace gg::ui

#endif // GG_UI_SPLATRENDERER_H
