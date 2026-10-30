// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The plugins' parameters, state and remote controls (spec §4, §5), through the clap_plugin
// vtables a host calls: parameter info, values and text; params.flush; modulation; state save
// and load, inactive and active; the FX's unusual wiring (Review Focus 5); and the
// remote-controls pages. The latest-wins hand-off that carries a state loaded while active
// (gg::core::LatestValue) is tested in tests/core/publish_tests.cpp.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <clap/clap.h>

#include "PluginHarness.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/PatchState.h"
#include "gg/core/Processor.h"
#include "gg/core/SessionState.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "support/ParamTable.h"
#include "support/StateContainer.h"

using gg::core::kParamCount;
using gg::core::paramId;
using gg::core::ParamSlot;
using gg::core::ParamValues;
using gg::test::fillNoise;
using gg::test::InputEvents;
using gg::test::OutputEvents;
using gg::test::PluginUnderTest;
using gg::test::sameBits;
using gg::test::Stereo;

namespace
{
constexpr const char *kInstrumentId = "org.sethbc.giga-grains";
constexpr const char *kFxId = "org.sethbc.giga-grains-fx";

std::size_t at(ParamSlot s) { return static_cast<std::size_t>(s); }

// Global Constraints' parameter table (tests/support/ParamTable.h): the IDs are frozen at the
// first public release (INTENT §18), append-only, and the order is the AUv2 parameter order.
using ExpectedParam = gg::test::ExpectedParamRow;
const std::array<ExpectedParam, kParamCount> &kExpected = gg::test::paramTable();

// The instrument's Position is 0–100% of the loaded file, 0% by default (spec §6.8), and its Source
// is Sample, in every layer; its Macro 8 is Macro Level (sub-project 5's spec §3.3). The rest of
// its table is the FX's above (Macro 7 is Macro Feedback, 0, in both; M2 sub-project 1's spec
// §6.9).
constexpr float kInstrumentPositionDefault = 0.0f;
constexpr float kInstrumentSourceDefault = 0.0f;

/// A layer parameter's index in its scope, or 0 for a global.
clap_id layerIndexOf(const ExpectedParam &e)
{
    const clap_id scope = e.id >> 16;
    return scope >= 1 && scope <= 4 ? (e.id & 0xFFFFu) : 0u;
}

/// e's default in the product.
float defaultIn(const ExpectedParam &e, bool isFx)
{
    if (!isFx && layerIndexOf(e) == 0x01)
    {
        return kInstrumentPositionDefault;
    }
    if (!isFx && layerIndexOf(e) == 0x0C)
    {
        return kInstrumentSourceDefault;
    }
    return e.def;
}

ParamValues defaults()
{
    ParamValues v{};
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        v[s] = kExpected[s].def;
    }
    return v;
}

// A host that offers clap.params and clap.state, and counts value rescans, text rescans, restart
// requests, params.request_flush and state.mark_dirty calls.
class ParamsHost
{
  public:
    ParamsHost() { host_.host_data = this; }
    ParamsHost(const ParamsHost &) = delete;
    ParamsHost &operator=(const ParamsHost &) = delete;

    const clap_host *clap() const noexcept { return &host_; }
    uint32_t valueRescans() const noexcept { return valueRescans_; }
    uint32_t textRescans() const noexcept { return textRescans_; }
    uint32_t otherRescans() const noexcept { return otherRescans_; }
    uint32_t restarts() const noexcept { return restarts_; }
    uint32_t dirtyMarks() const noexcept { return dirtyMarks_; }
    uint32_t flushRequests() const noexcept { return flushRequests_; }

  private:
    static ParamsHost &self(const clap_host *host)
    {
        return *static_cast<ParamsHost *>(host->host_data);
    }
    static void rescan(const clap_host *host, clap_param_rescan_flags flags) noexcept
    {
        auto &me = self(host);
        ++(flags == CLAP_PARAM_RESCAN_VALUES ? me.valueRescans_
           : flags == CLAP_PARAM_RESCAN_TEXT ? me.textRescans_
                                             : me.otherRescans_);
    }
    static const void *getExtension(const clap_host *, const char *id) noexcept
    {
        static const clap_host_params kParams{
            &ParamsHost::rescan,
            [](const clap_host *, clap_id, clap_param_clear_flags) {},
            [](const clap_host *host) { ++self(host).flushRequests_; }, // request_flush
        };
        static const clap_host_state kState{
            [](const clap_host *host) { ++self(host).dirtyMarks_; }, // mark_dirty
        };
        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        {
            return &kParams;
        }
        return std::strcmp(id, CLAP_EXT_STATE) == 0 ? &kState : nullptr;
    }

    uint32_t valueRescans_{0};
    uint32_t textRescans_{0};
    uint32_t otherRescans_{0};
    uint32_t restarts_{0};
    uint32_t dirtyMarks_{0};
    uint32_t flushRequests_{0};
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

float peakOf(const std::vector<float> &v)
{
    float p = 0.0f;
    for (float x : v)
    {
        p = std::max(p, std::abs(x));
    }
    return p;
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

// A saved state with Position's <p> cut out of its PTCH; its MEDI as it was.
std::string withoutPosition(const std::string &saved)
{
    auto ptch = gg::test::stateChunkPayload(saved, "PTCH");
    const auto medi = gg::test::stateChunkPayload(saved, "MEDI");
    REQUIRE(ptch.has_value());
    REQUIRE(medi.has_value());
    const auto from = ptch->find(R"(<p id="65537" )");
    REQUIRE(from != std::string::npos);
    ptch->erase(from, ptch->find("/>", from) + 2 - from);
    return gg::test::stateContainer(
        {gg::test::stateChunk("PTCH", *ptch), gg::test::stateChunk("MEDI", *medi)});
}

// The values and seed of a saved state, through the container.
gg::core::SessionState decoded(const std::string &saved, gg::core::Mode mode)
{
    gg::core::SessionState state;
    REQUIRE(gg::core::decodeState(saved, mode, state));
    return state;
}

// A clap_istream of `size` bytes, endless at UINT64_MAX: `prefix`, then zeros. It returns as
// much as asked, and counts what it served.
class PaddedIStream
{
  public:
    PaddedIStream(std::string prefix, uint64_t size) : prefix_(std::move(prefix)), size_(size) {}
    PaddedIStream(const PaddedIStream &) = delete;
    PaddedIStream &operator=(const PaddedIStream &) = delete;

    const clap_istream *stream() const noexcept { return &stream_; }
    uint64_t served() const noexcept { return served_; }

  private:
    static int64_t read(const clap_istream *stream, void *buffer, uint64_t size) noexcept
    {
        auto &self = *static_cast<PaddedIStream *>(stream->ctx);
        const uint64_t n = std::min<uint64_t>({size, self.size_ - self.served_, uint64_t{1} << 20});
        auto *out = static_cast<char *>(buffer);
        for (uint64_t i = 0; i < n; ++i)
        {
            const uint64_t at = self.served_ + i;
            out[i] = at < self.prefix_.size() ? self.prefix_[static_cast<std::size_t>(at)] : '\0';
        }
        self.served_ += n;
        return static_cast<int64_t>(n);
    }

    std::string prefix_;
    uint64_t size_;
    uint64_t served_{0};
    clap_istream stream_{this, &PaddedIStream::read};
};

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

TEST_CASE("plugin exposes every param", "[plugin]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const bool isFx = std::string_view(id) == kFxId;
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const auto *params = plugin.params();
    REQUIRE(params != nullptr);
    REQUIRE(params->count(plugin.clap()) == kParamCount);
    REQUIRE(params->count(plugin.clap()) == 223);

    for (uint32_t i = 0; i < kParamCount; ++i)
    {
        CAPTURE(i);
        clap_param_info info;
        std::memset(&info, 0xA5, sizeof(info)); // every field must be written
        REQUIRE(params->get_info(plugin.clap(), i, &info));
        const auto &expected = kExpected[i];
        CHECK(info.id == expected.id);
        std::string_view name = expected.name;
        if (!isFx && i == at(ParamSlot::Macro8))
        {
            name = "Macro Level";
        }
        CHECK(std::string_view(info.name) == name);
        // A layer parameter's module names its layer (spec §6.9); a global's is empty.
        const clap_id scope = expected.id >> 16;
        const std::string module =
            scope == 0 ? "" : std::string("Layer ") + static_cast<char>('A' + scope - 1);
        CHECK(std::string_view(info.module) == module);
        CHECK(info.cookie == nullptr);
        CHECK(info.min_value == static_cast<double>(expected.min));
        CHECK(info.max_value == static_cast<double>(expected.max));
        const float def = defaultIn(expected, isFx);
        CHECK(info.default_value == static_cast<double>(def));

        // Global Constraints' CLAP flags: every parameter is automatable and modulatable, the
        // switches and choices are stepped, and the instrument hides Mix and Dry to Reverb, which
        // have no dry signal to act on. Feedback is shown in both (M2 sub-project 1's spec §6.4).
        clap_param_info_flags flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
        if (expected.stepped)
        {
            flags |= CLAP_PARAM_IS_STEPPED;
        }
        if ((i == at(ParamSlot::Mix) || i == at(ParamSlot::DryToReverb)) && !isFx)
        {
            flags |= CLAP_PARAM_IS_HIDDEN;
        }
        CHECK(info.flags == flags);

        // A new instance holds the defaults.
        CHECK(plugin.value(expected.id) == static_cast<double>(def));
    }

    // The ones the spec names (spec §6.9).
    clap_param_info info{};
    REQUIRE(params->get_info(plugin.clap(), static_cast<uint32_t>(at(ParamSlot::B_Size)), &info));
    CHECK(std::string_view(info.module) == "Layer B");
    CHECK(std::string_view(info.name) == "B Size");
    REQUIRE(params->get_info(plugin.clap(), static_cast<uint32_t>(at(ParamSlot::Feedback)), &info));
    CHECK((info.flags & CLAP_PARAM_IS_HIDDEN) == 0);
    REQUIRE(params->get_info(plugin.clap(), static_cast<uint32_t>(at(ParamSlot::Macro7)), &info));
    CHECK(std::string_view(info.name) == "Macro Feedback");
    CHECK(info.default_value == 0.0);
    REQUIRE(params->get_info(plugin.clap(), static_cast<uint32_t>(at(ParamSlot::LevelD)), &info));
    CHECK(std::string_view(info.module).empty());
    // M2 sub-project 3's spec §6.8: Quality in every layer, Budget a global, the same in both
    // products.
    REQUIRE(
        params->get_info(plugin.clap(), static_cast<uint32_t>(at(ParamSlot::B_Quality)), &info));
    CHECK(info.id == 0x020050u);
    CHECK(std::string_view(info.module) == "Layer B");
    CHECK(std::string_view(info.name) == "B Quality");
    CHECK(info.default_value == 1.0);
    CHECK((info.flags & CLAP_PARAM_IS_STEPPED) != 0);
    REQUIRE(params->get_info(plugin.clap(), static_cast<uint32_t>(at(ParamSlot::Budget)), &info));
    CHECK(info.id == 0x000050u);
    CHECK(std::string_view(info.module).empty());
    CHECK(std::string_view(info.name) == "Budget");
    CHECK(info.min_value == 256.0);
    CHECK(info.max_value == 16384.0);
    CHECK(info.default_value == 4096.0);
    CHECK((info.flags & CLAP_PARAM_IS_STEPPED) != 0);
    CHECK(valueToText(plugin, 0x010050, 0.0) == "Eco");
    CHECK(valueToText(plugin, 0x020050, 1.0) == "Normal");
    CHECK(valueToText(plugin, 0x040050, 2.0) == "HQ");
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
                if (p.stepped)
                {
                    CHECK(back == std::round(v)); // a switch's or a choice's nearest step
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

    SECTION("Decay shows infinity at the top, and infinity parses to the top")
    {
        const clap_id decay = paramId(ParamSlot::ReverbDecay);
        CHECK(valueToText(plugin, decay, 1.0) == "2.00 s");
        CHECK(valueToText(plugin, decay, 5.95) == "∞");
        CHECK(valueToText(plugin, decay, static_cast<double>(gg::core::kDecayMax)) == "∞");
        double back = 0.0;
        REQUIRE(params->text_to_value(plugin.clap(), decay, "∞", &back));
        CHECK(back == static_cast<double>(gg::core::kDecayMax));
        // A bare number is seconds (4 s is 2 in log2 seconds); a unit still counts.
        REQUIRE(params->text_to_value(plugin.clap(), decay, "4", &back));
        CHECK(std::abs(back - 2.0) < 1e-6);
        REQUIRE(params->text_to_value(plugin.clap(), decay, "10", &back));
        CHECK(std::abs(back - std::log2(10.0)) < 1e-5);
        REQUIRE(params->text_to_value(plugin.clap(), decay, "500 ms", &back));
        CHECK(std::abs(back - (-1.0)) < 1e-5);
        // Pre-delay is seconds, shown in ms.
        const clap_id predelay = paramId(ParamSlot::ReverbPredelay);
        CHECK(valueToText(plugin, predelay, 0.25) == "250.0 ms");
        REQUIRE(params->text_to_value(plugin.clap(), predelay, "250.0 ms", &back));
        CHECK(std::abs(back - 0.25) < 1e-6);
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
        REQUIRE(plugin.load(withoutPosition(plugin.save())));
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
        REQUIRE(plugin.load(withoutPosition(plugin.save())));
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
    // renders the same second block, and C, which does neither, a different one. All three
    // start at S's seed, 0.
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
    const std::string state =
        gg::core::toStateXml(s, 0, gg::core::factoryRoutes(gg::core::Mode::Fx));

    ParamsHost hostA;
    PluginUnderTest a(kFxId, hostA.clap());
    PluginUnderTest b(kFxId);
    PluginUnderTest c(kFxId);
    std::array<Stereo, 3> outs{Stereo(kBlock), Stereo(kBlock), Stereo(kBlock)};
    std::array<PluginUnderTest *, 3> plugins{&a, &b, &c};
    for (std::size_t i = 0; i < plugins.size(); ++i)
    {
        REQUIRE(plugins[i]->valid());
        REQUIRE(plugins[i]->loadDefaultsWithSeed(0));
        REQUIRE(plugins[i]->activate(48000.0, kBlock));
        REQUIRE(plugins[i]->process(kBlock, first.data(), outs[i].data()) == CLAP_PROCESS_CONTINUE);
    }

    REQUIRE(a.load(state));
    CHECK(hostA.valueRescans() == 2); // loadDefaultsWithSeed's, then this one's
    CHECK(hostA.otherRescans() == 0);
    CHECK(hostA.restarts() == 0);
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
        REQUIRE(a.load(gg::core::toStateXml(t, 0, gg::core::factoryRoutes(gg::core::Mode::Fx))));
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
        REQUIRE(a.load(gg::core::toStateXml(t, 0, gg::core::factoryRoutes(gg::core::Mode::Fx))));
        REQUIRE(a.load(gg::core::toStateXml(u, 0, gg::core::factoryRoutes(gg::core::Mode::Fx))));
        CHECK(hostA.valueRescans() == 4);
        InputEvents none;
        a.flush(none.list());
        CHECK(a.impl().processor()->base(ParamSlot::Density) == 3.0f);
        CHECK(a.impl().processor()->base(ParamSlot::Stereo) == s[at(ParamSlot::Stereo)]);
    }

    SECTION("a load the audio thread never took holds through reactivation")
    {
        ParamValues t = s;
        t[at(ParamSlot::Density)] = 5.5f;
        REQUIRE(a.load(gg::core::toStateXml(t, 0, gg::core::factoryRoutes(gg::core::Mode::Fx))));
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

TEST_CASE("a new instance draws its own seed", "[plugin]")
{
    // Sub-project 5's spec §7: a new instance draws its engine seed from the OS's entropy
    // source, so two instances on two tracks make different clouds. Two draws colliding, or one
    // being 0, has a chance of 2^-64.
    const char *id = GENERATE(kFxId, kInstrumentId);
    CAPTURE(id);
    PluginUnderTest a(id);
    PluginUnderTest b(id);
    REQUIRE(a.valid());
    REQUIRE(b.valid());
    const uint64_t seedA = decoded(a.save(), a.impl().mode()).seed;
    const uint64_t seedB = decoded(b.save(), b.impl().mode()).seed;
    CAPTURE(seedA, seedB);
    CHECK(seedA != 0);
    CHECK(seedB != 0);
    CHECK(seedA != seedB);
    // The engine is built with it.
    REQUIRE(a.activate(48000.0, 512));
    CHECK(a.impl().processor()->seed() == seedA);
}

TEST_CASE("state carries the engine seed", "[plugin]")
{
    // The seed is saved and loaded with the values. Inactive, activate() builds the engine with
    // it; active, it goes to the audio thread with the values, which applies it at the start of
    // its next process() or params.flush, with no restart (carry-over row 115).
    const char *id = GENERATE(kFxId, kInstrumentId);
    CAPTURE(id);
    ParamsHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    const auto mode = plugin.impl().mode();

    REQUIRE(plugin.loadDefaultsWithSeed(9));
    CHECK(decoded(plugin.save(), mode).seed == 9);
    REQUIRE(plugin.activate(48000.0, 512));
    CHECK(plugin.impl().processor()->seed() == 9);

    REQUIRE(plugin.loadDefaultsWithSeed(10));
    CHECK(host.restarts() == 0);
    // Saved at once: the seed the audio thread is about to use.
    CHECK(decoded(plugin.save(), mode).seed == 10);
    CHECK(plugin.impl().processor()->seed() == 9); // not yet
    InputEvents none;
    plugin.flush(none.list());
    CHECK(plugin.impl().processor()->seed() == 10);

    REQUIRE(plugin.loadDefaultsWithSeed(11));
    Stereo out(64);
    REQUIRE(plugin.process(64, nullptr, out.data()) == CLAP_PROCESS_CONTINUE);
    CHECK(plugin.impl().processor()->seed() == 11);
    CHECK(host.restarts() == 0);
    CHECK(decoded(plugin.save(), mode).seed == 11);

    SECTION("a load the audio thread never took holds through reactivation")
    {
        REQUIRE(plugin.loadDefaultsWithSeed(12));
        plugin.deactivate();
        REQUIRE(plugin.activate(48000.0, 512));
        CHECK(plugin.impl().processor()->seed() == 12);
        plugin.flush(none.list()); // no stale seed is left to apply
        CHECK(plugin.impl().processor()->seed() == 12);
    }
}

namespace
{
// `state`, a state `mode`'s product saved, with engine seed `seed`.
std::string withSeed(const std::string &state, gg::core::Mode mode, uint64_t seed)
{
    gg::core::SessionState s = decoded(state, mode);
    s.seed = seed;
    return gg::core::encodeState(s);
}
} // namespace

TEST_CASE("a loaded state's seed reaches the audio thread without a restart", "[plugin]")
{
    // Three instances with the same history (seed 1, one block: of noise in the FX, of a held
    // note over a sine sample in the instrument) each load their own state while active, with
    // seed 7, 7 and 8, then render 2 s more. The two at seed 7 render the same bits, and the one
    // at 8 differs, from the first block after the load: the loaded seed reached the engine at
    // once, with no restart (carry-over row 115).
    const char *id = GENERATE(kFxId, kInstrumentId);
    CAPTURE(id);
    const bool isFx = std::string_view(id) == kFxId;
    constexpr uint32_t kBlock = 512;
    constexpr uint32_t kFrames = 96000;
    Stereo in(kBlock + kFrames);
    fillNoise(in, 41);
    const auto sample = isFx ? std::filesystem::path() : gg::test::sineSampleFile("seed-handoff");

    const auto run = [&](PluginUnderTest &plugin, uint64_t seed, Stereo &out) {
        REQUIRE(plugin.valid());
        const auto mode = plugin.impl().mode();
        REQUIRE(plugin.loadDefaultsWithSeed(1));
        if (isFx)
        {
            // Grains read about 20 ms behind the write head, so the wet signal starts early.
            flushValues(plugin, {{ParamSlot::Position, 0.05}, {ParamSlot::Spray, 0.0}});
        }
        else
        {
            REQUIRE(plugin.loadSampleState(sample));
        }
        // 256 Hz, a grain every 188 samples, so grains spawn inside the first block after the
        // load and the new seed shows there.
        flushValues(plugin, {{ParamSlot::Density, 8.0}});
        REQUIRE(plugin.activate(48000.0, kBlock));
        const auto on = gg::test::noteEvent(CLAP_EVENT_NOTE_ON, 0, 60, 0);
        InputEvents events;
        if (!isFx)
        {
            events.add(&on.header);
        }
        Stereo first(kBlock);
        REQUIRE(plugin.process(kBlock, isFx ? in.data() : nullptr, first.data(), events.list()) ==
                CLAP_PROCESS_CONTINUE);

        REQUIRE(plugin.load(withSeed(plugin.save(), mode, seed)));
        CHECK(plugin.impl().processor()->seed() == 1); // not yet
        for (uint32_t done = 0; done < kFrames; done += kBlock)
        {
            const uint32_t n = std::min(kBlock, kFrames - done);
            float *inPtrs[2] = {in.left.data() + kBlock + done, in.right.data() + kBlock + done};
            float *outPtrs[2] = {out.left.data() + done, out.right.data() + done};
            REQUIRE(plugin.process(n, isFx ? inPtrs : nullptr, outPtrs) == CLAP_PROCESS_CONTINUE);
            if (done == 0)
            {
                CHECK(plugin.impl().processor()->seed() == seed);
                CHECK(decoded(plugin.save(), mode).seed == seed);
            }
        }
    };

    ParamsHost hostA;
    PluginUnderTest a(id, hostA.clap());
    PluginUnderTest b(id);
    PluginUnderTest c(id);
    Stereo outA(kFrames);
    Stereo outB(kFrames);
    Stereo outC(kFrames);
    run(a, 7, outA);
    run(b, 7, outB);
    run(c, 8, outC);

    CHECK(hostA.restarts() == 0);
    CHECK(allFinite(outA.left));
    CHECK(peakOf(outA.left) > 0.01f);
    CHECK(sameBits(outA.left, outB.left));
    CHECK(sameBits(outA.right, outB.right));
    CHECK_FALSE(sameBits(outA.left, outC.left));
    // The new seed acts from the first block after the load, not a block later.
    const auto firstBlock = [](const std::vector<float> &v) {
        return std::vector<float>(v.begin(), v.begin() + kBlock);
    };
    CHECK_FALSE(sameBits(firstBlock(outA.left), firstBlock(outC.left)));
}

TEST_CASE("the value mirror is rescanned after the audio thread applies a state", "[plugin]")
{
    // Carry-over row 123 (ruling M1-x): a state loaded while active is in values_ at once, and
    // the host is asked to rescan them at once; the audio thread applies it at its next block,
    // where it stores the values again, and then asks for on_main_thread, which asks the host
    // to rescan them again, once, so it reads the values the audio thread settled on.
    const char *id = GENERATE(kFxId, kInstrumentId);
    CAPTURE(id);
    ParamsHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    Stereo in(512);
    Stereo out(512);

    REQUIRE(plugin.loadDefaultsWithSeed(3));
    CHECK(host.valueRescans() == 1);
    plugin.onMainThread(); // the audio thread hasn't applied it yet
    CHECK(host.valueRescans() == 1);
    REQUIRE(plugin.process(512, in.data(), out.data()) == CLAP_PROCESS_CONTINUE);
    CHECK(host.valueRescans() == 1);
    plugin.onMainThread();
    CHECK(host.valueRescans() == 2);
    plugin.onMainThread(); // once per application
    REQUIRE(plugin.process(512, in.data(), out.data()) == CLAP_PROCESS_CONTINUE);
    plugin.onMainThread();
    CHECK(host.valueRescans() == 2);

    SECTION("a params.flush that applies a state asks too")
    {
        REQUIRE(plugin.loadDefaultsWithSeed(4));
        CHECK(host.valueRescans() == 3);
        InputEvents none;
        plugin.flush(none.list());
        plugin.onMainThread();
        CHECK(host.valueRescans() == 4);
    }
    CHECK(host.otherRescans() == 0);
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
    const std::string otherXml =
        gg::core::toStateXml(other, 3, gg::core::factoryRoutes(gg::core::Mode::Fx));
    const std::string otherState = gg::core::encodeState(
        {other, 3, std::nullopt, std::nullopt, gg::core::factoryRoutes(gg::core::Mode::Fx)});

    SECTION("not a state")
    {
        CHECK_FALSE(plugin.load("not xml at all"));
        CHECK_FALSE(plugin.load(""));
        CHECK_FALSE(plugin.load(otherXml.substr(0, otherXml.size() / 2)));
        CHECK_FALSE(plugin.load(otherState.substr(0, otherState.size() - 1)));
        CHECK_FALSE(plugin.load("GGST"));
    }
    SECTION("a stream that fails part way")
    {
        gg::test::MemoryIStream failing(otherState, 16, 32);
        CHECK_FALSE(plugin.state()->load(plugin.clap(), failing.stream()));
    }
    SECTION("a stream longer than the cap")
    {
        // Endless: the load stops one byte past the cap, without reading further.
        PaddedIStream endless(otherState, UINT64_MAX);
        CHECK_FALSE(plugin.state()->load(plugin.clap(), endless.stream()));
        CHECK(endless.served() == gg::core::kMaxStateBytes + 1);
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
    // A regression guard only: a rejected load never reached mark_dirty before either.
    CHECK(host.dirtyMarks() == 0);

    // A save into a stream that fails fails.
    gg::test::MemoryOStream failing(8, 16);
    CHECK_FALSE(plugin.state()->save(plugin.clap(), failing.stream()));
}

TEST_CASE("state is a container, and save, load, save gives the same bytes", "[plugin]")
{
    // Both products save the GGST container (spec §6.6): PTCH, then a MEDI with no sample. A new
    // instance that loads it saves the same bytes again.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const auto mode = std::strcmp(id, kFxId) == 0 ? gg::core::Mode::Fx : gg::core::Mode::Instrument;

    PluginUnderTest a(id);
    REQUIRE(a.valid());
    flushValues(a, {{ParamSlot::Mix, 0.8},
                    {ParamSlot::Position, 0.1},
                    {ParamSlot::Pitch, 7.0},
                    {ParamSlot::Spray, 1.23e-5}});
    ParamValues values{};
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        values[s] = static_cast<float>(a.value(kExpected[s].id));
    }
    // The factory routes and one of the user's (M2 sub-project 1's spec §6.10): the routes in
    // effect are saved as they were loaded.
    gg::core::ModRoutes routes = gg::core::factoryRoutes(mode);
    routes[30] = gg::core::ModRoute{
        gg::core::ModSource::ModWheel, gg::core::kSameLayer, ParamSlot::Pitch, 2, 0.375f,
        gg::core::ModCurve::Unipolar};
    REQUIRE(routes != gg::core::factoryRoutes(mode));
    REQUIRE(a.load(gg::core::encodeState({values, 11, std::nullopt, std::nullopt, routes})));
    const std::string saved = a.save();
    CHECK(saved == gg::core::encodeState({values, 11, std::nullopt, std::nullopt, routes}));
    CHECK(gg::test::stateChunkPayload(saved, "PTCH") == gg::core::toStateXml(values, 11, routes));
    CHECK(decoded(saved, mode).routes == routes);
    CHECK(gg::test::stateChunkPayload(saved, "MEDI") == std::string(R"(<media version="1" />)"));
    CHECK(decoded(saved, mode).sample == std::nullopt);

    PluginUnderTest b(id);
    REQUIRE(b.valid());
    REQUIRE(b.load(saved));
    CHECK(b.save() == saved);

    // A state at the cap loads: a valid container padded with an unknown chunk to exactly
    // kMaxStateBytes. One byte more fails.
    const auto padded = [&](uint64_t size) {
        std::string prefix = saved + "XTRA";
        prefix += gg::test::littleEndian(size - saved.size() - 12, 8);
        return PaddedIStream(prefix, size);
    };
    PluginUnderTest c(id);
    REQUIRE(c.valid());
    {
        PaddedIStream atCap = padded(gg::core::kMaxStateBytes);
        REQUIRE(c.state()->load(c.clap(), atCap.stream()));
        CHECK(atCap.served() == gg::core::kMaxStateBytes);
        CHECK(c.save() == saved);
    }
    {
        flushValues(c, {{ParamSlot::Pitch, -3.0}});
        PaddedIStream overCap = padded(gg::core::kMaxStateBytes + 1);
        CHECK_FALSE(c.state()->load(c.clap(), overCap.stream()));
        CHECK(c.value(paramId(ParamSlot::Pitch)) == -3.0);
    }
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

TEST_CASE("remote controls pages are eight", "[plugin]")
{
    // Eight 8-slot pages (M2 sub-project 1's spec §6.9): "Perform", the eight macros (sub-project
    // 5's spec §3.3); "Cloud" (layer A's, spec §4) and "Space" (sub-project 4's spec §6.6), M1's
    // three at indices 0 to 2, unchanged; then "Layers" (the four switches and the four Levels),
    // "Morph" (X and Y, then six empty slots), and "Cloud B" to "Cloud D", each that layer's
    // Position, Size, Density, Pitch, Spray, Stereo, Freeze and Level. The instrument leaves
    // Mix's and Dry to Reverb's slots empty: they are hidden there. A page's index is its
    // page_id. There is no ninth.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const bool isFx = std::string_view(id) == kFxId;
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const auto *remote = static_cast<const clap_plugin_remote_controls *>(
        plugin.clap()->get_extension(plugin.clap(), CLAP_EXT_REMOTE_CONTROLS));
    REQUIRE(remote != nullptr);
    REQUIRE(remote->count(plugin.clap()) == 8);

    struct ExpectedPage
    {
        std::string_view name;
        std::array<clap_id, CLAP_REMOTE_CONTROLS_COUNT> ids;
    };
    // Layer L's cloud page (scope L + 1), with its Level.
    const auto cloud = [](std::string_view name, uint32_t scope, clap_id level) {
        const clap_id s = scope << 16;
        return ExpectedPage{name,
                            {s | 0x01, // Position
                             s | 0x02, // Size
                             s | 0x03, // Density
                             s | 0x05, // Pitch
                             s | 0x06, // Spray
                             s | 0x07, // Stereo
                             s | 0x08, // Freeze
                             level}};  // Level
    };
    const std::array<ExpectedPage, 8> pages{{
        {"Perform",
         {0x000011,   // Macro 1
          0x000012,   // Macro 2
          0x000013,   // Macro 3
          0x000014,   // Macro 4
          0x000015,   // Macro 5
          0x000016,   // Macro 6
          0x000017,   // Macro 7
          0x000018}}, // Macro 8
        {"Cloud",
         {0x010001,                            // Position
          0x010002,                            // Size
          0x010003,                            // Density
          0x010005,                            // Pitch
          0x010006,                            // Spray
          0x010007,                            // Stereo
          0x010008,                            // Freeze
          isFx ? 0x000001 : CLAP_INVALID_ID}}, // Mix
        {"Space",
         {0x01000B,                            // Diffusion
          0x000003,                            // Reverb Amount
          0x000004,                            // Reverb Decay
          0x000005,                            // Reverb Size
          0x000006,                            // Reverb Damping
          0x000007,                            // Reverb Pre-delay
          0x000008,                            // Reverb Modulation
          isFx ? 0x000009 : CLAP_INVALID_ID}}, // Dry to Reverb
        {"Layers",
         {0x00001B,   // Layer A On
          0x00001C,   // Layer B On
          0x00001D,   // Layer C On
          0x00001E,   // Layer D On
          0x000002,   // Level
          0x00001F,   // Level B
          0x000020,   // Level C
          0x000021}}, // Level D
        {"Morph",
         {0x000019, // Morph X
          0x00001A, // Morph Y
          CLAP_INVALID_ID, CLAP_INVALID_ID, CLAP_INVALID_ID, CLAP_INVALID_ID, CLAP_INVALID_ID,
          CLAP_INVALID_ID}},
        cloud("Cloud B", 2, 0x00001F),
        cloud("Cloud C", 3, 0x000020),
        cloud("Cloud D", 4, 0x000021),
    }};
    for (uint32_t p = 0; p < pages.size(); ++p)
    {
        CAPTURE(p);
        clap_remote_controls_page page;
        std::memset(&page, 0xA5, sizeof(page)); // every field must be written
        REQUIRE(remote->get(plugin.clap(), p, &page));
        CHECK(std::string_view(page.section_name) == pages[p].name);
        CHECK(std::string_view(page.page_name) == pages[p].name);
        CHECK(page.page_id == p);
        CHECK_FALSE(page.is_for_preset);
        for (std::size_t i = 0; i < pages[p].ids.size(); ++i)
        {
            CAPTURE(i);
            CHECK(page.param_ids[i] == pages[p].ids[i]);
        }
    }
    clap_remote_controls_page page;
    CHECK_FALSE(remote->get(plugin.clap(), 8, &page));
}

TEST_CASE("state carries the routes", "[plugin]")
{
    // M2 sub-project 1's spec §6.10: the routes are part of the state. A new instance's are the
    // factory routes; a loaded state's are saved back as they came, and reach the Processor at
    // activate() and, loaded while active, at the next block.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const auto mode = std::strcmp(id, kFxId) == 0 ? gg::core::Mode::Fx : gg::core::Mode::Instrument;
    {
        PluginUnderTest fresh(id);
        REQUIRE(fresh.valid());
        CHECK(decoded(fresh.save(), mode).routes == gg::core::factoryRoutes(mode));
    }

    gg::core::SessionState state;
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        state.values[s] = gg::core::paramMeta(static_cast<ParamSlot>(s), mode).defaultVal;
    }
    state.seed = 5;
    state.routes = gg::core::factoryRoutes(mode);
    state.routes[20] = gg::core::ModRoute{
        gg::core::ModSource::ModWheel, gg::core::kSameLayer, ParamSlot::Pitch, 1, 0.25f,
        gg::core::ModCurve::Bipolar};
    const std::string bytes = gg::core::encodeState(state);

    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.load(bytes));
    CHECK(plugin.save() == bytes);
    REQUIRE(plugin.activate(48000.0, 512));
    REQUIRE(plugin.impl().processor() != nullptr);
    CHECK(plugin.impl().processor()->routes() == state.routes);

    // Loaded while active: the next block applies them, and a save has them at once.
    state.routes[20].depth = -0.5f;
    state.routes[3] = gg::core::ModRoute{};
    const std::string other = gg::core::encodeState(state);
    REQUIRE(plugin.load(other));
    CHECK(plugin.save() == other);
    Stereo in(64);
    Stereo out(64);
    REQUIRE(plugin.process(64, in.data(), out.data()) == CLAP_PROCESS_CONTINUE);
    CHECK(plugin.impl().processor()->routes() == state.routes);

    // And a reactivation keeps them.
    plugin.deactivate();
    REQUIRE(plugin.activate(44100.0, 256));
    CHECK(plugin.impl().processor()->routes() == state.routes);
}

TEST_CASE("a kind change asks for a text rescan", "[plugin]")
{
    // Spec §6.4: Position's display follows its layer's built kind, seconds behind the head on a
    // Live layer and a percentage of the file on a Sample layer, in either product, active or
    // not. When a layer's kind changes the host is asked, once, on the main thread, to rescan
    // the parameters' text. An unbuilt kind is the product's default kind (builtKind), so a
    // change to one that falls back to the same kind asks nothing.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const bool isFx = std::string_view(id) == kFxId;
    ParamsHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    const auto source = gg::core::layerSlot(gg::core::LayerParam::Source, 1);
    const clap_id position = paramId(gg::core::layerSlot(gg::core::LayerParam::Position, 1));
    const clap_id positionA = paramId(ParamSlot::Position);
    const double seconds = static_cast<double>(0.28967658f); // 0.25 s on a Live layer
    const double other = isFx ? 0.0 : 1.0; // Sample in the FX, Live in the instrument
    const auto showsSeconds = [&](clap_id id_) {
        return valueToText(plugin, id_, seconds) == "0.25 s";
    };
    CHECK(showsSeconds(position) == isFx);
    CHECK(showsSeconds(positionA) == isFx);

    SECTION("while inactive")
    {
        flushValues(plugin, {{source, other}});
        CHECK(showsSeconds(position) == !isFx);
        CHECK(showsSeconds(positionA) == isFx); // layer A is unchanged
        if (!isFx)
        {
            CHECK(valueToText(plugin, position, 0.25).find('%') == std::string::npos);
        }
        else
        {
            CHECK(valueToText(plugin, position, 0.25).find('%') != std::string::npos);
        }
        double back = -1.0;
        const std::string text = valueToText(plugin, position, 0.6);
        REQUIRE(plugin.params()->text_to_value(plugin.clap(), position, text.c_str(), &back));
        CHECK(std::abs(back - 0.6) < 0.01);
        plugin.onMainThread();
        CHECK(host.textRescans() == 1);
        plugin.onMainThread();
        CHECK(host.textRescans() == 1);
        // Resample falls back to the product's default kind: back to the first display.
        flushValues(plugin, {{source, 2.0}});
        plugin.onMainThread();
        CHECK(host.textRescans() == 2);
        CHECK(showsSeconds(position) == isFx);
    }

    SECTION("while active, from process and from a state load")
    {
        REQUIRE(plugin.activate(48000.0, 512));
        Stereo in(64);
        Stereo out(64);
        const auto toOther = gg::test::paramValue(paramId(source), other);
        InputEvents events;
        events.add(&toOther.header);
        REQUIRE(plugin.process(64, in.data(), out.data(), events.list()) == CLAP_PROCESS_CONTINUE);
        CHECK(showsSeconds(position) == !isFx);
        CHECK(host.textRescans() == 0); // on the main thread only
        plugin.onMainThread();
        CHECK(host.textRescans() == 1);
        REQUIRE(plugin.process(64, in.data(), out.data()) == CLAP_PROCESS_CONTINUE);
        plugin.onMainThread();
        CHECK(host.textRescans() == 1); // once per change

        // Resample in the FX is Live, in the instrument Sample: each product's default kind.
        const auto toDefault = gg::test::paramValue(paramId(source), 2.0);
        InputEvents back;
        back.add(&toDefault.header);
        REQUIRE(plugin.process(64, in.data(), out.data(), back.list()) == CLAP_PROCESS_CONTINUE);
        plugin.onMainThread();
        CHECK(host.textRescans() == 2);
        CHECK(showsSeconds(position) == isFx);

        // A state load that changes the kind asks once the audio thread has applied it.
        auto loaded =
            decoded(plugin.save(), isFx ? gg::core::Mode::Fx : gg::core::Mode::Instrument);
        loaded.values[at(source)] = static_cast<float>(other);
        REQUIRE(plugin.load(gg::core::encodeState(loaded)));
        REQUIRE(plugin.process(64, in.data(), out.data()) == CLAP_PROCESS_CONTINUE);
        plugin.onMainThread();
        CHECK(host.textRescans() == 3);
        CHECK(showsSeconds(position) == !isFx);
    }
    CHECK(host.otherRescans() == 0);
}

namespace
{
using OutEvent = gg::test::OutputEvents::Event;

bool isGesture(const OutEvent &e, uint16_t type, clap_id id)
{
    return e.header.type == type && e.header.space_id == CLAP_CORE_EVENT_SPACE_ID &&
           e.header.time == 0 && e.header.flags == 0 &&
           e.header.size == sizeof(clap_event_param_gesture) && e.paramId == id;
}

// A CLAP_EVENT_PARAM_VALUE as the plugin sends the host its editor's changes: global (no cookie,
// note, port, channel or key), at time 0.
bool isValue(const OutEvent &e, clap_id id, double value)
{
    return e.header.type == CLAP_EVENT_PARAM_VALUE &&
           e.header.space_id == CLAP_CORE_EVENT_SPACE_ID && e.header.time == 0 &&
           e.header.flags == 0 && e.header.size == sizeof(clap_event_param_value) &&
           e.paramId == id && e.value == value && e.cookie == nullptr && e.noteId == -1 &&
           e.portIndex == -1 && e.channel == -1 && e.key == -1;
}

std::size_t countOf(const std::vector<OutEvent> &events, uint16_t type)
{
    return static_cast<std::size_t>(std::count_if(
        events.begin(), events.end(), [type](const OutEvent &e) { return e.header.type == type; }));
}
} // namespace

TEST_CASE("ui changes reach the host as a gesture with values", "[plugin]")
{
    // The editor's change (spec §6.3) goes to the audio thread, which applies it and tells the
    // host, from the next process() call: a gesture around the value, so the host records it.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    ParamsHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    auto &impl = plugin.impl();
    const clap_id size = paramId(ParamSlot::Size);
    REQUIRE(size == 0x010002);

    impl.uiBeginGesture(ParamSlot::Size);
    impl.uiSetValue(ParamSlot::Size, -1.0);
    impl.uiEndGesture(ParamSlot::Size);
    // One flush asked for: the three changes wait in one queue for the audio thread.
    CHECK(host.flushRequests() == 1);
    // Not applied until the audio thread takes them.
    CHECK(impl.uiBaseValue(ParamSlot::Size) == kExpected[at(ParamSlot::Size)].def);

    OutputEvents out;
    Stereo in(64);
    Stereo audio(64);
    REQUIRE(plugin.process(64, in.data(), audio.data(), nullptr, 2, out.list()) ==
            CLAP_PROCESS_CONTINUE);
    const auto &events = out.events();
    REQUIRE(events.size() == 3);
    CHECK(isGesture(events[0], CLAP_EVENT_PARAM_GESTURE_BEGIN, size));
    CHECK(isValue(events[1], size, -1.0));
    CHECK(isGesture(events[2], CLAP_EVENT_PARAM_GESTURE_END, size));
    CHECK(plugin.value(size) == -1.0);
    CHECK(impl.uiBaseValue(ParamSlot::Size) == -1.0f);
    CHECK(impl.processor()->base(ParamSlot::Size) == -1.0f);
    CHECK(impl.uiEventsDropped() == 0);

    // Drained: the next call sends nothing, and a new change asks for a flush again.
    out.clear();
    REQUIRE(plugin.process(64, in.data(), audio.data(), nullptr, 2, out.list()) ==
            CLAP_PROCESS_CONTINUE);
    CHECK(out.events().empty());
    impl.uiSetValue(ParamSlot::Size, -2.0);
    CHECK(host.flushRequests() == 2);
}

TEST_CASE("ui changes apply through flush when inactive", "[plugin]")
{
    // Before activate(), the host flushes on the main thread: the value goes into the mirror,
    // which activate() hands to the Processor, and the events go to flush's output list.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    ParamsHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    auto &impl = plugin.impl();
    const clap_id size = paramId(ParamSlot::Size);

    impl.uiBeginGesture(ParamSlot::Size);
    impl.uiSetValue(ParamSlot::Size, -1.0);
    impl.uiEndGesture(ParamSlot::Size);
    CHECK(host.flushRequests() == 1);

    OutputEvents out;
    InputEvents none;
    plugin.flush(none.list(), out.list());
    const auto &events = out.events();
    REQUIRE(events.size() == 3);
    CHECK(isGesture(events[0], CLAP_EVENT_PARAM_GESTURE_BEGIN, size));
    CHECK(isValue(events[1], size, -1.0));
    CHECK(isGesture(events[2], CLAP_EVENT_PARAM_GESTURE_END, size));
    CHECK(plugin.value(size) == -1.0);
    CHECK(impl.uiBaseValue(ParamSlot::Size) == -1.0f);

    REQUIRE(plugin.activate(48000.0, 512));
    CHECK(impl.processor()->base(ParamSlot::Size) == -1.0f);

    // Active, the same path through flush: the value applies at once (setBase), before the
    // host's own events in the same flush, which win.
    out.clear();
    impl.uiSetValue(ParamSlot::Size, -3.0);
    impl.uiSetValue(ParamSlot::Pitch, 5.0);
    const auto hostPitch = gg::test::paramValue(paramId(ParamSlot::Pitch), -7.0);
    InputEvents hostEvents;
    hostEvents.add(&hostPitch.header);
    plugin.flush(hostEvents.list(), out.list());
    REQUIRE(out.events().size() == 2);
    CHECK(isValue(out.events()[0], size, -3.0));
    CHECK(isValue(out.events()[1], paramId(ParamSlot::Pitch), 5.0));
    CHECK(impl.processor()->base(ParamSlot::Size) == -3.0f);
    CHECK(impl.processor()->base(ParamSlot::Pitch) == -7.0f);
    CHECK(plugin.value(paramId(ParamSlot::Pitch)) == -7.0);
}

TEST_CASE("a drag emits begin, values, end once", "[plugin]")
{
    // Review Focus 5, the plugin half: a drag of 100 moves, taken by two process() calls, the
    // host changing the same parameter meanwhile, reaches the host as exactly one begin and one
    // end, with every value between them in order, the last the drag's last.
    ParamsHost host;
    PluginUnderTest plugin(kFxId, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    auto &impl = plugin.impl();
    const clap_id position = paramId(ParamSlot::Position);
    const auto dragValue = [](int i) { return static_cast<double>(0.01f * static_cast<float>(i)); };

    OutputEvents out;
    Stereo in(64);
    Stereo audio(64);
    impl.uiBeginGesture(ParamSlot::Position);
    for (int i = 1; i <= 50; ++i)
    {
        impl.uiSetValue(ParamSlot::Position, dragValue(i));
    }
    REQUIRE(plugin.process(64, in.data(), audio.data(), nullptr, 2, out.list()) ==
            CLAP_PROCESS_CONTINUE);
    for (int i = 51; i <= 100; ++i)
    {
        impl.uiSetValue(ParamSlot::Position, dragValue(i));
    }
    impl.uiEndGesture(ParamSlot::Position);
    const auto hostMove = gg::test::paramValue(position, 0.25, 10);
    InputEvents hostEvents;
    hostEvents.add(&hostMove.header);
    REQUIRE(plugin.process(64, in.data(), audio.data(), hostEvents.list(), 2, out.list()) ==
            CLAP_PROCESS_CONTINUE);

    const auto &events = out.events();
    REQUIRE(events.size() == 102);
    CHECK(countOf(events, CLAP_EVENT_PARAM_GESTURE_BEGIN) == 1);
    CHECK(countOf(events, CLAP_EVENT_PARAM_GESTURE_END) == 1);
    CHECK(isGesture(events.front(), CLAP_EVENT_PARAM_GESTURE_BEGIN, position));
    CHECK(isGesture(events.back(), CLAP_EVENT_PARAM_GESTURE_END, position));
    for (int i = 1; i <= 100; ++i)
    {
        CAPTURE(i);
        CHECK(isValue(events[static_cast<std::size_t>(i)], position, dragValue(i)));
    }
    // The host's own move came after the drag's in the block, so it is the value now.
    CHECK(plugin.value(position) == 0.25);
    // Two flush requests at most: one per call that found the queue drained before it.
    CHECK(host.flushRequests() == 2);
}

TEST_CASE("ui changes the host's queue refuses are counted, not retried", "[plugin]")
{
    ParamsHost host;
    PluginUnderTest plugin(kFxId, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    auto &impl = plugin.impl();

    impl.uiBeginGesture(ParamSlot::Density);
    impl.uiSetValue(ParamSlot::Density, 6.0);
    impl.uiEndGesture(ParamSlot::Density);
    OutputEvents out(1); // one push accepted, then full
    Stereo in(64);
    Stereo audio(64);
    REQUIRE(plugin.process(64, in.data(), audio.data(), nullptr, 2, out.list()) ==
            CLAP_PROCESS_CONTINUE);
    REQUIRE(out.events().size() == 1);
    CHECK(isGesture(out.events()[0], CLAP_EVENT_PARAM_GESTURE_BEGIN, paramId(ParamSlot::Density)));
    CHECK(out.refused() == 2); // each refused once
    CHECK(impl.uiEventsDropped() == 2);
    // The value still applied: the host's queue only carries the news.
    CHECK(impl.processor()->base(ParamSlot::Density) == 6.0f);
    CHECK(plugin.value(paramId(ParamSlot::Density)) == 6.0);
}

TEST_CASE("ui values are clamped and a nan is ignored", "[plugin]")
{
    ParamsHost host;
    PluginUnderTest plugin(kFxId, host.clap());
    REQUIRE(plugin.valid());
    auto &impl = plugin.impl();
    impl.uiSetValue(ParamSlot::Pitch, 100.0);
    impl.uiSetValue(ParamSlot::Mix, std::numeric_limits<double>::quiet_NaN());
    impl.uiSetValue(ParamSlot::Density, 0.1);
    OutputEvents out;
    InputEvents none;
    plugin.flush(none.list(), out.list());
    // The host gets the value the plugin applies: in range, and a float, as values are end to
    // end (toParamEvent).
    REQUIRE(out.events().size() == 2);
    CHECK(isValue(out.events()[0], paramId(ParamSlot::Pitch), 24.0));
    CHECK(isValue(out.events()[1], paramId(ParamSlot::Density), static_cast<double>(0.1f)));
    CHECK(plugin.value(paramId(ParamSlot::Pitch)) == 24.0);
    CHECK(plugin.value(paramId(ParamSlot::Mix)) ==
          static_cast<double>(kExpected[at(ParamSlot::Mix)].def));
}
