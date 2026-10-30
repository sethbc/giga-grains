// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The automation bursts (M2 engine spec §6.11): 1,024 seeded random parameter events in one
// process() call, with notes, MIDI controls and a transport change among them, 16 times, 1.5 s
// played after each; every mod source routed somewhere, so the controls and the
// transport are heard. Then every parameter back at its default and every note off for a last
// second. The same again with every call one frame long.

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "core/LayersFixture.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "sweep/SweepDriver.h"

using gg::core::Mode;
using gg::test::Material;
using gg::test::SweepDriver;

namespace
{
constexpr int kBursts = 16;
constexpr int kEvents = 1024;

void bursts(Mode mode, uint64_t seed, uint32_t fixedBlock = 0)
{
    SweepDriver d(mode, seed, fixedBlock);
    d.processor().setRoutes(gg::test::layers::allSourceRoutes(mode));
    const Material m = mode == Mode::Fx ? Material::SineLive : Material::NoteOverSine;
    d.play(2.0, m);
    for (int b = 0; b < kBursts; ++b)
    {
        d.burst(kEvents, seed * 100 + static_cast<uint64_t>(b));
        // Past the 0.6 s a Reverb Size move exempts, and the up to 1.1 s the reverb's onset does
        // (SweepDriver.h), so the click check sees some of each state.
        d.play(1.5, m);
    }
    d.setDefaults();
    d.note(gg::core::NoteEventType::AllOff, -1, 1.0f, -1);
    d.play(0.05, m);
    d.play(1.0, m); // the instrument holds its note again
    gg::test::requireClean(d, gg::test::chainBound(d));
}
} // namespace

TEST_CASE("bursts: 16 x 1024 events, fx", "[sweep]") { bursts(Mode::Fx, 3101); }

TEST_CASE("bursts: 16 x 1024 events, instrument", "[sweep]") { bursts(Mode::Instrument, 3102); }

TEST_CASE("bursts at block size 1", "[sweep]") { bursts(Mode::Fx, 3103, 1); }
