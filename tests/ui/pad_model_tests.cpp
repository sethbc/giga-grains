// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// PadModel: the XY morph pad's JUCE-free maths (spec §6.3): pixels to Morph X and Y with Y up,
// back again, and the layers' weights by the morph law, which the pad shades its quadrants by.
// They need no editor or JUCE (one compares the law with gg-core's Processor), so they also run
// in the sanitizer builds.

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "gg/core/Rng.h"
#include "gg/ui/PadModel.h"

using Catch::Matchers::WithinAbs;
using gg::core::kLayers;
using gg::ui::PadPoint;

namespace
{
/// A uniform double in [0, 1) from a counter, for the tests' random values (UI tests only).
double unit(uint64_t seed, uint64_t index)
{
    return static_cast<double>(gg::rng::splitmix64(seed * 0x100000001B3ull + index) >> 11) *
           0x1.0p-53;
}
} // namespace

TEST_CASE("the pad maps pixels to Morph X and Y with Y up", "[ui]")
{
    // A 200 px pad: A's corner (X 0, Y 0) bottom-left, D's (X 1, Y 1) top-right (spec §6.3).
    constexpr float side = 200.0f;
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    struct Case
    {
        float px, py, x, y;
        const char *what;
    };
    for (const Case &c : {
             Case{0.0f, 0.0f, 0.0f, 1.0f, "top-left (C)"},
             Case{0.0f, 200.0f, 0.0f, 0.0f, "bottom-left (A)"},
             Case{200.0f, 200.0f, 1.0f, 0.0f, "bottom-right (B)"},
             Case{200.0f, 0.0f, 1.0f, 1.0f, "top-right (D)"},
             Case{100.0f, 100.0f, 0.5f, 0.5f, "the centre"},
             Case{50.0f, 150.0f, 0.25f, 0.25f, "a quarter in from A"},
             Case{-30.0f, 260.0f, 0.0f, 0.0f, "outside, past A"},
             Case{1.0e9f, -1.0e9f, 1.0f, 1.0f, "far outside, past D"},
             Case{kNaN, 50.0f, 0.5f, 0.75f, "a NaN x"},
             Case{150.0f, kNaN, 0.75f, 0.5f, "a NaN y"},
         })
    {
        INFO(c.what);
        const PadPoint v = gg::ui::padValueAt(c.px, c.py, side);
        CHECK_THAT(v.x, WithinAbs(c.x, 1e-6));
        CHECK_THAT(v.y, WithinAbs(c.y, 1e-6));
    }
    // A pad of no size can't place a value: the centre.
    for (const float bad : {0.0f, -10.0f, kNaN})
    {
        const PadPoint v = gg::ui::padValueAt(10.0f, 10.0f, bad);
        CHECK(v.x == 0.5f);
        CHECK(v.y == 0.5f);
    }

    // padPixelOf is the inverse: the handle's pixel for a value, and back.
    const auto a = gg::ui::padPixelOf({0.0f, 0.0f}, side);
    CHECK(a[0] == 0.0f);
    CHECK(a[1] == 200.0f);
    const auto d = gg::ui::padPixelOf({1.0f, 1.0f}, side);
    CHECK(d[0] == 200.0f);
    CHECK(d[1] == 0.0f);
    for (int i = 0; i <= 20; ++i)
    {
        for (int j = 0; j <= 20; ++j)
        {
            const PadPoint v{static_cast<float>(i) / 20.0f, static_cast<float>(j) / 20.0f};
            INFO("value " << v.x << ", " << v.y);
            const auto px = gg::ui::padPixelOf(v, side);
            const PadPoint back = gg::ui::padValueAt(px[0], px[1], side);
            CHECK_THAT(back.x, WithinAbs(v.x, 1e-6));
            CHECK_THAT(back.y, WithinAbs(v.y, 1e-6));
        }
    }
    // Values are clamped to 0..1 first, NaN to the centre.
    const auto clamped = gg::ui::padPixelOf({2.0f, -1.0f}, side);
    CHECK(clamped[0] == 200.0f);
    CHECK(clamped[1] == 200.0f);
    const auto centred = gg::ui::padPixelOf({kNaN, kNaN}, side);
    CHECK(centred[0] == 100.0f);
    CHECK(centred[1] == 100.0f);
}

TEST_CASE("the pad's weights equal the Processor's morph law", "[ui]")
{
    // The pad shades its quadrants by the weights the engine would use (spec §6.3, D1's law,
    // computed in the UI for the picture only): for random pad positions and On switches, after
    // the on/off ramps settle, morphWeights must be the Processor's own morphWeightAt. Each
    // tuple reaches the Processor as parameter events, so Morph X and Y and the On switches ramp
    // as a host's changes do, and is checked once they have settled: four ramps' length
    // (Processor::kRampSeconds is 10 ms). A whole second per tuple, which the ramps don't need,
    // took 30 s in the Debug build.
    using gg::core::Mode;
    using gg::core::ParamSlot;
    gg::core::Processor p({48000.0, 512, 7, Mode::Fx});
    constexpr uint32_t kBlock = 512;
    constexpr uint32_t kSettle = 4 * kBlock; // 43 ms: over four ramps
    STATIC_REQUIRE(kSettle >= 4.0 * gg::core::Processor::kRampSeconds * 48000.0);
    std::vector<float> left(kBlock, 0.0f), right(kBlock, 0.0f);
    std::array<float *, 2> out{left.data(), right.data()};

    for (uint64_t i = 0; i < 200; ++i)
    {
        float x = static_cast<float>(unit(11, i));
        float y = static_cast<float>(unit(12, i));
        // The corners and edges too, where the law clamps the pad.
        if (i < 4)
        {
            x = static_cast<float>(i & 1u);
            y = static_cast<float>(i >> 1);
        }
        else if (i < 8)
        {
            x = i == 4 ? 0.0005f : (i == 5 ? 0.9995f : 0.5f);
            y = i == 6 ? 0.0f : (i == 7 ? 1.0f : 0.5f);
        }
        std::array<bool, kLayers> on{};
        const auto bits = static_cast<uint32_t>(unit(13, i) * 16.0);
        for (uint8_t L = 0; L < kLayers; ++L)
        {
            on[L] = ((bits >> L) & 1u) != 0;
        }
        INFO("tuple " << i << ": x " << x << ", y " << y << ", on " << on[0] << on[1] << on[2]
                      << on[3]);

        std::vector<gg::core::ParamEvent> events{{0, ParamSlot::MorphX, false, x},
                                                 {0, ParamSlot::MorphY, false, y}};
        for (uint8_t L = 0; L < kLayers; ++L)
        {
            events.push_back({0, gg::core::layerOnSlot(L), false, on[L] ? 1.0 : 0.0});
        }
        for (uint32_t done = 0; done < kSettle; done += kBlock)
        {
            p.process(nullptr, 0, out.data(), 2, kBlock,
                      done == 0 ? std::span<const gg::core::ParamEvent>(events)
                                : std::span<const gg::core::ParamEvent>{},
                      {}, {});
        }
        const int64_t t = p.engine().sampleClock();

        const auto weights = gg::ui::morphWeights(x, y, on);
        for (uint8_t L = 0; L < kLayers; ++L)
        {
            INFO("layer " << int{L});
            CHECK_THAT(weights[L], WithinAbs(p.morphWeightAt(L, t), 1e-6));
            if (!on[L])
            {
                CHECK(weights[L] == 0.0f);
            }
        }
    }
}

TEST_CASE("the pad's weights follow D1's law", "[ui]")
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    const std::array<bool, kLayers> all{true, true, true, true};
    // The centre: a quarter each.
    for (const float w : gg::ui::morphWeights(0.5f, 0.5f, all))
    {
        CHECK(w == 0.25f);
    }
    // Three on: a third each, the off one nothing.
    const auto three = gg::ui::morphWeights(0.5f, 0.5f, {true, true, true, false});
    CHECK_THAT(three[0], WithinAbs(1.0 / 3.0, 1e-6));
    CHECK(three[3] == 0.0f);
    // One on: it alone, at full weight, wherever the pad is.
    const auto one = gg::ui::morphWeights(0.9f, 0.1f, {false, false, true, false});
    CHECK(one[2] == 1.0f);
    CHECK(one[0] == 0.0f);
    // None on: all nothing.
    for (const float w : gg::ui::morphWeights(0.3f, 0.7f, {false, false, false, false}))
    {
        CHECK(w == 0.0f);
    }
    // A corner: its layer at 0.999², as the engine clamps the pad to [0.001, 0.999].
    const auto cornerA = gg::ui::morphWeights(0.0f, 0.0f, all);
    CHECK_THAT(cornerA[0], WithinAbs(0.999 * 0.999, 1e-6));
    CHECK_THAT(cornerA[3], WithinAbs(1.0e-6, 1e-9));
    // A NaN coordinate is the centre's.
    const auto nan = gg::ui::morphWeights(kNaN, kNaN, all);
    for (const float w : nan)
    {
        CHECK(w == 0.25f);
    }
}
