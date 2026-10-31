// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The extremes (M2 engine spec §6.11): every parameter at its minimum, at its maximum, and at 32
// seeded random corners (each parameter at one end of its range), each held for 1 s with a note
// played in it, then every parameter back at its default for a last second.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "core/LayersFixture.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "sweep/SweepDriver.h"

using gg::core::LayerParam;
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

namespace
{
/// Corner 13's layer C, as the instrument extremes drew it under the sources branch's slot layout
/// (the corner13 report): layer C alone, the pad at X 0, Y 0, Level and Level C at +12 dB, and C's
/// grain parameters at their corner ends (Size 5 ms, Density 0.25 Hz, Texture 1, the gated
/// anchor, Pitch -24, Spray, Stereo, Freeze, Distribution, both spreads at their tops, Quality
/// Eco). Its Source, Oscillator, plays as Sample on main; Sample is set here so the case doesn't
/// move when Oscillator is built. The rest stay at their defaults: a lone 5 ms grain every few
/// seconds over the held note, with a 16-sample release into silence.
void corner13LayerC(SweepDriver &d)
{
    auto &p = d.processor();
    constexpr uint8_t kC = 2;
    for (uint8_t L = 0; L < gg::core::kLayers; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), L == kC ? 1.0f : 0.0f);
    }
    p.setBase(ParamSlot::MorphX, 0.0f);
    p.setBase(ParamSlot::MorphY, 0.0f);
    p.setBase(ParamSlot::Level, 12.0f);
    p.setBase(gg::core::levelSlot(kC), 12.0f);
    const auto end = [&](LayerParam q, bool top) {
        const ParamSlot slot = gg::core::layerSlot(q, kC);
        p.setBase(slot, top ? d.meta(slot).maxVal : d.meta(slot).minVal);
    };
    end(LayerParam::Source, false);
    end(LayerParam::Size, false);
    end(LayerParam::Density, false);
    end(LayerParam::Texture, true);
    end(LayerParam::Pitch, false);
    end(LayerParam::Spray, true);
    end(LayerParam::Stereo, true);
    end(LayerParam::Freeze, true);
    end(LayerParam::Distribution, true);
    end(LayerParam::SizeSpread, true);
    end(LayerParam::PitchSpread, true);
    end(LayerParam::Quality, false);
}

constexpr double kGrainSeconds = 5.5;

/// corner13LayerC over the held note for kGrainSeconds, with Reverb Pre-delay at `predelay`
/// seconds and, when `moveAt` >= 0, a Reverb Size move at sample `moveAt` (a multiple of 32, so
/// it applies there): its exemption then ends at moveAt + the pre-delay + kReverbSizeExempt.
/// Reverb Amount stays 0, so neither changes the output.
std::unique_ptr<SweepDriver> renderCorner13(int64_t moveAt, double predelay)
{
    auto d = std::make_unique<SweepDriver>(Mode::Instrument, 2103);
    corner13LayerC(*d);
    d->processor().setBase(ParamSlot::ReverbPredelay, static_cast<float>(predelay));
    if (moveAt >= 0)
    {
        d->play(static_cast<double>(moveAt) / SweepDriver::kRate, Material::NoteOverSine);
        d->set(ParamSlot::ReverbSize, 0.9);
    }
    d->play(kGrainSeconds - static_cast<double>(d->clock()) / SweepDriver::kRate,
            Material::NoteOverSine);
    return d;
}
} // namespace

TEST_CASE("extremes: corner 13's lone grain, its release cut by an exemption's end", "[sweep]")
{
    // The instrument extremes' corner 13 (under the sources branch's slot layout) failed at 36.6
    // against 15: a sample-end exemption ended 8 samples before the end of a lone 5 ms grain's
    // 16-sample release, and the score of the stretch after it saw those 8 samples against the
    // silence after them alone. Over the whole render the same samples score 2.8. The click
    // check scores each sample outside the exemptions as the goldens score it, over the whole
    // render, so cutting the render can only lower the score.
    const auto plain = renderCorner13(-1, 0.0);
    const auto &out = plain->output();
    const auto frames = static_cast<int64_t>(out[0].size());
    const auto mono = [&](int64_t i) {
        const auto j = static_cast<std::size_t>(i);
        return 0.5 * (static_cast<double>(out[0][j]) + static_cast<double>(out[1][j]));
    };
    // The last grain: its last sounding sample, and its first.
    int64_t last = frames - 1;
    while (last > 0 && mono(last) == 0.0)
    {
        --last;
    }
    int64_t first = last;
    while (first > 0 && mono(first - 1) != 0.0)
    {
        --first;
    }
    INFO("the last grain sounds from sample " << first << " to " << last);
    REQUIRE(last - first < 1000);      // one short grain
    REQUIRE(frames - last > 2000);     // silence after it
    REQUIRE(first > 2 * 28800 + 2000); // room for the exemption before it
    REQUIRE(mono(first - 1) == 0.0);
    // The exemption's end, 8 samples before the grain's last: inside its 16-sample release.
    const int64_t cut = last - 7;
    const int64_t moveAt = (cut - SweepDriver::kReverbSizeExempt - 1000) / 32 * 32;
    const int64_t predelayFrames = cut - SweepDriver::kReverbSizeExempt - moveAt;
    const auto moved =
        renderCorner13(moveAt, static_cast<double>(predelayFrames) / SweepDriver::kRate);
    REQUIRE(moved->output() == out); // the move changes the exemptions, not the audio

    const gg::test::Checks c = gg::test::chainBound(*plain, 0.0);
    const auto whole = plain->check(c);
    const auto cutUp = moved->check(c);
    INFO("whole: " << whole.clickScore << ", " << whole.where);
    INFO("with the exemption: " << cutUp.clickScore << ", scored " << cutUp.scoredFraction << ", "
                                << cutUp.where);
    REQUIRE(whole.clickScore <= c.clickLimit);
    REQUIRE(cutUp.scoredFraction < whole.scoredFraction); // the exemption is there
    CHECK(cutUp.clickScore <= whole.clickScore);
    CHECK(cutUp.clickScore <= c.clickLimit);
}
