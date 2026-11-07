// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Voices in the Processor (M3 Voice spec §3, §3a, §4.2–§4.5): a layer at Mode Voice plays each
// note as a voice with its own grain stream, Env 1 amplitude, per-voice modulation, bend and
// expression, spawning through its release.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "LayersFixture.h"
#include "Render.h"
#include "RenderSpec.h"
#include "gg/core/MathDet.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ModSources.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Processor.h"
#include "gg/metrics/Metrics.h"
#include "support/AllocTripwire.h"
#include "support/StubPool.h"

using Catch::Matchers::WithinRel;
using gg::kNoVoice;
using gg::core::ControlEvent;
using gg::core::ControlKind;
using gg::core::EndedNote;
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
constexpr int64_t kGrid = Processor::kControlGrid;

/// What a render feeds the Processor, by absolute sample (the events' offsets are ignored).
struct Script
{
    std::vector<std::pair<int64_t, NoteEvent>> notes;
    std::vector<std::pair<int64_t, ControlEvent>> controls;
    std::vector<std::pair<int64_t, ParamEvent>> params;

    void on(int64_t t, int16_t key, int16_t channel = 0, float velocity = 1.0f, int32_t id = -1)
    {
        notes.push_back({t, {0, NoteEventType::On, channel, key, velocity, id}});
    }
    void off(int64_t t, int16_t key, int16_t channel = 0, int32_t id = -1)
    {
        notes.push_back({t, {0, NoteEventType::Off, channel, key, 0.0f, id}});
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
/// that fall inside it, into out (resized to `to` if shorter).
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
    notes.reserve(script.notes.size());
    controls.reserve(script.controls.size());
    params.reserve(script.params.size());
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

/// An instrument (or FX) Processor with layer A at Mode Voice over a 220 Hz sine, its Spray 0,
/// Distribution 0 (regular onsets) and Density 6 (64 Hz).
std::unique_ptr<Processor> voiceProcessor(Mode mode = Mode::Instrument, bool voice = true,
                                          uint64_t seed = 7)
{
    auto p = std::make_unique<Processor>(ProcessorConfig{kRate, 4096, seed, mode});
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
    return p;
}

std::unique_ptr<Log> attachLog(Processor &p)
{
    auto log = std::make_unique<Log>();
    p.setEventSink(&Log::record, log.get());
    return log;
}

/// Layer `layer`'s grains of voice `ordinal` (kNoVoice: the Cloud stream's).
std::vector<gg::GrainSpec> grainsOf(const Log &log, uint32_t layer, uint32_t ordinal)
{
    std::vector<gg::GrainSpec> out;
    for (const gg::GrainSpec &g : log.layer(layer))
    {
        if (g.voice == ordinal)
        {
            out.push_back(g);
        }
    }
    return out;
}

/// The effective values in p, as a ParamValues (for envParams).
gg::core::ParamValues effectiveOf(const Processor &p)
{
    gg::core::ParamValues v{};
    for (size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        v[s] = p.effective(static_cast<ParamSlot>(s));
    }
    return v;
}

/// The first boundary c >= on at which an Env 1 gated on over [on, off) and off from off is
/// idle after its step there (the voice's end; V1, B8): the Processor's own Adsr, stepped once
/// per cell.
int64_t idleBoundary(const gg::core::AdsrParams &env, int64_t on, int64_t off)
{
    gg::core::Adsr a;
    const double cell = static_cast<double>(kGrid) / kRate;
    for (int64_t c = on; c < on + 600 * 48000; c += kGrid)
    {
        a.step(env, c < off, cell);
        if (a.idle())
        {
            return c;
        }
    }
    return -1;
}

double clickScore(const Stereo &s)
{
    const gg::metrics::AudioData audio{kRate, {s.left, s.right}};
    return gg::metrics::computeMetrics(audio).clickScore;
}

int64_t lastOnset(const std::vector<gg::GrainSpec> &g)
{
    return g.empty() ? -1 : g.back().onsetSample;
}

/// The grains of `g` with onsets in [from, to).
size_t countIn(const std::vector<gg::GrainSpec> &g, int64_t from, int64_t to)
{
    return static_cast<size_t>(std::count_if(g.begin(), g.end(), [&](const gg::GrainSpec &x) {
        return x.onsetSample >= from && x.onsetSample < to;
    }));
}
} // namespace

TEST_CASE("processor_voice_note_starts_voice", "[core][voice]")
{
    auto p = voiceProcessor();
    auto log = attachLog(*p);
    Script s;
    s.on(1000, 60);
    Stereo out;
    render(*p, s, 0, 9600, {256}, out, nullptr, log.get());
    const auto grains = log->layer(0);
    REQUIRE_FALSE(grains.empty());
    // The voice's forced onset lands on the note's boundary, and every grain is the voice's.
    CHECK(grains.front().onsetSample == 1024);
    for (const gg::GrainSpec &g : grains)
    {
        CHECK(g.voice == 0u);
    }
    CHECK(p->voicesSounding());
}

TEST_CASE("processor_voice_release_keeps_spawning", "[core][voice]")
{
    auto p = voiceProcessor();
    p->setBase(slotA(LayerParam::Env1Release), -1.0f); // 0.5 s
    auto log = attachLog(*p);
    Script s;
    s.on(0, 60);
    s.off(9600, 60);
    Stereo out;
    render(*p, s, 0, 48000, {256}, out, nullptr, log.get());
    const int64_t idle = idleBoundary(gg::core::envParams(effectiveOf(*p), 0, 0), 0, 9600);
    REQUIRE(idle > 9600 + 20000);
    const auto grains = grainsOf(*log, 0, 0);
    // Spawning through the release, at 64 Hz (750 samples apart), and nothing from its end.
    CHECK(countIn(grains, 9600, idle) > 25);
    CHECK(lastOnset(grains) < idle);
    CHECK(lastOnset(grains) >= idle - 750 - kGrid);
    CHECK(log->layer(0).size() == grains.size());
    CHECK_FALSE(p->voicesSounding());
}

TEST_CASE("processor_voice_env1_amplitude", "[core][voice]")
{
    auto p = voiceProcessor();
    p->setBase(slotA(LayerParam::Env1Attack), -1.0f); // 0.5 s
    p->setBase(slotA(LayerParam::Env1Release), -1.0f);
    auto log = attachLog(*p);
    Script s;
    s.on(0, 60);
    s.off(48000, 60);
    Stereo out;
    render(*p, s, 0, 96000, {256}, out, nullptr, log.get());
    const auto grains = grainsOf(*log, 0, 0);
    REQUIRE(grains.size() > 60);
    // The level at a grain's peak (onset + trunc(dur / 2)) with the gate as it stands at the
    // onset (V1: a note-off between a grain's onset and its peak doesn't change that grain): the
    // attack's 1 / 24,000 a sample from boundary 0, then 1, and after the off at 48,000 down at
    // 1 / 24,000.
    const auto expected = [](int64_t onset, int64_t peak) {
        if (onset < 48000)
        {
            return std::min(1.0, static_cast<double>(peak) / 24000.0);
        }
        return std::max(0.0, 1.0 - static_cast<double>(peak - 48000) / 24000.0);
    };
    // The sustained grains' gain is the draw's gain alone.
    float full = 0.0f;
    for (const gg::GrainSpec &g : grains)
    {
        const int64_t peak = g.onsetSample + static_cast<int64_t>(g.durationSamples * 0.5);
        if (peak > 30000 && peak < 47000)
        {
            full = g.gain;
        }
    }
    REQUIRE(full > 0.0f);
    size_t checked = 0;
    for (const gg::GrainSpec &g : grains)
    {
        const int64_t peak = g.onsetSample + static_cast<int64_t>(g.durationSamples * 0.5);
        INFO("onset " << g.onsetSample << " peak " << peak);
        CHECK(std::abs(static_cast<double>(g.gain) - full * expected(g.onsetSample, peak)) <
              2e-4 * full);
        ++checked;
    }
    CHECK(checked == grains.size());
    // The strike's forced grain speaks at the attack's level at its peak, not at 0.
    CHECK(grains.front().onsetSample == 0);
    CHECK(grains.front().gain > 0.0f);
}

TEST_CASE("processor_voice_short_tap", "[core][voice]")
{
    // Review focus 1, B5: a note-on and note-off in one cell plays a short voice and its release.
    auto p = voiceProcessor();
    auto log = attachLog(*p);
    Script s;
    s.on(100, 60);
    s.off(110, 60);
    Stereo out;
    render(*p, s, 0, 256, {256}, out, nullptr, log.get());
    CHECK(p->heldKeys().empty()); // Cloud's held set ignores it, as before
    render(*p, s, 256, 48000, {256}, out, nullptr, log.get());
    // The gate opens at 128 and closes at 160: one cell of attack, then the release.
    const int64_t idle = idleBoundary(gg::core::envParams(effectiveOf(*p), 0, 0), 128, 160);
    const auto grains = grainsOf(*log, 0, 0);
    REQUIRE_FALSE(grains.empty());
    CHECK(grains.front().onsetSample == 128);
    CHECK(grains.front().gain > 0.0f);
    CHECK(countIn(grains, 160, idle) > 5);
    CHECK(lastOnset(grains) < idle);
    CHECK_FALSE(p->voicesSounding());
}

TEST_CASE("processor_voice_steal_oldest", "[core][voice]")
{
    auto p = voiceProcessor();
    auto log = attachLog(*p);
    Script s;
    for (int16_t k = 0; k < 17; ++k)
    {
        s.on(640 * k, static_cast<int16_t>(48 + k));
    }
    const int64_t stealAt = 640 * 16;
    Stereo out;
    render(*p, s, 0, stealAt + 24000, {256}, out, nullptr, log.get());
    // The 17th steals the oldest held voice (ordinal 0): it spawns nothing from the steal.
    const auto first = grainsOf(*log, 0, 0);
    REQUIRE_FALSE(first.empty());
    CHECK(lastOnset(first) < stealAt);
    CHECK(countIn(grainsOf(*log, 0, 1), stealAt + 1000, stealAt + 24000) > 10);
    const auto newest = grainsOf(*log, 0, 16);
    REQUIRE_FALSE(newest.empty());
    CHECK(newest.front().onsetSample == stealAt);
    // Its grains play out their windows: no step in the output.
    CHECK(clickScore(out) < 15.0);
}

TEST_CASE("processor_voice_steal_pedal", "[core][voice]")
{
    // Review focus 2: with the pedal down, a pedal-held voice ranks as released and is stolen
    // first.
    auto p = voiceProcessor();
    auto log = attachLog(*p);
    Script s;
    s.control(0, ControlKind::Sustain, 0, 1.0f);
    for (int16_t k = 0; k < 16; ++k)
    {
        s.on(640 * k, static_cast<int16_t>(48 + k));
    }
    s.off(640 * 15 + 100, 52); // ordinal 4, pedal-held
    const int64_t stealAt = 640 * 16;
    s.on(stealAt, 70);
    Stereo out;
    render(*p, s, 0, stealAt + 24000, {256}, out, nullptr, log.get());
    const auto pedalHeld = grainsOf(*log, 0, 4);
    REQUIRE_FALSE(pedalHeld.empty());
    CHECK(lastOnset(pedalHeld) < stealAt);
    CHECK(countIn(grainsOf(*log, 0, 0), stealAt + 1000, stealAt + 24000) > 10);
    const auto newest = grainsOf(*log, 0, 16);
    REQUIRE_FALSE(newest.empty());
    CHECK(newest.front().onsetSample == stealAt);
    CHECK(clickScore(out) < 15.0);
}

TEST_CASE("processor_voice_bend_mpe_off_and_on", "[core][voice]")
{
    const auto rateAfter = [](Processor &p, Script s, int64_t from) {
        auto log = attachLog(p);
        Stereo out;
        render(p, std::move(s), 0, 9600, {256}, out, nullptr, log.get());
        const auto grains = log->layer(0);
        REQUIRE_FALSE(grains.empty());
        double rate = 0.0;
        for (const gg::GrainSpec &g : grains)
        {
            if (g.onsetSample >= from)
            {
                rate = g.rate;
            }
        }
        return rate;
    };
    const float half = 12288.0f / 16383.0f;   // +0.5 of the 14-bit range
    const float quarter = 4096.0f / 16383.0f; // −0.5
    SECTION("MPE Off: the channel's bend times Bend Range")
    {
        auto p = voiceProcessor();
        Script s;
        s.control(0, ControlKind::PitchBend, 0, half); // before the note: the channel's state
        s.on(32, 72);
        CHECK_THAT(rateAfter(*p, s, 0), WithinRel(std::exp2((12.0 + 0.5 * 2.0) / 12.0), 1e-9));
    }
    SECTION("MPE On: a member channel's bend times MPE Bend Range, plus the master's")
    {
        auto p = voiceProcessor();
        p->setBase(ParamSlot::Mpe, 1.0f);
        Script s;
        s.on(0, 60, 2);
        s.control(3200, ControlKind::PitchBend, 2, half);
        s.control(3200, ControlKind::PitchBend, 0, quarter);
        s.control(3200, ControlKind::PitchBend, 5, half); // no note there: nothing moves
        CHECK_THAT(rateAfter(*p, s, 3200 + 32),
                   WithinRel(std::exp2((0.5 * 48.0 - 0.5 * 2.0) / 12.0), 1e-9));
    }
}

TEST_CASE("processor_voice_pressure_to_density", "[core][voice]")
{
    // Factory route 44 (Pressure to Density, +2 log2 Hz, Voice only).
    const auto onsets = [](bool pressed) {
        auto p = voiceProcessor();
        p->setBase(slotA(LayerParam::Density), 4.0f); // 16 Hz
        auto log = attachLog(*p);
        Script s;
        s.on(0, 60);
        if (pressed)
        {
            s.control(0, ControlKind::PolyPressure, 0, 1.0f, 60);
        }
        Stereo out;
        render(*p, s, 0, 72000, {256}, out, nullptr, log.get());
        return countIn(grainsOf(*log, 0, 0), 24000, 72000);
    };
    const size_t rest = onsets(false);
    const size_t pressed = onsets(true);
    CHECK(rest > 10);
    CHECK(pressed > 3 * rest);
}

TEST_CASE("processor_voice_timbre_to_position", "[core][voice]")
{
    // Factory route 46 (Timbre to Position, +0.5, Voice only): CC 74 on the note's channel.
    auto p = voiceProcessor();
    p->setBase(slotA(LayerParam::Position), 0.0f);
    auto log = attachLog(*p);
    Script s;
    s.on(0, 60, 4);
    s.control(24000, ControlKind::Timbre, 4, 1.0f);
    Stereo out;
    render(*p, s, 0, 48000, {256}, out, nullptr, log.get());
    const auto grains = grainsOf(*log, 0, 0);
    size_t before = 0;
    size_t after = 0;
    for (const gg::GrainSpec &g : grains)
    {
        if (g.onsetSample < 24000)
        {
            CHECK(g.startPos < 2400.0);
            ++before;
        }
        else if (g.onsetSample >= 24032)
        {
            CHECK(std::abs(g.startPos - 24000.0) < 2400.0);
            ++after;
        }
    }
    CHECK(before > 10);
    CHECK(after > 10);
}

TEST_CASE("processor_voice_layers_mixed", "[core][voice][layers]")
{
    // Layer A Voice beside layer B Cloud: B's grains are those of a Cloud-only render.
    const auto render2 = [](bool voiceA) {
        auto p = voiceProcessor(Mode::Instrument, voiceA);
        p->setSourceNow(1, gg::test::layers::sine(330.0));
        p->setBase(gg::core::layerOnSlot(1), 1.0f);
        p->setBase(layerSlot(LayerParam::Density, 1), 5.0f);
        auto log = attachLog(*p);
        Script s;
        s.on(100, 60, 0, 0.7f);
        s.on(5000, 64, 0, 0.4f);
        s.off(20000, 60);
        s.on(30000, 67);
        s.control(31000, ControlKind::PolyPressure, 0, 0.8f, 67);
        s.off(40000, 64);
        Stereo out;
        render(*p, s, 0, 60000, {256, 100, 37}, out, nullptr, log.get());
        return std::make_pair(log->layer(1), grainsOf(*log, 0, kNoVoice).size());
    };
    const auto [mixed, mixedCloudA] = render2(true);
    const auto [cloud, cloudA] = render2(false);
    REQUIRE(mixed.size() > 30);
    CHECK(mixed == cloud);
    CHECK(mixedCloudA == 0);
    CHECK(cloudA > 0);
}

TEST_CASE("processor_mode_switch_mid_note", "[core][voice][determinism]")
{
    // Review focus 3: Voice to Cloud and back while a note sounds.
    const auto run = [](const std::vector<uint32_t> &blocks) {
        auto p = voiceProcessor();
        auto log = std::make_unique<Log>();
        p->setEventSink(&Log::record, log.get());
        Script s;
        s.on(0, 60);
        s.param(12000, slotA(LayerParam::Mode), 0.0);
        s.param(24000, slotA(LayerParam::Mode), 1.0);
        s.on(36000, 64);
        Stereo out;
        render(*p, s, 0, 60000, blocks, out, nullptr, log.get());
        return std::make_pair(std::move(log), std::move(out));
    };
    const auto [log, out] = run({256});
    const auto first = grainsOf(*log, 0, 0);
    const auto cloud = grainsOf(*log, 0, kNoVoice);
    const auto second = grainsOf(*log, 0, 1);
    REQUIRE_FALSE(first.empty());
    REQUIRE_FALSE(cloud.empty());
    REQUIRE_FALSE(second.empty());
    // The voice finishes at the switch; Cloud resumes from the held set there, until the switch
    // back; the held note has no voice until a new note-on starts one.
    CHECK(lastOnset(first) < 12000);
    CHECK(cloud.front().onsetSample == 12000);
    CHECK(lastOnset(cloud) < 24000);
    CHECK(second.front().onsetSample == 36000);
    CHECK(countIn(log->layer(0), 24000, 36000) == 0);
    CHECK(clickScore(out) < 15.0);
    // The same samples and grains at any block size.
    for (const std::vector<uint32_t> &blocks :
         {std::vector<uint32_t>{32}, std::vector<uint32_t>{4096},
          std::vector<uint32_t>{1, 777, 32, 5}})
    {
        const auto [otherLog, other] = run(blocks);
        CHECK(gg::test::layers::sameBits(other.left, out.left));
        CHECK(gg::test::layers::sameBits(other.right, out.right));
        CHECK(otherLog->layer(0) == log->layer(0));
    }
}

TEST_CASE("processor_voice_reset", "[core][voice]")
{
    // Review focus 5: a reset clears the voices with the pool and ends every live note.
    auto p = voiceProcessor();
    auto log = attachLog(*p);
    Script s;
    s.on(0, 60, 0, 1.0f, 7);
    s.on(100, 64, 1, 1.0f, 8);
    s.off(4000, 64, 1, 8);
    Stereo out;
    render(*p, s, 0, 6000, {256}, out, nullptr, log.get());
    std::array<EndedNote, 8> ended{};
    REQUIRE(p->takeEndedNotes(ended) == 0); // the second voice is still releasing
    REQUIRE(p->voicesSounding());
    p->reset();
    CHECK_FALSE(p->voicesSounding());
    CHECK(p->layerActiveGrains(0) == 0);
    REQUIRE(p->takeEndedNotes(ended) == 2);
    CHECK(ended[0].noteId + ended[1].noteId == 15);
    // Nothing sounds until a note, whose voice is the layer's first again.
    log->events.clear();
    Stereo after;
    render(*p, {}, 0, 4800, {256}, after, nullptr, log.get());
    CHECK(log->events.empty());
    CHECK(gg::test::layers::peak(after.left) == 0.0f);
    Script again;
    again.on(0, 62);
    render(*p, again, 0, 4800, {256}, after, nullptr, log.get());
    REQUIRE_FALSE(log->events.empty());
    CHECK(log->events.front().grain.voice == 0u);
}

TEST_CASE("processor_voice_layer_off_clears_voices", "[core][voice]")
{
    // Review focus 5: Layer On ramped off clears the voices at the sample its grains clear.
    auto p = voiceProcessor();
    auto log = attachLog(*p);
    Script s;
    s.on(0, 60);
    s.param(10000, gg::core::layerOnSlot(0), 0.0);
    s.param(20000, gg::core::layerOnSlot(0), 1.0);
    Stereo out;
    render(*p, s, 0, 30000, {256}, out, nullptr, log.get());
    const auto grains = grainsOf(*log, 0, 0);
    REQUIRE(countIn(grains, 5000, 10016) > 3);
    CHECK(lastOnset(grains) < 10016 + 480);
    // On again, the held note has no voice: nothing spawns until a note-on.
    CHECK(countIn(log->layer(0), 10016 + 480, 30000) == 0);
    CHECK_FALSE(p->voicesSounding());
    CHECK(clickScore(out) < 15.0);
}

TEST_CASE("processor_voice_fx_no_note_freeze", "[core][voice][fx]")
{
    // V2: notes don't freeze an FX Voice layer; its voices play the live input.
    const auto head = [](bool voice) {
        auto p = voiceProcessor(Mode::Fx, voice);
        auto log = attachLog(*p);
        const Stereo input = gg::test::layers::noise(24000, 0.5f, 3);
        Script s;
        s.on(0, 60);
        Stereo out;
        render(*p, s, 0, 24000, {256}, out, &input, log.get());
        return std::make_pair(p->engine(0).liveWriteHead(), grainsOf(*log, 0, 0).size());
    };
    const auto [voiceHead, voiceGrains] = head(true);
    const auto [cloudHead, cloudGrains] = head(false);
    CHECK(voiceHead == 24000);
    CHECK(voiceGrains > 10);
    CHECK(cloudHead < 24000 - 9600); // frozen within 20 ms
    CHECK(cloudGrains == 0);
}

namespace
{
/// The invariance scenario: notes with ids and velocities, the pedal, bends, pressure, timbre and
/// expressions, a steal, a short tap, a mode switch, layers A and B at Voice and C at Cloud.
Script richScript()
{
    Script s;
    s.control(0, ControlKind::Sustain, 0, 1.0f);
    for (int16_t k = 0; k < 18; ++k)
    {
        s.on(500 + 811 * k, static_cast<int16_t>(40 + 3 * k), static_cast<int16_t>(k % 4),
             0.3f + 0.04f * static_cast<float>(k), 100 + k);
    }
    s.off(3000, 43, 1, 101);
    s.off(7000, 49, 3, 103);
    s.control(9000, ControlKind::Sustain, 0, 0.0f);
    s.control(2000, ControlKind::PitchBend, 2, 0.8f);
    s.control(4000, ControlKind::Aftertouch, 1, 0.6f);
    s.control(4100, ControlKind::Timbre, 0, 0.9f);
    s.control(5000, ControlKind::PolyPressure, 0, 1.0f, 40);
    s.control(6000, ControlKind::NoteTuning, -1, 3.5f, -1, 104);
    s.control(6100, ControlKind::NoteVolume, -1, 2.0f, -1, 105);
    s.control(6200, ControlKind::NoteBrightness, -1, 0.3f, -1, 106);
    s.on(12000, 90, 5, 0.9f, 300);
    s.off(12001, 90, 5, 300); // a short tap
    s.param(15000, layerSlot(LayerParam::Mode, 1), 0.0);
    s.param(19000, layerSlot(LayerParam::Mode, 1), 1.0);
    s.on(20000, 72, 0, 0.5f, 400);
    s.off(26000, -1, -1); // everything off
    return s;
}

std::unique_ptr<Processor> richProcessor()
{
    auto p = voiceProcessor();
    p->setSourceNow(1, gg::test::layers::sine(330.0));
    p->setSourceNow(2, gg::test::layers::sine(440.0));
    p->setBase(gg::core::layerOnSlot(1), 1.0f);
    p->setBase(gg::core::layerOnSlot(2), 1.0f);
    p->setBase(layerSlot(LayerParam::Mode, 1), 1.0f);
    p->setBase(slotA(LayerParam::Env1Release), -2.0f);
    p->setBase(layerSlot(LayerParam::Env1Release, 1), -1.5f);
    // A per-voice Env 2 route on A, Velocity to Level on B (not Voice only).
    gg::core::ModRoutes routes = gg::core::factoryRoutes(Mode::Instrument);
    routes[12] = ModRoute{ModSource::Env2,
                          gg::core::kSameLayer,
                          ParamSlot::Pitch,
                          0,
                          7.0f,
                          gg::core::ModCurve::Unipolar};
    routes[13] =
        ModRoute{ModSource::Velocity,        gg::core::kSameLayer, ParamSlot::Level, 1, 9.0f,
                 gg::core::ModCurve::Bipolar};
    p->setRoutes(routes);
    return p;
}
} // namespace

TEST_CASE("processor_voice_block_size_invariant", "[core][voice][determinism]")
{
    constexpr int64_t kFrames = 36000;
    const auto run = [](const std::vector<uint32_t> &blocks) {
        auto p = richProcessor();
        auto log = attachLog(*p);
        Stereo out;
        render(*p, richScript(), 0, kFrames, blocks, out, nullptr, log.get());
        return std::make_pair(std::move(log), std::move(out));
    };
    const auto [log, out] = run({4096});
    REQUIRE(grainsOf(*log, 0, 17).size() > 0); // the steal happened
    REQUIRE(grainsOf(*log, 1, 0).size() > 0);
    REQUIRE(grainsOf(*log, 2, kNoVoice).size() > 0);
    REQUIRE(gg::test::layers::allFinite(out.left));
    for (const std::vector<uint32_t> &blocks :
         {std::vector<uint32_t>{1}, std::vector<uint32_t>{32}, std::vector<uint32_t>{64},
          std::vector<uint32_t>{100, 7, 333}})
    {
        INFO("first block " << blocks.front());
        const auto [otherLog, other] = run(blocks);
        CHECK(gg::test::layers::sameBits(other.left, out.left));
        CHECK(gg::test::layers::sameBits(other.right, out.right));
        for (uint32_t L = 0; L < 3; ++L)
        {
            CHECK(otherLog->layer(L) == log->layer(L));
        }
    }
}

TEST_CASE("processor_voice_threads_same_bytes", "[core][voice][threads]")
{
    constexpr int64_t kFrames = 30000;
    const auto run = [](gg::TaskRunner *runner) {
        auto p = richProcessor();
        p->setTaskRunner(runner);
        Stereo out;
        render(*p, richScript(), 0, kFrames, {512, 77}, out);
        return out;
    };
    const Stereo serial = run(nullptr);
    for (const unsigned workers : {1u, 2u, 8u})
    {
        INFO(workers << " workers");
        gg::test::StubPool pool(workers);
        const Stereo threaded = run(&pool);
        CHECK(gg::test::layers::sameBits(threaded.left, serial.left));
        CHECK(gg::test::layers::sameBits(threaded.right, serial.right));
    }
}

TEST_CASE("processor_voice_no_allocation", "[core][voice][rt]")
{
    auto p = richProcessor();
    p->setBase(layerSlot(LayerParam::Mode, 2), 1.0f);
    Stereo out;
    out.left.assign(64000, 0.0f);
    out.right.assign(64000, 0.0f);
    // Built before the tripwire: the script's vectors and the render's own.
    Script s = richScript();
    for (int16_t k = 0; k < 20; ++k)
    {
        s.on(28000 + 50 * k, static_cast<int16_t>(30 + k), static_cast<int16_t>(k % 16), 1.0f,
             500 + k);
    }
    s.param(30000, slotA(LayerParam::Mode), 0.0);
    s.param(31000, slotA(LayerParam::Mode), 1.0);
    s.sort();
    std::vector<NoteEvent> notes;
    std::vector<ControlEvent> controls;
    std::vector<ParamEvent> params;
    for (const auto &[t, e] : s.notes)
    {
        notes.push_back(e);
        notes.back().offset = static_cast<uint32_t>(t / 256);
    }
    for (const auto &[t, e] : s.controls)
    {
        controls.push_back(e);
        controls.back().offset = static_cast<uint32_t>(t / 256);
    }
    for (const auto &[t, e] : s.params)
    {
        params.push_back(e);
        params.back().offset = static_cast<uint32_t>(t / 256);
    }
    std::array<EndedNote, 16> ended{};
    size_t endedTotal = 0;
    gg::test::AllocTripwire::resetCount();
    {
        gg::test::AllocTripwire::Scope armed;
        float *o[2] = {out.left.data(), out.right.data()};
        // Every event of the script squeezed into the first 256 frames' offsets, in calls of
        // 256 (so 16 voices sound on three layers, steal, switch and end).
        p->process(nullptr, 0, o, 2, 256, params, notes, controls);
        for (int i = 0; i < 200; ++i)
        {
            p->process(nullptr, 0, o, 2, 256, {});
            endedTotal += p->takeEndedNotes(ended);
        }
        p->reset();
        endedTotal += p->takeEndedNotes(ended);
        p->process(nullptr, 0, o, 2, 256, {}, notes, controls);
    }
    const uint64_t allocations = gg::test::AllocTripwire::count();
    if (gg::test::AllocTripwire::available())
    {
        REQUIRE(allocations == 0);
    }
    CHECK(endedTotal > 0);
}

TEST_CASE("processor_note_end_on_eviction", "[core][voice][notes]")
{
    // A note a full note table replaces ends there (Task 7's fix round): with the pedal down,
    // 128 notes on and off stay pedal-held; the 129th note-on replaces the oldest of them, which
    // is the one note ended, with its ID, channel and key. Voice and Cloud layers alike.
    for (const bool voice : {true, false})
    {
        CAPTURE(voice);
        auto p = voiceProcessor(Mode::Instrument, voice);
        Script s;
        s.control(0, ControlKind::Sustain, 2, 1.0f);
        constexpr int kNotes = 129;
        for (int i = 0; i < kNotes; ++i)
        {
            const auto key = static_cast<int16_t>(i % 128);
            s.on(64 * (i + 1), key, 2, 1.0f, 100 + i);
            if (i < kNotes - 1)
            {
                s.off(64 * (i + 1) + 32, key, 2, 100 + i);
            }
        }
        Stereo out;
        std::array<EndedNote, 8> ended{};
        render(*p, s, 0, 64 * kNotes, {256}, out);
        CHECK(p->takeEndedNotes(ended) == 0); // all 128 pedal-held
        render(*p, s, 64 * kNotes, 64 * kNotes + 512, {256}, out);
        REQUIRE(p->takeEndedNotes(ended) == 1);
        CHECK(ended[0].noteId == 100);
        CHECK(ended[0].channel == 2);
        CHECK(ended[0].key == 0);
        // Pedal up: the 127 pedal-held notes left end (Cloud at once, Voice after Env 1), the
        // held 129th doesn't; the evicted note never ends again.
        Script up;
        up.control(64 * kNotes + 512, ControlKind::Sustain, 2, 0.0f);
        render(*p, up, 64 * kNotes + 512, 64 * kNotes + 512 + 48000, {256}, out);
        std::array<EndedNote, 256> rest{};
        const size_t n = p->takeEndedNotes(rest);
        CHECK(n == 127);
        CHECK(std::none_of(rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(n),
                           [](const EndedNote &e) { return e.noteId == 100 || e.noteId == 228; }));
    }
}

TEST_CASE("processor_voice_ended_notes", "[core][voice]")
{
    SECTION("Voice: a note ends when its voice finishes its release")
    {
        auto p = voiceProcessor();
        Script s;
        s.on(0, 64, 3, 1.0f, 42);
        s.off(4800, 64, 3, 42);
        Stereo out;
        render(*p, s, 0, 4864, {256}, out);
        std::array<EndedNote, 4> ended{};
        CHECK(p->takeEndedNotes(ended) == 0);
        const int64_t idle = idleBoundary(gg::core::envParams(effectiveOf(*p), 0, 0), 0, 4800);
        render(*p, {}, 4864, idle, {256}, out);
        CHECK(p->takeEndedNotes(ended) == 0);
        render(*p, {}, idle, idle + 32, {256}, out);
        REQUIRE(p->takeEndedNotes(ended) == 1);
        CHECK(ended[0].noteId == 42);
        CHECK(ended[0].channel == 3);
        CHECK(ended[0].key == 64);
    }
    SECTION("Cloud: a note ends at its release")
    {
        auto p = voiceProcessor(Mode::Instrument, false);
        Script s;
        s.on(0, 64, 3, 1.0f, 42);
        s.off(4800, 64, 3, 42);
        Stereo out;
        render(*p, s, 0, 4800, {256}, out);
        std::array<EndedNote, 4> ended{};
        CHECK(p->takeEndedNotes(ended) == 0);
        render(*p, s, 4800, 4801, {256}, out);
        REQUIRE(p->takeEndedNotes(ended) == 1);
        CHECK(ended[0].noteId == 42);
    }
}

TEST_CASE("processor_voice_end_without_events", "[core][voice][determinism]")
{
    // B1: a released note with no later event; the voice stops spawning at the cell its Env 1
    // goes idle, however the host splits blocks.
    const auto run = [](uint32_t block) {
        auto p = voiceProcessor();
        p->setBase(slotA(LayerParam::Env1Release), -1.3f);
        auto log = attachLog(*p);
        Script s;
        s.on(0, 60);
        s.off(9600, 60);
        Stereo out;
        render(*p, s, 0, 72000, {block}, out, nullptr, log.get());
        const int64_t idle = idleBoundary(gg::core::envParams(effectiveOf(*p), 0, 0), 0, 9600);
        return std::make_pair(lastOnset(grainsOf(*log, 0, 0)), idle);
    };
    const auto [last32, idle] = run(32);
    const auto [last4096, idle4096] = run(4096);
    CHECK(idle == idle4096);
    CHECK(last32 == last4096);
    CHECK(last32 < idle);
    CHECK(last32 >= idle - 750 - kGrid);
}

TEST_CASE("processor_voice_no_double_count", "[core][voice][mod]")
{
    // B2: a (not Voice only) Velocity to Level route on a Voice layer moves each voice's gain
    // once, and the layer's Level not at all.
    const auto run = [](bool route) {
        auto p = voiceProcessor();
        gg::core::ModRoutes routes = gg::core::factoryRoutes(Mode::Instrument);
        for (size_t i = 43; i < 48; ++i)
        {
            routes[i] = ModRoute{};
        }
        if (route)
        {
            routes[13] = ModRoute{ModSource::Velocity,
                                  gg::core::kSameLayer,
                                  ParamSlot::Level,
                                  gg::core::kAllLayers,
                                  12.0f,
                                  gg::core::ModCurve::Unipolar};
        }
        p->setRoutes(routes);
        auto log = attachLog(*p);
        Script s;
        s.on(0, 60, 0, 0.5f);
        Stereo out;
        render(*p, s, 0, 24000, {256}, out, nullptr, log.get());
        return std::make_pair(grainsOf(*log, 0, 0), p->effective(ParamSlot::Level));
    };
    const auto [with, levelWith] = run(true);
    const auto [without, levelWithout] = run(false);
    CHECK(levelWith == 0.0f);
    CHECK(levelWithout == 0.0f);
    REQUIRE(with.size() == without.size());
    REQUIRE(with.size() > 10);
    const double ratio = std::exp2(6.0 * std::log2(10.0) / 20.0); // +6 dB, once
    for (size_t i = 0; i < with.size(); ++i)
    {
        CHECK(with[i].onsetSample == without[i].onsetSample);
        if (without[i].gain > 0.0f)
        {
            CHECK_THAT(static_cast<double>(with[i].gain) / without[i].gain, WithinRel(ratio, 1e-5));
        }
    }
}

TEST_CASE("processor_voice_sustain_zero_frees", "[core][voice]")
{
    // B8: a held voice whose Env 1 sits at 0 in Sustain stops spawning and frees its slot.
    auto p = voiceProcessor();
    p->setBase(slotA(LayerParam::Env1Sustain), 0.0f);
    p->setBase(slotA(LayerParam::Env1Decay), -3.321928f); // 0.1 s
    auto log = attachLog(*p);
    Script s;
    s.on(0, 60);
    Stereo out;
    render(*p, s, 0, 24000, {256}, out, nullptr, log.get());
    const int64_t idle = idleBoundary(gg::core::envParams(effectiveOf(*p), 0, 0), 0, 1 << 30);
    REQUIRE(idle > 0);
    REQUIRE(idle < 24000);
    CHECK(lastOnset(grainsOf(*log, 0, 0)) < idle);
    CHECK_FALSE(p->voicesSounding());
    std::array<EndedNote, 4> ended{};
    CHECK(p->takeEndedNotes(ended) == 0); // still held
    // Sixteen more notes take sixteen voices with no steal: the freed slot is free.
    Script more;
    for (int16_t k = 0; k < 16; ++k)
    {
        more.on(24000 + 64 * k, static_cast<int16_t>(61 + k));
    }
    more.off(26000, 60);
    render(*p, more, 24000, 30000, {256}, out, nullptr, log.get());
    for (uint32_t ordinal = 1; ordinal <= 16; ++ordinal)
    {
        CHECK_FALSE(grainsOf(*log, 0, ordinal).empty());
    }
    REQUIRE(p->takeEndedNotes(ended) == 1); // its release ends it at once: no voice is left
    CHECK(ended[0].key == 60);
}

TEST_CASE("processor_voice_bend_center_exact", "[core][voice]")
{
    // B7: a centered 14-bit bend at MPE Bend Range 48 gives exactly the key's rate.
    const auto rates = [](bool bend) {
        auto p = voiceProcessor();
        p->setBase(ParamSlot::Mpe, 1.0f);
        auto log = attachLog(*p);
        Script s;
        if (bend)
        {
            s.control(0, ControlKind::PitchBend, 1, 8192.0f / 16383.0f);
            s.control(0, ControlKind::PitchBend, 0, 8192.0f / 16383.0f);
        }
        s.on(64, 67, 1);
        Stereo out;
        render(*p, s, 0, 9600, {256}, out, nullptr, log.get());
        std::vector<double> r;
        for (const gg::GrainSpec &g : log->layer(0))
        {
            r.push_back(g.rate);
        }
        return r;
    };
    const std::vector<double> centered = rates(true);
    REQUIRE_FALSE(centered.empty());
    CHECK(centered == rates(false));
    CHECK_THAT(centered.front(), WithinRel(std::exp2(7.0 / 12.0), 1e-12));
}

TEST_CASE("processor_voice_steal_takes_this_groups_release", "[core][voice]")
{
    // V4 at the cap, legato: a note-off and the 17th note-on in one group steal the voice the
    // group released, not the oldest held one.
    auto p = voiceProcessor();
    auto log = attachLog(*p);
    Script s;
    for (int16_t k = 0; k < 16; ++k)
    {
        s.on(640 * k, static_cast<int16_t>(48 + k));
    }
    const int64_t at = 640 * 16;
    s.off(at, 55); // ordinal 7
    s.on(at, 70);
    Stereo out;
    render(*p, s, 0, at + 24000, {256}, out, nullptr, log.get());
    const auto released = grainsOf(*log, 0, 7);
    REQUIRE_FALSE(released.empty());
    CHECK(lastOnset(released) < at);
    for (uint32_t ordinal = 0; ordinal < 16; ++ordinal)
    {
        if (ordinal != 7)
        {
            INFO("ordinal " << ordinal);
            CHECK(countIn(grainsOf(*log, 0, ordinal), at + 1000, at + 24000) > 10);
        }
    }
    const auto newest = grainsOf(*log, 0, 16);
    REQUIRE_FALSE(newest.empty());
    CHECK(newest.front().onsetSample == at);
    CHECK(clickScore(out) < 15.0);
}

TEST_CASE("processor_voice_params_recompute_only_on_change", "[core][voice][mod]")
{
    // Spec §4.4: a voice's CloudParams are recomputed only when what they are made of changes,
    // not every cell while free-running sources and its own envelopes move.
    auto p = voiceProcessor();
    p->setBase(slotA(LayerParam::Env1Attack), -1.0f); // Env 1 moves for 0.5 s
    gg::core::ModRoutes routes = gg::core::factoryRoutes(Mode::Instrument);
    // An LFO on Diffusion: every cell applied, the layer's sources moving, no voice input.
    routes[12] = ModRoute{ModSource::Lfo1,
                          gg::core::kSameLayer,
                          ParamSlot::Diffusion,
                          gg::core::kAllLayers,
                          0.5f,
                          gg::core::ModCurve::Unipolar};
    p->setRoutes(routes);
    REQUIRE(p->timeVarying());
    Script s;
    s.on(0, 60);
    Stereo out;
    render(*p, s, 0, 256, {256}, out);
    const uint64_t started = p->voiceParamRecomputes();
    CHECK(started == 1);
    render(*p, {}, 256, 24000, {256}, out);
    CHECK(p->voiceParamRecomputes() == started);
    // A per-voice input that moves every cell (Env 2, decaying until 1.5 s, to Pitch)
    // recomputes every cell.
    routes[13] = ModRoute{ModSource::Env2,
                          gg::core::kSameLayer,
                          ParamSlot::Pitch,
                          gg::core::kAllLayers,
                          1.0f,
                          gg::core::ModCurve::Unipolar};
    p->setRoutes(routes);
    const uint64_t before = p->voiceParamRecomputes();
    render(*p, {}, 24000, 24000 + 32 * 10, {256}, out);
    CHECK(p->voiceParamRecomputes() >= before + 10);
    // So does a change of the layer's own values, once.
    const uint64_t held = p->voiceParamRecomputes();
    p->setRoutes(gg::core::factoryRoutes(Mode::Instrument));
    render(*p, {}, 24320, 24320 + 3200, {256}, out);
    const uint64_t settled = p->voiceParamRecomputes();
    CHECK(settled > held);
    p->setBase(slotA(LayerParam::Size), -3.0f);
    render(*p, {}, 27520, 27520 + 3200, {256}, out);
    CHECK(p->voiceParamRecomputes() == settled + 1);
}

TEST_CASE("mpe and note expressions render the same voices", "[core][voice]")
{
    // Spec §3a B7: the goldens' gestures are exactly representable on both paths (a bend of
    // 3k semitones is the 14-bit value 8192 + 512k at MPE Bend Range 48), so the MPE render and
    // the note-expression render agree line for line (the grain index "i" dropped) and bit for
    // bit in the audio, with no tolerance needed.
    const auto run = [](const char *name, std::vector<std::string> &lines) {
        const auto path =
            std::filesystem::path{GG_GOLDEN_DIR} / "cases" / (std::string{name} + ".json");
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        const std::string json{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
        std::string error;
        const auto spec = gg::render::parseRenderSpec(json, error);
        INFO(name << ": " << error);
        REQUIRE(spec.has_value());
        auto audio = gg::render::render(*spec, &lines);
        for (std::string &line : lines)
        {
            const auto at = line.find("\"i\":");
            const auto comma = line.find(',', at);
            REQUIRE(at != std::string::npos);
            line.erase(at, comma - at + 1);
        }
        return audio;
    };
    std::vector<std::string> mpeLines;
    std::vector<std::string> exprLines;
    const auto mpe = run("inst-voice-mpe", mpeLines);
    const auto expr = run("inst-voice-expr", exprLines);
    REQUIRE(mpeLines.size() > 100);
    CHECK(mpeLines == exprLines);
    CHECK(mpe.channels == expr.channels);
}

TEST_CASE("a pedal down numbered before a note off at one sample sustains the note",
          "[core][voice][notes]")
{
    // M3 expression spec E8: at one offset the host's order decides: CC 64 down (seq 1), then
    // the note-off (seq 2): the note is pedal-held, and the Cloud held set keeps its key.
    auto p = voiceProcessor();
    Script s;
    s.on(0, 60);
    s.control(5, ControlKind::Sustain, 0, 1.0f);
    s.off(5, 60);
    s.controls.back().second.seq = 1;
    s.notes.back().second.seq = 2;
    Stereo out;
    render(*p, s, 0, 256, {256}, out);
    const auto keys = p->heldKeys();
    REQUIRE(keys.size() == 1);
    CHECK(keys[0] == 60);
}

TEST_CASE("with sequence numbers 0 a tie keeps the kinds order", "[core][voice][notes]")
{
    // The same two events with seq 0: the note-off comes before the control, as before, so the
    // pedal-down finds the note released and it is not held.
    auto p = voiceProcessor();
    Script s;
    s.on(0, 60);
    s.control(5, ControlKind::Sustain, 0, 1.0f);
    s.off(5, 60);
    Stereo out;
    render(*p, s, 0, 256, {256}, out);
    CHECK(p->heldKeys().empty());
}
