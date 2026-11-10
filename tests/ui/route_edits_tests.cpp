// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The Mod view's route edits (M2 sub-project 6's spec §6.6): framework-free, so they run
// everywhere.

#include <cmath>

#include <span>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <sst/basic-blocks/params/ParamMetadata.h>

#include "core/LayersFixture.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "gg/ui/RouteEdits.h"

using gg::core::LayerParam;
using gg::core::layerSlot;
using gg::core::ModCurve;
using gg::core::Mode;
using gg::core::ModRoutes;
using gg::core::ModSource;
using gg::core::NoteEvent;
using gg::core::NoteEventType;
using gg::core::ParamSlot;
using gg::core::Processor;
using gg::core::ProcessorConfig;
using gg::test::layers::Log;

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
    // The factory route Macro 1 to Size reaches every layer: a drop of Macro 1 on layer B's Size
    // selects it instead of doubling it.
    const auto factoryHit =
        gg::ui::addRouteForDrop(routes, ModSource::Macro1, ParamSlot::Size, 1, meta);
    CHECK_FALSE(factoryHit.added);
    CHECK(factoryHit.slot == 1);
    CHECK(factoryHit.notice.empty());
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

TEST_CASE("a drop on a Voice-only factory route makes it apply everywhere", "[ui][mod][voice]")
{
    // M3 Voice: slots 44 to 48 hold Voice-only routes, which a Cloud layer never applies. A drop
    // of the same source and target clears the route's flag (the controller's ruling): one route,
    // applied on Cloud and Voice layers alike, never doubled. A drop on a normal route still
    // changes nothing.
    const auto mode = Mode::Instrument;
    const ModRoutes factory = gg::core::factoryRoutes(mode);
    REQUIRE(factory[46].voiceOnly);
    REQUIRE(factory[46].source == ModSource::Velocity);
    REQUIRE(factory[46].target == ParamSlot::Level);
    const auto &level = gg::core::paramMeta(ParamSlot::Level, mode);
    ModRoutes routes = factory;
    const auto result =
        gg::ui::addRouteForDrop(routes, ModSource::Velocity, ParamSlot::Level, 0, level);
    CHECK(result.added);
    CHECK(result.slot == 47);
    CHECK(result.notice.empty());
    CHECK_FALSE(routes[46].voiceOnly);
    ModRoutes expected = factory;
    expected[46].voiceOnly = false;
    CHECK(routes == expected); // the one route, nothing added
    // Again: a normal route now, so nothing changes.
    const auto again =
        gg::ui::addRouteForDrop(routes, ModSource::Velocity, ParamSlot::Level, 0, level);
    CHECK_FALSE(again.added);
    CHECK(again.slot == 47);
    CHECK(routes == expected);
    // The others too, each in its own slot: Velocity to Size, Pressure to Density and Level,
    // Timbre to Position.
    const struct
    {
        ModSource source;
        ParamSlot target;
        uint8_t slot;
    } others[] = {{ModSource::Velocity, ParamSlot::Size, 48},
                  {ModSource::Pressure, ParamSlot::Density, 44},
                  {ModSource::Pressure, ParamSlot::Level, 45},
                  {ModSource::Timbre, ParamSlot::Position, 46}};
    for (const auto &o : others)
    {
        const auto r = gg::ui::addRouteForDrop(routes, o.source, o.target, 0,
                                               gg::core::paramMeta(o.target, mode));
        CHECK(r.added);
        CHECK(r.slot == o.slot);
        CHECK_FALSE(routes[o.slot - 1u].voiceOnly);
    }
    for (size_t i = 0; i < 43; ++i)
    {
        CHECK(routes[i] == factory[i]);
    }

    // What the Processor makes of it, after one note-on at velocity 1 (Bipolar: +6 dB of Level).
    const auto play = [&](const ModRoutes &r, bool voice, Log &log) {
        Processor p(ProcessorConfig{48000.0, 4096, 7, mode});
        p.setSourceNow(0, gg::test::layers::sine(220.0));
        p.setBase(layerSlot(LayerParam::Spray, 0), 0.0f);
        p.setBase(layerSlot(LayerParam::Distribution, 0), 0.0f);
        p.setBase(layerSlot(LayerParam::Density, 0), 6.0f);
        p.setBase(layerSlot(LayerParam::Mode, 0), voice ? 1.0f : 0.0f);
        p.setRoutes(r);
        p.setEventSink(&Log::record, &log);
        std::vector<float> left(256), right(256);
        float *out[2] = {left.data(), right.data()};
        const NoteEvent on{0, NoteEventType::On, 0, 60, 1.0f};
        for (int k = 0; k < 40; ++k)
        {
            p.process(nullptr, 0, out, 2, 256, {},
                      k == 0 ? std::span(&on, 1) : std::span<const NoteEvent>{});
            ++log.call;
        }
        return p.effective(ParamSlot::Level);
    };
    ModRoutes dropped = factory;
    REQUIRE(
        gg::ui::addRouteForDrop(dropped, ModSource::Velocity, ParamSlot::Level, 0, level).added);
    ModRoutes none = factory;
    none[46] = gg::core::ModRoute{};
    SECTION("A Cloud layer now gets the modulation")
    {
        Log a, b, c;
        CHECK(play(factory, false, a) == 0.0f); // Voice only: not on a Cloud layer
        CHECK(play(dropped, false, b) == 6.0f);
        CHECK(play(none, false, c) == 0.0f);
    }
    SECTION("A Voice layer's voices count it once")
    {
        Log withFlag, withoutFlag, without;
        CHECK(play(factory, true, withFlag) == 0.0f);
        CHECK(play(dropped, true, withoutFlag) == 0.0f); // per voice, not the layer's Level
        play(none, true, without);
        const auto a = withFlag.layer(0);
        const auto b = withoutFlag.layer(0);
        const auto c = without.layer(0);
        REQUIRE(a.size() > 10);
        REQUIRE(b.size() == a.size());
        REQUIRE(c.size() == a.size());
        const double once = std::exp2(6.0 * std::log2(10.0) / 20.0); // +6 dB, once
        for (size_t i = 0; i < a.size(); ++i)
        {
            CHECK(b[i].gain == a[i].gain); // the same route as Voice only: the same voices
            if (c[i].gain > 0.0f)
            {
                CHECK(static_cast<double>(b[i].gain) / c[i].gain ==
                      Catch::Approx(once).epsilon(1e-5));
            }
        }
    }
}
