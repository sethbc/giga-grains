// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Real-time safety of the plugin audio path (INTENT §14): process() and an active params flush
// never allocate, for both products, through the same clap_plugin vtables a host calls. The
// allocation tripwire checks the whole call, clap-helpers' dispatch included, in every preset.
// The asan and rtsan presets build the plugin core without its editor (GG_BUILD_EDITOR=OFF), so
// these tests also run under ASan/UBSan and under RealtimeSanitizer, which checks every
// SST_CPPUTILS_NONBLOCKING function they reach for allocation, locks and I/O.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <clap/clap.h>

#include "PluginHarness.h"
#include "gg/core/Rng.h"
#include "gg/engine/Engine.h"
#include "support/AllocTripwire.h"

using gg::test::AllocTripwire;

namespace
{
constexpr const char *kInstrumentId = "org.sethbc.giga-grains";
constexpr const char *kFxId = "org.sethbc.giga-grains-fx";

clap_event_note noteOn(int16_t key, uint32_t time)
{
    clap_event_note e{};
    e.header.size = sizeof(e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_NOTE_ON;
    e.note_id = -1;
    e.key = key;
    e.velocity = 0.8;
    return e;
}

clap_event_midi midiNoteOn(uint8_t key, uint32_t time)
{
    clap_event_midi e{};
    e.header.size = sizeof(e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_MIDI;
    e.data[0] = 0x90;
    e.data[1] = key;
    e.data[2] = 100;
    return e;
}

const clap_plugin_params *paramsOf(const gg::test::PluginUnderTest &plugin)
{
    return static_cast<const clap_plugin_params *>(
        plugin.clap()->get_extension(plugin.clap(), CLAP_EXT_PARAMS));
}
} // namespace

TEST_CASE("plugin process does not allocate", "[plugin][rt]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    const bool isFx = std::string_view(id) == kFxId;

    // Everything the calls touch exists before the tripwire is armed. 2048 frames is more than
    // the 512 the plugin was activated with.
    constexpr std::array<uint32_t, 4> kFrames{0, 1, 512, 2048};
    std::vector<float> inLeft(2048), inRight(2048), outLeft(2048), outRight(2048);
    for (std::size_t i = 0; i < inLeft.size(); ++i)
    {
        inLeft[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(2 * i)));
        inRight[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(2 * i + 1)));
    }
    float *inputs[2] = {inLeft.data(), inRight.data()};
    float *outputs[2] = {outLeft.data(), outRight.data()};
    const auto clapOn = noteOn(60, 0);
    const auto midiOn = midiNoteOn(64, 0);
    gg::test::InputEvents events;
    events.add(&clapOn.header);
    events.add(&midiOn.header);
    std::array<clap_process_status, kFrames.size()> status{};
    const uint32_t notesBefore = plugin.impl().noteOnCount.load();

    // No warm-up call: a host's first process() must not allocate either.
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (std::size_t i = 0; i < kFrames.size(); ++i)
        {
            status[i] = plugin.process(kFrames[i], isFx ? inputs : nullptr, outputs, events.list());
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    for (const auto s : status)
    {
        CHECK(s == CLAP_PROCESS_CONTINUE);
    }
    // The events were read: one CLAP and one MIDI note-on per call, the 0-frame call included.
    CHECK(plugin.impl().noteOnCount.load() - notesBefore == 2 * kFrames.size());
    REQUIRE(plugin.impl().engine()->sampleClock() == 1 + 512 + 2048);
    REQUIRE(allocations == 0);
}

TEST_CASE("plugin params flush does not allocate", "[plugin][rt]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    const auto *params = paramsOf(plugin);
    REQUIRE(params != nullptr);

    gg::test::InputEvents none;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        params->flush(plugin.clap(), none.list(), gg::test::discardingOutputEvents());
    }
    REQUIRE(AllocTripwire::count() == 0);
}

TEST_CASE("plugin params flush with notes does not allocate", "[plugin][rt]")
{
    // A host may deliver notes in an active flush (clap-wrapper's VST3 does). Release-type
    // builds check at Minimal and must not allocate then. Debug builds check at Maximal, which
    // builds a log string for each such event by design (ruling T8-e), so this runs only in
    // release-type builds: CI's build legs.
    if (gg::plugin::checkLevel != clap::helpers::CheckingLevel::Minimal)
    {
        SKIP("Debug build: clap-helpers checks at Maximal, which logs notes in a flush (T8-e)");
    }
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    const auto *params = paramsOf(plugin);
    REQUIRE(params != nullptr);

    const auto clapOn = noteOn(60, 0);
    const auto midiOn = midiNoteOn(67, 0);
    gg::test::InputEvents notes;
    notes.add(&clapOn.header);
    notes.add(&midiOn.header);
    const uint32_t notesBefore = plugin.impl().noteOnCount.load();

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        params->flush(plugin.clap(), notes.list(), gg::test::discardingOutputEvents());
    }
    const uint64_t allocations = AllocTripwire::count();

    CHECK(plugin.impl().noteOnCount.load() - notesBefore == 2);
    REQUIRE(allocations == 0);
}
