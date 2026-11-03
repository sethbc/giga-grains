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
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/LayersFixture.h"
#include "gg/core/GrainQueue.h"
#include "gg/core/LivePeaks.h"
#include "gg/core/ParamIds.h"
#include "gg/core/UiSnapshot.h"
#include "gg/metrics/Metrics.h"
#include "sweep/SweepDriver.h"

using gg::core::LayerParam;
using gg::core::Mode;
using gg::core::ParamSlot;
using gg::test::Material;
using gg::test::SweepDriver;

namespace gg::core
{
/// LivePeaks's friend (LivePeaks.h): stores a value into the bucket holding `frame`, which write()
/// never does for a non-finite one.
struct LivePeaksTestAccess
{
    static void plant(const LivePeaks &peaks, int64_t frame, float value)
    {
        peaks.peaks_[static_cast<size_t>((frame / LivePeaks::kBucket) % peaks.bucketCount_)].store(
            value);
    }
};
} // namespace gg::core

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
    gg::test::requireClean(d, gg::test::chainBound(d, 0.99));
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

TEST_CASE("sweep: a Reverb Size base set straight on the Processor is a move", "[sweep]")
{
    // Row 309: the driver saw a Size move only through its own set(); a base set on the
    // Processor between calls is a move at the next call, and a queued set to the same value
    // after it is not another.
    SweepDriver d(Mode::Fx, 1119);
    d.processor().setBase(ParamSlot::ReverbAmount, 0.5f);
    d.play(1.0, Material::SineLive);
    using gg::test::Exemption;
    CHECK(d.exemptShare(Exemption::ReverbSize) == 0.0);
    d.processor().setBase(ParamSlot::ReverbSize, 0.9f);
    d.play(2.0, Material::SineLive);
    const double afterDirect = d.exemptShare(Exemption::ReverbSize);
    CHECK(afterDirect > 0.0);
    d.set(ParamSlot::ReverbSize, static_cast<double>(d.processor().base(ParamSlot::ReverbSize)));
    d.play(2.0, Material::SineLive);
    // The same Size again exempts nothing more: the exempt stretch is the same in frames.
    CHECK(d.exemptShare(Exemption::ReverbSize) * 5.0 == Catch::Approx(afterDirect * 3.0));
}

TEST_CASE("sweep: a Reverb Modulation drop at infinite Decay scores under the limit", "[sweep]")
{
    // Carry-over row 311, closed: Reverb Modulation glides (0 to 1 in 30 ms), so a drop from 1
    // at infinite Decay no longer scrubs the reverb's lines, and the harness exempts nothing for
    // it (the FX extremes' corner 24 scored 29.2 at 1 to 0 and 33.0 at 1 to 0.5 before the glide).
    for (const double to : {0.0, 0.5})
    {
        INFO("Modulation 1 to " << to);
        SweepDriver d(Mode::Fx, 1113);
        d.processor().setBase(ParamSlot::ReverbAmount, 1.0f);
        d.processor().setBase(ParamSlot::ReverbSize, 0.0f);
        d.processor().setBase(ParamSlot::ReverbDamping, 0.0f);
        d.set(ParamSlot::ReverbDecay, gg::core::kDecayMax);
        d.set(ParamSlot::ReverbModulation, 1.0);
        d.play(1.0, Material::SineLive);
        d.set(ParamSlot::ReverbModulation, to);
        d.play(2.0, Material::SineLive);
        const gg::test::Checks c = gg::test::chainBound(d, 0.0);
        const auto v = d.check(c);
        INFO(v.where);
        CHECK(v.clickScore <= c.clickLimit);
        CHECK(v.where.empty());
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

TEST_CASE("sweep: the peak bound is 3 + the input's peak only with the delay engaged", "[sweep]")
{
    // A planted peak between the two bounds: 2.5 over the sine's input peak.
    const auto planted = [](SweepDriver &d, float v) { d.output()[0][110000] = v; };
    SECTION("the delay at Amount 0: 2 + the input's peak, so 2.5 + the peak fails")
    {
        SweepDriver d(Mode::Fx, 1119);
        d.play(3.0, Material::SineLive);
        REQUIRE_FALSE(d.delayEngaged());
        const gg::test::Checks c = gg::test::chainBound(d, 0.0);
        CHECK(c.peakBound == 2.0 + d.inputPeak());
        planted(d, 2.5f + d.inputPeak());
        const auto v = d.check(c);
        INFO(v.where);
        CHECK(v.peak > c.peakBound);
        CHECK(v.where.find("peak") != std::string::npos);
    }
    SECTION("the delay engaged: the same render passes up to 3 + the input's peak, not above")
    {
        SweepDriver d(Mode::Fx, 1119);
        d.set(ParamSlot::DelayAmount, 0.5);
        d.play(3.0, Material::SineLive);
        REQUIRE(d.delayEngaged());
        const gg::test::Checks c = gg::test::chainBound(d, 0.0);
        CHECK(c.peakBound == 3.0 + d.inputPeak());
        planted(d, 2.5f + d.inputPeak());
        const auto at = d.check(c);
        INFO(at.where);
        CHECK(at.peak <= c.peakBound);
        CHECK(at.where.find("peak") == std::string::npos);
        planted(d, 3.5f + d.inputPeak());
        const auto over = d.check(c);
        INFO(over.where);
        CHECK(over.peak > c.peakBound);
        CHECK(over.where.find("peak") != std::string::npos);
    }
    SECTION("the delay engaged then returned to 0 stays on the wider bound")
    {
        SweepDriver d(Mode::Fx, 1119);
        d.set(ParamSlot::DelayAmount, 0.5);
        d.play(1.0, Material::SineLive);
        d.set(ParamSlot::DelayAmount, 0.0);
        d.play(2.0, Material::SineLive);
        CHECK(gg::test::chainBound(d, 0.0).peakBound == 3.0 + d.inputPeak());
    }
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
        // a poke, through the class's test accessor (row 309).
        const auto &peaks = d.processor().livePeaks(1);
        constexpr int64_t kFrame = 100 * gg::core::LivePeaks::kBucket;
        REQUIRE(peaks.at(kFrame) > 0.0f);
        gg::core::LivePeaksTestAccess::plant(peaks, kFrame,
                                             std::numeric_limits<float>::quiet_NaN());
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

TEST_CASE("sweep: the click score over the whole render is the goldens' metric exactly", "[sweep]")
{
    // The check scores each sample against the whole render's ±5 ms, as computeMetrics does;
    // over every sample the two must be one number, with a click in the render so that it isn't
    // the trivial one.
    SweepDriver d(Mode::Fx, 1121);
    d.processor().setBase(ParamSlot::ReverbAmount, 0.5f);
    d.play(1.0, Material::SineLive);
    d.set(ParamSlot::Pitch, 7.0);
    d.play(1.0, Material::SineLive);
    for (auto &ch : d.output())
    {
        for (std::size_t i = 60000; i < ch.size(); ++i)
        {
            ch[i] += 0.1f;
        }
    }
    gg::metrics::AudioData audio;
    audio.sampleRate = SweepDriver::kRate;
    audio.channels = {d.output()[0], d.output()[1]};
    const double goldens = gg::metrics::computeMetrics(audio).clickScore;
    INFO("the goldens' score " << goldens);
    CHECK(goldens > 5.0);
    CHECK(d.wholeRenderClickScore() == goldens);
}

namespace
{
/// A step of `height` from sample `from` to the render's end; the default, -20 dBFS, is what the
/// goldens' click injection adds.
void plantStepFrom(SweepDriver &d, int64_t from, float height = 0.1f)
{
    for (auto &ch : d.output())
    {
        for (auto i = static_cast<std::size_t>(from); i < ch.size(); ++i)
        {
            ch[i] += height;
        }
    }
}

/// One past the last sample of [from, the render's end) the click check looks away from.
int64_t firstUnexemptAfterAll(const SweepDriver &d, int64_t from)
{
    int64_t end = d.clock();
    while (end > from && !d.exemptAt(end - 1))
    {
        --end;
    }
    return end;
}

/// Layer A's wet side at full Mix, so the limiter sees the cloud (the explored scenarios'
/// setup).
void limiterBed(SweepDriver &d) { d.processor().setBase(ParamSlot::Mix, 1.0f); }
} // namespace

TEST_CASE("sweep: kLimiterDrive covers Peak driven under -6 dB, and the call's 10 ms after",
          "[sweep]")
{
    // The director's D5b, option 1 (2026-10-31; row 378). Peak entering at a -24 dB ceiling
    // over the cloud has no attack, and scores 28 over the whole render; every call in which its
    // gain falls under -6 dB is exempt, so the check scores none of it.
    SECTION("the case it covers passes")
    {
        SweepDriver d(Mode::Fx, 1201);
        limiterBed(d);
        d.processor().setBase(ParamSlot::LimiterRelease, 0.0f); // 1 s
        d.play(1.0, Material::SineLive);
        d.set(ParamSlot::LimiterCeiling, -24.0);
        d.set(ParamSlot::LimiterMode, 2.0);
        d.play(3.0, Material::SineLive);
        CHECK(d.wholeRenderClickScore() > 15.0);
        const auto v = d.check(gg::test::chainBound(d, 0.2));
        INFO(v.where);
        CHECK(v.where.empty());
        CHECK(v.scoredFraction < 0.3); // the floor below fails when the exemption widens
        const auto widened = d.check(gg::test::chainBound(d, 0.4));
        CHECK(widened.where.find("the click check scored") != std::string::npos);
    }
    SECTION("a step 10 ms past the last driven call fails, and the tail is no wider")
    {
        for (const double mode : {1.0, 2.0})
        {
            INFO("mode " << mode);
            SweepDriver d(Mode::Fx, 1204, 64);
            limiterBed(d);
            d.processor().setBase(ParamSlot::LimiterRelease, 0.0f);
            d.play(1.0, Material::SineLive);
            d.set(ParamSlot::LimiterCeiling, -24.0);
            d.set(ParamSlot::LimiterMode, mode);
            d.play(1.0, Material::SineLive);
            const int64_t offAt = d.clock();
            d.set(ParamSlot::LimiterMode, 0.0);
            d.play(2.0, Material::SineLive);
            const gg::test::Checks c = gg::test::chainBound(d, 0.0);
            REQUIRE(d.check(c).where.empty());
            // Soft stops being driven at the Off event; Peak's gain holds until its fade ends,
            // 480 samples later. The last driven call ends by then (64-sample calls), and the
            // exemption runs 480 more.
            const int64_t end = firstUnexemptAfterAll(d, offAt);
            CHECK(end >= offAt + 480);
            CHECK(end <= offAt + 480 + 64 + 480);
            plantStepFrom(d, end);
            const auto v = d.check(c);
            INFO(v.where);
            CHECK(v.clickScore > c.clickLimit);
            CHECK(namesSample(v.where, static_cast<std::size_t>(end)));
        }
    }
}

TEST_CASE("sweep: kLimiterDrive looks away only where the limiter is driven", "[sweep]")
{
    // Soft at -24 dB has the cloud at about eight times its ceiling: exempt. At -9 dB the input
    // stays under twice it, and Peak at -9 dB holds its gain above -6 dB: nothing is exempt, so
    // a planted step fails there and the scored share is the unlimited render's.
    const auto render = [](double mode, double ceiling) {
        auto d = std::make_unique<SweepDriver>(Mode::Fx, 1205);
        limiterBed(*d);
        d->set(ParamSlot::LimiterCeiling, ceiling);
        d->set(ParamSlot::LimiterMode, mode);
        d->play(3.0, Material::SineLive);
        return d;
    };
    const auto off = render(0.0, -9.0);
    const double unlimited = off->check(gg::test::chainBound(*off, 0.0)).scoredFraction;
    SECTION("Soft past twice the ceiling")
    {
        const auto d = render(1.0, -24.0);
        CHECK(d->processor().limiterDrive().softDriven);
        const gg::test::Checks c = gg::test::chainBound(*d, 0.0);
        const auto v = d->check(c);
        CHECK(v.scoredFraction < 0.2);
        CHECK(d->exemptAt(d->clock() - 1000));
        plantStepFrom(*d, d->clock() - 20000);
        INFO(d->check(c).where);
        CHECK(d->check(c).clickScore <= c.clickLimit);
    }
    SECTION("Soft under twice the ceiling")
    {
        const auto d = render(1.0, -9.0);
        CHECK(!d->processor().limiterDrive().softDriven);
        const gg::test::Checks c = gg::test::chainBound(*d, 0.0);
        CHECK(d->check(c).scoredFraction == unlimited);
        plantStepFrom(*d, d->clock() - 20000);
        const auto v = d->check(c);
        INFO(v.where);
        CHECK(v.clickScore > c.clickLimit);
    }
    SECTION("Peak above -6 dB")
    {
        const auto d = render(2.0, -9.0);
        CHECK(d->processor().limiterDrive().leastGain > SweepDriver::kPeakDriveGain);
        const gg::test::Checks c = gg::test::chainBound(*d, 0.0);
        CHECK(d->check(c).scoredFraction == unlimited);
        plantStepFrom(*d, d->clock() - 20000);
        const auto v = d->check(c);
        INFO(v.where);
        CHECK(v.clickScore > c.clickLimit);
    }
    SECTION("Peak under -6 dB")
    {
        const auto d = render(2.0, -24.0);
        CHECK(d->processor().limiterDrive().leastGain < SweepDriver::kPeakDriveGain);
    }
    SECTION("an empty call resets the tap")
    {
        const auto d = render(2.0, -24.0);
        d->processor().process(nullptr, 0, nullptr, 0, 0, {}, {}, {});
        CHECK(d->processor().limiterDrive().leastGain == 1.0f);
        CHECK(!d->processor().limiterDrive().softDriven);
    }
}

TEST_CASE("sweep: kDelayGlideHold covers the glide at Feedback 95% and up, and nothing else",
          "[sweep]")
{
    // The director's D-glide, option 1 (2026-10-31; row 379): Delay Time glides at 0.5 samples a
    // sample, so 2 s to 1 ms takes 191,904 samples (4 s); inside a Feedback 100% loop each pass
    // is re-read a fifth higher. The check looks away over the samples with Amount above 0,
    // Feedback at 0.95 or more and the read still gliding.
    constexpr int64_t kGlide = 191904;
    const auto render = [](double amount, double feedback) {
        auto d = std::make_unique<SweepDriver>(Mode::Fx, 1206);
        d->processor().setBase(ParamSlot::DelayAmount, static_cast<float>(amount));
        d->processor().setBase(ParamSlot::DelayFeedback, static_cast<float>(feedback));
        d->processor().setBase(ParamSlot::DelayDamping, 0.0f);
        d->processor().setBase(ParamSlot::DelayTime, 1.0f); // 2 s
        d->play(1.0, Material::SineLive);
        d->set(ParamSlot::DelayTime, -9.9657842846620870); // 1 ms
        d->play(6.0, Material::SineLive);
        return d;
    };
    SECTION("a step inside the glide passes; one after it fails")
    {
        const auto d = render(1.0, 1.0);
        const int64_t glideFrom = 48000;
        CHECK(d->exemptAt(glideFrom + 100));
        CHECK(d->exemptAt(glideFrom + kGlide - 100));
        CHECK(!d->exemptAt(glideFrom + kGlide + 100));
        CHECK(!d->exemptAt(glideFrom - 100));
        const gg::test::Checks c = gg::test::chainBound(*d, 0.0);
        REQUIRE(d->check(c).where.empty());
        const auto scored = d->check(c).scoredFraction;
        CHECK(scored < 0.45);
        CHECK(d->check(gg::test::chainBound(*d, 0.5)).where.find("the click check scored") !=
              std::string::npos);
        plantStepFrom(*d, glideFrom + 100000);
        CHECK(d->check(c).where.empty()); // the step is inside the glide
        const int64_t after = glideFrom + kGlide + 2000;
        plantStepFrom(*d, after, 0.5f); // the loop is loud: a step of -6 dBFS
        const auto v = d->check(c);
        INFO(v.where);
        CHECK(v.clickScore > c.clickLimit);
        CHECK(namesSample(v.where, static_cast<std::size_t>(after)));
    }
    SECTION("Feedback just under 95% exempts nothing")
    {
        const auto d = render(1.0, 0.94);
        const gg::test::Checks c = gg::test::chainBound(*d, 0.0);
        CHECK(!d->exemptAt(48000 + 100000));
        plantStepFrom(*d, 48000 + 100000);
        const auto v = d->check(c);
        INFO(v.where);
        CHECK(v.clickScore > c.clickLimit);
    }
    SECTION("Feedback at 95% is covered")
    {
        const auto d = render(1.0, 0.95);
        CHECK(d->exemptAt(48000 + 100000));
    }
    SECTION("Amount 0 exempts nothing, and Amount rising from 0 exempts from there")
    {
        SweepDriver d(Mode::Fx, 1207);
        d.processor().setBase(ParamSlot::DelayAmount, 0.0f);
        d.processor().setBase(ParamSlot::DelayFeedback, 1.0f);
        d.processor().setBase(ParamSlot::DelayDamping, 0.0f);
        d.processor().setBase(ParamSlot::DelayTime, 1.0f);
        d.play(1.0, Material::SineLive);
        d.set(ParamSlot::DelayTime, -9.9657842846620870);
        d.play(2.0, Material::SineLive);
        d.set(ParamSlot::DelayAmount, 1.0);
        d.play(2.0, Material::SineLive);
        CHECK(!d.exemptAt(48000 + 48000));
        CHECK(!d.exemptAt(3 * 48000 - 100));
        CHECK(d.exemptAt(3 * 48000 + 960));
        CHECK(d.exemptAt(5 * 48000 - 100));
        const gg::test::Checks c = gg::test::chainBound(d, 0.0);
        plantStepFrom(d, 2 * 48000);
        const auto v = d.check(c);
        INFO(v.where);
        CHECK(v.clickScore > c.clickLimit);
    }
}
