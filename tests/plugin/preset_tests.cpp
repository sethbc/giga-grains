// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Loading presets through the plugin as a host does (M2 sub-project 5's spec §6.2): preset-load's
// from_location for a .ggpreset file and for INIT, the host's loaded and on_error calls, the one
// state path (applyState) with what a preset leaves alone (D5: the sample; D6: the seed), the
// preset name and the edited mark. Each test's preset files go under
// GG_TEST_OUTPUT_DIR/presets/ in names of its own, its samples under
// GG_TEST_OUTPUT_DIR/plugin-sample-<name>/ with a settings file of its own.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <clap/clap.h>

#include "PluginHarness.h"
#include "SampleLoader.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Preset.h"
#include "gg/core/PresetInfo.h"
#include "gg/core/Processor.h"
#include "gg/core/SessionState.h"
#include "gg/media/Utf8Path.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

using gg::core::kParamCount;
using gg::core::Mode;
using gg::core::paramId;
using gg::core::ParamSlot;
using gg::core::SessionState;
using gg::plugin::SampleState;
using gg::test::PluginUnderTest;
using gg::test::PresetHost;
using gg::test::Stereo;

namespace
{
constexpr const char *kInstrumentId = "org.sethbc.giga-grains";
constexpr const char *kFxId = "org.sethbc.giga-grains-fx";
namespace fs = std::filesystem;

Mode modeOf(const char *id) { return std::string_view(id) == kFxId ? Mode::Fx : Mode::Instrument; }
std::string tagOf(Mode mode) { return mode == Mode::Fx ? "fx" : "instrument"; }
std::size_t at(ParamSlot s) { return static_cast<std::size_t>(s); }

// A value a quarter of the way up `slot`'s range in `mode`: not its default, and never clamped.
float quarterOf(ParamSlot slot, Mode mode)
{
    const auto &meta = gg::core::paramMeta(slot, mode);
    return meta.minVal + 0.25f * (meta.maxVal - meta.minVal);
}

// The fixture's route 13: LFO 2 to every layer's Pitch.
const gg::core::ModRoute kRoute13{
    gg::core::ModSource::Lfo2,  gg::core::kSameLayer, ParamSlot::Pitch, gg::core::kAllLayers, 0.25f,
    gg::core::ModCurve::Bipolar};

gg::core::PresetInfo infoFor(Mode mode, const std::string &name = "Glass Hold")
{
    return {name, mode, "gg-tests", "A test preset", {"texture"}};
}

// `mode`'s defaults with Size and Density moved and route 13 set, on the factory routes.
SessionState fixtureState(Mode mode)
{
    SessionState s = gg::core::initState(mode);
    s.values[at(ParamSlot::Size)] = quarterOf(ParamSlot::Size, mode);
    s.values[at(ParamSlot::Density)] = quarterOf(ParamSlot::Density, mode);
    s.routes[13] = kRoute13;
    return s;
}

// The fixture preset "Glass Hold" for `mode`, written as presets/<name>-<mode>.ggpreset.
fs::path fixtureFile(const std::string &name, Mode mode)
{
    return gg::test::writePresetFile(name + "-" + tagOf(mode), infoFor(mode), fixtureState(mode));
}

std::string utf8(const fs::path &path) { return gg::media::toUtf8(path).value(); }

SessionState decoded(const std::string &bytes, Mode mode)
{
    SessionState s;
    REQUIRE(gg::core::decodeState(bytes, mode, s));
    return s;
}

std::vector<uint32_t> bitsOf(const gg::core::ParamValues &values)
{
    std::vector<uint32_t> bits;
    for (const float v : values)
    {
        bits.push_back(std::bit_cast<uint32_t>(v));
    }
    return bits;
}

// Every parameter's value as params.get_value reads it, as float bits.
std::vector<uint32_t> valueBits(const PluginUnderTest &plugin)
{
    std::vector<uint32_t> bits;
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        bits.push_back(std::bit_cast<uint32_t>(
            static_cast<float>(plugin.value(paramId(static_cast<ParamSlot>(s))))));
    }
    return bits;
}

std::string notAPreset(Mode mode)
{
    return mode == Mode::Fx ? "not a Giga Grains FX preset" : "not a Giga Grains preset";
}

// One block through process(): the FX with a silent input, the instrument with none.
void processBlock(PluginUnderTest &plugin, Mode mode, uint32_t frames = 64)
{
    Stereo in(frames);
    Stereo out(frames);
    REQUIRE(plugin.process(frames, mode == Mode::Fx ? in.data() : nullptr, out.data()) ==
            CLAP_PROCESS_CONTINUE);
}

// This test's scratch folder, with a settings file of its own for the instances created from
// now on.
fs::path setUpSamples(const std::string &name)
{
    const fs::path dir = gg::test::sampleDir(name);
    const fs::path settings = dir / "settings.xml";
    fs::remove(settings);
    gg::test::useSettingsFile(settings);
    return dir;
}

float peak(const std::vector<float> &v)
{
    float p = 0.0f;
    for (const float x : v)
    {
        p = std::max(p, std::abs(x));
    }
    return p;
}

// Renders out.left.size() frames in blocks of `block`, with a CLAP note-on of key 60 at the
// first frame.
void renderNote(PluginUnderTest &plugin, Stereo &out, uint32_t block)
{
    const auto on = gg::test::noteEvent(CLAP_EVENT_NOTE_ON, 0, 60, 0);
    const std::size_t frames = out.left.size();
    for (std::size_t done = 0; done < frames;)
    {
        const auto n = static_cast<uint32_t>(std::min<std::size_t>(block, frames - done));
        gg::test::InputEvents events;
        if (done == 0)
        {
            events.add(&on.header);
        }
        float *outputs[2] = {out.left.data() + done, out.right.data() + done};
        REQUIRE(plugin.process(n, nullptr, outputs, events.list()) == CLAP_PROCESS_CONTINUE);
        done += n;
    }
}

// Whether the instrument sounds: a note of 4,800 frames peaks above 0.01.
bool sounds(PluginUnderTest &instrument)
{
    Stereo out(4800);
    renderNote(instrument, out, 512);
    return peak(out.left) > 0.01f && peak(out.right) > 0.01f;
}
} // namespace

TEST_CASE("a preset file loaded by preset-load sets its values and routes", "[plugin][preset]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const Mode mode = modeOf(id);
    const fs::path file = fixtureFile("load", mode);
    const std::string location = utf8(file);
    const SessionState expected = fixtureState(mode);

    // Active: the values at once, the routes from the next block.
    PresetHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.presetLoad() != nullptr);
    REQUIRE(plugin.activate(48000.0, 512));
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, location.c_str(), nullptr));
    CHECK(plugin.value(paramId(ParamSlot::Size)) == expected.values[at(ParamSlot::Size)]);
    CHECK(plugin.value(paramId(ParamSlot::Density)) == expected.values[at(ParamSlot::Density)]);
    CHECK(valueBits(plugin) == bitsOf(expected.values));
    processBlock(plugin, mode);
    CHECK(plugin.impl().processor()->routes()[13] == kRoute13);
    CHECK(plugin.impl().processor()->routes() == expected.routes);
    CHECK(plugin.impl().processor()->base(ParamSlot::Size) == expected.values[at(ParamSlot::Size)]);
    CHECK(plugin.impl().presetName() == "Glass Hold");
    CHECK_FALSE(plugin.impl().presetEdited());

    // The host hears of it with the same identity.
    REQUIRE(host.calls().size() == 1);
    const auto &call = host.calls()[0];
    CHECK(call.loaded);
    CHECK(call.kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE);
    CHECK(call.location == location);
    CHECK_FALSE(call.loadKey.has_value());

    // Inactive: activate() takes them.
    PluginUnderTest idle(id);
    REQUIRE(idle.valid());
    REQUIRE(idle.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, location.c_str(), "ignored"));
    CHECK(valueBits(idle) == bitsOf(expected.values));
    REQUIRE(idle.activate(48000.0, 512));
    CHECK(idle.impl().processor()->routes() == expected.routes);
    CHECK(idle.impl().processor()->base(ParamSlot::Density) ==
          expected.values[at(ParamSlot::Density)]);
}

TEST_CASE("preset-load takes a FILE location in the OS's own form", "[plugin][preset]")
{
    // The form discovery declares and a host's crawl hands back (discovery_tests.cpp): on Windows
    // the drive and native separators throughout, as %APPDATA% gives them; elsewhere '/'.
    std::string location = utf8(fixtureFile("native", Mode::Fx));
#if defined(_WIN32)
    std::replace(location.begin(), location.end(), '/', '\\');
    REQUIRE(location.size() > 3);
    CHECK(location[1] == ':');
    CHECK(location[2] == '\\');
#else
    REQUIRE(location.front() == '/');
#endif
    PresetHost host;
    PluginUnderTest plugin(kFxId, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, location.c_str(), nullptr));
    CHECK(plugin.impl().presetName() == "Glass Hold");
    REQUIRE(host.calls().size() == 1);
    CHECK(host.calls()[0].loaded);
    CHECK(host.calls()[0].location == location); // announced as the host gave it
}

TEST_CASE("preset-load refuses a missing file, a non-preset file and an unknown location",
          "[plugin][preset]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const Mode mode = modeOf(id);
    PresetHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    const std::string fixture = utf8(fixtureFile("refuse", mode));
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, fixture.c_str(), nullptr));
    const std::string before = plugin.save();
    host.clear();

    const auto refused = [&](uint32_t kind, const char *location, const char *loadKey) {
        CHECK_FALSE(plugin.loadPreset(kind, location, loadKey));
        CHECK(plugin.save() == before); // nothing changed
        CHECK(plugin.impl().presetName() == "Glass Hold");
        REQUIRE(host.calls().size() == 1);
        const PresetHost::Call call = host.calls()[0];
        host.clear();
        CHECK_FALSE(call.loaded);
        CHECK(call.kind == kind);
        CHECK(call.location ==
              (location != nullptr ? std::optional<std::string>(location) : std::nullopt));
        CHECK(call.loadKey ==
              (loadKey != nullptr ? std::optional<std::string>(loadKey) : std::nullopt));
        return call;
    };

    const fs::path dir = fs::path(GG_TEST_OUTPUT_DIR) / "presets";
    const std::string missing = utf8(dir / ("no-such-" + tagOf(mode) + ".ggpreset"));
    auto call = refused(CLAP_PRESET_DISCOVERY_LOCATION_FILE, missing.c_str(), nullptr);
    CHECK(call.message == "can't read no-such-" + tagOf(mode) + ".ggpreset");
    CHECK(call.osError != 0); // the OS's reason (ENOENT)

    const fs::path text = dir / ("plain-" + tagOf(mode) + ".ggpreset");
    gg::test::writeBytes(text, "not a preset at all\n");
    call = refused(CLAP_PRESET_DISCOVERY_LOCATION_FILE, utf8(text).c_str(), nullptr);
    CHECK(call.message == notAPreset(mode));
    CHECK(call.osError == 0);

    // A session state is no preset either.
    const fs::path session = dir / ("session-" + tagOf(mode) + ".ggpreset");
    gg::test::writeBytes(session, gg::core::encodeState(gg::core::initState(mode)));
    call = refused(CLAP_PRESET_DISCOVERY_LOCATION_FILE, utf8(session).c_str(), nullptr);
    CHECK(call.message == notAPreset(mode));

    call = refused(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "nope");
    CHECK(call.message == "unknown preset location");
    call = refused(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, nullptr);
    CHECK(call.message == "unknown preset location");
}

TEST_CASE("the other product's preset is refused and changes nothing", "[plugin][preset]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const Mode mode = modeOf(id);
    const Mode other = mode == Mode::Fx ? Mode::Instrument : Mode::Fx;
    PresetHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.activate(48000.0, 512));
    const std::string own = utf8(fixtureFile("own", mode));
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, own.c_str(), nullptr));
    processBlock(plugin, mode);
    const auto values = valueBits(plugin);
    const auto routes = plugin.impl().processor()->routes();
    const std::string before = plugin.save();
    host.clear();

    SessionState theirs = gg::core::initState(other);
    theirs.values[at(ParamSlot::Pitch)] = 5.0f;
    theirs.routes = gg::core::ModRoutes{};
    const std::string path =
        utf8(gg::test::writePresetFile("other-" + tagOf(mode), infoFor(other, "Theirs"), theirs));
    CHECK_FALSE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));
    processBlock(plugin, mode);
    CHECK(valueBits(plugin) == values);
    CHECK(plugin.impl().processor()->routes() == routes);
    CHECK(decoded(plugin.save(), mode).routes == routes);
    CHECK(plugin.save() == before);
    CHECK(plugin.impl().presetName() == "Glass Hold");
    REQUIRE(host.calls().size() == 1);
    CHECK_FALSE(host.calls()[0].loaded);
    CHECK(host.calls()[0].message == notAPreset(mode));
    CHECK(host.calls()[0].osError == 0);
}

TEST_CASE("INIT by load key restores every default and the factory routes", "[plugin][preset]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const Mode mode = modeOf(id);
    const SessionState init = gg::core::initState(mode);
    SessionState changed = fixtureState(mode);
    changed.name = "Changed";
    changed.seed = 3;
    changed.routes[0] = gg::core::ModRoute{};

    PresetHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.load(gg::core::encodeState(changed)));
    CHECK(plugin.impl().presetName() == "Changed");
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "init"));
    CHECK(valueBits(plugin) == bitsOf(init.values));
    CHECK(decoded(plugin.save(), mode).routes == gg::core::factoryRoutes(mode));
    CHECK(plugin.impl().presetName() == "Init");
    CHECK_FALSE(plugin.impl().presetEdited());
    REQUIRE(host.calls().size() == 1);
    CHECK(host.calls()[0].loaded);
    CHECK(host.calls()[0].kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN);
    CHECK_FALSE(host.calls()[0].location.has_value());
    CHECK(host.calls()[0].loadKey == std::string("init"));

    // A fresh instance's state, but for its seed (D8, D6).
    PluginUnderTest fresh(id);
    REQUIRE(fresh.valid());
    const SessionState mine = decoded(plugin.save(), mode);
    const SessionState theirs = decoded(fresh.save(), mode);
    CHECK(bitsOf(mine.values) == bitsOf(theirs.values));
    CHECK(mine.routes == theirs.routes);
    CHECK(mine.name == theirs.name);
    CHECK(mine.seed == 3);

    // Active: the next block has them.
    REQUIRE(plugin.activate(48000.0, 512));
    REQUIRE(plugin.load(gg::core::encodeState(changed)));
    processBlock(plugin, mode);
    REQUIRE(plugin.impl().processor()->routes() == changed.routes);
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "init"));
    processBlock(plugin, mode);
    CHECK(plugin.impl().processor()->routes() == gg::core::factoryRoutes(mode));
    gg::core::ParamValues bases{};
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        bases[s] = plugin.impl().processor()->base(static_cast<ParamSlot>(s));
    }
    CHECK(bitsOf(bases) == bitsOf(init.values));
    CHECK(valueBits(plugin) == bitsOf(init.values));
}

TEST_CASE("presetName follows the load and presetEdited turns true after one ui change",
          "[plugin][preset]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const Mode mode = modeOf(id);
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    auto &impl = plugin.impl();
    CHECK(impl.presetName() == "Init");
    CHECK_FALSE(impl.presetEdited());
    REQUIRE(plugin.activate(48000.0, 512));

    const std::string path = utf8(fixtureFile("edited", mode));
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));
    CHECK(impl.presetName() == "Glass Hold");
    CHECK_FALSE(impl.presetEdited());

    const float size = fixtureState(mode).values[at(ParamSlot::Size)];
    impl.uiSetValue(ParamSlot::Size, quarterOf(ParamSlot::Size, mode) + 0.5);
    CHECK_FALSE(impl.presetEdited()); // queued, not yet taken
    gg::test::InputEvents none;
    gg::test::OutputEvents out;
    plugin.flush(none.list(), out.list());
    CHECK(out.events().size() == 1);
    CHECK(impl.presetEdited());
    CHECK(impl.presetName() == "Glass Hold"); // the name stays; the editor adds the mark

    // Back to the preset's value, bit for bit: no longer edited.
    impl.uiSetValue(ParamSlot::Size, size);
    plugin.flush(none.list());
    CHECK_FALSE(impl.presetEdited());

    impl.uiSetValue(ParamSlot::Density, quarterOf(ParamSlot::Density, mode) + 0.5);
    plugin.flush(none.list());
    REQUIRE(impl.presetEdited());
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "init"));
    CHECK(impl.presetName() == "Init");
    CHECK_FALSE(impl.presetEdited());
}

TEST_CASE("the instrument keeps its loaded sample across a sample-less preset and INIT (D5)",
          "[plugin][preset]")
{
    setUpSamples("preset-d5");
    const fs::path file = gg::test::sineSampleFile("preset-d5");
    PluginUnderTest instrument(kInstrumentId);
    REQUIRE(instrument.valid());
    REQUIRE(instrument.loadDefaultsWithSeed(0));
    REQUIRE(instrument.loadSampleState(file));
    REQUIRE(instrument.activate(48000.0, 512));
    auto *loader = instrument.impl().sampleLoader();
    REQUIRE(loader != nullptr);
    const auto entry = loader->entry();
    REQUIRE(entry.has_value());
    REQUIRE(sounds(instrument));

    // Active: the preset's values, the sample kept, entry and source.
    const std::string path = utf8(fixtureFile("d5", Mode::Instrument));
    REQUIRE(instrument.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));
    CHECK(loader->entry() == entry);
    CHECK(loader->status().state == SampleState::Loaded);
    CHECK(sounds(instrument));
    CHECK(instrument.impl().processor()->base(ParamSlot::Size) ==
          fixtureState(Mode::Instrument).values[at(ParamSlot::Size)]);
    CHECK(decoded(instrument.save(), Mode::Instrument).sample == entry);

    REQUIRE(instrument.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "init"));
    CHECK(loader->entry() == entry);
    CHECK(loader->status().state == SampleState::Loaded);
    CHECK(sounds(instrument));

    // Inactive: the next activation still has it.
    instrument.deactivate();
    REQUIRE(instrument.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));
    CHECK(loader->entry() == entry);
    REQUIRE(instrument.activate(48000.0, 512));
    CHECK(loader->status().state == SampleState::Loaded);
    CHECK(sounds(instrument));
}

TEST_CASE("an instrument preset that names a sample loads it as a restore does", "[plugin][preset]")
{
    const fs::path dir = setUpSamples("preset-sample");
    const fs::path first = gg::test::sineSampleFile("preset-sample");
    const fs::path second = gg::test::writeSineWav(dir / "second.wav", 48000, 2.0, 330.0);
    PluginUnderTest instrument(kInstrumentId);
    REQUIRE(instrument.valid());
    REQUIRE(instrument.loadDefaultsWithSeed(0));
    REQUIRE(instrument.loadSampleState(first));
    REQUIRE(instrument.activate(48000.0, 512));

    SessionState state = fixtureState(Mode::Instrument);
    state.sample = gg::test::entryOf(second);
    REQUIRE(state.sample.has_value());
    const std::string path =
        utf8(gg::test::writePresetFile("with-sample", infoFor(Mode::Instrument), state));
    REQUIRE(instrument.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));
    auto *loader = instrument.impl().sampleLoader();
    // Ready before from_location returned, as a restore is (D8).
    CHECK(loader->status().state == SampleState::Loaded);
    CHECK(loader->entry() == state.sample);
    CHECK(sounds(instrument));
}

TEST_CASE("a preset load keeps the instance's seed (D6)", "[plugin][preset]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const Mode mode = modeOf(id);
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.loadDefaultsWithSeed(77));
    const std::string path = utf8(fixtureFile("seed", mode)); // saved with seed 0
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));
    CHECK(decoded(plugin.save(), mode).seed == 77);

    REQUIRE(plugin.activate(48000.0, 512));
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));
    processBlock(plugin, mode);
    CHECK(plugin.impl().processor()->seed() == 77);
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "init"));
    processBlock(plugin, mode);
    CHECK(plugin.impl().processor()->seed() == 77);
    CHECK(decoded(plugin.save(), mode).seed == 77);
}

TEST_CASE("a state saved after a preset load carries the preset's name", "[plugin][preset]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const Mode mode = modeOf(id);
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    CHECK(decoded(plugin.save(), mode).name == "Init");
    const std::string path = utf8(fixtureFile("named", mode));
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));
    const std::string saved = plugin.save();
    const SessionState s = decoded(saved, mode);
    CHECK(s.name == "Glass Hold");
    CHECK_FALSE(s.preset.has_value()); // a session, not a preset

    // A reopened session shows the preset it came from.
    PluginUnderTest reopened(id);
    REQUIRE(reopened.valid());
    REQUIRE(reopened.load(saved));
    CHECK(reopened.impl().presetName() == "Glass Hold");
    CHECK_FALSE(reopened.impl().presetEdited());
}

TEST_CASE("a preset file longer than kMaxStateBytes is refused", "[plugin][preset]")
{
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const Mode mode = modeOf(id);
    PresetHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    const std::string before = plugin.save();

    std::string bytes = gg::core::encodePreset(infoFor(mode), fixtureState(mode));
    REQUIRE(bytes.size() < gg::core::kMaxStateBytes);
    bytes.resize(gg::core::kMaxStateBytes + 1, '\0');
    const fs::path file =
        fs::path(GG_TEST_OUTPUT_DIR) / "presets" / ("too-long-" + tagOf(mode) + ".ggpreset");
    fs::create_directories(file.parent_path());
    gg::test::writeBytes(file, bytes);
    bytes = {};
    const std::string path = utf8(file);
    CHECK_FALSE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));
    fs::remove(file);
    CHECK(plugin.save() == before);
    CHECK(plugin.impl().presetName() == "Init");
    REQUIRE(host.calls().size() == 1);
    CHECK_FALSE(host.calls()[0].loaded);
    CHECK(host.calls()[0].message == "the preset is larger than 64 MiB");
    CHECK(host.calls()[0].osError == 0);
}
