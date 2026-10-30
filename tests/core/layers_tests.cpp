// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The Processor's four layers (M2 sub-project 1's spec §6.2, §6.3, §6.4, §6.7): each layer's
// chain, the sum in layer order, the morph law, the on/off ramps and the engine's enable gate,
// the source kind per layer, the instrument's input into the rings, the grain events' order, and
// the real-time and determinism contracts with four layers rendering.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Processor.h"
#include "core/LayersFixture.h"
#include "gg/metrics/Metrics.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/SampleBuffer.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "support/AllocTripwire.h"

using gg::core::LayerParam;
using gg::core::layerSlot;
using gg::core::Mode;
using gg::core::NoteEvent;
using gg::core::NoteEventType;
using gg::core::ParamEvent;
using gg::core::ParamSlot;
using gg::core::Processor;
using gg::test::AllocTripwire;
using namespace gg::test::layers;

namespace
{
constexpr int64_t kRamp = 480; // Processor::kRampSeconds at 48 kHz
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

ParamSlot slotOf(LayerParam p, uint8_t layer) { return layerSlot(p, layer); }

uint32_t bitsOf(float f) { return std::bit_cast<uint32_t>(f); }

// FX defaults with Mix 1 and every layer's Feedback 0: the output is the layers' sum while the
// reverb is idle (Reverb Amount 0, the default).
void wetOnly(Processor &p) { p.setBase(ParamSlot::Mix, 1.0f); }
} // namespace

TEST_CASE("B-D off adds nothing: the sum assigns", "[core][layers]")
{
    // M2 sub-project 1's spec §6.3: the first rendering layer assigns, so layers B to D off cost
    // nothing in the bits. (The M1 bit identity itself is the golden WAVs' hash list.)
    constexpr int64_t kFrames = 48000;
    const Stereo input = noise(kFrames, 0.5f, 3);
    const auto renderWith = [&](const std::function<void(Processor &)> &setup) {
        Processor p({kRate, 4096, 11, Mode::Fx});
        wetOnly(p);
        setup(p);
        Stereo out = silence(kFrames);
        render(p, &input, 0, kFrames, {512}, {}, out);
        return out;
    };
    const Stereo untouched = renderWith([](Processor &) {});
    const Stereo withB = renderWith([](Processor &p) { p.setBase(ParamSlot::LayerBOn, 1.0f); });
    const Stereo onThenOff = renderWith([](Processor &p) {
        p.setBase(ParamSlot::LayerBOn, 1.0f);
        p.setBase(ParamSlot::LayerBOn, 0.0f);
    });
    REQUIRE(peak(untouched.left) > 0.05f);
    CHECK(sameBits(untouched.left, onThenOff.left));
    CHECK(sameBits(untouched.right, onThenOff.right));
    CHECK_FALSE(sameBits(untouched.left, withB.left));
}

TEST_CASE("layers sum in order A to D", "[core][layers]")
{
    // Four Sample layers over four sines, at the pad's centre: each weighs exactly 0.25. With
    // Mix 1, no input and the reverb idle, the FX's output is 0 · (1 − 1) + wet · 1 = +0 + wet.
    // A layer rendered alone weighs exactly 1, so its output is +0 + limited_L. The four-layer
    // output must then be, bit for bit,
    //     +0 + (((a · 0.25f + b · 0.25f) + c · 0.25f) + d · 0.25f)
    // with a..d the one-layer outputs: each layer's limited signal times its weight, assigned for
    // A and added for B, C and D in that order, as the Processor does.
    constexpr int64_t kFrames = 24000;
    const std::array<double, 4> hz{220.0, 330.0, 495.0, 742.5};
    const auto renderLayers = [&](std::array<bool, 4> on) {
        Processor p({kRate, 4096, 21, Mode::Fx});
        wetOnly(p);
        for (uint8_t L = 0; L < 4; ++L)
        {
            p.setBase(slotOf(LayerParam::Source, L), 0.0f); // Sample
            p.setBase(slotOf(LayerParam::Position, L), 0.1f + 0.2f * L);
            p.setBase(slotOf(LayerParam::Density, L), 5.0f);
            p.setBase(gg::core::layerOnSlot(L), on[L] ? 1.0f : 0.0f);
            p.setSourceNow(L, sine(hz[L]));
        }
        Stereo out = silence(kFrames);
        render(p, nullptr, 0, kFrames, {4096}, {}, out);
        return out;
    };
    const Stereo all = renderLayers({true, true, true, true});
    std::array<Stereo, 4> alone;
    for (size_t L = 0; L < 4; ++L)
    {
        std::array<bool, 4> on{};
        on[L] = true;
        alone[L] = renderLayers(on);
        INFO("layer " << L);
        REQUIRE(peak(alone[L].left) > 0.05f);
    }
    for (int64_t i = 0; i < kFrames; ++i)
    {
        const auto j = static_cast<std::size_t>(i);
        const auto expected = [&](const std::vector<float> Stereo::*channel) {
            const float a = (alone[0].*channel)[j];
            const float b = (alone[1].*channel)[j];
            const float c = (alone[2].*channel)[j];
            const float d = (alone[3].*channel)[j];
            return 0.0f + (((a * 0.25f + b * 0.25f) + c * 0.25f) + d * 0.25f);
        };
        INFO("sample " << i);
        REQUIRE(bitsOf(all.left[j]) == bitsOf(expected(&Stereo::left)));
        REQUIRE(bitsOf(all.right[j]) == bitsOf(expected(&Stereo::right)));
    }
}

TEST_CASE("the weight is exactly 1 with one layer on anywhere on the pad", "[core][layers]")
{
    // M2 sub-project 1's spec §6.3: b_A / b_A is 1.0f exactly, wherever the pad is and while it
    // ramps; so layer A alone renders the same bits at any Morph.
    constexpr int64_t kFrames = 36000;
    const Stereo input = noise(kFrames, 0.5f, 5);
    Stereo centre = silence(kFrames);
    {
        Processor p({kRate, 4096, 7, Mode::Fx});
        wetOnly(p);
        render(p, &input, 0, kFrames, {4096}, {}, centre);
    }
    REQUIRE(peak(centre.left) > 0.05f);
    for (const auto &[x, y] :
         std::array<std::array<float, 2>, 3>{{{0.0f, 0.0f}, {1.0f, 1.0f}, {0.37f, 0.9f}}})
    {
        INFO("Morph (" << x << ", " << y << ")");
        Processor p({kRate, 4096, 7, Mode::Fx});
        wetOnly(p);
        p.setBase(ParamSlot::MorphX, x);
        p.setBase(ParamSlot::MorphY, y);
        CHECK(p.morphWeightAt(0, 0) == 1.0f);
        for (uint8_t L = 1; L < 4; ++L)
        {
            CHECK(p.morphWeightAt(L, 0) == 0.0f);
        }
        // And while the pad ramps to the opposite corner from sample 4096.
        const std::vector<TimedEvent> moves{{4096, ParamSlot::MorphX, false, 1.0 - x},
                                            {4096, ParamSlot::MorphY, false, 1.0 - y}};
        Stereo out = silence(kFrames);
        render(p, &input, 0, 4096 + kRamp / 2, {4096}, moves, out);
        for (int64_t t = 4096; t < 4096 + kRamp + 10; ++t)
        {
            REQUIRE(p.morphWeightAt(0, t) == 1.0f);
        }
        render(p, &input, 4096 + kRamp / 2, kFrames, {4096}, moves, out);
        CHECK(sameBits(out.left, centre.left));
        CHECK(sameBits(out.right, centre.right));
    }
}

TEST_CASE("four layers at the centre weigh a quarter each", "[core][layers]")
{
    Processor p({kRate, 512, 1, Mode::Fx});
    for (uint8_t L = 0; L < 4; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 1.0f);
    }
    for (uint8_t L = 0; L < 4; ++L)
    {
        INFO("layer " << int{L});
        CHECK(p.morphWeightAt(L, 0) == 0.25f);
        CHECK(p.layerRendering(L));
    }
    // Three on: a third each, the amplitude sum 1.
    p.setBase(ParamSlot::LayerDOn, 0.0f);
    CHECK(p.morphWeightAt(3, 0) == 0.0f);
    float sum = 0.0f;
    for (uint8_t L = 0; L < 3; ++L)
    {
        CHECK(p.morphWeightAt(L, 0) == 0.25f / 0.75f);
        sum += p.morphWeightAt(L, 0);
    }
    CHECK_THAT(sum, Catch::Matchers::WithinAbs(1.0, 1e-6));
}

TEST_CASE("the pad at a corner silences the others", "[core][layers]")
{
    // The law clamps x and y to [0.001, 0.999] (spec §6.3), so at a corner its layer weighs
    // 0.999² ≈ 0.998, the two beside it 0.000999 (−60 dB) and the opposite one 10⁻⁶ (−120 dB).
    Processor p({kRate, 512, 1, Mode::Fx});
    for (uint8_t L = 0; L < 4; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 1.0f);
    }
    // A top left, B top right, C bottom left, D bottom right; D opposite A, C opposite B.
    const std::array<std::array<float, 2>, 4> corners{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
    for (uint8_t corner = 0; corner < 4; ++corner)
    {
        INFO("corner " << int{corner});
        p.setBase(ParamSlot::MorphX, corners[corner][0]);
        p.setBase(ParamSlot::MorphY, corners[corner][1]);
        float sum = 0.0f;
        for (uint8_t L = 0; L < 4; ++L)
        {
            INFO("layer " << int{L});
            const float w = p.morphWeightAt(L, 0);
            sum += w;
            if (L == corner)
            {
                CHECK(w > 0.998f);
            }
            else if (L == 3 - corner)
            {
                CHECK(w < 1.01e-6f);
            }
            else
            {
                CHECK(w < 1.0e-3f);
            }
        }
        CHECK_THAT(sum, Catch::Matchers::WithinAbs(1.0, 1e-6));
    }
}

TEST_CASE("a layer switched on speaks at once", "[core][layers]")
{
    // M2 sub-project 1's spec §6.3: on, the engine is enabled at the boundary, which forces an
    // onset there; at 0.25 Hz its own schedule would wait seconds.
    constexpr int64_t kFrames = 96000;
    const Stereo input = noise(kFrames, 0.5f, 9);
    Processor p({kRate, 4096, 13, Mode::Fx});
    p.setBase(slotOf(LayerParam::Density, 1), -2.0f); // 0.25 Hz
    Log log;
    p.setEventSink(&Log::record, &log);
    Stereo out = silence(kFrames);
    render(p, &input, 0, kFrames, {1000}, {{48000, ParamSlot::LayerBOn, false, 1.0}}, out, &log);
    REQUIRE_FALSE(log.overflowed);
    const auto b = log.layer(1);
    REQUIRE_FALSE(b.empty());
    CHECK(b.front().onsetSample == 48000);
    CHECK(b.front().onsetFrac == 0.0);
    CHECK(b.front().index == 0);
    REQUIRE_FALSE(log.layer(0).empty()); // layer A played all along
    CHECK(log.layer(2).empty());
    CHECK(log.layer(3).empty());
}

TEST_CASE("a layer switched off ramps over 10 ms then spawns nothing", "[core][layers]")
{
    // M2 sub-project 1's spec §6.3: off, the layer's ramp takes its weight to 0 over 10 ms; at
    // that sample the engine is disabled, its grains end and none spawn.
    constexpr int64_t kOff = 24000; // a grid boundary
    constexpr int64_t kFrames = 36000;
    const Stereo input = noise(kFrames, 0.5f, 15);
    Processor p({kRate, 4096, 17, Mode::Fx});
    wetOnly(p);
    p.setBase(ParamSlot::LayerBOn, 1.0f);
    p.setBase(slotOf(LayerParam::Density, 1), 7.0f); // 128 Hz
    Log log;
    p.setEventSink(&Log::record, &log);
    Stereo out = silence(kFrames);
    const std::vector<TimedEvent> off{{kOff, ParamSlot::LayerBOn, false, 0.0}};
    render(p, &input, 0, kOff + kRamp / 2, {100}, off, out, &log);
    CHECK(p.layerRendering(1));
    CHECK(p.engine(1).activeGrains() > 0); // still sounding, fading
    CHECK(p.engine(1).enabled());
    CHECK(p.morphWeightAt(1, kOff + kRamp - 1) > 0.0f);
    CHECK(p.morphWeightAt(1, kOff + kRamp) == 0.0f);
    CHECK(p.morphWeightAt(0, kOff + kRamp) == 1.0f);
    render(p, &input, kOff + kRamp / 2, kFrames, {100}, off, out, &log);
    CHECK_FALSE(p.layerRendering(1));
    CHECK_FALSE(p.engine(1).enabled());
    CHECK(p.engine(1).activeGrains() == 0);
    CHECK(p.engine(1).sampleClock() == kFrames); // its clock and ring go on
    CHECK(p.engine(1).liveWriteHead() == kFrames);
    const auto b = log.layer(1);
    REQUIRE(b.size() > 50);
    for (const gg::GrainSpec &g : b)
    {
        REQUIRE(g.onsetSample < kOff + kRamp);
    }
    CHECK(b.back().onsetSample >= kOff); // it spawned during the ramp
    CHECK(allFinite(out.left));
}

TEST_CASE("switching off the last layer fades over 10 ms", "[core][layers]")
{
    // D1 as amended (Task 10's fix round): the weights are times min(1, Σ r_K), so layer A
    // alone, switched off, fades with its ramp, (b/b) × r, rather than staying at 1 until the
    // ramp reaches 0 and being cut there. A Sample layer over a sine, with no input and Mix 1:
    // the output is the layer alone, where a cut is a click.
    constexpr int64_t kOff = 24000; // a grid boundary
    constexpr int64_t kFrames = 36000;
    Processor p({kRate, 4096, 55, Mode::Fx});
    wetOnly(p);
    p.setBase(ParamSlot::Source, 0.0f); // Sample
    p.setBase(ParamSlot::Position, 0.5f);
    p.setBase(ParamSlot::Density, 6.0f);
    p.setSourceNow(0, sine(440.0));
    Stereo out = silence(kFrames);
    const std::vector<TimedEvent> off{{kOff, ParamSlot::LayerAOn, false, 0.0}};
    render(p, nullptr, 0, kOff + 1, {4096}, off, out);
    CHECK(p.morphWeightAt(0, kOff) == 1.0f);
    CHECK_THAT(p.morphWeightAt(0, kOff + kRamp / 4), Catch::Matchers::WithinAbs(0.75, 1e-6));
    CHECK_THAT(p.morphWeightAt(0, kOff + kRamp / 2), Catch::Matchers::WithinAbs(0.5, 1e-6));
    CHECK_THAT(p.morphWeightAt(0, kOff + kRamp - 1), Catch::Matchers::WithinAbs(1.0 / 480, 1e-6));
    CHECK(p.morphWeightAt(0, kOff + kRamp) == 0.0f);
    render(p, nullptr, kOff + 1, kFrames, {4096}, off, out);
    CHECK_FALSE(p.layerRendering(0));
    // Full level before the switch, fading through the ramp, nothing after it.
    const float before = peakIn(out.left, kOff - kRamp, kOff);
    REQUIRE(before > 0.05f);
    CHECK(peakIn(out.left, kOff + kRamp / 2, kOff + kRamp / 2 + 48) < 0.6f * before);
    CHECK(peakIn(out.left, kOff + kRamp - 24, kOff + kRamp) < 0.06f * before);
    CHECK(peakIn(out.left, kOff + kRamp, kFrames) == 0.0f);
    const gg::metrics::AudioData audio{kRate, {out.left, out.right}};
    CHECK(gg::metrics::computeMetrics(audio).clickScore < 15.0);
}

TEST_CASE("a layer toggled within its ramp keeps its grains", "[core][layers]")
{
    // Review Focus 1: off at t and on again at t + 160, inside the 10 ms ramp. The ramp turns
    // from its value, never reaching 0, so the engine is never disabled, its grains keep
    // sounding, no forced onset fires, and the output has no click.
    constexpr int64_t kOff = 24000;
    constexpr int64_t kOn = kOff + 160;
    constexpr int64_t kFrames = 48000;
    const Stereo input = noise(kFrames, 0.5f, 19);
    Processor p({kRate, 4096, 23, Mode::Fx});
    wetOnly(p);
    p.setBase(ParamSlot::LayerBOn, 1.0f);
    p.setBase(slotOf(LayerParam::Density, 1), 7.0f);
    Log log;
    p.setEventSink(&Log::record, &log);
    Stereo out = silence(kFrames);
    const std::vector<TimedEvent> toggles{{kOff, ParamSlot::LayerBOn, false, 0.0},
                                          {kOn, ParamSlot::LayerBOn, false, 1.0}};
    bool dropped = false;
    render(p, &input, 0, kFrames, {32}, toggles, out, &log, [&](int64_t done) {
        if (done > kOff - 1000 && done < kOn + 2 * kRamp)
        {
            dropped = dropped || p.engine(1).activeGrains() == 0 || !p.engine(1).enabled();
        }
    });
    CHECK_FALSE(dropped);
    // Back at the pair's even share once the ramp has risen again.
    CHECK(p.morphWeightAt(1, kFrames) == 0.5f);
    for (const gg::GrainSpec &g : log.layer(1))
    {
        CHECK_FALSE((g.onsetSample == kOn && g.onsetFrac == 0.0)); // no forced onset
    }
    const gg::metrics::AudioData audio{kRate, {out.left, out.right}};
    const gg::metrics::Metrics m = gg::metrics::computeMetrics(audio);
    CHECK(m.clickScore < 15.0);
    CHECK(m.peakDb > -30.0);
}

TEST_CASE("all layers off renders silence", "[core][layers]")
{
    // Review Focus 2: with no layer rendering the sum has no first layer, so wet is +0, not a
    // stale buffer; the reverb still runs, on silence. Turning layer A on again speaks at that
    // boundary.
    constexpr int64_t kFrames = 72000;
    const Stereo input = noise(kFrames, 0.5f, 25);
    Processor p({kRate, 4096, 27, Mode::Fx});
    wetOnly(p);
    p.setBase(ParamSlot::ReverbAmount, 0.5f);
    p.setBase(ParamSlot::LayerAOn, 0.0f);
    Log log;
    p.setEventSink(&Log::record, &log);
    Stereo out = silence(kFrames);
    std::fill(out.left.begin(), out.left.end(), kNaN);
    std::fill(out.right.begin(), out.right.end(), kNaN);
    render(p, &input, 0, 48000, {1000}, {}, out, &log);
    CHECK(p.reverbActive());
    for (std::size_t i = 0; i < 48000; ++i)
    {
        INFO("sample " << i);
        REQUIRE(bitsOf(out.left[i]) == 0u);
        REQUIRE(bitsOf(out.right[i]) == 0u);
    }
    CHECK(log.events.empty());
    for (uint8_t L = 0; L < 4; ++L)
    {
        CHECK_FALSE(p.layerRendering(L));
        CHECK(p.engine(L).liveWriteHead() == 48000); // every ring kept recording
    }
    render(p, &input, 48000, kFrames, {1000}, {{48000, ParamSlot::LayerAOn, false, 1.0}}, out,
           &log);
    REQUIRE_FALSE(log.events.empty());
    CHECK(log.events.front().layer == 0);
    CHECK(log.events.front().grain.onsetSample == 48000);
    CHECK(peakIn(out.left, 48000, kFrames) > 0.05f);
    CHECK(allFinite(out.left));
}

TEST_CASE("Position, Spray and Feedback follow the layer's kind", "[core][layers]")
{
    SECTION("an instrument layer set to Live reads seconds behind the head")
    {
        constexpr int64_t kFrames = 48000;
        const Stereo input = noise(kFrames, 0.5f, 29);
        Processor p({kRate, 4096, 31, Mode::Instrument});
        p.setBase(ParamSlot::Source, 1.0f);          // Live
        p.setBase(ParamSlot::Position, 0.28967658f); // 0.25 s behind the head
        p.setBase(ParamSlot::Spray, 0.0f);
        Log log;
        p.setEventSink(&Log::record, &log);
        Stereo out = silence(kFrames);
        const std::array<NoteEvent, 1> note{{{0, NoteEventType::On, 0, 60}}};
        float *o[2] = {out.left.data(), out.right.data()};
        const float *in[2] = {input.left.data(), input.right.data()};
        p.process(in, 2, o, 2, kFrames, {}, note);
        CHECK(p.engine(0).sourceMode() == gg::SourceMode::Live);
        REQUIRE(log.events.size() > 10);
        int checked = 0;
        for (const Log::Event &e : log.events)
        {
            const double onset = static_cast<double>(e.grain.onsetSample) + e.grain.onsetFrac;
            if (onset > 13000.0)
            {
                INFO("grain " << e.grain.index);
                CHECK_THAT(e.grain.startPos, Catch::Matchers::WithinAbs(onset - 12000.0, 1.0));
                ++checked;
            }
        }
        CHECK(checked > 10);
        CHECK(peak(out.left) > 0.05f);
    }

    SECTION("an instrument layer set to Live feeds its limited signal back, 256 samples late")
    {
        // As "feedback writes the limited wet signal 256 samples late into the ring" checks for
        // the FX: the instrument's output is wet alone, so with layer A alone (weight 1) and the
        // reverb idle it is limited itself, and the ring's frame f + 256 holds
        // in[f + 256] + 0.5 × out[f].
        constexpr uint32_t kFrames = 1025;
        const Stereo input = noise(kFrames, 0.5f, 33);
        Processor p({kRate, 4096, 35, Mode::Instrument});
        p.setBase(ParamSlot::Source, 1.0f);
        p.setBase(ParamSlot::Level, 0.0f);
        p.setBase(ParamSlot::Density, 6.0f);
        p.setBase(ParamSlot::Size, -2.0f);
        p.setBase(ParamSlot::Position, 0.0f);
        p.setBase(ParamSlot::Spray, 0.0f);
        p.setBase(ParamSlot::Feedback, 0.5f);
        REQUIRE(p.feedbackAt(0) == 0.5f);
        Stereo out = silence(kFrames);
        const std::array<NoteEvent, 1> note{{{0, NoteEventType::On, 0, 60}}};
        float *o[2] = {out.left.data(), out.right.data()};
        const float *in[2] = {input.left.data(), input.right.data()};
        p.process(in, 2, o, 2, kFrames, {}, note);
        const gg::LiveSource &ring = p.engine().live();
        const int64_t head = p.engine().liveWriteHead();
        REQUIRE(head == kFrames);
        gg::LiveSource::Cursor cursor;
        int sounding = 0;
        for (int64_t f = 0; f < 1024; ++f)
        {
            float l = kNaN;
            float r = kNaN;
            REQUIRE(ring.readLinear(static_cast<double>(f), head, cursor, l, r));
            const auto j = static_cast<std::size_t>(f);
            float expectedL = input.left[j];
            float expectedR = input.right[j];
            if (f >= 256)
            {
                const auto k = static_cast<std::size_t>(f - 256);
                expectedL = input.left[j] + 0.5f * out.left[k];
                expectedR = input.right[j] + 0.5f * out.right[k];
                sounding += out.left[k] != 0.0f ? 1 : 0;
            }
            INFO("frame " << f);
            REQUIRE(bitsOf(l) == bitsOf(expectedL));
            REQUIRE(bitsOf(r) == bitsOf(expectedR));
        }
        REQUIRE(sounding > 200);
        // Set back to Sample, the layer feeds nothing back: its Feedback target is 0 at once.
        p.setBase(ParamSlot::Source, 0.0f);
        CHECK(p.feedbackAt(kFrames) == 0.0f);
    }

    SECTION("an FX layer set to Sample renders its sample")
    {
        constexpr int64_t kFrames = 24000;
        Processor p({kRate, 4096, 37, Mode::Fx});
        wetOnly(p);
        p.setBase(ParamSlot::Source, 0.0f); // Sample
        p.setBase(ParamSlot::Position, 0.5f);
        p.setBase(ParamSlot::Density, 6.0f); // 64 Hz
        p.setSourceNow(0, sine(440.0));
        Log log;
        p.setEventSink(&Log::record, &log);
        Stereo out = silence(kFrames);
        render(p, nullptr, 0, kFrames, {512}, {}, out, &log);
        CHECK(p.engine(0).sourceMode() == gg::SourceMode::Sample);
        REQUIRE(log.events.size() > 10);
        for (const Log::Event &e : log.events)
        {
            // Source frames, around the middle of the 1 s sine (Spray 50 ms either side).
            CHECK_THAT(e.grain.startPos, Catch::Matchers::WithinAbs(24000.0, 2401.0));
        }
        CHECK(peak(out.left) > 0.05f); // no input: the sine's grains alone
    }
}

TEST_CASE("the instrument's input feeds the rings", "[core][layers]")
{
    // M2 sub-project 1's spec §6.2: the instrument's input feeds every layer's ring (its
    // sidechain in the plugin), and a Live layer plays it while a note is held. It never
    // reaches the output dry.
    constexpr int64_t kFrames = 48000;
    const Stereo input = noise(kFrames, 0.5f, 39);
    Processor p({kRate, 4096, 41, Mode::Instrument});
    p.setBase(ParamSlot::Source, 1.0f); // Live
    Stereo out = silence(kFrames);
    float *o[2] = {out.left.data(), out.right.data()};
    const float *in[2] = {input.left.data(), input.right.data()};
    // No note: silence, though the rings record.
    p.process(in, 2, o, 2, kFrames / 2, {});
    CHECK(peakIn(out.left, 0, kFrames / 2) == 0.0f);
    const std::array<NoteEvent, 1> note{{{0, NoteEventType::On, 0, 60}}};
    float *o2[2] = {out.left.data() + kFrames / 2, out.right.data() + kFrames / 2};
    const float *in2[2] = {input.left.data() + kFrames / 2, input.right.data() + kFrames / 2};
    p.process(in2, 2, o2, 2, kFrames / 2, {}, note);
    CHECK(peakIn(out.left, kFrames / 2, kFrames) > 0.05f);
    for (uint8_t L = 0; L < 4; ++L)
    {
        INFO("layer " << int{L});
        CHECK(p.livePeaks(L).at(1000) > 0.25f);
        CHECK(p.livePeaks(L).at(30000) > 0.25f);
    }
}

TEST_CASE("grain events come in piece, layer, spawn order", "[core][layers]")
{
    // Spec §6.7: each layer buffers its piece's spawns and the Processor drains them in layer
    // order after the piece. Calls of 256 frames are one piece each here.
    constexpr int64_t kFrames = 48000;
    const Stereo input = noise(kFrames, 0.5f, 43);
    Processor p({kRate, 4096, 45, Mode::Fx});
    p.setPublishing(true);
    for (uint8_t L = 0; L < 4; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 1.0f);
        p.setBase(slotOf(LayerParam::Density, L), 6.0f + 0.5f * L);
    }
    Log log;
    p.setEventSink(&Log::record, &log);
    Stereo out = silence(kFrames);
    uint64_t queued = 0;
    std::array<uint64_t, 4> queuedPerLayer{};
    render(p, &input, 0, kFrames, {256}, {}, out, &log, [&](int64_t) {
        gg::core::GrainVis g;
        while (p.grainQueue().pop(g))
        {
            ++queued;
            ++queuedPerLayer[g.layer];
        }
    });
    REQUIRE_FALSE(log.overflowed);
    CHECK(p.droppedGrainEvents() == 0);
    std::array<uint64_t, 4> perLayer{};
    std::array<int64_t, 4> lastIndex{-1, -1, -1, -1};
    for (std::size_t i = 0; i < log.events.size(); ++i)
    {
        const Log::Event &e = log.events[i];
        INFO("event " << i);
        REQUIRE(e.layer < 4);
        if (i > 0 && log.events[i - 1].call == e.call)
        {
            REQUIRE(log.events[i - 1].layer <= e.layer);
        }
        REQUIRE(static_cast<int64_t>(e.grain.index) > lastIndex[e.layer]);
        lastIndex[e.layer] = static_cast<int64_t>(e.grain.index);
        ++perLayer[e.layer];
    }
    // Every layer spawned, and the grain queue saw each one with its layer.
    CHECK(queued == log.events.size());
    for (size_t L = 0; L < 4; ++L)
    {
        INFO("layer " << L);
        CHECK(perLayer[L] > 50);
        CHECK(queuedPerLayer[L] == perLayer[L]);
    }
}

TEST_CASE("the unbuilt kinds fall back to the product's default", "[core][layers]")
{
    // Spec §6.4: Resample, Spectral and Oscillator render as Live in the FX and as Sample in the
    // instrument, the parameter keeping its value.
    float l[1];
    float r[1];
    float *out[2] = {l, r};
    for (const float kind : {2.0f, 3.0f, 4.0f})
    {
        INFO("Source " << kind);
        Processor fx({kRate, 512, 1, Mode::Fx});
        fx.setBase(ParamSlot::Source, 0.0f);
        fx.setBase(ParamSlot::B_Source, 0.0f);
        fx.process(nullptr, 0, out, 2, 1, {});
        REQUIRE(fx.engine(0).sourceMode() == gg::SourceMode::Sample);
        fx.setBase(ParamSlot::Source, kind);
        fx.setBase(ParamSlot::B_Source, kind);
        fx.process(nullptr, 0, out, 2, 1, {});
        CHECK(fx.engine(0).sourceMode() == gg::SourceMode::Live);
        CHECK(fx.engine(1).sourceMode() == gg::SourceMode::Live);
        CHECK(fx.base(ParamSlot::Source) == kind);

        Processor inst({kRate, 512, 1, Mode::Instrument});
        inst.setBase(ParamSlot::Source, 1.0f);
        inst.setBase(ParamSlot::D_Source, 1.0f);
        inst.process(nullptr, 0, out, 2, 1, {});
        REQUIRE(inst.engine(0).sourceMode() == gg::SourceMode::Live);
        inst.setBase(ParamSlot::Source, kind);
        inst.setBase(ParamSlot::D_Source, kind);
        inst.process(nullptr, 0, out, 2, 1, {});
        CHECK(inst.engine(0).sourceMode() == gg::SourceMode::Sample);
        CHECK(inst.engine(3).sourceMode() == gg::SourceMode::Sample);
        CHECK(inst.base(ParamSlot::Source) == kind);
    }
}

TEST_CASE("four layers are block-size invariant", "[core][layers][determinism]")
{
    // Every layer parameter automated off the grid, layers switched off and on, kinds switched,
    // the pad swept: the same bits, the same grains per layer and the same ring heads at any host
    // block size and maxBlockSize. (The sink's interleaving of the layers follows the pieces, so
    // the grains are compared layer by layer.)
    const Stereo input = noise(kInvarianceFrames, 0.5f, 49);
    const auto events = layerAutomation();

    LayersRender ref;
    renderLayers(input, events, 4096, {4096}, ref);
    REQUIRE(allFinite(ref.audio.left));
    REQUIRE(allFinite(ref.audio.right));
    REQUIRE(peak(ref.audio.left) > 0.05f);
    REQUIRE_FALSE(ref.log.overflowed);
    for (uint32_t L = 0; L < 4; ++L)
    {
        INFO("layer " << L);
        REQUIRE(ref.log.layer(L).size() > 10);
    }
    // Not vacuous: grains went to bus 2 (the automation moves Bus Split).
    REQUIRE(std::count_if(ref.log.events.begin(), ref.log.events.end(),
                          [](const Log::Event &e) { return e.grain.bus == 1; }) > 10);
    // Not vacuous: some layer went off for longer than its ramp and came back, so its engine was
    // re-enabled with a forced onset on a grid boundary.
    bool forcedOnset = false;
    for (const Log::Event &e : ref.log.events)
    {
        forcedOnset = forcedOnset || (e.grain.onsetSample > 0 && e.grain.onsetSample % 32 == 0 &&
                                      e.grain.onsetFrac == 0.0);
    }
    REQUIRE(forcedOnset);

    struct Split
    {
        uint32_t maxBlock;
        std::vector<uint32_t> blockSizes;
    };
    std::vector<Split> splits{{4096, {1}},    {4096, {7}},  {4096, {64}}, {4096, {511}},
                              {4096, {4096}}, {64, {4096}}, {1, {4096}},  {256, {4096}}};
    for (const uint64_t seed : {401, 402})
    {
        auto random = randomSplit(seed);
        REQUIRE(std::count(random.begin(), random.end(), 0u) > 0); // 0-frame calls
        splits.push_back({4096, std::move(random)});
    }
    for (const Split &split : splits)
    {
        INFO("maxBlockSize = " << split.maxBlock << ", first block size = "
                               << split.blockSizes.front() << " of " << split.blockSizes.size());
        LayersRender r;
        renderLayers(input, events, split.maxBlock, split.blockSizes, r);
        REQUIRE_FALSE(r.log.overflowed);
        for (uint32_t L = 0; L < 4; ++L)
        {
            INFO("layer " << L);
            REQUIRE(r.log.layer(L) == ref.log.layer(L));
            REQUIRE(r.heads[L] == ref.heads[L]);
        }
        REQUIRE(sameBits(ref.audio.left, r.audio.left));
        REQUIRE(sameBits(ref.audio.right, r.audio.right));
    }
}

TEST_CASE("four layers do not allocate", "[core][layers][rt]")
{
    // All four layers rendering, every layer slot moved (kinds switched, layers off and on,
    // Freeze, Feedback, the pad), publishing on, notes held and released, mono, stereo, missing
    // and four-channel buffers and 0- and 1-frame calls, all armed.
    Processor p({kRate, 256, 51, Mode::Fx});
    p.setPublishing(true);
    for (uint8_t L = 0; L < 4; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 1.0f);
        p.setBase(slotOf(LayerParam::Feedback, L), 0.4f);
        p.setBase(slotOf(LayerParam::Density, L), 7.0f);
        p.setBase(slotOf(LayerParam::Size, L), -4.0f);
        p.setSourceNow(L, sine(220.0 * (L + 1)));
    }
    p.setBase(ParamSlot::Mix, 1.0f);
    p.setBase(ParamSlot::ReverbAmount, 0.3f);
    Log log;
    p.setEventSink(&Log::record, &log);

    constexpr uint32_t kFrames = 1000;
    const Stereo input = noise(kFrames, 0.5f, 53);
    const float *in[2] = {input.left.data(), input.right.data()};
    std::vector<float> o0(kFrames), o1(kFrames), o2(kFrames), o3(kFrames);
    float *out[4] = {o0.data(), o1.data(), o2.data(), o3.data()};

    std::vector<ParamEvent> events;
    for (uint8_t L = 0; L < 4; ++L)
    {
        const auto at = [L](uint32_t base) { return static_cast<uint32_t>(base + 37 * L); };
        events.push_back({at(10), slotOf(LayerParam::Source, L), false, L % 2 == 0 ? 0.0 : 2.0});
        events.push_back({at(200), slotOf(LayerParam::Pitch, L), true, 3.0});
        events.push_back({at(300), gg::core::layerOnSlot(L), false, 0.0});
        events.push_back({at(320), gg::core::layerOnSlot(L), false, 1.0}); // within the ramp
        events.push_back({at(400), slotOf(LayerParam::Freeze, L), false, 1.0});
        events.push_back({at(500), slotOf(LayerParam::Diffusion, L), false, 0.7});
        events.push_back({at(600), gg::core::levelSlot(L), false, -6.0});
        events.push_back({at(700), slotOf(LayerParam::Source, L), false, 1.0});
        events.push_back({at(800), slotOf(LayerParam::PitchSpread, L), false, 5.0});
    }
    events.push_back({100, ParamSlot::MorphX, false, 0.1});
    events.push_back({900, ParamSlot::MorphY, false, 0.8});
    events.push_back({950, ParamSlot::LayerCOn, false, 0.0}); // off for good: disabled later
    std::stable_sort(events.begin(), events.end(),
                     [](const ParamEvent &a, const ParamEvent &b) { return a.offset < b.offset; });
    const std::array<ParamEvent, 2> atStart{
        {{0, ParamSlot::LayerCOn, false, 1.0}, {0, ParamSlot::B_Freeze, false, 0.0}}};
    const std::array<NoteEvent, 2> notes{
        {{50, NoteEventType::On, 0, 60}, {700, NoteEventType::Off, 0, 60}}};

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        p.process(in, 2, out, 2, kFrames, events, notes); // the first call
        for (int i = 0; i < 10; ++i)
        {
            p.process(in, 2, out, 2, 0, atStart);
            p.process(in, 2, out, 2, 1, atStart);
            p.process(in, 1, out, 2, kFrames, events, notes);
            p.process(nullptr, 0, out, 2, 100, atStart);
            p.process(in, 2, nullptr, 0, 100, atStart);
            p.process(in, 2, out, 4, kFrames, events);
            p.process(in, 2, out, 1, 100, atStart);
            gg::core::GrainVis g;
            while (p.grainQueue().pop(g))
            {
            }
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    REQUIRE(log.events.size() > 100);
    REQUIRE_FALSE(log.overflowed);
    for (uint32_t L = 0; L < 4; ++L)
    {
        INFO("layer " << L);
        CHECK_FALSE(log.layer(L).empty());
    }
    CHECK(allFinite(o0));
    if (AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

namespace
{
// A value for every slot, each moved from its default in `mode` (ruling FR-3's test): layers B
// and C on, D off, a Source of each built kind and the unbuilt Resample, both spreads up, every
// macro moved but Macro Freeze, and every other continuous slot at a point of the middle half of
// its range drawn from gg::rng, away from its default. No Freeze is loaded on, Macro Freeze
// included: there setBases arms by its own rule (ruling FR-8), not the calls'. With `nanEvery`
// not 0, every nanEvery-th slot is NaN instead.
gg::core::ParamValues movedValues(Mode mode, uint64_t seed, size_t nanEvery = 0)
{
    gg::core::ParamValues v{};
    for (size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        const auto &meta = gg::core::paramMeta(slot, mode);
        const double u = uniform(seed, s);
        float value =
            meta.minVal + (meta.maxVal - meta.minVal) * static_cast<float>(0.25 + 0.5 * u);
        if (meta.type == sst::basic_blocks::params::ParamMetaData::INT)
        {
            value = std::floor(value);
        }
        if (value == meta.defaultVal)
        {
            value = meta.minVal + (meta.maxVal - meta.minVal) * 0.3f;
        }
        v[s] = value;
    }
    const auto set = [&v](ParamSlot slot, float value) { v[static_cast<size_t>(slot)] = value; };
    set(ParamSlot::LayerAOn, 1.0f);
    set(ParamSlot::LayerBOn, 1.0f);
    set(ParamSlot::LayerCOn, 1.0f);
    set(ParamSlot::LayerDOn, 0.0f);
    for (uint8_t L = 0; L < 4; ++L)
    {
        set(slotOf(LayerParam::Freeze, L), 0.0f);
    }
    set(ParamSlot::Macro6, 0.0f);
    // A Sample, B Live (the other product's default for one of them), C the unbuilt Resample.
    set(slotOf(LayerParam::Source, 0), 0.0f);
    set(slotOf(LayerParam::Source, 1), 1.0f);
    set(slotOf(LayerParam::Source, 2), 2.0f);
    set(slotOf(LayerParam::Source, 3), 1.0f);
    for (uint8_t L = 0; L < 4; ++L)
    {
        set(slotOf(LayerParam::PitchSpread, L), 5.0f + L);
        set(slotOf(LayerParam::SizeSpread, L), 0.5f + 0.25f * L);
        set(slotOf(LayerParam::Density, L), 7.0f); // 128 Hz: grains within the 4 pieces
        set(slotOf(LayerParam::Size, L), -4.0f);
        set(slotOf(LayerParam::Position, L), 0.1f); // a Live layer reads the last 0.05 s or so
        set(gg::core::levelSlot(L), -6.0f);
    }
    // Near the pad's middle once the transport sources' routes have moved it (Bar to Morph Y
    // -0.3 while stopped), so every layer on is heard.
    set(ParamSlot::MorphX, 0.6f);
    set(ParamSlot::MorphY, 0.8f);
    set(ParamSlot::Macro1, 0.8f);
    set(ParamSlot::Mix, 0.9f);
    // Macro 8 a little below rest: the FX's Mix stays below 1, so a fresh FX, whose rings hold
    // silence, still sounds its dry input.
    set(ParamSlot::Macro8, 0.45f);
    if (nanEvery != 0)
    {
        for (size_t s = 0; s < gg::core::kParamCount; s += nanEvery)
        {
            v[s] = kNaN;
        }
    }
    return v;
}

// Two Processors alike, one given `values` by setBase on every slot in slot order, the other by
// setBases, after `preRoll` frames (with a Mix event left mid-ramp at its end); then 4 pieces of
// 256. The output, every base and every effective value must match bit for bit. With
// `macroFreezeFirst`, Macro Freeze is on from construction, so every layer is frozen but none is
// armed while the pre-roll fills the rings.
void requireSetBasesMatchesSetBase(Mode mode, const gg::core::ParamValues &values, int64_t preRoll,
                                   bool macroFreezeFirst = false)
{
    constexpr int64_t kPieces = 4 * 256;
    const int64_t frames = preRoll + kPieces;
    const Stereo input = noise(frames, 0.5f, 61);
    const Stereo *in = mode == Mode::Fx ? &input : nullptr;
    // The instrument's notes; the FX's held key would freeze every layer.
    const std::vector<TimedNote> notes =
        mode == Mode::Instrument ? std::vector<TimedNote>{{0, NoteEventType::On, 0, 60},
                                                          {preRoll + 300, NoteEventType::On, 0, 67}}
                                 : std::vector<TimedNote>{};
    const std::vector<TimedEvent> preRollEvents{
        {preRoll > 0 ? preRoll - 200 : 0, ParamSlot::Mix, false, 0.2}};
    const auto make = [&]() {
        auto p = std::make_unique<Processor>(gg::core::ProcessorConfig{kRate, 256, 71, mode});
        p->setRoutes(allSourceRoutes(mode));
        if (macroFreezeFirst)
        {
            p->setBase(ParamSlot::Macro6, 1.0f);
        }
        for (uint8_t L = 0; L < 4; ++L)
        {
            p->setSourceNow(L, sine(110.0 * (L + 1)));
        }
        return p;
    };
    auto one = make();
    auto all = make();
    Stereo outOne = silence(frames);
    Stereo outAll = silence(frames);
    if (preRoll > 0)
    {
        render(*one, in, 0, preRoll, {256}, preRollEvents, outOne, nullptr, {}, notes);
        render(*all, in, 0, preRoll, {256}, preRollEvents, outAll, nullptr, {}, notes);
    }
    for (size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        one->setBase(static_cast<ParamSlot>(s), values[s]);
    }
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        all->setBases(values);
    }
    const auto allocations = AllocTripwire::count();
    for (size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        INFO("slot " << s << " " << gg::core::paramMeta(slot, mode).name);
        REQUIRE(bitsOf(one->base(slot)) == bitsOf(all->base(slot)));
        REQUIRE(bitsOf(one->effective(slot)) == bitsOf(all->effective(slot)));
    }
    render(*one, in, preRoll, frames, {256}, {}, outOne, nullptr, {}, notes);
    render(*all, in, preRoll, frames, {256}, {}, outAll, nullptr, {}, notes);
    REQUIRE(peakIn(outOne.left, preRoll, frames) > 1e-3f);
    REQUIRE(sameBits(outOne.left, outAll.left));
    REQUIRE(sameBits(outOne.right, outAll.right));
    for (uint8_t L = 0; L < 4; ++L)
    {
        INFO("layer " << int{L});
        REQUIRE(bitsOf(one->feedbackAt(L, frames)) == bitsOf(all->feedbackAt(L, frames)));
        REQUIRE(one->engine(L).liveWriteHead() == all->engine(L).liveWriteHead()); // frozen alike
    }
    if (AllocTripwire::available()) // off under ASan; RTSan covers the audio thread's calls
    {
        REQUIRE(allocations == 0);
    }
}
} // namespace

TEST_CASE("setBases is setBase on every slot in slot order", "[core][layers]")
{
    // Ruling FR-3: a state load (an offer's values, the plugin's activation and loaded state)
    // recomputes the effective values once. For a load with no Freeze on, its result is the 258
    // calls' bit for bit: on fresh Processors, after a pre-roll with a ramp running, and back to
    // the defaults again. (A load with Freeze on arms by ruling FR-8: the next test.)
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        INFO((mode == Mode::Fx ? "fx" : "instrument"));
        const gg::core::ParamValues moved = movedValues(mode, 83);
        requireSetBasesMatchesSetBase(mode, moved, 0);
        requireSetBasesMatchesSetBase(mode, moved, 2 * 256);
        gg::core::ParamValues defaults{};
        for (size_t s = 0; s < gg::core::kParamCount; ++s)
        {
            defaults[s] = gg::core::paramMeta(static_cast<ParamSlot>(s), mode).defaultVal;
        }
        requireSetBasesMatchesSetBase(mode, defaults, 2 * 256);
        // Every layer frozen by Macro Freeze but none armed, the rings full, then a load that
        // turns Macro Freeze off: every layer is seen off and arms, and the rings play live.
        requireSetBasesMatchesSetBase(mode, moved, 24000, true);
        // A NaN leaves its slot alone, as setBase does.
        requireSetBasesMatchesSetBase(mode, movedValues(mode, 89, 7), 2 * 256);
    }
}

namespace
{
// Renders the next 2048 frames of `input` (the FX's input, the instrument's sidechain) through p
// from `clock`, in pieces of 256, and returns which layers' rings froze: a frozen ring's head
// stops after the 20 ms ramp, so it advances less than the frames rendered.
std::array<bool, 4> frozenAfter(Processor &p, const Stereo &input, int64_t &clock,
                                const std::vector<TimedNote> &notes = {})
{
    constexpr int64_t kFrames = 2048;
    std::array<int64_t, 4> before{};
    for (uint8_t L = 0; L < 4; ++L)
    {
        before[L] = p.engine(L).liveWriteHead();
    }
    Stereo out = silence(clock + kFrames);
    render(p, &input, clock, clock + kFrames, {256}, {}, out, nullptr, {}, notes);
    clock += kFrames;
    std::array<bool, 4> frozen{};
    for (uint8_t L = 0; L < 4; ++L)
    {
        frozen[L] = p.engine(L).liveWriteHead() - before[L] < kFrames;
    }
    return frozen;
}
} // namespace

TEST_CASE("a state load arms Freeze from its loaded values alone, every layer alike", "[core]")
{
    // Ruling FR-8 (INTENT §23 #40): a Freeze that is on when a state loads into a fresh ring stays
    // live until it is toggled, in every layer alike, so the effect is heard after a reload. The
    // calls one slot at a time would have armed layers B to D at layer A's Freeze, from their old
    // values, and frozen their empty rings.
    using Frozen = std::array<bool, 4>;
    constexpr Frozen kNone{false, false, false, false};
    const Stereo input = noise(5 * 2048, 0.5f, 97);
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        INFO((mode == Mode::Fx ? "fx" : "instrument"));
        const auto defaults = [mode] {
            gg::core::ParamValues v{};
            for (size_t s = 0; s < gg::core::kParamCount; ++s)
            {
                v[s] = gg::core::paramMeta(static_cast<ParamSlot>(s), mode).defaultVal;
            }
            return v;
        }();

        // Every layer's Freeze on: no layer freezes; toggling layer B's freezes it alone.
        {
            gg::core::ParamValues v = defaults;
            for (uint8_t L = 0; L < 4; ++L)
            {
                v[static_cast<size_t>(slotOf(LayerParam::Freeze, L))] = 1.0f;
            }
            Processor p({kRate, 256, 101, mode});
            p.setBases(v);
            int64_t clock = 0;
            REQUIRE(frozenAfter(p, input, clock) == kNone);
            p.setBase(slotOf(LayerParam::Freeze, 1), 0.0f);
            p.setBase(slotOf(LayerParam::Freeze, 1), 1.0f);
            REQUIRE(frozenAfter(p, input, clock) == Frozen{false, true, false, false});
        }

        // Macro Freeze on instead: no layer freezes; toggling Macro Freeze freezes them all.
        {
            gg::core::ParamValues v = defaults;
            v[static_cast<size_t>(ParamSlot::Macro6)] = 1.0f;
            Processor p({kRate, 256, 101, mode});
            p.setBases(v);
            for (uint8_t L = 0; L < 4; ++L)
            {
                REQUIRE(p.effective(slotOf(LayerParam::Freeze, L)) == 1.0f);
            }
            int64_t clock = 0;
            REQUIRE(frozenAfter(p, input, clock) == kNone);
            p.setBase(ParamSlot::Macro6, 0.0f);
            p.setBase(ParamSlot::Macro6, 1.0f);
            REQUIRE(frozenAfter(p, input, clock) == Frozen{true, true, true, true});
        }

        // The same with a NaN in the values (the slot-by-slot path, setBase()'s own).
        {
            gg::core::ParamValues v = defaults;
            for (uint8_t L = 0; L < 4; ++L)
            {
                v[static_cast<size_t>(slotOf(LayerParam::Freeze, L))] = 1.0f;
            }
            v[static_cast<size_t>(ParamSlot::ReverbSize)] = kNaN;
            Processor p({kRate, 256, 101, mode});
            p.setBases(v);
            int64_t clock = 0;
            REQUIRE(frozenAfter(p, input, clock) == kNone);
        }

        // A layer whose loaded Freeze is off arms: turned on afterwards, it freezes.
        {
            Processor p({kRate, 256, 101, mode});
            p.setBases(defaults);
            p.setBase(slotOf(LayerParam::Freeze, 2), 1.0f);
            int64_t clock = 0;
            REQUIRE(frozenAfter(p, input, clock) == Frozen{false, false, true, false});
        }
    }

    // The FX: a held key freezes every layer, with no arming, after such a load too.
    {
        gg::core::ParamValues v{};
        for (size_t s = 0; s < gg::core::kParamCount; ++s)
        {
            v[s] = gg::core::paramMeta(static_cast<ParamSlot>(s), Mode::Fx).defaultVal;
        }
        for (uint8_t L = 0; L < 4; ++L)
        {
            v[static_cast<size_t>(slotOf(LayerParam::Freeze, L))] = 1.0f;
        }
        Processor p({kRate, 256, 101, Mode::Fx});
        p.setBases(v);
        int64_t clock = 0;
        REQUIRE(frozenAfter(p, input, clock, {{0, NoteEventType::On, 0, 60}}) ==
                Frozen{true, true, true, true});
    }
}
