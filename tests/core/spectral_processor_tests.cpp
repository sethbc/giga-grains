// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Spectral layers in the Processor (M2 sub-project 4's spec §6.2, §6.8): Source kind 3 in both
// products, its material (the layer's sample, else its live ring) and the units its Position
// takes, Freeze, Feedback, a kind switch's click score and the real-time contract.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <span>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/LayersFixture.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Processor.h"
#include "gg/core/Rng.h"
#include "gg/metrics/Metrics.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/SampleBuffer.h"
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
constexpr float kSpectralKind = 3.0f;
constexpr float kClickLimit = 15.0f;

ParamSlot A(LayerParam p) { return layerSlot(p, 0); }

float clickScore(const Stereo &s)
{
    const gg::metrics::AudioData audio{kRate, {s.left, s.right}};
    return static_cast<float>(gg::metrics::computeMetrics(audio).clickScore);
}

// A 1 s stereo noise sample in ±0.5.
std::shared_ptr<const gg::PreparedSource> noiseSample()
{
    constexpr int64_t kFrames = 48000;
    auto buf = std::make_shared<gg::SampleBuffer>(2, kFrames, kRate);
    for (uint32_t c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < kFrames; ++i)
        {
            buf->channel(c)[i] =
                0.5f * static_cast<float>(gg::rng::bipolar(gg::rng::hash(
                           {78, 0, c, gg::rng::Field::SignalNoise, static_cast<uint64_t>(i)})));
        }
    }
    return gg::PreparedSource::make(std::move(buf));
}

// The FX with layer A Spectral at Mix 1, 64 grains a second.
struct FxRig
{
    Processor p;
    explicit FxRig(uint32_t maxBlock = 512) : p({kRate, maxBlock, 83, Mode::Fx})
    {
        p.setBase(ParamSlot::Mix, 1.0f);
        p.setBase(A(LayerParam::Source), kSpectralKind);
        p.setBase(A(LayerParam::Density), 6.0f);
    }
    Stereo run(const Stereo *input, int64_t frames, const std::vector<TimedEvent> &events = {})
    {
        Stereo out = silence(frames);
        render(p, input, 0, frames, {512}, events, out);
        return out;
    }
};
} // namespace

TEST_CASE("an fx spectral layer smears the input", "[core][spectral]")
{
    constexpr int64_t kFrames = 24000;
    {
        FxRig rig;
        const Stereo input = noise(kFrames, 0.5f, 85);
        const Stereo out = rig.run(&input, kFrames);
        CHECK(rig.p.engine(0).sourceMode() == gg::SourceMode::Spectral);
        CHECK(rig.p.layerMaterial(0) == gg::SourceMode::Live);
        CHECK(allFinite(out.left));
        CHECK(allFinite(out.right));
        CHECK(peakIn(out.left, 12000, kFrames) > 1.0e-4f);
        CHECK(peakIn(out.right, 12000, kFrames) > 1.0e-4f);
    }
    {
        FxRig rig;
        const Stereo input = silence(kFrames);
        const Stereo out = rig.run(&input, kFrames);
        CHECK(peak(out.left) == 0.0f);
        CHECK(peak(out.right) == 0.0f);
    }
}

TEST_CASE("an instrument spectral layer smears its sample", "[core][spectral]")
{
    Processor p({kRate, 512, 84, Mode::Instrument});
    p.setBase(A(LayerParam::Source), kSpectralKind);
    p.setBase(A(LayerParam::Density), 6.0f);
    p.setSourceNow(0, noiseSample());
    CHECK(p.layerMaterial(0) == gg::SourceMode::Sample);
    constexpr int64_t kFrames = 24000;
    Stereo out = silence(kFrames);
    const std::array<NoteEvent, 1> note{{{0, NoteEventType::On, 0, 60}}};
    float *o[2] = {out.left.data(), out.right.data()};
    // One call per block of 512, the note in the first.
    for (int64_t done = 0; done < kFrames; done += 512)
    {
        const auto n = static_cast<uint32_t>(std::min<int64_t>(512, kFrames - done));
        float *ob[2] = {o[0] + done, o[1] + done};
        p.process(nullptr, 0, ob, 2, n, {},
                  done == 0 ? std::span<const NoteEvent>(note) : std::span<const NoteEvent>());
    }
    CHECK(p.engine(0).sourceMode() == gg::SourceMode::Spectral);
    CHECK(allFinite(out.left));
    CHECK(peakIn(out.left, 12000, kFrames) > 1.0e-4f);
}

TEST_CASE("a sample handed to a spectral layer changes the material and the units",
          "[core][spectral]")
{
    FxRig rig;
    rig.p.setBase(A(LayerParam::Position), 0.25f);
    constexpr int64_t kFrames = 12000;
    const Stereo input = noise(kFrames, 0.5f, 86);
    rig.run(&input, kFrames);
    // The ring material: the live law's seconds, (16/255)(2^(8 x 0.25) - 1).
    CHECK(rig.p.layerMaterial(0) == gg::SourceMode::Live);
    const double ringSeconds = 16.0 / 255.0 * (std::exp2(8.0 * 0.25) - 1.0);
    CHECK(rig.p.engine(0).cloudParams().position == Catch::Approx(ringSeconds).epsilon(1e-12));
    CHECK_FALSE(rig.p.engine(0).cloudParams().sprayInSeconds);

    rig.p.setSourceNow(0, noiseSample());
    CHECK(rig.p.layerMaterial(0) == gg::SourceMode::Sample);
    CHECK(rig.p.engine(0).cloudParams().position == 0.25); // a fraction of the sample
    CHECK(rig.p.engine(0).cloudParams().sprayInSeconds);
    const Stereo withSample = rig.run(&input, kFrames);
    CHECK(allFinite(withSample.left));
    CHECK(peakIn(withSample.left, 6000, kFrames) > 1.0e-4f);

    rig.p.setSourceNow(0, nullptr);
    CHECK(rig.p.layerMaterial(0) == gg::SourceMode::Live);
    CHECK(rig.p.engine(0).cloudParams().position == Catch::Approx(ringSeconds).epsilon(1e-12));
    CHECK_FALSE(rig.p.engine(0).cloudParams().sprayInSeconds);
    const Stereo back = rig.run(&input, kFrames);
    CHECK(allFinite(back.left));
    CHECK(allFinite(back.right));
    CHECK(peakIn(back.left, 6000, kFrames) > 1.0e-4f);
}

TEST_CASE("an offered sample reaches a spectral layer at the next call", "[core][spectral]")
{
    FxRig rig;
    rig.p.setBase(A(LayerParam::Position), 0.25f);
    constexpr int64_t kFrames = 2048;
    const Stereo input = noise(kFrames, 0.5f, 87);
    rig.run(&input, kFrames);
    REQUIRE(rig.p.layerMaterial(0) == gg::SourceMode::Live);
    rig.p.sources(0).offer(noiseSample());
    rig.run(&input, 512);
    CHECK(rig.p.layerMaterial(0) == gg::SourceMode::Sample);
    CHECK(rig.p.engine(0).cloudParams().position == 0.25);
}

TEST_CASE("freeze holds a spectral layer's material", "[core][spectral]")
{
    // Ring material: Freeze on stops the live ring's head, while the smear's own ring goes on
    // taking frames (it holds what the frozen ring plays back).
    FxRig rig;
    constexpr int64_t kFrames = 24000;
    const Stereo input = noise(2 * kFrames, 0.5f, 88);
    Stereo out = silence(kFrames);
    render(rig.p, &input, 0, kFrames, {512}, {}, out);
    const int64_t liveBefore = rig.p.engine(0).liveWriteHead();
    CHECK(liveBefore == kFrames);

    // Freeze takes hold within the first calls after it is set (its ramp); from there on the ring
    // holds.
    rig.p.setBase(A(LayerParam::Freeze), 1.0f);
    Stereo more = silence(2 * kFrames);
    render(rig.p, &input, kFrames, kFrames + 4096, {512}, {}, more);
    const int64_t liveHeld = rig.p.engine(0).liveWriteHead();
    const int64_t smearBefore2 = rig.p.engine(0).spectral()->ring().head();
    CHECK(liveHeld < liveBefore + 4096);
    render(rig.p, &input, kFrames + 4096, 2 * kFrames, {512}, {}, more);
    CHECK(rig.p.engine(0).liveWriteHead() == liveHeld);
    CHECK(rig.p.engine(0).spectral()->ring().head() > smearBefore2);
}

TEST_CASE("a spectral layer takes no feedback", "[core][spectral]")
{
    // Feedback acts on a Live layer alone: the bytes at Feedback 0.9 are the bytes at 0.
    constexpr int64_t kFrames = 24000;
    const Stereo input = noise(kFrames, 0.5f, 89);
    FxRig off;
    const Stereo a = off.run(&input, kFrames);
    FxRig on;
    on.p.setBase(A(LayerParam::Feedback), 0.9f);
    const Stereo b = on.run(&input, kFrames);
    CHECK(sameBits(a.left, b.left));
    CHECK(sameBits(a.right, b.right));
    CHECK(peak(a.left) > 1.0e-4f);
}

TEST_CASE("switching a layer to spectral and back does not click", "[core][spectral]")
{
    constexpr int64_t kFrames = 3 * 48000;
    FxRig rig;
    rig.p.setBase(A(LayerParam::Source), 1.0f);
    rig.p.setBase(A(LayerParam::Density), 5.0f);
    Stereo input = silence(kFrames);
    for (int64_t i = 0; i < kFrames; ++i)
    {
        const auto j = static_cast<std::size_t>(i);
        // Faded in over 10 ms (a raised cosine), as gg-render fades an FX input (ruling M1-v), so
        // the input's own edge isn't the click.
        const double fade = std::min(1.0, static_cast<double>(i) / (0.01 * kRate));
        const double shape = std::sin(0.5 * std::numbers::pi * fade);
        input.left[j] = input.right[j] = static_cast<float>(
            0.4 * shape * shape *
            std::sin(2.0 * std::numbers::pi * 330.0 * static_cast<double>(i) / kRate));
    }
    const std::vector<TimedEvent> events{{48000, A(LayerParam::Source), false, 3.0},
                                         {2 * 48000, A(LayerParam::Source), false, 1.0}};
    const Stereo out = rig.run(&input, kFrames, events);
    CHECK(allFinite(out.left));
    CHECK(clickScore(out) < kClickLimit);
    CHECK(peakIn(out.left, 48000, 2 * 48000) > 1.0e-3f);
}

TEST_CASE("a spectral layer alongside the others does not allocate", "[core][spectral][rt]")
{
    Processor p({kRate, 256, 90, Mode::Fx});
    for (uint8_t L = 0; L < 4; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 1.0f);
        p.setBase(layerSlot(LayerParam::Density, L), 7.0f);
        p.setBase(layerSlot(LayerParam::Source, L), L == 0 || L == 2 ? kSpectralKind : 1.0f);
        p.setBase(layerSlot(LayerParam::BusSplit, L), 0.5f);
    }
    p.setSourceNow(2, noiseSample());
    p.setBase(ParamSlot::Mix, 1.0f);
    constexpr uint32_t kFrames = 4000;
    const Stereo input = noise(kFrames, 0.5f, 91);
    const float *in[2] = {input.left.data(), input.right.data()};
    std::vector<float> o0(kFrames), o1(kFrames);
    float *out[2] = {o0.data(), o1.data()};
    std::vector<ParamEvent> events;
    for (uint32_t t = 32; t < kFrames; t += 96)
    {
        events.push_back({t, layerSlot(LayerParam::Source, 1), false, double((t / 96) % 4)});
        events.push_back({t, layerSlot(LayerParam::Position, 0), false, (t % 7) / 7.0});
        events.push_back({t, layerSlot(LayerParam::Position, 2), false, (t % 5) / 5.0});
    }
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        p.process(in, 2, out, 2, kFrames, events);
        for (int i = 0; i < 10; ++i)
        {
            p.process(in, 2, out, 2, 0, {});
            p.process(in, 2, out, 2, 1, {});
            p.process(in, 1, out, 1, 100, {});
            p.process(nullptr, 0, out, 2, 33, {});
        }
    }
    const uint64_t allocations = AllocTripwire::count();
    if (AllocTripwire::available())
    {
        REQUIRE(allocations == 0);
    }
    REQUIRE(allFinite(o0));
}
