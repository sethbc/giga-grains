// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The mod matrix in the Processor (M2 sub-project 1's spec §6.8, §7): the routes replace
// macroOffsets, every source steps once per grid boundary (the modulators, the MIDI controls and
// note-ons, the follower, the transport), a time-varying route applies every cell, and all of it
// is block-size invariant, real-time safe and the same bits with a thread pool.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/LayersFixture.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ModSources.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Processor.h"
#include "gg/engine/TaskRunner.h"
#include "gg/metrics/Metrics.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "support/AllocTripwire.h"
#include "support/StubPool.h"

using gg::core::ControlEvent;
using gg::core::ControlKind;
using gg::core::kAllLayers;
using gg::core::kParamCount;
using gg::core::kSameLayer;
using gg::core::LayerParam;
using gg::core::layerSlot;
using gg::core::ModCurve;
using gg::core::Mode;
using gg::core::ModRoute;
using gg::core::ModRoutes;
using gg::core::ModSource;
using gg::core::NoteEvent;
using gg::core::NoteEventType;
using gg::core::ParamEvent;
using gg::core::ParamSlot;
using gg::core::ParamValues;
using gg::core::Processor;
using gg::core::Transport;
using gg::test::AllocTripwire;
using gg::test::StubPool;
using namespace gg::test::layers;

namespace
{
constexpr double kCellSeconds = 32.0 / kRate;

uint32_t bitsOf(float f) { return std::bit_cast<uint32_t>(f); }

ParamSlot slotOf(LayerParam p, uint8_t layer) { return layerSlot(p, layer); }

// A route of `source` (its own layer for a per-layer source) to `target` on `targetLayer`.
ModRoute route(ModSource source, ParamSlot target, uint8_t targetLayer, float depth, ModCurve curve,
               uint8_t sourceLayer = kSameLayer)
{
    return ModRoute{source, sourceLayer, target, targetLayer, depth, curve};
}

// No routes but `r`, in slot 1.
ModRoutes only(const ModRoute &r)
{
    ModRoutes routes{};
    routes[0] = r;
    return routes;
}

// Every effective value, as a ParamValues for the ParamMap readers.
ParamValues effectiveValues(const Processor &p)
{
    ParamValues v{};
    for (size_t s = 0; s < kParamCount; ++s)
    {
        v[s] = p.effective(static_cast<ParamSlot>(s));
    }
    return v;
}

// The effective value of a slot at base `base` with no host modulation and one route's offset x:
// the Processor's clamp((base + 0) + (0 + x)).
float routed(ParamSlot slot, float base, float x, Mode mode)
{
    const auto &meta = gg::core::paramMeta(slot, mode);
    return std::clamp((base + 0.0f) + (0.0f + x), meta.minVal, meta.maxVal);
}

double correlation(const std::vector<double> &a, const std::vector<double> &b)
{
    const auto n = static_cast<double>(a.size());
    double ma = 0.0;
    double mb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        ma += a[i] / n;
        mb += b[i] / n;
    }
    double sab = 0.0;
    double saa = 0.0;
    double sbb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return sab / std::sqrt(saa * sbb);
}

// A stereo sine of `frames` at hz, amplitude 0.5.
Stereo sineInput(int64_t frames, double hz)
{
    Stereo s = silence(frames);
    for (int64_t i = 0; i < frames; ++i)
    {
        const auto j = static_cast<std::size_t>(i);
        s.left[j] = static_cast<float>(
            0.5 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
        s.right[j] = s.left[j];
    }
    return s;
}

// The instrument with one sine sample on layer A, a note held from sample 0, its grains' log.
struct InstrumentRun
{
    Stereo audio;
    Log log;
};

void renderHeldNote(const ModRoutes &routes, int64_t frames, InstrumentRun &r)
{
    Processor p({kRate, 512, 5, Mode::Instrument});
    p.setSourceNow(0, sine(220.0));
    p.setBase(ParamSlot::Spray, 0.0f);
    p.setBase(ParamSlot::Density, 6.0f);
    p.setBase(ParamSlot::Size, -5.0f);
    p.setBase(ParamSlot::Position, 0.5f);
    p.setRoutes(routes);
    p.setEventSink(&Log::record, &r.log);
    r.audio = silence(frames);
    render(p, nullptr, 0, frames, {512}, {}, r.audio, &r.log, {}, {{0, NoteEventType::On, 0, 60}});
}

// Every source routed somewhere (one route per source; the per-layer ones from each target's own
// layer, on every layer), after the factory routes: the routes of the invariance and allocation
// tests.
ModRoutes allSourceRoutes(Mode mode)
{
    ModRoutes r = gg::core::factoryRoutes(mode);
    const std::array<ModRoute, 16> extra{{
        route(ModSource::Lfo1, ParamSlot::Position, kAllLayers, 0.2f, ModCurve::Bipolar),
        route(ModSource::Lfo2, ParamSlot::Pitch, kAllLayers, 3.0f, ModCurve::Bipolar),
        route(ModSource::Env1, ParamSlot::Level, kAllLayers, 6.0f, ModCurve::Unipolar),
        route(ModSource::Env2, ParamSlot::Texture, kAllLayers, 0.5f, ModCurve::Unipolar),
        route(ModSource::Random1, ParamSlot::Spray, kAllLayers, 0.2f, ModCurve::Bipolar),
        route(ModSource::Random2, ParamSlot::Stereo, kAllLayers, 0.5f, ModCurve::Unipolar),
        route(ModSource::Step, ParamSlot::Density, kAllLayers, 1.0f, ModCurve::Bipolar),
        route(ModSource::ModWheel, ParamSlot::Size, kAllLayers, 1.0f, ModCurve::Bipolar),
        route(ModSource::Aftertouch, ParamSlot::Distribution, kAllLayers, 0.5f, ModCurve::Unipolar),
        route(ModSource::PitchBend, ParamSlot::Pitch, kAllLayers, 2.0f, ModCurve::Bipolar),
        route(ModSource::Velocity, ParamSlot::Level, kAllLayers, -6.0f, ModCurve::Unipolar),
        route(ModSource::Key, ParamSlot::PitchSpread, kAllLayers, 2.0f, ModCurve::Unipolar),
        route(ModSource::EnvFollower, ParamSlot::Diffusion, kAllLayers, 0.5f, ModCurve::Unipolar),
        route(ModSource::TransportBeat, ParamSlot::MorphX, kAllLayers, 0.3f, ModCurve::Bipolar),
        route(ModSource::TransportBar, ParamSlot::MorphY, kAllLayers, 0.3f, ModCurve::Bipolar),
        route(ModSource::TransportPlaying, ParamSlot::Mix, kAllLayers, -0.2f, ModCurve::Unipolar),
    }};
    std::copy(extra.begin(), extra.end(), r.begin() + 12);
    return r;
}

// The modulators' controls, different on every layer, so each layer's sources differ.
void varyModulators(Processor &p)
{
    for (uint8_t L = 0; L < 4; ++L)
    {
        const float f = static_cast<float>(L);
        p.setBase(slotOf(LayerParam::Lfo1Rate, L), 1.0f + 0.5f * f);
        p.setBase(slotOf(LayerParam::Lfo2Rate, L), 2.5f - 0.5f * f);
        p.setBase(slotOf(LayerParam::Lfo2Shape, L), f);
        p.setBase(slotOf(LayerParam::Env1Attack, L), -5.0f + f);
        p.setBase(slotOf(LayerParam::Env2Sustain, L), 0.2f * f);
        p.setBase(slotOf(LayerParam::Random1Rate, L), 3.0f);
        p.setBase(slotOf(LayerParam::Random2Smooth, L), 0.25f * f);
        p.setBase(slotOf(LayerParam::StepRate, L), 3.0f);
        p.setBase(slotOf(LayerParam::StepLength, L), 3.0f + f);
        for (uint8_t k = 0; k < 16; ++k)
        {
            const auto step = static_cast<LayerParam>(static_cast<size_t>(LayerParam::Step1) + k);
            p.setBase(slotOf(step, L), static_cast<float>((k * 5 + L * 3) % 16) / 15.0f);
        }
    }
}

// MIDI controls off the grid, every kind, over the invariance render.
std::vector<TimedControl> someControls()
{
    std::vector<TimedControl> c;
    for (int k = 0; k < 24; ++k)
    {
        const auto time = static_cast<int64_t>(uniform(77, 3 * k) * (kInvarianceFrames - 1));
        const auto kind = static_cast<ControlKind>(k % 3);
        c.push_back({time, kind, static_cast<int16_t>(k % 2 == 0 ? -1 : 2),
                     static_cast<float>(uniform(77, 3 * k + 1))});
    }
    std::stable_sort(c.begin(), c.end(),
                     [](const TimedControl &a, const TimedControl &b) { return a.time < b.time; });
    return c;
}

// The instrument's notes, with velocities.
std::vector<TimedNote> someNotes()
{
    return {{0, NoteEventType::On, 0, 60, 0.9f},     {9001, NoteEventType::On, 0, 67, 0.4f},
            {30017, NoteEventType::On, 1, 64, 0.7f}, {41003, NoteEventType::Off, 0, 60},
            {47999, NoteEventType::AllOff, -1, 0},   {55021, NoteEventType::On, 0, 55, 0.2f},
            {70013, NoteEventType::On, 0, 72, 1.0f}, {88001, NoteEventType::Off, 0, -1}};
}

// The factory routes and routes from the sources that move only with events (MIDI, the
// transport's playing flag): no time-varying route, so only the event boundaries apply, while the
// sources still step at every boundary.
ModRoutes eventSourceRoutes(Mode mode)
{
    ModRoutes r = gg::core::factoryRoutes(mode);
    r[12] = route(ModSource::ModWheel, ParamSlot::Size, kAllLayers, 1.0f, ModCurve::Bipolar);
    r[13] = route(ModSource::Velocity, ParamSlot::Level, kAllLayers, -6.0f, ModCurve::Unipolar);
    r[14] = route(ModSource::PitchBend, ParamSlot::Pitch, kAllLayers, 2.0f, ModCurve::Bipolar);
    r[15] =
        route(ModSource::TransportPlaying, ParamSlot::Mix, kAllLayers, -0.2f, ModCurve::Unipolar);
    return r;
}

LayersSetup modSetup(Mode mode, gg::TaskRunner *runner, bool timeVarying = true)
{
    LayersSetup s;
    s.mode = mode;
    s.runner = runner;
    s.controls = someControls();
    if (mode == Mode::Instrument)
    {
        s.notes = someNotes();
    }
    s.configure = [mode, timeVarying](Processor &p) {
        varyModulators(p);
        p.setRoutes(timeVarying ? allSourceRoutes(mode) : eventSourceRoutes(mode));
        REQUIRE(p.timeVarying() == timeVarying);
        p.setTransport({true, 3.25, 132.0, true, 3});
    };
    return s;
}

// Counts run() calls, and runs them on the calling thread.
class CountingRunner final : public gg::TaskRunner
{
  public:
    void run(uint32_t count, gg::TaskFn fn, void *ctx) noexcept override
    {
        ++calls;
        serial_.run(count, fn, ctx);
    }

    uint64_t calls{0};

  private:
    gg::SerialRunner serial_;
};
} // namespace

TEST_CASE("the factory routes keep the macro tests", "[core][mod]")
{
    // The macro tests of processor_tests.cpp run against these routes unchanged; here, that they
    // are the routes a Processor starts with, that they don't move with time, and that they
    // reach every layer.
    float l[64];
    float r[64];
    float *out[2] = {l, r};
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        INFO((mode == Mode::Fx ? "FX" : "instrument"));
        Processor p({kRate, 512, 3, mode});
        REQUIRE(p.routes() == gg::core::factoryRoutes(mode));
        REQUIRE_FALSE(p.timeVarying());
        const std::array<ParamEvent, 2> events{
            {{0, ParamSlot::Macro3, false, 0.75}, {0, ParamSlot::Macro8, false, 1.0}}};
        p.process(nullptr, 0, out, 2, 64, events);
        for (uint8_t L = 0; L < 4; ++L)
        {
            INFO("layer " << int{L});
            CHECK(p.effective(slotOf(LayerParam::Pitch, L)) == 12.0f); // 24 × (2 × 0.75 − 1)
            CHECK(p.effective(gg::core::levelSlot(L)) == (mode == Mode::Instrument ? 12.0f : 0.0f));
        }
        CHECK(p.sourceValue(ModSource::Macro3, 2) == 0.75f);
    }
}

TEST_CASE("an lfo on Position moves the grains per cell", "[core][mod]")
{
    // LFO 1 (1 Hz sine, the default) on layer A's Position, depth 0.5 bipolar around 0.5: each
    // grain reads where the LFO was at its onset's cell, Position 0.5 + 0.5 × sin(2π phase).
    constexpr int64_t kFrames = 48000;
    InstrumentRun lfo;
    renderHeldNote(only(route(ModSource::Lfo1, ParamSlot::Position, 0, 0.5f, ModCurve::Bipolar)),
                   kFrames, lfo);
    const std::vector<gg::GrainSpec> grains = lfo.log.layer(0);
    REQUIRE(grains.size() > 40);
    std::vector<double> starts;
    std::vector<double> expected;
    for (const gg::GrainSpec &g : grains)
    {
        const int64_t cell = g.onsetSample / 32;
        double phase = 0.0;
        for (int64_t k = 0; k < cell; ++k)
        {
            phase += 1.0 * kCellSeconds; // the LFO's accumulation, per cell
        }
        phase -= std::floor(phase);
        starts.push_back(g.startPos);
        expected.push_back(gg::core::sineDet(static_cast<float>(phase)));
    }
    CHECK(correlation(starts, expected) > 0.9);
    // It sweeps most of the sample.
    const auto [lo, hi] = std::minmax_element(starts.begin(), starts.end());
    CHECK(*lo < 0.1 * kRate);
    CHECK(*hi > 0.9 * kRate);

    // Depth 0 is no route at all: the same grains and bits as the factory routes alone.
    InstrumentRun plain;
    renderHeldNote(gg::core::factoryRoutes(Mode::Instrument), kFrames, plain);
    ModRoutes zero = gg::core::factoryRoutes(Mode::Instrument);
    zero[12] = route(ModSource::Lfo1, ParamSlot::Position, 0, 0.0f, ModCurve::Bipolar);
    InstrumentRun silent;
    renderHeldNote(zero, kFrames, silent);
    REQUIRE(plain.log.layer(0).size() > 40);
    CHECK(silent.log.layer(0) == plain.log.layer(0));
    CHECK(sameBits(silent.audio.left, plain.audio.left));
    CHECK(sameBits(silent.audio.right, plain.audio.right));
}

TEST_CASE("the envelope follows the held set", "[core][mod]")
{
    // Env 1 on layer A's Level, +24 dB unipolar over a base of −24 dB: Level rises from −24 after
    // the note-on, decays to the sustain, and falls back after the release, cell for cell as an
    // Adsr gated by the held set.
    constexpr int64_t kOn = 0;
    constexpr int64_t kOff = 24000; // a boundary
    constexpr int64_t kFrames = 40000;
    Processor p({kRate, 512, 7, Mode::Instrument});
    p.setBase(ParamSlot::Level, -24.0f);
    p.setBase(ParamSlot::Env1Attack, std::log2(0.1f));
    p.setBase(ParamSlot::Env1Decay, std::log2(0.1f));
    p.setBase(ParamSlot::Env1Sustain, 0.5f);
    p.setBase(ParamSlot::Env1Release, std::log2(0.2f));
    p.setRoutes(only(route(ModSource::Env1, ParamSlot::Level, 0, 24.0f, ModCurve::Unipolar)));
    REQUIRE(p.timeVarying());
    const gg::core::AdsrParams params = gg::core::envParams(effectiveValues(p), 0, 0);
    gg::core::Adsr model;

    Stereo out = silence(kFrames);
    std::vector<float> level;
    render(p, nullptr, 0, kFrames, {32}, {}, out, nullptr,
           [&](int64_t) { level.push_back(p.effective(ParamSlot::Level)); },
           {{kOn, NoteEventType::On, 0, 60}, {kOff, NoteEventType::Off, 0, 60}});
    REQUIRE(level.size() == kFrames / 32);
    float e = 0.0f;
    for (std::size_t k = 0; k < level.size(); ++k)
    {
        const auto b = static_cast<int64_t>(32 * k);
        e = model.step(params, b >= kOn && b < kOff, kCellSeconds);
        INFO("cell " << k);
        REQUIRE(bitsOf(level[k]) ==
                bitsOf(routed(ParamSlot::Level, -24.0f, 24.0f * e, Mode::Instrument)));
    }
    CHECK(bitsOf(p.sourceValue(ModSource::Env1, 0)) == bitsOf(e));
    // The shape: up to 0 dB at the attack's end (100 ms, cell 150), the sustain's −12 dB before
    // the release, back to −24 dB 200 ms after it.
    CHECK(level[0] == -24.0f);
    CHECK(level[150] > -0.01f);
    CHECK(level[700] == -12.0f);
    CHECK(level[750] == -12.0f); // the release's first cell starts at the sustain
    CHECK(level[751] < -12.0f);
    CHECK(level[kOff / 32 + 310] == -24.0f);
    CHECK(p.sourceValue(ModSource::Env1, 0) == 0.0f);
}

TEST_CASE("random and step are repeatable and keyed by layer", "[core][mod]")
{
    // Random 1 on every layer's Position and Step on every layer's Pitch, four layers on: two
    // renders, in different blocks, are the same bits; each layer draws its own randoms; Step
    // walks its steps.
    constexpr int64_t kFrames = 48000;
    const Stereo input = noise(kFrames, 0.5f, 81);
    ModRoutes routes{};
    routes[0] = route(ModSource::Random1, ParamSlot::Position, kAllLayers, 0.3f, ModCurve::Bipolar);
    routes[1] = route(ModSource::Step, ParamSlot::Pitch, kAllLayers, 12.0f, ModCurve::Unipolar);
    const std::array<float, 4> steps{0.0f, 0.25f, 0.5f, 1.0f};
    struct Run
    {
        Stereo audio;
        std::vector<std::array<float, 4>> random;
        std::vector<float> step;
    };
    const auto run = [&](uint32_t block, Run &r) {
        Processor p({kRate, 4096, 83, Mode::Fx});
        p.setBase(ParamSlot::Mix, 1.0f);
        for (uint8_t L = 0; L < 4; ++L)
        {
            p.setBase(gg::core::layerOnSlot(L), 1.0f);
            p.setBase(slotOf(LayerParam::Random1Rate, L), 3.0f); // 8 Hz
        }
        p.setBase(ParamSlot::StepLength, 4.0f);
        p.setBase(ParamSlot::StepRate, 2.0f); // 4 Hz
        for (uint8_t k = 0; k < 4; ++k)
        {
            const auto step = static_cast<LayerParam>(static_cast<size_t>(LayerParam::Step1) + k);
            p.setBase(slotOf(step, 0), steps[k]);
        }
        p.setRoutes(routes);
        r.audio = silence(kFrames);
        render(p, &input, 0, kFrames, {block}, {}, r.audio, nullptr, [&](int64_t) {
            r.random.push_back(
                {p.sourceValue(ModSource::Random1, 0), p.sourceValue(ModSource::Random1, 1),
                 p.sourceValue(ModSource::Random1, 2), p.sourceValue(ModSource::Random1, 3)});
            r.step.push_back(p.sourceValue(ModSource::Step, 0));
        });
    };
    Run a;
    run(32, a);
    Run b;
    run(32, b);
    Run c;
    run(100, c);
    REQUIRE(peak(a.audio.left) > 0.05f);
    CHECK(sameBits(a.audio.left, b.audio.left));
    CHECK(sameBits(a.audio.right, b.audio.right));
    CHECK(a.random == b.random);
    CHECK(sameBits(a.audio.left, c.audio.left));
    CHECK(sameBits(a.audio.right, c.audio.right));

    // Each layer's draws are its own: the layers' values differ in nearly every cell.
    std::size_t same = 0;
    for (const auto &v : a.random)
    {
        same += (v[0] == v[1] || v[0] == v[2] || v[0] == v[3] || v[1] == v[2]) ? 1 : 0;
    }
    CHECK(same < a.random.size() / 20);
    // A new draw every 1/8 s: 6000 samples, 187.5 cells.
    std::size_t changes = 0;
    for (std::size_t k = 1; k < a.random.size(); ++k)
    {
        changes += a.random[k][0] != a.random[k - 1][0] ? 1 : 0;
    }
    CHECK(changes == 7);

    // Step: cell k plays step floor(4 Hz × k × 32 / 48 kHz) mod 4, away from the step edges.
    for (std::size_t k = 0; k < a.step.size(); ++k)
    {
        const double steps4 = 4.0 * static_cast<double>(k) * kCellSeconds;
        if (steps4 - std::floor(steps4) < 0.01 || steps4 - std::floor(steps4) > 0.99)
        {
            continue;
        }
        INFO("cell " << k);
        CHECK(a.step[k] == steps[static_cast<std::size_t>(std::floor(steps4)) % 4]);
    }
}

TEST_CASE("midi sources arrive on the grid", "[core][mod]")
{
    // Mod Wheel on Pitch and Pitch Bend on Size (layer A): a Mod Wheel event at sample 5 applies
    // at the boundary 32, in 1-sample calls as in any others; Pitch Bend at its centre adds
    // nothing through a Bipolar curve; a note-on's velocity and key are sources from its boundary.
    Processor p({kRate, 512, 3, Mode::Instrument});
    ModRoutes routes{};
    routes[0] = route(ModSource::ModWheel, ParamSlot::Pitch, 0, 12.0f, ModCurve::Unipolar);
    routes[1] = route(ModSource::PitchBend, ParamSlot::Size, 0, 2.0f, ModCurve::Bipolar);
    p.setRoutes(routes);
    REQUIRE_FALSE(p.timeVarying());
    const float size = p.base(ParamSlot::Size);
    CHECK(p.sourceValue(ModSource::PitchBend, 0) == 0.5f);
    CHECK(bitsOf(p.effective(ParamSlot::Size)) == bitsOf(size));

    float left = 0.0f;
    float right = 0.0f;
    float *out[2] = {&left, &right};
    const ControlEvent wheel{5, ControlKind::ModWheel, 0, 0.8f};
    for (int64_t t = 0; t < 64; ++t)
    {
        INFO("after sample " << t);
        p.process(nullptr, 0, out, 2, 1, {}, {},
                  t == 5 ? std::span(&wheel, 1) : std::span<const ControlEvent>{});
        if (t < 32)
        {
            REQUIRE(p.sourceValue(ModSource::ModWheel, 0) == 0.0f);
            REQUIRE(p.effective(ParamSlot::Pitch) == 0.0f);
        }
        else
        {
            REQUIRE(p.sourceValue(ModSource::ModWheel, 3) == 0.8f);
            REQUIRE(p.effective(ParamSlot::Pitch) ==
                    routed(ParamSlot::Pitch, 0.0f, 12.0f * 0.8f, Mode::Instrument));
        }
    }

    std::vector<float> l(64), r(64);
    float *block[2] = {l.data(), r.data()};
    // Pitch bend at the centre: Size exactly at its base; at the top, +2.
    const ControlEvent centre{0, ControlKind::PitchBend, -1, 0.5f};
    p.process(nullptr, 0, block, 2, 64, {}, {}, std::span(&centre, 1));
    CHECK(bitsOf(p.effective(ParamSlot::Size)) == bitsOf(size));
    const std::array<ControlEvent, 2> bends{
        {{0, ControlKind::PitchBend, 15, 1.0f}, {10, ControlKind::Aftertouch, 0, 0.3f}}};
    p.process(nullptr, 0, block, 2, 64, {}, {}, bends);
    CHECK(p.effective(ParamSlot::Size) == routed(ParamSlot::Size, size, 2.0f, Mode::Instrument));
    CHECK(p.sourceValue(ModSource::Aftertouch, 1) == 0.3f);

    // A note-on: velocity 0.5 and key 64 / 127 from its boundary.
    const NoteEvent on{3, NoteEventType::On, 0, 64, 0.5f};
    p.process(nullptr, 0, block, 2, 64, {}, std::span(&on, 1));
    CHECK(p.sourceValue(ModSource::Velocity, 0) == 0.5f);
    CHECK(p.sourceValue(ModSource::Key, 2) == 64.0f / 127.0f);
    const NoteEvent loud{0, NoteEventType::On, 0, 0, 2.0f};
    p.process(nullptr, 0, block, 2, 64, {}, std::span(&loud, 1));
    CHECK(p.sourceValue(ModSource::Velocity, 0) == 1.0f); // clamped
    CHECK(p.sourceValue(ModSource::Key, 0) == 0.0f);

    // Out of range: ignored. In range but past 1: clamped.
    const std::array<ControlEvent, 4> odd{{
        {0, static_cast<ControlKind>(3), 0, 0.1f},
        {0, ControlKind::ModWheel, 16, 0.1f},
        {0, ControlKind::ModWheel, 0, std::numeric_limits<float>::quiet_NaN()},
        {0, ControlKind::Aftertouch, -2, 0.1f},
    }};
    p.process(nullptr, 0, block, 2, 64, {}, {}, odd);
    CHECK(p.sourceValue(ModSource::ModWheel, 0) == 0.8f);
    CHECK(p.sourceValue(ModSource::Aftertouch, 0) == 0.3f);
    const ControlEvent past{0, ControlKind::ModWheel, 0, 1.5f};
    p.process(nullptr, 0, block, 2, 64, {}, {}, std::span(&past, 1));
    CHECK(p.sourceValue(ModSource::ModWheel, 0) == 1.0f);

    // reset() returns them to their initial values.
    p.reset();
    CHECK(p.sourceValue(ModSource::ModWheel, 0) == 0.0f);
    CHECK(p.sourceValue(ModSource::PitchBend, 0) == 0.5f);
    CHECK(p.sourceValue(ModSource::Velocity, 0) == 0.0f);
}

TEST_CASE("the follower tracks the input", "[core][mod]")
{
    // Bursts of noise and silence: the follower, stepped per cell from the previous cell's peak,
    // rises in each burst and decays after it, cell for cell as an EnvFollower fed the same input;
    // the first cell's value is 0.
    constexpr int64_t kFrames = 48000;
    Stereo input = noise(kFrames, 0.5f, 91);
    for (int64_t i = 0; i < kFrames; ++i)
    {
        const bool burst = i < 2400 || (i >= 21600 && i < 24000);
        if (!burst)
        {
            input.left[static_cast<std::size_t>(i)] = 0.0f;
            input.right[static_cast<std::size_t>(i)] = 0.0f;
        }
    }
    input.left[100] = std::numeric_limits<float>::quiet_NaN(); // counts as 0
    Processor p({kRate, 512, 9, Mode::Fx});
    Stereo out = silence(kFrames);
    std::vector<float> follower;
    render(p, &input, 0, kFrames, {32}, {}, out, nullptr,
           [&](int64_t) { follower.push_back(p.sourceValue(ModSource::EnvFollower, 0)); });
    gg::core::EnvFollower model(kRate);
    for (std::size_t k = 0; k < follower.size(); ++k)
    {
        float e = 0.0f;
        if (k == 0)
        {
            const float *none = nullptr;
            e = model.step(&none, 1, 32);
        }
        else
        {
            const std::size_t from = 32 * (k - 1);
            const float *cell[2] = {input.left.data() + from, input.right.data() + from};
            e = model.step(cell, 2, 32);
        }
        INFO("cell " << k);
        REQUIRE(bitsOf(follower[k]) == bitsOf(e));
    }
    CHECK(follower[0] == 0.0f);
    CHECK(follower[2400 / 32] > 0.4f);
    CHECK(follower[(2400 + 9600) / 32] < 0.5f * follower[2400 / 32]);
    CHECK(follower[(2400 + 9600) / 32] > 0.0f);
    CHECK(follower[24000 / 32] > 0.4f);
    for (std::size_t k = 2400 / 32 + 1; k < 21600 / 32; ++k)
    {
        REQUIRE(follower[k] <= follower[k - 1]);
    }
}

TEST_CASE("the transport sources hold without a transport and advance with one", "[core][mod]")
{
    std::vector<float> l(1000), r(1000);
    float *out[2] = {l.data(), r.data()};
    Processor p({kRate, 512, 11, Mode::Fx});
    // Transport Playing on Pitch: no time-varying route, so its change applies at the next
    // call's first boundary as an event at offset 0 would.
    p.setRoutes(
        only(route(ModSource::TransportPlaying, ParamSlot::Pitch, 0, 12.0f, ModCurve::Unipolar)));
    REQUIRE_FALSE(p.timeVarying());
    for (int k = 0; k < 10; ++k)
    {
        p.process(nullptr, 0, out, 2, 1000, {});
    }
    CHECK(p.sourceValue(ModSource::TransportBeat, 0) == 0.0f);
    CHECK(p.sourceValue(ModSource::TransportBar, 0) == 0.0f);
    CHECK(p.sourceValue(ModSource::TransportPlaying, 0) == 0.0f);
    CHECK(p.effective(ParamSlot::Pitch) == 0.0f);

    // Playing at 120 BPM in 4/4 from beat 1.5, given before a call that starts mid-cell
    // (10000 = 312 × 32 + 16): the first boundary, 10016, takes it.
    const Transport t{true, 1.5, 120.0, true, 4};
    p.setTransport(t);
    gg::core::TransportSource model;
    model.set(t);
    p.process(nullptr, 0, out, 2, 1, {});
    CHECK(p.effective(ParamSlot::Pitch) == 0.0f);
    p.process(nullptr, 0, out, 2, 16, {}); // [10001, 10017): the boundary 10016
    CHECK(p.effective(ParamSlot::Pitch) == 12.0f);
    CHECK(p.sourceValue(ModSource::TransportPlaying, 0) == 1.0f);
    CHECK(p.sourceValue(ModSource::TransportBeat, 0) == model.beatPhase()); // 0.5 at 10016
    model.step(kCellSeconds);
    // Held between the host's events, it advances: 983 calls of 1000 frames later, the last
    // stepped boundary is the last below 993017, 992992, which took the position of 30718 cells
    // after 10016's.
    for (int k = 0; k < 983; ++k)
    {
        p.process(nullptr, 0, out, 2, 1000, {});
    }
    constexpr int64_t kLast = (993017 - 1) / 32 * 32;
    static_assert(kLast == 992992);
    for (int64_t b = 10016 + 64; b <= kLast; b += 32)
    {
        model.step(kCellSeconds);
    }
    CHECK(p.sourceValue(ModSource::TransportBeat, 3) == model.beatPhase());
    CHECK(p.sourceValue(ModSource::TransportBar, 3) == model.barPhase());
    CHECK(p.sourceValue(ModSource::TransportBeat, 0) != 0.5f);

    // Stopped: it holds where it is, and Playing's route goes at the next boundary.
    const float beat = p.sourceValue(ModSource::TransportBeat, 0);
    Transport stopped = t;
    stopped.playing = false;
    stopped.beatPosition = 40.25;
    p.setTransport(stopped);
    p.process(nullptr, 0, out, 2, 1000, {});
    CHECK(p.sourceValue(ModSource::TransportPlaying, 0) == 0.0f);
    CHECK(p.sourceValue(ModSource::TransportBeat, 0) == 0.25f);
    CHECK(p.sourceValue(ModSource::TransportBar, 0) == 0.0625f);
    CHECK(p.effective(ParamSlot::Pitch) == 0.0f);
    p.process(nullptr, 0, out, 2, 1000, {});
    CHECK(p.sourceValue(ModSource::TransportBeat, 0) == 0.25f);
    CHECK(beat != 0.25f);
}

TEST_CASE("a same-layer source modulates each layer by its own modulator", "[core][mod]")
{
    // LFO 1 on every layer's Pitch from each layer's own LFO 1, at four rates; and layer B's LFO
    // 1 on layer A's Texture.
    Processor p({kRate, 512, 13, Mode::Fx});
    const std::array<float, 4> rates{0.0f, 2.0f, -2.0f, 1.0f}; // 1, 4, 0.25 and 2 Hz
    for (uint8_t L = 0; L < 4; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 1.0f);
        p.setBase(slotOf(LayerParam::Lfo1Rate, L), rates[L]);
    }
    ModRoutes routes{};
    routes[0] = route(ModSource::Lfo1, ParamSlot::Pitch, kAllLayers, 12.0f, ModCurve::Bipolar);
    routes[1] = route(ModSource::Lfo1, ParamSlot::Texture, 0, 1.0f, ModCurve::Unipolar, 1);
    p.setRoutes(routes);
    Stereo out = silence(10000);
    render(p, nullptr, 0, 10000, {77}, {}, out);
    std::array<float, 4> u{};
    for (uint8_t L = 0; L < 4; ++L)
    {
        INFO("layer " << int{L});
        u[L] = p.sourceValue(ModSource::Lfo1, L);
        const float x = 12.0f * (2.0f * u[L] - 1.0f);
        CHECK(bitsOf(p.effective(slotOf(LayerParam::Pitch, L))) ==
              bitsOf(routed(slotOf(LayerParam::Pitch, L), 0.0f, x, Mode::Fx)));
    }
    CHECK(u[0] != u[1]);
    CHECK(u[1] != u[2]);
    CHECK(u[2] != u[3]);
    CHECK(bitsOf(p.effective(ParamSlot::Texture)) ==
          bitsOf(routed(ParamSlot::Texture, p.base(ParamSlot::Texture), u[1], Mode::Fx)));
    // The others' Textures have no route.
    CHECK(p.effective(ParamSlot::B_Texture) == p.base(ParamSlot::B_Texture));
}

TEST_CASE("source kind modulated per cell does not allocate or click", "[core][mod][rt]")
{
    // Review Focus 3: LFO 1, a 20 Hz square, on layer A's Source, depth 4 over Sample: the layer
    // switches between its sample and (Oscillator's fallback) its ring 40 times a second. The
    // tripwire holds, the output is finite and has no click, and the grains finish on the source
    // they started on (ASan sees no read of a freed slot).
    constexpr int64_t kFrames = 48000;
    constexpr uint32_t kBlock = 512;
    const Stereo input = sineInput(kFrames, 220.0);
    Processor p({kRate, kBlock, 15, Mode::Fx});
    p.setSourceNow(0, sine(440.0));
    p.setBase(ParamSlot::Mix, 1.0f);
    p.setBase(ParamSlot::Source, 0.0f); // Sample
    p.setBase(ParamSlot::Lfo1Rate, std::log2(20.0f));
    p.setBase(ParamSlot::Lfo1Shape, 4.0f); // Square
    p.setBase(ParamSlot::Density, 6.0f);
    const ModRoutes routes =
        only(route(ModSource::Lfo1, ParamSlot::Source, 0, 4.0f, ModCurve::Unipolar));
    Stereo out = silence(kFrames);
    std::vector<int> kinds(kFrames / kBlock + 1, -1);
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        p.setRoutes(routes);
        for (int64_t done = 0, k = 0; done < kFrames; ++k)
        {
            const auto n = static_cast<uint32_t>(std::min<int64_t>(kBlock, kFrames - done));
            const auto j = static_cast<std::size_t>(done);
            const float *in[2] = {input.left.data() + j, input.right.data() + j};
            float *o[2] = {out.left.data() + j, out.right.data() + j};
            p.process(in, 2, o, 2, n, {});
            kinds[static_cast<std::size_t>(k)] = static_cast<int>(p.engine(0).sourceMode());
            done += n;
        }
    }
    const uint64_t allocations = AllocTripwire::count();
    if (AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
    CHECK(std::count(kinds.begin(), kinds.end(), static_cast<int>(gg::SourceMode::Sample)) > 5);
    CHECK(std::count(kinds.begin(), kinds.end(), static_cast<int>(gg::SourceMode::Live)) > 5);
    REQUIRE(allFinite(out.left));
    REQUIRE(allFinite(out.right));
    REQUIRE(peak(out.left) > 0.05f);
    const gg::metrics::AudioData audio{kRate, {out.left, out.right}};
    CHECK(gg::metrics::computeMetrics(audio).clickScore < 15.0);
}

TEST_CASE("every source routed does not allocate", "[core][mod][rt]")
{
    // Spec §7: four layers on, every source routed to every layer, notes, controls and a
    // transport, publishing on, in calls of every shape, all armed.
    Processor p({kRate, 256, 17, Mode::Instrument});
    p.setPublishing(true);
    for (uint8_t L = 0; L < 4; ++L)
    {
        p.setBase(gg::core::layerOnSlot(L), 1.0f);
        p.setSourceNow(L, sine(220.0 * (L + 1)));
    }
    varyModulators(p);
    const ModRoutes routes = allSourceRoutes(Mode::Instrument);
    constexpr uint32_t kFrames = 1000;
    const Stereo input = noise(kFrames, 0.5f, 19);
    const float *in[2] = {input.left.data(), input.right.data()};
    std::vector<float> o0(kFrames), o1(kFrames);
    float *out[2] = {o0.data(), o1.data()};
    const std::array<NoteEvent, 3> notes{{{10, NoteEventType::On, 0, 60, 0.7f},
                                          {500, NoteEventType::On, 1, 72, 0.3f},
                                          {900, NoteEventType::Off, -1, -1}}};
    const std::array<ControlEvent, 3> controls{{{5, ControlKind::ModWheel, 0, 0.6f},
                                                {300, ControlKind::PitchBend, -1, 0.9f},
                                                {700, ControlKind::Aftertouch, 3, 0.2f}}};
    const std::array<ParamEvent, 2> events{
        {{100, ParamSlot::Lfo1Rate, false, 4.0}, {600, ParamSlot::B_StepLength, false, 7.0}}};
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        p.setRoutes(routes);
        p.setTransport({true, 0.0, 128.0, true, 4});
        for (int i = 0; i < 10; ++i)
        {
            p.process(in, 2, out, 2, kFrames, events, notes, controls);
            p.process(in, 2, out, 2, 0, {}, {}, controls);
            p.process(in, 2, out, 2, 1, {}, notes, controls);
            p.process(in, 1, out, 2, kFrames, events, notes, controls);
            p.process(nullptr, 0, out, 2, 100, {});
            p.process(in, 2, nullptr, 0, 100, {}, {}, controls);
            p.setTransport({true, 2.0 * i, 128.0, i % 2 == 0, 4});
            gg::core::GrainVis g;
            while (p.grainQueue().pop(g))
            {
            }
        }
    }
    const uint64_t allocations = AllocTripwire::count();
    CHECK(allFinite(o0));
    CHECK(allFinite(o1));
    if (AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("modulation is block-size invariant", "[core][mod][determinism]")
{
    // Every source routed (allSourceRoutes), the modulators' controls different on every layer,
    // the four-layer automation of the layers' invariance test, MIDI controls off the grid, a
    // transport, and in the instrument notes with velocities: the same bits, grains, ring heads
    // and sources at any host block size and maxBlockSize, serially and on a pool. And with only
    // event-driven routes (no route time-varying), where the sources step as rendering passes the
    // boundaries rather than at applied ones: the same again.
    const Stereo input = noise(kInvarianceFrames, 0.5f, 49);
    const auto events = layerAutomation();
    for (const auto [mode, timeVarying] :
         {std::pair{Mode::Fx, true}, std::pair{Mode::Instrument, true}, std::pair{Mode::Fx, false},
          std::pair{Mode::Instrument, false}})
    {
        INFO((mode == Mode::Fx ? "FX" : "instrument")
             << (timeVarying ? ", every source routed" : ", event-driven routes only"));
        LayersRender ref;
        renderLayers(input, events, 4096, {4096}, ref, modSetup(mode, nullptr, timeVarying));
        // Not vacuous: the per-layer sources moved, and differ between layers.
        REQUIRE(ref.sources[0] != ref.sources[1]);
        REQUIRE(ref.sources[0][static_cast<std::size_t>(ModSource::Lfo1)] != 0.0f);
        REQUIRE(allFinite(ref.audio.left));
        REQUIRE(peak(ref.audio.left) > 0.05f);
        REQUIRE_FALSE(ref.log.overflowed);
        for (uint32_t L = 0; L < 4; ++L)
        {
            INFO("layer " << L);
            REQUIRE(ref.log.layer(L).size() > 10);
        }

        struct Split
        {
            uint32_t maxBlock;
            std::vector<uint32_t> blockSizes;
            bool pool;
        };
        std::vector<Split> splits{{4096, {1}, false},   {4096, {7}, false},  {4096, {64}, false},
                                  {4096, {511}, false}, {64, {4096}, false}, {1, {4096}, false},
                                  {256, {4096}, false}, {4096, {7}, true},   {4096, {4096}, true}};
        for (const uint64_t seed : {403, 404})
        {
            splits.push_back({4096, randomSplit(seed), seed == 404});
        }
        StubPool pool(2);
        for (const Split &split : splits)
        {
            INFO("maxBlockSize = " << split.maxBlock << ", first block size = "
                                   << split.blockSizes.front() << " of " << split.blockSizes.size()
                                   << (split.pool ? ", on the pool" : ""));
            LayersRender r;
            renderLayers(input, events, split.maxBlock, split.blockSizes, r,
                         modSetup(mode, split.pool ? &pool : nullptr, timeVarying));
            REQUIRE(r.sources == ref.sources);
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
}

TEST_CASE("time-varying routes split rendering at cells", "[core][mod]")
{
    const Stereo input = noise(48000, 0.5f, 21);
    Stereo out = silence(48000);

    SECTION("one piece per call without, one per cell with")
    {
        CountingRunner counter;
        Processor p({kRate, 4096, 23, Mode::Fx});
        p.setTaskRunner(&counter);
        p.setBase(ParamSlot::LayerBOn, 1.0f);
        // The factory routes, then a route from a source that moves only with its events.
        render(p, &input, 0, 2560, {256}, {}, out);
        CHECK(counter.calls == 10);
        ModRoutes wheel = gg::core::factoryRoutes(Mode::Fx);
        wheel[12] =
            route(ModSource::ModWheel, ParamSlot::Position, kAllLayers, 0.1f, ModCurve::Unipolar);
        p.setRoutes(wheel);
        REQUIRE_FALSE(p.timeVarying());
        render(p, &input, 2560, 5120, {256}, {}, out);
        CHECK(counter.calls == 20);
        // An LFO: a request per cell, eight per call.
        ModRoutes lfo = wheel;
        lfo[13] = route(ModSource::Lfo1, ParamSlot::Position, 0, 0.1f, ModCurve::Bipolar);
        p.setRoutes(lfo);
        REQUIRE(p.timeVarying());
        render(p, &input, 5120, 7680, {256}, {}, out);
        CHECK(counter.calls == 100);
        // Depth 0, or a target it can't reach, is no time-varying route.
        lfo[13].depth = 0.0f;
        p.setRoutes(lfo);
        CHECK_FALSE(p.timeVarying());
        lfo[13] = route(ModSource::Lfo1, ParamSlot::Macro1, 0, 0.1f, ModCurve::Bipolar);
        p.setRoutes(lfo);
        CHECK_FALSE(p.timeVarying());
        lfo[13] = route(ModSource::Lfo1, ParamSlot::Position, 9, 0.1f, ModCurve::Bipolar);
        p.setRoutes(lfo);
        CHECK_FALSE(p.timeVarying());
        render(p, &input, 7680, 10240, {256}, {}, out);
        CHECK(counter.calls == 110);
    }

    SECTION("the engines' parameters follow every cell")
    {
        // A 5 Hz LFO on Position: in calls of 32, each call's effective Position is a new one.
        Processor p({kRate, 4096, 25, Mode::Fx});
        p.setBase(ParamSlot::Lfo1Rate, std::log2(5.0f));
        p.setRoutes(only(route(ModSource::Lfo1, ParamSlot::Position, 0, 0.2f, ModCurve::Bipolar)));
        std::vector<float> positions;
        render(p, &input, 0, 3200, {32}, {}, out, nullptr,
               [&](int64_t) { positions.push_back(p.effective(ParamSlot::Position)); });
        REQUIRE(positions.size() == 100);
        for (std::size_t k = 1; k < positions.size(); ++k)
        {
            INFO("cell " << k);
            CHECK(positions[k] != positions[k - 1]);
        }
    }
}

TEST_CASE("routes survive setBase jumps like macros", "[core][mod]")
{
    // Ruling PU-T2a through a user's route: Macro 2 on layer A's Level (−12 dB at the top) and on
    // Mix (−0.5). setBase of the macro sounds as setting Level and Mix directly: their ramps jump.
    constexpr int64_t kFrames = 24000;
    const Stereo input = noise(kFrames, 0.5f, 27);
    ModRoutes routes{};
    routes[0] = route(ModSource::Macro2, ParamSlot::Level, 0, -12.0f, ModCurve::BipolarPositive);
    routes[1] = route(ModSource::Macro2, ParamSlot::Mix, 0, -0.5f, ModCurve::BipolarPositive);
    const auto run = [&](bool viaMacro, Stereo &audio, Log &log) {
        Processor p({kRate, 512, 29, Mode::Fx});
        p.setRoutes(routes);
        p.setBase(ParamSlot::Mix, 1.0f);
        if (viaMacro)
        {
            p.setBase(ParamSlot::Macro2, 1.0f);
        }
        else
        {
            p.setBase(ParamSlot::Level, -12.0f);
            p.setBase(ParamSlot::Mix, 0.5f);
        }
        CHECK(p.effective(ParamSlot::Level) == -12.0f);
        CHECK(p.effective(ParamSlot::Mix) == 0.5f);
        p.setEventSink(&Log::record, &log);
        audio = silence(kFrames);
        render(p, &input, 0, kFrames, {512}, {}, audio);
    };
    Stereo direct;
    Log directLog;
    run(false, direct, directLog);
    Stereo macro;
    Log macroLog;
    run(true, macro, macroLog);
    REQUIRE(peak(direct.left) > 0.05f);
    CHECK(macroLog.layer(0) == directLog.layer(0));
    CHECK(sameBits(macro.left, direct.left));
    CHECK(sameBits(macro.right, direct.right));
}
