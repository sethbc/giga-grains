// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The extremes (M2 engine spec §6.11): every parameter at its minimum, at its maximum, and at 32
// seeded random corners (each parameter at one end of its range), each held for 1 s with a note
// played in it, then every parameter back at its default for a last second.

#include <cstddef>
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "core/LayersFixture.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "sweep/SweepDriver.h"

using gg::core::Mode;
using gg::core::NoteEventType;
using gg::core::ParamSlot;
using gg::test::Material;
using gg::test::SweepDriver;

namespace
{
constexpr int kCorners = 32;

/// Every parameter at its minimum (0), its maximum (1), or corner c's end for it (2 + c).
void setAll(SweepDriver &d, int state, uint64_t seed)
{
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        const auto &m = d.meta(slot);
        const bool top =
            state == 1 ||
            (state >= 2 && gg::test::layers::uniform(seed, (state - 2) * 1000 + s) < 0.5);
        d.set(slot, top ? m.maxVal : m.minVal);
    }
    d.mark(state == 0 ? std::string("every parameter at its minimum")
           : state == 1
               ? std::string("every parameter at its maximum")
               : "corner " + std::to_string(state - 1) + " of " + std::to_string(kCorners));
}

/// `minScored`: the share of the render the click check scores, pinned a little under what the
/// case measures (0.565 in the FX, 0.556 in the instrument): Reverb Size moves are exempt (row
/// 217).
void extremes(Mode mode, uint64_t seed, double minScored)
{
    SweepDriver d(mode, seed);
    const Material m = mode == Mode::Fx ? Material::SineLive : Material::NoteOverSine;
    d.play(2.0, m);
    for (int state = 0; state < 2 + kCorners; ++state)
    {
        setAll(d, state, seed);
        // A note in each second: in the FX it freezes the rings; in the instrument it joins the
        // held one.
        const auto key = static_cast<int16_t>(36 + state % 48);
        d.play(0.1, m);
        d.note(NoteEventType::On, key, 0.25f + 0.02f * static_cast<float>(state % 32));
        d.play(0.5, m);
        d.note(NoteEventType::Off, key);
        d.play(0.4, m);
    }
    d.setDefaults();
    d.play(1.0, m);
    gg::test::requireClean(d, gg::test::chainBound(d, minScored));
}
} // namespace

TEST_CASE("extremes: minima, maxima and 32 corners, fx", "[sweep]")
{
    extremes(Mode::Fx, 2101, 0.55);
}

TEST_CASE("extremes: minima, maxima and 32 corners, instrument", "[sweep]")
{
    extremes(Mode::Instrument, 2102, 0.54);
}
