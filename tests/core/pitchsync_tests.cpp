// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Clock Pitch in the Processor (M3 pitch-sync spec S2, S4, S6, S7, S9, §4.3, §4.4): a Voice
// layer at Clock Pitch takes its grain rate from the note (f0, with the bend and the tuning
// expression), its read rate from Pitch (the formant), its refresh rate from Density and its
// gain from the grain's length in periods.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "LayersFixture.h"
#include "gg/core/MathDet.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Processor.h"
#include "gg/metrics/Metrics.h"
#include "support/AllocTripwire.h"

using Catch::Matchers::WithinRel;
using gg::exp2Det;
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

    void on(int64_t t, int16_t key, int16_t channel = 0, float velocity = 1.0f, int32_t id = -1)
    {
        notes.push_back({t, {0, NoteEventType::On, channel, key, velocity, id}});
    }
    void control(int64_t t, ControlKind kind, int16_t channel, float value, int16_t key = -1,
                 int32_t id = -1)
    {
        controls.push_back({t, {0, kind, channel, value, key, id}});
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

/// Renders [from, to) through p in calls of blockSizes (cycling), each given the script's events
/// that fall inside it.
void render(Processor &p, Script script, int64_t from, int64_t to,
            const std::vector<uint32_t> &blockSizes, Stereo &out, const Stereo *input = nullptr,
            Log *log = nullptr)
{
    script.sort();
    if (static_cast<int64_t>(out.left.size()) < to)
    {
        out.left.resize(static_cast<size_t>(to), 0.0f);
        out.right.resize(static_cast<size_t>(to), 0.0f);
    }
    std::vector<NoteEvent> notes;
    std::vector<ControlEvent> controls;
    std::vector<ParamEvent> params;
    size_t nn = 0, nc = 0, np = 0;
    const auto skip = [from](const auto &list, size_t &i) {
        while (i < list.size() && list[i].first < from)
        {
            ++i;
        }
    };
    skip(script.notes, nn);
    skip(script.controls, nc);
    skip(script.params, np);
    int64_t done = from;
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
        if (log != nullptr)
        {
            ++log->call;
        }
    }
}

ParamSlot slotA(LayerParam p) { return layerSlot(p, 0); }

/// Size as the log2 of `seconds` (the parameter's unit).
float sizeOf(double seconds) { return static_cast<float>(std::log2(seconds)); }

/// A Processor with layer A at Mode Voice, Clock Pitch (when `pitch`), Spray 0, Distribution 0 and
/// Density 6; an instrument over a sine sample, or an FX.
std::unique_ptr<Processor> pitchProcessor(Mode mode = Mode::Instrument, bool pitch = true,
                                          bool voice = true)
{
    auto p = std::make_unique<Processor>(ProcessorConfig{kRate, 4096, 7, mode});
    if (mode == Mode::Instrument)
    {
        p->setSourceNow(0, gg::test::layers::sine(220.0));
    }
    p->setBase(slotA(LayerParam::Spray), 0.0f);
    p->setBase(slotA(LayerParam::Distribution), 0.0f);
    p->setBase(slotA(LayerParam::Density), 6.0f);
    if (voice)
    {
        p->setBase(slotA(LayerParam::Mode), 1.0f);
    }
    if (pitch)
    {
        p->setBase(slotA(LayerParam::Clock), 1.0f);
    }
    return p;
}

std::unique_ptr<Log> attachLog(Processor &p)
{
    auto log = std::make_unique<Log>();
    p.setEventSink(&Log::record, log.get());
    return log;
}

Stereo sineInput(int64_t frames, double hz)
{
    Stereo s = gg::test::layers::silence(frames);
    for (int64_t i = 0; i < frames; ++i)
    {
        const auto v = static_cast<float>(
            0.4 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
        s.left[static_cast<size_t>(i)] = v;
        s.right[static_cast<size_t>(i)] = v;
    }
    return s;
}

double clickScore(const Stereo &s)
{
    const gg::metrics::AudioData audio{kRate, {s.left, s.right}};
    return gg::metrics::computeMetrics(audio).clickScore;
}

std::vector<gg::GrainSpec> grainsOf(const Log &log, uint32_t ordinal)
{
    std::vector<gg::GrainSpec> out;
    for (const gg::GrainSpec &g : log.layer(0))
    {
        if (g.voice == ordinal)
        {
            out.push_back(g);
        }
    }
    return out;
}

/// The params voice slot 0 of layer A last took, after one block with the script.
gg::CloudParams cloudAfter(Processor &p, const Script &s, int64_t frames = 512)
{
    Stereo out;
    render(p, s, 0, frames, {256}, out);
    return p.engine(0).voiceParams(0).cloud;
}

bool sameAudio(const Stereo &a, const Stereo &b)
{
    return gg::test::layers::sameBits(a.left, b.left) &&
           gg::test::layers::sameBits(a.right, b.right);
}
} // namespace

TEST_CASE("pitchsync_f0_from_the_note", "[core][pitchsync]")
{
    for (const auto &[key, hz] : {std::pair{int16_t{69}, 440.0}, std::pair{int16_t{57}, 220.0}})
    {
        auto p = pitchProcessor();
        Script s;
        s.on(0, key);
        const gg::CloudParams c = cloudAfter(*p, s);
        CHECK(c.clock == gg::Clock::Pitch);
        CHECK_THAT(c.densityHz, WithinRel(hz, 1e-12));
    }
}

TEST_CASE("pitchsync_bend_and_tuning_move_f0_not_the_rate", "[core][pitchsync]")
{
    auto p = pitchProcessor();
    p->setBase(ParamSlot::BendRange, 4.0f);
    p->setBase(slotA(LayerParam::Pitch), 3.0f);
    Script s;
    // 12288 of 16383 is exactly +0.5 of the bend; at Bend Range 4 that is +2 semitones.
    s.control(0, ControlKind::PitchBend, 0, 12288.0f / 16383.0f);
    s.on(0, 69, 0, 1.0f, 100);
    s.control(0, ControlKind::NoteTuning, -1, 0.5f, -1, 100);
    const gg::CloudParams c = cloudAfter(*p, s);
    CHECK(c.densityHz == gg::core::noteFrequencyHz(9.0 + 2.0 + 0.5));
    CHECK(c.pitchSemitones == 3.0);
}

TEST_CASE("pitchsync_pitch_moves_the_rate_not_f0", "[core][pitchsync]")
{
    Script s;
    s.on(0, 64); // off middle C, so a key term leaking into the read rate shows
    auto base = pitchProcessor();
    const gg::CloudParams c0 = cloudAfter(*base, s);
    CHECK(c0.pitchSemitones == 0.0);
    SECTION("Pitch +12")
    {
        auto p = pitchProcessor();
        p->setBase(slotA(LayerParam::Pitch), 12.0f);
        const gg::CloudParams c = cloudAfter(*p, s);
        CHECK(c.pitchSemitones == 12.0);
        CHECK(c.densityHz == c0.densityHz);
    }
    SECTION("a Pitch route offset adds")
    {
        auto p = pitchProcessor();
        p->setBase(slotA(LayerParam::Pitch), 12.0f);
        gg::core::ModRoutes routes = gg::core::factoryRoutes(Mode::Instrument);
        routes[13] = ModRoute{ModSource::Velocity,
                              gg::core::kSameLayer,
                              ParamSlot::Pitch,
                              gg::core::kAllLayers,
                              5.0f,
                              gg::core::ModCurve::Unipolar};
        p->setRoutes(routes);
        const gg::CloudParams c = cloudAfter(*p, s);
        CHECK(c.pitchSemitones == 17.0);
        CHECK(c.densityHz == c0.densityHz);
    }
    SECTION("the sum clamps to 48")
    {
        auto p = pitchProcessor();
        p->setBase(slotA(LayerParam::Pitch), 24.0f);
        gg::core::ModRoutes routes = gg::core::factoryRoutes(Mode::Instrument);
        routes[13] = ModRoute{ModSource::Velocity,
                              gg::core::kSameLayer,
                              ParamSlot::Pitch,
                              gg::core::kAllLayers,
                              30.0f,
                              gg::core::ModCurve::Unipolar};
        p->setRoutes(routes);
        CHECK(cloudAfter(*p, s).pitchSemitones == 48.0);
    }
}

TEST_CASE("pitchsync_f0_extremes_clamp", "[core][pitchsync]")
{
    // Review focus 3: f0 clamps to [1 Hz, sampleRate / 4]; lengths stay in [16, 2 s].
    const auto run = [](int16_t key, float tuning) {
        auto p = pitchProcessor();
        auto log = attachLog(*p);
        Script s;
        s.control(0, ControlKind::NoteTuning, -1, tuning, -1, 5);
        s.on(0, key, 0, 1.0f, 5);
        s.control(0, ControlKind::NoteTuning, -1, tuning, -1, 5);
        Stereo out;
        render(*p, s, 0, 120000, {256}, out, nullptr, log.get());
        CHECK(gg::test::layers::allFinite(out.left));
        CHECK(gg::test::layers::allFinite(out.right));
        const auto grains = log->layer(0);
        REQUIRE(grains.size() >= 3);
        for (const gg::GrainSpec &g : grains)
        {
            CHECK(g.durationSamples >= 16.0);
            CHECK(g.durationSamples <= 96000.0);
        }
        return grains;
    };
    SECTION("key 0 at -120: 1 Hz")
    {
        const auto g = run(0, -120.0f);
        CHECK(g[1].onsetSample - g[0].onsetSample == 48000);
    }
    SECTION("key 127 at +120: a quarter of the rate")
    {
        const auto g = run(127, 120.0f);
        CHECK(g[1].onsetSample - g[0].onsetSample == 4);
        CHECK(g[100].onsetSample - g[99].onsetSample == 4);
    }
}

TEST_CASE("pitchsync_density_is_the_refresh", "[core][pitchsync]")
{
    Script s;
    s.on(0, 69, 0, 1.0f, 3);
    SECTION("Density is the refresh rate, Smear's term left out")
    {
        auto p = pitchProcessor();
        p->setBase(slotA(LayerParam::Density), 4.0f);
        p->setBase(slotA(LayerParam::Smear), 1.0f);
        const gg::CloudParams c = cloudAfter(*p, s);
        CHECK(c.refreshHz == 16.0);
        CHECK_THAT(c.densityHz, WithinRel(440.0, 1e-12));
    }
    SECTION("the voice's Density offset is in it")
    {
        auto p = pitchProcessor();
        p->setBase(slotA(LayerParam::Density), 4.0f);
        Script t = s;
        t.control(0, ControlKind::NotePressure, -1, 1.0f, -1, 3);
        const gg::CloudParams c = cloudAfter(*p, t);
        CHECK(c.refreshHz == exp2Det(4.0 + 2.0));
    }
}

TEST_CASE("pitchsync_gain_is_one_over_root_n", "[core][pitchsync]")
{
    Script s;
    s.on(0, 60);
    for (const double seconds : {0.15, 0.005})
    {
        auto p = pitchProcessor();
        p->setRoutes(gg::core::ModRoutes{}); // the factory routes move Size and Density
        p->setBase(slotA(LayerParam::Size), sizeOf(seconds));
        const gg::CloudParams c = cloudAfter(*p, s);
        const double sec = exp2Det(static_cast<double>(sizeOf(seconds)));
        CHECK(c.gain == static_cast<float>(
                            1.0 / std::sqrt(std::max(1.0, sec * gg::core::kPeriodsPerSizeSecond))));
        if (seconds < 0.01)
        {
            CHECK(c.gain == 1.0f);
        }
        else
        {
            CHECK(c.gain < 1.0f);
        }
    }
}

TEST_CASE("pitchsync_clock_ignored_on_cloud_layers", "[core][pitchsync]")
{
    for (const Mode mode : {Mode::Instrument, Mode::Fx})
    {
        const Stereo input = sineInput(96000, 330.0);
        const auto run = [&](bool pitch, std::vector<gg::GrainSpec> &events) {
            auto p = pitchProcessor(mode, pitch, false);
            auto log = attachLog(*p);
            Script s;
            s.on(0, 60);
            s.on(0, 64);
            s.on(0, 67);
            Stereo out;
            render(*p, s, 0, 96000, {256}, out, mode == Mode::Fx ? &input : nullptr, log.get());
            events = log->layer(0);
            return out;
        };
        std::vector<gg::GrainSpec> ea, eb;
        const Stereo a = run(true, ea);
        const Stereo b = run(false, eb);
        CHECK(sameAudio(a, b));
        CHECK_FALSE(ea.empty());
        CHECK(ea == eb);
    }
}

TEST_CASE("pitchsync_free_voice_unchanged", "[core][pitchsync]")
{
    const auto run = [](int mode) {
        auto p = pitchProcessor(Mode::Instrument, false);
        if (mode == 1)
        {
            p->setBase(slotA(LayerParam::Clock), 0.0f);
        }
        Script s;
        s.on(0, 60);
        s.on(2000, 67);
        Stereo out;
        render(*p, s, 0, 48000, {256}, out);
        return out;
    };
    CHECK(sameAudio(run(0), run(1)));
}

TEST_CASE("pitchsync_clock_change_recomputes_once", "[core][pitchsync]")
{
    auto p = pitchProcessor(Mode::Instrument, false);
    Script s;
    s.on(0, 60);
    s.on(0, 64);
    s.on(0, 67);
    Stereo out;
    render(*p, s, 0, 256, {256}, out);
    const uint64_t before = p->voiceParamRecomputes();
    Script flip;
    flip.param(512, slotA(LayerParam::Clock), 1.0);
    render(*p, flip, 256, 768, {256}, out);
    const uint64_t after = p->voiceParamRecomputes();
    CHECK(after == before + 3);
    render(*p, {}, 768, 1280, {256}, out);
    CHECK(p->voiceParamRecomputes() == after);
}

TEST_CASE("pitchsync_clock_and_note_same_cell", "[core][pitchsync]")
{
    // Review focus 5: the Clock change and a note-on at one boundary give that voice one grain.
    auto p = pitchProcessor(Mode::Instrument, false);
    auto log = attachLog(*p);
    Script s;
    s.on(0, 60);
    s.on(2048, 64);
    s.param(2048, slotA(LayerParam::Clock), 1.0);
    Stereo out;
    render(*p, s, 0, 8192, {256}, out, nullptr, log.get());
    const auto fresh = grainsOf(*log, 1);
    REQUIRE(fresh.size() >= 2);
    CHECK(fresh[0].onsetSample == 2048);
    CHECK(fresh[1].onsetSample > 2048);
    const auto old = grainsOf(*log, 0);
    size_t at = 0;
    for (const gg::GrainSpec &g : old)
    {
        at += g.onsetSample == 2048 ? 1u : 0u;
    }
    CHECK(at == 1);
}

TEST_CASE("pitchsync_steal_starts_fresh", "[core][pitchsync]")
{
    // Review focus 4: 17 notes over the live ring steal a voice; every voice's first grain is a
    // refresh with its own draw, anchored behind its own onset, never the slot's old anchor.
    auto p = pitchProcessor(Mode::Fx);
    auto log = attachLog(*p);
    const Stereo input = sineInput(96000, 330.0);
    Script s;
    for (int16_t k = 0; k < 17; ++k)
    {
        // From 0.5 s: no held anchor reads the ring before the input began (a kink, not a click).
        s.on(24000 + 1500 * k, static_cast<int16_t>(48 + 2 * k), 0, 1.0f, 10 + k);
    }
    Stereo out;
    render(*p, s, 0, 96000, {256}, out, &input, log.get());
    std::vector<double> behind;
    for (uint32_t ord = 0; ord < 17; ++ord)
    {
        const auto g = grainsOf(*log, ord);
        REQUIRE_FALSE(g.empty());
        behind.push_back(static_cast<double>(g[0].onsetSample) - g[0].startPos);
    }
    for (const double b : behind)
    {
        CHECK(std::abs(b - behind[0]) < 2.0);
    }
    CHECK(gg::test::layers::allFinite(out.left));
    CHECK(clickScore(out) < 15.0);
}

TEST_CASE("pitchsync_block_size_invariant", "[core][pitchsync][determinism]")
{
    const Stereo input = sineInput(96000, 220.0);
    Script s;
    s.param(0, slotA(LayerParam::Density), 1.0);
    for (int i = 1; i <= 12; ++i)
    {
        s.param(i * 4000, slotA(LayerParam::Density), 1.0 + 0.5 * i);
    }
    s.param(20000, slotA(LayerParam::Clock), 0.0);
    s.param(40000, slotA(LayerParam::Clock), 1.0);
    s.on(1000, 48, 1, 0.8f, 1);
    s.on(1000, 55, 2, 0.8f, 2);
    s.on(30000, 64, 3, 0.6f, 3);
    // An MPE slide of +12 over 1 s on channel 1: 2048 of the 14-bit range at MPE Bend Range 48.
    for (int i = 0; i <= 10; ++i)
    {
        const float v14 = 8192.0f + 2048.0f * static_cast<float>(i) / 10.0f;
        s.control(1000 + i * 4800, ControlKind::PitchBend, 1, v14 / 16383.0f);
    }
    const auto run = [&](uint32_t block, std::vector<gg::GrainSpec> &events) {
        auto p = pitchProcessor(Mode::Fx);
        p->setBase(ParamSlot::Mpe, 1.0f);
        auto log = attachLog(*p);
        Stereo out;
        render(*p, s, 0, 96000, {block}, out, &input, log.get());
        events = log->layer(0);
        return out;
    };
    std::vector<gg::GrainSpec> ref;
    const Stereo a = run(256, ref);
    CHECK(ref.size() > 50);
    for (const uint32_t block : {1u, 7u, 32u, 4096u})
    {
        std::vector<gg::GrainSpec> ev;
        const Stereo b = run(block, ev);
        CHECK(sameAudio(a, b));
        CHECK(ev == ref);
    }
}

TEST_CASE("pitchsync_no_allocation", "[core][pitchsync][rt]")
{
    auto p = pitchProcessor();
    p->setBase(slotA(LayerParam::Size), sizeOf(0.005));
    Script s;
    for (int16_t k = 112; k < 128; ++k)
    {
        s.on(0, k);
    }
    s.sort();
    std::vector<NoteEvent> notes;
    for (const auto &[t, e] : s.notes)
    {
        notes.push_back(e);
    }
    std::vector<float> l(64), r(64);
    float *o[2] = {l.data(), r.data()};
    gg::test::AllocTripwire::resetCount();
    {
        gg::test::AllocTripwire::Scope armed;
        p->process(nullptr, 0, o, 2, 64, {}, notes, {});
        for (int i = 0; i < 750; ++i)
        {
            p->process(nullptr, 0, o, 2, 64, {});
        }
    }
    if (gg::test::AllocTripwire::available())
    {
        REQUIRE(gg::test::AllocTripwire::count() == 0);
    }
    CHECK(gg::test::layers::allFinite(l));
}
