// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <clap/clap.h>

#include "gg/core/Rng.h"
#include "gg/engine/Engine.h"
#include "PluginHarness.h"

namespace
{
constexpr const char *kInstrumentId = "org.sethbc.giga-grains";
constexpr const char *kFxId = "org.sethbc.giga-grains-fx";

// A stereo buffer: two channels of `frames` samples each.
struct Stereo
{
    explicit Stereo(uint32_t frames, float fill = 0.0f)
        : left(frames, fill), right(frames, fill), ptrs{left.data(), right.data()}
    {
    }
    Stereo(const Stereo &) = delete;
    Stereo &operator=(const Stereo &) = delete;

    float **data() noexcept { return ptrs; }

    std::vector<float> left;
    std::vector<float> right;
    float *ptrs[2];
};

// Deterministic noise in [-1, 1): never -0.0, so "bit-exact" is meaningful for x + 0.0f.
void fillNoise(Stereo &s, uint64_t seed)
{
    for (std::size_t i = 0; i < s.left.size(); ++i)
    {
        s.left[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(seed + 2 * i)));
        s.right[i] = static_cast<float>(gg::rng::bipolar(gg::rng::splitmix64(seed + 2 * i + 1)));
    }
}

bool sameBits(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

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

clap_event_note noteOn(int16_t key, uint32_t time)
{
    clap_event_note e{};
    e.header.size = sizeof(e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_NOTE_ON;
    e.note_id = -1;
    e.port_index = 0;
    e.channel = 0;
    e.key = key;
    e.velocity = 0.8;
    return e;
}

clap_event_midi midi(uint8_t status, uint8_t data1, uint8_t data2, uint32_t time)
{
    clap_event_midi e{};
    e.header.size = sizeof(e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_MIDI;
    e.port_index = 0;
    e.data[0] = status;
    e.data[1] = data1;
    e.data[2] = data2;
    return e;
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

TEST_CASE("fx passes audio through bit-exactly", "[plugin]")
{
    gg::test::PluginUnderTest fx(kFxId);
    REQUIRE(fx.valid());
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
    const auto midiOn = midi(0x90, 67, 100, 20);
    const auto midiOnVelocity0 = midi(0x90, 67, 0, 30); // a note-off in disguise
    const auto midiOnChannel5 = midi(0x95, 70, 1, 40);
    const auto midiOff = midi(0x80, 67, 64, 50);
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
    const auto midiOn = midi(0x90, 67, 100, 0);
    gg::test::InputEvents notes;
    notes.add(&clapOn.header);
    notes.add(&midiOn.header);
    params->flush(plugin.clap(), notes.list(), &out);
    CHECK(plugin.impl().noteOnCount.load() == 2);
    CHECK(pushed == 0);
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
    const auto midiOn = midi(0x90, 67, 100, 0);
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
