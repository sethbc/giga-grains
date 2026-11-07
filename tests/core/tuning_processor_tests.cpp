// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The Processor's tuning (M3 tuning spec §4.1, §4.3, §4.4, §5.3; T5 to T8): the local table and
// the MTS-ESP snapshot resolved at each call's start, the key term of Cloud grains and of voices,
// Note Pitch following the tuning (Key does not), and unmapped note-ons dropped at the intake with
// an immediate end.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "LayersFixture.h"
#include "gg/core/MathDet.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Processor.h"
#include "gg/core/Tuning.h"
#include "support/AllocTripwire.h"

using gg::kNoVoice;
using gg::core::ControlEvent;
using gg::core::ControlKind;
using gg::core::EndedNote;
using gg::core::LayerParam;
using gg::core::layerSlot;
using gg::core::Mode;
using gg::core::ModRoute;
using gg::core::ModSource;
using gg::core::MtsTuning;
using gg::core::NoteEvent;
using gg::core::NoteEventType;
using gg::core::ParamEvent;
using gg::core::ParamSlot;
using gg::core::Processor;
using gg::core::ProcessorConfig;
using gg::core::tuningFromFrequencies;
using gg::core::tuningFromScala;
using gg::core::TuningTable;
using gg::test::layers::Log;
using gg::test::layers::Stereo;

namespace
{
constexpr double kRate = 48000.0;

/// What a render feeds the Processor, by absolute sample (the events' offsets are ignored).
struct Script
{
    std::vector<std::pair<int64_t, NoteEvent>> notes;
    std::vector<std::pair<int64_t, ControlEvent>> controls;
    std::vector<std::pair<int64_t, ParamEvent>> params;

    void on(int64_t t, int16_t key, int16_t channel = 0, int32_t id = -1, float velocity = 1.0f)
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
/// that fall inside it, into out (resized to `to` if shorter). Each call bumps log->call; each
/// call's ended notes are appended to `ended`, if given.
void render(Processor &p, Script script, int64_t from, int64_t to,
            const std::vector<uint32_t> &blockSizes, Stereo &out, const Stereo *input = nullptr,
            Log *log = nullptr, std::vector<EndedNote> *ended = nullptr)
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
        if (ended != nullptr)
        {
            std::array<EndedNote, 16> e{};
            for (size_t got = p.takeEndedNotes(e); got > 0; got = p.takeEndedNotes(e))
            {
                ended->insert(ended->end(), e.begin(),
                              e.begin() + static_cast<std::ptrdiff_t>(got));
            }
        }
    }
}

ParamSlot slotOf(LayerParam p, uint8_t layer = 0) { return layerSlot(p, layer); }

/// An instrument (or FX) Processor with layer A over a 220 Hz sine (the instrument), its Spray 0,
/// Distribution 0 (regular onsets), Density 6 (64 Hz) and Pitch `pitch`; at Mode Voice if asked.
std::unique_ptr<Processor> tunedProcessor(Mode mode = Mode::Instrument, bool voice = false,
                                          float pitch = 3.0f)
{
    auto p = std::make_unique<Processor>(ProcessorConfig{kRate, 4096, 11, mode});
    if (mode == Mode::Instrument)
    {
        p->setSourceNow(0, gg::test::layers::sine(220.0));
    }
    p->setBase(slotOf(LayerParam::Spray), 0.0f);
    p->setBase(slotOf(LayerParam::Distribution), 0.0f);
    p->setBase(slotOf(LayerParam::Density), 6.0f);
    p->setBase(slotOf(LayerParam::Pitch), pitch);
    if (voice)
    {
        p->setBase(slotOf(LayerParam::Mode), 1.0f);
    }
    return p;
}

/// Turns an instrument's layer L on over a sine, with A's grain settings, at Mode Voice if asked.
void addLayer(Processor &p, uint8_t L, bool voice, double hz = 330.0)
{
    p.setSourceNow(L, gg::test::layers::sine(hz));
    p.setBase(gg::core::layerOnSlot(L), 1.0f);
    p.setBase(slotOf(LayerParam::Spray, L), 0.0f);
    p.setBase(slotOf(LayerParam::Distribution, L), 0.0f);
    p.setBase(slotOf(LayerParam::Density, L), 6.0f);
    p.setBase(slotOf(LayerParam::Mode, L), voice ? 1.0f : 0.0f);
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

bool sameDouble(double a, double b) { return std::memcmp(&a, &b, sizeof(double)) == 0; }

bool sameBytes(const TuningTable &a, const TuningTable &b)
{
    return std::memcmp(&a, &b, sizeof(TuningTable)) == 0;
}

/// A live grain's rate for a total pitch, as the scheduler computes it.
double liveRateOf(double semitones)
{
    return gg::exp2Det(std::clamp(semitones, -48.0, 48.0) / 12.0);
}

/// A sample grain's: the scheduler's rate times the source's rate over the output's (both
/// 48 kHz), as Engine::sampleGrain computes it.
double rateOf(double semitones) { return liveRateOf(semitones) * kRate / kRate; }

/// A `.scl` text: a comment, the description, the count and one tone a line (Task 2's form).
std::string makeScl(const std::string &description, const std::vector<std::string> &tones)
{
    std::string s =
        "! test.scl\n!\n" + description + "\n " + std::to_string(tones.size()) + "\n!\n";
    for (const auto &t : tones)
    {
        s += " " + t + "\n";
    }
    return s;
}

/// 19-EDO in cents, closed by 2/1 (Task 2's text).
TuningTable nineteenEdo()
{
    std::vector<std::string> tones;
    for (int j = 1; j < 19; ++j)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.12f", 1200.0 * j / 19.0);
        tones.emplace_back(buf);
    }
    tones.emplace_back("2/1");
    const auto t = tuningFromScala(makeScl("19-EDO", tones), "");
    REQUIRE(t.has_value());
    return *t;
}

/// The pentatonic on the white keys C D E G A, A4 = 440 on key 69 (Task 2's texts): 61 unmapped.
TuningTable pentatonic()
{
    const std::string kbm = "! test.kbm\n12\n0\n127\n60\n69\n440.0\n5\n0\nx\n1\nx\n2\nx\nx\n3\nx\n"
                            "4\nx\nx\n";
    const auto t = tuningFromScala(makeScl("pentatonic", {"9/8", "5/4", "3/2", "5/3", "2/1"}), kbm);
    REQUIRE(t.has_value());
    REQUIRE(t->isUnmapped(61));
    REQUIRE_FALSE(t->isUnmapped(60));
    REQUIRE_FALSE(t->isUnmapped(67));
    REQUIRE_FALSE(t->isUnmapped(79));
    return *t;
}

double s(const TuningTable &t, int k) { return t.keySemitones[static_cast<size_t>(k)]; }

/// +0.5 of the 14-bit bend range: the control value whose 14-bit value is 12288.
constexpr float kHalfBend = 12288.0f / 16383.0f;
} // namespace

TEST_CASE("tuned cloud grains follow the table", "[core][tuning]")
{
    const TuningTable t = nineteenEdo();
    auto p = tunedProcessor();
    p->setTuning(t);
    auto log = attachLog(*p);
    const std::array<int, 3> keys{60, 67, 79};
    Script sc;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        sc.on(static_cast<int64_t>(i) * 12000, static_cast<int16_t>(keys[i]));
        sc.off(static_cast<int64_t>(i) * 12000 + 9600, static_cast<int16_t>(keys[i]));
    }
    Stereo out;
    render(*p, sc, 0, 36000, {256}, out, nullptr, log.get());
    const double pitch = static_cast<double>(p->effective(slotOf(LayerParam::Pitch)));
    const auto grains = grainsOf(*log, 0, kNoVoice);
    std::array<size_t, 3> counts{};
    for (const gg::GrainSpec &g : grains)
    {
        const auto i = static_cast<size_t>(g.onsetSample / 12000);
        REQUIRE(i < keys.size());
        REQUIRE(g.onsetSample - static_cast<int64_t>(i) * 12000 < 9600);
        CAPTURE(keys[i], g.onsetSample);
        CHECK(sameDouble(g.rate, rateOf(pitch + s(t, keys[i]))));
        ++counts[i];
    }
    for (const size_t c : counts)
    {
        CHECK(c > 5);
    }
    CHECK(s(t, 79) == 12.0); // 19 steps of 19-EDO: the octave
}

TEST_CASE("tuned voices follow the table", "[core][tuning][voice]")
{
    const TuningTable t = nineteenEdo();
    auto p = tunedProcessor(Mode::Instrument, true);
    p->setTuning(t);
    auto log = attachLog(*p);
    const std::array<int, 3> keys{60, 67, 79};
    Script sc;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        sc.on(static_cast<int64_t>(i) * 12000, static_cast<int16_t>(keys[i]));
        sc.off(static_cast<int64_t>(i) * 12000 + 9600, static_cast<int16_t>(keys[i]));
    }
    Stereo out;
    render(*p, sc, 0, 48000, {256}, out, nullptr, log.get());
    const double pitch = static_cast<double>(p->effective(slotOf(LayerParam::Pitch)));
    for (uint32_t i = 0; i < keys.size(); ++i)
    {
        CAPTURE(keys[i]);
        const auto grains = grainsOf(*log, 0, i);
        CHECK(grains.size() > 5);
        for (const gg::GrainSpec &g : grains)
        {
            CHECK(sameDouble(g.rate, rateOf(pitch + s(t, keys[i]))));
        }
    }
    CHECK(grainsOf(*log, 0, kNoVoice).empty());
}

TEST_CASE("bend and the tuning expression add after the tuning", "[core][tuning][voice]")
{
    const TuningTable t = nineteenEdo();
    auto p = tunedProcessor(Mode::Instrument, true, 0.0f);
    p->setTuning(t);
    p->setBase(ParamSlot::Mpe, 1.0f);
    p->setBase(ParamSlot::MpeBendRange, 4.0f); // half the range up: +2 semitones
    auto log = attachLog(*p);
    Script sc;
    sc.on(0, 67, 2, 5);
    sc.control(3200, ControlKind::PitchBend, 2, kHalfBend);
    sc.control(3200, ControlKind::NoteTuning, -1, 0.5f, -1, 5);
    Stereo out;
    render(*p, sc, 0, 9600, {256}, out, nullptr, log.get());
    const auto grains = grainsOf(*log, 0, 0);
    size_t before = 0, after = 0;
    for (const gg::GrainSpec &g : grains)
    {
        CAPTURE(g.onsetSample);
        if (g.onsetSample < 3200)
        {
            CHECK(sameDouble(g.rate, rateOf(s(t, 67))));
            ++before;
        }
        else if (g.onsetSample >= 3200 + 32)
        {
            CHECK(sameDouble(g.rate, gg::exp2Det(((s(t, 67) + 2.0) + 0.5) / 12.0) * kRate / kRate));
            ++after;
        }
    }
    CHECK(before > 2);
    CHECK(after > 5);
}

TEST_CASE("note pitch follows the tuning and key does not", "[core][tuning][mod]")
{
    const TuningTable t = nineteenEdo();
    auto p = tunedProcessor();
    addLayer(*p, 1, false);
    addLayer(*p, 2, true, 440.0);
    for (uint8_t L = 0; L < 3; ++L)
    {
        p->setBase(slotOf(LayerParam::Texture, L), 0.0f);
    }
    p->setTuning(t);
    gg::core::ModRoutes routes = gg::core::factoryRoutes(Mode::Instrument);
    const auto texture = [](ModSource src, uint8_t layer) {
        return ModRoute{src,  gg::core::kSameLayer,        ParamSlot::Texture, layer,
                        1.0f, gg::core::ModCurve::Unipolar};
    };
    routes[12] = texture(ModSource::NotePitch, 0); // the instance-wide Note Pitch, a Cloud layer
    routes[13] = texture(ModSource::Key, 1);       // Key, a Cloud layer
    routes[14] = texture(ModSource::NotePitch, 2); // the voice's own Note Pitch
    p->setRoutes(routes);
    auto log = attachLog(*p);
    Script sc;
    sc.on(0, 67);
    Stereo out;
    render(*p, sc, 0, 9600, {256}, out, nullptr, log.get());
    const auto notePitch = static_cast<float>((60.0 + s(t, 67)) / 127.0);
    const float key = 67.0f / 127.0f;
    REQUIRE(notePitch != key); // the tuning moved it
    CHECK(p->sourceValue(ModSource::NotePitch, 0) == notePitch);
    CHECK(p->effective(slotOf(LayerParam::Texture, 0)) == notePitch);
    CHECK(p->effective(slotOf(LayerParam::Texture, 1)) == key);
    const auto voiceGrains = grainsOf(*log, 2, 0);
    REQUIRE(voiceGrains.size() > 5);
    for (const gg::GrainSpec &g : voiceGrains)
    {
        CHECK(g.texture == static_cast<double>(notePitch));
    }
    for (const gg::GrainSpec &g : grainsOf(*log, 1, kNoVoice))
    {
        CHECK(g.texture == static_cast<double>(key));
    }
}

TEST_CASE("an unmapped note-on starts nothing and ends at once", "[core][tuning][notes]")
{
    const TuningTable t = pentatonic();
    SECTION("the instrument")
    {
        // A dropped note against none at all: the same bytes out, the same later voices and
        // ends, Velocity and Key untouched.
        const auto run = [&t](bool withDropped, std::vector<EndedNote> &ended, Log &log) {
            auto p = tunedProcessor();
            addLayer(*p, 1, true);
            p->setTuning(t);
            p->setEventSink(&Log::record, &log);
            Script sc;
            if (withDropped)
            {
                sc.on(64, 61, 3, 9, 0.25f);
                sc.off(4800, 61, 3, 9);
            }
            sc.on(9600, 60, 0, 1);
            sc.off(14400, 60, 0, 1);
            Stereo out;
            render(*p, sc, 0, 128, {128}, out, nullptr, &log, &ended);
            if (withDropped)
            {
                REQUIRE(ended.size() == 1);
                CHECK(ended[0].noteId == 9);
                CHECK(ended[0].channel == 3);
                CHECK(ended[0].key == 61);
                CHECK(p->heldKeys().empty());
                CHECK(p->sourceValue(ModSource::Key, 0) == 0.0f);
                CHECK(p->sourceValue(ModSource::Velocity, 0) == 0.0f);
            }
            render(*p, sc, 128, 48000, {256}, out, nullptr, &log, &ended);
            return out;
        };
        std::vector<EndedNote> endedA, endedB;
        Log logA, logB;
        const Stereo a = run(true, endedA, logA);
        const Stereo b = run(false, endedB, logB);
        for (const auto &e : logA.events)
        {
            CHECK(e.grain.onsetSample >= 9600); // nothing before the mapped note
        }
        CHECK(gg::test::layers::sameBits(a.left, b.left));
        CHECK(gg::test::layers::sameBits(a.right, b.right));
        CHECK(logA.layer(0) == logB.layer(0));
        CHECK(logA.layer(1) == logB.layer(1));
        REQUIRE(endedA.size() == endedB.size() + 1);
        for (size_t i = 0; i < endedB.size(); ++i)
        {
            CHECK(endedA[i + 1].noteId == endedB[i].noteId);
            CHECK(endedA[i + 1].key == endedB[i].key);
        }
        CHECK_FALSE(endedB.empty());
    }
    SECTION("the FX: the cloud keeps its unkeyed Pitch and nothing freezes (A4)")
    {
        const Stereo input = gg::test::layers::noise(24000, 0.5f, 3);
        const auto run = [&](bool withDropped, std::vector<EndedNote> &ended, Log &log,
                             int64_t &head) {
            auto p = tunedProcessor(Mode::Fx);
            p->setTuning(t);
            p->setEventSink(&Log::record, &log);
            Script sc;
            if (withDropped)
            {
                sc.on(0, 61, 0, 9);
                sc.off(12000, 61, 0, 9);
            }
            Stereo out;
            render(*p, sc, 0, 24000, {256}, out, &input, &log, &ended);
            head = p->engine(0).liveWriteHead();
            return out;
        };
        std::vector<EndedNote> endedA, endedB;
        Log logA, logB;
        int64_t headA = 0, headB = 0;
        const Stereo a = run(true, endedA, logA, headA);
        const Stereo b = run(false, endedB, logB, headB);
        REQUIRE(endedA.size() == 1);
        CHECK(endedA[0].noteId == 9);
        CHECK(endedA[0].key == 61);
        CHECK(endedB.empty());
        CHECK(headA == 24000); // never frozen
        const auto grains = logA.layer(0);
        REQUIRE(grains.size() > 10);
        for (const gg::GrainSpec &g : grains)
        {
            CHECK(sameDouble(g.rate, liveRateOf(3.0)));
        }
        CHECK(gg::test::layers::sameBits(a.left, b.left));
        CHECK(gg::test::layers::sameBits(a.right, b.right));
        CHECK(headA == headB);
    }
}

TEST_CASE("an unmapped note's id reaches no other note", "[core][tuning][voice]")
{
    const TuningTable t = pentatonic();
    auto p = tunedProcessor(Mode::Instrument, true, 0.0f);
    p->setBase(slotOf(LayerParam::Texture), 0.0f);
    gg::core::ModRoutes routes = gg::core::factoryRoutes(Mode::Instrument);
    routes[12] =
        ModRoute{ModSource::Pressure,         gg::core::kSameLayer, ParamSlot::Texture, 0, 1.0f,
                 gg::core::ModCurve::Unipolar};
    p->setRoutes(routes);
    auto log = attachLog(*p);
    std::vector<EndedNote> ended;
    Script sc;
    sc.on(0, 61, 0, 7);
    Stereo out;
    render(*p, sc, 0, 4800, {256}, out, nullptr, log.get(), &ended);
    p->setTuning(t); // 61 unmapped from here
    sc.on(4800, 61, 1, 9);
    sc.control(6400, ControlKind::NoteTuning, -1, 5.0f, -1, 9);
    sc.control(6400, ControlKind::PolyPressure, 1, 1.0f, 61);
    sc.off(8000, 61, 1, 9);
    render(*p, sc, 4800, 24000, {256}, out, nullptr, log.get(), &ended);
    REQUIRE(ended.size() == 1);
    CHECK(ended[0].noteId == 9);
    CHECK(ended[0].channel == 1);
    CHECK(ended[0].key == 61);
    REQUIRE(p->heldKeys().size() == 1);
    CHECK(p->heldKeys()[0] == 61);
    const auto grains = grainsOf(*log, 0, 0);
    size_t late = 0;
    for (const gg::GrainSpec &g : grains)
    {
        CAPTURE(g.onsetSample);
        const double term = g.onsetSample < 4800 ? 1.0 : s(t, 61);
        CHECK(sameDouble(g.rate, rateOf(term)));
        CHECK(g.texture == 0.0);
        late += g.onsetSample >= 8000 ? 1 : 0;
    }
    CHECK(late > 10); // note 7's voice keeps spawning
    CHECK(grainsOf(*log, 0, 1).empty());
}

TEST_CASE("a held note a change unmaps keeps sounding", "[core][tuning]")
{
    const TuningTable t = pentatonic();
    auto p = tunedProcessor();
    addLayer(*p, 1, true);
    auto log = attachLog(*p);
    Script sc;
    sc.on(0, 61);
    Stereo out;
    render(*p, sc, 0, 4800, {256}, out, nullptr, log.get());
    p->setTuning(t);
    render(*p, sc, 4800, 19200, {256}, out, nullptr, log.get());
    REQUIRE(t.isUnmapped(61));
    const double interpolated = s(t, 61);
    CHECK(interpolated != 1.0);
    size_t cloudLate = 0, voiceLate = 0;
    for (const gg::GrainSpec &g : grainsOf(*log, 0, kNoVoice))
    {
        CHECK(sameDouble(g.rate, rateOf(3.0 + (g.onsetSample < 4800 ? 1.0 : interpolated))));
        cloudLate += g.onsetSample >= 4800 ? 1 : 0;
    }
    for (const gg::GrainSpec &g : grainsOf(*log, 1, 0))
    {
        CHECK(sameDouble(g.rate, rateOf(g.onsetSample < 4800 ? 1.0 : interpolated)));
        voiceLate += g.onsetSample >= 4800 ? 1 : 0;
    }
    CHECK(cloudLate > 10);
    CHECK(voiceLate > 10);
}

TEST_CASE("a mid-note change retunes the next grains", "[core][tuning]")
{
    const TuningTable t = nineteenEdo();
    auto p = tunedProcessor();
    addLayer(*p, 1, true);
    auto log = attachLog(*p);
    const std::array<int, 3> chord{60, 64, 67};
    Script sc;
    for (const int k : chord)
    {
        sc.on(0, static_cast<int16_t>(k));
    }
    constexpr int64_t kChange = 4800 + 100; // a call's first sample, off the grid
    Stereo out;
    render(*p, sc, 0, kChange, {256, 100}, out, nullptr, log.get());
    p->setTuning(t);
    render(*p, sc, kChange, 19200, {256}, out, nullptr, log.get());
    const TuningTable twelve = TuningTable::twelveTet();
    size_t oldCount = 0, newCount = 0;
    for (const gg::GrainSpec &g : grainsOf(*log, 0, kNoVoice))
    {
        CAPTURE(g.onsetSample, g.rate);
        const TuningTable &table = g.onsetSample < kChange ? twelve : t;
        bool found = false;
        for (const int k : chord)
        {
            found = found || sameDouble(g.rate, rateOf(3.0 + s(table, k)));
        }
        CHECK(found);
        (g.onsetSample < kChange ? oldCount : newCount) += 1;
    }
    for (uint32_t i = 0; i < chord.size(); ++i)
    {
        for (const gg::GrainSpec &g : grainsOf(*log, 1, i))
        {
            CAPTURE(i, g.onsetSample);
            const TuningTable &table = g.onsetSample < kChange ? twelve : t;
            CHECK(sameDouble(g.rate, rateOf(s(table, chord[i]))));
            (g.onsetSample < kChange ? oldCount : newCount) += 1;
        }
    }
    CHECK(oldCount > 10);
    CHECK(newCount > 30);
}

TEST_CASE("tuning precedence", "[core][tuning]")
{
    const TuningTable local = nineteenEdo();
    const TuningTable master = pentatonic();
    auto p = tunedProcessor();
    std::array<float, 32> buf{};
    float *o[2] = {buf.data(), buf.data()};
    const auto call = [&](std::span<const ParamEvent> events = {}) {
        p->process(nullptr, 0, o, 2, 32, events);
    };
    call();
    CHECK(sameBytes(p->effectiveTuning(), TuningTable::twelveTet())); // no local: 12-TET
    p->setMtsTuning(MtsTuning{master, false});
    call();
    CHECK(sameBytes(p->effectiveTuning(), TuningTable::twelveTet())); // disconnected, no local
    p->setTuning(local);
    CHECK(sameBytes(p->effectiveTuning(), TuningTable::twelveTet())); // not before the next call
    call();
    CHECK(sameBytes(p->effectiveTuning(), local)); // disconnected: the local table
    p->setMtsTuning(MtsTuning{master, true});
    call();
    CHECK(sameBytes(p->effectiveTuning(), master)); // connected, the switch On: the master's
    p->setBase(ParamSlot::MtsEsp, 0.0f);
    call();
    CHECK(sameBytes(p->effectiveTuning(), local)); // connected, the switch Off: the local table
    p->setBase(ParamSlot::MtsEsp, 1.0f);
    call();
    CHECK(sameBytes(p->effectiveTuning(), master));
    // The switch flipped by an event lands at the next call.
    const std::array<ParamEvent, 1> off{ParamEvent{0, ParamSlot::MtsEsp, false, 0.0}};
    call(off);
    CHECK(p->base(ParamSlot::MtsEsp) == 0.0f);
    CHECK(sameBytes(p->effectiveTuning(), master));
    call();
    CHECK(sameBytes(p->effectiveTuning(), local));
    p->setMtsTuning(MtsTuning{master, false});
    p->setBase(ParamSlot::MtsEsp, 1.0f);
    call();
    CHECK(sameBytes(p->effectiveTuning(), local));
}

TEST_CASE("tuning from frequencies keeps twelve tet for bad frequencies", "[core][tuning]")
{
    std::array<double, 128> hz{};
    std::array<bool, 128> filtered{};
    for (int k = 0; k < 128; ++k)
    {
        hz[static_cast<size_t>(k)] = 440.0 * std::pow(2.0, (k - 69) / 12.0);
    }
    hz[10] = std::nan("");
    hz[20] = 0.0;
    hz[30] = -5.0;
    hz[40] = 1e300;
    hz[41] = 1e-300;
    hz[42] = std::numeric_limits<double>::infinity();
    hz[43] = -std::numeric_limits<double>::infinity();
    hz[44] = std::numeric_limits<double>::denorm_min();
    hz[69] = 432.0;
    filtered[50] = true;
    const TuningTable t = tuningFromFrequencies(hz, filtered);
    CHECK(s(t, 10) == -50.0);
    CHECK(s(t, 20) == -40.0);
    CHECK(s(t, 30) == -30.0);
    CHECK(s(t, 40) == 1024.0);
    CHECK(s(t, 41) == -1024.0);
    CHECK(s(t, 42) == -18.0);
    CHECK(s(t, 43) == -17.0);
    CHECK(s(t, 44) == -1024.0);
    CHECK(std::abs(s(t, 69) - 12.0 * std::log2(432.0 / TuningTable::kC4Hz)) < 1e-12);
    for (int k = 0; k < 128; ++k)
    {
        CAPTURE(k);
        CHECK(std::isfinite(s(t, k)));
        CHECK(std::abs(s(t, k)) <= 1024.0);
        CHECK(t.isUnmapped(k) == (k == 50));
        if (k != 40 && k != 41 && k != 44 && k != 69)
        {
            CHECK(s(t, k) == static_cast<double>(k) - 60.0);
        }
    }
}

TEST_CASE("tuning from frequencies of twelve tet is exactly twelve tet", "[core][tuning]")
{
    std::array<double, 128> hz{};
    const std::array<bool, 128> filtered{};
    for (int k = 0; k < 128; ++k)
    {
        hz[static_cast<size_t>(k)] = 440.0 * std::pow(2.0, (k - 69) / 12.0);
    }
    const TuningTable t = tuningFromFrequencies(hz, filtered);
    CHECK(sameBytes(t, TuningTable::twelveTet()));
}

TEST_CASE("a tuned render is block-size invariant", "[core][tuning][determinism]")
{
    constexpr int64_t kFrames = 30000;
    const TuningTable t = pentatonic();
    const auto run = [&t](const std::vector<uint32_t> &blocks) {
        auto p = tunedProcessor();
        addLayer(*p, 1, true);
        p->setBase(ParamSlot::Mpe, 1.0f);
        p->setTuning(t);
        auto log = std::make_unique<Log>();
        p->setEventSink(&Log::record, log.get());
        Script sc;
        const std::array<int16_t, 8> keys{60, 61, 62, 64, 66, 67, 69, 72};
        for (size_t i = 0; i < keys.size(); ++i)
        {
            const auto at = static_cast<int64_t>(500 + 1711 * i);
            sc.on(at, keys[i], static_cast<int16_t>(1 + i % 3), static_cast<int32_t>(100 + i));
            sc.off(at + 9000, keys[i], static_cast<int16_t>(1 + i % 3),
                   static_cast<int32_t>(100 + i));
        }
        sc.control(3000, ControlKind::PitchBend, 1, 0.7f);
        sc.control(5000, ControlKind::NoteTuning, -1, -1.25f, -1, 102);
        std::vector<EndedNote> ended;
        Stereo out;
        render(*p, sc, 0, kFrames, blocks, out, nullptr, log.get(), &ended);
        return std::make_tuple(std::move(log), std::move(out), ended.size());
    };
    const auto [log, out, ended] = run({4096});
    REQUIRE_FALSE(grainsOf(*log, 0, kNoVoice).empty());
    REQUIRE_FALSE(grainsOf(*log, 1, 0).empty());
    REQUIRE(ended >= 2); // 61 and 66 are unmapped: they end at once
    REQUIRE(gg::test::layers::allFinite(out.left));
    for (const std::vector<uint32_t> &blocks :
         {std::vector<uint32_t>{1}, std::vector<uint32_t>{32}, std::vector<uint32_t>{64}})
    {
        INFO("block " << blocks.front());
        const auto [otherLog, other, otherEnded] = run(blocks);
        CHECK(gg::test::layers::sameBits(other.left, out.left));
        CHECK(gg::test::layers::sameBits(other.right, out.right));
        CHECK(otherLog->layer(0) == log->layer(0));
        CHECK(otherLog->layer(1) == log->layer(1));
        CHECK(otherEnded == ended);
    }
}

TEST_CASE("set tuning allocates nothing", "[core][tuning][rt]")
{
    const TuningTable a = nineteenEdo();
    const TuningTable b = pentatonic();
    auto p = tunedProcessor();
    addLayer(*p, 1, true);
    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    float *o[2] = {l.data(), r.data()};
    std::vector<NoteEvent> notes;
    for (int16_t k = 0; k < 12; ++k)
    {
        notes.push_back({static_cast<uint32_t>(10 * k), NoteEventType::On, 0,
                         static_cast<int16_t>(60 + k), 1.0f, 200 + k});
    }
    std::array<EndedNote, 16> ended{};
    size_t endedTotal = 0;
    gg::test::AllocTripwire::resetCount();
    {
        gg::test::AllocTripwire::Scope armed;
        p->process(nullptr, 0, o, 2, 256, {}, notes);
        for (int i = 0; i < 100; ++i)
        {
            p->setTuning(i % 2 == 0 ? a : b);
            p->setMtsTuning(MtsTuning{i % 3 == 0 ? b : a, i % 4 == 0});
            p->process(nullptr, 0, o, 2, 256, {});
            endedTotal += p->takeEndedNotes(ended);
        }
        p->setTuning(b);
        p->process(nullptr, 0, o, 2, 256, {}, notes); // the unmapped ones end at once
        endedTotal += p->takeEndedNotes(ended);
    }
    const uint64_t allocations = gg::test::AllocTripwire::count();
    if (gg::test::AllocTripwire::available())
    {
        REQUIRE(allocations == 0);
    }
    CHECK(endedTotal > 0);
}
