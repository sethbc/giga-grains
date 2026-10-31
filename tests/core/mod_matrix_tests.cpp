// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The mod matrix (M2 sub-project 1's spec §6.8): its routes, curves, factory routes and
// evaluation. The factory routes are pinned against M1's macroOffsets, frozen here, bit for bit
// over random macro vectors, so the matrix replaces it without moving a golden.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Rng.h"

using gg::core::applyCurve;
using gg::core::applyRoutes;
using gg::core::factoryRoutes;
using gg::core::isLayerTarget;
using gg::core::isPerLayerSource;
using gg::core::isTimeVarying;
using gg::core::isValidTarget;
using gg::core::kAllLayers;
using gg::core::kLayers;
using gg::core::kMacroSlots;
using gg::core::kModSlots;
using gg::core::kParamCount;
using gg::core::kSameLayer;
using gg::core::LayerParam;
using gg::core::layerSlot;
using gg::core::levelSlot;
using gg::core::ModCurve;
using gg::core::Mode;
using gg::core::ModRoute;
using gg::core::ModRoutes;
using gg::core::ModSource;
using gg::core::ModSourceValues;
using gg::core::ParamSlot;
using gg::core::ParamValues;
using gg::core::routeTargetSlot;

namespace
{
using Sources = std::array<ModSourceValues, kLayers>;

constexpr size_t idx(ParamSlot s) { return static_cast<size_t>(s); }
constexpr size_t idx(ModSource s) { return static_cast<size_t>(s); }

uint32_t bits(float x) { return std::bit_cast<uint32_t>(x); }

ModSource macroSource(size_t m) { return static_cast<ModSource>(idx(ModSource::Macro1) + m); }

// The macros' values in every layer's array, as the Processor will fill the global sources.
Sources macroSources(const std::array<float, 8> &macros)
{
    Sources s{};
    for (auto &layer : s)
    {
        for (size_t m = 0; m < 8; ++m)
        {
            layer.v[idx(macroSource(m))] = macros[m];
        }
    }
    return s;
}

ParamValues macroValues(const std::array<float, 8> &macros)
{
    ParamValues v{};
    for (size_t m = 0; m < 8; ++m)
    {
        v[idx(kMacroSlots[m])] = macros[m];
    }
    return v;
}

ParamValues evaluate(const ModRoutes &routes, const Sources &sources)
{
    ParamValues o{};
    applyRoutes(routes, sources, o);
    return o;
}

// The macros resting: the bipolar ones (1-4, 8) at 0.5, the unipolar ones (5-7) at 0.
constexpr std::array<float, 8> kRest{0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f};

// The 28 M1 slots: everything before layer A's new parameters.
constexpr size_t kM1Slots = idx(ParamSlot::Source);

// M1's macroOffsets (sub-project 5's spec §3.2), frozen here verbatim from gg/core/ParamMap.cpp
// as it was when the matrix replaced it (M2 sub-project 1's spec §6.8): the factory routes must
// keep its values bit for bit.
ParamValues m1MacroOffsets(const ParamValues &effectiveMacros, Mode mode) noexcept
{
    // Every entry starts at +0.0f, and each recipe's term is exactly 0 at rest: 2 × 0.5f − 1 is
    // exactly 0, so are max(0, 0), 0/0.75, smoothstep below its edge and 0 × 0. +0 plus or minus
    // (±0) is +0, so a resting entry stays +0.0f.
    ParamValues o{};
    const auto macro = [&effectiveMacros](ParamSlot slot) {
        return effectiveMacros[static_cast<size_t>(slot)];
    };
    const auto offset = [&o](ParamSlot slot) -> float & { return o[static_cast<size_t>(slot)]; };
    const auto bipolar = [](float v) { return 2.0f * v - 1.0f; };

    const float b1 = bipolar(macro(ParamSlot::Macro1));
    offset(ParamSlot::Size) += 3.0f * b1;
    offset(ParamSlot::Density) -= 1.5f * b1;

    const float b2 = bipolar(macro(ParamSlot::Macro2));
    offset(ParamSlot::Density) += 4.0f * b2;
    offset(ParamSlot::Distribution) += 0.25f * std::max(b2, 0.0f);

    offset(ParamSlot::Pitch) += 24.0f * bipolar(macro(ParamSlot::Macro3));

    const float b4 = bipolar(macro(ParamSlot::Macro4));
    offset(ParamSlot::Spray) += 0.5f * b4;
    offset(ParamSlot::Stereo) += 0.3f * std::max(b4, 0.0f);

    const float u5 = macro(ParamSlot::Macro5);
    offset(ParamSlot::Texture) += std::min(u5 / 0.75f, 1.0f);
    offset(ParamSlot::Diffusion) += gg::core::smoothstep(0.75f, 1.0f, u5);

    offset(ParamSlot::Freeze) += macro(ParamSlot::Macro6);

    // Macro 7 is Macro Feedback in both products (M2 sub-project 1's spec §6.9, §6.10); Feedback
    // acts on a layer that reads its live ring (toWetParams).
    const float v7 = macro(ParamSlot::Macro7);
    offset(ParamSlot::Feedback) += 0.9f * (v7 * v7);

    const float v8 = macro(ParamSlot::Macro8);
    if (mode == Mode::Instrument)
    {
        offset(ParamSlot::Level) += 12.0f * bipolar(v8);
    }
    else
    {
        offset(ParamSlot::Mix) += 0.5f * bipolar(v8);
    }
    return o;
}
} // namespace

TEST_CASE("the factory routes equal M1's macroOffsets bit for bit", "[core][mod]")
{
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        const ModRoutes routes = factoryRoutes(mode);
        const auto check = [&](const std::array<float, 8> &macros) {
            const ParamValues expected = m1MacroOffsets(macroValues(macros), mode);
            const ParamValues got = evaluate(routes, macroSources(macros));
            for (size_t s = 0; s < kM1Slots; ++s)
            {
                if (bits(got[s]) != bits(expected[s]))
                {
                    INFO("slot " << s << " mode " << (mode == Mode::Fx ? "fx" : "instrument")
                                 << ": " << got[s] << " vs " << expected[s]);
                    CHECK(bits(got[s]) == bits(expected[s]));
                    return false;
                }
            }
            // "Every layer": layers B to D get layer A's offsets, Level B to D the Level's in the
            // instrument (and nothing in the FX).
            for (uint8_t layer = 1; layer < kLayers; ++layer)
            {
                for (size_t p = 0; p < 11; ++p)
                {
                    const auto param = static_cast<LayerParam>(p);
                    if (bits(got[idx(layerSlot(param, layer))]) !=
                        bits(got[idx(layerSlot(param, 0))]))
                    {
                        INFO("layer " << int{layer} << " param " << p);
                        CHECK(false);
                        return false;
                    }
                }
                const float level = mode == Mode::Instrument ? got[idx(ParamSlot::Level)] : 0.0f;
                if (bits(got[idx(levelSlot(layer))]) != bits(level))
                {
                    INFO("Level of layer " << int{layer});
                    CHECK(false);
                    return false;
                }
            }
            return true;
        };

        // The edges and the curves' corners, then 10,000 random vectors.
        REQUIRE(check(kRest));
        REQUIRE(check({0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}));
        REQUIRE(check({1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}));
        REQUIRE(check({0.25f, 0.75f, 0.1f, 0.9f, 0.75f, 0.5f, 0.5f, 0.3f}));
        for (uint64_t i = 0; i < 10000; ++i)
        {
            std::array<float, 8> macros{};
            for (size_t m = 0; m < 8; ++m)
            {
                macros[m] = static_cast<float>(gg::rng::uniform01(gg::rng::splitmix64(i * 8 + m)));
            }
            REQUIRE(check(macros));
        }
    }
}

TEST_CASE("every factory route is +0 at rest", "[core][mod]")
{
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        const ParamValues o = evaluate(factoryRoutes(mode), macroSources(kRest));
        for (size_t s = 0; s < kParamCount; ++s)
        {
            INFO("slot " << s);
            CHECK(bits(o[s]) == bits(+0.0f));
        }
    }
}

TEST_CASE("the factory routes are spec 6.8's twelve", "[core][mod]")
{
    struct Row
    {
        ModSource source;
        ParamSlot target;
        float depth;
        ModCurve curve;
    };
    const std::array<Row, 11> shared{{
        {ModSource::Macro1, ParamSlot::Size, 3.0f, ModCurve::Bipolar},
        {ModSource::Macro1, ParamSlot::Density, -1.5f, ModCurve::Bipolar},
        {ModSource::Macro2, ParamSlot::Density, 4.0f, ModCurve::Bipolar},
        {ModSource::Macro2, ParamSlot::Distribution, 0.25f, ModCurve::BipolarPositive},
        {ModSource::Macro3, ParamSlot::Pitch, 24.0f, ModCurve::Bipolar},
        {ModSource::Macro4, ParamSlot::Spray, 0.5f, ModCurve::Bipolar},
        {ModSource::Macro4, ParamSlot::Stereo, 0.3f, ModCurve::BipolarPositive},
        {ModSource::Macro5, ParamSlot::Texture, 1.0f, ModCurve::RampThreeQuarters},
        {ModSource::Macro5, ParamSlot::Diffusion, 1.0f, ModCurve::TopQuarter},
        {ModSource::Macro6, ParamSlot::Freeze, 1.0f, ModCurve::Unipolar},
        {ModSource::Macro7, ParamSlot::Feedback, 0.9f, ModCurve::UnipolarSquared},
    }};
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        const ModRoutes r = factoryRoutes(mode);
        for (size_t i = 0; i < shared.size(); ++i)
        {
            INFO("slot " << i + 1);
            CHECK(r[i] == ModRoute{shared[i].source, kSameLayer, shared[i].target, kAllLayers,
                                   shared[i].depth, shared[i].curve});
        }
        const ModRoute twelfth = mode == Mode::Fx
                                     ? ModRoute{ModSource::Macro8, kSameLayer, ParamSlot::Mix,
                                                kAllLayers,        0.5f,       ModCurve::Bipolar}
                                     : ModRoute{ModSource::Macro8, kSameLayer, ParamSlot::Level,
                                                kAllLayers,        12.0f,      ModCurve::Bipolar};
        CHECK(r[11] == twelfth);
        for (size_t i = 12; i < kModSlots; ++i)
        {
            INFO("slot " << i + 1);
            CHECK(r[i] == ModRoute{});
        }
        for (const ModRoute &route : r)
        {
            CHECK((route.source == ModSource::None || isValidTarget(route.target)));
        }
    }
    CHECK(ModRoute{}.source == ModSource::None);
    CHECK(ModRoute{}.target == ParamSlot::Count);
}

TEST_CASE("the curves are M1's expressions", "[core][mod]")
{
    for (const float v : {0.0f, 0.1f, 0.25f, 0.5f, 0.6f, 0.75f, 0.8f, 0.9f, 1.0f})
    {
        INFO("v " << v);
        CHECK(bits(applyCurve(ModCurve::Unipolar, v)) == bits(v));
        CHECK(bits(applyCurve(ModCurve::Bipolar, v)) == bits(2.0f * v - 1.0f));
        CHECK(bits(applyCurve(ModCurve::BipolarPositive, v)) ==
              bits(std::max(2.0f * v - 1.0f, 0.0f)));
        CHECK(bits(applyCurve(ModCurve::UnipolarSquared, v)) == bits(v * v));
        CHECK(bits(applyCurve(ModCurve::RampThreeQuarters, v)) == bits(std::min(v / 0.75f, 1.0f)));
        CHECK(bits(applyCurve(ModCurve::TopQuarter, v)) ==
              bits(gg::core::smoothstep(0.75f, 1.0f, v)));
    }
    // Each is exactly +0 at its rest: the bipolar ones at 0.5, the unipolar ones at 0.
    CHECK(bits(applyCurve(ModCurve::Bipolar, 0.5f)) == bits(+0.0f));
    CHECK(bits(applyCurve(ModCurve::BipolarPositive, 0.5f)) == bits(+0.0f));
    for (const ModCurve c : {ModCurve::Unipolar, ModCurve::UnipolarSquared,
                             ModCurve::RampThreeQuarters, ModCurve::TopQuarter})
    {
        CHECK(bits(applyCurve(c, 0.0f)) == bits(+0.0f));
    }
    CHECK(bits(applyCurve(ModCurve::Count, 0.7f)) == bits(+0.0f));
}

TEST_CASE("routes add in slot order", "[core][mod]")
{
    // Two routes to B Pitch, from sources at 1.0 and 0.25.
    ModRoutes routes{};
    routes[0] = {ModSource::Macro1, kSameLayer, ParamSlot::Pitch, 1, 1.0f, ModCurve::Bipolar};
    routes[1] = {ModSource::Macro2, kSameLayer, ParamSlot::Pitch, 1, 2.0f, ModCurve::Bipolar};
    Sources s{};
    for (auto &layer : s)
    {
        layer.v[idx(ModSource::Macro1)] = 1.0f;
        layer.v[idx(ModSource::Macro2)] = 0.25f;
    }
    const ParamValues o = evaluate(routes, s);
    const float expected = (1.0f * applyCurve(ModCurve::Bipolar, 1.0f)) +
                           (2.0f * applyCurve(ModCurve::Bipolar, 0.25f));
    CHECK(bits(o[idx(ParamSlot::B_Pitch)]) == bits(expected));
    for (size_t i = 0; i < kParamCount; ++i)
    {
        if (i != idx(ParamSlot::B_Pitch))
        {
            INFO("slot " << i);
            CHECK(bits(o[i]) == bits(+0.0f));
        }
    }

    // Three routes whose sum depends on the order: ((0 + 1) + 1e8) - 1e8 is 0 in float (1e8's
    // spacing is 8), but 1 in the order 2, 3, 1. Slot order gives 0.
    ModRoutes ordered{};
    ordered[0] = {ModSource::Macro1, kSameLayer, ParamSlot::Mix,
                  kAllLayers,        1.0f,       ModCurve::Unipolar};
    ordered[1] = {ModSource::Macro1, kSameLayer, ParamSlot::Mix,
                  kAllLayers,        1e8f,       ModCurve::Unipolar};
    ordered[2] = {ModSource::Macro1, kSameLayer, ParamSlot::Mix,
                  kAllLayers,        -1e8f,      ModCurve::Unipolar};
    CHECK(evaluate(ordered, s)[idx(ParamSlot::Mix)] == 0.0f);
    std::swap(ordered[0], ordered[2]);
    std::swap(ordered[0], ordered[1]);
    CHECK(evaluate(ordered, s)[idx(ParamSlot::Mix)] == 1.0f);

    // An empty slot between routes is skipped.
    ModRoutes gap{};
    gap[0] = routes[0];
    gap[5] = routes[1];
    CHECK(bits(evaluate(gap, s)[idx(ParamSlot::B_Pitch)]) == bits(expected));
}

TEST_CASE("a same-layer source reads the target's layer", "[core][mod]")
{
    Sources s{};
    for (uint8_t layer = 0; layer < kLayers; ++layer)
    {
        s[layer].v[idx(ModSource::Lfo1)] = static_cast<float>(layer);
        // The globals are the same in every layer's array but layer 0's is the one read: make
        // the others differ, to see it.
        s[layer].v[idx(ModSource::ModWheel)] = layer == 0 ? 0.75f : 0.125f;
    }
    const float depth = 0.5f;

    ModRoutes routes{};
    routes[0] = {ModSource::Lfo1, kSameLayer, ParamSlot::Pitch,
                 kAllLayers,      depth,      ModCurve::Unipolar};
    const ParamValues same = evaluate(routes, s);
    for (uint8_t layer = 0; layer < kLayers; ++layer)
    {
        CHECK(bits(same[idx(layerSlot(LayerParam::Pitch, layer))]) ==
              bits(depth * applyCurve(ModCurve::Unipolar, static_cast<float>(layer))));
    }

    // A fixed source layer reads that layer for every target layer.
    routes[0].sourceLayer = 2;
    const ParamValues fixed = evaluate(routes, s);
    for (uint8_t layer = 0; layer < kLayers; ++layer)
    {
        CHECK(fixed[idx(layerSlot(LayerParam::Pitch, layer))] == depth * 2.0f);
    }

    // An out-of-range source layer reads layer 0.
    routes[0].sourceLayer = 7;
    const ParamValues outOfRange = evaluate(routes, s);
    for (uint8_t layer = 0; layer < kLayers; ++layer)
    {
        CHECK(bits(outOfRange[idx(layerSlot(LayerParam::Pitch, layer))]) == bits(+0.0f));
    }

    // A global target reads layer 0 for Same, and the named layer otherwise.
    routes[0] = {ModSource::Lfo1, kSameLayer, ParamSlot::Mix,
                 kAllLayers,      depth,      ModCurve::Unipolar};
    CHECK(bits(evaluate(routes, s)[idx(ParamSlot::Mix)]) == bits(+0.0f));
    routes[0].sourceLayer = 3;
    CHECK(evaluate(routes, s)[idx(ParamSlot::Mix)] == depth * 3.0f);

    // A global source reads layer 0's array, whatever the target's layer or the source layer.
    routes[0] = {ModSource::ModWheel, 2, ParamSlot::Size, kAllLayers, 1.0f, ModCurve::Unipolar};
    const ParamValues global = evaluate(routes, s);
    for (uint8_t layer = 0; layer < kLayers; ++layer)
    {
        CHECK(global[idx(layerSlot(LayerParam::Size, layer))] == 0.75f);
    }
}

TEST_CASE("a route reaches the layers it names", "[core][mod]")
{
    Sources s{};
    for (auto &layer : s)
    {
        layer.v[idx(ModSource::Velocity)] = 1.0f;
    }
    ModRoutes routes{};

    // One layer.
    routes[0] = {ModSource::Velocity, kSameLayer, ParamSlot::Density, 2, 1.0f, ModCurve::Unipolar};
    ParamValues o = evaluate(routes, s);
    CHECK(o[idx(ParamSlot::C_Density)] == 1.0f);
    CHECK(o[idx(ParamSlot::Density)] == 0.0f);
    CHECK(o[idx(ParamSlot::B_Density)] == 0.0f);
    CHECK(o[idx(ParamSlot::D_Density)] == 0.0f);

    // Out of range: nothing.
    routes[0].targetLayer = 4;
    o = evaluate(routes, s);
    for (size_t i = 0; i < kParamCount; ++i)
    {
        CHECK(bits(o[i]) == bits(+0.0f));
    }

    // Level on every layer: the four Levels; Layer A On given layer 2: Layer C On.
    routes[0] = {ModSource::Velocity, kSameLayer, ParamSlot::Level,
                 kAllLayers,          6.0f,       ModCurve::Unipolar};
    routes[1] = {ModSource::Velocity, kSameLayer, ParamSlot::LayerAOn, 2, 1.0f, ModCurve::Unipolar};
    o = evaluate(routes, s);
    for (const ParamSlot slot :
         {ParamSlot::Level, ParamSlot::LevelB, ParamSlot::LevelC, ParamSlot::LevelD})
    {
        CHECK(o[idx(slot)] == 6.0f);
    }
    CHECK(o[idx(ParamSlot::LayerCOn)] == 1.0f);
    CHECK(o[idx(ParamSlot::LayerAOn)] == 0.0f);

    // A global target ignores the target layer; an invalid target or an empty source does
    // nothing.
    routes = {};
    routes[0] = {ModSource::Velocity, kSameLayer, ParamSlot::ReverbDecay, 3, 1.0f,
                 ModCurve::Unipolar};
    routes[1] = {ModSource::Velocity, kSameLayer, ParamSlot::B_Size,
                 kAllLayers,          1.0f,       ModCurve::Unipolar};
    routes[2] = {ModSource::Velocity, kSameLayer, ParamSlot::Macro1,
                 kAllLayers,          1.0f,       ModCurve::Unipolar};
    routes[3] = {ModSource::Velocity, kSameLayer, ParamSlot::Count,
                 kAllLayers,          1.0f,       ModCurve::Unipolar};
    routes[4] = {ModSource::None, kSameLayer, ParamSlot::Size,
                 kAllLayers,      1.0f,       ModCurve::Unipolar};
    routes[5] = {ModSource::Count, kSameLayer, ParamSlot::Size,
                 kAllLayers,       1.0f,       ModCurve::Unipolar};
    o = evaluate(routes, s);
    for (size_t i = 0; i < kParamCount; ++i)
    {
        INFO("slot " << i);
        CHECK(o[i] == (i == idx(ParamSlot::ReverbDecay) ? 1.0f : 0.0f));
    }
}

TEST_CASE("invalid targets are refused", "[core][mod]")
{
    CHECK_FALSE(isValidTarget(ParamSlot::Macro1));
    CHECK_FALSE(isValidTarget(ParamSlot::B_Position));
    CHECK_FALSE(isValidTarget(ParamSlot::B_Lfo1Rate));
    CHECK_FALSE(isValidTarget(ParamSlot::Lfo1Rate));
    CHECK(isValidTarget(ParamSlot::Level));
    CHECK(isValidTarget(ParamSlot::LayerAOn));
    CHECK(isValidTarget(ParamSlot::Mix));
    CHECK(isValidTarget(ParamSlot::Position));

    CHECK_FALSE(isValidTarget(ParamSlot::Count));
    CHECK_FALSE(isValidTarget(ParamSlot::Step16));
    CHECK_FALSE(isValidTarget(ParamSlot::D_Step16));
    CHECK_FALSE(isValidTarget(ParamSlot::D_Diffusion));
    CHECK_FALSE(isValidTarget(ParamSlot::LevelB));
    CHECK_FALSE(isValidTarget(ParamSlot::LayerDOn));
    CHECK(isValidTarget(ParamSlot::Source));
    CHECK(isValidTarget(ParamSlot::PitchSpread));
    CHECK(isValidTarget(ParamSlot::SizeSpread));
    CHECK(isValidTarget(ParamSlot::ReverbDecay));
    CHECK(isValidTarget(ParamSlot::MorphX));
    CHECK(isValidTarget(ParamSlot::MorphY));

    // Every slot: the macros, the modulators' controls, Ring Length (neither automatable nor
    // modulatable) and layers B to D's slots are refused.
    size_t valid = 0;
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto slot = static_cast<ParamSlot>(i);
        const auto ref = gg::core::layerOf(slot);
        const bool control =
            ref && ((ref->param >= LayerParam::Lfo1Rate && ref->param <= LayerParam::Step16) ||
                    ref->param == LayerParam::RingLength);
        const bool expected = !gg::core::isMacro(slot) && !control &&
                              (ref ? ref->layer == 0
                                   : !gg::core::perLayerGlobalLayer(slot) ||
                                         slot == ParamSlot::Level || slot == ParamSlot::LayerAOn);
        INFO("slot " << i);
        CHECK(isValidTarget(slot) == expected);
        valid += isValidTarget(slot) ? 1 : 0;
    }
    // 14 layer parameters, four of M2 sub-project 2's five ring parameters (not Ring Length) and
    // sub-project 3's Quality, Level and Layer A On, Mix, Space's seven but Diffusion, Morph X
    // and Y, and Budget.
    CHECK(valid == 14 + 4 + 1 + 2 + 1 + 7 + 2 + 1);
    CHECK_FALSE(isValidTarget(ParamSlot::B_LoopLength));

    CHECK(isLayerTarget(ParamSlot::Position));
    CHECK(isLayerTarget(ParamSlot::SizeSpread));
    CHECK(isLayerTarget(ParamSlot::Level));
    CHECK(isLayerTarget(ParamSlot::LayerAOn));
    CHECK_FALSE(isLayerTarget(ParamSlot::Mix));
    CHECK_FALSE(isLayerTarget(ParamSlot::MorphX));
    CHECK_FALSE(isLayerTarget(ParamSlot::B_Position));
    CHECK_FALSE(isLayerTarget(ParamSlot::LevelB));

    CHECK(routeTargetSlot(ParamSlot::Position, 0) == ParamSlot::Position);
    CHECK(routeTargetSlot(ParamSlot::Position, 3) == ParamSlot::D_Position);
    CHECK(routeTargetSlot(ParamSlot::PitchSpread, 1) == ParamSlot::B_PitchSpread);
    CHECK(routeTargetSlot(ParamSlot::Level, 0) == ParamSlot::Level);
    CHECK(routeTargetSlot(ParamSlot::Level, 2) == ParamSlot::LevelC);
    CHECK(routeTargetSlot(ParamSlot::LayerAOn, 1) == ParamSlot::LayerBOn);
    CHECK(routeTargetSlot(ParamSlot::Mix, 2) == ParamSlot::Mix);
}

TEST_CASE("a route to Quality is accepted and a route to LFO 1 Rate refused", "[core][mod]")
{
    // The programme manager's ruling PM-1 (M2 sub-project 3's spec §6.1): the layer rule refuses
    // the modulators' own controls by name (LFO, Env, Random and Step, rows 0x0F to 0x30) and
    // admits every other layer parameter, wave 2's included; Budget is a global target.
    using gg::core::isModulatorControl;
    STATIC_REQUIRE(isValidTarget(ParamSlot::Quality));
    STATIC_REQUIRE(isValidTarget(ParamSlot::Budget));
    STATIC_REQUIRE_FALSE(isValidTarget(ParamSlot::Lfo1Rate));
    STATIC_REQUIRE_FALSE(isValidTarget(ParamSlot::B_Quality));
    STATIC_REQUIRE(isModulatorControl(LayerParam::Lfo1Rate));
    STATIC_REQUIRE(isModulatorControl(LayerParam::Step16));
    STATIC_REQUIRE_FALSE(isModulatorControl(LayerParam::SizeSpread));
    STATIC_REQUIRE_FALSE(isModulatorControl(LayerParam::Quality));

    // Every layer-A row: 0x0F to 0x30 refused, and Ring Length (0x40, neither automatable nor
    // modulatable; M2 sub-project 2's Task 12 ruling), every other row a target.
    size_t controls = 0;
    for (size_t p = 0; p < gg::core::kLayerParamCount; ++p)
    {
        const auto param = static_cast<LayerParam>(p);
        const uint32_t index = gg::core::layerParamIndex(param);
        const bool control = index >= 0x0Fu && index <= 0x30u;
        INFO("index " << index);
        CHECK(isModulatorControl(param) == control);
        CHECK(isValidTarget(layerSlot(param, 0)) == (!control && param != LayerParam::RingLength));
        for (uint8_t layer = 1; layer < kLayers; ++layer)
        {
            CHECK_FALSE(isValidTarget(layerSlot(param, layer)));
        }
        controls += control ? 1 : 0;
    }
    CHECK(controls == 0x30 - 0x0F + 1);

    // A route to Quality on every layer moves all four Quality slots, and nothing else.
    Sources s{};
    for (auto &layer : s)
    {
        layer.v[idx(ModSource::Velocity)] = 1.0f;
    }
    ModRoutes routes{};
    routes[0] = {ModSource::Velocity, kSameLayer, ParamSlot::Quality,
                 kAllLayers,          1.0f,       ModCurve::Unipolar};
    routes[1] = {ModSource::Velocity, kSameLayer, ParamSlot::Lfo1Rate,
                 kAllLayers,          1.0f,       ModCurve::Unipolar};
    routes[2] = {ModSource::Velocity, kSameLayer, ParamSlot::Budget, 0, -512.0f,
                 ModCurve::Unipolar};
    const ParamValues o = evaluate(routes, s);
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto slot = static_cast<ParamSlot>(i);
        INFO("slot " << i);
        const bool quality = slot == ParamSlot::Quality || slot == ParamSlot::B_Quality ||
                             slot == ParamSlot::C_Quality || slot == ParamSlot::D_Quality;
        CHECK(o[i] == (quality ? 1.0f : slot == ParamSlot::Budget ? -512.0f : 0.0f));
    }
}

TEST_CASE("source kinds", "[core][mod]")
{
    for (size_t i = 0; i < idx(ModSource::Count); ++i)
    {
        const auto s = static_cast<ModSource>(i);
        const bool perLayer = s >= ModSource::Lfo1 && s <= ModSource::Step;
        INFO(gg::core::modSourceName(s));
        CHECK(isPerLayerSource(s) == perLayer);
        CHECK(isTimeVarying(s) == (perLayer || s == ModSource::EnvFollower ||
                                   s == ModSource::TransportBeat || s == ModSource::TransportBar));
    }
}

TEST_CASE("source and curve names round-trip", "[core][mod]")
{
    const std::array<std::string_view, idx(ModSource::Count)> sourceNames{
        "None",    "Macro 1",      "Macro 2",        "Macro 3",       "Macro 4",
        "Macro 5", "Macro 6",      "Macro 7",        "Macro 8",       "LFO 1",
        "LFO 2",   "Env 1",        "Env 2",          "Random 1",      "Random 2",
        "Step",    "Mod Wheel",    "Aftertouch",     "Pitch Bend",    "Velocity",
        "Key",     "Env Follower", "Transport Beat", "Transport Bar", "Transport Playing",
    };
    for (size_t i = 0; i < sourceNames.size(); ++i)
    {
        const auto s = static_cast<ModSource>(i);
        CHECK(gg::core::modSourceName(s) == sourceNames[i]);
        CHECK(gg::core::modSourceNamed(sourceNames[i]) == s);
    }
    const std::array<std::string_view, static_cast<size_t>(ModCurve::Count)> curveNames{
        "Unipolar",        "Bipolar",           "BipolarPositive",
        "UnipolarSquared", "RampThreeQuarters", "TopQuarter",
    };
    for (size_t i = 0; i < curveNames.size(); ++i)
    {
        const auto c = static_cast<ModCurve>(i);
        CHECK(gg::core::modCurveName(c) == curveNames[i]);
        CHECK(gg::core::modCurveNamed(curveNames[i]) == c);
    }
    // Exact and case-sensitive.
    for (const std::string_view bad :
         {"", "none", "macro 1", "Macro1", "Macro 9", "LFO1", "Lfo 1", " Step", "Step "})
    {
        INFO(std::string{bad});
        CHECK_FALSE(gg::core::modSourceNamed(bad));
    }
    for (const std::string_view bad : {"", "unipolar", "Bipolar ", "TopQuarter2"})
    {
        INFO(std::string{bad});
        CHECK_FALSE(gg::core::modCurveNamed(bad));
    }
    CHECK(gg::core::modSourceName(ModSource::Count) == std::string_view{});
    CHECK(gg::core::modCurveName(ModCurve::Count) == std::string_view{});
}

TEST_CASE("wave-2 layer targets are admitted", "[core][mod]")
{
    // M2 sub-project 2's spec §6.3: Loop, Loop Length, Overdub and Splice are targets under
    // sub-project 3's rule (any layer parameter but a modulator's control). Ring Length is not:
    // it is neither automatable nor modulatable (its flags, which patch_tests pin), applied from
    // its base value on the main thread, so a route to it would do nothing (the controller's ruling
    // for Task 12).
    CHECK(isValidTarget(ParamSlot::Loop));
    CHECK(isValidTarget(ParamSlot::LoopLength));
    CHECK(isValidTarget(ParamSlot::Overdub));
    CHECK(isValidTarget(ParamSlot::Splice));
    CHECK_FALSE(isValidTarget(ParamSlot::RingLength));
    CHECK_FALSE(isValidTarget(ParamSlot::B_RingLength)); // and B to D's slots, as every kind's
}
