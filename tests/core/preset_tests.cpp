// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The preset file (M2 sub-project 5's spec §6.1): a GGST container whose PTCH carries the
// preset's name in the root's `name` and its metadata in a <preset> element, written by
// encodePreset and read by readPreset and presetInfoOf (gg/core/Preset.h), and the tag
// vocabulary (spec §6.7, gg/core/PresetInfo.h). Like session_state_tests.cpp, this file never
// includes sst-plugininfra or tinyxml (ruling M1-l); the containers encodePreset never writes it
// builds with support/StateContainer.h.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/PatchState.h"
#include "gg/core/Preset.h"
#include "gg/core/PresetInfo.h"
#include "gg/core/SessionState.h"
#include "gg/media/Sha256.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "support/StateContainer.h"

using gg::core::decodeState;
using gg::core::encodePreset;
using gg::core::encodeState;
using gg::core::factoryRoutes;
using gg::core::fromStateXml;
using gg::core::initState;
using gg::core::kModSlots;
using gg::core::kParamCount;
using gg::core::Mode;
using gg::core::ModRoutes;
using gg::core::ParamSlot;
using gg::core::ParamValues;
using gg::core::PresetInfo;
using gg::core::presetInfoOf;
using gg::core::readPreset;
using gg::core::SessionState;
using gg::test::stateChunk;
using gg::test::stateChunkPayload;
using gg::test::stateContainer;

namespace
{
std::array<uint32_t, kParamCount> bitsOf(const ParamValues &v)
{
    std::array<uint32_t, kParamCount> b{};
    for (std::size_t i = 0; i < kParamCount; ++i)
    {
        b[i] = std::bit_cast<uint32_t>(v[i]);
    }
    return b;
}

bool sameRoutes(const ModRoutes &a, const ModRoutes &b)
{
    for (std::size_t k = 0; k < kModSlots; ++k)
    {
        if (!(a[k] == b[k]) ||
            std::bit_cast<uint32_t>(a[k].depth) != std::bit_cast<uint32_t>(b[k].depth))
        {
            return false;
        }
    }
    return true;
}

std::size_t at(ParamSlot s) { return static_cast<std::size_t>(s); }

// Four tags, a non-ASCII description holding the characters XML escapes (and a literal character
// reference, which must come back as its text), and a name that needs escaping too.
PresetInfo someInfo()
{
    PresetInfo info;
    info.name = "Glass Hold \xe2\x9c\x93 &#x41; <\"x\">";
    info.product = Mode::Fx;
    info.author = "Giga Grains & friends";
    info.description =
        "A held, glassy freeze \xc3\xa4 that repitches to the keys: \"&amp;\" 'q'\ttab";
    info.tags = {"texture", "freeze", "midi", "sample"};
    return info;
}

// The FX's defaults with a few values moved and three routes of a user's on top of none.
SessionState someState()
{
    SessionState s = initState(Mode::Fx);
    s.values[at(ParamSlot::Position)] = 0.8f;
    s.values[at(ParamSlot::Pitch)] = -6.0f;
    s.values[at(ParamSlot::Size)] = 1.23e-5f;
    s.seed = 1234;
    s.routes = ModRoutes{};
    s.routes[0] = {gg::core::ModSource::Lfo1,
                   gg::core::kSameLayer,
                   ParamSlot::Pitch,
                   gg::core::kAllLayers,
                   0.5f,
                   gg::core::ModCurve::Bipolar};
    s.routes[7] = {gg::core::ModSource::Macro3, gg::core::kSameLayer, ParamSlot::Size, 0, -1.25f,
                   gg::core::ModCurve::Unipolar};
    s.routes[47] = {
        gg::core::ModSource::Velocity, gg::core::kSameLayer, ParamSlot::ReverbAmount,
        gg::core::kAllLayers,          1.2345678e-7f,        gg::core::ModCurve::TopQuarter};
    return s;
}

// A value of every field unlike what a read would write, to see that a failed read touches
// nothing.
SessionState untouchedState()
{
    SessionState s;
    s.values.fill(0.125f);
    s.seed = 99;
    s.name = "untouched";
    s.preset = PresetInfo{"p", Mode::Instrument, "a", "d", {"dark"}};
    return s;
}

PresetInfo untouchedInfo() { return PresetInfo{"q", Mode::Instrument, "b", "e", {"bright"}}; }

// `bytes` with its PTCH payload replaced by `ptch` (the rest as it was).
std::string withPatch(const std::string &bytes, const std::string &ptch)
{
    return stateContainer(
        {stateChunk("PTCH", ptch), stateChunk("MEDI", *stateChunkPayload(bytes, "MEDI"))});
}

std::string replaced(std::string s, std::string_view from, std::string_view to)
{
    const auto i = s.find(from);
    REQUIRE(i != std::string::npos);
    s.replace(i, from.size(), to);
    return s;
}

std::string repeated(std::string_view s, std::size_t n)
{
    std::string out;
    for (std::size_t i = 0; i < n; ++i)
    {
        out += s;
    }
    return out;
}
} // namespace

TEST_CASE("a preset round trips through encodePreset and readPreset", "[core][preset]")
{
    const PresetInfo info = someInfo();
    const SessionState state = someState();
    const std::string bytes = encodePreset(info, state);

    // A container like any state: PTCH then MEDI, no AUDI.
    REQUIRE(bytes.starts_with("GGST"));
    CHECK(stateChunkPayload(bytes, "AUDI") == std::nullopt);
    const std::string ptch = *stateChunkPayload(bytes, "PTCH");
    CHECK(ptch.find("<preset product=\"fx\"") != std::string::npos);
    CHECK(ptch.find("tags=\"texture freeze midi sample\"") != std::string::npos);
    CHECK(ptch.find("<engine seed=\"0\" />") != std::string::npos); // a preset's seed is 0

    PresetInfo readInfo;
    SessionState out = untouchedState();
    REQUIRE(readPreset(bytes, Mode::Fx, readInfo, out));
    CHECK(readInfo == info);
    CHECK(out.name == info.name);
    REQUIRE(out.preset.has_value());
    CHECK(*out.preset == info);
    CHECK(bitsOf(out.values) == bitsOf(state.values));
    CHECK(sameRoutes(out.routes, state.routes));
    CHECK(out.seed == 0u);
    CHECK_FALSE(out.samples[0].has_value());

    // Saving what was read writes the same bytes again.
    CHECK(encodePreset(readInfo, out) == bytes);
    CHECK(encodeState(out) == bytes);

    // The instrument's preset reads in the instrument.
    PresetInfo inst = info;
    inst.product = Mode::Instrument;
    REQUIRE(readPreset(encodePreset(inst, state), Mode::Instrument, readInfo, out));
    CHECK(readInfo == inst);
}

TEST_CASE("a session state carries its name and its preset element", "[core][preset]")
{
    SessionState s = someState();
    CHECK(s.name == "Init");
    CHECK_FALSE(s.preset.has_value());
    s.name = "My session";
    s.preset = someInfo();
    s.preset->name = "ignored: the root's name is the state's";
    SessionState out = untouchedState();
    REQUIRE(decodeState(encodeState(s), Mode::Fx, out));
    CHECK(out.name == "My session");
    REQUIRE(out.preset.has_value());
    PresetInfo expected = someInfo();
    expected.name = "My session";
    CHECK(*out.preset == expected);

    // No name in the XML reads "Init"; no <preset> reads none.
    const std::string bytes = encodeState(someState());
    const std::string ptch = *stateChunkPayload(bytes, "PTCH");
    REQUIRE(decodeState(withPatch(bytes, replaced(ptch, " name=\"Init\"", "")), Mode::Fx, out));
    CHECK(out.name == "Init");
    CHECK_FALSE(out.preset.has_value());

    // The session's PTCH is toStateXml's; its bytes are pinned below (T1).
    CHECK(ptch == gg::core::toStateXml(someState().values, 1234, someState().routes));
}

namespace
{
std::string sha256Of(std::string_view bytes)
{
    return gg::media::sha256Hex(std::as_bytes(std::span(bytes.data(), bytes.size())));
}
} // namespace

TEST_CASE("a session state's bytes are pinned", "[core][preset]")
{
    // T1: the bytes a session state writes, PTCH and whole container, as literals (their size
    // and SHA-256, taken at the group 1 fix wave), so a change to how a session is written fails
    // here rather than passing a comparison with the code's own output. The edited mark (D-Q2)
    // is written only for an edited preset, so these unedited bytes are unchanged by it. Retaken
    // at the merge of sub-project 3, whose five parameters (A to D Quality, Budget) add five
    // <p> elements, 258 bytes: without them the bytes are the group 1 fix wave's (11,755,
    // c3d26f4e...; 11,808, ed5e43d5...). Retaken again at the merge of sub-project 2, whose twenty
    // ring parameters (A to D Ring Length, Loop, Loop Length, Overdub and Splice) add twenty <p>
    // elements to PTCH, 1,035 bytes, and whose container writes MEDI version 2 and an INST chunk,
    // 56 bytes more: before it the bytes were sub-project 3's merge's (12,013, 875d7ac8...;
    // 12,066, 37dd926d...). Retaken again at the merge of sub-projects 2 and 4, whose forty
    // parameters (eight per layer, the delay's five and the limiter's three) add forty <p>
    // elements, 2,047 bytes, to sub-project 2's merge's (13,048, 6e2115f1...; 13,157,
    // 5573ae15...). Retaken once more for M3 sub-project 1's seven parameters (A to D Mode, MPE,
    // MPE Bend Range and Bend Range), 352 bytes (15,095, 765ee8fb...; 15,204, b66606a7...), and
    // once more for M3 sub-project 3b's twelve (A to D Scan Speed, Edge and Track), 621 bytes
    // (15,447, 252fd91e...; 15,556, 79706b53...).
    const std::string session = encodeState(someState());
    const std::string ptch = *stateChunkPayload(session, "PTCH");
    CHECK(ptch.size() == 16068);
    CHECK(sha256Of(ptch) == "0eefa7889b64cae02bde8c933d1edd70824958a11ba2c0dd37736ef719045e36");
    CHECK(session.size() == 16177);
    CHECK(sha256Of(session) == "c0e794946c6b9d5e3bbf972294dcedcf6390859b47a3ea6cd8d05ac6a5fd871a");
}

TEST_CASE("a name read back is cut at 255 bytes and at a NUL", "[core][preset]")
{
    // T1: the read side's cut, from a PTCH written with a longer name than any writer writes.
    const std::string bytes = encodeState(someState());
    const std::string ptch = *stateChunkPayload(bytes, "PTCH");
    const std::string longName = std::string(254, 'n') + "\xe2\x9c\x93" + "tail";
    SessionState out = untouchedState();
    REQUIRE(decodeState(
        withPatch(bytes, replaced(ptch, " name=\"Init\"", " name=\"" + longName + "\"")), Mode::Fx,
        out));
    CHECK(out.name == std::string(254, 'n')); // the straddling character is dropped whole
    // The write side cuts a name at its first NUL.
    SessionState named = someState();
    named.name = std::string("ab\0cd", 5);
    REQUIRE(decodeState(encodeState(named), Mode::Fx, out));
    CHECK(out.name == "ab");
}

TEST_CASE("a name past 255 bytes is cut at a UTF-8 boundary", "[core][preset]")
{
    // 254 ASCII bytes, then a three-byte character that would straddle byte 255.
    PresetInfo info = someInfo();
    info.name = std::string(254, 'n') + "\xe2\x9c\x93" + "tail";
    PresetInfo readInfo;
    SessionState out;
    const std::string bytes = encodePreset(info, someState());
    REQUIRE(readPreset(bytes, Mode::Fx, readInfo, out));
    CHECK(readInfo.name == std::string(254, 'n'));
    CHECK(out.name == readInfo.name);
    PresetInfo meta;
    REQUIRE(presetInfoOf(bytes, meta));
    CHECK(meta.name == readInfo.name);

    // Exactly 255 bytes, ending on a whole character, is kept.
    info.name = std::string(252, 'n') + "\xe2\x9c\x93";
    REQUIRE(readPreset(encodePreset(info, someState()), Mode::Fx, readInfo, out));
    CHECK(readInfo.name == info.name);
}

TEST_CASE("fromStateXml without the preset out-parameter ignores the element", "[core][preset]")
{
    const SessionState state = someState();
    const std::string bytes = encodePreset(someInfo(), state);
    const std::string ptch = *stateChunkPayload(bytes, "PTCH");
    ParamValues values{};
    uint64_t seed{77};
    ModRoutes routes{};
    REQUIRE(fromStateXml(ptch, values, seed, routes, Mode::Fx));
    CHECK(bitsOf(values) == bitsOf(state.values));
    CHECK(sameRoutes(routes, state.routes));
    CHECK(seed == 0u);

    // With it, the element and the name come back.
    std::string name;
    std::optional<PresetInfo> preset;
    REQUIRE(fromStateXml(ptch, values, seed, routes, Mode::Fx, nullptr, &name, &preset));
    CHECK(name == someInfo().name);
    REQUIRE(preset.has_value());
    CHECK(*preset == someInfo());
}

TEST_CASE("a version-1 patch with a preset element loads through the migration unchanged",
          "[core][preset]")
{
    constexpr std::string_view kHead =
        R"(<patch id="org.sethbc.giga-grains.patch" version="1" name="Old"><params>)"
        R"(<p id="1" v="0.800000011921" bits="3f4ccccd" />)"
        R"(<p id="65537" v="0.100000001490" bits="3dcccccd" />)"
        R"(</params><engine seed="77" />)";
    const std::string plain = std::string(kHead) + "</patch>";
    const std::string withElement =
        std::string(kHead) +
        R"(<preset product="instrument" author="M1" description="d" tags="pad" /></patch>)";
    for (const Mode mode : {Mode::Instrument, Mode::Fx})
    {
        ParamValues a{};
        ParamValues b{};
        uint64_t seedA{0};
        uint64_t seedB{0};
        ModRoutes routesA{};
        ModRoutes routesB{};
        uint32_t migratedA{0};
        uint32_t migratedB{0};
        REQUIRE(fromStateXml(plain, a, seedA, routesA, mode, &migratedA));
        REQUIRE(fromStateXml(withElement, b, seedB, routesB, mode, &migratedB));
        CHECK(bitsOf(a) == bitsOf(b));
        CHECK(seedA == seedB);
        CHECK(sameRoutes(routesA, routesB));
        CHECK(sameRoutes(routesB, factoryRoutes(mode)));
        CHECK(migratedA == 1u);
        CHECK(migratedB == 1u);
    }
}

TEST_CASE("readPreset refuses what isn't this product's preset and writes nothing",
          "[core][preset]")
{
    const std::string preset = encodePreset(someInfo(), someState());
    const auto refused = [](std::string_view bytes, Mode mode) {
        PresetInfo info = untouchedInfo();
        SessionState out = untouchedState();
        const bool ok = readPreset(bytes, mode, info, out);
        CHECK(info == untouchedInfo());
        CHECK(out.name == "untouched");
        CHECK(out.seed == 99u);
        CHECK(out.values[0] == 0.125f);
        return !ok;
    };
    SECTION("not a preset: a session state") { CHECK(refused(encodeState(someState()), Mode::Fx)); }
    SECTION("the other product") { CHECK(refused(preset, Mode::Instrument)); }
    SECTION("a truncated container")
    {
        CHECK(refused(std::string_view(preset).substr(0, preset.size() - 1), Mode::Fx));
    }
    SECTION("an unknown product")
    {
        const std::string ptch = *stateChunkPayload(preset, "PTCH");
        CHECK(refused(withPatch(preset, replaced(ptch, "product=\"fx\"", "product=\"midi-fx\"")),
                      Mode::Fx));
        PresetInfo info;
        CHECK_FALSE(presetInfoOf(
            withPatch(preset, replaced(ptch, "product=\"fx\"", "product=\"\"")), info));
    }
}

TEST_CASE("presetInfoOf reads the metadata alone, of either product", "[core][preset]")
{
    const PresetInfo info = someInfo();
    const std::string bytes = encodePreset(info, someState());
    PresetInfo meta = untouchedInfo();
    REQUIRE(presetInfoOf(bytes, meta));
    CHECK(meta == info);

    PresetInfo inst = info;
    inst.product = Mode::Instrument;
    REQUIRE(presetInfoOf(encodePreset(inst, someState()), meta));
    CHECK(meta == inst);

    // Values it never reads: garbage there doesn't stop it.
    const std::string ptch = *stateChunkPayload(bytes, "PTCH");
    std::string garbage = ptch;
    garbage.replace(garbage.find("<params>"),
                    garbage.find("</params>") + 9 - garbage.find("<params>"),
                    R"(<params><p id="x" v="not a number" bits="zz" /><p /></params>)");
    REQUIRE(presetInfoOf(withPatch(bytes, garbage), meta));
    CHECK(meta == info);

    // No preset element, or not a container: false, and `meta` is untouched.
    meta = untouchedInfo();
    CHECK_FALSE(presetInfoOf(encodeState(someState()), meta));
    CHECK_FALSE(presetInfoOf(std::string_view(bytes).substr(0, bytes.size() - 1), meta));
    CHECK_FALSE(presetInfoOf("", meta));
    CHECK_FALSE(presetInfoOf("GGST", meta));
    CHECK(meta == untouchedInfo());
}

TEST_CASE("a preset past the cap or the nesting limit is refused", "[core][preset]")
{
    const std::string bytes = encodePreset(someInfo(), someState());
    const std::string ptch = *stateChunkPayload(bytes, "PTCH");
    const std::string medi = *stateChunkPayload(bytes, "MEDI");
    PresetInfo info;
    SessionState out;

    SECTION("a PTCH 65 deep")
    {
        // The root is level 1: 63 nested elements inside it read, 64 don't.
        const auto nested = [&](std::size_t n) {
            return replaced(ptch, "</patch>",
                            repeated("<a>", n) + repeated("</a>", n) + "</patch>");
        };
        const auto at64 =
            stateContainer({stateChunk("PTCH", nested(63)), stateChunk("MEDI", medi)});
        CHECK(presetInfoOf(at64, info));
        CHECK(readPreset(at64, Mode::Fx, info, out));
        const auto at65 =
            stateContainer({stateChunk("PTCH", nested(64)), stateChunk("MEDI", medi)});
        CHECK_FALSE(presetInfoOf(at65, info));
        CHECK_FALSE(readPreset(at65, Mode::Fx, info, out));
    }
    SECTION("kMaxStateBytes + 1 bytes")
    {
        // The preset, padded with an unknown chunk to exactly the cap, reads; one byte more
        // doesn't.
        const std::size_t padding = gg::core::kMaxStateBytes - bytes.size() - 12;
        const std::string atCap = bytes + stateChunk("PADD", std::string(padding, '\0'));
        REQUIRE(atCap.size() == gg::core::kMaxStateBytes);
        CHECK(presetInfoOf(atCap, info));
        CHECK(readPreset(atCap, Mode::Fx, info, out));
        const std::string overCap = bytes + stateChunk("PADD", std::string(padding + 1, '\0'));
        CHECK_FALSE(presetInfoOf(overCap, info));
        CHECK_FALSE(readPreset(overCap, Mode::Fx, info, out));
    }
}

TEST_CASE("the tag vocabulary is the spec's 28 words", "[core][preset]")
{
    constexpr std::array<std::string_view, 28> kWords{
        "texture", "playable", "rhythmic", "extreme", "pad",   "drone",      "freeze",
        "smear",   "stretch",  "glitch",   "keys",    "pluck", "percussive", "voice",
        "strings", "winds",    "brass",    "metal",   "noise", "dark",       "bright",
        "wide",    "mono",     "reverb",   "dry",     "midi",  "sample",     "sidechain"};
    const auto vocabulary = gg::core::presetTagVocabulary();
    REQUIRE(vocabulary.size() == kWords.size());
    for (std::size_t i = 0; i < kWords.size(); ++i)
    {
        CHECK(vocabulary[i] == kWords[i]);
        CHECK(gg::core::isPresetTag(kWords[i]));
    }
    CHECK_FALSE(gg::core::isPresetTag("Texture"));
    CHECK_FALSE(gg::core::isPresetTag("midi-fx"));
    CHECK_FALSE(gg::core::isPresetTag(""));

    CHECK(gg::core::kPresetExtension == ".ggpreset");
    CHECK(gg::core::kInitLoadKey == "init");
    CHECK(std::string_view(gg::core::modeName(Mode::Fx)) == "fx");
    CHECK(std::string_view(gg::core::modeName(Mode::Instrument)) == "instrument");
    CHECK(gg::core::modeNamed("fx") == Mode::Fx);
    CHECK(gg::core::modeNamed("instrument") == Mode::Instrument);
    CHECK(gg::core::modeNamed("FX") == std::nullopt);
    CHECK(gg::core::modeNamed("") == std::nullopt);
}

TEST_CASE("a tag outside the vocabulary is dropped on read", "[core][preset]")
{
    const std::string bytes = encodePreset(someInfo(), someState());
    const std::string ptch = *stateChunkPayload(bytes, "PTCH");
    const std::string edited =
        replaced(ptch, "tags=\"texture freeze midi sample\"",
                 "tags=\" texture Texture  bogus freeze midi-fx live-input sample \"");
    PresetInfo info;
    SessionState out;
    REQUIRE(readPreset(withPatch(bytes, edited), Mode::Fx, info, out));
    CHECK(info.tags == std::vector<std::string>{"texture", "freeze", "sample"});
    REQUIRE(presetInfoOf(withPatch(bytes, edited), info));
    CHECK(info.tags == std::vector<std::string>{"texture", "freeze", "sample"});

    // A missing attribute reads empty.
    const std::string minimal =
        ptch.substr(0, ptch.find("<preset")) + R"(<preset product="fx" /></patch>)";
    REQUIRE(readPreset(withPatch(bytes, minimal), Mode::Fx, info, out));
    CHECK(info.author.empty());
    CHECK(info.description.empty());
    CHECK(info.tags.empty());
    CHECK(info.name == someInfo().name);
}

TEST_CASE("initState is every default and the factory routes", "[core][preset]")
{
    // What a new plugin instance holds: paramMeta's defaults, factoryRoutes, seed 0, no sample.
    // Not compared with a state built the way initState builds it (T1): a few defaults by value,
    // and both products' INIT bytes by size and SHA-256 (taken at the group 1 fix wave, and
    // retaken at the merge of sub-project 3: its five parameters add 258 bytes, without which
    // they are the fix wave's, 13,123 f5bebe3b... and 13,124 4b56d092...; retaken again at the
    // merge of sub-project 2: its twenty ring parameters and its container's MEDI version 2 and
    // INST add 1,091 bytes, without which they are sub-project 3's merge's, 13,381 d3fce75c...
    // and 13,382 ffdb8cc2...; and retaken at the merge of sub-projects 2 and 4: sub-project 4's
    // forty parameters add 2,047 bytes to sub-project 2's merge's, 14,472 5c5c3e20... and 14,473
    // 58a43b60...); retaken at M3 Voice's Task 4: the five voice-only factory routes (44 to 48)
    // add 784 bytes, so INIT's are 17,303 7211d643... and 17,304 36b032f0..., and without them the
    // pins above stand), and again for sub-project 1's seven
    // parameters, 352 bytes more, 17,655 5337a57b... and 17,656 81fb0c52...).
    const SessionState fx = initState(Mode::Fx);
    CHECK(fx.values[at(ParamSlot::Size)] ==
          gg::core::paramMeta(ParamSlot::Size, Mode::Fx).defaultVal);
    CHECK(fx.values[at(ParamSlot::LayerAOn)] == 1.0f);
    CHECK(fx.values[at(ParamSlot::LayerBOn)] == 0.0f);
    CHECK(fx.routes == factoryRoutes(Mode::Fx));
    const std::string fxBytes = encodeState(fx);
    CHECK(fxBytes.size() == 18276);
    CHECK(sha256Of(fxBytes) == "9b40d46079751e771ff5564dd2d37053cdf814c12b40716dce0b766997d944fd");
    const std::string instBytes = encodeState(initState(Mode::Instrument));
    CHECK(instBytes.size() == 18277);
    CHECK(sha256Of(instBytes) ==
          "1e313f780b9fac0c3882dce9d752fe4d919cd6f62d61b2766ec8da450c7293e4");
    CHECK(fx.name == "Init");
    CHECK_FALSE(fx.preset.has_value());
    CHECK_FALSE(fx.samples[0].has_value());
    CHECK(fx.seed == 0u);

    // The instrument's differs in each layer's Position (0% of the sample, where the FX's is
    // 0.25 s behind the write head) and Source (Sample, where the FX's is Live), and in the
    // Macro 8 route alone.
    const SessionState inst = initState(Mode::Instrument);
    std::vector<std::size_t> differentValues;
    for (std::size_t i = 0; i < kParamCount; ++i)
    {
        if (std::bit_cast<uint32_t>(fx.values[i]) != std::bit_cast<uint32_t>(inst.values[i]))
        {
            differentValues.push_back(i);
        }
    }
    std::vector<std::size_t> expected;
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        expected.push_back(at(gg::core::layerSlot(gg::core::LayerParam::Position, layer)));
        expected.push_back(at(gg::core::layerSlot(gg::core::LayerParam::Source, layer)));
    }
    std::sort(expected.begin(), expected.end());
    CHECK(differentValues == expected);
    std::vector<std::size_t> differentRoutes;
    for (std::size_t k = 0; k < kModSlots; ++k)
    {
        if (!(fx.routes[k] == inst.routes[k]))
        {
            differentRoutes.push_back(k);
        }
    }
    CHECK(differentRoutes == std::vector<std::size_t>{11}); // slot 12: Macro 8 to Mix or Level
    CHECK(fx.routes[11].source == gg::core::ModSource::Macro8);
    CHECK(inst.routes[11].source == gg::core::ModSource::Macro8);
}

// ---- The director's decision D-Q2 (2026-10-30): a session saves the edited mark ----

TEST_CASE("a session saved while its preset is edited carries the mark back", "[core][preset]")
{
    SessionState s = someState();
    s.name = "Glass Hold";
    s.edited = true;
    const std::string bytes = encodeState(s);
    const std::string ptch = *stateChunkPayload(bytes, "PTCH");
    CHECK(ptch.find(" edited=\"1\"") != std::string::npos); // on the PTCH root
    CHECK(ptch.find("<preset") == std::string::npos);       // still a session, not a preset
    SessionState out = untouchedState();
    REQUIRE(decodeState(bytes, Mode::Fx, out));
    CHECK(out.edited);
    CHECK(out.name == "Glass Hold");
    CHECK_FALSE(out.preset.has_value());

    // Unedited, nothing is written: the bytes are a session's as before (pinned above).
    s.edited = false;
    const std::string plain = encodeState(s);
    CHECK(plain.find("edited") == std::string::npos);
    REQUIRE(decodeState(plain, Mode::Fx, out));
    CHECK_FALSE(out.edited);
    // A preset file never carries it, whatever the state it is made from says.
    SessionState edited = someState();
    edited.edited = true;
    CHECK(encodePreset(someInfo(), edited).find("edited") == std::string::npos);
}

TEST_CASE("a reader that doesn't know the edited mark reads the state unchanged", "[core][preset]")
{
    // An older build reads the PTCH through fromStateXml with no edited output: the values, seed,
    // routes and name are what it reads of the unmarked state.
    SessionState s = someState();
    s.name = "Glass Hold";
    const std::string plainPtch = *stateChunkPayload(encodeState(s), "PTCH");
    s.edited = true;
    const std::string markedPtch = *stateChunkPayload(encodeState(s), "PTCH");
    REQUIRE(markedPtch != plainPtch);
    ParamValues plainValues{}, markedValues{};
    uint64_t plainSeed = 0, markedSeed = 0;
    ModRoutes plainRoutes{}, markedRoutes{};
    std::string plainName, markedName;
    REQUIRE(gg::core::fromStateXml(plainPtch, plainValues, plainSeed, plainRoutes, Mode::Fx,
                                   nullptr, &plainName));
    REQUIRE(gg::core::fromStateXml(markedPtch, markedValues, markedSeed, markedRoutes, Mode::Fx,
                                   nullptr, &markedName));
    CHECK(bitsOf(markedValues) == bitsOf(plainValues));
    CHECK(markedSeed == plainSeed);
    CHECK(sameRoutes(markedRoutes, plainRoutes));
    CHECK(markedName == plainName);
}

// ---- Group 2: samples by their content-relative reference, and what a preset never carries ----

TEST_CASE("a preset names a sample by its content-relative reference", "[core][preset]")
{
    // MEDI version 2's reference (MediaEntry::contentPath, written as content="…"): a factory
    // preset's entry has no absolute or sample-folder path, only the reference and the file's
    // fingerprint and shape (spec §6.1, §6.12).
    SessionState state = someState();
    gg::core::MediaEntry entry;
    entry.sha256Hex = std::string(64, 'c');
    entry.fileSize = 123456;
    entry.format = "flac";
    entry.channels = 2;
    entry.sampleRate = 44100.0;
    entry.frames = 30000;
    entry.layer = 1;
    entry.contentPath = "Samples/Strings/cello \xc3\xa9 & bow.flac";
    state.samples[1] = entry;
    const std::string bytes = encodePreset(someInfo(), state);
    const std::string media = *stateChunkPayload(bytes, "MEDI");
    CHECK(media.find("version=\"2\"") != std::string::npos);
    CHECK(media.find("content=\"Samples/Strings/cello \xc3\xa9 &#x26; bow.flac\"") !=
          std::string::npos);
    CHECK(stateChunkPayload(bytes, "AUDI") == std::nullopt);

    PresetInfo info;
    SessionState out;
    REQUIRE(readPreset(bytes, Mode::Fx, info, out));
    REQUIRE(out.samples[1].has_value());
    CHECK(*out.samples[1] == entry);
    CHECK_FALSE(out.samples[0].has_value());
    CHECK(encodePreset(info, out) == bytes);
}

TEST_CASE("a preset carries no ring, no embedded audio and the default cap", "[core][preset]")
{
    // A preset never embeds audio and carries no ring or cap (spec §6.1; D5): encodePreset leaves
    // them out whatever the state holds, so its container is PTCH and MEDI alone.
    SessionState state = someState();
    gg::core::MediaEntry filed;
    filed.sha256Hex = std::string(64, 'a');
    filed.fileSize = 10;
    filed.absolutePath = "/samples/a.wav";
    filed.format = "wav";
    filed.channels = 1;
    filed.sampleRate = 48000.0;
    filed.frames = 1;
    filed.layer = 0;
    state.samples[0] = filed;
    gg::core::MediaEntry capture = filed;
    capture.absolutePath.clear();
    capture.embed = true;
    capture.audioId = 2;
    capture.layer = 2;
    state.samples[2] = capture;
    gg::core::RingEntry ring;
    ring.layer = 1;
    ring.frames = 4;
    ring.sampleRate = 48000.0;
    ring.sha256Hex = std::string(64, 'b');
    ring.fileSize = 60;
    ring.embed = true;
    ring.audioId = 1;
    state.rings[1] = ring;
    state.audio = {{1, "ring bytes"}, {2, "capture bytes"}};
    state.memoryCapMiB = 4096;
    const std::string bytes = encodePreset(someInfo(), state);
    CHECK(stateChunkPayload(bytes, "AUDI") == std::nullopt);

    PresetInfo info;
    SessionState out;
    REQUIRE(readPreset(bytes, Mode::Fx, info, out));
    CHECK(out.samples[0] == filed);
    CHECK_FALSE(out.samples[2].has_value());
    for (const auto &r : out.rings)
    {
        CHECK_FALSE(r.has_value());
    }
    CHECK(out.audio.empty());
    CHECK(out.memoryCapMiB == gg::core::kMemoryCapDefaultMiB);
}

TEST_CASE("the ring parameters are each layer's five", "[core][preset]")
{
    // Ring Length, Loop, Loop Length, Overdub and Splice: capture and memory, which a preset load
    // keeps (the program manager's ruling) and Mutate locks.
    std::vector<ParamSlot> rings;
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        if (gg::core::isRingParam(static_cast<ParamSlot>(s)))
        {
            rings.push_back(static_cast<ParamSlot>(s));
        }
    }
    REQUIRE(rings.size() == 5 * gg::core::kLayers);
    for (const ParamSlot s :
         {ParamSlot::RingLength, ParamSlot::Loop, ParamSlot::LoopLength, ParamSlot::Overdub,
          ParamSlot::Splice, ParamSlot::B_RingLength, ParamSlot::C_Overdub, ParamSlot::D_Splice})
    {
        CHECK(std::find(rings.begin(), rings.end(), s) != rings.end());
    }
    CHECK_FALSE(gg::core::isRingParam(ParamSlot::Freeze));
    CHECK_FALSE(gg::core::isRingParam(ParamSlot::Budget));
    CHECK_FALSE(gg::core::isRingParam(ParamSlot::Count));

    // keepRingParams copies those slots from the instance and leaves every other one.
    ParamValues current{};
    current.fill(0.25f);
    ParamValues loaded{};
    loaded.fill(0.75f);
    gg::core::keepRingParams(current, loaded);
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        INFO("slot " << s);
        const auto slot = static_cast<ParamSlot>(s);
        CHECK(loaded[s] ==
              (gg::core::isRingParam(slot) || gg::core::isMidiSetting(slot) ? 0.25f : 0.75f));
    }

    // The three MIDI settings are kept the same way (M3 sub-project 1's spec §4.1).
    for (const ParamSlot s : {ParamSlot::Mpe, ParamSlot::MpeBendRange, ParamSlot::BendRange})
    {
        CHECK(gg::core::isMidiSetting(s));
        CHECK(loaded[static_cast<std::size_t>(s)] == 0.25f);
    }
    CHECK_FALSE(gg::core::isMidiSetting(ParamSlot::Budget));
    CHECK_FALSE(gg::core::isMidiSetting(ParamSlot::Count));

    // keepRingLengths (INIT's) copies each layer's Ring Length and the three MIDI settings.
    loaded.fill(0.75f);
    gg::core::keepRingLengths(current, loaded);
    std::size_t kept = 0;
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        INFO("slot " << s);
        const auto ref = gg::core::layerOf(static_cast<ParamSlot>(s));
        const bool length = ref && ref->param == gg::core::LayerParam::RingLength;
        const bool midi = gg::core::isMidiSetting(static_cast<ParamSlot>(s));
        CHECK(loaded[s] == (length || midi ? 0.25f : 0.75f));
        kept += length ? 1 : 0;
    }
    CHECK(kept == gg::core::kLayers);
}
