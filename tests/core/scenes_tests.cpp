// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// M3 sub-project 10 (Scenes): the seven scene controls' exclusions and the scene-slot table
// (its spec §4.1, §4.2).

#include <cstddef>

#include <catch2/catch_test_macros.hpp>
#include <clap/ext/params.h>

#include "gg/core/ModMatrix.h"
#include "gg/core/Mutate.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

using gg::core::isMidiSetting;
using gg::core::isSceneControl;
using gg::core::isSceneSlot;
using gg::core::isValidTarget;
using gg::core::kParamCount;
using gg::core::LayerParam;
using gg::core::Mode;
using gg::core::ParamSlot;

namespace
{
constexpr ParamSlot kSceneControls[] = {
    ParamSlot::Scene,         ParamSlot::SceneB,     ParamSlot::SceneMorph, ParamSlot::ScenePunch,
    ParamSlot::SceneQuantize, ParamSlot::SceneGlide, ParamSlot::SceneStore};
} // namespace

// The spec's §4.2 table: a scene holds every parameter but the seven controls, Budget, the three
// MIDI settings and each layer's Ring Length. A parameter merged later changes this test on
// purpose (spec §7): it decides whether the new parameter is in a scene.
TEST_CASE("scenes_slot_table", "[core][scenes]")
{
    std::size_t in = 0;
    for (std::size_t i = 0; i < kParamCount; ++i)
    {
        const auto s = static_cast<ParamSlot>(i);
        const auto ref = gg::core::layerOf(s);
        const bool ringLength = ref && ref->param == LayerParam::RingLength;
        const bool expected =
            !(isSceneControl(s) || s == ParamSlot::Budget || isMidiSetting(s) || ringLength);
        INFO("slot " << i);
        CHECK(isSceneSlot(s) == expected);
        in += isSceneSlot(s) ? 1 : 0;
    }
    CHECK(in == kParamCount - 7 - 1 - 3 - 4);
    CHECK_FALSE(isSceneSlot(ParamSlot::Count));
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        for (const LayerParam p :
             {LayerParam::Loop, LayerParam::LoopLength, LayerParam::Overdub, LayerParam::Splice,
              LayerParam::Mode, LayerParam::Source, LayerParam::Freeze, LayerParam::Quality})
        {
            CHECK(isSceneSlot(gg::core::layerSlot(p, layer)));
        }
        CHECK(isSceneSlot(gg::core::layerOnSlot(layer)));
    }
    CHECK(isSceneSlot(ParamSlot::MorphX));
    CHECK(isSceneSlot(ParamSlot::MorphY));
}

TEST_CASE("scenes_slot_flags_agree", "[core][scenes]")
{
    for (std::size_t i = 0; i < kParamCount; ++i)
    {
        const auto s = static_cast<ParamSlot>(i);
        INFO("slot " << i);
        CHECK(((gg::core::paramMeta(s, Mode::Fx).flags & CLAP_PARAM_IS_AUTOMATABLE) != 0) ==
              ((gg::core::paramMeta(s, Mode::Instrument).flags & CLAP_PARAM_IS_AUTOMATABLE) != 0));
    }
}

TEST_CASE("scenes_controls_not_targets", "[core][scenes]")
{
    for (const ParamSlot s : kSceneControls)
    {
        CHECK(isSceneControl(s));
        CHECK_FALSE(isValidTarget(s));
    }
    CHECK_FALSE(isSceneControl(ParamSlot::Budget));
    CHECK_FALSE(isSceneControl(ParamSlot::Count));
}

TEST_CASE("scenes_controls_locked", "[core][scenes]")
{
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        const auto locks = gg::core::structuralLocks(mode);
        for (const ParamSlot s : kSceneControls)
        {
            CHECK(locks.test(static_cast<std::size_t>(s)));
        }
    }
}
