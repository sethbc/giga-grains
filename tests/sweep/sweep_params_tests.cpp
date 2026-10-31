// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The parameter sweeps (M2 engine spec §6.11): every parameter swept over its CLAP range and a
// quarter beyond each end (the mapping clamps) in 64 steps over 2 s while a cloud plays, then
// put back, one case per group: layer A's in four (the grain parameters in each product, and the
// modulators' controls in two), the globals in two, and layers B, C and D one each with the
// layer on.

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/LayersFixture.h"
#include "gg/core/GrainQueue.h"
#include "gg/core/LivePeaks.h"
#include "gg/core/ParamIds.h"
#include "gg/core/UiSnapshot.h"
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
    gg::test::requireClean(d, gg::test::chainBound(d, 0.99));
}

TEST_CASE("sweep: layer A grain params, instrument", "[sweep]")
{
    SweepDriver d(Mode::Instrument, 1102);
    sweepGroup(d, grainSlots(0), Material::NoteOverSine);
    gg::test::requireClean(d, gg::test::chainBound(d, 0.98));
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
    gg::test::requireClean(d, gg::test::chainBound(d, 0.99));
}

TEST_CASE("sweep: layer A modulators, random and step", "[sweep]")
{
    SweepDriver d(Mode::Fx, 1104);
    withLayerAndRoutes(d, 0);
    sweepGroup(d, layerSlots(LayerParam::Random1Rate, LayerParam::Step16, 0), Material::SineLive);
    gg::test::requireClean(d, gg::test::chainBound(d, 0.99));
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
    gg::test::requireClean(d, gg::test::chainBound(d, 0.78));
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
    gg::test::requireClean(d, gg::test::chainBound(d, 0.99));
}

TEST_CASE("sweep: layer B", "[sweep]")
{
    SweepDriver d(Mode::Fx, 1107);
    withLayerAndRoutes(d, 1);
    sweepGroup(d, layerSlots(LayerParam::Position, LayerParam::Quality, 1), materialOf(d.mode()));
    gg::test::requireClean(d, gg::test::chainBound(d, 0.99));
}

TEST_CASE("sweep: layer C", "[sweep]")
{
    SweepDriver d(Mode::Instrument, 1108);
    withLayerAndRoutes(d, 2);
    sweepGroup(d, layerSlots(LayerParam::Position, LayerParam::Quality, 2), materialOf(d.mode()));
    gg::test::requireClean(d, gg::test::chainBound(d, 0.99));
}

TEST_CASE("sweep: layer D", "[sweep]")
{
    SweepDriver d(Mode::Fx, 1109);
    withLayerAndRoutes(d, 3);
    sweepGroup(d, layerSlots(LayerParam::Position, LayerParam::Quality, 3), materialOf(d.mode()));
    gg::test::requireClean(d, gg::test::chainBound(d, 0.99));
}

TEST_CASE("sweep: the checks catch a planted NaN, peak, runaway and click", "[sweep]")
{
    // The driver's own check: a clean render passes, and each fault planted in its output fails
    // the one check it should, naming the sample.
    SweepDriver d(Mode::Fx, 1110);
    d.play(4.0, Material::SineLive);
    const gg::test::Checks c = gg::test::chainBound(d, 0.0);
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

namespace
{
/// Whether `where` names the planted step's sample, or one of the two after it (the score's
/// second difference peaks at the step or just past it).
bool namesSample(const std::string &where, std::size_t at)
{
    for (std::size_t k = 0; k < 3; ++k)
    {
        if (where.find("at sample " + std::to_string(at + k) + " ") != std::string::npos)
        {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("sweep: a click planted beside an exemption is caught", "[sweep]")
{
    // The click check must score right up to an exemption's edge: a step planted in the stretch
    // between two Layer On events (an old exemption left it unscored), and one planted just
    // past a Reverb Size move's exemption, each fails with its sample named.
    const auto plantStep = [](SweepDriver &d, std::size_t from) {
        for (auto &ch : d.output())
        {
            for (std::size_t i = from; i < ch.size(); ++i)
            {
                ch[i] += 0.1f; // a -20 dBFS step, as the goldens' click injection adds
            }
        }
    };
    SECTION("between Layer On events")
    {
        SweepDriver d(Mode::Fx, 1111);
        d.play(1.0, Material::SineLive);
        const int64_t start = d.clock();
        // 64 steps over 2 s: an event every 1500 samples; Layer D stays off for the first 31.
        d.sweep(gg::core::layerOnSlot(3), 0.0, 1.0, 64, 2.0);
        d.play(1.0, Material::SineLive);
        const gg::test::Checks c = gg::test::chainBound(d, 0.0);
        REQUIRE(d.check(c).where.empty());
        const auto at = static_cast<std::size_t>(start + 1500 * 20 + 1000);
        plantStep(d, at);
        const auto v = d.check(c);
        INFO(v.where);
        CHECK(v.clickScore > c.clickLimit);
        CHECK(namesSample(v.where, at));
    }
    SECTION("beside a Reverb Size event that changes nothing")
    {
        SweepDriver d(Mode::Fx, 1115);
        d.processor().setBase(ParamSlot::ReverbAmount, 0.5f);
        d.play(1.0, Material::SineLive);
        const int64_t at = d.clock();
        d.set(ParamSlot::ReverbSize, SweepDriver::meta(ParamSlot::ReverbSize, Mode::Fx).defaultVal);
        d.play(1.0, Material::SineLive);
        const gg::test::Checks c = gg::test::chainBound(d, 0.0);
        REQUIRE(d.check(c).where.empty());
        const auto plant = static_cast<std::size_t>(at + 400);
        plantStep(d, plant);
        const auto v = d.check(c);
        INFO(v.where);
        CHECK(v.clickScore > c.clickLimit);
        CHECK(namesSample(v.where, plant));
    }
    SECTION("just past a Reverb Size move's exemption")
    {
        SweepDriver d(Mode::Fx, 1112);
        d.processor().setBase(ParamSlot::ReverbAmount, 0.5f);
        d.play(1.0, Material::SineLive);
        const int64_t moveAt = d.clock();
        d.set(ParamSlot::ReverbSize, 0.9);
        d.play(3.0, Material::SineLive);
        const gg::test::Checks c = gg::test::chainBound(d, 0.0);
        REQUIRE(d.check(c).where.empty());
        const int64_t predelay = static_cast<int64_t>(
            std::llround(d.processor().effective(ParamSlot::ReverbPredelay) * SweepDriver::kRate));
        const auto at =
            static_cast<std::size_t>(moveAt + predelay + SweepDriver::kReverbSizeExempt + 2000);
        plantStep(d, at);
        const auto v = d.check(c);
        INFO(v.where);
        CHECK(v.clickScore > c.clickLimit);
        CHECK(namesSample(v.where, at));
    }
}

TEST_CASE("sweep: a floor on the scored share fails when an exemption widens", "[sweep]")
{
    SweepDriver d(Mode::Fx, 1118);
    d.processor().setBase(ParamSlot::ReverbAmount, 0.5f);
    d.play(1.0, Material::SineLive);
    d.set(ParamSlot::ReverbSize, 0.9); // exempts about 0.6 s of the 3 s
    d.play(2.0, Material::SineLive);
    const auto ok = d.check(gg::test::chainBound(d, 0.5));
    INFO(ok.where);
    CHECK(ok.where.empty());
    CHECK(ok.scoredFraction < 0.9);
    const auto v = d.check(gg::test::chainBound(d, 0.9));
    INFO(v.where);
    CHECK(v.where.find("the click check scored") != std::string::npos);
}

TEST_CASE("sweep: the checks catch non-finite values the Processor exposes", "[sweep]")
{
    SECTION("a NaN in a live ring's mirrored guard slots")
    {
        SweepDriver d(Mode::Fx, 1113);
        d.play(2.0, Material::SineLive);
        const gg::test::Checks c = gg::test::chainBound(d, 0.0);
        REQUIRE(d.check(c).where.empty());
        const auto ring = d.processor().engine(2).live().view();
        REQUIRE(ring.slots != nullptr);
        // The last mirrored slot: a check that stopped short of the guard frames would miss it.
        const_cast<float *>(
            ring.slots)[2 * static_cast<std::size_t>(ring.capacity + SweepDriver::kLiveGuardFrames -
                                                     1)] = std::numeric_limits<float>::quiet_NaN();
        const auto v = d.check(c);
        INFO(v.where);
        CHECK(!v.finite);
        CHECK(v.where.find("non-finite live ring of layer 2") != std::string::npos);
    }
    SECTION("a NaN in a peak strip bucket")
    {
        SweepDriver d(Mode::Fx, 1116);
        d.play(1.0, Material::SineLive);
        const gg::test::Checks c = gg::test::chainBound(d, 0.0);
        REQUIRE(d.check(c).where.empty());
        // LivePeaks::write() stores a non-finite sample as 0, so a NaN reaches a bucket only by
        // a poke: its first member is the bucket array (confirmed below by reading a written
        // bucket through it, before anything is written).
        const auto &peaks = d.processor().livePeaks(1);
        auto *buckets = *reinterpret_cast<std::atomic<float> *const *>(&peaks);
        constexpr int64_t kFrame = 100 * gg::core::LivePeaks::kBucket;
        REQUIRE(peaks.at(kFrame) > 0.0f);
        REQUIRE(buckets[100].load() == peaks.at(kFrame));
        buckets[100].store(std::numeric_limits<float>::quiet_NaN());
        const auto v = d.check(c);
        INFO(v.where);
        CHECK(!v.finite);
        CHECK(v.where.find("non-finite peak strip of layer 1 at frame " + std::to_string(kFrame)) !=
              std::string::npos);
    }
    SECTION("a NaN in the UI snapshot")
    {
        SweepDriver d(Mode::Fx, 1117);
        d.play(1.0, Material::SineLive);
        gg::core::UiSnapshot snap;
        snap.feedback = std::numeric_limits<float>::quiet_NaN();
        d.processor().snapshot().push(snap);
        d.inspectExposed();
        const gg::test::Checks c = gg::test::chainBound(d, 0.0);
        const auto v = d.check(c);
        INFO(v.where);
        CHECK(!v.finite);
        CHECK(v.where.find("non-finite snapshot") != std::string::npos);
    }
    SECTION("a NaN in a grain the queue holds")
    {
        SweepDriver d(Mode::Fx, 1114);
        d.play(1.0, Material::SineLive);
        gg::core::GrainVis g;
        g.pan = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(d.processor().grainQueue().push(g));
        d.play(0.05, Material::SineLive); // inspect() drains the queue after each call
        const gg::test::Checks c = gg::test::chainBound(d, 0.0);
        const auto v = d.check(c);
        INFO(v.where);
        CHECK(!v.finite);
        CHECK(v.where.find("non-finite grain in the grain queue") != std::string::npos);
    }
}
