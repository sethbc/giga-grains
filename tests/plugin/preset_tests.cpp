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
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <clap/clap.h>
#include <nlohmann/json.hpp>

#include "PluginHarness.h"
#include "support/StateContainer.h"
#include "PresetSource.h"
#include "Render.h"
#include "RenderSpec.h"
#include "SampleLoader.h"
#include "Signals.h"
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

TEST_CASE("a session saved with its preset edited reopens with the edited mark", "[plugin][preset]")
{
    // The director's decision D-Q2 (2026-10-30), replacing the earlier amendment to §6.2.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const Mode mode = modeOf(id);
    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const std::string path = utf8(fixtureFile("edited-mark", mode));
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));

    // Unedited: the saved state carries no mark, and reopens unedited.
    const std::string plain = plugin.save();
    CHECK(gg::test::stateChunkPayload(plain, "PTCH")->find("edited") == std::string::npos);
    {
        PluginUnderTest reopened(id);
        REQUIRE(reopened.load(plain));
        CHECK_FALSE(reopened.impl().presetEdited());
    }

    // Edited: the mark is saved, and the reopened session shows the name with it.
    plugin.impl().uiSetValue(ParamSlot::Size, quarterOf(ParamSlot::Size, mode) + 0.5);
    gg::test::InputEvents none;
    plugin.flush(none.list());
    REQUIRE(plugin.impl().presetEdited());
    const std::string marked = plugin.save();
    CHECK(decoded(marked, mode).edited);
    PluginUnderTest reopened(id);
    REQUIRE(reopened.load(marked));
    CHECK(reopened.impl().presetName() == "Glass Hold");
    CHECK(reopened.impl().presetEdited());
    // Saved again, it stays edited; a preset load clears it.
    CHECK(decoded(reopened.save(), mode).edited);
    REQUIRE(reopened.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));
    CHECK_FALSE(reopened.impl().presetEdited());
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
    CHECK(decoded(instrument.save(), Mode::Instrument).samples[0] == entry);

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
    state.samples[0] = gg::test::entryOf(second);
    REQUIRE(state.samples[0].has_value());
    const std::string path =
        utf8(gg::test::writePresetFile("with-sample", infoFor(Mode::Instrument), state));
    REQUIRE(instrument.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));
    auto *loader = instrument.impl().sampleLoader();
    // Ready before from_location returned, as a restore is (D8).
    CHECK(loader->status().state == SampleState::Loaded);
    CHECK(loader->entry() == state.samples[0]);
    CHECK(sounds(instrument));
}

TEST_CASE("a preset keeps each layer's sample it names none for, and replaces the others",
          "[plugin][preset]")
{
    // D5 per layer, after sub-project 2's merge (MediaSession::restorePreset): layer A keeps its
    // sample, entry and source, and layer B takes the preset's.
    const fs::path dir = setUpSamples("preset-layers");
    const fs::path first = gg::test::sineSampleFile("preset-layers");
    const fs::path second = gg::test::writeSineWav(dir / "second.wav", 48000, 2.0, 330.0);
    const fs::path third = gg::test::writeSineWav(dir / "third.wav", 48000, 2.0, 440.0);
    PluginUnderTest instrument(kInstrumentId);
    REQUIRE(instrument.valid());
    SessionState session = gg::core::initState(Mode::Instrument);
    session.samples[0] = gg::test::entryOf(first);
    session.samples[1] = gg::test::entryOf(second);
    session.samples[1]->layer = 1;
    REQUIRE(instrument.load(gg::core::encodeState(session)));
    REQUIRE(instrument.activate(48000.0, 512));
    auto &media = instrument.impl().media();
    REQUIRE(media.loader(0).entry() == session.samples[0]);
    REQUIRE(media.loader(1).entry() == session.samples[1]);

    SessionState state = fixtureState(Mode::Instrument);
    state.samples[1] = gg::test::entryOf(third);
    state.samples[1]->layer = 1;
    const std::string path =
        utf8(gg::test::writePresetFile("layers", infoFor(Mode::Instrument), state));
    REQUIRE(instrument.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, path.c_str(), nullptr));
    CHECK(media.loader(0).entry() == session.samples[0]);
    CHECK(media.loader(0).status().state == SampleState::Loaded);
    CHECK(media.loader(1).entry() == state.samples[1]);
    CHECK(media.loader(1).status().state == SampleState::Loaded);
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

#if !defined(_WIN32)
TEST_CASE("an oversize preset file is refused by its size before it is read", "[plugin][preset]")
{
    // M8: the size is checked first, so a file past the cap is never read: an unreadable one
    // that is too long says it is too long, not that it can't be read. Sparse: nothing is written.
    PresetHost host;
    PluginUnderTest plugin(kFxId, host.clap());
    REQUIRE(plugin.valid());
    const fs::path file = fs::path(GG_TEST_OUTPUT_DIR) / "presets" / "too-long-unreadable.ggpreset";
    fs::create_directories(file.parent_path());
    fs::remove(file);
    gg::test::writeBytes(file, "");
    fs::resize_file(file, gg::core::kMaxStateBytes + 1);
    fs::permissions(file, fs::perms::none);
    if (std::ifstream(file).good())
    {
        fs::remove(file);
        SKIP("the file stays readable: running as root?");
    }
    std::string error;
    CHECK_FALSE(plugin.impl().loadPresetFile(file, error));
    CHECK(error == "the preset is larger than 64 MiB");
    fs::permissions(file, fs::perms::owner_all);
    fs::remove(file);
}
#endif

TEST_CASE("a preset loaded in the FX renders the bytes gg-render renders with it",
          "[plugin][preset][render]")
{
    // M2 sub-project 5's spec §10: the FX's test preset (tests/fixtures/presets/fx-test.json,
    // packed as gg-render --pack-preset packs it), loaded through preset-load and fed 4 s of the
    // faded sweep makeInput makes, in blocks of 256 at 48 kHz, gives the stereo output render()
    // gives a spec whose "preset" is the same file, over the same input and seed: every sample
    // equal, memcmp. The plugin's seed is the spec's (a preset keeps the instance's, D6), set
    // through a state first.
    constexpr uint64_t kSeed = 5;
    constexpr uint32_t kBlock = 256;
    constexpr double kRate = 48000.0;
    constexpr double kSeconds = 4.0;
    constexpr std::size_t kFrames = 192000;

    std::ifstream in(fs::path(GG_PRESET_FIXTURE_DIR) / "fx-test.json", std::ios::binary);
    const std::string json{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::string error;
    const auto source = gg::render::parsePresetSource(json, error);
    INFO(error);
    REQUIRE(source.has_value());
    const std::string bytes = gg::render::packPreset(*source, std::nullopt, error);
    INFO(error);
    REQUIRE_FALSE(bytes.empty());
    const fs::path file = fs::path(GG_TEST_OUTPUT_DIR) / "presets" / "bit-identity-fx.ggpreset";
    fs::create_directories(file.parent_path());
    gg::test::writeBytes(file, bytes);
    const std::string location = utf8(file);

    gg::render::RenderSpec spec;
    spec.mode = gg::render::RenderMode::Fx;
    spec.sampleRate = kRate;
    spec.durationSeconds = kSeconds;
    spec.blockSize = kBlock;
    spec.seed = kSeed;
    spec.fx.input.kind = gg::render::SourceKind::Sweep;
    spec.fx.input.seconds = kSeconds;
    spec.fx.channels = 2;
    spec.preset = location;
    const gg::metrics::AudioData expected = gg::render::render(spec, nullptr);
    REQUIRE(expected.channels.size() == 2);
    REQUIRE(expected.channels[0].size() == kFrames);
    float level = 0.0f;
    for (const float v : expected.channels[0])
    {
        level = std::max(level, std::abs(v));
    }
    REQUIRE(level > 0.01f); // the comparison is of sound, not of two silences

    const gg::metrics::AudioData input =
        gg::render::makeInput(spec.fx.input, spec.fx.channels, kRate, kSeed);
    REQUIRE(input.channels.size() == 2);
    REQUIRE(input.channels[0].size() == kFrames);

    // The preset is loaded either before the plugin is activated (activate() takes it) or after.
    const bool loadActive = GENERATE(false, true);
    CAPTURE(loadActive);
    setUpSamples("bit-identity");
    PresetHost host;
    PluginUnderTest plugin(kFxId, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.loadDefaultsWithSeed(kSeed));
    if (!loadActive)
    {
        REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, location.c_str(), nullptr));
    }
    REQUIRE(plugin.activate(kRate, kBlock));
    if (loadActive)
    {
        REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, location.c_str(), nullptr));
    }
    CHECK(plugin.impl().presetName() == "FX Test");

    std::vector<float> left(kFrames);
    std::vector<float> right(kFrames);
    std::vector<float> inLeft = input.channels[0];
    std::vector<float> inRight = input.channels[1];
    for (std::size_t done = 0; done < kFrames; done += kBlock)
    {
        float *inputs[2] = {inLeft.data() + done, inRight.data() + done};
        float *outputs[2] = {left.data() + done, right.data() + done};
        REQUIRE(plugin.process(kBlock, inputs, outputs) == CLAP_PROCESS_CONTINUE);
    }
    CHECK(std::memcmp(left.data(), expected.channels[0].data(), kFrames * sizeof(float)) == 0);
    CHECK(std::memcmp(right.data(), expected.channels[1].data(), kFrames * sizeof(float)) == 0);
}

// R4 (the fix wave's residuals): an active load applies its routes before its values, as at
// 9837c17, so FR-8's Freeze arming sees the new routes. The instrument, layer A Live, a fresh
// ring (reset) and a load whose Macro 6 route to Freeze is what turns Freeze on while its base is
// off: with the routes first, the layer never sees Freeze off and stays live until toggled (the
// head keeps advancing); with the values first under the outgoing routes, it arms and freezes.
TEST_CASE("an instrument active load arms Freeze under its own routes, as an inactive one does",
          "[plugin][preset]")
{
    constexpr Mode mode = Mode::Instrument;
    const auto at = [](ParamSlot s) { return static_cast<std::size_t>(s); };
    SessionState without = gg::core::initState(mode);
    without.values[at(ParamSlot::Source)] = static_cast<float>(gg::core::SourceKind::Live);
    without.routes[9] = gg::core::ModRoute{}; // no Macro 6 to Freeze
    SessionState with = without;
    with.routes = gg::core::factoryRoutes(mode); // Macro 6 to Freeze
    with.values[at(ParamSlot::Macro6)] = 1.0f;
    const fs::path fileWithout = gg::test::writePresetFile("arm-without", infoFor(mode), without);
    const fs::path fileWith = gg::test::writePresetFile("arm-with", infoFor(mode), with);
    const std::string locationWithout = utf8(fileWithout);
    const std::string locationWith = utf8(fileWith);

    constexpr uint32_t kBlock = 512;
    constexpr int kBlocks = 40; // far beyond the freeze ramp
    const auto headAfter = [&](bool activeLoad) {
        PresetHost host;
        PluginUnderTest plugin(kInstrumentId, host.clap());
        REQUIRE(plugin.valid());
        if (activeLoad)
        {
            REQUIRE(plugin.activate(48000.0, kBlock));
            REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, locationWithout.c_str(),
                                      nullptr));
            processBlock(plugin, mode, kBlock); // the outgoing routes are in
            plugin.impl().processor()->reset(); // a fresh ring: nothing armed
            REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, locationWith.c_str(),
                                      nullptr));
        }
        else
        {
            REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, locationWith.c_str(),
                                      nullptr));
            REQUIRE(plugin.activate(48000.0, kBlock));
        }
        for (int i = 0; i < kBlocks; ++i)
        {
            processBlock(plugin, mode, kBlock);
        }
        return plugin.impl().engine()->live().head();
    };
    const int64_t active = headAfter(true);
    const int64_t inactive = headAfter(false);
    CAPTURE(active, inactive);
    // The active one ran its 40 blocks after the reset, the inactive one after activation.
    CHECK(active == inactive);
}

// ---- Group 2: factory samples by their content-relative reference (spec §6.4, §6.12) ----

namespace
{
/// Points the content roots at `root` alone: GG_CONTENT_DIR, and the install roots' test seam
/// (GG_SYSTEM_CONTENT_DIR) at an empty folder beside it, so the machine's installed content never
/// joins. Each test case runs in its own process.
void useContentRoot(const fs::path &root)
{
    const fs::path system = root.parent_path() / "system";
    fs::create_directories(root);
    fs::create_directories(system);
#if defined(_WIN32)
    _wputenv_s(L"GG_CONTENT_DIR", root.c_str());
    _wputenv_s(L"GG_SYSTEM_CONTENT_DIR", system.c_str());
#else
    setenv("GG_CONTENT_DIR", root.c_str(), 1);
    setenv("GG_SYSTEM_CONTENT_DIR", system.c_str(), 1);
#endif
}

constexpr const char *kToneRef = "Samples/Test/tone.wav";

/// A scratch content root for the test `name` holding kToneRef: 1.5 s of a 330 Hz stereo sine at
/// 44.1 kHz (so both the plugin and gg-render convert it to 48 kHz). Generated, never committed.
fs::path contentRootWithTone(const std::string &name)
{
    const fs::path root = gg::test::sampleDir(name) / "content";
    gg::test::writeSineWav(root / "Samples" / "Test" / "tone.wav", 44100, 1.5, 330.0);
    return root;
}

/// The test preset of `mode` (tests/fixtures/presets/), its layer A sample kToneRef (the FX's
/// layer A set to Sample, so it reads it), packed as gg-render --pack-preset packs it with
/// --content-dir `root`, into presets/<name>.ggpreset.
fs::path packedWithTone(Mode mode, const fs::path &root, const std::string &name)
{
    std::ifstream in(fs::path(GG_PRESET_FIXTURE_DIR) /
                         (mode == Mode::Fx ? "fx-test.json" : "inst-test.json"),
                     std::ios::binary);
    std::string json{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::string error;
    auto source = gg::render::parsePresetSource(json, error);
    INFO(error);
    REQUIRE(source.has_value());
    source->samples = {{0, kToneRef}};
    if (mode == Mode::Fx)
    {
        source->params.emplace_back("A Source", 0.0); // Sample
        source->params.emplace_back("Mix", 1.0);      // the grains alone
    }
    const std::string bytes = gg::render::packPreset(*source, root, error);
    INFO(error);
    REQUIRE_FALSE(bytes.empty());
    const fs::path file = fs::path(GG_TEST_OUTPUT_DIR) / "presets" / (name + ".ggpreset");
    fs::create_directories(file.parent_path());
    gg::test::writeBytes(file, bytes);
    return file;
}

/// The bytes the plugin `id` renders after preset-load of `file` (before activation or after,
/// per `loadActive`), seed kSeed, in blocks of `block` at 48 kHz: the FX over `input`, the
/// instrument with a note of key 60 at velocity 1 from frame 0 to frame `noteOff`.
gg::metrics::AudioData pluginRender(const char *id, const fs::path &file, bool loadActive,
                                    uint64_t seed, uint32_t block, std::size_t frames,
                                    const gg::metrics::AudioData *input, std::size_t noteOff,
                                    SampleState *status)
{
    const Mode mode = modeOf(id);
    const std::string location = utf8(file);
    PresetHost host;
    PluginUnderTest plugin(id, host.clap());
    REQUIRE(plugin.valid());
    REQUIRE(plugin.loadDefaultsWithSeed(seed));
    if (!loadActive)
    {
        REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, location.c_str(), nullptr));
    }
    REQUIRE(plugin.activate(48000.0, block));
    if (loadActive)
    {
        REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, location.c_str(), nullptr));
    }
    *status = plugin.impl().media().loader(0).status().state;
    const auto &entry = plugin.impl().media().loader(0).entry();
    REQUIRE(entry.has_value());
    CHECK(entry->contentPath == kToneRef); // the reference is kept for the next save

    gg::metrics::AudioData out;
    out.channels.assign(2, std::vector<float>(frames));
    std::vector<float> inLeft(frames);
    std::vector<float> inRight(frames);
    if (input != nullptr)
    {
        inLeft = input->channels[0];
        inRight = input->channels[1];
    }
    auto on = gg::test::noteEvent(CLAP_EVENT_NOTE_ON, 0, 60, 0);
    on.velocity = 1.0;
    auto off = gg::test::noteEvent(CLAP_EVENT_NOTE_OFF, 0, 60, 0);
    off.velocity = 1.0;
    for (std::size_t done = 0; done < frames; done += block)
    {
        gg::test::InputEvents events;
        if (mode == Mode::Instrument && done == 0)
        {
            events.add(&on.header);
        }
        if (mode == Mode::Instrument && done == noteOff)
        {
            events.add(&off.header);
        }
        float *inputs[2] = {inLeft.data() + done, inRight.data() + done};
        float *outputs[2] = {out.channels[0].data() + done, out.channels[1].data() + done};
        REQUIRE(plugin.process(block, mode == Mode::Fx ? inputs : nullptr, outputs,
                               events.list()) == CLAP_PROCESS_CONTINUE);
    }
    return out;
}
} // namespace

TEST_CASE("a preset's content sample loads from the content roots and renders as gg-render does",
          "[plugin][preset][render]")
{
    // Spec §6.4's candidate (the content roots, after the absolute path) and §6.10's bit
    // identity, for a preset that names layer A's sample by its content-relative reference alone:
    // the plugin finds it under GG_CONTENT_DIR, gg-render under --content-dir, and the two render
    // the same bytes (memcmp), loaded before activation or after.
    constexpr uint64_t kSeed = 5;
    constexpr uint32_t kBlock = 256;
    constexpr std::size_t kFrames = 96000; // 2 s at 48 kHz
    constexpr std::size_t kNoteOff = 38400;
    const char *id = GENERATE(kInstrumentId, kFxId);
    const bool loadActive = GENERATE(false, true);
    CAPTURE(id, loadActive);
    const Mode mode = modeOf(id);
    const std::string name = std::string("content-") + tagOf(mode);
    setUpSamples(name);
    const fs::path root = contentRootWithTone(name);
    useContentRoot(root);
    const fs::path file = packedWithTone(mode, root, name);

    nlohmann::json j = {{"mode", tagOf(mode)}, {"sampleRate", 48000}, {"durationSeconds", 2.0},
                        {"blockSize", kBlock}, {"seed", kSeed},       {"preset", utf8(file)}};
    if (mode == Mode::Fx)
    {
        j["input"] = {{"kind", "sweep"},
                      {"startHz", 50},
                      {"endHz", 4000},
                      {"seconds", 2.0},
                      {"amplitude", 0.5}};
        j["channels"] = 2;
    }
    else
    {
        j["notes"] = {{{"time", 0.0}, {"key", 60}, {"duration", 0.8}, {"velocity", 127}}};
    }
    std::string error;
    const auto spec = gg::render::parseRenderSpec(j.dump(), error);
    INFO(error);
    REQUIRE(spec.has_value());
    const gg::metrics::AudioData expected =
        gg::render::render(*spec, nullptr, {.contentDir = utf8(root)});
    REQUIRE(expected.channels.size() == 2);
    REQUIRE(expected.channels[0].size() == kFrames);
    REQUIRE(peak(expected.channels[0]) > 0.01f);

    gg::metrics::AudioData input;
    if (mode == Mode::Fx)
    {
        input = gg::render::makeInput(spec->fx.input, 2, 48000.0, kSeed);
        REQUIRE(input.channels.size() == 2);
        input.channels[0].resize(kFrames, 0.0f);
        input.channels[1].resize(kFrames, 0.0f);
    }
    SampleState status = SampleState::None;
    const gg::metrics::AudioData got =
        pluginRender(id, file, loadActive, kSeed, kBlock, kFrames,
                     mode == Mode::Fx ? &input : nullptr, kNoteOff, &status);
    CHECK(status == SampleState::Loaded);
    CHECK(std::memcmp(got.channels[0].data(), expected.channels[0].data(),
                      kFrames * sizeof(float)) == 0);
    CHECK(std::memcmp(got.channels[1].data(), expected.channels[1].data(),
                      kFrames * sizeof(float)) == 0);
}

TEST_CASE("a preset's content sample missing from every content root leaves the layer missing",
          "[plugin][preset]")
{
    // Nothing under the roots: the load succeeds (its values apply), and the layer's status says
    // its sample is missing, naming the reference's file.
    setUpSamples("content-missing");
    const fs::path root = contentRootWithTone("content-missing");
    const fs::path file = packedWithTone(Mode::Instrument, root, "content-missing");
    useContentRoot(gg::test::sampleDir("content-missing") / "elsewhere");
    PluginUnderTest instrument(kInstrumentId);
    REQUIRE(instrument.valid());
    const std::string location = utf8(file);
    REQUIRE(instrument.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, location.c_str(), nullptr));
    const auto status = instrument.impl().media().loader(0).status();
    CHECK(status.state == SampleState::Missing);
    CHECK(status.fileName == "tone.wav");
}

TEST_CASE("a preset load keeps each layer's ring parameters", "[plugin][preset]")
{
    // The program manager's ruling: Ring Length, Loop, Loop Length, Overdub and Splice are the
    // instance's capture and memory, kept by every preset load (INIT's too), as the seed is, so
    // no preset load resizes a ring. The preset's other values apply, and it isn't edited.
    const char *id = GENERATE(kInstrumentId, kFxId);
    CAPTURE(id);
    const Mode mode = modeOf(id);
    const auto ringValues = [](float ring, float loop, float loopLength, float overdub,
                               float splice) {
        gg::core::ParamValues v{};
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            using gg::core::LayerParam;
            v[at(gg::core::layerSlot(LayerParam::RingLength, layer))] = ring;
            v[at(gg::core::layerSlot(LayerParam::Loop, layer))] = loop;
            v[at(gg::core::layerSlot(LayerParam::LoopLength, layer))] = loopLength;
            v[at(gg::core::layerSlot(LayerParam::Overdub, layer))] = overdub;
            v[at(gg::core::layerSlot(LayerParam::Splice, layer))] = splice;
        }
        return v;
    };
    const gg::core::ParamValues mine = ringValues(3.0f, 1.0f, -1.0f, 0.5f, 2.0f);
    const gg::core::ParamValues theirs = ringValues(5.0f, 0.0f, 2.0f, 0.25f, 7.0f);
    SessionState session = gg::core::initState(mode);
    SessionState preset = fixtureState(mode);
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        if (gg::core::isRingParam(static_cast<ParamSlot>(s)))
        {
            session.values[s] = mine[s];
            preset.values[s] = theirs[s];
        }
    }
    const std::string location =
        utf8(gg::test::writePresetFile("rings-" + tagOf(mode), infoFor(mode), preset));
    const auto check = [&](PluginUnderTest &plugin, const gg::core::ParamValues &values) {
        for (std::size_t s = 0; s < kParamCount; ++s)
        {
            INFO("slot " << s);
            const auto slot = static_cast<ParamSlot>(s);
            CHECK(plugin.value(paramId(slot)) ==
                  (gg::core::isRingParam(slot) ? mine[s] : values[s]));
        }
        CHECK_FALSE(plugin.impl().presetEdited());
    };

    PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    REQUIRE(plugin.load(gg::core::encodeState(session)));
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, location.c_str(), nullptr));
    check(plugin, preset.values);
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "init"));
    check(plugin, gg::core::initState(mode).values);

    // Active: the Processor's rings keep their length; nothing is resized.
    REQUIRE(plugin.activate(48000.0, 512));
    processBlock(plugin, mode);
    const auto *processor = plugin.impl().processor();
    std::array<int64_t, gg::core::kLayers> frames{};
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        frames[layer] = processor->ringRead(layer).storage->frames();
    }
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, location.c_str(), nullptr));
    processBlock(plugin, mode);
    check(plugin, preset.values);
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        CHECK(plugin.impl().processor()->base(
                  gg::core::layerSlot(gg::core::LayerParam::RingLength, layer)) == 3.0f);
        CHECK(processor->ringRead(layer).storage->frames() == frames[layer]);
    }
    CHECK(plugin.impl().media().status(0).find("resizing") == std::string::npos);
}

// R4's active params.flush path (the residuals re-review's (d)): a load taken by params.flush
// while active, not by process(), applies its routes before its values too, so FR-8's Freeze
// arming sees the new routes, as the inactive load does (the test above, through process()).
TEST_CASE("an instrument load taken by params.flush arms Freeze under its own routes",
          "[plugin][preset]")
{
    constexpr Mode mode = Mode::Instrument;
    SessionState without = gg::core::initState(mode);
    without.values[at(ParamSlot::Source)] = static_cast<float>(gg::core::SourceKind::Live);
    without.routes[9] = gg::core::ModRoute{}; // no Macro 6 to Freeze
    SessionState with = without;
    with.routes = gg::core::factoryRoutes(mode); // Macro 6 to Freeze
    with.values[at(ParamSlot::Macro6)] = 1.0f;
    const std::string locationWithout =
        utf8(gg::test::writePresetFile("flush-arm-without", infoFor(mode), without));
    const std::string locationWith =
        utf8(gg::test::writePresetFile("flush-arm-with", infoFor(mode), with));

    constexpr uint32_t kBlock = 512;
    constexpr int kBlocks = 40; // far beyond the freeze ramp
    const auto headAfter = [&](bool flushLoad) {
        PresetHost host;
        PluginUnderTest plugin(kInstrumentId, host.clap());
        REQUIRE(plugin.valid());
        if (flushLoad)
        {
            REQUIRE(plugin.activate(48000.0, kBlock));
            REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, locationWithout.c_str(),
                                      nullptr));
            gg::test::InputEvents none;
            plugin.flush(none.list()); // the outgoing routes are in, by the flush path
            plugin.impl().processor()->reset();
            REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, locationWith.c_str(),
                                      nullptr));
            plugin.flush(none.list()); // the load taken here, not by process()
        }
        else
        {
            REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, locationWith.c_str(),
                                      nullptr));
            REQUIRE(plugin.activate(48000.0, kBlock));
        }
        for (int i = 0; i < kBlocks; ++i)
        {
            processBlock(plugin, mode, kBlock);
        }
        return plugin.impl().engine()->live().head();
    };
    const int64_t flushed = headAfter(true);
    const int64_t inactive = headAfter(false);
    CAPTURE(flushed, inactive);
    CHECK(flushed == inactive);
}
