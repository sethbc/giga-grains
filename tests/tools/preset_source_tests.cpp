// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// gg-render's preset sources and their packer (PresetSource.h; M2 sub-project 5's spec §6.6), and
// renders through a packed preset (the spec's "preset" key, §6.10).

#include <algorithm>
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
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "PresetSource.h"
#include "Render.h"
#include "RenderSpec.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/Mutate.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Preset.h"
#include "gg/core/PresetInfo.h"
#include "gg/core/SessionState.h"
#include "gg/media/Sha256.h"
#include "media/AudioFixtures.h"
#include "support/Utf8.h"

using Catch::Matchers::ContainsSubstring;
using gg::core::Mode;
using gg::render::PresetSource;
using gg::test::utf8;

namespace
{
std::string readText(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::filesystem::path fixture(const std::string &name)
{
    return std::filesystem::path{GG_PRESET_FIXTURE_DIR} / name;
}

PresetSource parseFixture(const std::string &name)
{
    std::string error;
    auto source = gg::render::parsePresetSource(readText(fixture(name)), error);
    INFO(name << ": " << error);
    REQUIRE(source.has_value());
    CHECK(error.empty());
    return std::move(*source);
}

/// The fx-test fixture as JSON, to change one key at a time.
nlohmann::json fxTestJson() { return nlohmann::json::parse(readText(fixture("fx-test.json"))); }

/// Parses j, requires the parse to fail, and returns the error message.
std::string rejection(const nlohmann::json &j)
{
    std::string error;
    CHECK_FALSE(gg::render::parsePresetSource(j.dump(), error).has_value());
    CHECK_FALSE(error.empty());
    return error;
}

std::string pack(const PresetSource &source)
{
    std::string error;
    std::string bytes = gg::render::packPreset(source, std::nullopt, error);
    INFO(error);
    REQUIRE(error.empty());
    REQUIRE_FALSE(bytes.empty());
    return bytes;
}

/// A directory of this file's own under GG_TEST_OUTPUT_DIR.
std::filesystem::path scratch(const std::string &name)
{
    const auto dir = std::filesystem::path{GG_TEST_OUTPUT_DIR} / "preset-source-tests" / name;
    std::filesystem::create_directories(dir);
    return dir;
}

void writeBytes(const std::filesystem::path &path, const std::string &bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    REQUIRE_FALSE(out.fail());
}

gg::render::RenderSpec parseSpec(const nlohmann::json &j)
{
    std::string error;
    auto spec = gg::render::parseRenderSpec(j.dump(), error);
    INFO(error);
    REQUIRE(spec.has_value());
    return std::move(*spec);
}

/// A render spec of tests/fixtures/render/, apart from the preset sources (T4).
std::filesystem::path renderFixture(const std::string &name)
{
    return std::filesystem::path{GG_RENDER_FIXTURE_DIR} / name;
}

/// The 1 s FX render over a sine of tests/fixtures/render/render-fx-preset.json.
nlohmann::json fxSpecJson()
{
    return nlohmann::json::parse(readText(renderFixture("render-fx-preset.json")));
}

bool bitIdentical(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

/// The content-relative reference of the test sample contentWith writes.
constexpr const char *kToneRef = "Samples/Test/tone.wav";

/// A scratch content root of this file's own holding kToneRef: 1 s of makeTestAudio's stereo
/// sines at 44.1 kHz, 24-bit (so a render at 48 kHz converts it). Generated, never committed.
std::filesystem::path contentWith(const std::string &name)
{
    const auto root = scratch(name) / "content";
    const auto file = root / "Samples" / "Test" / "tone.wav";
    std::filesystem::create_directories(file.parent_path());
    gg::test::writeFile(file, gg::test::wavBytes(gg::test::makeTestAudio(2, 44100, 44100),
                                                 gg::test::SampleFormat::Int24));
    return root;
}

/// `source` packed with `contentDir`, which must succeed.
std::string packWith(const PresetSource &source, const std::filesystem::path &contentDir)
{
    std::string error;
    std::string bytes = gg::render::packPreset(source, contentDir, error);
    INFO(error);
    REQUIRE(error.empty());
    REQUIRE_FALSE(bytes.empty());
    return bytes;
}

/// The instrument's test source with layer A's sample kToneRef (and layer C's too when
/// `twoLayers`).
PresetSource instWithSample(bool twoLayers = false)
{
    auto j = nlohmann::json::parse(readText(fixture("inst-test.json")));
    j["samples"] = {{"A", kToneRef}};
    if (twoLayers)
    {
        j["samples"]["C"] = kToneRef;
    }
    std::string error;
    auto source = gg::render::parsePresetSource(j.dump(), error);
    INFO(error);
    REQUIRE(source.has_value());
    return std::move(*source);
}

/// What render() throws as BadInput for `spec` with `options`; fails the test if it doesn't.
std::string badInputOf(const nlohmann::json &j, const gg::render::RenderOptions &options = {})
{
    try
    {
        gg::render::render(parseSpec(j), nullptr, options);
    }
    catch (const gg::render::BadInput &e)
    {
        return std::string(e.what());
    }
    FAIL("render() didn't throw BadInput");
    return std::string();
}
} // namespace

TEST_CASE("a preset source packs Clock", "[render]")
{
    auto j = fxTestJson();
    j["params"]["A Clock"] = 1.0;
    std::string error;
    const auto source = gg::render::parsePresetSource(j.dump(), error);
    INFO(error);
    REQUIRE(source.has_value());
    gg::core::PresetInfo info;
    gg::core::SessionState state;
    REQUIRE(gg::core::readPreset(pack(*source), Mode::Fx, info, state));
    CHECK(state.values[static_cast<std::size_t>(gg::core::ParamSlot::Clock)] == 1.0f);
    CHECK(state.values[static_cast<std::size_t>(gg::core::ParamSlot::B_Clock)] == 0.0f);
}

TEST_CASE("preset sources parse", "[render]")
{
    const PresetSource fx = parseFixture("fx-test.json");
    CHECK(fx.info.name == "FX Test");
    CHECK(fx.info.product == Mode::Fx);
    CHECK(fx.info.author == "Giga Grains");
    CHECK_FALSE(fx.info.description.empty());
    CHECK(fx.info.tags == std::vector<std::string>{"texture", "dry"});
    // In name order, as the render spec's reader gives them (each names its own slot).
    CHECK(fx.params ==
          std::vector<std::pair<std::string, double>>{
              {"B Pitch", 7.0}, {"Density", 5.0}, {"Layer B On", 1.0}, {"Size", -1.0}});
    REQUIRE(fx.mod.size() == 1);
    CHECK(fx.mod[0].slot == 13);
    CHECK(fx.mod[0].source == "LFO 1");
    CHECK(fx.mod[0].target == "Position");
    CHECK(fx.mod[0].targetLayer == "all");
    CHECK(fx.mod[0].depth == 0.1);
    CHECK(fx.samples.empty());

    const PresetSource inst = parseFixture("inst-test.json");
    CHECK(inst.info.name == "Inst Test");
    CHECK(inst.info.product == Mode::Instrument);
    CHECK(inst.info.tags == std::vector<std::string>{"playable", "sample"});
    CHECK(inst.params ==
          std::vector<std::pair<std::string, double>>{{"Position", 0.25}, {"Reverb Amount", 0.3}});
    REQUIRE(inst.mod.size() == 1);
    CHECK(inst.mod[0].source == "Env 1");
    CHECK(inst.mod[0].target == "Level");
    CHECK(inst.mod[0].depth == 6.0);

    // Author, description, params, mod and samples are optional.
    std::string error;
    const auto bare = gg::render::parsePresetSource(
        R"({"product":"instrument","name":"Bare","tags":["texture"]})", error);
    INFO(error);
    REQUIRE(bare.has_value());
    CHECK(bare->info.author.empty());
    CHECK(bare->info.description.empty());
    CHECK(bare->params.empty());
    CHECK(bare->mod.empty());

    // "samples" maps letters to content-relative paths, in layer order.
    auto j = fxTestJson();
    j["samples"] = {{"C", "Samples/Voices/alto-ah.flac"}, {"A", "Samples/Keys/piano.flac"}};
    const auto withSamples = gg::render::parsePresetSource(j.dump(), error);
    INFO(error);
    REQUIRE(withSamples.has_value());
    CHECK(withSamples->samples ==
          std::vector<std::pair<uint8_t, std::string>>{{0, "Samples/Keys/piano.flac"},
                                                       {2, "Samples/Voices/alto-ah.flac"}});
}

TEST_CASE("preset sources reject bad input", "[render]")
{
    auto j = fxTestJson();
    j["product"] = "synth";
    CHECK(rejection(j) == "product must be fx or instrument");

    j = fxTestJson();
    j["tags"] = {"texture", "Loud"};
    CHECK(rejection(j) == "tags[1] 'Loud' is not in the vocabulary");

    // live-input was struck from the vocabulary (the M2 gate, item 29).
    j = fxTestJson();
    j["tags"] = {"texture", "live-input"};
    CHECK(rejection(j) == "tags[1] 'live-input' is not in the vocabulary");

    j = fxTestJson();
    j["params"]["Foo"] = 1.0;
    CHECK(rejection(j) == "params.Foo is not a parameter of Giga Grains FX");
    j = fxTestJson();
    j["product"] = "instrument";
    j["params"] = {{"Macro Mix", 0.5}}; // the FX's Macro 8 alone
    CHECK(rejection(j) == "params.Macro Mix is not a parameter of Giga Grains");

    j = fxTestJson();
    j["bogus"] = 1;
    CHECK(rejection(j) == "unknown key bogus");

    j = fxTestJson();
    j["mod"][0]["target"] = "Macro Size";
    CHECK(rejection(j) == "mod[0].target is not a modulation target");

    // A value out of its parameter's range, as the render spec's parser says it.
    j = fxTestJson();
    j["params"]["Density"] = 100.0;
    CHECK_THAT(rejection(j), ContainsSubstring("params.Density must be in"));

    for (const char *key : {"product", "name", "tags"})
    {
        j = fxTestJson();
        j.erase(key);
        CHECK(rejection(j) == std::string(key) + " is missing");
    }
    j = fxTestJson();
    j["name"] = "";
    CHECK(rejection(j) == "name must not be empty");
    j = fxTestJson();
    j["tags"] = "texture";
    CHECK(rejection(j) == "tags must be a JSON array of strings");
    j = fxTestJson();
    j["author"] = 3;
    CHECK(rejection(j) == "author must be a string");
    j = fxTestJson();
    j["samples"] = {{"E", "x.flac"}};
    CHECK(rejection(j) == "unknown key samples.E");
    j = fxTestJson();
    j["samples"] = {{"A", ""}};
    CHECK(rejection(j) == "samples.A must not be empty");
    CHECK_THAT(rejection(nlohmann::json::array()), ContainsSubstring("must be a JSON object"));
    std::string error;
    CHECK_FALSE(gg::render::parsePresetSource("{", error).has_value());
    CHECK_THAT(error, ContainsSubstring("not valid JSON"));
}

TEST_CASE("a packed preset reads back as its source", "[render]")
{
    for (const char *name : {"fx-test.json", "inst-test.json"})
    {
        INFO(name);
        const PresetSource source = parseFixture(name);
        const Mode mode = source.info.product;
        const std::string bytes = pack(source);

        gg::core::PresetInfo info;
        gg::core::SessionState state;
        REQUIRE(gg::core::readPreset(bytes, mode, info, state));
        CHECK(info == source.info);
        CHECK(state.name == source.info.name);
        CHECK(state.seed == 0u);
        CHECK_FALSE(state.samples[0].has_value());

        // Every value its default but the source's.
        gg::core::SessionState expected = gg::core::initState(mode);
        for (const auto &[param, value] : source.params)
        {
            const auto slot = gg::render::paramSlotNamed(param);
            REQUIRE(slot.has_value());
            expected.values[static_cast<std::size_t>(*slot)] = static_cast<float>(value);
        }
        for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
        {
            INFO("slot " << s);
            CHECK(std::bit_cast<uint32_t>(state.values[s]) ==
                  std::bit_cast<uint32_t>(expected.values[s]));
        }
        // The factory twelve, and the source's route in its slot.
        CHECK(state.routes == gg::render::routesWith(gg::core::factoryRoutes(mode), source.mod));
        CHECK(state.routes[12].source != gg::core::ModSource::None);
        for (std::size_t k = 0; k < 12; ++k)
        {
            CHECK(state.routes[k] == gg::core::factoryRoutes(mode)[k]);
        }
    }
}

TEST_CASE("a packed preset names its samples by their content-relative reference", "[render]")
{
    // Spec §6.6, §6.12: with --content-dir the packer checks each sample exists and fills its
    // MEDI version 2 entry from the file: the reference, the fingerprint, format, channels, rate
    // and frames; no absolute or sample-folder path (they are the packing machine's).
    const auto root = contentWith("pack-samples");
    const PresetSource source = instWithSample(true);
    const std::string bytes = packWith(source, root);
    const auto hash = gg::media::hashFile(root / "Samples" / "Test" / "tone.wav");
    REQUIRE(hash.has_value());

    gg::core::PresetInfo info;
    gg::core::SessionState state;
    REQUIRE(gg::core::readPreset(bytes, Mode::Instrument, info, state));
    for (const uint8_t layer : {uint8_t{0}, uint8_t{2}})
    {
        INFO("layer " << int{layer});
        REQUIRE(state.samples[layer].has_value());
        const gg::core::MediaEntry &entry = *state.samples[layer];
        CHECK(entry.contentPath == kToneRef);
        CHECK(entry.sha256Hex == hash->sha256Hex);
        CHECK(entry.fileSize == hash->size);
        CHECK(entry.format == "wav");
        CHECK(entry.channels == 2u);
        CHECK(entry.sampleRate == 44100.0);
        CHECK(entry.frames == 44100);
        CHECK(entry.layer == layer);
        CHECK(entry.absolutePath.empty());
        CHECK(entry.relativePath.empty());
        CHECK_FALSE(entry.embed);
        CHECK(entry.audioId == 0u);
    }
    CHECK_FALSE(state.samples[1].has_value());
    CHECK_FALSE(state.samples[3].has_value());
    // The same bytes on every pack (nothing of the machine's in them).
    CHECK(packWith(source, root) == bytes);
}

TEST_CASE("a preset source's samples need --content-dir and a file under it", "[render]")
{
    const PresetSource source = instWithSample();
    std::string error;
    CHECK(gg::render::packPreset(source, std::nullopt, error).empty());
    CHECK_THAT(error, ContainsSubstring("--content-dir"));
    CHECK_THAT(error, ContainsSubstring(kToneRef));

    const auto empty = scratch("pack-missing") / "content";
    std::filesystem::create_directories(empty);
    CHECK(gg::render::packPreset(source, empty, error).empty());
    CHECK_THAT(error, ContainsSubstring(kToneRef));
    CHECK_THAT(error, ContainsSubstring("not found"));

    // A reference may not leave its root.
    auto j = nlohmann::json::parse(readText(fixture("inst-test.json")));
    j["samples"] = {{"A", "../content/Samples/Test/tone.wav"}};
    const auto outside = gg::render::parsePresetSource(j.dump(), error);
    REQUIRE(outside.has_value());
    CHECK(gg::render::packPreset(*outside, contentWith("pack-outside"), error).empty());
    CHECK_THAT(error, ContainsSubstring("isn't a valid content-relative path"));
}

TEST_CASE("a preset source may not set a ring parameter", "[render]")
{
    // The program manager's ruling: a preset load keeps each layer's ring parameters, so a
    // factory source sets none.
    for (const char *name :
         {"A Ring Length", "A Loop", "B Loop", "C Loop Length", "D Overdub", "A Splice"})
    {
        INFO(name);
        auto j = fxTestJson();
        j["params"][name] = 1;
        const std::string error = rejection(j);
        CHECK_THAT(error, ContainsSubstring(std::string("params.") + name));
        CHECK_THAT(error, ContainsSubstring("ring parameter"));
    }
}

TEST_CASE("a preset source may not set a MIDI setting", "[render]")
{
    // M3 sub-project 1's spec §4.1: a preset load keeps the instance's MPE, MPE Bend Range and
    // Bend Range, so a source sets none (exit 2 through gg-render --pack-preset).
    for (const char *name : {"MPE", "MPE Bend Range", "Bend Range"})
    {
        INFO(name);
        auto j = fxTestJson();
        j["params"][name] = 1;
        const std::string error = rejection(j);
        CHECK_THAT(error, ContainsSubstring(std::string("params.") + name));
        CHECK_THAT(error, ContainsSubstring("MIDI setting"));
    }
}

TEST_CASE("a render through a preset loads its samples from --content-dir", "[render]")
{
    // Spec §6.10: the preset's samples are resolved against --content-dir alone, loaded and
    // converted as a spec's file sample is: the same bytes as the spec that names the file.
    const auto root = contentWith("render-samples");
    const auto dir = scratch("render-samples");
    const auto withSample = dir / "with-sample.ggpreset";
    writeBytes(withSample, packWith(instWithSample(), root));
    const auto without = dir / "without.ggpreset";
    writeBytes(without, pack(parseFixture("inst-test.json")));

    auto viaPreset = nlohmann::json::parse(readText(renderFixture("render-inst-preset.json")));
    viaPreset.erase("sample"); // layer A's is the preset's
    viaPreset["preset"] = utf8(withSample);
    auto viaSpec = nlohmann::json::parse(readText(renderFixture("render-inst-preset.json")));
    viaSpec["preset"] = utf8(without);
    viaSpec["sample"] = {{"kind", "file"}, {"path", utf8(root / "Samples" / "Test" / "tone.wav")}};

    const gg::render::RenderOptions options{
        .contentDir = utf8(root), .capturesDir = {}, .saveStatePath = {}, .mediaDir = {}};
    std::vector<std::string> presetEvents, specEvents;
    const auto a = gg::render::render(parseSpec(viaPreset), &presetEvents, options);
    const auto b = gg::render::render(parseSpec(viaSpec), &specEvents, options);
    REQUIRE(a.channels.size() == 2);
    float level = 0.0f;
    for (const float v : a.channels[0])
    {
        level = std::max(level, std::abs(v));
    }
    CHECK(level > 0.01f);
    CHECK(bitIdentical(a.channels[0], b.channels[0]));
    CHECK(bitIdentical(a.channels[1], b.channels[1]));
    CHECK(presetEvents == specEvents);

    // Without --content-dir, or with one that lacks the file, it is bad input naming the
    // reference.
    CHECK_THAT(badInputOf(viaPreset), ContainsSubstring(kToneRef));
    CHECK_THAT(badInputOf(viaPreset), ContainsSubstring("--content-dir"));
    const auto empty = scratch("render-samples-empty");
    const auto missing = badInputOf(
        viaPreset,
        {.contentDir = utf8(empty), .capturesDir = {}, .saveStatePath = {}, .mediaDir = {}});
    CHECK_THAT(missing, ContainsSubstring(kToneRef));
    CHECK_THAT(missing, ContainsSubstring("not found"));
    // A file of other content under the reference has changed.
    const auto other = scratch("render-samples-other") / "content";
    std::filesystem::create_directories(other / "Samples" / "Test");
    gg::test::writeFile(
        other / "Samples" / "Test" / "tone.wav",
        gg::test::wavBytes(gg::test::makeTestAudio(1, 44100, 4410), gg::test::SampleFormat::Int16));
    CHECK_THAT(
        badInputOf(
            viaPreset,
            {.contentDir = utf8(other), .capturesDir = {}, .saveStatePath = {}, .mediaDir = {}}),
        ContainsSubstring("changed"));
}

TEST_CASE("an instrument spec without a sample needs a preset that names layer A's", "[render]")
{
    // The review's Minor 2: "sample" may be left out beside a preset only when the preset gives
    // layer A one; else the render would be silent, so it is bad input.
    const auto dir = scratch("render-no-sample");
    const auto without = dir / "without.ggpreset";
    writeBytes(without, pack(parseFixture("inst-test.json")));
    auto j = nlohmann::json::parse(readText(renderFixture("render-inst-preset.json")));
    j.erase("sample");
    j["preset"] = utf8(without);
    const std::string message = badInputOf(j);
    CHECK_THAT(message, ContainsSubstring("the instrument has no sample"));
    CHECK_THAT(message, ContainsSubstring("layer A"));
}

TEST_CASE("a render through a preset keeps the defaults' ring parameters", "[render]")
{
    // The program manager's ruling, on gg-render's preset path as in the plugins: a preset's ring
    // parameters are ignored (an old file may hold them), and the spec's own params still set
    // them.
    const auto dir = scratch("ring-params");
    gg::core::SessionState state = gg::core::initState(Mode::Fx);
    const auto at = [](gg::core::ParamSlot s) { return static_cast<std::size_t>(s); };
    state.values[at(gg::core::ParamSlot::RingLength)] = 1.0f;
    state.values[at(gg::core::ParamSlot::B_Loop)] = 1.0f;
    state.values[at(gg::core::ParamSlot::C_Overdub)] = 0.5f;
    state.values[at(gg::core::ParamSlot::Size)] = -1.0f;
    const auto preset = dir / "rings.ggpreset";
    writeBytes(preset, gg::core::encodePreset(parseFixture("fx-test.json").info, state));

    auto spec = fxSpecJson();
    spec["preset"] = utf8(preset);
    spec["params"] = {{"D Splice", 2}};
    const auto saved = dir / "saved.ggst";
    gg::render::render(
        parseSpec(spec), nullptr,
        {.contentDir = {}, .capturesDir = {}, .saveStatePath = utf8(saved), .mediaDir = {}});
    gg::core::SessionState out;
    REQUIRE(gg::core::decodeState(readText(saved), Mode::Fx, out));
    const gg::core::SessionState defaults = gg::core::initState(Mode::Fx);
    for (const auto s : {gg::core::ParamSlot::RingLength, gg::core::ParamSlot::B_Loop,
                         gg::core::ParamSlot::C_Overdub})
    {
        CHECK(out.values[at(s)] == defaults.values[at(s)]);
    }
    CHECK(out.values[at(gg::core::ParamSlot::Size)] == -1.0f);
    CHECK(out.values[at(gg::core::ParamSlot::D_Splice)] == 2.0f);
}

TEST_CASE("a render through a preset equals one that spells its values", "[render]")
{
    const auto dir = scratch("render");
    const auto presetPath = dir / "fx-test.ggpreset";
    writeBytes(presetPath, pack(parseFixture("fx-test.json")));

    auto withPreset = fxSpecJson();
    withPreset["preset"] = utf8(presetPath);
    auto spelled = fxSpecJson();
    spelled["params"] = {{"Size", -1.0}, {"Density", 5.0}, {"Layer B On", 1}, {"B Pitch", 7}};
    spelled["mod"] = nlohmann::json::array(
        {{{"slot", 13}, {"source", "LFO 1"}, {"target", "Position"}, {"depth", 0.1}}});
    const auto plain = fxSpecJson();

    std::vector<std::string> presetEvents, spelledEvents, plainEvents;
    const auto a = gg::render::render(parseSpec(withPreset), &presetEvents);
    const auto b = gg::render::render(parseSpec(spelled), &spelledEvents);
    const auto c = gg::render::render(parseSpec(plain), &plainEvents);
    REQUIRE(a.channels.size() == 2);
    REQUIRE(b.channels.size() == 2);
    CHECK(bitIdentical(a.channels[0], b.channels[0]));
    CHECK(bitIdentical(a.channels[1], b.channels[1]));
    CHECK(presetEvents == spelledEvents);
    CHECK_FALSE(presetEvents.empty());
    // ...and the preset changed something.
    CHECK_FALSE(bitIdentical(a.channels[0], c.channels[0]));

    // The spec's own params and mod go on top of the preset's.
    auto override = withPreset;
    override["params"] = {{"Density", 4.0}};
    override["mod"] = nlohmann::json::array({{{"slot", 13}, {"source", "None"}}});
    auto overrideSpelled = fxSpecJson();
    overrideSpelled["params"] = {
        {"Size", -1.0}, {"Density", 4.0}, {"Layer B On", 1}, {"B Pitch", 7}};
    const auto d = gg::render::render(parseSpec(override), nullptr);
    const auto e = gg::render::render(parseSpec(overrideSpelled), nullptr);
    CHECK(bitIdentical(d.channels[0], e.channels[0]));
    CHECK(bitIdentical(d.channels[1], e.channels[1]));
}

TEST_CASE("a preset for the other product, or that isn't one, is bad input", "[render]")
{
    const auto dir = scratch("wrong-product");
    const auto instPreset = dir / "inst-test.ggpreset";
    writeBytes(instPreset, pack(parseFixture("inst-test.json")));
    const auto fxPreset = dir / "fx-test.ggpreset";
    writeBytes(fxPreset, pack(parseFixture("fx-test.json")));

    const auto badInput = [](const nlohmann::json &j) {
        try
        {
            gg::render::render(parseSpec(j), nullptr);
        }
        catch (const gg::render::BadInput &e)
        {
            return std::string(e.what());
        }
        FAIL("render() didn't throw BadInput");
        return std::string();
    };

    auto fx = fxSpecJson();
    fx["preset"] = utf8(instPreset);
    CHECK_THAT(badInput(fx),
               ContainsSubstring("the preset is for Giga Grains, not Giga Grains FX"));

    auto inst = nlohmann::json::parse(readText(renderFixture("render-inst-preset.json")));
    inst["preset"] = utf8(fxPreset);
    CHECK_THAT(badInput(inst),
               ContainsSubstring("the preset is for Giga Grains FX, not Giga Grains"));
    // The instrument's own preset renders.
    inst["preset"] = utf8(instPreset);
    CHECK_FALSE(gg::render::render(parseSpec(inst), nullptr).channels[0].empty());

    // A missing file, a session state and bytes that are no state at all.
    fx["preset"] = utf8(dir / "missing.ggpreset");
    CHECK_THAT(badInput(fx), ContainsSubstring("can't read the preset"));
    const auto session = dir / "session.ggpreset";
    writeBytes(session, gg::core::encodeState(gg::core::initState(Mode::Fx)));
    fx["preset"] = utf8(session);
    CHECK_THAT(badInput(fx), ContainsSubstring("isn't a preset file"));
    const auto junk = dir / "junk.ggpreset";
    writeBytes(junk, "not a preset");
    fx["preset"] = utf8(junk);
    CHECK_THAT(badInput(fx), ContainsSubstring("isn't a preset file"));

    // A preset naming a sample by its absolute path alone: gg-render reads only content-relative
    // references (spec §6.4: it never searches).
    gg::core::SessionState withSample = gg::core::initState(Mode::Fx);
    withSample.samples[0] = gg::core::MediaEntry{.sha256Hex = std::string(64, 'a'),
                                                 .fileSize = 4,
                                                 .absolutePath = "/nowhere/a.wav",
                                                 .relativePath = "a.wav",
                                                 .format = "wav",
                                                 .channels = 1,
                                                 .sampleRate = 48000.0,
                                                 .frames = 1,
                                                 .embed = false,
                                                 .layer = 0,
                                                 .contentPath = "",
                                                 .audioId = 0};
    const auto sampled = dir / "sampled.ggpreset";
    writeBytes(sampled, gg::core::encodePreset(parseFixture("fx-test.json").info, withSample));
    fx["preset"] = utf8(sampled);
    CHECK_THAT(badInput(fx), ContainsSubstring("no content-relative reference"));
}

TEST_CASE("mutatePreset is one Mutate press of seed 0 under the default locks", "[render][mutate]")
{
    // gg-render --mutate-preset (the listening pack's ladder, spec §11): the preset's values as
    // the plugin's press would move them, with seed 0 and the default locks; the metadata, the
    // routes and the samples as they were.
    const auto content = contentWith("mutate-preset");
    for (const bool withSample : {false, true})
    {
        INFO("withSample " << withSample);
        const PresetSource source = withSample ? instWithSample() : parseFixture("fx-test.json");
        const Mode mode = source.info.product;
        const std::string bytes = withSample ? packWith(source, content) : pack(source);
        gg::core::PresetInfo inInfo;
        gg::core::SessionState in;
        REQUIRE(gg::core::readPreset(bytes, mode, inInfo, in));

        std::string error;
        const std::string mutated = gg::render::mutatePreset(bytes, 0.3f, 2, error);
        INFO(error);
        REQUIRE(error.empty());
        REQUIRE_FALSE(mutated.empty());
        CHECK(mutated != bytes);
        // Deterministic: the same input, amount and counter give the same bytes.
        CHECK(gg::render::mutatePreset(bytes, 0.3f, 2, error) == mutated);
        // Another counter is another press.
        CHECK(gg::render::mutatePreset(bytes, 0.3f, 3, error) != mutated);

        gg::core::PresetInfo info;
        gg::core::SessionState out;
        REQUIRE(gg::core::readPreset(mutated, mode, info, out));
        CHECK(info == inInfo);
        CHECK(out.routes == in.routes);
        CHECK(out.samples == in.samples);
        gg::core::ParamValues expected{};
        gg::core::mutate(in.values, gg::core::defaultMutateLocks(in.values, mode), 0.3f, 0, 2, mode,
                         expected);
        for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
        {
            INFO("slot " << s);
            CHECK(std::bit_cast<uint32_t>(out.values[s]) == std::bit_cast<uint32_t>(expected[s]));
        }
    }
}

TEST_CASE("mutatePreset at amount 0 rewrites the preset byte for byte", "[render][mutate]")
{
    const std::string bytes = pack(parseFixture("fx-test.json"));
    std::string error;
    CHECK(gg::render::mutatePreset(bytes, 0.0f, 1, error) == bytes);
    CHECK(error.empty());
}

TEST_CASE("mutatePreset refuses a bad amount and a file that isn't a preset", "[render][mutate]")
{
    const std::string bytes = pack(parseFixture("fx-test.json"));
    for (const float amount : {-0.01f, 1.01f, std::nanf("")})
    {
        INFO("amount " << amount);
        std::string error;
        CHECK(gg::render::mutatePreset(bytes, amount, 1, error).empty());
        CHECK_THAT(error, Catch::Matchers::ContainsSubstring("amount"));
    }
    std::string error;
    CHECK(gg::render::mutatePreset("not a preset", 0.1f, 1, error).empty());
    CHECK_THAT(error, Catch::Matchers::ContainsSubstring("isn't a preset file"));
    // A session state is not a preset.
    CHECK(gg::render::mutatePreset(gg::core::encodeState(gg::core::initState(Mode::Fx)), 0.1f, 1,
                                   error)
              .empty());
    CHECK_THAT(error, Catch::Matchers::ContainsSubstring("isn't a preset file"));
}
