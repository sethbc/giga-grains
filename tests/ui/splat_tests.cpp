// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The CPU additive splat renderer behind the grain canvas (INTENT §17). These tests link
// gg-ui-core only, so they also run in the sanitizer builds, which have no editor or JUCE.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gg/ui/SplatRenderer.h"

using Catch::Matchers::WithinAbs;

namespace
{
/// The kernel the renderer promises: intensity * max(0, 1 - d^2/r^2), for pixel centres at
/// integer coordinates.
float expectedSplat(float cx, float cy, float r, float intensity, uint32_t x, uint32_t y)
{
    const float dx = static_cast<float>(x) - cx;
    const float dy = static_cast<float>(y) - cy;
    const float d2 = dx * dx + dy * dy;
    return d2 < r * r ? intensity * (1.0f - d2 / (r * r)) : 0.0f;
}

uint32_t channel(uint32_t argb, int shift) { return (argb >> shift) & 0xFFu; }
} // namespace

TEST_CASE("splat writes intensity at center and nothing outside radius", "[ui]")
{
    constexpr uint32_t w = 32, h = 24;
    gg::ui::SplatRenderer splat(w, h);
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            REQUIRE(splat.accumAt(x, y) == 0.0f);
        }
    }

    splat.add(10, 10, 3, 0.7f);
    REQUIRE(splat.accumAt(10, 10) == 0.7f);
    REQUIRE(splat.accumAt(14, 10) == 0.0f);
    REQUIRE(splat.accumAt(13, 10) == 0.0f); // exactly on the radius: weight 0
    REQUIRE(splat.accumAt(10, 7) == 0.0f);

    // Every pixel follows the kernel: inside the radius it falls off as 1 - d^2/r^2, outside it
    // is untouched.
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            INFO("pixel " << x << ", " << y);
            const float expected = expectedSplat(10, 10, 3, 0.7f, x, y);
            if (expected == 0.0f)
            {
                REQUIRE(splat.accumAt(x, y) == 0.0f);
            }
            else
            {
                REQUIRE_THAT(splat.accumAt(x, y), WithinAbs(expected, 1e-6));
            }
        }
    }

    // A splat between pixel centres weights each pixel by its own distance.
    splat.clear();
    splat.add(20.5f, 5.25f, 2.5f, 1.0f);
    REQUIRE_THAT(splat.accumAt(20, 5),
                 WithinAbs(expectedSplat(20.5f, 5.25f, 2.5f, 1, 20, 5), 1e-6));
    REQUIRE_THAT(splat.accumAt(22, 6),
                 WithinAbs(expectedSplat(20.5f, 5.25f, 2.5f, 1, 22, 6), 1e-6));
    REQUIRE(splat.accumAt(10, 10) == 0.0f); // clear() removed the first splat
}

TEST_CASE("splats are additive", "[ui]")
{
    constexpr uint32_t w = 32, h = 24;
    gg::ui::SplatRenderer once(w, h), twice(w, h);
    once.add(10, 10, 3, 0.7f);
    twice.add(10, 10, 3, 0.7f);
    twice.add(10, 10, 3, 0.7f);

    REQUIRE(twice.accumAt(10, 10) == 2 * 0.7f);
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            INFO("pixel " << x << ", " << y);
            REQUIRE(twice.accumAt(x, y) == 2 * once.accumAt(x, y));
        }
    }

    // Overlapping splats at different points sum too.
    gg::ui::SplatRenderer a(w, h), b(w, h), ab(w, h);
    a.add(8, 8, 4, 0.25f);
    b.add(11, 9, 5, 0.5f);
    ab.add(8, 8, 4, 0.25f);
    ab.add(11, 9, 5, 0.5f);
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            INFO("pixel " << x << ", " << y);
            REQUIRE(ab.accumAt(x, y) == a.accumAt(x, y) + b.accumAt(x, y));
        }
    }
}

TEST_CASE("offscreen splats are clipped safely", "[ui]")
{
    constexpr uint32_t w = 16, h = 12;
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();
    gg::ui::SplatRenderer splat(w, h);

    // Partly offscreen, past each corner: only the in-bounds part lands.
    splat.add(-5, -5, 8, 1);
    splat.add(w + 3, h + 3, 8, 1);
    REQUIRE_THAT(splat.accumAt(0, 0), WithinAbs(1.0f - 50.0f / 64.0f, 1e-6));
    REQUIRE_THAT(splat.accumAt(w - 1, h - 1), WithinAbs(1.0f - 32.0f / 64.0f, 1e-6));
    REQUIRE(splat.accumAt(w / 2, h / 2) == 0.0f);

    // Entirely offscreen, or degenerate: nothing lands, nothing is touched out of bounds.
    std::vector<float> before;
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            before.push_back(splat.accumAt(x, y));
        }
    }
    splat.add(-100, 5, 8, 1);
    splat.add(5, h + 100, 8, 1);
    splat.add(1e30f, -1e30f, 4, 1);
    splat.add(5, 5, 0, 1);
    splat.add(5, 5, -2, 1);
    splat.add(kNaN, 5, 3, 1);
    splat.add(5, kInf, 3, 1);
    splat.add(5, 5, kNaN, 1);
    splat.add(5, 5, kInf, 1);
    splat.add(5, 5, 3, kNaN);
    splat.add(5, 5, 3, kInf);
    std::size_t i = 0;
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            INFO("pixel " << x << ", " << y);
            REQUIRE(splat.accumAt(x, y) == before[i++]);
        }
    }

    // A splat far bigger than the canvas covers all of it.
    splat.clear();
    splat.add(w / 2.0f, h / 2.0f, 1e6f, 1);
    REQUIRE(splat.accumAt(0, 0) > 0.99f);
    REQUIRE(splat.accumAt(w - 1, h - 1) > 0.99f);

    // Reading outside the canvas is safe and reads nothing.
    REQUIRE(splat.accumAt(w, 0) == 0.0f);
    REQUIRE(splat.accumAt(0, h) == 0.0f);
    REQUIRE(splat.accumAt(UINT32_MAX, UINT32_MAX) == 0.0f);

    // A zero-sized canvas accepts everything and draws nothing.
    gg::ui::SplatRenderer empty(0, 0);
    empty.clear();
    empty.add(0, 0, 4, 1);
    REQUIRE(empty.accumAt(0, 0) == 0.0f);
    empty.resolveToArgb(nullptr, 0, 0xFFFFFF);
}

TEST_CASE("a splat at the right edge of a canvas over 16M pixels wide stays on it", "[ui]")
{
    // Past 2^24 a float no longer holds every integer: float(width - 1) rounds up to width for
    // this width, so a clip done in float alone would let a splat at the right edge write one
    // pixel past the end of its row: onto the first pixel of the next row, or past the canvas's
    // storage from the last row (which ASan reports). Pixel (0, 1) is far from every splat here,
    // so anything there came through the end of row 0.
    constexpr uint32_t w = (1u << 24) + 4, h = 2;
    REQUIRE(static_cast<uint32_t>(static_cast<float>(w - 1)) == w);
    gg::ui::SplatRenderer splat(w, h);
    const auto x = static_cast<float>(w - 1); // rounds to w, just past the last pixel
    splat.add(x, 0, 4, 1);
    REQUIRE(splat.accumAt(w - 1, 0) > 0.0f); // the splat lands at the right edge ...
    REQUIRE(splat.accumAt(0, 1) == 0.0f);    // ... and not past it

    // The last row's right edge, the end of the canvas's storage.
    splat.clear();
    splat.add(x, 1, 4, 1);
    REQUIRE(splat.accumAt(w - 1, 1) > 0.0f);
    REQUIRE(splat.accumAt(0, 1) == 0.0f);
}

TEST_CASE("resolve saturates monotonically", "[ui]")
{
    const float intensities[] = {0.0f, 0.5f, 1.0f, 4.0f, 100.0f};
    constexpr uint32_t w = 5, h = 1;
    gg::ui::SplatRenderer splat(w, h);
    for (uint32_t x = 0; x < w; ++x)
    {
        // Radius 0.5 reaches only the pixel under the centre, at full weight.
        splat.add(static_cast<float>(x), 0, 0.5f, intensities[x]);
        REQUIRE(splat.accumAt(x, 0) == intensities[x]);
    }

    // Two pixels of row padding, which resolve must leave alone.
    constexpr uint32_t stride = w + 2;
    constexpr uint32_t kPad = 0x12345678u;
    std::vector<uint32_t> out(stride * h, kPad);
    splat.resolveToArgb(out.data(), stride, 0xFFFFFF);
    REQUIRE(out[w] == kPad);
    REQUIRE(out[w + 1] == kPad);

    for (uint32_t x = 0; x < w; ++x)
    {
        INFO("intensity " << intensities[x]);
        const uint32_t p = out[x];
        REQUIRE(channel(p, 24) == 255); // opaque
        // A white tint gives grey: all three channels equal 255 * (1 - exp(-a)), give or take
        // one step of rounding.
        const double expected = 255.0 * (1.0 - std::exp(-static_cast<double>(intensities[x])));
        for (const int shift : {16, 8, 0})
        {
            REQUIRE_THAT(static_cast<double>(channel(p, shift)), WithinAbs(expected, 1.0));
        }
        if (x > 0)
        {
            REQUIRE(channel(p, 16) > channel(out[x - 1], 16));
        }
    }
    REQUIRE(channel(out[0], 16) == 0);       // no light is black
    REQUIRE(channel(out[w - 1], 16) == 255); // saturated

    // The tint scales each channel: saturation reaches the tint itself, never beyond it.
    constexpr uint32_t tint = 0x40C0FF;
    splat.resolveToArgb(out.data(), stride, tint);
    for (uint32_t x = 0; x < w; ++x)
    {
        INFO("intensity " << intensities[x]);
        for (const int shift : {16, 8, 0})
        {
            REQUIRE(channel(out[x], shift) <= channel(tint, shift));
        }
    }
    REQUIRE(out[0] == 0xFF000000u);
    REQUIRE(out[w - 1] == (0xFF000000u | tint));

    // A dense sweep of a from 0 to 30: never decreasing, and within one step of the exact curve.
    constexpr uint32_t n = 3001;
    gg::ui::SplatRenderer ramp(n, 1);
    for (uint32_t x = 0; x < n; ++x)
    {
        ramp.add(static_cast<float>(x), 0, 0.5f, 0.01f * static_cast<float>(x));
    }
    std::vector<uint32_t> sweep(n);
    ramp.resolveToArgb(sweep.data(), n, 0xFFFFFF);
    for (uint32_t x = 0; x < n; ++x)
    {
        const double a = ramp.accumAt(x, 0);
        INFO("a = " << a);
        REQUIRE_THAT(static_cast<double>(channel(sweep[x], 16)),
                     WithinAbs(255.0 * (1.0 - std::exp(-a)), 1.0));
        if (x > 0)
        {
            REQUIRE(channel(sweep[x], 16) >= channel(sweep[x - 1], 16));
        }
    }
}

namespace
{
/// The capsule kernel: the splat's falloff, measured from the nearest point of the segment
/// (x0, cy) - (x1, cy).
float expectedCapsule(float x0, float x1, float cy, float r, float intensity, uint32_t x,
                      uint32_t y)
{
    const float px = static_cast<float>(x);
    const float lo = std::min(x0, x1);
    const float hi = std::max(x0, x1);
    const float nearest = px < lo ? lo : (px > hi ? hi : px);
    const float dx = px - nearest;
    const float dy = static_cast<float>(y) - cy;
    const float d2 = dx * dx + dy * dy;
    return d2 < r * r ? intensity * (1.0f - d2 / (r * r)) : 0.0f;
}
} // namespace

TEST_CASE("a capsule equals a splat when its ends coincide", "[ui]")
{
    constexpr uint32_t w = 32, h = 24;
    struct Case
    {
        float x, y, r, intensity;
    };
    for (const Case &c :
         {Case{10, 10, 3, 0.7f}, Case{20.5f, 5.25f, 2.5f, 1.0f}, Case{0.3f, 23.6f, 4.0f, 0.4f}})
    {
        INFO("splat at " << c.x << ", " << c.y << ", radius " << c.r);
        gg::ui::SplatRenderer splat(w, h), capsule(w, h);
        splat.add(c.x, c.y, c.r, c.intensity);
        capsule.addCapsule(c.x, c.x, c.y, c.r, c.intensity);
        for (uint32_t y = 0; y < h; ++y)
        {
            for (uint32_t x = 0; x < w; ++x)
            {
                INFO("pixel " << x << ", " << y);
                REQUIRE(capsule.accumAt(x, y) == splat.accumAt(x, y)); // bit for bit
            }
        }
    }
}

TEST_CASE("a capsule is flat along its axis", "[ui]")
{
    constexpr uint32_t w = 40, h = 24;
    gg::ui::SplatRenderer capsule(w, h);
    capsule.addCapsule(5, 20, 10, 3, 0.7f);

    // On the axis, between the ends, every pixel gets the splat's centre value exactly.
    for (uint32_t x = 5; x <= 20; ++x)
    {
        INFO("pixel " << x);
        REQUIRE(capsule.accumAt(x, 10) == 0.7f);
    }
    // Off the axis between the ends, only the vertical distance counts.
    REQUIRE_THAT(capsule.accumAt(12, 11), WithinAbs(0.7f * (1.0f - 1.0f / 9.0f), 1e-6));
    // Past an end, it falls off from that end as a splat does.
    REQUIRE_THAT(capsule.accumAt(22, 10), WithinAbs(expectedSplat(20, 10, 3, 0.7f, 22, 10), 1e-6));
    REQUIRE_THAT(capsule.accumAt(3, 11), WithinAbs(expectedSplat(5, 10, 3, 0.7f, 3, 11), 1e-6));
    REQUIRE(capsule.accumAt(23, 10) == 0.0f); // exactly on the radius past the right end
    REQUIRE(capsule.accumAt(2, 10) == 0.0f);

    // Every pixel follows the kernel, and the ends may come in either order.
    gg::ui::SplatRenderer reversed(w, h), between(w, h);
    reversed.addCapsule(20, 5, 10, 3, 0.7f);
    between.addCapsule(7.25f, 30.5f, 12.75f, 2.5f, 0.3f);
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            INFO("pixel " << x << ", " << y);
            const float expected = expectedCapsule(5, 20, 10, 3, 0.7f, x, y);
            if (expected == 0.0f)
            {
                REQUIRE(capsule.accumAt(x, y) == 0.0f);
            }
            else
            {
                REQUIRE_THAT(capsule.accumAt(x, y), WithinAbs(expected, 1e-6));
            }
            REQUIRE(reversed.accumAt(x, y) == capsule.accumAt(x, y));
            REQUIRE_THAT(between.accumAt(x, y),
                         WithinAbs(expectedCapsule(7.25f, 30.5f, 12.75f, 2.5f, 0.3f, x, y), 1e-6));
        }
    }

    // Capsules are additive, with each other and with splats.
    capsule.addCapsule(5, 20, 10, 3, 0.7f);
    capsule.add(12, 10, 3, 0.1f);
    REQUIRE(capsule.accumAt(12, 10) == 2 * 0.7f + 0.1f);
}

TEST_CASE("offscreen capsules are clipped safely", "[ui]")
{
    constexpr uint32_t w = 16, h = 12;
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();
    gg::ui::SplatRenderer capsule(w, h);

    // Partly offscreen past each side: only the in-bounds part lands.
    capsule.addCapsule(-20, -5, -5, 8, 1);
    capsule.addCapsule(w + 3, w + 30, h + 3, 8, 1);
    REQUIRE_THAT(capsule.accumAt(0, 0), WithinAbs(1.0f - 50.0f / 64.0f, 1e-6));
    REQUIRE_THAT(capsule.accumAt(w - 1, h - 1), WithinAbs(1.0f - 32.0f / 64.0f, 1e-6));
    REQUIRE(capsule.accumAt(w / 2, h / 2) == 0.0f);

    // Entirely offscreen, or degenerate: nothing lands, nothing is touched out of bounds.
    std::vector<float> before;
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            before.push_back(capsule.accumAt(x, y));
        }
    }
    capsule.addCapsule(-100, -50, 5, 8, 1);
    capsule.addCapsule(w + 50, w + 100, 5, 8, 1);
    capsule.addCapsule(2, 10, h + 100, 8, 1);
    capsule.addCapsule(1e30f, 2e30f, -1e30f, 4, 1);
    capsule.addCapsule(2, 10, 5, 0, 1);
    capsule.addCapsule(2, 10, 5, -2, 1);
    capsule.addCapsule(kNaN, 10, 5, 3, 1);
    capsule.addCapsule(2, kNaN, 5, 3, 1);
    capsule.addCapsule(-kInf, 10, 5, 3, 1);
    capsule.addCapsule(2, kInf, 5, 3, 1);
    capsule.addCapsule(2, 10, kNaN, 3, 1);
    capsule.addCapsule(2, 10, kInf, 3, 1);
    capsule.addCapsule(2, 10, 5, kNaN, 1);
    capsule.addCapsule(2, 10, 5, kInf, 1);
    capsule.addCapsule(2, 10, 5, 3, kNaN);
    capsule.addCapsule(2, 10, 5, 3, kInf);
    std::size_t i = 0;
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            INFO("pixel " << x << ", " << y);
            REQUIRE(capsule.accumAt(x, y) == before[i++]);
        }
    }

    // A capsule far longer than the canvas lights its whole row at full intensity.
    capsule.clear();
    capsule.addCapsule(-1e30f, 1e30f, 6, 2, 1);
    for (uint32_t x = 0; x < w; ++x)
    {
        INFO("pixel " << x);
        REQUIRE(capsule.accumAt(x, 6) == 1.0f);
        REQUIRE(capsule.accumAt(x, 3) == 0.0f);
    }

    // A zero-sized canvas accepts everything and draws nothing.
    gg::ui::SplatRenderer empty(0, 0);
    empty.addCapsule(0, 4, 0, 4, 1);
    REQUIRE(empty.accumAt(0, 0) == 0.0f);
}

TEST_CASE("a capsule at the right edge of a canvas over 16M pixels wide stays on it", "[ui]")
{
    // As for add: the clip is done in double, so float(width - 1) rounding up to width can't
    // carry a capsule's right end past its row.
    constexpr uint32_t w = (1u << 24) + 4, h = 2;
    gg::ui::SplatRenderer capsule(w, h);
    const auto x = static_cast<float>(w - 1); // rounds to w, just past the last pixel
    capsule.addCapsule(x - 16, x, 1, 4, 1);
    REQUIRE(capsule.accumAt(w - 1, 1) > 0.0f);
    REQUIRE(capsule.accumAt(0, 1) == 0.0f);
    capsule.addCapsule(x - 16, x, 0, 4, 1);
    REQUIRE(capsule.accumAt(0, 1) == 0.0f);
}

TEST_CASE("the banded resolve tints each band", "[ui]")
{
    // M2's four lanes share one accumulator; the resolve tints each lane's rows with its layer's
    // colour (spec §6.3). A 4-row canvas, one band per row, one splat per row: each row's pixel
    // carries its band's tint, as the one-tint resolve would give it.
    constexpr uint32_t w = 8, h = 4, stride = 10;
    constexpr uint32_t kPad = 0xDEADBEEFu;
    gg::ui::SplatRenderer splat(w, h);
    for (uint32_t y = 0; y < h; ++y)
    {
        splat.add(3.0f, static_cast<float>(y), 0.9f, 1.0f); // pixel (3, y) alone
    }
    const std::array<uint32_t, 4> tints{0xE69F00u, 0x56B4E9u, 0x009E73u, 0xCC79A7u};
    const std::array<gg::ui::TintBand, 4> bands{
        {{0, 1, tints[0]}, {1, 2, tints[1]}, {2, 3, tints[2]}, {3, 4, tints[3]}}};
    std::vector<uint32_t> out(stride * h, kPad);
    splat.resolveToArgb(out.data(), stride, bands);
    for (uint32_t y = 0; y < h; ++y)
    {
        INFO("row " << y);
        std::vector<uint32_t> single(stride * h, kPad);
        splat.resolveToArgb(single.data(), stride, tints[y]);
        const uint32_t lit = out[y * stride + 3];
        CHECK(lit == single[y * stride + 3]);
        CHECK(lit != 0xFF000000u);
        // Each channel is its tint's, scaled by 1 - exp(-1).
        constexpr double v = 0.63212055882855767;
        CHECK(std::abs(static_cast<double>(channel(lit, 16)) -
                       v * static_cast<double>((tints[y] >> 16) & 0xFFu)) <= 1.0);
        CHECK(std::abs(static_cast<double>(channel(lit, 8)) -
                       v * static_cast<double>((tints[y] >> 8) & 0xFFu)) <= 1.0);
        CHECK(std::abs(static_cast<double>(channel(lit, 0)) -
                       v * static_cast<double>(tints[y] & 0xFFu)) <= 1.0);
        for (uint32_t x = 0; x < w; ++x)
        {
            if (x != 3)
            {
                CHECK(out[y * stride + x] == 0xFF000000u);
            }
        }
        // The stride's padding is left alone, as the one-tint resolve leaves it.
        CHECK(out[y * stride + 8] == kPad);
        CHECK(out[y * stride + 9] == kPad);
    }

    // A row no band covers resolves black, splat and all; a band past the canvas is clipped.
    const std::array<gg::ui::TintBand, 3> gappy{
        {{0, 2, tints[0]}, {3, 4, tints[3]}, {4, 100, 0xFFFFFFu}}};
    std::vector<uint32_t> gap(stride * h, kPad);
    splat.resolveToArgb(gap.data(), stride, gappy);
    for (uint32_t x = 0; x < w; ++x)
    {
        INFO("pixel " << x);
        CHECK(gap[2 * stride + x] == 0xFF000000u);
    }
    CHECK(gap[0 * stride + 3] == out[0 * stride + 3]);
    CHECK(gap[3 * stride + 3] == out[3 * stride + 3]);
    // Rows 0 and 1 share band 0's tint.
    std::vector<uint32_t> amber(stride * h, kPad);
    splat.resolveToArgb(amber.data(), stride, tints[0]);
    CHECK(gap[1 * stride + 3] == amber[1 * stride + 3]);

    // No bands: every row black. A null destination or a short stride writes nothing.
    std::vector<uint32_t> none(stride * h, kPad);
    splat.resolveToArgb(none.data(), stride, std::span<const gg::ui::TintBand>{});
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            CHECK(none[y * stride + x] == 0xFF000000u);
        }
        CHECK(none[y * stride + 8] == kPad);
    }
    splat.resolveToArgb(nullptr, stride, bands);
    std::vector<uint32_t> untouched(stride * h, kPad);
    splat.resolveToArgb(untouched.data(), w - 1, bands);
    for (const uint32_t px : untouched)
    {
        CHECK(px == kPad);
    }
}

TEST_CASE("one band equals the M1 resolve", "[ui]")
{
    // The one-tint resolve is the banded one's one-band case, bit for bit, over a 64 x 64
    // canvas of random splats and capsules.
    constexpr uint32_t n = 64;
    gg::ui::SplatRenderer splat(n, n);
    uint64_t state = 0x243F6A8885A308D3ull;
    const auto next = [&state] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<float>(static_cast<double>(state >> 11) * 0x1.0p-53);
    };
    for (int i = 0; i < 300; ++i)
    {
        const float x = 80.0f * next() - 8.0f;
        const float y = 80.0f * next() - 8.0f;
        const float r = 0.5f + 6.0f * next();
        const float intensity = 3.0f * next();
        if (i % 2 == 0)
        {
            splat.add(x, y, r, intensity);
        }
        else
        {
            splat.addCapsule(x, x + 20.0f * next(), y, r, intensity);
        }
    }
    for (const uint32_t tint : {0xFFC46Bu, 0xE69F00u, 0xFFFFFFu, 0x000000u})
    {
        INFO("tint " << tint);
        std::vector<uint32_t> m1(n * n, 0u), banded(n * n, 1u);
        splat.resolveToArgb(m1.data(), n, tint);
        const gg::ui::TintBand band{0, n, tint};
        splat.resolveToArgb(banded.data(), n, std::span<const gg::ui::TintBand>(&band, 1));
        CHECK(std::memcmp(m1.data(), banded.data(), m1.size() * sizeof(uint32_t)) == 0);
    }
}
