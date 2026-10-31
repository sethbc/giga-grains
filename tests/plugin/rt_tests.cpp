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
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <clap/clap.h>

#include "PluginHarness.h"
#include "gg/core/GrainQueue.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/PatchState.h"
#include "gg/core/Processor.h"
#include "gg/core/Rng.h"
#include "gg/core/UiSnapshot.h"
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
    return gg::test::noteEvent(CLAP_EVENT_NOTE_ON, 0, key, time);
}

clap_event_midi midiNoteOn(uint8_t key, uint32_t time)
{
    return gg::test::midiEvent(0x90, key, 100, time);
}

// The keys held now, as ints so that failures print numbers.
std::vector<int> heldKeys(const gg::test::PluginUnderTest &plugin)
{
    const auto keys = plugin.impl().processor()->heldKeys();
    return {keys.begin(), keys.end()};
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
    const bool isFx = std::string_view(id) == kFxId;
    if (!isFx)
    {
        // A source, so the notes spawn grains in the instrument too.
        REQUIRE(plugin.loadSampleState(gg::test::sineSampleFile("rt-process")));
    }
    REQUIRE(plugin.activate(48000.0, 512));

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
    // The events were read: one CLAP and one MIDI note-on per call, the 0-frame call included,
    // and both notes reached the Processor.
    CHECK(plugin.impl().noteOnCount.load() - notesBefore == 2 * kFrames.size());
    CHECK(heldKeys(plugin) == std::vector<int>{60, 64});
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

TEST_CASE("plugin process with space automated does not allocate", "[plugin][rt][space]")
{
    // Every Space parameter automated through the CLAP events, values and modulation: Decay
    // into ∞ and out again, the reverb on and then off (its idle clear) and on again, the
    // pre-delay gliding. The calls are 0, 1, 512 and 2048 frames long, and none allocates.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const bool isFx = std::string_view(id) == kFxId;
    if (!isFx)
    {
        REQUIRE(plugin.loadSampleState(gg::test::sineSampleFile("rt-space")));
    }
    REQUIRE(plugin.activate(48000.0, 512));

    std::vector<float> inLeft(2048), inRight(2048), outLeft(2048), outRight(2048);
    for (std::size_t i = 0; i < inLeft.size(); ++i)
    {
        inLeft[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(2 * i)));
        inRight[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(2 * i + 1)));
    }
    float *inputs[2] = {inLeft.data(), inRight.data()};
    float *outputs[2] = {outLeft.data(), outRight.data()};
    const auto clapOn = noteOn(60, 0);
    const auto value = [](ParamSlot slot, double v, uint32_t time) {
        return gg::test::paramValue(paramId(slot), v, time);
    };
    const auto mod = [](ParamSlot slot, double v, uint32_t time) {
        return gg::test::paramMod(paramId(slot), v, time);
    };
    const auto infinite = static_cast<double>(gg::core::kDecayMax);
    // Sorted by time, as CLAP requires.
    const std::array onValues{
        value(ParamSlot::ReverbAmount, 1.0, 0),     value(ParamSlot::ReverbDecay, infinite, 0),
        value(ParamSlot::Diffusion, 0.9, 0),        value(ParamSlot::ReverbSize, 1.0, 1),
        value(ParamSlot::ReverbDamping, 0.0, 1),    value(ParamSlot::ReverbPredelay, 0.3, 1),
        value(ParamSlot::ReverbModulation, 1.0, 1), value(ParamSlot::DryToReverb, 1.0, 1),
    };
    const std::array onMods{
        mod(ParamSlot::ReverbDecay, 0.5, 1),       mod(ParamSlot::Diffusion, -0.2, 1),
        mod(ParamSlot::ReverbPredelay, 0.1, 1),    mod(ParamSlot::ReverbSize, -0.3, 1),
        mod(ParamSlot::ReverbModulation, -0.4, 1), mod(ParamSlot::DryToReverb, -0.5, 1),
    };
    // Out of ∞ and the reverb off.
    const std::array offValues{value(ParamSlot::ReverbAmount, 0.0, 0),
                               value(ParamSlot::ReverbDecay, 1.0, 0)};
    const std::array offMods{mod(ParamSlot::ReverbDecay, 0.0, 0)};
    gg::test::InputEvents on;
    on.add(&clapOn.header);
    for (const auto &e : onValues)
    {
        on.add(&e.header);
    }
    for (const auto &e : onMods)
    {
        on.add(&e.header);
    }
    gg::test::InputEvents off;
    for (const auto &e : offValues)
    {
        off.add(&e.header);
    }
    off.add(&offMods[0].header);
    gg::test::InputEvents none;
    struct Call
    {
        uint32_t frames;
        const gg::test::InputEvents *events;
    };
    constexpr std::size_t kCalls = 8;
    const std::array<Call, kCalls> calls{{{0, &on},
                                          {1, &on},
                                          {512, &on},
                                          {2048, &none},
                                          {512, &off},
                                          {2048, &none}, // idle: the clear runs
                                          {1, &on},
                                          {2048, &none}}};
    std::array<clap_process_status, kCalls> status{};
    std::array<bool, kCalls> active{};
    const auto *processor = plugin.impl().processor();
    REQUIRE(processor != nullptr);

    // No warm-up call: a host's first process() must not allocate either.
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (std::size_t i = 0; i < kCalls; ++i)
        {
            status[i] = plugin.process(calls[i].frames, isFx ? inputs : nullptr, outputs,
                                       calls[i].events->list());
            active[i] = processor->reverbActive();
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    for (const auto s : status)
    {
        CHECK(s == CLAP_PROCESS_CONTINUE);
    }
    // The reverb ran, went idle and ran again; the last values reached the processor.
    CHECK(active[3]);
    CHECK_FALSE(active[5]);
    CHECK(active[7]);
    CHECK(processor->effective(ParamSlot::ReverbDecay) >= gg::core::kDecayInfinite);
    CHECK(processor->effective(ParamSlot::ReverbPredelay) == 0.3f + 0.1f);
    CHECK(std::all_of(outLeft.begin(), outLeft.end(), [](float x) { return std::isfinite(x); }));
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("plugin process with the perform parameters automated does not allocate", "[plugin][rt]")
{
    // Every one of the 28 slots automated through the CLAP events, values and then modulation,
    // with publishing on (the editor open): the macros away from rest and back, Texture,
    // Feedback (the FX's; hidden in the instrument, whose processor ignores it) and Freeze
    // through Macro Freeze. The calls are the Space case's 0, 1, 512 and 2048 frames, and none
    // allocates, the grain queue and the snapshot included.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const bool isFx = std::string_view(id) == kFxId;
    const auto mode = plugin.impl().mode();
    if (!isFx)
    {
        REQUIRE(plugin.loadSampleState(gg::test::sineSampleFile("rt-perform")));
    }
    plugin.impl().setPublishing(true); // as the editor does when it opens
    REQUIRE(plugin.activate(48000.0, 512));

    std::vector<float> inLeft(2048), inRight(2048), outLeft(2048), outRight(2048);
    for (std::size_t i = 0; i < inLeft.size(); ++i)
    {
        inLeft[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(2 * i)));
        inRight[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(2 * i + 1)));
    }
    float *inputs[2] = {inLeft.data(), inRight.data()};
    float *outputs[2] = {outLeft.data(), outRight.data()};
    const auto clapOn = noteOn(60, 0);
    constexpr std::size_t kSlots = gg::core::kParamCount;
    static_assert(kSlots == 243);
    const auto metaOf = [mode](std::size_t s) -> const auto & {
        return gg::core::paramMeta(static_cast<ParamSlot>(s), mode);
    };
    const auto stepped = [&metaOf](std::size_t s) {
        return (metaOf(s).flags & CLAP_PARAM_IS_STEPPED) != 0;
    };
    // On: each slot at 70% of its range (stepped ones at their top: the switches on), then a
    // modulation of a tenth of its range; values at sample 0 and modulation at sample 1, sorted by
    // time as CLAP requires.
    std::array<clap_event_param_value, kSlots> onValues{};
    std::array<clap_event_param_mod, kSlots> onMods{};
    // Off: each slot back to its default (every macro to rest, Texture and Feedback to 0, Freeze
    // off), its modulation to 0.
    std::array<clap_event_param_value, kSlots> offValues{};
    std::array<clap_event_param_mod, kSlots> offMods{};
    for (std::size_t s = 0; s < kSlots; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        const auto &meta = metaOf(s);
        const double range = static_cast<double>(meta.maxVal) - meta.minVal;
        const double on = stepped(s) ? meta.maxVal : meta.minVal + 0.7 * range;
        onValues[s] = gg::test::paramValue(paramId(slot), on, 0);
        onMods[s] = gg::test::paramMod(paramId(slot), stepped(s) ? 0.0 : 0.1 * range, 1);
        offValues[s] = gg::test::paramValue(paramId(slot), meta.defaultVal, 0);
        offMods[s] = gg::test::paramMod(paramId(slot), 0.0, 0);
    }
    gg::test::InputEvents on;
    on.add(&clapOn.header);
    for (const auto &e : onValues)
    {
        on.add(&e.header);
    }
    for (const auto &e : onMods)
    {
        on.add(&e.header);
    }
    gg::test::InputEvents off;
    for (std::size_t s = 0; s < kSlots; ++s)
    {
        off.add(&offValues[s].header);
        off.add(&offMods[s].header);
    }
    gg::test::InputEvents none;
    struct Call
    {
        uint32_t frames;
        const gg::test::InputEvents *events;
    };
    constexpr std::size_t kCalls = 8;
    const std::array<Call, kCalls> calls{{{0, &on},
                                          {1, &on},
                                          {512, &on},
                                          {2048, &none},
                                          {512, &off},
                                          {2048, &none},
                                          {1, &on},
                                          {2048, &none}}};
    std::array<clap_process_status, kCalls> status{};
    auto *processor = plugin.impl().processor();
    REQUIRE(processor != nullptr);
    REQUIRE(plugin.impl().publishing());

    // No warm-up call: a host's first process() must not allocate either.
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (std::size_t i = 0; i < kCalls; ++i)
        {
            status[i] = plugin.process(calls[i].frames, isFx ? inputs : nullptr, outputs,
                                       calls[i].events->list());
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    for (const auto s : status)
    {
        CHECK(s == CLAP_PROCESS_CONTINUE);
    }
    // The last call's values reached the processor: the macros away from rest, and their
    // targets moved by them.
    for (const ParamSlot slot : gg::core::kMacroSlots)
    {
        const auto s = static_cast<std::size_t>(slot);
        CHECK(processor->base(slot) == static_cast<float>(onValues[s].value));
    }
    CHECK(processor->base(ParamSlot::Texture) ==
          static_cast<float>(onValues[static_cast<std::size_t>(ParamSlot::Texture)].value));
    CHECK(processor->effective(ParamSlot::Texture) == 1.0f); // Macro Texture tops it up
    if (isFx)
    {
        CHECK(processor->effective(ParamSlot::Feedback) == gg::core::kFeedbackMax);
    }
    // Published: grains queued and a snapshot of the last call.
    gg::core::GrainVis grain{};
    CHECK(processor->grainQueue().pop(grain));
    gg::core::UiSnapshot snap{};
    REQUIRE(processor->snapshot().take(snap));
    CHECK(snap.clock == plugin.impl().engine()->sampleClock());
    CHECK(std::all_of(outLeft.begin(), outLeft.end(), [](float x) { return std::isfinite(x); }));
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("plugin process under a small Budget does not allocate", "[plugin][rt][budget]")
{
    // M2 engine spec §6.4, §6.5, §7: Budget at its minimum, 256 grains, under four HQ layers of
    // 400 Hz and 100 ms grains, set and then automated through the CLAP events (Budget's value
    // and each layer's Quality modulated), so the quotas move and the overload ladder lowers,
    // drops and steals inside process(). No call allocates, the steal events' path included.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const bool isFx = std::string_view(id) == kFxId;
    if (!isFx)
    {
        REQUIRE(plugin.loadSampleState(gg::test::sineSampleFile("rt-budget")));
    }
    plugin.impl().setPublishing(true);
    REQUIRE(plugin.activate(48000.0, 512));

    std::vector<float> inLeft(2048), inRight(2048), outLeft(2048), outRight(2048);
    for (std::size_t i = 0; i < inLeft.size(); ++i)
    {
        inLeft[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(2 * i)));
        inRight[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(2 * i + 1)));
    }
    float *inputs[2] = {inLeft.data(), inRight.data()};
    float *outputs[2] = {outLeft.data(), outRight.data()};
    const auto clapOn = noteOn(60, 0);
    std::vector<clap_event_param_value> setup;
    setup.push_back(gg::test::paramValue(paramId(ParamSlot::Budget), 256.0));
    for (uint8_t L = 0; L < gg::core::kLayers; ++L)
    {
        using gg::core::LayerParam;
        setup.push_back(gg::test::paramValue(paramId(gg::core::layerOnSlot(L)), 1.0));
        setup.push_back(
            gg::test::paramValue(paramId(gg::core::layerSlot(LayerParam::Quality, L)), 2.0));
        setup.push_back(gg::test::paramValue(paramId(gg::core::layerSlot(LayerParam::Density, L)),
                                             8.643856189774725));
        setup.push_back(
            gg::test::paramValue(paramId(gg::core::layerSlot(LayerParam::Size, L)), -3.3));
    }
    gg::test::InputEvents first;
    first.add(&clapOn.header);
    for (const auto &e : setup)
    {
        first.add(&e.header);
    }
    // Budget up and back, and layer A's Quality modulated down to Eco and back, mid-call.
    const auto budgetUp = gg::test::paramValue(paramId(ParamSlot::Budget), 1024.0, 100);
    const auto qualityDown = gg::test::paramMod(paramId(ParamSlot::Quality), -2.0, 300);
    const auto budgetDown = gg::test::paramValue(paramId(ParamSlot::Budget), 256.0, 700);
    const auto qualityBack = gg::test::paramMod(paramId(ParamSlot::Quality), 0.0, 900);
    gg::test::InputEvents moves;
    moves.add(&budgetUp.header);
    moves.add(&qualityDown.header);
    moves.add(&budgetDown.header);
    moves.add(&qualityBack.header);
    gg::test::InputEvents none;
    struct Call
    {
        uint32_t frames;
        const gg::test::InputEvents *events;
    };
    constexpr std::size_t kCalls = 12;
    const std::array<Call, kCalls> calls{{{0, &first},
                                          {1, &none},
                                          {512, &none},
                                          {2048, &none},
                                          {2048, &none},
                                          {2048, &moves},
                                          {2048, &none},
                                          {1, &moves},
                                          {2048, &moves},
                                          {2048, &none},
                                          {2048, &none},
                                          {2048, &none}}};
    std::array<clap_process_status, kCalls> status{};
    auto *processor = plugin.impl().processor();
    REQUIRE(processor != nullptr);

    // No warm-up call: a host's first process() must not allocate either.
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (std::size_t i = 0; i < kCalls; ++i)
        {
            status[i] = plugin.process(calls[i].frames, isFx ? inputs : nullptr, outputs,
                                       calls[i].events->list());
            gg::core::GrainVis g;
            while (processor->grainQueue().pop(g))
            {
            }
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    for (const auto s : status)
    {
        CHECK(s == CLAP_PROCESS_CONTINUE);
    }
    CHECK(processor->base(ParamSlot::Budget) == 256.0f);
    CHECK(processor->base(ParamSlot::D_Quality) == 2.0f);
    // The ladder ran.
    CHECK(processor->loweredGrains() > 0);
    CHECK(processor->stolenGrains() > 0);
    CHECK(std::all_of(outLeft.begin(), outLeft.end(), [](float x) { return std::isfinite(x); }));
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("plugin process takes more events than its event array holds", "[plugin][rt]")
{
    // An automation storm past the fixed event array (kMaxParamEvents, 5,120): the events
    // beyond it apply at the block end, values with setBase and modulation at the next grid
    // boundary, without allocating; the last value per parameter wins.
    gg::test::PluginUnderTest plugin(kFxId);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));

    constexpr std::size_t kValues = 6000;
    static_assert(kValues > gg::plugin::GigaGrainsPlugin::kMaxParamEvents);
    constexpr std::size_t kMods = 100;
    std::vector<clap_event_param_value> values;
    std::vector<clap_event_param_mod> mods;
    gg::test::InputEvents events;
    for (std::size_t i = 0; i < kValues; ++i)
    {
        const double v = (i % 2 == 0) ? 2.0 : 5.0 + static_cast<double>(i) / kValues;
        values.push_back(
            gg::test::paramValue(paramId(ParamSlot::Density), v, static_cast<uint32_t>(i / 12)));
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
    // start of its next process() or params.flush, which must not allocate. Each state brings
    // a new seed (5, then 6), so the seed's hand-off runs too.
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

    REQUIRE(plugin.load(gg::core::toStateXml(
        first, 5,
        gg::core::factoryRoutes(isFx ? gg::core::Mode::Fx : gg::core::Mode::Instrument))));
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        status = plugin.process(512, isFx ? inputs : nullptr, outputs, none.list());
    }
    uint64_t allocations = AllocTripwire::count();
    CHECK(status == CLAP_PROCESS_CONTINUE);
    CHECK(plugin.impl().processor()->base(ParamSlot::Density) == 3.0f);
    CHECK(plugin.impl().processor()->seed() == 5);
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }

    REQUIRE(plugin.load(gg::core::toStateXml(
        second, 6,
        gg::core::factoryRoutes(isFx ? gg::core::Mode::Fx : gg::core::Mode::Instrument))));
    const auto *params = paramsOf(plugin);
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        params->flush(plugin.clap(), none.list(), gg::test::discardingOutputEvents());
    }
    allocations = AllocTripwire::count();
    CHECK(plugin.impl().processor()->base(ParamSlot::Density) == 7.0f);
    CHECK(plugin.impl().processor()->seed() == 6);
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("plugin process adopts and retires sample sources without allocating", "[plugin][rt]")
{
    // Spec §6.4, §7: a sample restored while active reaches the audio thread through the
    // Processor's SourceExchange. process() adopts it, and once a newer one has replaced it and
    // its last grain has ended, hands it back, neither allocating nor freeing; the main thread
    // frees it (on_main_thread).
    const auto dir = gg::test::sampleDir("rt-exchange");
    const auto a = gg::test::writeSineWav(dir / "a.wav", 48000, 1.0, 220.0);
    const auto b = gg::test::writeSineWav(dir / "b.wav", 48000, 1.0, 330.0);
    gg::test::PluginUnderTest plugin(kInstrumentId);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    const auto &sources = plugin.impl().processor()->sources();

    std::vector<float> outLeft(512), outRight(512);
    float *outputs[2] = {outLeft.data(), outRight.data()};
    const auto on = noteOn(60, 0);
    gg::test::InputEvents withNote;
    withNote.add(&on.header);
    gg::test::InputEvents none;
    clap_process_status status = CLAP_PROCESS_CONTINUE;

    REQUIRE(plugin.loadSampleState(a)); // offers A
    CHECK(sources.held() == 1);
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        status = plugin.process(512, nullptr, outputs, withNote.list()); // adopts A
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    CHECK(status == CLAP_PROCESS_CONTINUE);
    CHECK(*std::max_element(outLeft.begin(), outLeft.end()) > 0.0f); // the note plays A

    REQUIRE(plugin.loadSampleState(b)); // offers B while A's grains sound
    CHECK(sources.held() == 2);
    // 40 blocks, about 0.43 s: B is adopted at once, and A drains (Size, 150 ms) and retires.
    bool continued = true;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int i = 0; i < 40; ++i)
        {
            continued =
                plugin.process(512, nullptr, outputs, none.list()) == CLAP_PROCESS_CONTINUE &&
                continued;
        }
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    CHECK(continued);
    CHECK(sources.held() == 2); // A handed back, but the audio side never frees
    plugin.onMainThread();
    CHECK(sources.held() == 1); // freed on the main thread
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
        restored[s] = gg::core::paramMeta(static_cast<ParamSlot>(s),
                                          isFx ? gg::core::Mode::Fx : gg::core::Mode::Instrument)
                          .defaultVal;
    }
    restored[static_cast<std::size_t>(ParamSlot::Mix)] = 1.0f;
    restored[static_cast<std::size_t>(ParamSlot::Freeze)] = 1.0f;
    REQUIRE(plugin.load(gg::core::toStateXml(
        restored, 0,
        gg::core::factoryRoutes(isFx ? gg::core::Mode::Fx : gg::core::Mode::Instrument))));
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
        // The instrument's layer A reads a sample (its ring goes unread, M2 sub-project 1's spec
        // §6.4) and, in this slice, has no source: silence throughout.
        CHECK(engine->sourceMode() == gg::SourceMode::Sample);
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

TEST_CASE("ui parameter changes do not allocate", "[plugin][rt]")
{
    // The editor's queue full (1,024 changes: a gesture around 1,022 values), drained by
    // process() and then again by an active params flush, each change sent on to the host.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    auto &impl = plugin.impl();
    const auto *params = paramsOf(plugin);
    REQUIRE(params != nullptr);
    constexpr std::size_t kChanges = gg::plugin::GigaGrainsPlugin::kUiQueueCapacity;
    static_assert(kChanges == 1024);
    const auto queueDrag = [&impl](ParamSlot slot, float base) {
        impl.uiBeginGesture(slot);
        for (std::size_t i = 0; i + 2 < kChanges; ++i)
        {
            impl.uiSetValue(slot, base + 0.0001f * static_cast<float>(i % 1000));
        }
        impl.uiEndGesture(slot);
    };

    std::vector<float> inLeft(512, 0.25f), inRight(512, -0.25f), outLeft(512), outRight(512);
    float *inputs[2] = {inLeft.data(), inRight.data()};
    float *outputs[2] = {outLeft.data(), outRight.data()};
    gg::test::InputEvents none;
    gg::test::OutputEvents fromProcess(kChanges);
    gg::test::OutputEvents fromFlush(kChanges);
    clap_process_status status{};

    queueDrag(ParamSlot::Position, 0.5f);
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        status = plugin.process(512, inputs, outputs, none.list(), 2, fromProcess.list());
    }
    queueDrag(ParamSlot::Spray, 0.2f); // unarmed: the main thread's side
    {
        AllocTripwire::Scope armed;
        params->flush(plugin.clap(), none.list(), fromFlush.list());
    }
    const uint64_t allocations = AllocTripwire::count(); // both armed scopes

    CHECK(status == CLAP_PROCESS_CONTINUE);
    CHECK(fromProcess.events().size() == kChanges);
    CHECK(fromFlush.events().size() == kChanges);
    CHECK(impl.uiEventsDropped() == 0);
    const float lastPosition = 0.5f + 0.0001f * static_cast<float>((kChanges - 3) % 1000);
    const float lastSpray = 0.2f + 0.0001f * static_cast<float>((kChanges - 3) % 1000);
    CHECK(impl.processor()->base(ParamSlot::Position) == lastPosition);
    CHECK(impl.processor()->base(ParamSlot::Spray) == lastSpray);
    CHECK(impl.uiBaseValue(ParamSlot::Spray) == lastSpray);
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
    // MIDI controls, which a flush passes as it does notes (M2 sub-project 1's spec §6.8).
    const auto wheel = gg::test::midiEvent(0xB0, 1, 127, 0);
    const auto bend = gg::test::midiEvent(0xE0, 0x7F, 0x7F, 0);
    gg::test::InputEvents notes;
    notes.add(&clapOn.header);
    notes.add(&midiOn.header);
    notes.add(&wheel.header);
    notes.add(&bend.header);
    const uint32_t notesBefore = plugin.impl().noteOnCount.load();

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        params->flush(plugin.clap(), notes.list(), gg::test::discardingOutputEvents());
    }
    const uint64_t allocations = AllocTripwire::count();

    CHECK(plugin.impl().noteOnCount.load() - notesBefore == 2);
    // They wait for the next boundary, which the next call applies.
    std::vector<float> left(64), right(64);
    float *outputs[2] = {left.data(), right.data()};
    REQUIRE(plugin.process(64, nullptr, outputs) == CLAP_PROCESS_CONTINUE);
    CHECK(plugin.impl().processor()->sourceValue(gg::core::ModSource::ModWheel, 0) == 1.0f);
    CHECK(plugin.impl().processor()->sourceValue(gg::core::ModSource::PitchBend, 0) == 1.0f);
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("plugin notes of every kind reach the processor without allocating", "[plugin][rt]")
{
    // Spec §6.1's inputs, through process(): CLAP note-on, note-off (with wildcards) and note
    // choke; MIDI note-on, note-on with velocity 0, note-off and CC 123. Each call's notes are
    // at offset 0, so they apply at a boundary inside the call; one call is a single frame.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const bool isFx = std::string_view(id) == kFxId;
    if (!isFx)
    {
        REQUIRE(plugin.loadSampleState(gg::test::sineSampleFile("rt-notes")));
    }
    REQUIRE(plugin.activate(48000.0, 512));

    using gg::test::midiEvent;
    using gg::test::noteEvent;
    const auto clapOn60 = noteEvent(CLAP_EVENT_NOTE_ON, 0, 60);
    const auto midiOn64Channel1 = midiEvent(0x91, 64, 100);
    const auto choke60 = noteEvent(CLAP_EVENT_NOTE_CHOKE, 0, 60);
    const auto midiOff64Channel1 = midiEvent(0x81, 64, 0);
    const auto midiOn62 = midiEvent(0x90, 62, 100);
    const auto clapOn65Channel2 = noteEvent(CLAP_EVENT_NOTE_ON, 2, 65);
    const auto clapOffAll = noteEvent(CLAP_EVENT_NOTE_OFF, -1, -1);
    const auto clapOn70 = noteEvent(CLAP_EVENT_NOTE_ON, 0, 70);
    const auto midiOn71Channel15 = midiEvent(0x9F, 71, 100);
    const auto midiOff71Velocity0 = midiEvent(0x9F, 71, 0);
    const auto midiOn72 = midiEvent(0x90, 72, 100);
    const auto allNotesOff = midiEvent(0xB0, 123, 0);
    constexpr std::size_t kCalls = 7;
    std::array<gg::test::InputEvents, kCalls> events;
    events[0].add(&clapOn60.header);
    events[0].add(&midiOn64Channel1.header);
    events[1].add(&choke60.header);
    events[2].add(&midiOff64Channel1.header);
    events[3].add(&midiOn62.header);
    events[3].add(&clapOn65Channel2.header);
    events[4].add(&clapOffAll.header);
    events[5].add(&clapOn70.header);
    events[5].add(&midiOn71Channel15.header);
    events[5].add(&midiOff71Velocity0.header);
    events[5].add(&midiOn72.header);
    events[6].add(&allNotesOff.header);
    // The second call is one frame long, at sample 64: a boundary, so its notes apply in it.
    constexpr std::array<uint32_t, kCalls> kFrames{64, 1, 64, 64, 64, 64, 64};
    const std::array<std::vector<int>, kCalls> expected{{
        {60, 64},
        {64},
        {},
        {62, 65},
        {},
        {70, 72},
        {},
    }};

    std::vector<float> inLeft(64, 0.25f), inRight(64, -0.25f), outLeft(64), outRight(64);
    float *inputs[2] = {inLeft.data(), inRight.data()};
    float *outputs[2] = {outLeft.data(), outRight.data()};
    std::array<clap_process_status, kCalls> status{};
    // The held keys after each call, recorded without allocating: at most two.
    std::array<std::size_t, kCalls> counts{};
    std::array<std::array<uint8_t, 2>, kCalls> keys{};

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (std::size_t i = 0; i < kCalls; ++i)
        {
            status[i] =
                plugin.process(kFrames[i], isFx ? inputs : nullptr, outputs, events[i].list());
            const auto held = plugin.impl().processor()->heldKeys();
            counts[i] = held.size();
            for (std::size_t j = 0; j < std::min<std::size_t>(held.size(), 2); ++j)
            {
                keys[i][j] = held[j];
            }
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    for (std::size_t i = 0; i < kCalls; ++i)
    {
        CAPTURE(i);
        CHECK(status[i] == CLAP_PROCESS_CONTINUE);
        REQUIRE(counts[i] == expected[i].size());
        CHECK(std::vector<int>(keys[i].begin(), keys[i].begin() + counts[i]) == expected[i]);
    }
    CHECK(plugin.impl().noteOnCount.load() == 7);
    REQUIRE_NO_ALLOCATIONS(allocations);
}

TEST_CASE("plugin process takes more notes than its note array holds", "[plugin][rt]")
{
    // 1,500 notes in one call, past the fixed note array (1,024): the notes beyond it apply at
    // the block end, at the next grid boundary, without allocating, and none is lost. Notes
    // 0..1498 hold (channel i % 16, key i / 16), CLAP and MIDI in turn, so keys 0..93 are held,
    // and the keys from 64 up only by notes past the array. The last note, a CLAP note-off
    // (channel −1, key 93), releases key 93 on every channel.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const bool isFx = std::string_view(id) == kFxId;
    REQUIRE(plugin.activate(48000.0, 512));

    constexpr std::size_t kNotes = 1500;
    std::vector<clap_event_note> clapNotes;
    std::vector<clap_event_midi> midiNotes;
    clapNotes.reserve(kNotes);
    midiNotes.reserve(kNotes);
    gg::test::InputEvents events;
    for (std::size_t i = 0; i + 1 < kNotes; ++i)
    {
        const auto time = static_cast<uint32_t>(i / 3); // 0..499, in order
        const auto channel = static_cast<uint8_t>(i % 16);
        const auto key = static_cast<uint8_t>(i / 16);
        if (i % 2 == 0)
        {
            clapNotes.push_back(gg::test::noteEvent(CLAP_EVENT_NOTE_ON, channel, key, time));
            events.add(&clapNotes.back().header);
        }
        else
        {
            midiNotes.push_back(
                gg::test::midiEvent(static_cast<uint8_t>(0x90 | channel), key, 100, time));
            events.add(&midiNotes.back().header);
        }
    }
    clapNotes.push_back(gg::test::noteEvent(CLAP_EVENT_NOTE_OFF, -1, 93, 499));
    events.add(&clapNotes.back().header);
    std::vector<int> expected(93);
    for (std::size_t k = 0; k < expected.size(); ++k)
    {
        expected[k] = static_cast<int>(k);
    }

    std::vector<float> inLeft(512, 0.25f), inRight(512, -0.25f), outLeft(512), outRight(512);
    float *inputs[2] = {inLeft.data(), inRight.data()};
    float *outputs[2] = {outLeft.data(), outRight.data()};
    gg::test::InputEvents none;
    // Then key 80, held only by notes past the array (1280..1295), released channel by channel:
    // CLAP and MIDI note-offs in turn.
    std::vector<clap_event_note> clapOffs;
    std::vector<clap_event_midi> midiOffs;
    gg::test::InputEvents offs80;
    for (int16_t channel = 0; channel < 16; ++channel)
    {
        if (channel % 2 == 0)
        {
            clapOffs.push_back(gg::test::noteEvent(CLAP_EVENT_NOTE_OFF, channel, 80));
        }
        else
        {
            midiOffs.push_back(gg::test::midiEvent(static_cast<uint8_t>(0x80 | channel), 80, 0));
        }
    }
    for (const auto &e : clapOffs)
    {
        offs80.add(&e.header);
    }
    for (const auto &e : midiOffs)
    {
        offs80.add(&e.header);
    }
    std::array<clap_process_status, 3> status{};
    std::size_t heldAfterStorm = 0;

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        status[0] = plugin.process(512, isFx ? inputs : nullptr, outputs, events.list());
        // The notes beyond the array wait for the next boundary: sample 512.
        status[1] = plugin.process(32, isFx ? inputs : nullptr, outputs, none.list());
        heldAfterStorm = plugin.impl().processor()->heldKeys().size();
        status[2] = plugin.process(32, isFx ? inputs : nullptr, outputs, offs80.list());
    }
    const uint64_t allocations = AllocTripwire::count();

    for (const auto s : status)
    {
        CHECK(s == CLAP_PROCESS_CONTINUE);
    }
    CHECK(heldAfterStorm == expected.size());
    expected.erase(expected.begin() + 80);
    CHECK(heldKeys(plugin) == expected);
    CHECK(plugin.impl().noteOnCount.load() == kNotes - 1);
    REQUIRE_NO_ALLOCATIONS(allocations);
}

TEST_CASE("plugin note ids stay bounded and do not allocate", "[plugin][rt]")
{
    // Ruling R15: the plugin remembers at most kMaxNoteIds (256) CLAP note IDs, replacing the
    // oldest. 258 note-ons with IDs 0..257: ID 0 holds (channel 0, key 0), ID i > 0 holds
    // (channel 1, key 1 + i % 127), so keys 0..127 are held. ID 256 replaces ID 0 and ID 257
    // replaces ID 1, the oldest left. A note-off by ID 256 releases its key (3) alone; one by
    // ID 1, replaced, is unknown, so it releases everything.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    REQUIRE(gg::plugin::GigaGrainsPlugin::kMaxNoteIds == 256);

    std::vector<clap_event_note> ons;
    for (int32_t i = 0; i <= 257; ++i)
    {
        const auto channel = static_cast<int16_t>(i == 0 ? 0 : 1);
        const auto key = static_cast<int16_t>(i == 0 ? 0 : 1 + i % 127);
        ons.push_back(gg::test::noteEvent(CLAP_EVENT_NOTE_ON, channel, key));
        ons.back().note_id = i;
    }
    gg::test::InputEvents first256;
    for (std::size_t i = 0; i <= 256; ++i)
    {
        first256.add(&ons[i].header);
    }
    gg::test::InputEvents on257;
    on257.add(&ons[257].header);
    auto offById256 = gg::test::noteEvent(CLAP_EVENT_NOTE_OFF, -1, -1);
    offById256.note_id = 256;
    auto offById1 = gg::test::noteEvent(CLAP_EVENT_NOTE_OFF, -1, -1);
    offById1.note_id = 1;
    gg::test::InputEvents off256;
    off256.add(&offById256.header);
    gg::test::InputEvents off1;
    off1.add(&offById1.header);

    std::array<std::size_t, 4> held{};
    bool key3Held = true;
    bool allContinue = true;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        const auto call = [&](const gg::test::InputEvents &events) {
            allContinue = allContinue && plugin.process(64, nullptr, nullptr, events.list()) ==
                                             CLAP_PROCESS_CONTINUE;
            return plugin.impl().processor()->heldKeys().size();
        };
        held[0] = call(first256);
        held[1] = call(on257);
        held[2] = call(off256);
        const auto keys = plugin.impl().processor()->heldKeys();
        key3Held = std::find(keys.begin(), keys.end(), uint8_t{3}) != keys.end();
        held[3] = call(off1);
    }
    const uint64_t allocations = AllocTripwire::count();

    CHECK(allContinue);
    CHECK(held[0] == 128);
    CHECK(held[1] == 128);
    CHECK(held[2] == 127); // ID 256 still remembered: 0 if ID 257 had replaced it
    CHECK_FALSE(key3Held);
    CHECK(held[3] == 0); // a remembered ID 1 would have left 126
    REQUIRE_NO_ALLOCATIONS(allocations);
}

TEST_CASE("plugin process with four layers, controls and the pool does not allocate",
          "[plugin][rt]")
{
    // M2 sub-project 1's spec §6.7, §6.8: the four layers on (the instrument's A on its sample,
    // the others Live on the sidechain), the host's thread pool taking their tasks (a host that
    // accepts and one that declines), MIDI controls (more than kMaxControlEvents in one call, so
    // the rest apply at the block's end), velocity, the transport each call, and a last call with
    // the transport alone. (A params.flush with controls is "plugin params flush with notes does
    // not allocate"'s: Debug builds log MIDI in a flush, T8-e.) The tripwire counts the audio
    // thread's allocations; under RTSan the tasks the pool's threads run (the plugin's exec) are
    // checked too.
    const char *id = GENERATE(kInstrumentId, kFxId);
    const bool declining = GENERATE(false, true);
    CAPTURE(id, declining);
    const bool isFx = std::string_view(id) == kFxId;
    gg::test::PoolHost host(declining);
    gg::test::PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    host.attach(plugin.clap());
    if (!isFx)
    {
        REQUIRE(plugin.loadSampleState(gg::test::sineSampleFile("rt-pool")));
    }
    {
        std::vector<clap_event_param_value> values;
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            values.push_back(gg::test::paramValue(paramId(gg::core::layerOnSlot(layer)), 1.0));
            values.push_back(gg::test::paramValue(
                paramId(gg::core::layerSlot(gg::core::LayerParam::Source, layer)),
                layer == 0 && !isFx ? 0.0 : 1.0));
        }
        gg::test::InputEvents list;
        for (const auto &v : values)
        {
            list.add(&v.header);
        }
        plugin.flush(list.list());
    }
    REQUIRE(plugin.activate(48000.0, 512));

    std::vector<float> inLeft(2048), inRight(2048), outLeft(2048), outRight(2048);
    for (std::size_t i = 0; i < inLeft.size(); ++i)
    {
        inLeft[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(2 * i)));
        inRight[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(2 * i + 1)));
    }
    float *inputs[2] = {inLeft.data(), inRight.data()};
    float *outputs[2] = {outLeft.data(), outRight.data()};
    auto clapOn = noteOn(60, 0);
    clapOn.velocity = 0.25;
    const auto midiOn = midiNoteOn(64, 0);
    const auto bend = gg::test::midiEvent(0xE0, 0x10, 0x50, 1);
    const auto pressure = gg::test::midiEvent(0xD0, 90, 0, 2);
    // More CCs than one call hands the Processor: the last 100 apply at the block's end, the last
    // of them (99 / 127) at the next call's first boundary.
    constexpr std::size_t kWheels = gg::plugin::GigaGrainsPlugin::kMaxControlEvents + 100;
    std::vector<clap_event_midi> wheels;
    wheels.reserve(kWheels);
    for (std::size_t i = 0; i < kWheels; ++i)
    {
        wheels.push_back(gg::test::midiEvent(0xB0, 1, static_cast<uint8_t>(i % 128), 3));
    }
    gg::test::InputEvents events;
    events.add(&clapOn.header);
    events.add(&midiOn.header);
    events.add(&bend.header);
    events.add(&pressure.header);
    for (const auto &w : wheels)
    {
        events.add(&w.header);
    }
    const auto transport = gg::test::transportEvent(128.0, true, 3.25);
    constexpr std::array<uint32_t, 4> kFrames{0, 1, 512, 2048};
    std::array<clap_process_status, kFrames.size()> status{};
    auto *processor = plugin.impl().processor();
    REQUIRE(processor != nullptr);

    // No warm-up call: a host's first process() must not allocate either.
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (std::size_t i = 0; i < kFrames.size(); ++i)
        {
            status[i] =
                plugin.process(kFrames[i], inputs, outputs, events.list(), 2, nullptr, &transport);
        }
        plugin.process(512, inputs, outputs, nullptr, 2, nullptr, &transport);
    }
    const uint64_t allocations = AllocTripwire::count();

    for (const auto s : status)
    {
        CHECK(s == CLAP_PROCESS_CONTINUE);
    }
    using gg::core::ModSource;
    CHECK(heldKeys(plugin) == std::vector<int>{60, 64});
    CHECK(processor->sourceValue(ModSource::Velocity, 0) == 100.0f / 127.0f); // the last note-on
    CHECK(processor->sourceValue(ModSource::PitchBend, 0) == ((0x50 << 7) | 0x10) / 16383.0f);
    CHECK(processor->sourceValue(ModSource::Aftertouch, 0) == 90.0f / 127.0f);
    CHECK(processor->sourceValue(ModSource::ModWheel, 0) == 99.0f / 127.0f);
    CHECK(processor->sourceValue(ModSource::TransportPlaying, 0) == 1.0f);
    // Every call with four layers asked the host for four tasks per piece.
    CHECK(host.requests() > 0);
    CHECK(host.tasks() == 4 * host.requests());
    CHECK(std::all_of(outLeft.begin(), outLeft.end(), [](float x) { return std::isfinite(x); }));
    CHECK(std::any_of(outLeft.begin(), outLeft.end(), [](float x) { return x != 0.0f; }));
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("plugin process and flush adopt rings and apply commands without allocating",
          "[plugin][rt]")
{
    // M2 sub-project 2's spec §7: a ring resize's offer (the bounded copy of the frames written
    // since the main thread's), a restored ring's offer and a ring command. process() takes them
    // without allocating or freeing; params.flush, with one pending, adopts no ring (it stays
    // trivial: the offer waits whole for the next process() call) and doesn't allocate either.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const bool isFx = std::string_view(id) == kFxId;
    gg::test::useMediaScratch("rt-rings");
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    const auto *params = paramsOf(plugin);
    const auto *processor = plugin.impl().processor();
    std::vector<float> inLeft(512, 0.25f), inRight(512, -0.25f), outLeft(512), outRight(512);
    float *inputs[2] = {inLeft.data(), inRight.data()};
    float *outputs[2] = {outLeft.data(), outRight.data()};
    gg::test::InputEvents none;
    const auto ringLength = [&](double log2Seconds) {
        const auto e = gg::test::paramValue(
            paramId(gg::core::layerSlot(gg::core::LayerParam::RingLength, 0)), log2Seconds);
        gg::test::InputEvents events;
        events.add(&e.header);
        REQUIRE(plugin.process(512, inputs, outputs, events.list()) == CLAP_PROCESS_CONTINUE);
        plugin.onMainThread(); // the watcher offers the new ring
        REQUIRE(processor->sources(0).ringAdoptionPending());
    };
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(plugin.process(512, inputs, outputs, none.list()) == CLAP_PROCESS_CONTINUE);
    }

    // A resize: a flush with it pending adopts nothing, the next process() adopts it.
    ringLength(2.0);
    const gg::RingStorage *before = processor->ringRead(0).storage;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        params->flush(plugin.clap(), none.list(), gg::test::discardingOutputEvents());
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    CHECK(processor->ringRead(0).storage == before);
    CHECK(processor->sources(0).ringAdoptionPending());
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        plugin.process(512, inputs, outputs, none.list());
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    CHECK(processor->ringRead(0).storage->frames() == 4 * 48000);
    CHECK_FALSE(processor->sources(0).ringAdoptionPending());
    plugin.onMainThread(); // frees the storage replaced

    // A restored ring (layer A Live and frozen: D1 saves it), taken by process(), then by a flush.
    const clap_id sourceA = paramId(gg::core::layerSlot(gg::core::LayerParam::Source, 0));
    const auto freeze = gg::test::paramValue(paramId(ParamSlot::Freeze), 1.0);
    const auto live = gg::test::paramValue(sourceA, 1.0); // the instrument's is a Sample layer
    gg::test::InputEvents freezeOn;
    freezeOn.add(&freeze.header);
    freezeOn.add(&live.header);
    params->flush(plugin.clap(), freezeOn.list(), gg::test::discardingOutputEvents());
    for (int i = 0; i < 4; ++i)
    {
        plugin.process(512, inputs, outputs, none.list());
    }
    const std::string state = plugin.save();
    REQUIRE(plugin.load(state));
    REQUIRE(processor->sources(0).ringAdoptionPending());
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        plugin.process(512, inputs, outputs, none.list());
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    CHECK_FALSE(processor->sources(0).ringAdoptionPending());
    REQUIRE(plugin.load(state));
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        params->flush(plugin.clap(), none.list(), gg::test::discardingOutputEvents());
        params->flush(plugin.clap(), none.list(), gg::test::discardingOutputEvents());
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    CHECK(processor->sources(0).ringAdoptionPending()); // a flush adopts no ring
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        plugin.process(512, inputs, outputs, none.list());
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    CHECK_FALSE(processor->sources(0).ringAdoptionPending());
    plugin.onMainThread();

    // A command (the FX's layer A reads its ring; the instrument's, a Sample layer again, ignores
    // it), unfrozen first.
    const auto thaw = gg::test::paramValue(paramId(ParamSlot::Freeze), 0.0);
    const auto kind = gg::test::paramValue(sourceA, isFx ? 1.0 : 0.0);
    gg::test::InputEvents freezeOff;
    freezeOff.add(&thaw.header);
    freezeOff.add(&kind.header);
    params->flush(plugin.clap(), freezeOff.list(), gg::test::discardingOutputEvents());
    plugin.impl().uiCommand({0, gg::core::CommandKind::AddSplice, 0, 0.01f});
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        plugin.process(512, inputs, outputs, none.list());
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    CHECK(processor->engine(0).live().spliceCount() == (isFx ? 1u : 0u));
}

TEST_CASE("plugin process and flush take four loaders' offers at once without allocating",
          "[plugin][rt]")
{
    // M2 sub-project 2's spec §7: four loaders, one per layer, each load a file on its own worker
    // and offer its source on its layer's exchange; all four offers wait at once. A params.flush
    // and then a process() run armed while they do: neither allocates or frees, and the process()
    // call has every layer's sample playing.
    const auto dir = gg::test::useMediaScratch("rt-four-loaders");
    gg::test::PluginUnderTest plugin(kInstrumentId);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    const auto *params = paramsOf(plugin);
    auto *processor = plugin.impl().processor();
    auto &media = plugin.impl().media();

    // Layers B to D on, and every layer's Level up (the defaults), by a flush.
    const auto on = [](ParamSlot slot) { return gg::test::paramValue(paramId(slot), 1.0); };
    const std::array<clap_event_param_value, 3> layersOn{
        on(ParamSlot::LayerBOn), on(ParamSlot::LayerCOn), on(ParamSlot::LayerDOn)};
    gg::test::InputEvents layerEvents;
    for (const auto &e : layersOn)
    {
        layerEvents.add(&e.header);
    }
    params->flush(plugin.clap(), layerEvents.list(), gg::test::discardingOutputEvents());

    std::array<std::size_t, gg::core::kLayers> heldBefore{};
    for (uint8_t l = 0; l < gg::core::kLayers; ++l)
    {
        heldBefore[l] = processor->sources(l).held();
        const double hz = 220.0 * (l + 1);
        media.loader(l).requestLoad(gg::test::writeSineWav(
            dir / ("layer-" + std::to_string(l) + ".wav"), 48000, 1.0, hz, 0.5));
    }
    // The loaders work in parallel; each offers when the main thread applies its result.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    const auto loading = [&] {
        for (uint8_t l = 0; l < gg::core::kLayers; ++l)
        {
            if (media.loader(l).status().state == gg::plugin::SampleState::Loading)
            {
                return true;
            }
        }
        return false;
    };
    while (loading() && std::chrono::steady_clock::now() < deadline)
    {
        plugin.onMainThread();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (uint8_t l = 0; l < gg::core::kLayers; ++l)
    {
        INFO("layer " << int{l});
        REQUIRE(media.loader(l).status().state == gg::plugin::SampleState::Loaded);
        REQUIRE(processor->sources(l).held() == heldBefore[l] + 1); // offered, not yet taken
    }

    std::vector<float> outLeft(512), outRight(512);
    float *outputs[2] = {outLeft.data(), outRight.data()};
    const auto note = noteOn(60, 0);
    gg::test::InputEvents withNote;
    withNote.add(&note.header);
    gg::test::InputEvents none;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        params->flush(plugin.clap(), none.list(), gg::test::discardingOutputEvents());
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    clap_process_status status = CLAP_PROCESS_CONTINUE;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int i = 0; i < 8; ++i)
        {
            status = plugin.process(512, nullptr, outputs, i == 0 ? withNote.list() : none.list());
        }
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    CHECK(status == CLAP_PROCESS_CONTINUE);
    for (uint8_t l = 0; l < gg::core::kLayers; ++l)
    {
        INFO("layer " << int{l});
        CHECK(processor->engine(l).activeGrains() > 0); // each layer plays its sample
    }
    CHECK(*std::max_element(outLeft.begin(), outLeft.end()) > 0.0f);
    plugin.onMainThread(); // frees what the layers handed back
}
