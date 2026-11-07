// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The playhead in the Processor (M3 sub-project 3b's spec §4.1, §4.5, HB6, H4, H6, H7): Scan
// Speed's law in toCloudParams, per-voice Scan Speed (a route's offset adds to v before the law),
// Scan Track's ratio, the restarts, and block-size invariance with the speed moving.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "LayersFixture.h"
#include "gg/core/MathDet.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Processor.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using gg::core::ControlEvent;
using gg::core::ControlKind;
using gg::core::LayerParam;
using gg::core::layerSlot;
using gg::core::Mode;
using gg::core::ModRoute;
using gg::core::ModSource;
using gg::core::NoteEvent;
using gg::core::NoteEventType;
using gg::core::ParamEvent;
using gg::core::ParamSlot;
using gg::core::Processor;
using gg::core::ProcessorConfig;
using gg::test::layers::Log;
using gg::test::layers::Stereo;

namespace
{
constexpr double kRate = 48000.0;

struct Script
{
    std::vector<std::pair<int64_t, NoteEvent>> notes;
    std::vector<std::pair<int64_t, ControlEvent>> controls;
    std::vector<std::pair<int64_t, ParamEvent>> params;

    void on(int64_t t, int16_t key, int16_t channel = 0, float velocity = 1.0f)
    {
        notes.push_back({t, {0, NoteEventType::On, channel, key, velocity, -1}});
    }
    void off(int64_t t, int16_t key, int16_t channel = 0)
    {
        notes.push_back({t, {0, NoteEventType::Off, channel, key, 0.0f, -1}});
    }
    void control(int64_t t, ControlKind kind, int16_t channel, float value)
    {
        controls.push_back({t, {0, kind, channel, value, -1, -1}});
    }
    void param(int64_t t, ParamSlot slot, double value)
    {
        params.push_back({t, {0, slot, false, value}});
    }
    void sort()
    {
        const auto byTime = [](const auto &a, const auto &b) { return a.first < b.first; };
        std::stable_sort(notes.begin(), notes.end(), byTime);
        std::stable_sort(controls.begin(), controls.end(), byTime);
        std::stable_sort(params.begin(), params.end(), byTime);
    }
};

/// Renders [0, to) through p in calls of blockSizes (cycling), each given the script's events
/// that fall inside it.
void render(Processor &p, Script script, int64_t to, const std::vector<uint32_t> &blockSizes,
            Stereo &out, const Stereo *input = nullptr)
{
    script.sort();
    out = gg::test::layers::silence(to);
    std::vector<NoteEvent> notes;
    std::vector<ControlEvent> controls;
    std::vector<ParamEvent> params;
    size_t nn = 0, nc = 0, np = 0;
    int64_t done = 0;
    for (size_t k = 0; done < to; ++k)
    {
        const auto n =
            static_cast<uint32_t>(std::min<int64_t>(blockSizes[k % blockSizes.size()], to - done));
        const int64_t limit = done + n;
        notes.clear();
        controls.clear();
        params.clear();
        for (; nn < script.notes.size() && script.notes[nn].first < limit; ++nn)
        {
            NoteEvent e = script.notes[nn].second;
            e.offset = static_cast<uint32_t>(script.notes[nn].first - done);
            notes.push_back(e);
        }
        for (; nc < script.controls.size() && script.controls[nc].first < limit; ++nc)
        {
            ControlEvent e = script.controls[nc].second;
            e.offset = static_cast<uint32_t>(script.controls[nc].first - done);
            controls.push_back(e);
        }
        for (; np < script.params.size() && script.params[np].first < limit; ++np)
        {
            ParamEvent e = script.params[np].second;
            e.offset = static_cast<uint32_t>(script.params[np].first - done);
            params.push_back(e);
        }
        const auto j = static_cast<size_t>(done);
        const float *in[2] = {input != nullptr ? input->left.data() + j : nullptr,
                              input != nullptr ? input->right.data() + j : nullptr};
        float *o[2] = {out.left.data() + j, out.right.data() + j};
        p.process(input != nullptr ? in : nullptr, input != nullptr ? 2 : 0, o, 2, n, params, notes,
                  controls);
        done += n;
    }
}

ParamSlot slotA(LayerParam p) { return layerSlot(p, 0); }

/// Layer A at Mode Voice (or Cloud) over a 220 Hz sine (the instrument), Spray 0, Distribution 0,
/// Density 6 (64 Hz), Position 0.
std::unique_ptr<Processor> make(Mode mode, bool voice = true)
{
    auto p = std::make_unique<Processor>(ProcessorConfig{kRate, 4096, 7, mode});
    if (mode == Mode::Instrument)
    {
        p->setSourceNow(0, gg::test::layers::sine(220.0));
    }
    p->setBase(slotA(LayerParam::Spray), 0.0f);
    p->setBase(slotA(LayerParam::Distribution), 0.0f);
    p->setBase(slotA(LayerParam::Density), 6.0f);
    p->setBase(slotA(LayerParam::Position), 0.0f);
    if (voice)
    {
        p->setBase(slotA(LayerParam::Mode), 1.0f);
    }
    return p;
}

std::vector<gg::GrainSpec> grainsOf(const Log &log, uint32_t layer, uint32_t voice)
{
    std::vector<gg::GrainSpec> out;
    for (const gg::GrainSpec &g : log.layer(layer))
    {
        if (g.voice == voice)
        {
            out.push_back(g);
        }
    }
    return out;
}

double onsetOf(const gg::GrainSpec &g) { return static_cast<double>(g.onsetSample) + g.onsetFrac; }

/// The playhead's speed in source frames per output sample, from the first grain at or after
/// `from` and the last one before `to` (no wrap between them).
double speedOf(const std::vector<gg::GrainSpec> &grains, int64_t from, int64_t to)
{
    const gg::GrainSpec *first = nullptr;
    const gg::GrainSpec *last = nullptr;
    for (const gg::GrainSpec &g : grains)
    {
        if (g.onsetSample >= from && g.onsetSample < to)
        {
            first = first != nullptr ? first : &g;
            last = &g;
        }
    }
    REQUIRE(first != nullptr);
    REQUIRE(last != first);
    return (last->startPos - first->startPos) / (onsetOf(*last) - onsetOf(*first));
}

/// A voice route, Velocity (or `source`) to layer A's Scan Speed, in slot 14.
void scanRoute(Processor &p, ModSource source, float depth,
               gg::core::ModCurve curve = gg::core::ModCurve::Unipolar)
{
    gg::core::ModRoutes routes = p.routes();
    routes[14] =
        ModRoute{source, gg::core::kSameLayer, slotA(LayerParam::ScanSpeed), 0, depth, curve};
    p.setRoutes(routes);
}
} // namespace

TEST_CASE("toCloudParams carries the scan law", "[core][playhead]")
{
    for (const Mode mode : {Mode::Instrument, Mode::Fx})
    {
        gg::core::ParamValues v{};
        for (size_t s = 0; s < gg::core::kParamCount; ++s)
        {
            v[s] = gg::core::paramMeta(static_cast<ParamSlot>(s), mode).defaultVal;
        }
        const gg::CloudParams rest = gg::core::toCloudParams(v, mode, 0);
        CHECK(rest.scanSpeed == 0.0);
        CHECK(rest.scanEdge == gg::ScanEdge::Wrap);
        v[static_cast<size_t>(slotA(LayerParam::ScanSpeed))] = 0.5f;
        v[static_cast<size_t>(slotA(LayerParam::ScanEdge))] = 2.0f;
        const gg::CloudParams on = gg::core::toCloudParams(v, mode, 0);
        CHECK(on.scanSpeed == 1.0);
        CHECK(on.scanEdge == gg::ScanEdge::Stop);
        v[static_cast<size_t>(slotA(LayerParam::ScanEdge))] = 0.5f; // halves up
        CHECK(gg::core::toCloudParams(v, mode, 0).scanEdge == gg::ScanEdge::Bounce);
        const gg::CloudParams b = gg::core::toCloudParams(v, mode, 1);
        CHECK(b.scanSpeed == 0.0);
    }
}

TEST_CASE("a velocity route gives two voices two speeds", "[core][playhead]")
{
    auto p = make(Mode::Instrument);
    p->setBase(slotA(LayerParam::ScanSpeed), 0.0f);
    scanRoute(*p, ModSource::Velocity, 0.5f);
    auto log = std::make_unique<Log>();
    p->setEventSink(&Log::record, log.get());
    Script s;
    s.on(0, 60, 0, 1.0f);
    s.on(0, 64, 0, 0.0f);
    Stereo out;
    render(*p, s, 24000, {256}, out);
    const auto fast = grainsOf(*log, 0, 0);
    const auto still = grainsOf(*log, 0, 1);
    REQUIRE(fast.size() > 8);
    REQUIRE(still.size() > 8);
    CHECK_THAT(speedOf(fast, 0, 20000), WithinRel(gg::core::scanSpeedOf(0.5f), 1e-6));
    for (const gg::GrainSpec &g : still)
    {
        CHECK(g.startPos == 0.0);
    }
}

TEST_CASE("scan track doubles the speed an octave up", "[core][playhead]")
{
    const auto speeds = [](bool track, int16_t key, float bend) {
        auto p = make(Mode::Instrument);
        p->setBase(slotA(LayerParam::ScanSpeed), 0.5f);
        p->setBase(slotA(LayerParam::ScanTrack), track ? 1.0f : 0.0f);
        // The grains' own pitch moves with the key; the playhead's rate is in source frames per
        // output sample, which is the speed alone.
        auto log = std::make_unique<Log>();
        p->setEventSink(&Log::record, log.get());
        Script s;
        s.control(0, ControlKind::PitchBend, 0, bend);
        s.on(64, key);
        Stereo out;
        render(*p, s, 20000, {256}, out);
        return speedOf(grainsOf(*log, 0, 0), 0, 19000);
    };
    const float center = 8192.0f / 16383.0f;
    CHECK_THAT(speeds(true, 60, center), WithinRel(1.0, 1e-6));
    CHECK_THAT(speeds(true, 72, center), WithinRel(2.0, 1e-6));
    CHECK_THAT(speeds(false, 60, center), WithinRel(1.0, 1e-6));
    CHECK_THAT(speeds(false, 72, center), WithinRel(1.0, 1e-6));
    // Bend Range 2: the 14-bit maximum is (16383 - 8192) / 8192 x 2 semitones.
    const double bent = gg::exp2Det(((16383.0 - 8192.0) / 8192.0 * 2.0) / 12.0);
    CHECK_THAT(speeds(true, 60, 1.0f), WithinRel(bent, 1e-6));
    CHECK_THAT(speeds(false, 60, 1.0f), WithinRel(1.0, 1e-6));
}

TEST_CASE("the cloud playhead restarts at the first key after none", "[core][playhead]")
{
    for (const Mode mode : {Mode::Instrument, Mode::Fx})
    {
        INFO((mode == Mode::Fx ? "fx" : "instrument"));
        const auto grains = [mode](float speed) {
            auto p = make(mode, false);
            p->setBase(slotA(LayerParam::ScanSpeed), speed);
            if (mode == Mode::Fx)
            {
                p->setBase(slotA(LayerParam::Position), 0.3f);
            }
            auto log = std::make_unique<Log>();
            p->setEventSink(&Log::record, log.get());
            Script s;
            Stereo in = gg::test::layers::noise(192000, 0.5f, 3);
            if (mode == Mode::Fx)
            {
                s.param(30000, layerSlot(LayerParam::Freeze, 0), 1.0);
            }
            s.on(48000, 60);
            s.off(60000, 60);
            s.on(144000, 60);
            s.off(160000, 60);
            Stereo out;
            render(*p, s, 192000, {256}, out, mode == Mode::Fx ? &in : nullptr);
            return log->layer(0);
        };
        const auto rest = grains(0.0f);
        const auto scan = grains(0.3f);
        for (const int64_t note : {int64_t{48000}, int64_t{144000}})
        {
            // The grain forced at the note's boundary starts at Position, as at speed 0; the
            // ones after it move on.
            const auto at = [note](const std::vector<gg::GrainSpec> &g, int64_t from) {
                return std::find_if(g.begin(), g.end(),
                                    [&](const gg::GrainSpec &x) { return x.onsetSample >= from; });
            };
            const auto r = at(rest, note);
            const auto sc = at(scan, note);
            REQUIRE(r != rest.end());
            REQUIRE(sc != scan.end());
            CHECK(sc->onsetSample == r->onsetSample);
            CHECK_THAT(sc->startPos, WithinAbs(r->startPos, 1e-9));
            const auto r2 = at(rest, note + 9000);
            const auto sc2 = at(scan, note + 9000);
            REQUIRE(r2 != rest.end());
            REQUIRE(sc2 != scan.end());
            CHECK(std::abs(sc2->startPos - r2->startPos) > 100.0);
        }
    }
}

TEST_CASE("each voice starts at its own position", "[core][playhead]")
{
    auto p = make(Mode::Instrument);
    p->setBase(slotA(LayerParam::ScanSpeed), 0.5f);
    auto log = std::make_unique<Log>();
    p->setEventSink(&Log::record, log.get());
    Script s;
    s.control(0, ControlKind::Timbre, 1, 0.2f);
    s.control(0, ControlKind::Timbre, 2, 0.8f);
    s.on(64, 60, 1);
    s.on(64, 60, 2);
    Stereo out;
    render(*p, s, 16000, {256}, out);
    const auto a = grainsOf(*log, 0, 0);
    const auto b = grainsOf(*log, 0, 1);
    REQUIRE(a.size() > 8);
    REQUIRE(b.size() > 8);
    // Factory route 46: Timbre x 0.5 added to Position, over the 48,000-frame sample.
    CHECK_THAT(a.front().startPos, WithinAbs(0.1 * 48000.0, 1.0));
    CHECK_THAT(b.front().startPos, WithinAbs(0.4 * 48000.0, 1.0));
    CHECK_THAT(speedOf(a, 0, 16000), WithinRel(1.0, 1e-6));
    CHECK_THAT(speedOf(b, 0, 16000), WithinRel(1.0, 1e-6));
}

TEST_CASE("scan speed automation is block-size invariant", "[core][playhead][determinism]")
{
    for (const Mode mode : {Mode::Instrument, Mode::Fx})
    {
        INFO((mode == Mode::Fx ? "fx" : "instrument"));
        constexpr int64_t kFrames = 120000;
        const Stereo in = gg::test::layers::noise(kFrames, 0.5f, 5);
        const auto run = [&](const std::vector<uint32_t> &blocks) {
            auto p = make(mode);
            if (mode == Mode::Instrument)
            {
                p->setSourceNow(1, gg::test::layers::sine(330.0));
            }
            p->setBase(gg::core::layerOnSlot(1), 1.0f);
            p->setBase(layerSlot(LayerParam::Spray, 1), 0.0f);
            p->setBase(layerSlot(LayerParam::Density, 1), 6.0f);
            p->setBase(slotA(LayerParam::ScanSpeed), 0.4f);
            p->setBase(layerSlot(LayerParam::ScanSpeed, 1), 0.4f);
            gg::core::ModRoutes routes = p->routes();
            routes[14] = ModRoute{ModSource::Lfo1,
                                  gg::core::kSameLayer,
                                  slotA(LayerParam::ScanSpeed),
                                  gg::core::kAllLayers,
                                  0.25f,
                                  gg::core::ModCurve::Bipolar};
            p->setRoutes(routes);
            auto log = std::make_unique<Log>();
            p->setEventSink(&Log::record, log.get());
            Script s;
            s.on(2000, 60, 0, 0.7f);
            s.on(5000, 67, 1, 0.4f);
            s.param(35088, slotA(LayerParam::ScanSpeed), 0.55);
            s.param(96000, slotA(LayerParam::ScanEdge), 1.0);
            s.param(96000, layerSlot(LayerParam::ScanEdge, 1), 1.0);
            Stereo out;
            render(*p, s, kFrames, blocks, out, mode == Mode::Fx ? &in : nullptr);
            return std::make_pair(std::move(log), std::move(out));
        };
        const auto [log, out] = run({4096});
        REQUIRE(grainsOf(*log, 0, 0).size() > 10);
        REQUIRE(grainsOf(*log, 0, 1).size() > 10);
        REQUIRE(grainsOf(*log, 1, gg::kNoVoice).size() > 10);
        for (const std::vector<uint32_t> &blocks :
             {std::vector<uint32_t>{1}, std::vector<uint32_t>{7}, std::vector<uint32_t>{32},
              std::vector<uint32_t>{64}, std::vector<uint32_t>{256}, std::vector<uint32_t>{333}})
        {
            INFO("block " << blocks.front());
            const auto [otherLog, other] = run(blocks);
            CHECK(gg::test::layers::sameBits(other.left, out.left));
            CHECK(gg::test::layers::sameBits(other.right, out.right));
            for (uint32_t L = 0; L < 2; ++L)
            {
                CHECK(otherLog->layer(L) == log->layer(L));
            }
        }
    }
}
