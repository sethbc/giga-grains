// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/SplatRenderer.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>

// The two inner loops below (one row of a splat, one row of the resolve) are written so the
// compiler can vectorize them: no calls, no early exits and only selects for branches. This is
// display code, so the determinism rules for the audio path (INTENT §14) don't bind it.

namespace gg::ui
{
namespace
{
/// Past this, 1 - exp(-a) is 1 to float precision, so the resolve clamps there.
constexpr float kMaxResolved = 24.0f;

/// exp(-a) for a in [0, kMaxResolved], to about 1e-4 relative: far below one 8-bit step.
/// exp(-a) = 2^t with t = -a log2(e), split into t = n + f with n = floor(t) and f in [0, 1);
/// 2^n is built directly in the float's exponent and 2^f comes from a cubic that is 1 at f = 0
/// and 2 at f = 1 (up to rounding), so the result is continuous and monotonic, and exp(0) is
/// exactly 1.
inline float expNegative(float a) noexcept
{
    constexpr float kLog2e = 1.44269504f;
    constexpr float c1 = 0.69606564f;
    constexpr float c2 = 0.22449434f;
    constexpr float c3 = 1.0f - c1 - c2;    // so that 1 + c1 + c2 + c3 = 2
    const float t = -a * kLog2e;            // in [-34.7, 0]
    int32_t n = static_cast<int32_t>(t);    // truncates toward 0, so n >= t ...
    n -= static_cast<float>(n) > t ? 1 : 0; // ... and this makes it floor(t)
    const float f = t - static_cast<float>(n);
    const float p = 1.0f + f * (c1 + f * (c2 + f * c3));
    // n >= -35, so the biased exponent n + 127 stays a normal float's.
    const float twoToN = std::bit_cast<float>(static_cast<uint32_t>(n + 127) << 23);
    return twoToN * p;
}

/// One 8-bit channel: tint * v, rounded. tint is in [0, 255] and v in [0, 1].
inline uint32_t scaleChannel(float tint, float v) noexcept
{
    return static_cast<uint32_t>(static_cast<int32_t>(tint * v + 0.5f));
}
} // namespace

SplatRenderer::SplatRenderer(uint32_t width, uint32_t height)
    : width_(width), height_(height), accum_(static_cast<std::size_t>(width) * height, 0.0f)
{
}

void SplatRenderer::clear() noexcept { std::fill(accum_.begin(), accum_.end(), 0.0f); }

void SplatRenderer::add(float x, float y, float radius, float intensity) noexcept
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) ||
        !std::isfinite(intensity) || !(radius > 0.0f) || width_ == 0 || height_ == 0)
    {
        return;
    }

    // The pixels whose centres can lie within the radius: the splat's bounding box, clipped to
    // the canvas. The bounds are clipped while still floating-point, so a centre far off the
    // canvas never reaches an integer conversion. The last pixel's coordinates are doubles, which
    // hold every uint32_t exactly: a float rounds width − 1 up to width for some widths past
    // 2^24, which would let a splat write past the row's end.
    const auto maxX = static_cast<double>(width_ - 1);
    const auto maxY = static_cast<double>(height_ - 1);
    const double left = std::ceil(x - radius);
    const double right = std::floor(x + radius);
    const double top = std::ceil(y - radius);
    const double bottom = std::floor(y + radius);
    if (right < 0.0 || left > maxX || bottom < 0.0 || top > maxY)
    {
        return;
    }
    const auto px0 = static_cast<uint32_t>(std::max(left, 0.0));
    const auto px1 = static_cast<uint32_t>(std::min(right, maxX));
    const auto py0 = static_cast<uint32_t>(std::max(top, 0.0));
    const auto py1 = static_cast<uint32_t>(std::min(bottom, maxY));

    // The weight is 1 - d^2/r^2 = (1 - dy^2/r^2) - dx^2/r^2: one base per row, then one
    // subtraction per pixel. At the centre it is exactly 1, so a splat lands its intensity there.
    // Every row spans the whole box and the clamp to 0 drops its corners (adding 0 leaves a
    // pixel as it was). That is about twice as fast as fitting each row to the circle's chord:
    // the rows are short, so a per-row square root, rounding and changing trip count cost more
    // than the corner pixels.
    const float invR2 = 1.0f / (radius * radius);
    for (uint32_t py = py0; py <= py1; ++py)
    {
        const float dy = static_cast<float>(py) - y;
        const float rowBase = 1.0f - dy * dy * invR2;
        float *row = accum_.data() + static_cast<std::size_t>(py) * width_;
        for (uint32_t px = px0; px <= px1; ++px)
        {
            const float dx = static_cast<float>(px) - x;
            const float weight = rowBase - dx * dx * invR2;
            row[px] += intensity * (weight > 0.0f ? weight : 0.0f);
        }
    }
}

void SplatRenderer::addCapsule(float x0, float x1, float y, float radius, float intensity) noexcept
{
    if (!std::isfinite(x0) || !std::isfinite(x1) || !std::isfinite(y) || !std::isfinite(radius) ||
        !std::isfinite(intensity) || !(radius > 0.0f) || width_ == 0 || height_ == 0)
    {
        return;
    }
    const float lo = std::min(x0, x1);
    const float hi = std::max(x0, x1);

    // The bounding box of the segment grown by the radius, computed and clipped as add's is (so
    // x0 == x1 covers add's pixels), in floating point: ends far off the canvas never reach an
    // integer conversion.
    const auto maxX = static_cast<double>(width_ - 1);
    const auto maxY = static_cast<double>(height_ - 1);
    const double left = std::ceil(lo - radius);
    const double right = std::floor(hi + radius);
    const double top = std::ceil(y - radius);
    const double bottom = std::floor(y + radius);
    if (right < 0.0 || left > maxX || bottom < 0.0 || top > maxY)
    {
        return;
    }
    const auto px0 = static_cast<uint32_t>(std::max(left, 0.0));
    const auto px1 = static_cast<uint32_t>(std::min(right, maxX));
    const auto py0 = static_cast<uint32_t>(std::max(top, 0.0));
    const auto py1 = static_cast<uint32_t>(std::min(bottom, maxY));

    // add's loop, with dx measured from the pixel's nearest point on the segment: 0 between the
    // ends, so the axis gets the full intensity. With lo == hi the nearest point is x0 itself and
    // dx = px - x0 bit for bit, so the result is add's exactly.
    const float invR2 = 1.0f / (radius * radius);
    for (uint32_t py = py0; py <= py1; ++py)
    {
        const float dy = static_cast<float>(py) - y;
        const float rowBase = 1.0f - dy * dy * invR2;
        float *row = accum_.data() + static_cast<std::size_t>(py) * width_;
        for (uint32_t px = px0; px <= px1; ++px)
        {
            const auto fx = static_cast<float>(px);
            const float nearest = fx < lo ? lo : (fx > hi ? hi : fx);
            const float dx = fx - nearest;
            const float weight = rowBase - dx * dx * invR2;
            row[px] += intensity * (weight > 0.0f ? weight : 0.0f);
        }
    }
}

void SplatRenderer::resolveToArgb(uint32_t *dst, uint32_t strideU32,
                                  uint32_t rgbTint) const noexcept
{
    if (dst == nullptr || strideU32 < width_)
    {
        return;
    }
    const auto tintR = static_cast<float>((rgbTint >> 16) & 0xFFu);
    const auto tintG = static_cast<float>((rgbTint >> 8) & 0xFFu);
    const auto tintB = static_cast<float>(rgbTint & 0xFFu);

    // Locals, not members: a store through out could alias width_ (both uint32_t), which would
    // make the loop's trip count unknown to the vectorizer.
    const uint32_t width = width_;
    const uint32_t height = height_;
    const float *accum = accum_.data();
    for (uint32_t y = 0; y < height; ++y)
    {
        const float *src = accum + static_cast<std::size_t>(y) * width;
        uint32_t *out = dst + static_cast<std::size_t>(y) * strideU32;
        for (uint32_t x = 0; x < width; ++x)
        {
            float a = src[x];
            a = a > 0.0f ? a : 0.0f; // also maps NaN to 0
            a = a < kMaxResolved ? a : kMaxResolved;
            float v = 1.0f - expNegative(a);
            v = v > 0.0f ? v : 0.0f;
            out[x] = 0xFF000000u | (scaleChannel(tintR, v) << 16) | (scaleChannel(tintG, v) << 8) |
                     scaleChannel(tintB, v);
        }
    }
}

float SplatRenderer::accumAt(uint32_t x, uint32_t y) const noexcept
{
    if (x >= width_ || y >= height_)
    {
        return 0.0f;
    }
    return accum_[static_cast<std::size_t>(y) * width_ + x];
}
} // namespace gg::ui
