// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Real-time safety of the plugin audio path (INTENT §14): process() and an active params flush
// never allocate, for both products, with notes, parameter values and modulation, through the
// same clap_plugin vtables a host calls. The
// allocation tripwire checks the whole call, clap-helpers' dispatch included, in every preset
// but asan, where it is compiled out and only its count goes unchecked. The asan and rtsan
// presets build the plugin core without its editor (GG_BUILD_EDITOR=OFF), so these tests also
// run under ASan/UBSan and under RealtimeSanitizer, which checks every SST_CPPUTILS_NONBLOCKING
// function they reach for allocation, locks and I/O.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <clap/clap.h>

#include "PluginHarness.h"
#include "gg/core/ParamIds.h"
#include "gg/core/PatchState.h"
#include "gg/core/Processor.h"
#include "gg/core/Rng.h"
#include "gg/engine/Engine.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "support/AllocTripwire.h"

using gg::core::paramId;
using gg::core::ParamSlot;
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
    const auto density = gg::test::paramValue(paramId(ParamSlot::Density), 6.0);
    const auto mix = gg::test::paramValue(paramId(ParamSlot::Mix), 0.25);
    const auto pitchMod = gg::test::paramMod(paramId(ParamSlot::Pitch), 7.0);
    const auto levelMod = gg::test::paramMod(paramId(ParamSlot::Level), -6.0);
    const auto unknown = gg::test::paramValue(0x7FFFFF, 1.0);
    gg::test::InputEvents events;
    events.add(&clapOn.header);
    events.add(&density.header);
    events.add(&midiOn.header);
    events.add(&pitchMod.header);
    events.add(&mix.header);
    events.add(&unknown.header);
    events.add(&levelMod.header);
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
    // The parameter events were applied: values to the base, modulation on top of it.
    const auto *processor = plugin.impl().processor();
    REQUIRE(processor != nullptr);
    CHECK(processor->base(ParamSlot::Density) == 6.0f);
    CHECK(processor->base(ParamSlot::Mix) == 0.25f);
    CHECK(processor->base(ParamSlot::Pitch) == 0.0f);
    CHECK(processor->effective(ParamSlot::Pitch) == 7.0f);
    CHECK(processor->effective(ParamSlot::Level) == -6.0f);
    REQUIRE(plugin.impl().engine()->sampleClock() == 1 + 512 + 2048);
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("plugin process takes more events than its event array holds", "[plugin][rt]")
{
    // An automation storm past the fixed event array (4,096): the events beyond it apply at the
    // block end, values with setBase and modulation at the next grid boundary, without
    // allocating; the last value per parameter wins.
    gg::test::PluginUnderTest plugin(kFxId);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));

    constexpr std::size_t kValues = 5000;
    constexpr std::size_t kMods = 100;
    std::vector<clap_event_param_value> values;
    std::vector<clap_event_param_mod> mods;
    gg::test::InputEvents events;
    for (std::size_t i = 0; i < kValues; ++i)
    {
        const double v = (i % 2 == 0) ? 2.0 : 5.0 + static_cast<double>(i) / kValues;
        values.push_back(
            gg::test::paramValue(paramId(ParamSlot::Density), v, static_cast<uint32_t>(i / 10)));
    }
    for (std::size_t i = 0; i < kMods; ++i)
    {
        mods.push_back(
            gg::test::paramMod(paramId(ParamSlot::Pitch), static_cast<double>(i + 1) / 10.0, 500));
    }
    for (const auto &e : values)
    {
        events.add(&e.header);
    }
    for (const auto &e : mods)
    {
        events.add(&e.header);
    }
    const auto lastValue = static_cast<float>(values.back().value);
    const auto lastMod = static_cast<float>(mods.back().amount);

    std::vector<float> inLeft(512, 0.25f), inRight(512, -0.25f), outLeft(512), outRight(512);
    float *inputs[2] = {inLeft.data(), inRight.data()};
    float *outputs[2] = {outLeft.data(), outRight.data()};
    gg::test::InputEvents none;
    std::array<clap_process_status, 2> status{};

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        status[0] = plugin.process(512, inputs, outputs, events.list());
        // The modulation beyond the array waits for the next boundary: sample 512.
        status[1] = plugin.process(32, inputs, outputs, none.list());
    }
    const uint64_t allocations = AllocTripwire::count();

    CHECK(status[0] == CLAP_PROCESS_CONTINUE);
    CHECK(status[1] == CLAP_PROCESS_CONTINUE);
    const auto *processor = plugin.impl().processor();
    REQUIRE(processor != nullptr);
    CHECK(processor->base(ParamSlot::Density) == lastValue);
    CHECK(plugin.value(paramId(ParamSlot::Density)) == static_cast<double>(lastValue));
    CHECK(processor->base(ParamSlot::Pitch) == 0.0f);
    CHECK(processor->effective(ParamSlot::Pitch) == lastMod);
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("plugin applies a state loaded while active without allocating", "[plugin][rt]")
{
    // state.load (main thread) hands the values over; the audio thread applies them at the
    // start of its next process() or params.flush, which must not allocate.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    const bool isFx = std::string_view(id) == kFxId;

    gg::core::ParamValues first{};
    gg::core::ParamValues second{};
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        first[s] = plugin.impl().processor()->base(static_cast<ParamSlot>(s));
        second[s] = first[s];
    }
    first[static_cast<std::size_t>(ParamSlot::Density)] = 3.0f;
    second[static_cast<std::size_t>(ParamSlot::Density)] = 7.0f;

    std::vector<float> inLeft(512), inRight(512), outLeft(512), outRight(512);
    float *inputs[2] = {inLeft.data(), inRight.data()};
    float *outputs[2] = {outLeft.data(), outRight.data()};
    gg::test::InputEvents none;
    clap_process_status status{};

    REQUIRE(plugin.load(gg::core::toStateXml(first, 0)));
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        status = plugin.process(512, isFx ? inputs : nullptr, outputs, none.list());
    }
    uint64_t allocations = AllocTripwire::count();
    CHECK(status == CLAP_PROCESS_CONTINUE);
    CHECK(plugin.impl().processor()->base(ParamSlot::Density) == 3.0f);
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }

    REQUIRE(plugin.load(gg::core::toStateXml(second, 0)));
    const auto *params = paramsOf(plugin);
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        params->flush(plugin.clap(), none.list(), gg::test::discardingOutputEvents());
    }
    allocations = AllocTripwire::count();
    CHECK(plugin.impl().processor()->base(ParamSlot::Density) == 7.0f);
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("plugin reset does not allocate and a restored Freeze plays live", "[plugin][rt]")
{
    // CLAP reset(), which clap-wrapper's VST3 calls on every setProcessing(false), restarts the
    // Processor and its engine. It and the process() calls after it run armed (carry-over
    // C-m4). For the FX this also proves review finding I1's fix at the plugin boundary: a state
    // restored with Freeze on, as a project reopens, and a reset with Freeze on, play live
    // grains until Freeze is turned off and on again (INTENT §14, "live until toggled").
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const bool isFx = std::string_view(id) == kFxId;

    // Mix 1, so the FX's output is its wet signal alone, and Freeze on, restored before the
    // activation.
    gg::core::ParamValues restored{};
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        restored[s] = gg::core::paramMeta(static_cast<ParamSlot>(s)).defaultVal;
    }
    restored[static_cast<std::size_t>(ParamSlot::Mix)] = 1.0f;
    restored[static_cast<std::size_t>(ParamSlot::Freeze)] = 1.0f;
    REQUIRE(plugin.load(gg::core::toStateXml(restored, 0)));
    REQUIRE(plugin.activate(48000.0, 512));
    CHECK(plugin.value(paramId(ParamSlot::Freeze)) == 1.0); // Freeze still reads On

    constexpr uint32_t kBlock = 512;
    constexpr int kCalls = 94; // about 1 s
    std::vector<float> inLeft(kBlock), inRight(kBlock), outLeft(kBlock), outRight(kBlock);
    for (std::size_t i = 0; i < kBlock; ++i)
    {
        inLeft[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(2 * i)));
        inRight[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(2 * i + 1)));
    }
    float *inputs[2] = {inLeft.data(), inRight.data()};
    float *outputs[2] = {outLeft.data(), outRight.data()};
    gg::test::InputEvents none;
    const auto freezeOff = gg::test::paramValue(paramId(ParamSlot::Freeze), 0.0);
    const auto freezeOn = gg::test::paramValue(paramId(ParamSlot::Freeze), 1.0);
    gg::test::InputEvents off;
    off.add(&freezeOff.header);
    gg::test::InputEvents on;
    on.add(&freezeOn.header);
    const gg::Engine *engine = plugin.impl().engine();
    REQUIRE(engine != nullptr);

    // kCalls calls; returns the output's peak over the second half, without allocating.
    bool allContinue = true;
    const auto run = [&](const clap_input_events *first) {
        float p = 0.0f;
        for (int k = 0; k < kCalls; ++k)
        {
            allContinue = allContinue &&
                          plugin.process(kBlock, isFx ? inputs : nullptr, outputs,
                                         k == 0 ? first : none.list()) == CLAP_PROCESS_CONTINUE;
            for (uint32_t i = 0; k >= kCalls / 2 && i < kBlock; ++i)
            {
                p = std::max({p, std::abs(outLeft[i]), std::abs(outRight[i])});
            }
        }
        return p;
    };

    const float activated = run(none.list());
    const int64_t headAfterActivation = engine->liveWriteHead();

    float afterReset = 0.0f;
    float afterToggle = 0.0f;
    int64_t headAfterReset = 0;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        plugin.clap()->reset(plugin.clap());
        afterReset = run(none.list());
        headAfterReset = engine->liveWriteHead();
        run(off.list());
        afterToggle = run(on.list());
    }
    const uint64_t allocations = AllocTripwire::count();

    CHECK(allContinue);
    CHECK(engine->sampleClock() == 3 * kCalls * kBlock); // reset() restarted the clock
    CHECK(plugin.value(paramId(ParamSlot::Freeze)) == 1.0);
    if (isFx)
    {
        // Live after the activation and after the reset: the head never stopped, and the wet
        // signal sounds.
        CHECK(headAfterActivation == kCalls * kBlock);
        CHECK(headAfterReset == kCalls * kBlock);
        CHECK(activated > 0.05f);
        CHECK(afterReset > 0.05f);
        // Off, then on again: Freeze engages at the first boundary of the last run, and the
        // head stops 20 ms (960 samples) later. The held ring still sounds.
        CHECK(engine->liveWriteHead() == 2 * kCalls * kBlock + 960);
        CHECK(afterToggle > 0.05f);
    }
    else
    {
        // The instrument has no ring and, in this slice, no source: silence throughout.
        CHECK(engine->liveFrames() == 0);
        CHECK(activated == 0.0f);
        CHECK(afterReset == 0.0f);
        CHECK(afterToggle == 0.0f);
    }
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
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

    // Values apply at once (setBase), modulation at the next grid boundary; an unknown ID is
    // ignored.
    const auto density = gg::test::paramValue(paramId(ParamSlot::Density), 6.0);
    const auto mix = gg::test::paramValue(paramId(ParamSlot::Mix), 0.25);
    const auto pitchMod = gg::test::paramMod(paramId(ParamSlot::Pitch), 7.0);
    const auto unknown = gg::test::paramValue(0x7FFFFF, 1.0);
    gg::test::InputEvents none;
    gg::test::InputEvents events;
    events.add(&density.header);
    events.add(&pitchMod.header);
    events.add(&mix.header);
    events.add(&unknown.header);
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        params->flush(plugin.clap(), none.list(), gg::test::discardingOutputEvents());
        params->flush(plugin.clap(), events.list(), gg::test::discardingOutputEvents());
    }
    const uint64_t allocations = AllocTripwire::count();

    const auto *processor = plugin.impl().processor();
    REQUIRE(processor != nullptr);
    CHECK(processor->base(ParamSlot::Density) == 6.0f);
    CHECK(processor->base(ParamSlot::Mix) == 0.25f);
    CHECK(processor->base(ParamSlot::Pitch) == 0.0f);
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
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
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}
