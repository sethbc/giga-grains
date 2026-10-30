// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The overload sweeps (M2 engine spec §6.11): four HQ layers at full Density (400 Hz) and Size
// (2 s), so the overload ladder lowers tiers, drops grains and steals them, with Budget swept
// from 16,384 to 256 and back; and Quality swept on each layer in turn under Budget 256. The
// ladder must never click (INTENT §14's overload rule).

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "sweep/SweepDriver.h"

using gg::core::LayerParam;
using gg::core::Mode;
using gg::core::ParamSlot;
using gg::test::Material;
using gg::test::SweepDriver;

namespace
{
/// Every layer on at HQ, Density 400 Hz, Size 2 s, under `budget`.
void fourHqLayers(SweepDriver &d, float budget)
{
    auto &p = d.processor();
    for (uint8_t L = 0; L < gg::core::kLayers; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 1.0f);
        p.setBase(gg::core::layerSlot(LayerParam::Quality, L), 2.0f);
        p.setBase(gg::core::layerSlot(LayerParam::Density, L),
                  d.meta(gg::core::layerSlot(LayerParam::Density, L)).maxVal);
        p.setBase(gg::core::layerSlot(LayerParam::Size, L),
                  d.meta(gg::core::layerSlot(LayerParam::Size, L)).maxVal);
    }
    p.setBase(ParamSlot::Budget, budget);
}
} // namespace

TEST_CASE("overload: Budget swept under four HQ layers", "[sweep]")
{
    SweepDriver d(Mode::Fx, 4101);
    fourHqLayers(d, 16384.0f);
    // 1 s of clouds filling (400 grains a layer, past the quota Budget 256 leaves each), then
    // the sweeps. Trimmed to 4 s in all: under ASan, at Debug, each second at Budget 16,384 costs
    // about two minutes.
    d.play(1.0, Material::SineLive);
    d.sweep(ParamSlot::Budget, 16384.0, 256.0, 32, 1.0);
    d.play(0.5, Material::SineLive);
    d.sweep(ParamSlot::Budget, 256.0, 16384.0, 32, 1.0);
    d.play(0.5, Material::SineLive);
    INFO("lowered " << d.processor().loweredGrains() << ", stolen "
                    << d.processor().stolenGrains());
    CHECK(d.processor().stolenGrains() > 0);
    gg::test::requireClean(d, gg::test::chainBound(d));
}

TEST_CASE("overload: Quality swept under a small Budget", "[sweep]")
{
    SweepDriver d(Mode::Instrument, 4102);
    fourHqLayers(d, 256.0f);
    d.play(2.5, Material::NoteOverSine);
    for (uint8_t L = 0; L < gg::core::kLayers; ++L)
    {
        const ParamSlot slot = gg::core::layerSlot(LayerParam::Quality, L);
        d.sweepWide(slot, 64, 2.0);
        d.set(slot, 2.0);
        d.play(0.25, Material::NoteOverSine);
    }
    d.play(1.0, Material::NoteOverSine);
    INFO("lowered " << d.processor().loweredGrains() << ", stolen "
                    << d.processor().stolenGrains());
    CHECK(d.processor().stolenGrains() > 0);
    gg::test::requireClean(d, gg::test::chainBound(d));
}
