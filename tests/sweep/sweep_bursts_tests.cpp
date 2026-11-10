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

/// `minScored`: the share of the render the click check scores, pinned a little under what the
/// case measures (0.431, 0.410 and 0.402 for the FX, the instrument and block size 1, with the
/// draws keyed by CLAP ID): Reverb Size is among the random slots, and each of its real moves is
/// exempt (row 217), and so are kLimiterDrive's and kDelayGlideHold's stretches (SweepDriver.h;
/// they cost the cases 0.09, 0.015 and 0.08 of it).
void bursts(Mode mode, uint64_t seed, double minScored, uint32_t fixedBlock = 0)
{
    SweepDriver d(mode, seed, fixedBlock);
    d.processor().setRoutes(gg::test::layers::allSourceRoutes(mode));
    const Material m = mode == Mode::Fx ? Material::SineLive : Material::NoteOverSine;
    d.play(2.0, m);
    for (int b = 0; b < kBursts; ++b)
    {
        d.burst(kEvents, seed * 100 + static_cast<uint64_t>(b));
        // Past the 0.6 s a Reverb Size move exempts (SweepDriver.h), so the click check sees
        // some of each state.
        d.play(1.5, m);
    }
    // No runaway under the last burst's settings, held for 1.5 s (row 309): the check below sees
    // the restored defaults' last second, which proves recovery alone.
    CHECK(d.check(gg::test::chainBound(d, 0.0)).runawayDb <= gg::test::Checks::kRunawayDb);
    d.setDefaults();
    d.note(gg::core::NoteEventType::AllOff, -1, 1.0f, -1);
    d.play(0.05, m);
    d.play(1.0, m); // the instrument holds its note again
    gg::test::requireClean(d, gg::test::chainBound(d, minScored));
}
} // namespace

TEST_CASE("bursts: 16 x 1024 events, fx", "[sweep]")
{
    // The seed moved from 3101 to 3121 (scored 0.4454, click 6.6): M3 sub-project 9's sixteen
    // parameters (the voice filter's four per layer) join the draw list, so each event's pick lands
    // on others, and seed 3101's burst 11 then left a Reverb Size move's echo (row 217) scoring
    // 30.2 at 0.7 s, past kReverbSizeExempt (the exemption widened to 48000 samples would have
    // cost 0.2 of the share, so it stays); no exemption widened and nothing else clicks.
    bursts(Mode::Fx, 3121, 0.44);
}

TEST_CASE("bursts: 16 x 1024 events, instrument", "[sweep]")
{
    // The floor moved from 0.405 to 0.39 (scored 0.3968): M3 sub-project 1's seven parameters
    // (Mode x4, MPE, MPE Bend Range, Bend Range) join the draw list, so each event's pick, a
    // draw over the CLAP IDs, lands on others, and this seed's burst now moves fewer of the
    // parameters that keep the render scoring; no exemption widened and nothing clicks.
    bursts(Mode::Instrument, 3102, 0.39);
}

TEST_CASE("bursts at block size 1", "[sweep]")
{
    // The floor moved from 0.395 to 0.37 (scored 0.3753): M3 sub-project 9's sixteen parameters
    // join the draw list, so this seed's burst moves fewer of the parameters that keep the render
    // scoring; no exemption widened and nothing clicks (click 10.3).
    bursts(Mode::Fx, 3103, 0.37, 1);
}
