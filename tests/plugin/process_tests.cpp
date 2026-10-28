// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <numbers>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <clap/clap.h>

#include "gg/core/ParamIds.h"
#include "gg/engine/Engine.h"
#include "PluginHarness.h"

namespace
{
constexpr const char *kInstrumentId = "org.sethbc.giga-grains";
constexpr const char *kFxId = "org.sethbc.giga-grains-fx";

using gg::core::paramId;
using gg::core::ParamSlot;
using gg::test::fillNoise;
using gg::test::sameBits;
using gg::test::Stereo;

bool allZero(const std::vector<float> &v)
{
    for (float x : v)
    {
        if (x != 0.0f)
        {
            return false;
        }
    }
    return true;
}

// Sets Mix by params.flush: before activation it applies at activate(), after it at once.
void setMix(const gg::test::PluginUnderTest &plugin, double mix)
{
    const auto event = gg::test::paramValue(gg::core::paramId(gg::core::ParamSlot::Mix), mix);
    gg::test::InputEvents events;
    events.add(&event.header);
    plugin.flush(events.list());
}

bool allFinite(const std::vector<float> &v)
{
    for (float x : v)
    {
        if (!std::isfinite(x))
        {
            return false;
        }
    }
    return true;
}

clap_event_note noteOn(int16_t key, uint32_t time)
{
    return gg::test::noteEvent(CLAP_EVENT_NOTE_ON, 0, key, time);
}

// The keys held now, as ints so that failures print numbers.
std::vector<int> heldKeys(const gg::test::PluginUnderTest &plugin)
{
    const auto keys = plugin.impl().processor()->heldKeys();
    return {keys.begin(), keys.end()};
}

// The peak of channels left and right over the samples [from, to).
float peakIn(const Stereo &s, std::size_t from, std::size_t to)
{
    float p = 0.0f;
    for (std::size_t i = from; i < to; ++i)
    {
        p = std::max({p, std::abs(s.left[i]), std::abs(s.right[i])});
    }
    return p;
}

// Applies `events` (paramValue events) by params.flush.
template <std::size_t N>
void flushParams(const gg::test::PluginUnderTest &plugin,
                 const std::array<clap_event_param_value, N> &events)
{
    gg::test::InputEvents list;
    for (const auto &e : events)
    {
        list.add(&e.header);
    }
    plugin.flush(list.list());
}

// A stub host that offers only clap.log, and counts the host-misbehaviour reports it receives.
class LoggingHost
{
  public:
    LoggingHost() { host_.host_data = this; }
    LoggingHost(const LoggingHost &) = delete;
    LoggingHost &operator=(const LoggingHost &) = delete;

    const clap_host *clap() const noexcept { return &host_; }
    uint32_t hostMisbehaving() const noexcept { return hostMisbehaving_; }

  private:
    static void log(const clap_host *host, clap_log_severity severity, const char *) noexcept
    {
        if (severity == CLAP_LOG_HOST_MISBEHAVING)
        {
            ++static_cast<LoggingHost *>(host->host_data)->hostMisbehaving_;
        }
    }
    static const void *getExtension(const clap_host *, const char *id) noexcept
    {
        static const clap_host_log kLog{&LoggingHost::log};
        return std::strcmp(id, CLAP_EXT_LOG) == 0 ? &kLog : nullptr;
    }

    uint32_t hostMisbehaving_{0};
    clap_host host_{
        CLAP_VERSION_INIT,
        nullptr,
        "gg-tests logging host",
        "Giga Grains",
        "",
        "0.0.0",
        &LoggingHost::getExtension,
        [](const clap_host *) {}, // request_restart
        [](const clap_host *) {}, // request_process
        [](const clap_host *) {}, // request_callback
    };
};
} // namespace

TEST_CASE("fx passes audio through bit-exactly at mix 0", "[plugin]")
{
    // At Mix 0 the output is the dry input, bit for bit for normal floats (ruling M1-q); the
    // noise has no -0.0 and no subnormals.
    gg::test::PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
    setMix(fx, 0.0);
    REQUIRE(fx.activate(48000.0, 512));

    SECTION("separate input and output buffers")
    {
        Stereo in(512);
        fillNoise(in, 1);
        Stereo out(512, 7.0f);
        CHECK(fx.process(512, in.data(), out.data()) == CLAP_PROCESS_CONTINUE);
        CHECK(sameBits(out.left, in.left));
        CHECK(sameBits(out.right, in.right));
    }

    SECTION("input and output are the same buffers (in place)")
    {
        Stereo io(512);
        fillNoise(io, 2);
        const std::vector<float> left = io.left;
        const std::vector<float> right = io.right;
        CHECK(fx.process(512, io.data(), io.data()) == CLAP_PROCESS_CONTINUE);
        CHECK(sameBits(io.left, left));
        CHECK(sameBits(io.right, right));
    }

    SECTION("one-frame and oversized blocks")
    {
        for (uint32_t frames : {1u, 2048u})
        {
            Stereo in(frames);
            fillNoise(in, frames);
            Stereo out(frames, 7.0f);
            CHECK(fx.process(frames, in.data(), out.data()) == CLAP_PROCESS_CONTINUE);
            CHECK(sameBits(out.left, in.left));
            CHECK(sameBits(out.right, in.right));
        }
    }
}

TEST_CASE("instrument outputs silence", "[plugin]")
{
    gg::test::PluginUnderTest instrument(kInstrumentId);
    REQUIRE(instrument.valid());
    REQUIRE(instrument.activate(48000.0, 512));

    for (uint32_t frames : {512u, 1u, 2048u})
    {
        Stereo out(frames, 7.0f);
        CHECK(instrument.process(frames, nullptr, out.data()) == CLAP_PROCESS_CONTINUE);
        CHECK(allZero(out.left));
        CHECK(allZero(out.right));
    }
}

TEST_CASE("note-ons are counted from clap and midi", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));

    const auto clap1 = noteOn(60, 0);
    const auto clap2 = noteOn(64, 10);
    const auto midiOn = gg::test::midiEvent(0x90, 67, 100, 20);
    const auto midiOnVelocity0 = gg::test::midiEvent(0x90, 67, 0, 30); // a note-off in disguise
    const auto midiOnChannel5 = gg::test::midiEvent(0x95, 70, 1, 40);
    const auto midiOff = gg::test::midiEvent(0x80, 67, 64, 50);
    clap_event_note clapOff = noteOn(60, 60);
    clapOff.header.type = CLAP_EVENT_NOTE_OFF;

    gg::test::InputEvents events;
    events.add(&clap1.header);
    events.add(&clap2.header);
    events.add(&midiOn.header);
    events.add(&midiOnVelocity0.header);

    Stereo in(512);
    Stereo out(512);
    float **inputs = std::string_view(id) == kFxId ? in.data() : nullptr;

    REQUIRE(plugin.process(512, inputs, out.data(), events.list()) == CLAP_PROCESS_CONTINUE);
    CHECK(plugin.impl().noteOnCount.load() == 3);

    gg::test::InputEvents more;
    more.add(&midiOnChannel5.header);
    more.add(&midiOff.header);
    more.add(&clapOff.header);
    REQUIRE(plugin.process(512, inputs, out.data(), more.list()) == CLAP_PROCESS_CONTINUE);
    CHECK(plugin.impl().noteOnCount.load() == 4);
}

TEST_CASE("params flush is safe and counts note-ons", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const auto *params = static_cast<const clap_plugin_params *>(
        plugin.clap()->get_extension(plugin.clap(), CLAP_EXT_PARAMS));
    REQUIRE(params != nullptr);

    // With no parameters, a flush has nothing to report: count anything the plugin pushes.
    uint32_t pushed = 0;
    const clap_output_events out{&pushed,
                                 [](const clap_output_events *list, const clap_event_header *) {
                                     ++*static_cast<uint32_t *>(list->ctx);
                                     return true;
                                 }};
    gg::test::InputEvents none;

    // Inactive, flush is a main-thread call; active, an audio-thread one.
    params->flush(plugin.clap(), none.list(), &out);
    CHECK(plugin.impl().noteOnCount.load() == 0);
    REQUIRE(plugin.activate(48000.0, 512));
    params->flush(plugin.clap(), none.list(), &out);
    CHECK(plugin.impl().noteOnCount.load() == 0);
    CHECK(pushed == 0);

    // CLAP puts only parameter events in a flush, but a host may deliver notes there too.
    const auto clapOn = noteOn(60, 0);
    const auto midiOn = gg::test::midiEvent(0x90, 67, 100, 0);
    gg::test::InputEvents notes;
    notes.add(&clapOn.header);
    notes.add(&midiOn.header);
    params->flush(plugin.clap(), notes.list(), &out);
    CHECK(plugin.impl().noteOnCount.load() == 2);
    CHECK(pushed == 0);
    // They reach the Processor too, at the next grid boundary: here the next call's first sample.
    CHECK(plugin.impl().processor()->heldKeys().empty());
    REQUIRE(plugin.process(32, nullptr, nullptr) == CLAP_PROCESS_CONTINUE);
    CHECK(heldKeys(plugin) == std::vector<int>{60, 67});
}

TEST_CASE("params flush reports misbehaving hosts only in Debug builds", "[plugin]")
{
    // clap-helpers' Maximal checking reports each non-parameter event in a flush by building a
    // string and calling the host's log, on the audio thread. clap-wrapper's VST3 delivers notes
    // and MIDI there, so only Debug builds check at Maximal (ruling T8-e).
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    LoggingHost host;
    gg::test::PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    const auto *params = static_cast<const clap_plugin_params *>(
        plugin.clap()->get_extension(plugin.clap(), CLAP_EXT_PARAMS));
    REQUIRE(params != nullptr);

    const auto clapOn = noteOn(60, 0);
    const auto midiOn = gg::test::midiEvent(0x90, 67, 100, 0);
    gg::test::InputEvents notes;
    notes.add(&clapOn.header);
    notes.add(&midiOn.header);
    const uint32_t before = host.hostMisbehaving();
    params->flush(plugin.clap(), notes.list(), gg::test::discardingOutputEvents());
    CHECK(plugin.impl().noteOnCount.load() == 2);
#if defined(BUILD_IS_DEBUG)
    CHECK(gg::plugin::checkLevel == clap::helpers::CheckingLevel::Maximal);
    CHECK(host.hostMisbehaving() > before);
#else
    CHECK(gg::plugin::checkLevel == clap::helpers::CheckingLevel::Minimal);
    CHECK(host.hostMisbehaving() == before);
#endif
}

TEST_CASE("zero-frame process is safe", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));

    Stereo in(1, 0.5f);
    Stereo out(1, 7.0f);
    float **inputs = std::string_view(id) == kFxId ? in.data() : nullptr;
    CHECK(plugin.process(0, inputs, out.data()) == CLAP_PROCESS_CONTINUE);
    // Nothing is written outside the zero frames the host asked for.
    CHECK(out.left[0] == 7.0f);
    CHECK(out.right[0] == 7.0f);
}

TEST_CASE("reactivate at new sample rate", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const bool isFx = std::string_view(id) == kFxId;
    // Mix 0, set once while inactive, holds through the reactivation: the FX passes its input.
    setMix(plugin, 0.0);

    REQUIRE(plugin.activate(48000.0, 512));
    REQUIRE(plugin.impl().engine() != nullptr);
    CHECK(plugin.impl().engine()->sampleRate() == 48000.0);
    {
        Stereo in(512);
        fillNoise(in, 3);
        Stereo out(512, 7.0f);
        CHECK(plugin.process(512, isFx ? in.data() : nullptr, out.data()) == CLAP_PROCESS_CONTINUE);
    }

    plugin.deactivate();
    CHECK(plugin.impl().engine() == nullptr);

    REQUIRE(plugin.activate(96000.0, 1024));
    REQUIRE(plugin.impl().engine() != nullptr);
    CHECK(plugin.impl().engine()->sampleRate() == 96000.0);
    CHECK(plugin.impl().engine()->sampleClock() == 0);
    {
        Stereo in(1024);
        fillNoise(in, 4);
        Stereo out(1024, 7.0f);
        CHECK(plugin.process(1024, isFx ? in.data() : nullptr, out.data()) ==
              CLAP_PROCESS_CONTINUE);
        if (isFx)
        {
            CHECK(sameBits(out.left, in.left));
            CHECK(sameBits(out.right, in.right));
        }
        else
        {
            CHECK(allZero(out.left));
            CHECK(allZero(out.right));
        }
    }
    CHECK(plugin.impl().engine()->sampleClock() == 1024);
}

TEST_CASE("fx activates with a huge max frame count", "[plugin]")
{
    // CLAP allows a max frame count up to INT32_MAX. The live ring holds N + maxBlockSize frames
    // (ruling M1-o), so the plugin caps the engine's block size; the output doesn't depend on it.
    // Position 0.05 (about 20 ms) and no Spray: the grains read what this render wrote.
    Stereo in(8193);
    fillNoise(in, 5);
    const auto position =
        gg::test::paramValue(gg::core::paramId(gg::core::ParamSlot::Position), 0.05);
    const auto spray = gg::test::paramValue(gg::core::paramId(gg::core::ParamSlot::Spray), 0.0);
    gg::test::InputEvents params;
    params.add(&position.header);
    params.add(&spray.header);
    const auto render = [&](uint32_t maxFrames, Stereo &out) {
        gg::test::PluginUnderTest fx(kFxId);
        REQUIRE(fx.valid());
        fx.flush(params.list());
        REQUIRE(fx.activate(48000.0, maxFrames));
        CHECK(fx.impl().engineBlockSize() == std::min<uint32_t>(maxFrames, 4096));
        float *inPtrs[2] = {in.left.data(), in.right.data()};
        float *outPtrs[2] = {out.left.data(), out.right.data()};
        CHECK(fx.process(8192, inPtrs, outPtrs) == CLAP_PROCESS_CONTINUE);
        inPtrs[0] += 8192;
        inPtrs[1] += 8192;
        outPtrs[0] += 8192;
        outPtrs[1] += 8192;
        CHECK(fx.process(1, inPtrs, outPtrs) == CLAP_PROCESS_CONTINUE);
        CHECK(fx.impl().engine()->sampleClock() == 8193);
    };

    Stereo huge(8193, 7.0f);
    render(static_cast<uint32_t>(INT32_MAX), huge);
    CHECK(allFinite(huge.left));
    CHECK(allFinite(huge.right));
    // The wet signal is there: the output isn't the dry half alone.
    float wet = 0.0f;
    for (std::size_t i = 0; i < huge.left.size(); ++i)
    {
        wet = std::max(wet, std::abs(huge.left[i] - 0.5f * in.left[i]));
    }
    CHECK(wet > 0.01f);

    Stereo small(8193, 7.0f);
    render(64, small);
    CHECK(sameBits(huge.left, small.left));
    CHECK(sameBits(huge.right, small.right));
}

TEST_CASE("a note freezes the fx and releasing it lets the input through again", "[plugin]")
{
    // Spec §6.3, D4: a note-on freezes the live ring, so the grains keep playing what the input
    // played before it, even once the input is silent; releasing the note lets the ring follow
    // the (now silent) input again. Mix 1, so the output is the wet path alone; Spray 0 and
    // Position 0.05 (about 20 ms behind the head), so the grains read just behind it; Density 64
    // Hz; Size stays at its 150 ms default. The same render without the note is the control.
    constexpr uint32_t kBlock = 512;
    constexpr std::size_t kOnBlock = 48;   // 0.512 s; the input is silent from here on
    constexpr std::size_t kOffBlock = 144; // 1.536 s
    constexpr std::size_t kBlocks = 240;   // 2.56 s
    constexpr std::size_t kOn = kOnBlock * kBlock;
    constexpr std::size_t kOff = kOffBlock * kBlock;
    constexpr std::size_t kEnd = kBlocks * kBlock;
    // Size + 20 ms at 48 kHz: 150 ms + 20 ms.
    constexpr std::size_t kRelease = 8160;

    const auto render = [&](bool withNote, Stereo &out, int64_t &frozenHead,
                            std::vector<int> &frozenKeys) {
        gg::test::PluginUnderTest fx(kFxId);
        REQUIRE(fx.valid());
        flushParams<4>(fx, {
                               gg::test::paramValue(paramId(ParamSlot::Mix), 1.0),
                               gg::test::paramValue(paramId(ParamSlot::Spray), 0.0),
                               gg::test::paramValue(paramId(ParamSlot::Position), 0.05),
                               gg::test::paramValue(paramId(ParamSlot::Density), 6.0),
                           });
        REQUIRE(fx.activate(48000.0, kBlock));
        const auto on = noteOn(60, 0);
        const auto off = gg::test::noteEvent(CLAP_EVENT_NOTE_OFF, 0, 60, 0);
        Stereo in(kBlock);
        Stereo block(kBlock);
        for (std::size_t k = 0; k < kBlocks; ++k)
        {
            for (uint32_t i = 0; i < kBlock; ++i)
            {
                const auto t = static_cast<double>(k * kBlock + i) / 48000.0;
                const auto x =
                    k < kOnBlock
                        ? static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 330.0 * t))
                        : 0.0f;
                in.left[i] = x;
                in.right[i] = x;
            }
            gg::test::InputEvents events;
            if (withNote && k == kOnBlock)
            {
                events.add(&on.header);
            }
            if (withNote && k == kOffBlock)
            {
                events.add(&off.header);
            }
            REQUIRE(fx.process(kBlock, in.data(), block.data(), events.list()) ==
                    CLAP_PROCESS_CONTINUE);
            std::copy(block.left.begin(), block.left.end(),
                      out.left.begin() + static_cast<std::ptrdiff_t>(k * kBlock));
            std::copy(block.right.begin(), block.right.end(),
                      out.right.begin() + static_cast<std::ptrdiff_t>(k * kBlock));
            if (k == kOffBlock - 1)
            {
                frozenHead = fx.impl().engine()->liveWriteHead();
                frozenKeys = heldKeys(fx);
            }
        }
        CHECK(heldKeys(fx).empty());
    };

    Stereo noted(static_cast<uint32_t>(kEnd));
    Stereo control(static_cast<uint32_t>(kEnd));
    int64_t notedHead = 0;
    int64_t controlHead = 0;
    std::vector<int> notedKeys;
    std::vector<int> controlKeys;
    render(true, noted, notedHead, notedKeys);
    render(false, control, controlHead, controlKeys);

    // Before the note the two are the same live texture, and it sounds.
    CHECK(std::equal(noted.left.begin(), noted.left.begin() + kOn, control.left.begin()));
    CHECK(std::equal(noted.right.begin(), noted.right.begin() + kOn, control.right.begin()));
    CHECK(peakIn(noted, kOn / 2, kOn) > 0.05f);

    // The note held the ring: its head stopped 20 ms (960 samples) after the note's boundary,
    // and the grains still play the sine, long after the input went silent. Without the note
    // the ring follows the input, and the wet path is silent once the last sine grain is over.
    CHECK(notedKeys == std::vector<int>{60});
    CHECK(notedHead == static_cast<int64_t>(kOn) + 960);
    CHECK(controlKeys.empty());
    CHECK(controlHead == static_cast<int64_t>(kOff));
    CHECK(peakIn(noted, kOn + 24000, kOff) > 0.05f);
    CHECK(peakIn(control, kOn + 24000, kOff) == 0.0f);

    // The note-off unfreezes it: silence returns to the wet path within Size + 20 ms.
    CHECK(peakIn(noted, kOff + kRelease, kEnd) == 0.0f);
}

TEST_CASE("midi notes play the instrument's sample and release it", "[plugin]")
{
    // Spec §6.2, D1: the instrument is silent until a note, plays its source while one is held,
    // and releases within Size (150 ms by default) + 20 ms once none is. MIDI dialect: a
    // note-on with velocity 0 is a note-off, and CC 123 (all notes off) releases its own channel's
    // notes alone (ruling R14).
    constexpr uint32_t kBlock = 512;
    constexpr std::size_t kRelease = 8160; // Size + 20 ms at 48 kHz
    gg::test::PluginUnderTest instrument(kInstrumentId);
    REQUIRE(instrument.valid());
    REQUIRE(instrument.loadSampleState(gg::test::sineSampleFile("midi-notes")));
    REQUIRE(instrument.activate(48000.0, kBlock));

    const auto on60 = gg::test::midiEvent(0x90, 60, 100, 0);
    const auto off60 = gg::test::midiEvent(0x90, 60, 0, 0);
    const auto on64 = gg::test::midiEvent(0x90, 64, 100, 0);
    const auto on67Channel3 = gg::test::midiEvent(0x93, 67, 100, 0);
    const auto allOffChannel0 = gg::test::midiEvent(0xB0, 123, 0, 0);
    const auto allOffChannel3 = gg::test::midiEvent(0xB3, 123, 0, 0);

    // Renders kBlocks blocks into out, `first` the first block's events.
    constexpr std::size_t kBlocks = 40; // about 0.43 s
    constexpr std::size_t kLength = kBlocks * kBlock;
    Stereo out(static_cast<uint32_t>(kLength));
    const auto run = [&](std::initializer_list<const clap_event_header *> first) {
        Stereo block(kBlock);
        for (std::size_t k = 0; k < kBlocks; ++k)
        {
            gg::test::InputEvents events;
            for (const auto *e :
                 k == 0 ? first : std::initializer_list<const clap_event_header *>{})
            {
                events.add(e);
            }
            REQUIRE(instrument.process(kBlock, nullptr, block.data(), events.list()) ==
                    CLAP_PROCESS_CONTINUE);
            std::copy(block.left.begin(), block.left.end(),
                      out.left.begin() + static_cast<std::ptrdiff_t>(k * kBlock));
            std::copy(block.right.begin(), block.right.end(),
                      out.right.begin() + static_cast<std::ptrdiff_t>(k * kBlock));
        }
    };

    run({});
    CHECK(peakIn(out, 0, kLength) == 0.0f);

    run({&on60.header});
    CHECK(heldKeys(instrument) == std::vector<int>{60});
    CHECK(peakIn(out, 0, kLength / 2) > 0.05f);
    CHECK(peakIn(out, kLength / 2, kLength) > 0.05f);

    run({&off60.header});
    CHECK(heldKeys(instrument).empty());
    CHECK(peakIn(out, 0, kRelease) > 0.0f); // the tail of the grains still sounding
    CHECK(peakIn(out, kRelease, kLength) == 0.0f);

    run({&on64.header, &on67Channel3.header});
    CHECK(heldKeys(instrument) == std::vector<int>{64, 67});
    CHECK(peakIn(out, kLength / 2, kLength) > 0.05f);

    // CC 123 on channel 0 releases 64 alone: 67, on channel 3, still sounds.
    run({&allOffChannel0.header});
    CHECK(heldKeys(instrument) == std::vector<int>{67});
    CHECK(peakIn(out, kLength / 2, kLength) > 0.05f);

    run({&allOffChannel3.header});
    CHECK(heldKeys(instrument).empty());
    CHECK(peakIn(out, kRelease, kLength) == 0.0f);
}

TEST_CASE("a clap note-off by note id releases that note alone", "[plugin]")
{
    // Ruling R15: CLAP lets a note-off or choke with channel and key −1 address one voice by its
    // note_id. A known ID releases that note alone; an unknown one releases everything, which is
    // safer than a stuck note. Both products.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));

    // A note event with an ID: on (channel, key), or off and choke with wildcards.
    const auto withId = [](uint16_t type, int16_t channel, int16_t key, int32_t noteId) {
        auto e = gg::test::noteEvent(type, channel, key);
        e.note_id = noteId;
        return e;
    };
    const auto call = [&](std::initializer_list<const clap_event_header *> notes) {
        gg::test::InputEvents events;
        for (const auto *e : notes)
        {
            events.add(e);
        }
        REQUIRE(plugin.process(64, nullptr, nullptr, events.list()) == CLAP_PROCESS_CONTINUE);
        return heldKeys(plugin);
    };
    const auto on60 = withId(CLAP_EVENT_NOTE_ON, 0, 60, 100);
    const auto on64 = withId(CLAP_EVENT_NOTE_ON, 1, 64, 101);
    const auto on67 = withId(CLAP_EVENT_NOTE_ON, 0, 67, 102);
    const auto offById64 = withId(CLAP_EVENT_NOTE_OFF, -1, -1, 101);
    const auto chokeById60 = withId(CLAP_EVENT_NOTE_CHOKE, -1, -1, 100);
    const auto offById64Again = withId(CLAP_EVENT_NOTE_OFF, -1, -1, 101); // forgotten by now
    const auto offWithKey = withId(CLAP_EVENT_NOTE_OFF, 0, 67, 102);
    const auto offUnknownId = withId(CLAP_EVENT_NOTE_OFF, -1, -1, 999);
    const auto offAnyKeyChannel0 = withId(CLAP_EVENT_NOTE_OFF, 0, -1, 100);

    CHECK(call({&on60.header, &on64.header, &on67.header}) == std::vector<int>{60, 64, 67});
    CHECK(call({&offById64.header}) == std::vector<int>{60, 67});
    CHECK(call({&chokeById60.header}) == std::vector<int>{67});
    // Fields given explicitly keep their meaning; the ID is forgotten.
    CHECK(call({&offWithKey.header}) == std::vector<int>{});

    // An unknown ID, or one already released, releases the whole chord.
    CHECK(call({&on60.header, &on64.header, &on67.header}) == std::vector<int>{60, 64, 67});
    CHECK(call({&offUnknownId.header}) == std::vector<int>{});
    CHECK(call({&on60.header, &on64.header, &on67.header}) == std::vector<int>{60, 64, 67});
    CHECK(call({&offById64.header}) == std::vector<int>{60, 67});
    CHECK(call({&offById64Again.header}) == std::vector<int>{});

    // Any other wildcard keeps its meaning, ID or not: channel 0, any key.
    CHECK(call({&on60.header, &on64.header, &on67.header}) == std::vector<int>{60, 64, 67});
    CHECK(call({&offAnyKeyChannel0.header}) == std::vector<int>{64});

    // reset() releases every note and forgets every ID: ID 101 (key 64 on channel 1) is unknown
    // after it, so its note-off releases everything, key 60 included.
    plugin.clap()->reset(plugin.clap());
    CHECK(call({&on60.header}) == std::vector<int>{60});
    CHECK(call({&offById64.header}) == std::vector<int>{});
}

TEST_CASE("notes render the same at any host block size", "[plugin][determinism]")
{
    // Note timing is on the control grid (spec §6.1), so the plugin's output with notes doesn't
    // depend on how the host splits blocks (INTENT §8.6), a 0-frame call included.
    struct Timed
    {
        std::size_t time; // absolute sample
        clap_event_midi event;
    };
    const std::vector<Timed> notes{
        {1000, gg::test::midiEvent(0x90, 60, 100)}, {5003, gg::test::midiEvent(0x91, 67, 100)},
        {12000, gg::test::midiEvent(0x80, 60, 0)},  {16001, gg::test::midiEvent(0x92, 55, 90)},
        {20000, gg::test::midiEvent(0xB0, 123, 0)},
    };
    constexpr std::size_t kFrames = 30000;

    const auto render = [&](const std::vector<uint32_t> &blockSizes, Stereo &out) {
        gg::test::PluginUnderTest instrument(kInstrumentId);
        REQUIRE(instrument.valid());
        REQUIRE(instrument.loadSampleState(gg::test::sineSampleFile("notes-block-size")));
        REQUIRE(instrument.activate(48000.0, 2048));
        std::vector<clap_event_midi> events;
        events.reserve(notes.size());
        std::size_t next = 0;
        std::size_t done = 0;
        for (std::size_t call = 0; done < kFrames; ++call)
        {
            const auto n = static_cast<uint32_t>(
                std::min<std::size_t>(blockSizes[call % blockSizes.size()], kFrames - done));
            gg::test::InputEvents list;
            for (; next < notes.size() && notes[next].time < done + std::max<uint32_t>(n, 1);
                 ++next)
            {
                events.push_back(notes[next].event);
                events.back().header.time = static_cast<uint32_t>(notes[next].time - done);
                list.add(&events.back().header);
            }
            float *outputs[2] = {out.left.data() + done, out.right.data() + done};
            REQUIRE(instrument.process(n, nullptr, outputs, list.list()) == CLAP_PROCESS_CONTINUE);
            done += n;
        }
    };

    Stereo regular(static_cast<uint32_t>(kFrames));
    Stereo irregular(static_cast<uint32_t>(kFrames));
    render({512}, regular);
    render({1, 37, 300, 0, 2048, 64}, irregular);
    CHECK(peakIn(regular, 0, kFrames) > 0.05f);
    CHECK(sameBits(regular.left, irregular.left));
    CHECK(sameBits(regular.right, irregular.right));
}
