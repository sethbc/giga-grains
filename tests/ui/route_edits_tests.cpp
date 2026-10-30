// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The Mod view's route edits (M2 sub-project 6's spec §6.6): framework-free, so they run
// everywhere.

#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <sst/basic-blocks/params/ParamMetadata.h>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/ui/RouteEdits.h"

using gg::core::ModCurve;
using gg::core::ModRoutes;
using gg::core::ModSource;
using gg::core::ParamSlot;

namespace
{
const auto &metaOf(ParamSlot slot) { return gg::core::paramMeta(slot, gg::core::Mode::Fx); }
} // namespace

TEST_CASE("a drop takes the first empty slot with a quarter depth", "[ui]")
{
    ModRoutes routes = gg::core::factoryRoutes(gg::core::Mode::Fx);
    const auto &meta = metaOf(ParamSlot::Size);
    const auto result = gg::ui::addRouteForDrop(routes, ModSource::Lfo1, ParamSlot::Size, 1, meta);
    CHECK(result.added);
    CHECK(result.slot == 13);
    CHECK(result.notice.empty());
    const gg::core::ModRoute &route = routes[12];
    CHECK(route.source == ModSource::Lfo1);
    CHECK(route.sourceLayer == gg::core::kSameLayer);
    CHECK(route.target == ParamSlot::Size);
    CHECK(route.targetLayer == 1);
    CHECK(route.depth == 0.25f * (meta.maxVal - meta.minVal));
    CHECK(route.curve == ModCurve::Bipolar);

    // A stepped or on/off target takes Unipolar.
    const auto toggled = gg::ui::addRouteForDrop(routes, ModSource::Env1, ParamSlot::Freeze, 0,
                                                 metaOf(ParamSlot::Freeze));
    CHECK(toggled.added);
    CHECK(toggled.slot == 14);
    CHECK(routes[13].curve == ModCurve::Unipolar);
    const auto stepped = gg::ui::addRouteForDrop(routes, ModSource::Env1, ParamSlot::Source, 0,
                                                 metaOf(ParamSlot::Source));
    CHECK(stepped.added);
    CHECK(routes[14].curve == ModCurve::Unipolar);
}

TEST_CASE("a drop on a full matrix is refused", "[ui]")
{
    ModRoutes routes = gg::core::factoryRoutes(gg::core::Mode::Fx);
    for (std::size_t i = 0; i < routes.size(); ++i)
    {
        if (routes[i].source == ModSource::None)
        {
            routes[i].source = ModSource::Lfo2;
            routes[i].target = ParamSlot::Pitch;
            routes[i].targetLayer = 0;
            routes[i].depth = 1.0f;
        }
    }
    const ModRoutes before = routes;
    const auto result = gg::ui::addRouteForDrop(routes, ModSource::Env2, ParamSlot::Size, 0,
                                                metaOf(ParamSlot::Size));
    CHECK_FALSE(result.added);
    CHECK(result.slot == 0);
    CHECK(result.notice == "the matrix is full: 48 routes");
    CHECK(routes == before);
}

TEST_CASE("a second drop of the same route selects it", "[ui]")
{
    ModRoutes routes = gg::core::factoryRoutes(gg::core::Mode::Fx);
    const auto &meta = metaOf(ParamSlot::Size);
    const auto first = gg::ui::addRouteForDrop(routes, ModSource::Lfo1, ParamSlot::Size, 1, meta);
    REQUIRE(first.added);
    const ModRoutes after = routes;
    const auto again = gg::ui::addRouteForDrop(routes, ModSource::Lfo1, ParamSlot::Size, 1, meta);
    CHECK_FALSE(again.added);
    CHECK(again.slot == first.slot);
    CHECK(again.notice.empty());
    CHECK(routes == after);
    // Another layer, or another source, is a new route.
    CHECK(gg::ui::addRouteForDrop(routes, ModSource::Lfo1, ParamSlot::Size, 2, meta).added);
    CHECK(gg::ui::addRouteForDrop(routes, ModSource::Lfo2, ParamSlot::Size, 1, meta).added);
    // The factory route Macro 1 to Size on every layer: a drop of Macro 1 on layer B's Size is
    // a different route (its layer is B alone).
    CHECK(gg::ui::addRouteForDrop(routes, ModSource::Macro1, ParamSlot::Size, 1, meta).added);
}

TEST_CASE("a macro target is refused", "[ui]")
{
    ModRoutes routes = gg::core::factoryRoutes(gg::core::Mode::Fx);
    const ModRoutes before = routes;
    const auto macro = gg::ui::addRouteForDrop(routes, ModSource::Lfo1, ParamSlot::Macro1, 0,
                                               metaOf(ParamSlot::Macro1));
    CHECK_FALSE(macro.added);
    CHECK(macro.slot == 0);
    CHECK(macro.notice == "a macro can't be a target");
    // A modulator's own control is no target either, nor a layer B slot (a route names layer A's).
    const auto step = gg::ui::addRouteForDrop(routes, ModSource::Lfo1, ParamSlot::Step1, 0,
                                              metaOf(ParamSlot::Step1));
    CHECK_FALSE(step.added);
    CHECK_FALSE(step.notice.empty());
    const auto layerB = gg::ui::addRouteForDrop(routes, ModSource::Lfo1, ParamSlot::B_Size, 1,
                                                metaOf(ParamSlot::B_Size));
    CHECK_FALSE(layerB.added);
    CHECK_FALSE(layerB.notice.empty());
    // No source: nothing to route.
    CHECK_FALSE(gg::ui::addRouteForDrop(routes, ModSource::None, ParamSlot::Size, 0,
                                        metaOf(ParamSlot::Size))
                    .added);
    CHECK(routes == before);
}

TEST_CASE("setDepth and clearSlot", "[ui]")
{
    ModRoutes routes = gg::core::factoryRoutes(gg::core::Mode::Fx);
    const ModRoutes factory = routes;
    gg::ui::setDepth(routes, 1, 0.5f);
    CHECK(routes[0].depth == 0.5f);
    CHECK(routes[0].source == factory[0].source);
    CHECK(routes[1] == factory[1]);

    // Out of range, and not finite: nothing.
    const ModRoutes set = routes;
    gg::ui::setDepth(routes, 0, 3.0f);
    gg::ui::setDepth(routes, 49, 3.0f);
    gg::ui::setDepth(routes, 2, std::nanf(""));
    CHECK(routes == set);

    gg::ui::clearSlot(routes, 1);
    CHECK(routes[0] == gg::core::ModRoute{});
    CHECK(routes[1] == factory[1]);
    gg::ui::clearSlot(routes, 0);
    gg::ui::clearSlot(routes, 49);
    CHECK(routes[1] == factory[1]);

    // The slot a clear frees is the next drop's.
    CHECK(gg::ui::addRouteForDrop(routes, ModSource::Lfo1, ParamSlot::Size, 0,
                                  metaOf(ParamSlot::Size))
              .slot == 1);
}

TEST_CASE("a route target is layer A's slot for a layer parameter", "[ui]")
{
    CHECK(gg::ui::routeTargetOf(ParamSlot::B_Size) == std::pair{ParamSlot::Size, uint8_t{1}});
    CHECK(gg::ui::routeTargetOf(ParamSlot::Size) == std::pair{ParamSlot::Size, uint8_t{0}});
    CHECK(gg::ui::routeTargetOf(gg::core::levelSlot(2)) == std::pair{ParamSlot::Level, uint8_t{2}});
    CHECK(gg::ui::routeTargetOf(gg::core::layerOnSlot(3)) ==
          std::pair{ParamSlot::LayerAOn, uint8_t{3}});
    CHECK(gg::ui::routeTargetOf(ParamSlot::Mix) == std::pair{ParamSlot::Mix, uint8_t{0}});
}
