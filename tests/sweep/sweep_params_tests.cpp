// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The parameter sweeps (M2 engine spec §6.11): every parameter swept over its CLAP range and a
// quarter beyond each end (the mapping clamps) in 64 steps over 2 s while a cloud plays, then
// put back, one case per group: layer A's in four (the grain parameters in each product, and the
// modulators' controls in two), the globals in two, and layers B, C and D one each with the
// layer on.

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/LayersFixture.h"
#include "gg/core/ParamIds.h"
#include "sweep/SweepDriver.h"

using gg::core::LayerParam;
using gg::core::Mode;
using gg::core::ParamSlot;
using gg::test::Material;
using gg::test::SweepDriver;

namespace
{
constexpr int kSteps = 64;
constexpr double kSweepSeconds = 2.0;

/// Layer L's slots of the parameters from `first` to `last`, in order.
std::vector<ParamSlot> layerSlots(LayerParam first, LayerParam last, uint8_t L)
{
    std::vector<ParamSlot> slots;
    for (auto p = static_cast<std::size_t>(first); p <= static_cast<std::size_t>(last); ++p)
    {
        slots.push_back(gg::core::layerSlot(static_cast<LayerParam>(p), L));
    }
    return slots;
}

/// The grain parameters: Position … Diffusion, Source, the spreads and Quality.
std::vector<ParamSlot> grainSlots(uint8_t L)
{
    auto slots = layerSlots(LayerParam::Position, LayerParam::SizeSpread, L);
    slots.push_back(gg::core::layerSlot(LayerParam::Quality, L));
    return slots;
}

/// Two seconds at rest (the first for the input to steady), then each slot swept wide and put back
/// to the base it had, with a quarter second after it, then a second at rest: the last second the
/// runaway check compares with the rest.
void sweepGroup(SweepDriver &d, const std::vector<ParamSlot> &slots, Material m)
{
    d.play(2.0, m);
    for (const ParamSlot slot : slots)
    {
        const float rest = d.processor().base(slot);
        d.sweepWide(slot, kSteps, kSweepSeconds);
        d.set(slot, rest);
        d.play(0.25, m);
    }
    d.play(1.0, m);
}

/// Turns layer L on, and on every layer routes every mod source somewhere (the factory routes
/// kept), so the modulators' controls are heard.
void withLayerAndRoutes(SweepDriver &d, uint8_t L)
{
    d.processor().setBase(gg::core::layerOnSlot(L), 1.0f);
    d.processor().setRoutes(gg::test::layers::allSourceRoutes(d.mode()));
}

Material materialOf(Mode mode)
{
    return mode == Mode::Fx ? Material::SineLive : Material::NoteOverSine;
}
} // namespace

TEST_CASE("sweep: layer A grain params, fx", "[sweep]")
{
    SweepDriver d(Mode::Fx, 1101);
    sweepGroup(d, grainSlots(0), Material::SineLive);
    gg::test::requireClean(d, gg::test::chainBound(d));
}

TEST_CASE("sweep: layer A grain params, instrument", "[sweep]")
{
    SweepDriver d(Mode::Instrument, 1102);
    sweepGroup(d, grainSlots(0), Material::NoteOverSine);
    gg::test::requireClean(d, gg::test::chainBound(d));
}

TEST_CASE("sweep: layer A modulators, LFOs and envelopes", "[sweep]")
{
    // The instrument, so a note gates the envelopes: held through each sweep's first half,
    // released for its second, and held again for the quarter second after it.
    SweepDriver d(Mode::Instrument, 1103);
    withLayerAndRoutes(d, 0);
    d.play(2.0, Material::NoteOverSine);
    for (const ParamSlot slot : layerSlots(LayerParam::Lfo1Rate, LayerParam::Env2Release, 0))
    {
        const float rest = d.processor().base(slot);
        const auto &m = d.meta(slot);
        const double q = (static_cast<double>(m.maxVal) - m.minVal) / 4.0;
        const double mid = (static_cast<double>(m.minVal) + m.maxVal) / 2.0;
        d.sweep(slot, m.minVal - q, mid, kSteps / 2, kSweepSeconds / 2);
        d.note(gg::core::NoteEventType::Off, SweepDriver::kNoteKey);
        d.sweep(slot, mid, m.maxVal + q, kSteps / 2, kSweepSeconds / 2);
        d.set(slot, rest);
        d.play(0.25, Material::NoteOverSine);
    }
    d.play(1.0, Material::NoteOverSine);
    gg::test::requireClean(d, gg::test::chainBound(d));
}

TEST_CASE("sweep: layer A modulators, random and step", "[sweep]")
{
    SweepDriver d(Mode::Fx, 1104);
    withLayerAndRoutes(d, 0);
    sweepGroup(d, layerSlots(LayerParam::Random1Rate, LayerParam::Step16, 0), Material::SineLive);
    gg::test::requireClean(d, gg::test::chainBound(d));
}

TEST_CASE("sweep: globals, space", "[sweep]")
{
    // Reverb Amount at 50%, so the others are heard; Reverb Size's moves are exempt from the
    // click check (carry-over row 217), the driver's own doing.
    SweepDriver d(Mode::Fx, 1105);
    d.processor().setBase(ParamSlot::ReverbAmount, 0.5f);
    sweepGroup(d,
               {ParamSlot::ReverbAmount, ParamSlot::ReverbDecay, ParamSlot::ReverbSize,
                ParamSlot::ReverbDamping, ParamSlot::ReverbPredelay, ParamSlot::ReverbModulation,
                ParamSlot::DryToReverb},
               Material::SineLive);
    gg::test::requireClean(d, gg::test::chainBound(d));
}

TEST_CASE("sweep: globals, macros and morph", "[sweep]")
{
    // Every layer on, so the morph, the Levels and the switches act.
    SweepDriver d(Mode::Fx, 1106);
    for (uint8_t L = 1; L < gg::core::kLayers; ++L)
    {
        d.processor().setBase(gg::core::layerOnSlot(L), 1.0f);
    }
    std::vector<ParamSlot> slots{ParamSlot::Mix,    ParamSlot::Level,  ParamSlot::LevelB,
                                 ParamSlot::LevelC, ParamSlot::LevelD, ParamSlot::MorphX,
                                 ParamSlot::MorphY};
    slots.insert(slots.end(), gg::core::kMacroSlots.begin(), gg::core::kMacroSlots.end());
    for (uint8_t L = 0; L < gg::core::kLayers; ++L)
    {
        slots.push_back(gg::core::layerOnSlot(L));
    }
    slots.push_back(ParamSlot::Budget);
    sweepGroup(d, slots, Material::SineLive);
    gg::test::requireClean(d, gg::test::chainBound(d));
}

TEST_CASE("sweep: layer B", "[sweep]")
{
    SweepDriver d(Mode::Fx, 1107);
    withLayerAndRoutes(d, 1);
    sweepGroup(d, layerSlots(LayerParam::Position, LayerParam::Quality, 1), materialOf(d.mode()));
    gg::test::requireClean(d, gg::test::chainBound(d));
}

TEST_CASE("sweep: layer C", "[sweep]")
{
    SweepDriver d(Mode::Instrument, 1108);
    withLayerAndRoutes(d, 2);
    sweepGroup(d, layerSlots(LayerParam::Position, LayerParam::Quality, 2), materialOf(d.mode()));
    gg::test::requireClean(d, gg::test::chainBound(d));
}

TEST_CASE("sweep: layer D", "[sweep]")
{
    SweepDriver d(Mode::Fx, 1109);
    withLayerAndRoutes(d, 3);
    sweepGroup(d, layerSlots(LayerParam::Position, LayerParam::Quality, 3), materialOf(d.mode()));
    gg::test::requireClean(d, gg::test::chainBound(d));
}

TEST_CASE("sweep: the checks catch a planted NaN, peak, runaway and click", "[sweep]")
{
    // The driver's own check: a clean render passes, and each fault planted in its output fails
    // the one check it should, naming the sample.
    SweepDriver d(Mode::Fx, 1110);
    d.play(4.0, Material::SineLive);
    const gg::test::Checks c = gg::test::chainBound(d);
    const auto clean = d.check(c);
    INFO(clean.where);
    REQUIRE(clean.finite);
    REQUIRE(clean.peak <= c.peakBound);
    REQUIRE(clean.runawayDb <= 6.0);
    REQUIRE(clean.clickScore <= c.clickLimit);
    REQUIRE(clean.where.empty());

    auto &out = d.output();
    const auto plant = [&](auto &&fault, auto &&failed, const char *expect) {
        const auto saved = out;
        fault();
        const auto v = d.check(c);
        INFO(v.where);
        CHECK(failed(v));
        CHECK(v.where.find(expect) != std::string::npos);
        out = saved;
    };
    plant([&] { out[1][100000] = std::numeric_limits<float>::quiet_NaN(); },
          [](const auto &v) { return !v.finite; }, "non-finite output at sample 100000");
    plant([&] { out[0][110000] = 3.0f; }, [&](const auto &v) { return v.peak > c.peakBound; },
          "at sample 110000");
    plant(
        [&] {
            for (auto &ch : out)
            {
                for (std::size_t i = ch.size() - 48000; i < ch.size(); ++i)
                {
                    ch[i] *= 4.0f; // +12 dB
                }
            }
        },
        [](const auto &v) { return v.runawayDb > 6.0; }, "runaway");
    plant(
        [&] {
            for (auto &ch : out)
            {
                for (std::size_t i = 120000; i < ch.size(); ++i)
                {
                    ch[i] += 0.1f; // a -20 dBFS step, as the goldens' click injection adds
                }
            }
        },
        [&](const auto &v) { return v.clickScore > c.clickLimit; }, "at sample 120000 ");
}
