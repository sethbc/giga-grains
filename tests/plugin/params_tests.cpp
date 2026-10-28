// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The plugins' parameters, state and remote controls (spec §4, §5), through the clap_plugin
// vtables a host calls: parameter info, values and text; params.flush; modulation; state save
// and load, inactive and active; the FX's unusual wiring (Review Focus 5); the remote-controls
// page; and the latest-wins hand-off that carries a state loaded while active.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <clap/clap.h>

#include "LatestValue.h"
#include "PluginHarness.h"
#include "gg/core/ParamIds.h"
#include "gg/core/PatchState.h"
#include "gg/core/Processor.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

using gg::core::kParamCount;
using gg::core::paramId;
using gg::core::ParamSlot;
using gg::core::ParamValues;
using gg::test::fillNoise;
using gg::test::InputEvents;
using gg::test::PluginUnderTest;
using gg::test::sameBits;
using gg::test::Stereo;

namespace
{
constexpr const char *kInstrumentId = "org.sethbc.giga-grains";
constexpr const char *kFxId = "org.sethbc.giga-grains-fx";

std::size_t at(ParamSlot s) { return static_cast<std::size_t>(s); }

// Global Constraints' parameter table, spelled out: the IDs are frozen at the first public
// release (INTENT §18), append-only, and the order is the AUv2 parameter order.
struct ExpectedParam
{
    clap_id id;
    const char *name;
    float min;
    float max;
    float def;
};
constexpr std::array<ExpectedParam, kParamCount> kExpected{{
    {0x000001, "Mix", 0.0f, 1.0f, 0.5f},
    {0x000002, "Level", -60.0f, 12.0f, 0.0f},
    {0x010001, "Position", 0.0f, 1.0f, 0.28967658f},
    {0x010002, "Size", -7.643856189774724f, 1.0f, -2.736965594166206f},
    {0x010003, "Density", -2.0f, 8.643856189774725f, 4.0f},
    {0x010004, "Distribution", 0.0f, 1.0f, 0.5f},
    {0x010005, "Pitch", -24.0f, 24.0f, 0.0f},
    {0x010006, "Spray", 0.0f, 1.0f, 0.36033038f},
    {0x010007, "Stereo", 0.0f, 1.0f, 0.5f},
    {0x010008, "Freeze", 0.0f, 1.0f, 0.0f},
}};
// The instrument's Position is 0–100% of the loaded file, 0% by default; the rest of its table
// is the FX's above (spec §6.8).
constexpr float kInstrumentPositionDefault = 0.0f;

ParamValues defaults()
{
    ParamValues v{};
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        v[s] = kExpected[s].def;
    }
    return v;
}

// A host that offers clap.params, and counts value rescans and restart requests.
class ParamsHost
{
  public:
    ParamsHost() { host_.host_data = this; }
    ParamsHost(const ParamsHost &) = delete;
    ParamsHost &operator=(const ParamsHost &) = delete;

    const clap_host *clap() const noexcept { return &host_; }
    uint32_t valueRescans() const noexcept { return valueRescans_; }
    uint32_t otherRescans() const noexcept { return otherRescans_; }
    uint32_t restarts() const noexcept { return restarts_; }

  private:
    static ParamsHost &self(const clap_host *host)
    {
        return *static_cast<ParamsHost *>(host->host_data);
    }
    static void rescan(const clap_host *host, clap_param_rescan_flags flags) noexcept
    {
        ++(flags == CLAP_PARAM_RESCAN_VALUES ? self(host).valueRescans_ : self(host).otherRescans_);
    }
    static const void *getExtension(const clap_host *, const char *id) noexcept
    {
        static const clap_host_params kParams{
            &ParamsHost::rescan,
            [](const clap_host *, clap_id, clap_param_clear_flags) {},
            [](const clap_host *) {}, // request_flush
        };
        return std::strcmp(id, CLAP_EXT_PARAMS) == 0 ? &kParams : nullptr;
    }

    uint32_t valueRescans_{0};
    uint32_t otherRescans_{0};
    uint32_t restarts_{0};
    clap_host host_{
        CLAP_VERSION_INIT,
        nullptr,
        "gg-tests params host",
        "Giga Grains",
        "",
        "0.0.0",
        &ParamsHost::getExtension,
        [](const clap_host *host) { ++self(host).restarts_; }, // request_restart
        [](const clap_host *) {},                              // request_process
        [](const clap_host *) {},                              // request_callback
    };
};

// One params.flush of PARAM_VALUE events.
void flushValues(const PluginUnderTest &plugin,
                 std::initializer_list<std::pair<ParamSlot, double>> values)
{
    std::vector<clap_event_param_value> storage;
    storage.reserve(values.size());
    InputEvents events;
    for (const auto &[slot, value] : values)
    {
        storage.push_back(gg::test::paramValue(paramId(slot), value));
        events.add(&storage.back().header);
    }
    plugin.flush(events.list());
}

// Renders `in` in blocks of `block` frames into `out`, with no events.
void renderBlocks(PluginUnderTest &plugin, Stereo &in, Stereo &out, uint32_t block)
{
    const auto frames = static_cast<uint32_t>(in.left.size());
    for (uint32_t done = 0; done < frames; done += block)
    {
        const uint32_t n = std::min(block, frames - done);
        float *inPtrs[2] = {in.left.data() + done, in.right.data() + done};
        float *outPtrs[2] = {out.left.data() + done, out.right.data() + done};
        REQUIRE(plugin.process(n, inPtrs, outPtrs) == CLAP_PROCESS_CONTINUE);
    }
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

std::string valueToText(const PluginUnderTest &plugin, clap_id id, double value)
{
    char buffer[CLAP_NAME_SIZE] = {};
    if (!plugin.params()->value_to_text(plugin.clap(), id, value, buffer, sizeof(buffer)))
    {
        return "<failed>";
    }
    return buffer;
}
} // namespace

TEST_CASE("plugin exposes the ten params", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const bool isFx = std::string_view(id) == kFxId;
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const auto *params = plugin.params();
    REQUIRE(params != nullptr);
    REQUIRE(params->count(plugin.clap()) == kParamCount);

    for (uint32_t i = 0; i < kParamCount; ++i)
    {
        CAPTURE(i);
        clap_param_info info;
        std::memset(&info, 0xA5, sizeof(info)); // every field must be written
        REQUIRE(params->get_info(plugin.clap(), i, &info));
        const auto &expected = kExpected[i];
        CHECK(info.id == expected.id);
        CHECK(std::string_view(info.name) == expected.name);
        CHECK(std::string_view(info.module).empty());
        CHECK(info.cookie == nullptr);
        CHECK(info.min_value == static_cast<double>(expected.min));
        CHECK(info.max_value == static_cast<double>(expected.max));
        const float def =
            i == at(ParamSlot::Position) && !isFx ? kInstrumentPositionDefault : expected.def;
        CHECK(info.default_value == static_cast<double>(def));

        // Global Constraints' CLAP flags: every parameter is automatable and modulatable,
        // Freeze is stepped, and the instrument hides Mix, which has no dry signal to mix.
        clap_param_info_flags flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
        if (i == at(ParamSlot::Freeze))
        {
            flags |= CLAP_PARAM_IS_STEPPED;
        }
        if (i == at(ParamSlot::Mix) && !isFx)
        {
            flags |= CLAP_PARAM_IS_HIDDEN;
        }
        CHECK(info.flags == flags);

        // A new instance holds the defaults.
        CHECK(plugin.value(expected.id) == static_cast<double>(def));
    }
}

TEST_CASE("value and text round trip through clap", "[plugin]")
{
    PluginUnderTest plugin(kFxId);
    REQUIRE(plugin.valid());
    const auto *params = plugin.params();
    REQUIRE(params != nullptr);

    SECTION("every parameter, at its bounds, default and in between")
    {
        for (std::size_t s = 0; s < kParamCount; ++s)
        {
            const auto &p = kExpected[s];
            const double range = static_cast<double>(p.max) - p.min;
            for (const double v :
                 {static_cast<double>(p.min), static_cast<double>(p.def),
                  static_cast<double>(p.max), p.min + 0.3 * range, p.min + 0.71 * range})
            {
                CAPTURE(p.name, v);
                const std::string text = valueToText(plugin, p.id, v);
                CAPTURE(text);
                REQUIRE(text != "<failed>");
                REQUIRE(!text.empty());

                double back = std::numeric_limits<double>::quiet_NaN();
                REQUIRE(params->text_to_value(plugin.clap(), p.id, text.c_str(), &back));
                CAPTURE(back);
                CHECK(back >= p.min);
                CHECK(back <= p.max);
                // The text has the display's precision, so the value comes back within a
                // display step (Spray shows 10 ms steps, 1.6% of its range near 30 ms), and
                // gives the same text.
                if (s == at(ParamSlot::Freeze))
                {
                    CHECK(back == (v > 0.5 ? 1.0 : 0.0));
                }
                else
                {
                    CHECK(std::abs(back - v) <= 0.05 * range);
                }
                CHECK(valueToText(plugin, p.id, back) == text);
            }
        }
    }

    SECTION("the display follows the metadata")
    {
        CHECK(valueToText(plugin, paramId(ParamSlot::Mix), 0.5) ==
              gg::core::paramMeta(ParamSlot::Mix, gg::core::Mode::Fx)
                  .valueToString(0.5f)
                  .value_or("?"));
        CHECK(valueToText(plugin, paramId(ParamSlot::Freeze), 1.0) ==
              gg::core::paramMeta(ParamSlot::Freeze, gg::core::Mode::Fx)
                  .valueToString(1.0f)
                  .value_or("?"));
        CHECK(valueToText(plugin, paramId(ParamSlot::Density), 4.0).find("Hz") !=
              std::string::npos);
        CHECK(valueToText(plugin, paramId(ParamSlot::Size), -2.736965594166206).find("ms") !=
              std::string::npos);
    }

    SECTION("unparseable text fails")
    {
        double value = 0.0;
        CHECK_FALSE(params->text_to_value(plugin.clap(), paramId(ParamSlot::Density),
                                          "not a number", &value));
    }
}

TEST_CASE("position is per product", "[plugin]")
{
    const clap_id position = paramId(ParamSlot::Position);
    constexpr auto kPositionIndex = static_cast<uint32_t>(ParamSlot::Position);
    clap_param_info info{};

    SECTION("the instrument's is a percentage of the file, 0 by default")
    {
        ParamsHost host;
        PluginUnderTest plugin(kInstrumentId, host.clap());
        REQUIRE(plugin.valid());
        REQUIRE(plugin.params()->get_info(plugin.clap(), kPositionIndex, &info));
        CHECK(info.default_value == 0.0);
        CHECK(info.min_value == 0.0);
        CHECK(info.max_value == 1.0);
        CHECK(plugin.value(position) == 0.0);

        const std::string text = valueToText(plugin, position, 0.25);
        CHECK(text == gg::core::paramMeta(ParamSlot::Position, gg::core::Mode::Instrument)
                          .valueToString(0.25f)
                          .value_or("?"));
        CHECK(text.find('%') != std::string::npos);
        double back = -1.0;
        REQUIRE(plugin.params()->text_to_value(plugin.clap(), position, text.c_str(), &back));
        CHECK(std::abs(back - 0.25) < 1e-6);

        // A state that doesn't mention Position loads the instrument's default.
        flushValues(plugin, {{ParamSlot::Position, 0.6}});
        REQUIRE(plugin.value(position) == static_cast<double>(0.6f));
        std::string state = plugin.save();
        const auto from = state.find(R"(<p id="65537" )");
        REQUIRE(from != std::string::npos);
        state.erase(from, state.find("/>", from) + 2 - from);
        REQUIRE(plugin.load(state));
        CHECK(plugin.value(position) == 0.0);
    }

    SECTION("the FX's is unchanged: seconds behind the write head, 0.25 s by default")
    {
        ParamsHost host;
        PluginUnderTest plugin(kFxId, host.clap());
        REQUIRE(plugin.valid());
        REQUIRE(plugin.params()->get_info(plugin.clap(), kPositionIndex, &info));
        CHECK(info.default_value == static_cast<double>(0.28967658f));
        CHECK(plugin.value(position) == static_cast<double>(0.28967658f));
        CHECK(valueToText(plugin, position, static_cast<double>(0.28967658f)) == "0.25 s");
        CHECK(valueToText(plugin, position, 0.25).find('%') == std::string::npos);

        flushValues(plugin, {{ParamSlot::Position, 0.6}});
        std::string state = plugin.save();
        const auto from = state.find(R"(<p id="65537" )");
        REQUIRE(from != std::string::npos);
        state.erase(from, state.find("/>", from) + 2 - from);
        REQUIRE(plugin.load(state));
        CHECK(plugin.value(position) == static_cast<double>(0.28967658f));
    }
}

TEST_CASE("flush applies a param change", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const clap_id density = paramId(ParamSlot::Density);
    const clap_id pitch = paramId(ParamSlot::Pitch);
    const clap_id level = paramId(ParamSlot::Level);

    // Inactive (main thread): the value is kept, and applied at activate().
    flushValues(plugin, {{ParamSlot::Density, 6.5}});
    CHECK(plugin.value(density) == 6.5);
    REQUIRE(plugin.activate(48000.0, 512));
    REQUIRE(plugin.impl().processor() != nullptr);
    CHECK(plugin.impl().processor()->base(ParamSlot::Density) == 6.5f);

    // Active (audio thread, between process calls): it applies at once.
    flushValues(plugin, {{ParamSlot::Pitch, -12.0}});
    CHECK(plugin.value(pitch) == -12.0);
    CHECK(plugin.impl().processor()->base(ParamSlot::Pitch) == -12.0f);

    // Values are floats end to end: a double is converted on receipt.
    flushValues(plugin, {{ParamSlot::Density, 0.1}});
    CHECK(plugin.value(density) == static_cast<double>(0.1f));
    CHECK(plugin.impl().processor()->base(ParamSlot::Density) == 0.1f);

    // Out-of-range values clamp; NaN and unknown IDs are ignored.
    flushValues(plugin, {{ParamSlot::Level, 100.0}});
    CHECK(plugin.value(level) == 12.0);
    {
        const auto nan = gg::test::paramValue(density, std::numeric_limits<double>::quiet_NaN());
        const auto unknown = gg::test::paramValue(0x7FFFFF, 0.5);
        InputEvents events;
        events.add(&nan.header);
        events.add(&unknown.header);
        plugin.flush(events.list());
    }
    CHECK(plugin.value(density) == static_cast<double>(0.1f));
    CHECK(plugin.impl().processor()->base(ParamSlot::Density) == 0.1f);

    // Modulation in a flush joins the control grid: it applies at the next boundary, here the
    // next process call's first sample, and leaves the base alone.
    {
        const auto mod = gg::test::paramMod(pitch, 5.0);
        InputEvents events;
        events.add(&mod.header);
        plugin.flush(events.list());
        Stereo in(32);
        Stereo out(32);
        REQUIRE(plugin.process(32, std::string_view(id) == kFxId ? in.data() : nullptr,
                               out.data()) == CLAP_PROCESS_CONTINUE);
    }
    CHECK(plugin.impl().processor()->effective(ParamSlot::Pitch) == -7.0f);
    CHECK(plugin.impl().processor()->base(ParamSlot::Pitch) == -12.0f);
    CHECK(plugin.value(pitch) == -12.0);

    // The values outlive the activation.
    plugin.deactivate();
    CHECK(plugin.value(density) == static_cast<double>(0.1f));
    CHECK(plugin.value(pitch) == -12.0);
    REQUIRE(plugin.activate(96000.0, 512));
    CHECK(plugin.impl().processor()->base(ParamSlot::Pitch) == -12.0f);
    CHECK(plugin.impl().processor()->effective(ParamSlot::Pitch) == -12.0f); // no modulation
}

TEST_CASE("the host reads a value from process at once", "[plugin]")
{
    // A value in a block's last 31 samples waits for the next grid boundary, in the next call
    // when the block ends on the grid (ruling M1-u), but the host reads it as soon as the block
    // that carried it is processed.
    PluginUnderTest plugin(kFxId);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    const auto density = gg::test::paramValue(paramId(ParamSlot::Density), 6.0, 500);
    InputEvents events;
    events.add(&density.header);
    Stereo in(512);
    Stereo out(512);
    REQUIRE(plugin.process(512, in.data(), out.data(), events.list()) == CLAP_PROCESS_CONTINUE);
    CHECK(plugin.value(paramId(ParamSlot::Density)) == 6.0);
    CHECK(plugin.impl().processor()->base(ParamSlot::Density) == 4.0f); // at sample 512
    REQUIRE(plugin.process(32, in.data(), out.data()) == CLAP_PROCESS_CONTINUE);
    CHECK(plugin.impl().processor()->base(ParamSlot::Density) == 6.0f);
}

TEST_CASE("modulation offsets do not change the base", "[plugin]")
{
    // Two FX instances on the same noise: B's Mix is modulated by -0.5 from sample 0, so after
    // the 10 ms ramp (480 samples) B outputs its dry input, while A mixes in its wet signal.
    // B's Mix value, as the host reads it, stays at the base 0.5.
    constexpr uint32_t kFrames = 2048;
    constexpr uint32_t kRamp = 480;
    Stereo in(kFrames);
    fillNoise(in, 11);
    const clap_id mix = paramId(ParamSlot::Mix);

    PluginUnderTest a(kFxId);
    PluginUnderTest b(kFxId);
    REQUIRE(a.valid());
    REQUIRE(b.valid());
    // Grains read about 20 ms behind the write head, so the wet signal is there from early on.
    for (auto *p : {&a, &b})
    {
        flushValues(*p, {{ParamSlot::Position, 0.05}, {ParamSlot::Spray, 0.0}});
        REQUIRE(p->activate(48000.0, 512));
    }

    Stereo outA(kFrames);
    Stereo outB(kFrames);
    REQUIRE(a.process(kFrames, in.data(), outA.data()) == CLAP_PROCESS_CONTINUE);
    const auto mod = gg::test::paramMod(mix, -0.5);
    InputEvents events;
    events.add(&mod.header);
    REQUIRE(b.process(kFrames, in.data(), outB.data(), events.list()) == CLAP_PROCESS_CONTINUE);

    CHECK_FALSE(sameBits(outA.left, outB.left));
    CHECK(std::vector<float>(outB.left.begin() + kRamp, outB.left.end()) ==
          std::vector<float>(in.left.begin() + kRamp, in.left.end()));
    CHECK(std::vector<float>(outB.right.begin() + kRamp, outB.right.end()) ==
          std::vector<float>(in.right.begin() + kRamp, in.right.end()));
    CHECK(outB.left[kRamp - 1] != in.left[kRamp - 1]);

    CHECK(b.value(mix) == 0.5);
    CHECK(b.impl().processor()->base(ParamSlot::Mix) == 0.5f);
    CHECK(b.impl().processor()->effective(ParamSlot::Mix) == 0.0f);

    // A modulation of 0 restores the effective value; the base never moved.
    const auto unmod = gg::test::paramMod(mix, 0.0);
    InputEvents restore;
    restore.add(&unmod.header);
    REQUIRE(b.process(kFrames, in.data(), outB.data(), restore.list()) == CLAP_PROCESS_CONTINUE);
    CHECK(b.impl().processor()->effective(ParamSlot::Mix) == 0.5f);
    CHECK(b.value(mix) == 0.5);
}

TEST_CASE("state save then load renders identically", "[plugin]")
{
    // A sets six parameters by flush, renders 2 s of noise and saves; B loads that state into a
    // new instance, activates at the same rate and renders the same noise. The streams accept
    // and return a few bytes per call, as a host's may.
    constexpr uint32_t kFrames = 96000;
    Stereo in(kFrames);
    fillNoise(in, 21);

    PluginUnderTest a(kFxId);
    REQUIRE(a.valid());
    REQUIRE(a.activate(48000.0, 512));
    // Spray 1.23e-5 needs the state's exact float bits: 12 decimals don't hold it.
    flushValues(a, {{ParamSlot::Mix, 0.8},
                    {ParamSlot::Position, 0.1},
                    {ParamSlot::Size, -4.25},
                    {ParamSlot::Density, 6.7},
                    {ParamSlot::Pitch, 7.0},
                    {ParamSlot::Spray, 1.23e-5}});
    Stereo outA(kFrames);
    renderBlocks(a, in, outA, 512);

    gg::test::MemoryOStream saved(7);
    REQUIRE(a.state()->save(a.clap(), saved.stream()));
    CAPTURE(saved.data());

    PluginUnderTest b(kFxId);
    REQUIRE(b.valid());
    gg::test::MemoryIStream loading(saved.data(), 5);
    REQUIRE(b.state()->load(b.clap(), loading.stream()));
    for (const auto &p : kExpected)
    {
        CAPTURE(p.name);
        CHECK(b.value(p.id) == a.value(p.id));
    }
    REQUIRE(b.activate(48000.0, 512));
    Stereo outB(kFrames);
    renderBlocks(b, in, outB, 512);

    CHECK(allFinite(outA.left));
    CHECK(sameBits(outA.left, outB.left));
    CHECK(sameBits(outA.right, outB.right));
    // B saves what it loaded.
    CHECK(b.save() == saved.data());
}

TEST_CASE("state load while active applies at the next block", "[plugin]")
{
    // A loads state S between two blocks. The host reads the new values at once (and is asked
    // to rescan them), but the audio thread takes S only at the start of the next block, where
    // it applies like a params.flush of every value: B, which flushes S's values instead,
    // renders the same second block, and C, which does neither, a different one.
    constexpr uint32_t kBlock = 4096;
    Stereo first(kBlock);
    Stereo second(kBlock);
    fillNoise(first, 31);
    fillNoise(second, 32);

    ParamValues s = defaults();
    s[at(ParamSlot::Density)] = 7.0f;
    s[at(ParamSlot::Pitch)] = -5.0f;
    s[at(ParamSlot::Mix)] = 0.9f;
    s[at(ParamSlot::Position)] = 0.05f;
    s[at(ParamSlot::Spray)] = 0.0f;
    const std::string state = gg::core::toStateXml(s, 0);

    ParamsHost hostA;
    PluginUnderTest a(kFxId, hostA.clap());
    PluginUnderTest b(kFxId);
    PluginUnderTest c(kFxId);
    std::array<Stereo, 3> outs{Stereo(kBlock), Stereo(kBlock), Stereo(kBlock)};
    std::array<PluginUnderTest *, 3> plugins{&a, &b, &c};
    for (std::size_t i = 0; i < plugins.size(); ++i)
    {
        REQUIRE(plugins[i]->valid());
        REQUIRE(plugins[i]->activate(48000.0, kBlock));
        REQUIRE(plugins[i]->process(kBlock, first.data(), outs[i].data()) == CLAP_PROCESS_CONTINUE);
    }

    REQUIRE(a.load(state));
    CHECK(hostA.valueRescans() == 1);
    CHECK(hostA.otherRescans() == 0);
    CHECK(hostA.restarts() == 0); // the same seed
    CHECK(a.value(paramId(ParamSlot::Density)) == 7.0);
    CHECK(a.impl().processor()->base(ParamSlot::Density) == 4.0f); // not yet

    {
        std::vector<clap_event_param_value> storage;
        InputEvents events;
        storage.reserve(kParamCount);
        for (std::size_t i = 0; i < kParamCount; ++i)
        {
            storage.push_back(gg::test::paramValue(kExpected[i].id, s[i]));
            events.add(&storage.back().header);
        }
        b.flush(events.list());
    }

    for (std::size_t i = 0; i < plugins.size(); ++i)
    {
        REQUIRE(plugins[i]->process(kBlock, second.data(), outs[i].data()) ==
                CLAP_PROCESS_CONTINUE);
    }
    for (std::size_t i = 0; i < kParamCount; ++i)
    {
        CHECK(a.impl().processor()->base(static_cast<ParamSlot>(i)) == s[i]);
    }
    CHECK(sameBits(outs[0].left, outs[1].left));
    CHECK(sameBits(outs[0].right, outs[1].right));
    CHECK_FALSE(sameBits(outs[0].left, outs[2].left));

    SECTION("a flush takes a loaded state too")
    {
        ParamValues t = s;
        t[at(ParamSlot::Density)] = 2.0f;
        REQUIRE(a.load(gg::core::toStateXml(t, 0)));
        CHECK(a.impl().processor()->base(ParamSlot::Density) == 7.0f);
        InputEvents none;
        a.flush(none.list());
        CHECK(a.impl().processor()->base(ParamSlot::Density) == 2.0f);
    }

    SECTION("only the newest of several loads applies")
    {
        ParamValues t = s;
        t[at(ParamSlot::Density)] = 1.0f;
        t[at(ParamSlot::Stereo)] = 0.0f;
        ParamValues u = s;
        u[at(ParamSlot::Density)] = 3.0f;
        REQUIRE(a.load(gg::core::toStateXml(t, 0)));
        REQUIRE(a.load(gg::core::toStateXml(u, 0)));
        CHECK(hostA.valueRescans() == 3);
        InputEvents none;
        a.flush(none.list());
        CHECK(a.impl().processor()->base(ParamSlot::Density) == 3.0f);
        CHECK(a.impl().processor()->base(ParamSlot::Stereo) == s[at(ParamSlot::Stereo)]);
    }

    SECTION("a load the audio thread never took holds through reactivation")
    {
        ParamValues t = s;
        t[at(ParamSlot::Density)] = 5.5f;
        REQUIRE(a.load(gg::core::toStateXml(t, 0)));
        a.deactivate();
        flushValues(a, {{ParamSlot::Pitch, 3.0}}); // inactive, after the load
        REQUIRE(a.activate(48000.0, kBlock));
        CHECK(a.impl().processor()->base(ParamSlot::Density) == 5.5f);
        InputEvents none;
        a.flush(none.list()); // no stale state is left to apply
        CHECK(a.impl().processor()->base(ParamSlot::Pitch) == 3.0f);
        CHECK(a.value(paramId(ParamSlot::Pitch)) == 3.0);
    }
}

TEST_CASE("state carries the engine seed", "[plugin]")
{
    // The seed is saved and loaded with the values. The engine takes it when it is built, at
    // activate(), so a load that changes it while active asks the host for a restart.
    ParamValues values = defaults();
    uint64_t seed = 0;

    ParamsHost host;
    PluginUnderTest plugin(kFxId, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(gg::core::fromStateXml(plugin.save(), values, seed, gg::core::Mode::Fx));
    CHECK(seed == 0); // new instances start at seed 0 (spec §4)

    REQUIRE(plugin.load(gg::core::toStateXml(values, 9)));
    CHECK(host.restarts() == 0); // inactive: activate() takes it
    REQUIRE(plugin.activate(48000.0, 512));
    REQUIRE(plugin.load(gg::core::toStateXml(values, 9)));
    CHECK(host.restarts() == 0); // the same seed
    REQUIRE(plugin.load(gg::core::toStateXml(values, 10)));
    CHECK(host.restarts() == 1);
    REQUIRE(gg::core::fromStateXml(plugin.save(), values, seed, gg::core::Mode::Fx));
    CHECK(seed == 10);
}

TEST_CASE("state load rejects bad state and keeps the values", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    ParamsHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    flushValues(plugin, {{ParamSlot::Density, 6.0}});
    const std::string good = plugin.save();
    REQUIRE_FALSE(good.empty());

    ParamValues other = defaults();
    other[at(ParamSlot::Density)] = 1.0f;
    const std::string otherState = gg::core::toStateXml(other, 3);

    SECTION("not a state")
    {
        CHECK_FALSE(plugin.load("not xml at all"));
        CHECK_FALSE(plugin.load(""));
        CHECK_FALSE(plugin.load(otherState.substr(0, otherState.size() / 2)));
    }
    SECTION("a stream that fails part way")
    {
        gg::test::MemoryIStream failing(otherState, 16, 32);
        CHECK_FALSE(plugin.state()->load(plugin.clap(), failing.stream()));
    }
    SECTION("while active")
    {
        REQUIRE(plugin.activate(48000.0, 512));
        CHECK_FALSE(plugin.load("<patch"));
        InputEvents none;
        plugin.flush(none.list());
        CHECK(plugin.impl().processor()->base(ParamSlot::Density) == 6.0f);
    }

    CHECK(plugin.value(paramId(ParamSlot::Density)) == 6.0);
    CHECK(plugin.save() == good);
    CHECK(host.valueRescans() == 0);
    CHECK(host.restarts() == 0);

    // A save into a stream that fails fails.
    gg::test::MemoryOStream failing(8, 16);
    CHECK_FALSE(plugin.state()->save(plugin.clap(), failing.stream()));
}

TEST_CASE("fx handles mono, missing input and new sample rates", "[plugin]")
{
    // Review Focus 5. At each rate the plugin is reactivated, with Mix 1 kept from before the
    // activation:
    // - a mono input, with Mix stepped to 0 at sample 0: the FX feeds the one channel to both
    //   outputs, which reach the input exactly round(0.01 × sr) samples later (the Mix ramp
    //   follows the rate), and the live ring holds 16 s at the new rate;
    // - no input buffer: silence in, silence out at Mix 0;
    // - a stereo block at Mix 1: finite.
    // The instrument stays silent throughout.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const bool isFx = std::string_view(id) == kFxId;
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());

    for (const double rate : {44100.0, 96000.0, 192000.0})
    {
        CAPTURE(rate);
        flushValues(plugin, {{ParamSlot::Mix, 1.0}});
        REQUIRE(plugin.activate(rate, 4096));
        REQUIRE(plugin.impl().engine() != nullptr);
        CHECK(plugin.impl().engine()->sampleRate() == rate);
        if (isFx)
        {
            CHECK(plugin.impl().engine()->liveFrames() == static_cast<int64_t>(16.0 * rate));
        }
        const auto ramp = static_cast<std::size_t>(std::lround(0.01 * rate));

        // Mono in.
        std::vector<float> mono(4096);
        {
            Stereo noise(4096);
            fillNoise(noise, static_cast<uint64_t>(rate));
            mono = noise.left;
        }
        float *monoIn[1] = {mono.data()};
        Stereo out(4096, 7.0f);
        const auto mix0 = gg::test::paramValue(paramId(ParamSlot::Mix), 0.0);
        InputEvents events;
        events.add(&mix0.header);
        REQUIRE(plugin.process(4096, monoIn, out.data(), events.list(), 1) ==
                CLAP_PROCESS_CONTINUE);
        CHECK(allFinite(out.left));
        CHECK(allFinite(out.right));
        if (isFx)
        {
            const std::vector<float> tail(mono.begin() + static_cast<std::ptrdiff_t>(ramp),
                                          mono.end());
            CHECK(std::vector<float>(out.left.begin() + static_cast<std::ptrdiff_t>(ramp),
                                     out.left.end()) == tail);
            CHECK(std::vector<float>(out.right.begin() + static_cast<std::ptrdiff_t>(ramp),
                                     out.right.end()) == tail);
            CHECK(out.left[ramp - 1] != mono[ramp - 1]);
        }
        else
        {
            CHECK(allZero(out.left));
            CHECK(allZero(out.right));
        }

        // No input buffer.
        Stereo silentOut(4096, 7.0f);
        REQUIRE(plugin.process(4096, nullptr, silentOut.data()) == CLAP_PROCESS_CONTINUE);
        CHECK(allZero(silentOut.left));
        CHECK(allZero(silentOut.right));

        // Stereo in, all wet.
        flushValues(plugin, {{ParamSlot::Mix, 1.0}});
        Stereo in(4096);
        fillNoise(in, static_cast<uint64_t>(rate) + 1);
        Stereo wetOut(4096, 7.0f);
        REQUIRE(plugin.process(4096, in.data(), wetOut.data()) == CLAP_PROCESS_CONTINUE);
        CHECK(allFinite(wetOut.left));
        CHECK(allFinite(wetOut.right));
        if (!isFx)
        {
            CHECK(allZero(wetOut.left));
            CHECK(allZero(wetOut.right));
        }
        plugin.deactivate();
    }
}

TEST_CASE("remote controls page lists the cloud params", "[plugin]")
{
    // One 8-slot page (spec §4). The instrument leaves Mix's slot empty: Mix is hidden there.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const bool isFx = std::string_view(id) == kFxId;
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const auto *remote = static_cast<const clap_plugin_remote_controls *>(
        plugin.clap()->get_extension(plugin.clap(), CLAP_EXT_REMOTE_CONTROLS));
    REQUIRE(remote != nullptr);
    REQUIRE(remote->count(plugin.clap()) == 1);

    clap_remote_controls_page page;
    std::memset(&page, 0xA5, sizeof(page)); // every field must be written
    REQUIRE(remote->get(plugin.clap(), 0, &page));
    CHECK(std::string_view(page.section_name) == "Cloud");
    CHECK(std::string_view(page.page_name) == "Cloud");
    CHECK(page.page_id == 0);
    CHECK_FALSE(page.is_for_preset);
    const std::array<clap_id, CLAP_REMOTE_CONTROLS_COUNT> expected{
        0x010001,                         // Position
        0x010002,                         // Size
        0x010003,                         // Density
        0x010005,                         // Pitch
        0x010006,                         // Spray
        0x010007,                         // Stereo
        0x010008,                         // Freeze
        isFx ? 0x000001 : CLAP_INVALID_ID // Mix
    };
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        CAPTURE(i);
        CHECK(page.param_ids[i] == expected[i]);
    }
}

TEST_CASE("latest value hands over the newest snapshot", "[plugin]")
{
    using Snapshot = std::array<uint64_t, 4>;
    gg::plugin::LatestValue<Snapshot> latest;
    Snapshot got{};

    SECTION("one thread")
    {
        CHECK_FALSE(latest.take(got));
        latest.push({1, 1, 1, 1});
        latest.push({2, 2, 2, 2});
        REQUIRE(latest.take(got));
        CHECK(got == Snapshot{2, 2, 2, 2});
        CHECK_FALSE(latest.take(got));
        latest.push({3, 3, 3, 3});
        REQUIRE(latest.take(got));
        CHECK(got == Snapshot{3, 3, 3, 3});
        latest.push({4, 4, 4, 4});
        latest.push({5, 5, 5, 5});
        latest.push({6, 6, 6, 6});
        REQUIRE(latest.take(got));
        CHECK(got == Snapshot{6, 6, 6, 6});
        CHECK_FALSE(latest.take(got));
    }

    SECTION("two threads: never torn, never older, and the last is never lost")
    {
        constexpr uint64_t kPushes = 200000;
        std::atomic<bool> done{false};
        bool torn = false;
        bool older = false;
        uint64_t last = 0;
        std::thread consumer([&] {
            for (;;)
            {
                const bool finished = done.load(std::memory_order_acquire);
                if (latest.take(got))
                {
                    torn = torn || got[0] != got[1] || got[0] != got[2] || got[0] != got[3];
                    older = older || got[0] <= last;
                    last = got[0];
                }
                if (finished)
                {
                    break;
                }
            }
        });
        for (uint64_t i = 1; i <= kPushes; ++i)
        {
            latest.push({i, i, i, i});
        }
        done.store(true, std::memory_order_release);
        consumer.join();
        CHECK_FALSE(torn);
        CHECK_FALSE(older);
        CHECK(last == kPushes);
    }
}
