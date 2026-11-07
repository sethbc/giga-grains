// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The plugin's input paths into the voices (M3 Voice spec §4.2, §4.5, §4.6): CLAP note IDs and
// release velocity, CLAP note expressions, MIDI-MPE (per-channel bend, pressure, CC 74 and poly
// pressure), the sustain pedal, clap_plugin_as_vst3, CLAP_EVENT_NOTE_END and the tail.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <clap/clap.h>
#include <clapwrapper/vst3.h>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "gg/engine/Engine.h"
#include "GigaGrainsPlugin.h"
#include "PluginHarness.h"

namespace
{
constexpr const char *kInstrumentId = "org.sethbc.giga-grains";
constexpr const char *kFxId = "org.sethbc.giga-grains-fx";
constexpr uint32_t kBlock = 512;

using Catch::Matchers::WithinRel;
using gg::core::LayerParam;
using gg::core::layerSlot;
using gg::core::ModSource;
using gg::core::paramId;
using gg::core::ParamSlot;
using gg::test::OutputEvents;
using gg::test::PluginUnderTest;
using gg::test::Stereo;

// The grains a plugin's Processor spawned, with their layers, in the order it reported them.
struct GrainLog
{
    struct Entry
    {
        uint32_t layer;
        gg::GrainSpec grain;
    };
    std::vector<Entry> entries;
    GrainLog() { entries.reserve(1 << 16); }
    static void record(void *ctx, uint32_t layer, const gg::GrainSpec &g)
    {
        auto *log = static_cast<GrainLog *>(ctx);
        if (log->entries.size() < log->entries.capacity())
        {
            log->entries.push_back({layer, g});
        }
    }
    // The last grain voice `ordinal` of layer A spawned; nullptr if none.
    const gg::GrainSpec *last(uint32_t ordinal) const
    {
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        {
            if (it->layer == 0 && it->grain.voice == ordinal)
            {
                return &it->grain;
            }
        }
        return nullptr;
    }
    bool any(uint32_t ordinal) const { return last(ordinal) != nullptr; }
    void clear() { entries.clear(); }
};

clap_event_note note(uint16_t type, int16_t channel, int16_t key, int32_t noteId,
                     double velocity = 0.8, uint32_t time = 0)
{
    auto e = gg::test::noteEvent(type, channel, key, time);
    e.note_id = noteId;
    e.velocity = velocity;
    return e;
}

clap_event_note_expression expression(clap_note_expression which, int32_t noteId, int16_t channel,
                                      int16_t key, double value, uint32_t time = 0)
{
    clap_event_note_expression e{};
    e.header.size = sizeof(e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_NOTE_EXPRESSION;
    e.expression_id = which;
    e.note_id = noteId;
    e.port_index = 0;
    e.channel = channel;
    e.key = key;
    e.value = value;
    return e;
}

// Sets base values by params.flush (before activation they apply at activate()).
void setValues(const PluginUnderTest &plugin, std::initializer_list<std::pair<ParamSlot, double>> v)
{
    std::vector<clap_event_param_value> values;
    values.reserve(v.size());
    for (const auto &[slot, value] : v)
    {
        values.push_back(gg::test::paramValue(paramId(slot), value));
    }
    gg::test::InputEvents events;
    for (const auto &e : values)
    {
        events.add(&e.header);
    }
    plugin.flush(events.list());
}

ParamSlot slotA(LayerParam p) { return layerSlot(p, 0); }

// The instrument over a 220 Hz sine, layer A at Mode Voice (or Cloud) with regular onsets at
// 64 Hz, `extra` set on top, activated at 48 kHz with the grain log attached.
void setUpInstrument(PluginUnderTest &plugin, GrainLog &log, const char *sample, bool voice = true,
                     std::initializer_list<std::pair<ParamSlot, double>> extra = {})
{
    REQUIRE(plugin.valid());
    REQUIRE(plugin.loadDefaultsWithSeed(0));
    REQUIRE(plugin.loadSampleState(gg::test::sineSampleFile(sample)));
    setValues(plugin, {{slotA(LayerParam::Mode), voice ? 1.0 : 0.0},
                       {slotA(LayerParam::Spray), 0.0},
                       {slotA(LayerParam::Distribution), 0.0},
                       {slotA(LayerParam::Density), 6.0}});
    setValues(plugin, extra);
    REQUIRE(plugin.activate(48000.0, kBlock));
    plugin.impl().processor()->setEventSink(&GrainLog::record, &log);
}

// One block of the instrument with `events`, its output events into `out` (or discarded).
void run(PluginUnderTest &plugin, std::initializer_list<const clap_event_header *> events,
         OutputEvents *out = nullptr, uint32_t frames = kBlock)
{
    gg::test::InputEvents in;
    for (const auto *e : events)
    {
        in.add(e);
    }
    Stereo audio(frames);
    REQUIRE(plugin.process(frames, nullptr, audio.data(), in.list(), 2,
                           out != nullptr ? out->list() : nullptr) == CLAP_PROCESS_CONTINUE);
}

std::vector<int> heldKeys(const PluginUnderTest &plugin)
{
    const auto keys = plugin.impl().processor()->heldKeys();
    return {keys.begin(), keys.end()};
}

float source(const PluginUnderTest &plugin, ModSource s)
{
    return plugin.impl().processor()->sourceValue(s, 0);
}

// The NOTE_END events in `out`, as (note ID, channel, key) triples.
struct End
{
    int32_t noteId;
    int16_t channel;
    int16_t key;
    friend bool operator==(const End &, const End &) = default;
};
std::vector<End> endsIn(const OutputEvents &out)
{
    std::vector<End> ends;
    for (const auto &e : out.events())
    {
        if (e.header.space_id == CLAP_CORE_EVENT_SPACE_ID && e.header.type == CLAP_EVENT_NOTE_END)
        {
            ends.push_back({e.noteId, e.channel, e.key});
        }
    }
    return ends;
}

double pitchRatio(double semitones) { return std::exp2(semitones / 12.0); }
} // namespace

TEST_CASE("plugin_clap_note_id_reaches_voice", "[plugin][voice]")
{
    // Spec §4.2: two CLAP notes on one (channel, key) are two notes, two voices; a note-off by
    // ID releases that note alone (row 184), carrying its release velocity.
    PluginUnderTest plugin(kInstrumentId);
    GrainLog log;
    setUpInstrument(plugin, log, "voice-note-id");
    const auto on7 = note(CLAP_EVENT_NOTE_ON, 0, 60, 7);
    const auto on8 = note(CLAP_EVENT_NOTE_ON, 0, 60, 8);
    run(plugin, {&on7.header, &on8.header});
    run(plugin, {});
    CHECK(heldKeys(plugin) == std::vector<int>{60});
    CHECK(log.any(0)); // note 7's voice
    CHECK(log.any(1)); // note 8's voice

    const auto off8 = note(CLAP_EVENT_NOTE_OFF, -1, -1, 8, 0.25);
    OutputEvents out;
    run(plugin, {&off8.header}, &out);
    CHECK(heldKeys(plugin) == std::vector<int>{60}); // note 7 is still held
    CHECK(source(plugin, ModSource::ReleaseVelocity) == 0.25f);
    CHECK(endsIn(out).empty()); // voice 1 is releasing

    // Env 1's 200 ms release: note 8 ends within 20 blocks, note 7 never.
    std::vector<End> ends;
    for (int k = 0; k < 20 && ends.empty(); ++k)
    {
        out.clear();
        run(plugin, {}, &out);
        ends = endsIn(out);
    }
    CHECK(ends == std::vector<End>{{8, 0, 60}});
    log.clear();
    for (int k = 0; k < 4; ++k)
    {
        out.clear();
        run(plugin, {}, &out);
        CHECK(endsIn(out).empty());
    }
    CHECK(log.any(0));
    CHECK_FALSE(log.any(1)); // voice 1 finished

    // An off by ID with its channel and key given releases that note alone too.
    const auto off7 = note(CLAP_EVENT_NOTE_OFF, 0, 60, 7, 1.0);
    run(plugin, {&off7.header});
    CHECK(heldKeys(plugin).empty());
    CHECK(source(plugin, ModSource::ReleaseVelocity) == 1.0f);
}

TEST_CASE("plugin_note_expression_tuning_pressure_brightness", "[plugin][voice]")
{
    // Spec §4.5: CLAP note expressions, matched by note ID, else by (channel, key): TUNING adds
    // semitones, PRESSURE and BRIGHTNESS are the note's Pressure and Timbre, VOLUME multiplies
    // its voice's gain; PAN, VIBRATO and EXPRESSION are accepted and ignored. An unknown ID
    // changes nothing.
    using gg::plugin::GigaGrainsPlugin;
    for (const auto which : {CLAP_NOTE_EXPRESSION_TUNING, CLAP_NOTE_EXPRESSION_PRESSURE,
                             CLAP_NOTE_EXPRESSION_BRIGHTNESS, CLAP_NOTE_EXPRESSION_VOLUME})
    {
        CAPTURE(which);
        const auto e = expression(which, 1, 0, 60, 0.5);
        CHECK(GigaGrainsPlugin::isControlEvent(&e.header));
        CHECK_FALSE(GigaGrainsPlugin::isNoteEvent(&e.header));
    }
    for (const auto which :
         {CLAP_NOTE_EXPRESSION_PAN, CLAP_NOTE_EXPRESSION_VIBRATO, CLAP_NOTE_EXPRESSION_EXPRESSION})
    {
        CAPTURE(which);
        const auto e = expression(which, 1, 0, 60, 0.5);
        CHECK_FALSE(GigaGrainsPlugin::isControlEvent(&e.header));
    }

    PluginUnderTest plugin(kInstrumentId);
    GrainLog log;
    setUpInstrument(plugin, log, "voice-expression");
    const auto on5 = note(CLAP_EVENT_NOTE_ON, 0, 60, 5);
    const auto on6 = note(CLAP_EVENT_NOTE_ON, 1, 67, 6);
    run(plugin, {&on5.header, &on6.header});
    run(plugin, {});
    REQUIRE(log.any(0));
    REQUIRE(log.any(1));
    const double rate5 = log.last(0)->rate;
    const double rate6 = log.last(1)->rate;
    CHECK_THAT(rate6 / rate5, WithinRel(pitchRatio(7.0), 1e-6));

    // TUNING by ID, then by (channel, key); an unknown ID and a PAN change nothing.
    const auto tuneById = expression(CLAP_NOTE_EXPRESSION_TUNING, 5, -1, -1, 12.0);
    const auto tuneByKey = expression(CLAP_NOTE_EXPRESSION_TUNING, -1, 1, 67, -12.0);
    const auto tuneUnknown = expression(CLAP_NOTE_EXPRESSION_TUNING, 99, -1, -1, 24.0);
    const auto pan = expression(CLAP_NOTE_EXPRESSION_PAN, 5, -1, -1, 0.0);
    run(plugin, {&tuneById.header, &tuneByKey.header, &tuneUnknown.header, &pan.header});
    run(plugin, {});
    CHECK_THAT(log.last(0)->rate, WithinRel(rate5 * 2.0, 1e-6));
    CHECK_THAT(log.last(1)->rate, WithinRel(rate6 * 0.5, 1e-6));

    // PRESSURE and BRIGHTNESS: the newest held note's (6) are the instance's sources (B4).
    const auto pressure = expression(CLAP_NOTE_EXPRESSION_PRESSURE, 6, -1, -1, 0.75);
    const auto brightness = expression(CLAP_NOTE_EXPRESSION_BRIGHTNESS, -1, 1, 67, 0.375);
    const auto pressureUnknown = expression(CLAP_NOTE_EXPRESSION_PRESSURE, 99, -1, -1, 0.125);
    run(plugin, {&pressure.header, &brightness.header, &pressureUnknown.header});
    CHECK(source(plugin, ModSource::Pressure) == 0.75f);
    CHECK(source(plugin, ModSource::Timbre) == 0.375f);

    // VOLUME 2 doubles note 5's voice's grains' gain, and leaves note 6's.
    const float gain5 = log.last(0)->gain;
    const float gain6 = log.last(1)->gain;
    const auto volume = expression(CLAP_NOTE_EXPRESSION_VOLUME, 5, -1, -1, 2.0);
    run(plugin, {&volume.header});
    run(plugin, {});
    CHECK_THAT(log.last(0)->gain, WithinRel(gain5 * 2.0f, 1e-5f));
    CHECK(log.last(1)->gain == gain6);
}

TEST_CASE("plugin_midi_mpe_channel_bend_pressure_cc74", "[plugin][voice]")
{
    // Spec §4.5 with MPE On: a member channel's bend × MPE Bend Range (48) bends its voices,
    // kept from before its note-on; the master channel's (1) × Bend Range (2) bends every voice;
    // the center is exact (B7). Channel pressure, CC 74 and poly pressure are the note's
    // Pressure and Timbre.
    using gg::plugin::GigaGrainsPlugin;
    const auto isControl = [](uint8_t status, uint8_t data1) {
        const auto e = gg::test::midiEvent(status, data1, 64);
        return GigaGrainsPlugin::isControlEvent(&e.header) &&
               !GigaGrainsPlugin::isNoteEvent(&e.header);
    };
    CHECK(isControl(0xB3, 64)); // the pedal
    CHECK(isControl(0xB3, 74)); // timbre
    CHECK(isControl(0xA3, 60)); // poly pressure
    CHECK(isControl(0xD3, 60));
    CHECK(isControl(0xE3, 0));
    CHECK_FALSE(isControl(0xB3, 7));

    PluginUnderTest plugin(kInstrumentId);
    GrainLog log;
    setUpInstrument(plugin, log, "voice-mpe", true, {{ParamSlot::Mpe, 1.0}});
    // 12288 = 8192 + 4096: half the way up, 24 semitones on a member channel.
    const auto bendUp2 = gg::test::midiEvent(0xE2, 0x00, 0x60);
    const auto on2 = gg::test::midiEvent(0x92, 60, 100);
    const auto on3 = gg::test::midiEvent(0x93, 60, 100);
    run(plugin, {&bendUp2.header, &on2.header, &on3.header});
    run(plugin, {});
    REQUIRE(log.any(0));
    REQUIRE(log.any(1));
    const double flat = log.last(1)->rate;
    CHECK_THAT(log.last(0)->rate, WithinRel(flat * pitchRatio(24.0), 1e-6));

    // The center is exactly no bend.
    const auto center2 = gg::test::midiEvent(0xE2, 0x00, 0x40);
    run(plugin, {&center2.header});
    run(plugin, {});
    CHECK(log.last(0)->rate == flat);

    // The master channel bends every voice, by Bend Range: one semitone.
    const auto bendUp0 = gg::test::midiEvent(0xE0, 0x00, 0x60);
    run(plugin, {&bendUp0.header});
    run(plugin, {});
    CHECK_THAT(log.last(0)->rate, WithinRel(flat * pitchRatio(1.0), 1e-6));
    CHECK_THAT(log.last(1)->rate, WithinRel(flat * pitchRatio(1.0), 1e-6));

    // Channel 3's note is the newest: its channel pressure and CC 74, then its poly pressure.
    const auto pressure3 = gg::test::midiEvent(0xD3, 100, 0);
    const auto timbre3 = gg::test::midiEvent(0xB3, 74, 64);
    const auto pressure2 = gg::test::midiEvent(0xD2, 10, 0); // another note's
    run(plugin, {&pressure3.header, &timbre3.header, &pressure2.header});
    CHECK(source(plugin, ModSource::Pressure) == 100.0f / 127.0f);
    CHECK(source(plugin, ModSource::Timbre) == 64.0f / 127.0f);
    const auto poly = gg::test::midiEvent(0xA3, 60, 20);
    run(plugin, {&poly.header});
    CHECK(source(plugin, ModSource::Pressure) == 20.0f / 127.0f);
}

TEST_CASE("plugin_cc64_pedal", "[plugin][voice]")
{
    // Spec §4.2: CC 64 >= 64 holds its channel's released notes (pedal-held counts as held) and
    // < 64 releases them; with MPE On the master channel's pedal holds every channel. Both
    // products, Cloud layers: the NOTE_END comes at the pedal-up's boundary.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const bool mpe = GENERATE(false, true);
    CAPTURE(mpe);
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    setValues(plugin, {{ParamSlot::Mpe, mpe ? 1.0 : 0.0}});
    REQUIRE(plugin.activate(48000.0, kBlock));
    const auto call = [&](std::initializer_list<const clap_event_header *> events,
                          std::vector<End> *ends = nullptr) {
        gg::test::InputEvents in;
        for (const auto *e : events)
        {
            in.add(e);
        }
        OutputEvents out;
        Stereo audio(kBlock);
        REQUIRE(plugin.process(kBlock, audio.data(), audio.data(), in.list(), 2, out.list()) ==
                CLAP_PROCESS_CONTINUE);
        if (ends != nullptr)
        {
            *ends = endsIn(out);
        }
        return heldKeys(plugin);
    };
    const auto on = gg::test::midiEvent(0x94, 60, 100);
    const auto off = gg::test::midiEvent(0x84, 60, 0);
    const auto down = gg::test::midiEvent(0xB4, 64, 127);
    const auto up = gg::test::midiEvent(0xB4, 64, 0);
    const auto masterDown = gg::test::midiEvent(0xB0, 64, 64);
    const auto masterUp = gg::test::midiEvent(0xB0, 64, 63);

    std::vector<End> ends;
    CHECK(call({&on.header, &down.header}) == std::vector<int>{60});
    CHECK(call({&off.header}, &ends) == std::vector<int>{60}); // pedal-held
    CHECK(ends.empty());
    CHECK(call({&up.header}, &ends).empty());
    CHECK(ends == std::vector<End>{{-1, 4, 60}});

    // The master channel's pedal: every channel's with MPE On, its own alone with MPE Off.
    CHECK(call({&on.header, &masterDown.header}) == std::vector<int>{60});
    CHECK(call({&off.header}, &ends) == (mpe ? std::vector<int>{60} : std::vector<int>{}));
    CHECK(call({&masterUp.header}, &ends).empty());
    CHECK(ends == (mpe ? std::vector<End>{{-1, 4, 60}} : std::vector<End>{}));
}

TEST_CASE("plugin_as_vst3_answers", "[plugin][voice]")
{
    // Spec §4.5: clap-wrapper's clap_plugin_as_vst3, so its VST3 offers 16 MIDI channels and the
    // note expressions TUNING, VOLUME, PAN and BRIGHTNESS (not PRESSURE: poly pressure then
    // reaches us as MIDI 0xAn). Both products.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const auto *ext = static_cast<const clap_plugin_as_vst3 *>(
        plugin.clap()->get_extension(plugin.clap(), CLAP_PLUGIN_AS_VST3));
    REQUIRE(ext != nullptr);
    REQUIRE(ext->getNumMIDIChannels != nullptr);
    REQUIRE(ext->supportedNoteExpressions != nullptr);
    CHECK(ext->getNumMIDIChannels(plugin.clap(), 0) == 16);
    CHECK(ext->supportedNoteExpressions(plugin.clap()) ==
          (AS_VST3_NOTE_EXPRESSION_TUNING | AS_VST3_NOTE_EXPRESSION_VOLUME |
           AS_VST3_NOTE_EXPRESSION_PAN | AS_VST3_NOTE_EXPRESSION_BRIGHTNESS));
    REQUIRE(plugin.activate(48000.0, kBlock)); // the same while active
    CHECK(ext->getNumMIDIChannels(plugin.clap(), 0) == 16);
}

TEST_CASE("plugin_note_end_after_release", "[plugin][voice]")
{
    // Spec §4.5: CLAP_EVENT_NOTE_END (its ID, port 0, its channel and key) once a note is released
    // and no Voice layer still has a spawning voice for it: at the end of Env 1's release (200 ms)
    // on a Voice layer, at the release's boundary with none. A refused push is sent again in the
    // next call.
    const bool voice = GENERATE(true, false);
    CAPTURE(voice);
    PluginUnderTest plugin(kInstrumentId);
    GrainLog log;
    setUpInstrument(plugin, log, "voice-note-end", voice);
    const auto on = note(CLAP_EVENT_NOTE_ON, 3, 64, 42);
    const auto midiOn = gg::test::midiEvent(0x95, 67, 100);
    run(plugin, {&on.header, &midiOn.header});
    run(plugin, {});

    const auto off = note(CLAP_EVENT_NOTE_OFF, 3, 64, 42, 0.5);
    const auto midiOff = gg::test::midiEvent(0x85, 67, 30);
    OutputEvents out;
    run(plugin, {&off.header, &midiOff.header}, &out);
    std::vector<End> ends = endsIn(out);
    std::size_t blocks = 0; // after the off's
    while (ends.empty() && blocks < 40)
    {
        out.clear();
        run(plugin, {}, &out);
        ends = endsIn(out);
        ++blocks;
    }
    CHECK(ends == std::vector<End>{{42, 3, 64}, {-1, 5, 67}});
    // 200 ms is 9,600 samples: in the 18th block after the off's, or in the off's own.
    CHECK(blocks == (voice ? 18u : 0u));
    for (const auto &e : out.events())
    {
        if (e.header.type == CLAP_EVENT_NOTE_END)
        {
            CHECK(e.header.size == sizeof(clap_event_note));
            CHECK(e.header.space_id == CLAP_CORE_EVENT_SPACE_ID);
            CHECK(e.header.time < kBlock);
            CHECK(e.portIndex == 0);
        }
    }

    // A host whose queue is full: the end waits for the next call's.
    OutputEvents full(0);
    run(plugin, {&on.header});
    run(plugin, {&off.header}, &full);
    for (int k = 0; k < 25; ++k)
    {
        run(plugin, {}, &full);
    }
    CHECK(full.refused() > 0);
    out.clear();
    run(plugin, {}, &out);
    CHECK(endsIn(out) == std::vector<End>{{42, 3, 64}});
    out.clear();
    run(plugin, {}, &out);
    CHECK(endsIn(out).empty());
}

TEST_CASE("plugin_note_end_on_reset", "[plugin][voice]")
{
    // Review focus 5: reset() ends every note the Processor holds (held, pedal-held and
    // releasing), and the next process() call sends their NOTE_ENDs; the voices clear with the
    // pool, so that call is silent. Deactivation drops the notes with the Processor: the next
    // activation sends no stale NOTE_END.
    PluginUnderTest plugin(kInstrumentId);
    GrainLog log;
    setUpInstrument(plugin, log, "voice-reset", true, {{slotA(LayerParam::Env1Release), 0.0}});
    const auto on1 = note(CLAP_EVENT_NOTE_ON, 0, 60, 1);
    const auto on2 = note(CLAP_EVENT_NOTE_ON, 0, 64, 2);
    const auto midiOn = gg::test::midiEvent(0x95, 67, 100);
    const auto down = gg::test::midiEvent(0xB0, 64, 127);
    const auto off2 = note(CLAP_EVENT_NOTE_OFF, -1, -1, 2);
    const auto on4 = note(CLAP_EVENT_NOTE_ON, 0, 62, 4);
    const auto off4 = note(CLAP_EVENT_NOTE_OFF, -1, -1, 4);
    run(plugin, {&on1.header, &on2.header, &midiOn.header, &on4.header});
    // Note 4 releasing (Env 1's release is 1 s), then note 2 pedal-held. (A pedal and an off at
    // one sample apply notes first, Processor::process's tie rule: the off goes in a later call.)
    run(plugin, {&off4.header, &down.header});
    run(plugin, {&off2.header});
    OutputEvents before;
    run(plugin, {}, &before);
    CHECK(endsIn(before).empty());
    CHECK(heldKeys(plugin) == std::vector<int>{60, 64, 67});

    plugin.clap()->reset(plugin.clap());
    CHECK(heldKeys(plugin).empty());
    log.clear();
    OutputEvents out;
    gg::test::InputEvents none;
    Stereo audio(kBlock);
    REQUIRE(plugin.process(kBlock, nullptr, audio.data(), none.list(), 2, out.list()) ==
            CLAP_PROCESS_CONTINUE);
    auto ends = endsIn(out);
    std::sort(ends.begin(), ends.end(), [](const End &a, const End &b) { return a.key < b.key; });
    CHECK(ends == std::vector<End>{{1, 0, 60}, {4, 0, 62}, {2, 0, 64}, {-1, 5, 67}});
    CHECK(log.entries.empty());
    CHECK(std::all_of(audio.left.begin(), audio.left.end(), [](float x) { return x == 0.0f; }));
    out.clear();
    run(plugin, {}, &out);
    CHECK(endsIn(out).empty());

    // Notes held at a deactivation leave nothing behind.
    run(plugin, {&on1.header});
    plugin.deactivate();
    REQUIRE(plugin.activate(48000.0, kBlock));
    for (int k = 0; k < 4; ++k)
    {
        out.clear();
        run(plugin, {}, &out);
        CHECK(endsIn(out).empty());
    }
    CHECK(heldKeys(plugin).empty());
}

TEST_CASE("plugin_tail_covers_release", "[plugin][voice][space]")
{
    // Spec §4.6: the instrument's tail grows by Env 1's longest release, 10 s, to 132.5 s; the
    // FX's stays infinite (no layer outlasts it). A voice at that release ends inside the tail.
    using gg::plugin::GigaGrainsPlugin;
    const auto &release =
        gg::core::paramMeta(slotA(LayerParam::Env1Release), gg::core::Mode::Instrument);
    CHECK_THAT(std::exp2(static_cast<double>(release.maxVal)), WithinRel(10.0, 1e-6));
    CHECK(GigaGrainsPlugin::kInstrumentTailSeconds == 2.0 + 0.5 + 2.0 * 60.0 + 10.0);
    CHECK(GigaGrainsPlugin::instrumentTailFrames(48000.0) == 6360000u);

    PluginUnderTest plugin(kInstrumentId);
    GrainLog log;
    setUpInstrument(plugin, log, "voice-tail", true,
                    {{slotA(LayerParam::Env1Release), static_cast<double>(release.maxVal)}});
    const auto *tail = static_cast<const clap_plugin_tail *>(
        plugin.clap()->get_extension(plugin.clap(), CLAP_EXT_TAIL));
    REQUIRE(tail != nullptr);
    CHECK(tail->get(plugin.clap()) == 6360000u);

    const auto on = note(CLAP_EVENT_NOTE_ON, 0, 60, 3);
    const auto off = note(CLAP_EVENT_NOTE_OFF, 0, 60, 3);
    run(plugin, {&on.header});
    run(plugin, {&off.header});
    constexpr uint32_t kLong = 4096;
    OutputEvents out;
    std::size_t frames = 0; // after the off's block
    std::vector<End> ends;
    while (ends.empty() && frames < 12 * 48000)
    {
        out.clear();
        run(plugin, {}, &out, kLong);
        frames += kLong;
        ends = endsIn(out);
    }
    CHECK(ends == std::vector<End>{{3, 0, 60}});
    CHECK(frames + kBlock >= 480000u); // not before the 10 s release
    CHECK(frames < 480000u + 2 * kLong);
    CHECK(frames < 6360000u);
}
