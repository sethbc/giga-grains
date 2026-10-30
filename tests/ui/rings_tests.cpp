// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Rings: the routes that target a control, as the Edit view draws them (M2 sub-project 6's
// spec §6.5). Framework-free, so these also run in the sanitizer builds.

#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <sst/basic-blocks/params/ParamMetadata.h>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/ui/Rings.h"

using gg::core::ModCurve;
using gg::core::ModRoute;
using gg::core::ModRoutes;
using gg::core::ModSource;
using gg::core::ParamSlot;

namespace
{
std::vector<gg::ui::RingSpec> sizeRings(const ModRoutes &routes, uint8_t layer)
{
    const auto &meta = gg::core::paramMeta(ParamSlot::Size, gg::core::Mode::Fx);
    return gg::ui::ringsFor(routes, ParamSlot::Size, layer, meta.minVal, meta.maxVal);
}
} // namespace

TEST_CASE("rings follow the routes that target the control", "[ui][mod]")
{
    const auto &meta = gg::core::paramMeta(ParamSlot::Size, gg::core::Mode::Fx);
    const float range = meta.maxVal - meta.minVal;

    // The factory routes: Macro 1 reaches Size on every layer, bipolar, 3 octaves deep.
    ModRoutes routes = gg::core::factoryRoutes(gg::core::Mode::Fx);
    const auto rings = sizeRings(routes, 1);
    REQUIRE(rings.size() == 1);
    CHECK(rings[0].slot == 1);
    CHECK(rings[0].depth01 == Catch::Approx(3.0f / range));
    CHECK(rings[0].bipolar);
    CHECK(rings[0].family == 0); // macros

    // A target without a layer takes every route to it, and Density has two, in slot order.
    const auto &density = gg::core::paramMeta(ParamSlot::Density, gg::core::Mode::Fx);
    const auto dr = gg::ui::ringsFor(routes, ParamSlot::Density, 2, density.minVal, density.maxVal);
    REQUIRE(dr.size() == 2);
    CHECK(dr[0].slot == 2);
    CHECK(dr[1].slot == 3);
    CHECK(dr[0].depth01 < 0.0f); // -1.5 lowers

    // A route to layer C alone gives layer B none, and layer C one.
    routes = ModRoutes{};
    routes[5] = ModRoute{ModSource::Lfo1,   gg::core::kSameLayer, ParamSlot::Size, 2, 1.0f,
                         ModCurve::Unipolar};
    CHECK(sizeRings(routes, 1).empty());
    const auto c = sizeRings(routes, 2);
    REQUIRE(c.size() == 1);
    CHECK(c[0].slot == 6);
    CHECK_FALSE(c[0].bipolar); // Unipolar swings one way
    CHECK(c[0].family == 1);   // modulators

    // None gives none; a global target ignores the layer; an invalid range gives none.
    routes[5].source = ModSource::None;
    CHECK(sizeRings(routes, 2).empty());
    routes[6] = ModRoute{ModSource::EnvFollower,
                         gg::core::kSameLayer,
                         ParamSlot::ReverbSize,
                         gg::core::kAllLayers,
                         0.5f,
                         ModCurve::Bipolar};
    CHECK(gg::ui::ringsFor(routes, ParamSlot::ReverbSize, 3, 0.0f, 1.0f).size() == 1);
    CHECK(gg::ui::ringsFor(routes, ParamSlot::ReverbSize, 3, 1.0f, 1.0f).empty());
}

TEST_CASE("rings colour by the source's family", "[ui][mod]")
{
    const auto family = [](ModSource source) {
        ModRoutes routes{};
        routes[0] = ModRoute{source, gg::core::kSameLayer, ParamSlot::Size, gg::core::kAllLayers,
                             1.0f,   ModCurve::Bipolar};
        const auto rings = sizeRings(routes, 0);
        REQUIRE(rings.size() == 1);
        return rings[0].family;
    };
    CHECK(family(ModSource::Macro8) == 0);
    CHECK(family(ModSource::Step) == 1);
    CHECK(family(ModSource::Key) == 2);
    CHECK(family(ModSource::ModWheel) == 2);
    CHECK(family(ModSource::EnvFollower) == 3);
    CHECK(family(ModSource::TransportBar) == 4);
    CHECK(family(ModSource::TransportPlaying) == 4);
}
