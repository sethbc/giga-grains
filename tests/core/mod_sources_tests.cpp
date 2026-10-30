// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The modulation sources (M2 spec §6.8): the LFO and its deterministic sine, the ADSR, the random
// source, the step sequencer, the envelope follower and the transport, each a small state machine
// stepped once per 32-sample grid cell.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gg/core/ModSources.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Rng.h"
#include "support/AllocTripwire.h"

using gg::core::Adsr;
using gg::core::AdsrParams;
using gg::core::EnvFollower;
using gg::core::LayerModulators;
using gg::core::Lfo;
using gg::core::LfoParams;
using gg::core::LfoShape;
using gg::core::RandomParams;
using gg::core::RandomSource;
using gg::core::StepParams;
using gg::core::StepSequencer;
using gg::core::Transport;
using gg::core::TransportSource;
using gg::test::AllocTripwire;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kCell = 32.0 / kSampleRate;

float drawn(uint64_t seed, uint32_t layer, gg::rng::Field field, uint64_t k)
{
    return static_cast<float>(gg::rng::uniform01(gg::rng::hash({seed, layer, 0, field, k})));
}
} // namespace

TEST_CASE("sineDet is exact at the quarter points and close elsewhere", "[core][mod]")
{
    CHECK(gg::core::sineDet(0.0f) == 0.0f);
    CHECK(gg::core::sineDet(0.25f) == 1.0f);
    CHECK(gg::core::sineDet(0.5f) == 0.0f);
    CHECK(gg::core::sineDet(0.75f) == -1.0f);

    // The polynomial's coefficients sum to exactly 1 in float, so x = 1 (phase 0.25) gives 1.
    float sum = 0.0f;
    for (std::size_t i = gg::core::detail::kSineDetCoeffs.size(); i-- > 0;)
    {
        sum += gg::core::detail::kSineDetCoeffs[i];
    }
    CHECK(sum == 1.0f);

    double worst = 0.0;
    float lo = 0.0f;
    float hi = 0.0f;
    constexpr int kPoints = 10000;
    for (int i = 0; i < kPoints; ++i)
    {
        const float p = static_cast<float>(i) / static_cast<float>(kPoints);
        const float s = gg::core::sineDet(p);
        const double ref = std::sin(2.0 * std::numbers::pi * static_cast<double>(p));
        worst = std::max(worst, std::abs(static_cast<double>(s) - ref));
        lo = std::min(lo, s);
        hi = std::max(hi, s);
    }
    CHECK(worst < 1e-4);
    // Never outside [-1, 1], so the LFO's Sine stays in [0, 1].
    CHECK(lo == -1.0f);
    CHECK(hi == 1.0f);
    // Odd symmetry: the second half is the first half negated, bit for bit (q − 0.5 is exact).
    for (int i = 0; i < kPoints / 2; ++i)
    {
        const float q = 0.5f + static_cast<float>(i) / static_cast<float>(kPoints);
        CHECK(gg::core::sineDet(q) == -gg::core::sineDet(q - 0.5f));
    }
}

TEST_CASE("the lfo shapes at known phases", "[core][mod]")
{
    // At 1 Hz a 0.25 s cell is a quarter period: the phases are exactly 0, 0.25, 0.5 and 0.75.
    struct Expected
    {
        LfoShape shape;
        std::array<float, 4> values;
    };
    const std::array<Expected, 5> cases{{
        {LfoShape::Sine, {0.5f, 1.0f, 0.5f, 0.0f}},
        {LfoShape::Triangle, {0.0f, 0.5f, 1.0f, 0.5f}},
        {LfoShape::SawUp, {0.0f, 0.25f, 0.5f, 0.75f}},
        {LfoShape::SawDown, {1.0f, 0.75f, 0.5f, 0.25f}},
        {LfoShape::Square, {1.0f, 1.0f, 0.0f, 0.0f}},
    }};
    for (const auto &c : cases)
    {
        Lfo lfo;
        lfo.reset();
        const LfoParams params{1.0f, c.shape};
        for (int period = 0; period < 3; ++period)
        {
            for (std::size_t q = 0; q < 4; ++q)
            {
                INFO("shape " << static_cast<int>(c.shape) << " period " << period << " quarter "
                              << q);
                CHECK(lfo.step(params, 0.25) == c.values[q]);
            }
        }
        // reset() puts the phase back at 0.
        lfo.step(params, 0.25);
        lfo.reset();
        CHECK(lfo.phase() == 0.0);
        CHECK(lfo.step(params, 0.25) == c.values[0]);
    }
}

TEST_CASE("a rate change does not jump the phase", "[core][mod]")
{
    Lfo lfo;
    lfo.reset();
    const LfoParams slow{1.0f, LfoShape::SawUp};
    const LfoParams fast{2.0f, LfoShape::SawUp};
    for (int i = 0; i < 10; ++i)
    {
        lfo.step(slow, kCell);
    }
    const double atChange = lfo.phase();
    CHECK(atChange == Catch::Approx(10.0 * kCell).margin(1e-12));
    for (int i = 0; i < 25; ++i)
    {
        // The value is the phase reached, not a phase restarted for the new rate.
        CHECK(lfo.step(fast, kCell) ==
              Catch::Approx(atChange + 2.0 * kCell * static_cast<double>(i)).margin(1e-6));
    }
    CHECK(lfo.phase() == Catch::Approx(atChange + 25.0 * 2.0 * kCell).margin(1e-12));

    // A rate past one cycle per cell still wraps into [0, 1).
    Lfo wild;
    wild.reset();
    for (int i = 0; i < 100; ++i)
    {
        const float v = wild.step({1e6f, LfoShape::SawUp}, kCell);
        CHECK(v >= 0.0f);
        CHECK(v <= 1.0f);
        CHECK(wild.phase() >= 0.0);
        CHECK(wild.phase() < 1.0);
    }
}

TEST_CASE("the adsr's segments", "[core][mod]")
{
    // A 1/64 s cell, so the times below are exact in float: attack 10 cells, decay 10 cells to
    // 0.5, release 20 cells.
    constexpr double cell = 1.0 / 64.0;
    const AdsrParams params{static_cast<float>(10.0 * cell), static_cast<float>(10.0 * cell), 0.5f,
                            static_cast<float>(20.0 * cell)};
    Adsr env;
    env.reset();
    CHECK(env.step(params, false, cell) == 0.0f); // idle
    // Each step returns the level at the start of its cell, then advances.
    std::vector<float> attack;
    for (int i = 0; i <= 10; ++i)
    {
        attack.push_back(env.step(params, true, cell));
    }
    CHECK(attack[0] == 0.0f);
    for (int i = 1; i < 10; ++i)
    {
        CHECK(attack[static_cast<std::size_t>(i)] ==
              Catch::Approx(0.1 * static_cast<double>(i)).margin(1e-5));
        CHECK(attack[static_cast<std::size_t>(i)] > attack[static_cast<std::size_t>(i - 1)]);
    }
    CHECK(attack[10] == 1.0f); // the top, after 10 cells
    // Decay: from 1 to 0.5 in 10 cells.
    for (int i = 1; i <= 10; ++i)
    {
        const float v = env.step(params, true, cell);
        if (i < 10)
        {
            CHECK(v == Catch::Approx(1.0 - 0.05 * static_cast<double>(i)).margin(1e-5));
        }
        else
        {
            CHECK(v == 0.5f);
        }
    }
    // Sustain: held.
    for (int i = 0; i < 100; ++i)
    {
        CHECK(env.step(params, true, cell) == 0.5f);
    }
    // Release: from 0.5 to 0 in 20 cells, then idle.
    CHECK(env.step(params, false, cell) == 0.5f);
    for (int i = 1; i <= 20; ++i)
    {
        const float v = env.step(params, false, cell);
        if (i < 20)
        {
            CHECK(v == Catch::Approx(0.5 - 0.025 * static_cast<double>(i)).margin(1e-5));
        }
        else
        {
            CHECK(v == 0.0f);
        }
    }
    CHECK(env.step(params, false, cell) == 0.0f);

    SECTION("gate off mid-attack releases from the current level")
    {
        Adsr e;
        e.reset();
        for (int i = 0; i < 5; ++i)
        {
            e.step(params, true, cell);
        }
        const float level = e.step(params, false, cell); // 0.5: five attack steps taken
        CHECK(level == Catch::Approx(0.5).margin(1e-5));
        // Release from 0.5 over 20 cells: 0.025 a cell, the same slope as from the sustain.
        std::vector<float> release;
        float v = level;
        int cells = 0;
        while (v > 0.0f && cells < 100)
        {
            ++cells;
            v = e.step(params, false, cell);
            release.push_back(v);
        }
        CHECK(cells == 20);
        CHECK(release[0] == Catch::Approx(0.475).margin(1e-5));
    }

    SECTION("a retrigger attacks from the current level, and times shorter than a cell clamp")
    {
        Adsr e;
        e.reset();
        const AdsrParams instant{0.0f, 0.0f, 0.25f, -1.0f};
        CHECK(e.step(instant, true, cell) == 0.0f);
        CHECK(e.step(instant, true, cell) == 1.0f);  // a one-cell attack
        CHECK(e.step(instant, true, cell) == 0.25f); // a one-cell decay
        CHECK(e.step(instant, false, cell) == 0.25f);
        CHECK(e.step(instant, false, cell) == 0.0f); // a one-cell release
        const AdsrParams nan{std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::quiet_NaN(), 0.5f,
                             std::numeric_limits<float>::quiet_NaN()};
        CHECK(e.step(nan, true, cell) == 0.0f);
        CHECK(e.step(nan, true, cell) == 1.0f);
    }
}

TEST_CASE("random values are keyed and the glide follows", "[core][mod]")
{
    constexpr uint64_t kSeed = 0x5EED;
    const RandomParams jump{1.0f, 0.0f};

    SECTION("the same key gives the same sequence; another field or layer another")
    {
        RandomSource a;
        RandomSource b;
        RandomSource otherField;
        RandomSource otherLayer;
        a.reset();
        b.reset();
        otherField.reset();
        otherLayer.reset();
        const RandomParams params{37.0f, 0.3f};
        int fieldDiffers = 0;
        int layerDiffers = 0;
        for (int i = 0; i < 1000; ++i)
        {
            const float va = a.step(params, kCell, kSeed, 1, gg::rng::Field::ModRandom1);
            const float vb = b.step(params, kCell, kSeed, 1, gg::rng::Field::ModRandom1);
            const float vf = otherField.step(params, kCell, kSeed, 1, gg::rng::Field::ModRandom2);
            const float vl = otherLayer.step(params, kCell, kSeed, 2, gg::rng::Field::ModRandom1);
            CHECK(va == vb);
            CHECK(va >= 0.0f);
            CHECK(va <= 1.0f);
            fieldDiffers += va != vf ? 1 : 0;
            layerDiffers += va != vl ? 1 : 0;
        }
        CHECK(fieldDiffers > 900);
        CHECK(layerDiffers > 900);
    }

    SECTION("smooth 0 jumps to each new value, one every 1/rate s")
    {
        RandomSource r;
        r.reset();
        // At 1 Hz a 0.25 s cell is a quarter period: a new value every 4 cells.
        for (uint64_t k = 0; k < 8; ++k)
        {
            for (int q = 0; q < 4; ++q)
            {
                INFO("step " << k << " cell " << q);
                CHECK(r.step(jump, 0.25, kSeed, 0, gg::rng::Field::ModRandom1) ==
                      drawn(kSeed, 0, gg::rng::Field::ModRandom1, k));
            }
        }
        // reset() starts the sequence again from k = 0.
        r.reset();
        CHECK(r.step(jump, 0.25, kSeed, 0, gg::rng::Field::ModRandom1) ==
              drawn(kSeed, 0, gg::rng::Field::ModRandom1, 0));
    }

    SECTION("the glide's time constant is smooth / rate")
    {
        // A 1/64 s cell at 1 Hz: 64 cells a period, each gliding by cell × rate / smooth.
        constexpr double cell = 1.0 / 64.0;
        auto remaining = [&](float smooth) {
            RandomSource r;
            r.reset();
            const RandomParams params{1.0f, smooth};
            std::vector<float> out;
            for (int i = 0; i < 64 * 12; ++i)
            {
                out.push_back(r.step(params, cell, kSeed, 3, gg::rng::Field::ModRandom2));
            }
            // Over each full period k (cells 64k − 1 to 64k + 63) the output glides toward
            // target k. Take the period that starts farthest from its target.
            double worstStart = 0.0;
            double ratio = 0.0;
            for (uint64_t k = 1; k < 12; ++k)
            {
                const double target = drawn(kSeed, 3, gg::rng::Field::ModRandom2, k);
                const double start = target - out[64 * k - 1];
                const double end = target - out[64 * k + 63];
                if (std::abs(start) > worstStart)
                {
                    worstStart = std::abs(start);
                    ratio = end / start;
                }
            }
            REQUIRE(worstStart > 0.2);
            return ratio;
        };
        // Smooth 1: one period is one time constant, so e^-1 of the distance remains
        // ((63/64)^64 in 64 discrete steps).
        const double one = remaining(1.0f);
        CHECK(one == Catch::Approx(std::pow(63.0 / 64.0, 64.0)).margin(1e-4));
        CHECK(one == Catch::Approx(std::exp(-1.0)).margin(0.01));
        // Smooth 0.2: five time constants a period, within 1% of the target by its end.
        const double fifth = remaining(0.2f);
        CHECK(std::abs(fifth) < 0.01);
    }
}

TEST_CASE("the step sequencer cycles its length", "[core][mod]")
{
    StepParams params{1.0f, 3, {}};
    for (std::size_t i = 0; i < params.steps.size(); ++i)
    {
        params.steps[i] = static_cast<float>(i) / 16.0f;
    }
    StepSequencer seq;
    seq.reset();
    // At 1 Hz a 0.25 s cell is a quarter of a step.
    for (int k = 0; k < 12; ++k)
    {
        for (int q = 0; q < 4; ++q)
        {
            INFO("step " << k << " cell " << q);
            CHECK(seq.step(params, 0.25) == params.steps[static_cast<std::size_t>(k % 3)]);
        }
    }
    // Length clamps to 1..16: 0 holds the first step, 40 cycles all 16.
    StepSequencer one;
    one.reset();
    params.length = 0;
    for (int i = 0; i < 20; ++i)
    {
        CHECK(one.step(params, 0.5) == params.steps[0]);
    }
    StepSequencer all;
    all.reset();
    params.length = 40;
    for (int k = 0; k < 40; ++k)
    {
        CHECK(all.step(params, 1.0) == params.steps[static_cast<std::size_t>(k % 16)]);
    }
}

TEST_CASE("the follower rises in 5 ms and falls in 200 ms", "[core][mod]")
{
    constexpr uint32_t kFrames = 32;
    std::array<float, kFrames> burst{};
    burst.fill(1.0f);
    std::array<float, kFrames> silence{};
    const float *burstIn[2] = {burst.data(), burst.data()};
    const float *silenceIn[2] = {silence.data(), nullptr}; // a null channel is silence

    EnvFollower f(kSampleRate);
    f.reset();
    // One cell is 2/3 ms at 48 kHz.
    auto msToCells = [](double ms) { return static_cast<int>(std::lround(ms * 48.0 / 32.0)); };
    float env = 0.0f;
    int cells = 0;
    for (; cells < msToCells(6.0); ++cells)
    {
        env = f.step(burstIn, 2, kFrames);
    }
    // 6 ms is 1.2 time constants: above 1 − e^-1.
    CHECK(env > 1.0f - static_cast<float>(std::exp(-1.0)));
    CHECK(env == Catch::Approx(1.0 - std::exp(-6.0 / 5.0)).margin(1e-4));
    for (; cells < msToCells(12.0); ++cells)
    {
        env = f.step(burstIn, 2, kFrames);
    }
    CHECK(env > 0.9f); // 2.3 τ
    for (; cells < msToCells(100.0); ++cells)
    {
        env = f.step(burstIn, 2, kFrames);
    }
    CHECK(env == Catch::Approx(1.0).margin(1e-6));

    // The fall: about 460 ms (2.3 × 200 ms) brings it below 0.1; 400 ms doesn't.
    for (int i = 0; i < msToCells(400.0); ++i)
    {
        env = f.step(silenceIn, 2, kFrames);
    }
    CHECK(env > 0.1f);
    for (int i = msToCells(400.0); i < msToCells(465.0); ++i)
    {
        env = f.step(silenceIn, 2, kFrames);
    }
    CHECK(env < 0.1f);

    // Clamped to 1 for a hot input; non-finite samples count as silence.
    std::array<float, kFrames> hot{};
    hot.fill(-10.0f);
    hot[3] = std::numeric_limits<float>::quiet_NaN();
    hot[4] = std::numeric_limits<float>::infinity();
    const float *hotIn[1] = {hot.data()};
    for (int i = 0; i < 200; ++i)
    {
        CHECK(f.step(hotIn, 1, kFrames) <= 1.0f);
    }
    CHECK(f.step(hotIn, 1, kFrames) == 1.0f);
    std::array<float, kFrames> bad{};
    bad.fill(std::numeric_limits<float>::quiet_NaN());
    const float *badIn[1] = {bad.data()};
    EnvFollower g(kSampleRate);
    g.reset();
    CHECK(g.step(badIn, 1, kFrames) == 0.0f);
    CHECK(g.step(nullptr, 0, kFrames) == 0.0f);
    CHECK(g.step(badIn, 1, 0) == 0.0f);
}

TEST_CASE("the transport advances only while playing and holds otherwise", "[core][mod]")
{
    TransportSource t;
    // Before any transport: 0.
    t.step(0.25);
    CHECK(t.beatPhase() == 0.0f);
    CHECK(t.barPhase() == 0.0f);
    CHECK(t.playing() == 0.0f);

    // 120 BPM: a 0.25 s cell is half a beat.
    t.set({true, 0.0, 120.0, true, 4});
    CHECK(t.playing() == 1.0f);
    for (int i = 0; i < 3; ++i)
    {
        t.step(0.25);
    }
    CHECK(t.beatPhase() == 0.5f);  // beat 1.5
    CHECK(t.barPhase() == 0.375f); // 1.5 of 4
    for (int i = 0; i < 6; ++i)
    {
        t.step(0.25);
    }
    CHECK(t.beatPhase() == 0.5f);  // beat 4.5
    CHECK(t.barPhase() == 0.125f); // the next bar's 0.5 of 4
    // Stopped: holds.
    t.set({true, 4.5, 120.0, false, 4});
    for (int i = 0; i < 10; ++i)
    {
        t.step(0.25);
    }
    CHECK(t.playing() == 0.0f);
    CHECK(t.beatPhase() == 0.5f);
    CHECK(t.barPhase() == 0.125f);
    // A bar of at least one beat; a negative position (a pre-roll) still gives phases in [0, 1).
    t.set({true, -0.25, 60.0, false, 0});
    CHECK(t.beatPhase() == 0.75f);
    CHECK(t.barPhase() == 0.75f);
    // Without beats the phases are 0, the playing flag still the host's.
    t.set({false, 2.5, 120.0, true, 4});
    t.step(0.25);
    CHECK(t.beatPhase() == 0.0f);
    CHECK(t.barPhase() == 0.0f);
    CHECK(t.playing() == 1.0f);
}

TEST_CASE("every step is allocation-free", "[core][mod][rt]")
{
    LayerModulators mods;
    EnvFollower follower(kSampleRate);
    TransportSource transport;
    std::array<float, 32> buffer{};
    buffer.fill(0.5f);
    const float *in[2] = {buffer.data(), buffer.data()};
    StepParams stepParams{8.0f, 5, {}};
    float sink = 0.0f;

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        mods.reset();
        follower.reset();
        transport.set({true, 0.0, 128.0, true, 4});
        for (int i = 0; i < 1000; ++i)
        {
            const bool gate = (i / 100) % 2 == 0;
            for (uint32_t m = 0; m < 2; ++m)
            {
                sink += mods.lfo[m].step({3.0f, static_cast<LfoShape>(i % 5)}, kCell);
                sink += mods.env[m].step({0.01f, 0.02f, 0.6f, 0.05f}, gate, kCell);
                sink += mods.random[m].step({5.0f, 0.5f}, kCell, 7, m,
                                            m == 0 ? gg::rng::Field::ModRandom1
                                                   : gg::rng::Field::ModRandom2);
            }
            sink += mods.stepSeq.step(stepParams, kCell);
            sink += follower.step(in, 2, 32);
            transport.step(kCell);
            sink += transport.beatPhase() + transport.barPhase() + transport.playing();
            sink += gg::core::sineDet(static_cast<float>(i) / 1000.0f);
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    CHECK(std::isfinite(sink));
    if (AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}
