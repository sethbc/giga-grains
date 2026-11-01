// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The session state: the GGST container with its PTCH (the patch XML), MEDI (the media
// manifest), AUDI (the embedded audio) and INST (the per-instance settings) chunks (M1's spec
// §6.6, M2 sub-project 2's spec §6.6, INTENT §18), through gg-core's public API only
// (gg/core/SessionState.h). Like patch_tests.cpp, this file never includes sst-plugininfra or
// tinyxml (ruling M1-l). The containers it can't get from encodeState (unknown chunks, missing
// ones, other versions) it builds byte by byte with support/StateContainer.h.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/PatchState.h"
#include "gg/core/Rng.h"
#include "gg/core/SessionState.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "support/DeepXml.h"
#include "support/StateContainer.h"

using gg::core::AudioPayload;
using gg::core::decodeState;
using gg::core::encodeState;
using gg::core::factoryRoutes;
using gg::core::isValidMediaEntry;
using gg::core::isValidRingEntry;
using gg::core::kParamCount;
using gg::core::MediaEntry;
using gg::core::Mode;
using gg::core::ModRoutes;
using gg::core::ParamSlot;
using gg::core::ParamValues;
using gg::core::RingEntry;
using gg::core::SessionState;
using gg::test::audioChunkPayload;
using gg::test::instanceChunkPayload;
using gg::test::stateChunk;
using gg::test::stateChunkPayload;
using gg::test::stateContainer;

namespace
{
// A state sub-project 1 saved: toStateXml's output for kSubProject1Values and seed 77, copied
// literally. Every state saved before the container loads this way, migrating from patch
// version 1.
constexpr std::string_view kSubProject1State =
    R"(<patch id="org.sethbc.giga-grains.patch" version="1" name="Init"><params>)"
    R"(<p id="1" v="0.800000011921" bits="3f4ccccd" />)"
    R"(<p id="2" v="-6.000000000000" bits="c0c00000" />)"
    R"(<p id="65537" v="0.100000001490" bits="3dcccccd" />)"
    R"(<p id="65538" v="-4.250000000000" bits="c0880000" />)"
    R"(<p id="65539" v="6.699999809265" bits="40d66666" />)"
    R"(<p id="65540" v="0.250000000000" bits="3e800000" />)"
    R"(<p id="65541" v="7.000000000000" bits="40e00000" />)"
    R"(<p id="65542" v="0.000012300000" bits="374e5c19" />)"
    R"(<p id="65543" v="0.000000000000" bits="00000000" />)"
    R"(<p id="65544" v="1.000000000000" bits="3f800000" />)"
    R"(</params><engine seed="77" /></patch>)";

// The values kSubProject1State loads as in the FX, slot by slot: the ten it holds, then Space's
// eight (sub-project 4) and the Perform UI's ten (sub-project 5) at their defaults, which it
// doesn't mention; then M2's 190 new slots (M2 sub-project 1's spec §6.9) at the FX's defaults,
// which patch_tests.cpp pins literally.
const ParamValues kSubProject1Values = [] {
    constexpr std::array<float, 28> kM1{
        0.8f, -6.0f, 0.1f, -4.25f, 6.7f, 0.25f, 7.0f,  1.23e-5f, 0.0f, 1.0f,  // sub-projects 1-3
        0.0f, 0.0f,  1.0f, 0.5f,   0.3f, 0.0f,  0.25f, 0.0f,                  // Space
        0.0f, 0.0f,  0.5f, 0.5f,   0.5f, 0.5f,  0.0f,  0.0f,     0.0f, 0.5f}; // the Perform UI
    ParamValues v{};
    for (std::size_t i = 0; i < kParamCount; ++i)
    {
        v[i] = i < kM1.size() ? kM1[i]
                              : gg::core::paramMeta(static_cast<ParamSlot>(i), Mode::Fx).defaultVal;
    }
    return v;
}();

std::size_t at(ParamSlot s) { return static_cast<std::size_t>(s); }

std::array<uint32_t, kParamCount> bitsOf(const ParamValues &v)
{
    std::array<uint32_t, kParamCount> b{};
    for (std::size_t i = 0; i < kParamCount; ++i)
    {
        b[i] = std::bit_cast<uint32_t>(v[i]);
    }
    return b;
}

/// Whether two matrices are the same, every depth bit for bit.
bool sameRoutes(const ModRoutes &a, const ModRoutes &b)
{
    for (std::size_t k = 0; k < gg::core::kModSlots; ++k)
    {
        if (!(a[k] == b[k]) ||
            std::bit_cast<uint32_t>(a[k].depth) != std::bit_cast<uint32_t>(b[k].depth))
        {
            return false;
        }
    }
    return true;
}

/// The instrument's factory routes and two of a user's: what someState saves.
ModRoutes someRoutes()
{
    ModRoutes r = factoryRoutes(Mode::Instrument);
    r[19] = {gg::core::ModSource::Lfo1, 2,     ParamSlot::Pitch,
             gg::core::kAllLayers,      -0.0f, gg::core::ModCurve::Bipolar};
    r[47] = {gg::core::ModSource::EnvFollower,
             gg::core::kSameLayer,
             ParamSlot::ReverbAmount,
             gg::core::kAllLayers,
             1.2345678e-7f,
             gg::core::ModCurve::TopQuarter};
    return r;
}

// A sample entry whose every field differs from a default MediaEntry, with a UTF-8 path that
// needs XML escaping: ä, ✓, a double quote, and the characters tinyxml treats specially.
MediaEntry sampleEntry()
{
    MediaEntry e;
    e.sha256Hex = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    e.fileSize = 0x0123456789abcdefull;
    e.absolutePath = "/Users/ä/Samples/✓ \"quoted\" & <angled> 'single' &#x41; &amp;.wav";
    e.relativePath = "Drums/✓ \"kick\".wav";
    e.format = "flac";
    e.channels = 6;
    // Not an integer, so only the double's bits bring it back exactly.
    e.sampleRate = std::nextafter(44100.0, 48000.0);
    e.frames = 0x7fffffffffffull;
    e.embed = false;
    return e;
}

SessionState someState(bool withSample)
{
    SessionState s;
    s.values = kSubProject1Values;
    s.seed = 0x0123456789abcdefull;
    s.routes = someRoutes();
    if (withSample)
    {
        s.samples[0] = sampleEntry();
    }
    return s;
}

/// Whether `s` lists no sample, no ring and no payload.
bool noEntries(const SessionState &s)
{
    return std::ranges::none_of(s.samples, [](const auto &e) { return e.has_value(); }) &&
           std::ranges::none_of(s.rings, [](const auto &e) { return e.has_value(); }) &&
           s.audio.empty();
}

/// A ring whose every field differs from a default RingEntry's, in `layer`, embedded as payload
/// `audioId` or, with 0, filed at a path that needs escaping.
RingEntry ringEntry(uint8_t layer, uint32_t audioId)
{
    RingEntry r;
    r.layer = layer;
    r.frames = 96000 + layer;
    r.sampleRate = std::nextafter(48000.0, 0.0); // only the bits bring it back exactly
    r.splices = {0, 12000, 24000 + layer, 95999};
    r.sha256Hex = "5d41402abc4b2a76b9719d911017c592ae41e4649b934ca495991b7852b855e3";
    r.sha256Hex[static_cast<std::size_t>(layer)] = 'f';
    r.fileSize = 768068 + layer;
    r.embed = audioId != 0;
    r.audioId = audioId;
    if (!r.embed)
    {
        r.absolutePath =
            "/Users/ä/Captures/✓ \"ring\" & <" + std::to_string(layer) + "> &#x41;.wav";
    }
    return r;
}

/// The bytes of a payload: every byte value, NUL included, then `tag`.
std::string payloadBytes(std::string_view tag)
{
    std::string b;
    for (int i = 0; i < 256; ++i)
    {
        b += static_cast<char>(i);
    }
    return b + std::string(tag);
}

/// someState(true) with a sample and a ring in every layer: samples A and B filed (B with a
/// content path), C and D embedded captures; rings A and C embedded, B and D filed; the four
/// payloads they name in an order of their own; a memory cap off the default.
SessionState fullState()
{
    SessionState s = someState(true);
    for (uint8_t k = 1; k < gg::core::kLayers; ++k)
    {
        MediaEntry e = sampleEntry();
        e.layer = k;
        e.sha256Hex[0] = static_cast<char>('0' + k);
        e.contentPath = "Factory/Pads/✓ & " + std::to_string(k) + ".wav";
        if (k >= 2)
        {
            e.absolutePath.clear();
            e.relativePath.clear();
            e.format = "wav";
            e.channels = 2;
            e.embed = true;
            e.audioId = 10 + k;
        }
        s.samples[k] = e;
    }
    s.rings[0] = ringEntry(0, 3);
    s.rings[1] = ringEntry(1, 0);
    s.rings[2] = ringEntry(2, 0xffffffffu);
    s.rings[3] = ringEntry(3, 0);
    s.audio = {{0xffffffffu, payloadBytes("ring C")},
               {12, payloadBytes("capture C")},
               {3, payloadBytes("ring A")},
               {13, payloadBytes("capture D")}};
    s.memoryCapMiB = 2048;
    return s;
}

// What a decode writes: the session state and the migration report. A failed decode must leave
// all of it untouched.
struct Decoded
{
    SessionState state;
    uint32_t migratedFromVersion{0};

    bool decode(std::string_view bytes, Mode mode = Mode::Fx)
    {
        return decodeState(bytes, mode, state, &migratedFromVersion);
    }
    bool sameAs(const Decoded &o) const
    {
        return bitsOf(state.values) == bitsOf(o.state.values) && state.seed == o.state.seed &&
               state.samples == o.state.samples && state.rings == o.state.rings &&
               state.audio == o.state.audio && state.memoryCapMiB == o.state.memoryCapMiB &&
               state.sampleAudioMissing == o.state.sampleAudioMissing &&
               state.ringAudioMissing == o.state.ringAudioMissing &&
               state.newerMedia == o.state.newerMedia && state.newerAudio == o.state.newerAudio &&
               sameRoutes(state.routes, o.state.routes) &&
               migratedFromVersion == o.migratedFromVersion;
    }
};

// Values, a seed, samples, a ring, a payload, a memory cap, a newer manifest, routes and a
// migration report that no decode here produces.
Decoded distinct()
{
    Decoded d;
    d.state.values.fill(0.125f);
    d.state.seed = 42;
    MediaEntry e;
    e.sha256Hex = std::string(64, 'f');
    e.absolutePath = "/distinct.wav";
    e.format = "wav";
    e.channels = 1;
    e.sampleRate = 8000.0;
    e.frames = 1;
    d.state.samples[0] = e;
    e.layer = 3;
    d.state.samples[3] = e;
    RingEntry r;
    r.layer = 1;
    r.frames = 3;
    r.sampleRate = 8000.0;
    r.sha256Hex = std::string(64, 'e');
    r.embed = true;
    r.audioId = 77;
    d.state.rings[1] = r;
    d.state.audio = {{77, "distinct"}};
    d.state.memoryCapMiB = 4321;
    d.state.sampleAudioMissing[2] = true;
    d.state.ringAudioMissing[0] = true;
    d.state.newerMedia = "distinct";
    d.state.newerAudio = "distinct";
    d.state.routes.fill({gg::core::ModSource::Step, 3, ParamSlot::Spray, 3, 0.5f,
                         gg::core::ModCurve::RampThreeQuarters});
    d.migratedFromVersion = 12345;
    return d;
}

// The empty manifests of version 1 (M1's and sub-project 1's) and version 2 (this build's), and
// the INST encodeState writes by default.
constexpr std::string_view kEmptyMedia = R"(<media version="1" />)";
constexpr std::string_view kEmptyMedia2 = R"(<media version="2" />)";
const std::string kDefaultInst = instanceChunkPayload();

// A MEDI payload with one <sample>, its attributes as given.
std::string mediaWith(std::string_view attributes)
{
    return std::string(R"(<media version="1"><sample )") + std::string(attributes) + " /></media>";
}

constexpr std::string_view kGoodSample =
    R"(sha256="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" size="10" )"
    R"(path="/a.wav" relative="" format="wav" channels="2" rate="40e7700000000000" )"
    R"(frames="48000" embed="0")";
} // namespace

TEST_CASE("the container's layout is GGST, version 1, PTCH, MEDI, AUDI, INST", "[core]")
{
    STATIC_REQUIRE(gg::core::kMaxStateBytes == std::size_t{64} << 20);
    STATIC_REQUIRE(gg::core::kEmbedCapBytes == std::size_t{32} << 20);
    STATIC_REQUIRE(gg::core::kMemoryCapDefaultMiB == 1024);
    STATIC_REQUIRE(gg::core::kMemoryCapMinMiB == 64);
    STATIC_REQUIRE(gg::core::kMemoryCapMaxMiB == 16384);

    const SessionState s = someState(false);
    const auto bytes = encodeState(s);
    const auto ptch = gg::core::toStateXml(s.values, s.seed, s.routes);

    // The magic, the version as a little-endian u32, then PTCH holding toStateXml's bytes
    // unchanged, then an empty MEDI of version 2, no AUDI (no payload), then INST, and nothing
    // else.
    const std::string expected =
        stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", kEmptyMedia2),
                        stateChunk("INST", kDefaultInst)});
    CHECK(bytes == expected);
    CHECK(bytes.substr(0, 8) == std::string_view("GGST\x01\x00\x00\x00", 8));
    CHECK(bytes.substr(8, 12) ==
          std::string("PTCH") + gg::test::littleEndian(ptch.size(), 8)); // the length is LE
    CHECK(kDefaultInst == R"(<instance version="1" memoryCapMiB="1024" />)");

    // With a sample, MEDI holds one <sample>: layer A's, the rate as its double's bits, embed 0,
    // an empty content path and audio 0.
    SessionState withSample = someState(false);
    MediaEntry e = sampleEntry();
    e.absolutePath = "/a.wav";
    e.relativePath = "";
    e.sampleRate = 48000.0;
    withSample.samples[0] = e;
    const auto medi = stateChunkPayload(encodeState(withSample), "MEDI");
    REQUIRE(medi.has_value());
    CHECK(*medi == R"(<media version="2"><sample layer="A" )"
                   R"(sha256="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" )"
                   R"(size="81985529216486895" path="/a.wav" relative="" format="flac" )"
                   R"(channels="6" rate="40e7700000000000" frames="140737488355327" embed="0" )"
                   R"(content="" audio="0" /></media>)");

    // Samples, then rings, each in layer order; a filed ring has its path, an embedded one and an
    // embedded sample their payload's id; AUDI holds the payloads in their order, then INST.
    SessionState full = someState(false);
    MediaEntry captured = e;
    captured.layer = 2;
    captured.absolutePath = "";
    captured.format = "wav";
    captured.channels = 2;
    captured.frames = 4;
    captured.embed = true;
    captured.contentPath = "Pads/&.wav";
    captured.audioId = 7;
    full.samples[2] = captured;
    e.layer = 1;
    full.samples[1] = e;
    RingEntry filed;
    filed.layer = 3;
    filed.frames = 96000;
    filed.sampleRate = 48000.0;
    filed.splices = {0, 12000, 95999};
    filed.sha256Hex = std::string(64, 'a');
    filed.fileSize = 384068;
    filed.absolutePath = "/Captures/a&b.wav";
    full.rings[3] = filed;
    RingEntry embedded;
    embedded.layer = 0;
    embedded.frames = 2;
    embedded.sampleRate = 44100.0;
    embedded.sha256Hex = std::string(64, 'b');
    embedded.fileSize = 84;
    embedded.embed = true;
    embedded.audioId = 1;
    full.rings[0] = embedded;
    full.audio = {{7, std::string("cap\0", 4)}, {1, "ring"}};
    full.memoryCapMiB = 16384;
    const auto fullBytes = encodeState(full);
    CHECK(fullBytes ==
          stateContainer(
              {stateChunk("PTCH", ptch),
               stateChunk(
                   "MEDI",
                   R"(<media version="2"><sample layer="B" )"
                   R"(sha256="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" )"
                   R"(size="81985529216486895" path="/a.wav" relative="" format="flac" )"
                   R"(channels="6" rate="40e7700000000000" frames="140737488355327" embed="0" )"
                   R"(content="" audio="0" /><sample layer="C" )"
                   R"(sha256="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" )"
                   R"(size="81985529216486895" path="" relative="" format="wav" channels="2" )"
                   R"(rate="40e7700000000000" frames="4" embed="1" content="Pads/&#x26;.wav" )"
                   R"(audio="7" /><ring layer="A" frames="2" rate="40e5888000000000" splices="" )"
                   R"(sha256="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" )"
                   R"(size="84" embed="1" audio="1" /><ring layer="D" frames="96000" )"
                   R"(rate="40e7700000000000" splices="0 12000 95999" )"
                   R"(sha256="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" )"
                   R"(size="384068" embed="0" path="/Captures/a&#x26;b.wav" /></media>)"),
               stateChunk("AUDI", std::string("\x02\x00\x00\x00"
                                              "\x07\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00"
                                              "cap\x00"
                                              "\x01\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00"
                                              "ring",
                                              4 + 2 * 12 + 8)),
               stateChunk("INST", instanceChunkPayload("16384"))}));
    CHECK(stateChunkPayload(fullBytes, "AUDI") ==
          audioChunkPayload({{7, std::string("cap\0", 4)}, {1, "ring"}}));
}

TEST_CASE("the container round trips with and without a sample", "[core]")
{
    for (const bool withSample : {false, true})
    {
        INFO("with a sample: " << withSample);
        const SessionState s = someState(withSample);
        const auto bytes = encodeState(s);

        for (const Mode mode : {Mode::Instrument, Mode::Fx})
        {
            Decoded d = distinct();
            REQUIRE(d.decode(bytes, mode));
            CHECK(bitsOf(d.state.values) == bitsOf(s.values));
            CHECK(d.state.seed == s.seed);
            CHECK(d.state.samples == s.samples);
            CHECK(sameRoutes(d.state.routes, s.routes));
            CHECK(d.migratedFromVersion == 0u);
            // Saving what was loaded writes the same bytes again.
            CHECK(encodeState(d.state) == bytes);
        }
    }

    // Every field at its extremes, and a path in each of the UTF-8 lengths.
    SessionState s = someState(true);
    s.seed = std::numeric_limits<uint64_t>::max();
    s.samples[0]->fileSize = std::numeric_limits<uint64_t>::max();
    s.samples[0]->channels = std::numeric_limits<uint32_t>::max();
    s.samples[0]->sampleRate = std::numeric_limits<double>::denorm_min();
    s.samples[0]->frames = std::numeric_limits<int64_t>::max();
    s.samples[0]->absolutePath = "C:\\Samples\\a\xc3\xa4\xe2\x9c\x93\xf0\x9f\x8e\xb5.aiff";
    s.samples[0]->relativePath = "tab\there\nnewline";
    s.samples[0]->format = "aiff";
    Decoded d = distinct();
    REQUIRE(d.decode(encodeState(s)));
    CHECK(d.state.samples == s.samples);
    CHECK(d.state.seed == s.seed);

    // A sample with every string empty, as a default MediaEntry has them, but for its hash.
    s.samples[0] = MediaEntry{};
    s.samples[0]->sha256Hex = std::string(64, '0');
    REQUIRE(d.decode(encodeState(s)));
    CHECK(d.state.samples == s.samples);
}

TEST_CASE("a pre-container state loads with no sample", "[core]")
{
    // The bare XML of sub-projects 1 and 2: its whole content is PTCH, with an empty MEDI. It is
    // patch version 1, so it migrates (gg/core/PatchState.h): the factory routes. Today's XML for
    // the same values is the same up to its ten params but for its version, then adds the
    // other params and the routes.
    const auto fxRoutes = factoryRoutes(Mode::Fx);
    std::string tenParams(kSubProject1State.substr(0, kSubProject1State.find("</params>")));
    tenParams.replace(tenParams.find(R"(version="1")"), 11, R"(version="2")");
    CHECK(gg::core::toStateXml(kSubProject1Values, 77, fxRoutes).starts_with(tenParams));

    Decoded d = distinct();
    REQUIRE(d.decode(kSubProject1State));
    CHECK(bitsOf(d.state.values) == bitsOf(kSubProject1Values));
    CHECK(d.state.seed == 77u);
    CHECK(noEntries(d.state));
    CHECK(sameRoutes(d.state.routes, fxRoutes));
    CHECK(d.migratedFromVersion == 1u);

    // As before the container, bytes after the XML's NUL are ignored: an old state never fails
    // to load now that loaded then.
    Decoded nul = distinct();
    REQUIRE(nul.decode(std::string(kSubProject1State) + std::string("\0junk", 5)));
    CHECK(bitsOf(nul.state.values) == bitsOf(kSubProject1Values));
    CHECK(noEntries(nul.state));

    // Saving it again writes a container.
    const auto bytes = encodeState(d.state);
    CHECK(bytes.starts_with("GGST"));
    CHECK(stateChunkPayload(bytes, "PTCH") ==
          gg::core::toStateXml(kSubProject1Values, 77, fxRoutes));

    // Exactly as fromStateXml loads it, in either product: a param it doesn't mention takes the
    // product's default.
    std::string noPosition(kSubProject1State);
    const auto from = noPosition.find(R"(<p id="65537" )");
    REQUIRE(from != std::string::npos);
    noPosition.erase(from, noPosition.find("/>", from) + 2 - from);
    for (const Mode mode : {Mode::Instrument, Mode::Fx})
    {
        ParamValues values{};
        uint64_t seed = 0;
        ModRoutes routes{};
        REQUIRE(gg::core::fromStateXml(noPosition, values, seed, routes, mode));
        CHECK(sameRoutes(routes, factoryRoutes(mode)));
        Decoded bare = distinct();
        REQUIRE(bare.decode(noPosition, mode));
        CHECK(bitsOf(bare.state.values) == bitsOf(values));
        CHECK(sameRoutes(bare.state.routes, routes));
        Decoded wrapped = distinct();
        REQUIRE(wrapped.decode(
            stateContainer({stateChunk("PTCH", noPosition), stateChunk("MEDI", kEmptyMedia)}),
            mode));
        CHECK(bitsOf(wrapped.state.values) == bitsOf(values));
        CHECK(sameRoutes(wrapped.state.routes, routes));
        CHECK(wrapped.state.values[at(ParamSlot::Position)] ==
              gg::core::paramMeta(ParamSlot::Position, mode).defaultVal);
    }
}

TEST_CASE("the container carries the routes", "[core]")
{
    // The routes go in PTCH's <mod> (patch version 2) and come back bit for bit, in either
    // product; saving what was loaded writes the same bytes again.
    for (const bool withSample : {false, true})
    {
        INFO("with a sample: " << withSample);
        const SessionState s = someState(withSample);
        const auto bytes = encodeState(s);
        const auto ptch = stateChunkPayload(bytes, "PTCH");
        REQUIRE(ptch.has_value());
        CHECK(ptch->find(R"(<engine seed="81985529216486895" /><mod><route slot="1" )") !=
              std::string::npos);
        CHECK(ptch->find(R"(<route slot="20" source="LFO 1" sourceLayer="C" target="65541" )"
                         R"(targetLayer="all" depth="-0.000000000000" bits="80000000" )"
                         R"(curve="Bipolar" />)") != std::string::npos);
        for (const Mode mode : {Mode::Instrument, Mode::Fx})
        {
            Decoded d = distinct();
            REQUIRE(d.decode(bytes, mode));
            CHECK(sameRoutes(d.state.routes, s.routes));
            CHECK(d.migratedFromVersion == 0u);
            CHECK(encodeState(d.state) == bytes);
        }
    }

    // No routes: an empty <mod>, and every slot empty again.
    SessionState empty = someState(false);
    empty.routes = ModRoutes{};
    const auto bytes = encodeState(empty);
    CHECK(stateChunkPayload(bytes, "PTCH")->ends_with("<mod /></patch>"));
    Decoded d = distinct();
    REQUIRE(d.decode(bytes));
    CHECK(sameRoutes(d.state.routes, ModRoutes{}));

    // A version 1 PTCH, in a container or bare, migrates: the factory routes, and in the
    // instrument Macro 7 folded into Stereo (gg/core/PatchState.h). kSubProject1State's Stereo is
    // 0; with Macro 7 at 0.8 it becomes 0.5 × (2 × 0.8 − 1).
    std::string m1(kSubProject1State);
    m1.insert(m1.find("</params>"), R"(<p id="23" v="0.800000011921" bits="3f4ccccd" />)");
    const float folded = std::clamp(0.0f + 0.5f * (2.0f * 0.8f - 1.0f), 0.0f, 1.0f);
    for (const Mode mode : {Mode::Instrument, Mode::Fx})
    {
        INFO((mode == Mode::Fx ? "fx" : "instrument"));
        Decoded bare = distinct();
        REQUIRE(bare.decode(m1, mode));
        Decoded wrapped = distinct();
        REQUIRE(wrapped.decode(
            stateContainer({stateChunk("PTCH", m1), stateChunk("MEDI", kEmptyMedia)}), mode));
        for (const Decoded *x : {&bare, &wrapped})
        {
            CHECK(x->migratedFromVersion == 1u);
            CHECK(sameRoutes(x->state.routes, factoryRoutes(mode)));
            const float stereo = x->state.values[at(ParamSlot::Stereo)];
            const float macro7 = x->state.values[at(ParamSlot::Macro7)];
            if (mode == Mode::Instrument)
            {
                CHECK(std::bit_cast<uint32_t>(stereo) == std::bit_cast<uint32_t>(folded));
                CHECK(macro7 == 0.0f);
            }
            else
            {
                CHECK(stereo == 0.0f);
                CHECK(std::bit_cast<uint32_t>(macro7) == std::bit_cast<uint32_t>(0.8f));
            }
        }
    }
}

TEST_CASE("a newer MEDI loads the patch with no sample, and saves back unchanged", "[core]")
{
    // The director's decision of 2026-10-28 (INTENT §18): a manifest of a newer version loads the
    // patch, keeps its bytes, and a save writes them back byte for byte, so an older build that
    // saves over a newer session keeps its sample reference.
    const SessionState s = someState(true);
    const auto ptch = *stateChunkPayload(encodeState(s), "PTCH");
    const std::string kGood(
        R"(sha256="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" size="10" )"
        R"(path="/a.wav" relative="" format="wav" channels="2" rate="40e7700000000000" )"
        R"(frames="48000" embed="0")");
    // Whatever a newer build might write inside <media>: unknown elements and attributes, two
    // samples, an embedded one, text, comments and white space, and bytes that aren't UTF-8.
    // This build reads versions 1 and 2, so 3 is the first newer one.
    const std::vector<std::string> newer{
        R"(<media version="3" />)",
        R"(<media version="3"><sample )" + kGood + " /></media>",
        R"(<media version="3" layers="2"><sample )" + kGood +
            R"( layer="0" /><sample sha256="ab" )"
            R"(embed="1" layer="1" /><capture hash="ab" /></media>)",
        "<media version=\"99\">\n  <!-- notes -->\n  <sample path=\"/\xff\xc3(\" />text\n</media>",
        R"(<media version="2147483647"><future><nested deeper="yes" /></future></media>)",
        R"(<media version="4"><ring layer="E" audio="0" /><sample layer="A" embed="1" )"
        R"(audio="99" /></media>)",
    };
    // Beside it, no AUDI, an AUDI this build could read, and one it couldn't (a newer build's
    // layout): AUDI carries no version, so beside a newer manifest it is kept unread and written
    // back as it came (the controller's ruling (b)).
    const std::vector<std::optional<std::string>> audis{
        std::nullopt,
        audioChunkPayload({{99, "a payload the newer manifest names"}, {3, ""}}),
        std::string("a future layout\0\xff", 17),
    };
    for (const std::string &medi : newer)
    {
        for (const auto &audi : audis)
        {
            INFO("MEDI '" << medi << "'");
            INFO("AUDI: " << (audi ? std::to_string(audi->size()) + " bytes" : "none"));
            const auto bytes =
                audi ? stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", medi),
                                       stateChunk("AUDI", *audi), stateChunk("INST", kDefaultInst)})
                     : stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", medi),
                                       stateChunk("INST", kDefaultInst)});
            for (const Mode mode : {Mode::Instrument, Mode::Fx})
            {
                Decoded d = distinct();
                REQUIRE(d.decode(bytes, mode));
                // The patch applies; there is no sample, and the manifest and its AUDI are kept
                // as they came.
                CHECK(bitsOf(d.state.values) == bitsOf(s.values));
                CHECK(d.state.seed == s.seed);
                CHECK(noEntries(d.state));
                REQUIRE(d.state.newerMedia.has_value());
                CHECK(*d.state.newerMedia == medi);
                CHECK(d.state.newerAudio == audi);
                CHECK(d.migratedFromVersion == 0u);

                // Saving it writes the manifest's and AUDI's bytes back unchanged, and the same
                // patch.
                const auto saved = encodeState(d.state);
                CHECK(stateChunkPayload(saved, "MEDI") == medi);
                CHECK(stateChunkPayload(saved, "AUDI") == audi);
                CHECK(stateChunkPayload(saved, "PTCH") == ptch);
                CHECK(saved == bytes);
                Decoded again = distinct();
                REQUIRE(again.decode(saved, mode));
                CHECK(again.state.newerMedia == d.state.newerMedia);
                CHECK(again.state.newerAudio == d.state.newerAudio);
            }
        }
    }

    SECTION("a sample or a ring loaded since replaces the newer manifest")
    {
        // And its AUDI with it.
        Decoded d = distinct();
        REQUIRE(d.decode(stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", newer[1]),
                                         stateChunk("AUDI", *audis[2])})));
        REQUIRE(d.state.newerAudio == audis[2]);
        d.state.samples[0] = sampleEntry();
        CHECK(encodeState(d.state) == encodeState(someState(true)));

        Decoded r = distinct();
        REQUIRE(r.decode(stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", newer[1])})));
        RingEntry ring;
        ring.layer = 2;
        ring.sha256Hex = std::string(64, 'c');
        r.state.rings[2] = ring;
        SessionState expected = someState(false);
        expected.rings[2] = ring;
        CHECK(encodeState(r.state) == encodeState(expected));
        CHECK(stateChunkPayload(encodeState(r.state), "MEDI")
                  ->starts_with(R"(<media version="2"><ring layer="C" )"));
    }

    SECTION("a newerMedia decodeState wouldn't keep is left out")
    {
        for (const std::string &bad :
             {std::string{}, std::string{"not xml"}, std::string(kEmptyMedia), mediaWith(kGood),
              std::string(kEmptyMedia2), // this build's version isn't newer
              std::string{"<media version=\"3\">"}, std::string{"<media version=\"3\" />junk"},
              std::string("<media version=\"3\" />\0", 22),
              std::string{"<manifest version=\"3\" />"}})
        {
            INFO("newerMedia '" << bad << "'");
            SessionState t = someState(false);
            t.newerMedia = bad;
            CHECK(encodeState(t) == encodeState(someState(false)));
        }
    }
}

TEST_CASE("a MEDI at this build's version reads as before, with no newer manifest", "[core]")
{
    // Version 2, the build's own, with and without a sample: read, never kept as bytes, and
    // written by this build (the older-or-equal half of the newer-MEDI decision).
    for (const bool withSample : {false, true})
    {
        INFO("with a sample: " << withSample);
        const auto bytes = encodeState(someState(withSample));
        Decoded d = distinct();
        REQUIRE(d.decode(bytes));
        CHECK(d.state.samples == someState(withSample).samples);
        CHECK(d.state.newerMedia == std::nullopt);
        CHECK(encodeState(d.state) == bytes);
    }
    // Version 1, older: read, never kept either.
    const auto ptch = *stateChunkPayload(encodeState(someState(false)), "PTCH");
    Decoded v1 = distinct();
    REQUIRE(v1.decode(stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", kEmptyMedia)})));
    CHECK(noEntries(v1.state));
    CHECK(v1.state.newerMedia == std::nullopt);
    // A pre-container state has no MEDI, and so no newer manifest.
    Decoded d = distinct();
    REQUIRE(d.decode(kSubProject1State));
    CHECK(d.state.newerMedia == std::nullopt);
}

TEST_CASE("a newer MEDI that isn't a manifest still fails the whole state", "[core]")
{
    // Only the version is read of a newer manifest, but it must still be one: well-formed, its
    // <media> root alone at the top, its version a whole decimal integer.
    const auto ptch = *stateChunkPayload(encodeState(someState(false)), "PTCH");
    const Decoded before = distinct();
    for (const std::string &bad : {
             std::string{"<media version=\"3\">"},
             std::string{"<media version=\"3\"><sample></media>"},
             std::string{"<media version=\"3\" />junk"},
             std::string{"<media version=\"3\" /><media version=\"3\" />"},
             std::string{"<!-- c --><media version=\"3\" />"},
             std::string{R"(<?xml version="1.0"?><media version="3" />)"},
             std::string{"<manifest version=\"3\" />"},
             std::string{"<media version=\"3x\" />"},
             std::string{"<media version=\"2147483648\" />"}, // not an int
             std::string{"<media version=\"+3\" />"},
             std::string("<media version=\"3\" />\0", 22),
             "<media version=\"3\">" + gg::test::repeated("<a>", 64) + "</media>",
         })
    {
        INFO("MEDI '" << bad << "'");
        Decoded d = distinct();
        CHECK_FALSE(d.decode(stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", bad)})));
        CHECK(d.sameAs(before));
    }
}

TEST_CASE("unknown chunks are skipped", "[core]")
{
    const SessionState s = someState(true);
    const auto bytes = encodeState(s);
    const auto ptch = stateChunkPayload(bytes, "PTCH");
    const auto medi = stateChunkPayload(bytes, "MEDI");
    REQUIRE(ptch.has_value());
    REQUIRE(medi.has_value());

    // A newer build's chunk between PTCH and MEDI, before both, after both; an empty one; one
    // whose payload looks like a chunk; one named like AUDI in lower case; and around AUDI and
    // INST, which this build reads.
    const std::string xtra = stateChunk("XTRA", std::string("\x00\xff GGST PTCH", 11));
    const std::string audi = stateChunk("AUDI", audioChunkPayload({{5, "unnamed"}}));
    const std::string inst = stateChunk("INST", instanceChunkPayload("2048"));
    for (const auto &state :
         {stateContainer({stateChunk("PTCH", *ptch), xtra, stateChunk("MEDI", *medi)}),
          stateContainer({xtra, stateChunk("PTCH", *ptch), stateChunk("MEDI", *medi)}),
          stateContainer({stateChunk("PTCH", *ptch), stateChunk("MEDI", *medi), xtra}),
          stateContainer(
              {stateChunk("MEDI", *medi), stateChunk("XTRA", ""), stateChunk("PTCH", *ptch)}),
          stateContainer({stateChunk("PTCH", *ptch), stateChunk("XTRA", stateChunk("PTCH", "x")),
                          stateChunk("MEDI", *medi), stateChunk("audi", "fLaC")}),
          stateContainer({inst, xtra, stateChunk("MEDI", *medi), audi, stateChunk("CAPT", ""),
                          stateChunk("PTCH", *ptch)})})
    {
        Decoded d = distinct();
        REQUIRE(d.decode(state));
        CHECK(bitsOf(d.state.values) == bitsOf(s.values));
        CHECK(d.state.seed == s.seed);
        CHECK(d.state.samples == s.samples);
        CHECK(d.migratedFromVersion == 0u);
    }
}

TEST_CASE("session state rejects bad state and writes nothing", "[core]")
{
    // The snapshot is everything a decode writes: the values, the seed, the sample and the
    // migration report (carry-over T5 105). `dirty` belongs to the patch inside gg-core: each
    // decode loads into a patch of its own, so none outlives the call.
    const SessionState s = someState(true);
    const auto good = encodeState(s);
    const auto ptch = *stateChunkPayload(good, "PTCH");
    const auto medi = *stateChunkPayload(good, "MEDI");

    Decoded d = distinct();
    const Decoded before = distinct();

    SECTION("truncated at every byte offset")
    {
        // A state with every chunk, the embedded entries naming AUDI's payloads. Cut just before
        // INST, it is a whole state without one, which loads with the default memory cap; cut
        // just before AUDI, a whole state without either, whose embedded entries load with their
        // audio missing.
        const auto full = encodeState(fullState());
        const auto inst = stateChunk("INST", *stateChunkPayload(full, "INST"));
        const auto audi = stateChunk("AUDI", *stateChunkPayload(full, "AUDI"));
        REQUIRE(full.ends_with(audi + inst));
        const std::size_t instAt = full.size() - inst.size();
        const std::size_t audiAt = instAt - audi.size();
        for (std::size_t n = 0; n < full.size(); ++n)
        {
            INFO("cut at " << n << " of " << full.size());
            if (n == instAt || n == audiAt)
            {
                REQUIRE(d.decode(std::string_view(full).substr(0, n)));
                REQUIRE(d.state.memoryCapMiB == gg::core::kMemoryCapDefaultMiB);
                REQUIRE(d.state.samples == fullState().samples);
                REQUIRE(d.state.rings == fullState().rings);
                if (n == instAt)
                {
                    REQUIRE(d.state.audio == fullState().audio);
                    REQUIRE(d.state.sampleAudioMissing == std::array<bool, 4>{});
                    REQUIRE(d.state.ringAudioMissing == std::array<bool, 4>{});
                }
                else
                {
                    REQUIRE(d.state.audio.empty());
                    REQUIRE(d.state.sampleAudioMissing ==
                            std::array<bool, 4>{false, false, true, true});
                    REQUIRE(d.state.ringAudioMissing ==
                            std::array<bool, 4>{true, false, true, false});
                }
                d = distinct();
                continue;
            }
            REQUIRE_FALSE(d.decode(std::string_view(full).substr(0, n)));
            REQUIRE(d.sameAs(before));
        }
        REQUIRE(d.decode(full));
        REQUIRE(d.decode(good));
    }

    SECTION("a bad container fails")
    {
        const auto p = stateChunk("PTCH", ptch);
        const auto m = stateChunk("MEDI", medi);
        auto pastTheEnd = stateContainer({p, m});
        pastTheEnd[8 + 12 + ptch.size() + 4] += 1; // MEDI's length one too long
        auto hugeLength = stateContainer({p, m});
        for (std::size_t i = 0; i < 8; ++i)
        {
            hugeLength[8 + 4 + i] = '\xff'; // PTCH's length 2^64 - 1
        }
        for (const auto &[what, bad] : std::initializer_list<std::pair<const char *, std::string>>{
                 {"empty", ""},
                 {"bad magic", stateContainer({p, m}, 1, "GGSX")},
                 {"lower-case magic", stateContainer({p, m}, 1, "ggst")},
                 {"version 2", stateContainer({p, m}, 2)},
                 {"version 0", stateContainer({p, m}, 0)},
                 {"version 2^32 - 1", stateContainer({p, m}, 0xffffffffu)},
                 {"no chunks", stateContainer({})},
                 {"no PTCH", stateContainer({m})},
                 {"no MEDI", stateContainer({p})},
                 {"two PTCH", stateContainer({p, p, m})},
                 {"two MEDI", stateContainer({p, m, m})},
                 {"two AUDI", stateContainer({p, m, stateChunk("AUDI", audioChunkPayload({})),
                                              stateChunk("AUDI", audioChunkPayload({}))})},
                 {"two INST", stateContainer({p, m, stateChunk("INST", kDefaultInst),
                                              stateChunk("INST", kDefaultInst)})},
                 {"a chunk running past the end", pastTheEnd},
                 {"a length of 2^64 - 1", hugeLength},
                 {"a partial chunk header", stateContainer({p, m}) + "XTRA"},
                 {"bad PTCH", stateContainer({stateChunk("PTCH", "<patch/>"), m})},
                 {"empty PTCH", stateContainer({stateChunk("PTCH", ""), m})},
                 {"PTCH with a NUL",
                  stateContainer({stateChunk("PTCH", ptch + std::string("\0junk", 5)), m})},
                 {"not XML at all", "<garbage"}})
        {
            INFO(what);
            CHECK_FALSE(d.decode(bad));
            CHECK(d.sameAs(before));
        }
    }

    SECTION("a malformed MEDI fails")
    {
        const std::string sample(kGoodSample);
        const auto replaced = [&](std::string_view from, std::string_view to) {
            auto attributes = sample;
            const auto i = attributes.find(from);
            REQUIRE(i != std::string::npos);
            attributes.replace(i, from.size(), to);
            return mediaWith(attributes);
        };
        // The good sample, as the baseline every bad one edits.
        REQUIRE(d.decode(
            stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", mediaWith(sample))})));
        CHECK(d.state.samples[0].has_value());
        d = distinct();

        for (const std::string &bad : {
                 std::string{},
                 std::string{"<media version=\"1\">"},
                 std::string{"<media version=\"1\"><sample "},
                 std::string{"not xml"},
                 std::string{"<manifest version=\"1\" />"},
                 std::string{"<media version=\"0\" />"},
                 std::string{"<media version=\"-1\" />"},
                 std::string(kEmptyMedia) + "<media version=\"1\" />", // trailing content
                 std::string(kEmptyMedia) + "junk",
                 std::string(kEmptyMedia) + "<!-- note -->",
                 "<!-- note -->" + std::string(kEmptyMedia),
                 R"(<?xml version="1.0" ?>)" + std::string(kEmptyMedia),
                 std::string{"<media />"},
                 std::string{"<media version=\"one\" />"},
                 std::string{"<media version=\"1x\" />"}, // the whole attribute is the number
                 std::string{"<media version=\" 1\" />"},
                 std::string{"<media version=\"\" />"},
                 std::string(kEmptyMedia) + std::string("\0", 1),
                 mediaWith(sample).insert(std::string_view(R"(<media version="1">)").size(),
                                          "<sample " + sample + " />"), // two samples
                 replaced(R"(sha256="e3b0)", R"(sha256="E3B0)"),
                 replaced(R"(sha256="e3b0)", R"(sha256="e3b)"),
                 replaced(R"(sha256="e3b0)", R"(sha256="e3b0x)"),
                 replaced(R"(size="10")", R"(size="-1")"),
                 replaced(R"(size="10")", R"(size="18446744073709551616")"),
                 replaced(R"(size="10")", R"(size="")"),
                 replaced(R"(size="10")", R"(size="1 0")"),
                 replaced(R"(path="/a.wav" )", ""),
                 replaced(R"(relative="" )", ""),
                 replaced(R"(format="wav" )", ""),
                 replaced(R"(channels="2")", R"(channels="4294967296")"),
                 replaced(R"(channels="2")", R"(channels="2.0")"),
                 replaced(R"(rate="40e7700000000000")", R"(rate="48000")"),
                 replaced(R"(rate="40e7700000000000")", R"(rate="40e770000000000")"),
                 replaced(R"(rate="40e7700000000000")", R"(rate="40e77000000000000")"),
                 replaced(R"(frames="48000")", R"(frames="9223372036854775808")"),
                 replaced(R"(frames="48000")", R"(frames="")"),
                 replaced(R"(frames="48000")", R"(frames="-1")"),
                 replaced(R"(embed="0")", R"(embed="1")"), // no AUDI in this version
                 replaced(R"(embed="0")", R"(embed="2")"),
                 replaced(R"(embed="0")", R"(embed="false")"),
                 replaced(R"( embed="0")", ""),
             })
        {
            INFO("MEDI '" << bad << "'");
            CHECK_FALSE(
                d.decode(stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", bad)})));
            CHECK(d.sameAs(before));
        }
    }

    SECTION("a newer patch version loads through the migration hooks")
    {
        auto newer = ptch;
        newer.replace(newer.find(R"(version="2")"), 11, R"(version="99")");
        REQUIRE(d.decode(stateContainer({stateChunk("PTCH", newer), stateChunk("MEDI", medi)})));
        CHECK(d.migratedFromVersion == 99u);
        CHECK(bitsOf(d.state.values) == bitsOf(s.values));
        CHECK(d.state.samples == s.samples);
        CHECK(sameRoutes(d.state.routes, s.routes));

        // A failed decode keeps that report; the next good one at the current version clears it.
        CHECK_FALSE(d.decode(good.substr(0, good.size() - 1)));
        CHECK(d.migratedFromVersion == 99u);
        REQUIRE(d.decode(good));
        CHECK(d.migratedFromVersion == 0u);
    }

    SECTION("the migration report is optional")
    {
        SessionState out;
        REQUIRE(decodeState(good, Mode::Fx, out));
        CHECK(out.seed == s.seed);
        CHECK(out.samples == s.samples);
    }
}

namespace
{
// An entry from the engine's counter-based RNG: every field random, the strings any bytes but
// NUL (so often not valid UTF-8), of 0 to 47 bytes; embedded one time in four, naming a payload
// `audioId` that the round trip must provide.
MediaEntry randomEntry(uint64_t index)
{
    uint64_t draw = 0;
    const auto next = [&] {
        return gg::rng::hash(
            {0x5eed'7e57'0000'0009ull, 0, 0, gg::rng::Field::SignalNoise, index * 1024 + draw++});
    };
    const auto bytes = [&] {
        std::string s(static_cast<std::size_t>(next() % 48), ' ');
        for (auto &c : s)
        {
            c = static_cast<char>(1 + next() % 255);
        }
        return s;
    };
    MediaEntry e;
    for (int i = 0; i < 64; ++i)
    {
        e.sha256Hex += "0123456789abcdef"[next() % 16];
    }
    e.fileSize = next();
    e.absolutePath = bytes();
    e.relativePath = bytes();
    e.format = bytes();
    e.channels = static_cast<uint32_t>(next());
    e.sampleRate = std::bit_cast<double>(next());
    e.frames = static_cast<int64_t>(next() >> 1); // never negative
    e.layer = static_cast<uint8_t>(next() % gg::core::kLayers);
    e.contentPath = bytes();
    e.embed = next() % 4 == 0;
    e.audioId = e.embed ? static_cast<uint32_t>(1 + next() % 0xffffffffu) : 0;
    return e;
}

// A ring from the same RNG, as randomEntry draws a sample: up to 8 ascending splices, a path of
// any bytes but NUL when filed.
RingEntry randomRing(uint64_t index)
{
    uint64_t draw = 0;
    const auto next = [&] {
        return gg::rng::hash(
            {0x5eed'7e57'0000'000aull, 0, 0, gg::rng::Field::SignalNoise, index * 1024 + draw++});
    };
    RingEntry r;
    r.layer = static_cast<uint8_t>(next() % gg::core::kLayers);
    r.frames = static_cast<int64_t>(next() >> 1);
    r.sampleRate = std::bit_cast<double>(next());
    const auto count = next() % 9;
    for (uint64_t k = 0; k < count && r.frames > 0; ++k)
    {
        const int64_t from = r.splices.empty() ? 0 : r.splices.back() + 1;
        if (from >= r.frames)
        {
            break;
        }
        r.splices.push_back(from + static_cast<int64_t>(next() % uint64_t(r.frames - from)) / 8);
    }
    for (int i = 0; i < 64; ++i)
    {
        r.sha256Hex += "0123456789abcdef"[next() % 16];
    }
    r.fileSize = next();
    r.embed = next() % 2 == 0;
    r.audioId = r.embed ? static_cast<uint32_t>(1 + next() % 0xffffffffu) : 0;
    if (!r.embed)
    {
        r.absolutePath.resize(static_cast<std::size_t>(next() % 48));
        for (auto &c : r.absolutePath)
        {
            c = static_cast<char>(1 + next() % 255);
        }
    }
    return r;
}

bool sameEntry(const MediaEntry &a, const MediaEntry &b)
{
    // The rate by its bits, so a NaN counts as itself.
    return a.sha256Hex == b.sha256Hex && a.fileSize == b.fileSize &&
           a.absolutePath == b.absolutePath && a.relativePath == b.relativePath &&
           a.format == b.format && a.channels == b.channels &&
           std::bit_cast<uint64_t>(a.sampleRate) == std::bit_cast<uint64_t>(b.sampleRate) &&
           a.frames == b.frames && a.embed == b.embed && a.layer == b.layer &&
           a.contentPath == b.contentPath && a.audioId == b.audioId;
}

bool sameRing(const RingEntry &a, const RingEntry &b)
{
    // The rate by its bits, so a NaN counts as itself; the rest as RingEntry compares it.
    RingEntry x = a;
    RingEntry y = b;
    x.sampleRate = 0.0;
    y.sampleRate = 0.0;
    return x == y && std::bit_cast<uint64_t>(a.sampleRate) == std::bit_cast<uint64_t>(b.sampleRate);
}
} // namespace

TEST_CASE("a state nested deeper than 64 elements fails, in PTCH, MEDI or INST", "[core]")
{
    // tinyxml parses nested elements recursively: a project of a million nested elements would
    // overflow the stack and crash the host as it opens. decodeState refuses nesting deeper than
    // 64 before tinyxml sees it.
    const auto good = encodeState(someState(true));
    const auto ptch = *stateChunkPayload(good, "PTCH");
    const auto medi = *stateChunkPayload(good, "MEDI");
    const auto open = [](std::size_t n) {
        std::string s;
        for (std::size_t i = 0; i < n; ++i)
        {
            s += "<a>";
        }
        return s;
    };
    const auto close = [](std::size_t n) {
        std::string s;
        for (std::size_t i = 0; i < n; ++i)
        {
            s += "</a>";
        }
        return s;
    };
    Decoded d = distinct();
    const Decoded before = distinct();
    SECTION("in MEDI")
    {
        // 64 levels, <media> included, decode; 65 and a million don't.
        const std::string at64 = R"(<media version="1">)" + open(63) + close(63) + "</media>";
        REQUIRE(d.decode(stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", at64)})));
        d = distinct();
        for (const std::size_t n : {std::size_t{64}, std::size_t{1'000'000}})
        {
            INFO(n + 1 << " levels");
            const std::string deep = R"(<media version="1">)" + open(n) + close(n) + "</media>";
            REQUIRE_FALSE(
                d.decode(stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", deep)})));
            REQUIRE(d.sameAs(before));
        }
    }
    SECTION("in PTCH")
    {
        std::string deep = ptch;
        deep.insert(deep.find("<params>") + 8, open(1'000'000));
        REQUIRE_FALSE(
            d.decode(stateContainer({stateChunk("PTCH", deep), stateChunk("MEDI", medi)})));
        REQUIRE(d.sameAs(before));
    }
    SECTION("in INST")
    {
        // As in MEDI: 64 levels, <instance> included, decode; 65 and a million don't.
        const auto with = [&](const std::string &inst) {
            return stateContainer(
                {stateChunk("PTCH", ptch), stateChunk("MEDI", medi), stateChunk("INST", inst)});
        };
        REQUIRE(d.decode(with(R"(<instance version="1">)" + open(63) + close(63) + "</instance>")));
        d = distinct();
        for (const std::size_t n : {std::size_t{64}, std::size_t{1'000'000}})
        {
            INFO(n + 1 << " levels");
            REQUIRE_FALSE(
                d.decode(with(R"(<instance version="1">)" + open(n) + close(n) + "</instance>")));
            REQUIRE(d.sameAs(before));
        }
    }
}

TEST_CASE("a state that tinyxml would nest deeper than it looks fails, in PTCH, MEDI or INST",
          "[core]")
{
    // The shapes a scan of the bytes can miscount (tests/support/DeepXml.h): each would overflow
    // tinyxml's stack and crash the host as the project opens. A process killed here is the
    // failure.
    const auto good = encodeState(someState(true));
    const auto ptch = *stateChunkPayload(good, "PTCH");
    const auto medi = *stateChunkPayload(good, "MEDI");
    const Decoded before = distinct();
    for (const gg::test::DeepXml &shape : gg::test::deepXmlShapes())
        DYNAMIC_SECTION(shape.name)
        {
            Decoded d = distinct();
            const std::string deepMedi =
                shape.head + R"(<media version="1">)" + shape.body + "</media>";
            CHECK_FALSE(
                d.decode(stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", deepMedi)})));
            CHECK(d.sameAs(before));

            std::string deepPtch = ptch;
            deepPtch.insert(deepPtch.find("<params>") + 8, shape.body);
            deepPtch.insert(0, shape.head);
            CHECK_FALSE(
                d.decode(stateContainer({stateChunk("PTCH", deepPtch), stateChunk("MEDI", medi)})));
            CHECK(d.sameAs(before));

            const std::string deepInst =
                shape.head + R"(<instance version="1">)" + shape.body + "</instance>";
            CHECK_FALSE(d.decode(stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", medi),
                                                 stateChunk("INST", deepInst)})));
            CHECK(d.sameAs(before));
        }
}

TEST_CASE("every valid entry round trips, whatever bytes its strings hold", "[core]")
{
    // Ruling R9: decodeState loads whatever encodeState writes. 2,000 random valid entries (and
    // rings), then
    // each byte but NUL alone, and each pair of bytes from 0x80 up (lone lead and continuation
    // bytes, overlong forms, surrogates: tinyxml's UTF-8 mode would misread many of these).
    std::vector<MediaEntry> entries;
    for (uint64_t i = 0; i < 2000; ++i)
    {
        entries.push_back(randomEntry(i));
    }
    for (int b = 1; b < 256; ++b)
    {
        MediaEntry e = randomEntry(0);
        e.absolutePath = std::string(1, static_cast<char>(b));
        e.relativePath = "a" + e.absolutePath + "z";
        entries.push_back(e);
    }
    std::string pairs;
    for (int a = 0x80; a < 0x100; ++a)
    {
        for (int b = 0x80; b < 0x100; ++b)
        {
            pairs += static_cast<char>(a);
            pairs += static_cast<char>(b);
            pairs += '/';
        }
    }
    MediaEntry allPairs = randomEntry(1);
    allPairs.absolutePath = pairs;
    entries.push_back(allPairs);

    // Each beside a random ring in another layer, both naming their payloads when embedded.
    std::size_t failures = 0;
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        REQUIRE(isValidMediaEntry(entries[i]));
        RingEntry ring = randomRing(i);
        ring.layer = static_cast<uint8_t>((entries[i].layer + 1) % gg::core::kLayers);
        REQUIRE(isValidRingEntry(ring));
        SessionState s = someState(false);
        const std::size_t layer = entries[i].layer;
        s.samples[layer] = entries[i];
        s.rings[ring.layer] = ring;
        if (entries[i].embed)
        {
            s.audio.push_back({entries[i].audioId, "sample"});
        }
        if (ring.embed && ring.audioId != entries[i].audioId)
        {
            s.audio.push_back({ring.audioId, "ring"});
        }
        Decoded d = distinct();
        if (!d.decode(encodeState(s)) || !d.state.samples[layer] ||
            !sameEntry(*d.state.samples[layer], entries[i]) || !d.state.rings[ring.layer] ||
            !sameRing(*d.state.rings[ring.layer], ring) || d.state.audio != s.audio ||
            bitsOf(d.state.values) != bitsOf(s.values))
        {
            INFO("entry " << i);
            FAIL_CHECK("didn't round trip");
            ++failures;
        }
    }
    CHECK(failures == 0);
}

TEST_CASE("only valid entries are valid", "[core]")
{
    const MediaEntry good = sampleEntry();
    CHECK(isValidMediaEntry(good));
    CHECK_FALSE(isValidMediaEntry(MediaEntry{})); // an empty hash

    const auto broken = [&](auto change) {
        MediaEntry e = good;
        change(e);
        return isValidMediaEntry(e);
    };
    CHECK_FALSE(broken([](MediaEntry &e) { e.sha256Hex[0] = 'E'; }));
    CHECK_FALSE(broken([](MediaEntry &e) { e.sha256Hex.pop_back(); }));
    CHECK_FALSE(broken([](MediaEntry &e) { e.sha256Hex += '0'; }));
    CHECK_FALSE(broken([](MediaEntry &e) { e.sha256Hex[10] = 'g'; }));
    CHECK_FALSE(broken([](MediaEntry &e) { e.absolutePath += '\0'; }));
    CHECK_FALSE(broken([](MediaEntry &e) { e.relativePath = std::string("a\0b", 3); }));
    CHECK_FALSE(broken([](MediaEntry &e) { e.format = std::string(1, '\0'); }));
    CHECK_FALSE(broken([](MediaEntry &e) { e.embed = true; })); // with no payload
    CHECK_FALSE(broken([](MediaEntry &e) { e.audioId = 1; }));  // a payload, not embedded
    CHECK_FALSE(broken([](MediaEntry &e) { e.frames = -1; }));
    CHECK_FALSE(broken([](MediaEntry &e) { e.layer = 4; }));
    CHECK_FALSE(broken([](MediaEntry &e) { e.layer = 255; }));
    CHECK_FALSE(broken([](MediaEntry &e) { e.contentPath = std::string("a\0", 2); }));
    // Not valid UTF-8, but it round trips, so it's valid.
    CHECK(broken([](MediaEntry &e) { e.absolutePath = "/\xc3(\xff\xed\xa0\x80.wav"; }));
    CHECK(broken([](MediaEntry &e) { e.layer = 3; }));
    CHECK(broken([](MediaEntry &e) {
        e.embed = true;
        e.audioId = 0xffffffffu;
    }));

    // A ring.
    const RingEntry ring = ringEntry(2, 0);
    CHECK(isValidRingEntry(ring));
    CHECK(isValidRingEntry(ringEntry(3, 1)));
    RingEntry empty;
    CHECK_FALSE(isValidRingEntry(empty)); // an empty hash
    empty.sha256Hex = std::string(64, '0');
    CHECK(isValidRingEntry(empty)); // no frames, filed at ""
    const auto brokenRing = [&](auto change) {
        RingEntry r = ring;
        change(r);
        return isValidRingEntry(r);
    };
    CHECK_FALSE(brokenRing([](RingEntry &r) { r.layer = 4; }));
    CHECK_FALSE(brokenRing([](RingEntry &r) { r.frames = -1; }));
    CHECK_FALSE(brokenRing([](RingEntry &r) { r.channels = 1; }));
    CHECK_FALSE(brokenRing([](RingEntry &r) { r.sha256Hex[0] = 'A'; }));
    CHECK_FALSE(brokenRing([](RingEntry &r) { r.sha256Hex.pop_back(); }));
    CHECK_FALSE(brokenRing([](RingEntry &r) { r.splices = {-1}; }));
    CHECK_FALSE(brokenRing([](RingEntry &r) { r.splices = {5, 5}; }));
    CHECK_FALSE(brokenRing([](RingEntry &r) { r.splices = {6, 5}; }));
    CHECK_FALSE(brokenRing([](RingEntry &r) { r.splices = {r.frames}; }));
    CHECK_FALSE(brokenRing([](RingEntry &r) { r.absolutePath += '\0'; }));
    CHECK_FALSE(brokenRing([](RingEntry &r) { r.embed = true; })); // no payload, and a path
    CHECK_FALSE(brokenRing([](RingEntry &r) { r.audioId = 2; }));  // a payload, not embedded
    CHECK_FALSE(brokenRing([](RingEntry &r) {
        r.embed = true;
        r.audioId = 2; // embedded with a path, which isn't written
    }));
    CHECK(brokenRing([](RingEntry &r) {
        r.embed = true;
        r.audioId = 2;
        r.absolutePath.clear();
    }));
    CHECK(brokenRing([](RingEntry &r) { r.splices = {r.frames - 1}; }));
    CHECK(brokenRing([](RingEntry &r) { r.splices.clear(); }));
}

TEST_CASE("an invalid entry is left out and the state still loads", "[core]")
{
    // Ruling R9, the release-build half: encodeState leaves out an entry that wouldn't decode
    // (here, the one entry, so MEDI is empty), so the parameters survive. Debug builds assert the
    // precondition instead, so this runs only where NDEBUG is set (the rtsan and release presets).
#if defined(NDEBUG)
    std::vector<MediaEntry> invalid;
    invalid.push_back(MediaEntry{});
    for (const auto &change : std::initializer_list<void (*)(MediaEntry &)>{
             [](MediaEntry &e) { e.sha256Hex = std::string(64, 'A'); },
             [](MediaEntry &e) { e.sha256Hex.pop_back(); },
             [](MediaEntry &e) { e.absolutePath = std::string("/a\0b.wav", 8); },
             [](MediaEntry &e) { e.relativePath = std::string(1, '\0'); },
             [](MediaEntry &e) { e.format = std::string("wav\0", 4); },
             [](MediaEntry &e) { e.embed = true; }})
    {
        MediaEntry e = sampleEntry();
        change(e);
        invalid.push_back(e);
    }
    for (std::size_t i = 0; i < invalid.size(); ++i)
    {
        INFO("entry " << i);
        REQUIRE_FALSE(isValidMediaEntry(invalid[i]));
        SessionState s = someState(false);
        s.samples[0] = invalid[i];
        const auto bytes = encodeState(s);
        CHECK(bytes == encodeState(someState(false)));
        Decoded d = distinct();
        REQUIRE(d.decode(bytes));
        CHECK(bitsOf(d.state.values) == bitsOf(s.values));
        CHECK(d.state.seed == s.seed);
        CHECK(noEntries(d.state));
    }

    // A ring that isn't valid, an entry in another layer's slot, an embedded entry whose payload
    // isn't in `audio`, and payloads with id 0 or an id already used: each is left out, and the
    // rest of fullState is written.
    const SessionState full = fullState();
    const auto without = [&](auto change) {
        SessionState s = full;
        change(s);
        return s;
    };
    for (const auto &[what, broken, expected] :
         std::initializer_list<std::tuple<const char *, SessionState, SessionState>>{
             {"an invalid ring", without([](SessionState &s) { s.rings[1]->frames = -1; }),
              without([](SessionState &s) { s.rings[1].reset(); })},
             {"a ring in another layer's slot",
              without([](SessionState &s) { s.rings[3]->layer = 2; }),
              without([](SessionState &s) { s.rings[3].reset(); })},
             {"a sample in another layer's slot",
              without([](SessionState &s) { s.samples[1]->layer = 0; }),
              without([](SessionState &s) { s.samples[1].reset(); })},
             {"a ring naming no payload", without([](SessionState &s) { s.rings[0]->audioId = 4; }),
              without([](SessionState &s) { s.rings[0].reset(); })},
             {"a sample naming no payload",
              without([](SessionState &s) { s.samples[3]->audioId = 4; }),
              without([](SessionState &s) { s.samples[3].reset(); })},
             {"a payload with id 0",
              without([](SessionState &s) { s.audio.insert(s.audio.begin(), {0, "zero"}); }), full},
             {"a payload id used twice",
              without([](SessionState &s) { s.audio.push_back({3, "again"}); }), full}})
    {
        INFO(what);
        const auto bytes = encodeState(broken);
        CHECK(bytes == encodeState(expected));
        Decoded d = distinct();
        REQUIRE(d.decode(bytes));
        CHECK(d.state.samples == expected.samples);
        CHECK(d.state.rings == expected.rings);
    }
#else
    SKIP("debug builds assert isValidMediaEntry in encodeState");
#endif
}

namespace
{
// A version 2 <sample> and <ring>, as encodeState writes them, for the tests below to edit; the
// ring names payload 1.
constexpr std::string_view kGoodSample2 =
    R"(layer="A" sha256="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" )"
    R"(size="10" path="/a.wav" relative="" format="wav" channels="2" rate="40e7700000000000" )"
    R"(frames="48000" embed="0" content="" audio="0")";
constexpr std::string_view kGoodRing =
    R"(layer="B" frames="10" rate="40e7700000000000" splices="1 2" )"
    R"(sha256="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" size="10" )"
    R"(embed="1" audio="1")";

// A version 2 manifest holding `elements` as they are.
std::string media2(std::string_view elements)
{
    return R"(<media version="2">)" + std::string(elements) + "</media>";
}

// `attributes` with `from` replaced by `to` (which must be there).
std::string edited(std::string_view attributes, std::string_view from, std::string_view to)
{
    std::string out(attributes);
    const auto i = out.find(from);
    REQUIRE(i != std::string::npos);
    out.replace(i, from.size(), to);
    return out;
}

std::string sampleElement(std::string_view attributes)
{
    return "<sample " + std::string(attributes) + " />";
}

std::string ringElement(std::string_view attributes)
{
    return "<ring " + std::string(attributes) + " />";
}
} // namespace

TEST_CASE("MEDI version 2 round-trips four samples and four rings", "[core]")
{
    const SessionState s = fullState();
    for (std::size_t k = 0; k < gg::core::kLayers; ++k)
    {
        REQUIRE(isValidMediaEntry(*s.samples[k]));
        REQUIRE(isValidRingEntry(*s.rings[k]));
    }
    const auto bytes = encodeState(s);
    const auto medi = stateChunkPayload(bytes, "MEDI");
    REQUIRE(medi.has_value());
    // The samples A to D, then the rings A to D.
    std::size_t at = 0;
    for (const std::string_view element :
         {R"(<sample layer="A" )", R"(<sample layer="B" )", R"(<sample layer="C" )",
          R"(<sample layer="D" )", R"(<ring layer="A" )", R"(<ring layer="B" )",
          R"(<ring layer="C" )", R"(<ring layer="D" )"})
    {
        INFO(element);
        const auto found = medi->find(element, at);
        REQUIRE(found != std::string::npos);
        at = found;
    }
    for (const Mode mode : {Mode::Instrument, Mode::Fx})
    {
        Decoded d = distinct();
        REQUIRE(d.decode(bytes, mode));
        for (std::size_t k = 0; k < gg::core::kLayers; ++k)
        {
            INFO("layer " << k);
            REQUIRE(d.state.samples[k].has_value());
            CHECK(sameEntry(*d.state.samples[k], *s.samples[k]));
            REQUIRE(d.state.rings[k].has_value());
            CHECK(sameRing(*d.state.rings[k], *s.rings[k]));
        }
        CHECK(d.state.audio == s.audio);
        CHECK(d.state.memoryCapMiB == 2048u);
        CHECK(bitsOf(d.state.values) == bitsOf(s.values));
        CHECK(d.state.seed == s.seed);
        CHECK(sameRoutes(d.state.routes, s.routes));
        CHECK(d.state.newerMedia == std::nullopt);
        // Saving what was loaded writes the same bytes again.
        CHECK(encodeState(d.state) == bytes);
    }

    // Any subset of the layers, each alone.
    for (std::size_t k = 0; k < gg::core::kLayers; ++k)
    {
        INFO("layer " << k << " alone");
        SessionState one = someState(false);
        one.samples[k] = s.samples[k];
        one.rings[k] = s.rings[k];
        one.audio = s.audio;
        Decoded d = distinct();
        REQUIRE(d.decode(encodeState(one)));
        CHECK(d.state.samples == one.samples);
        CHECK(d.state.rings == one.rings);
    }
}

TEST_CASE("a version 1 manifest loads as layer A's", "[core]")
{
    // A state M1 saved: its patch (version 1, migrating) and a version 1 manifest with one
    // sample, copied literally.
    const std::string m1 = stateContainer(
        {stateChunk("PTCH", kSubProject1State),
         stateChunk(
             "MEDI",
             R"(<media version="1"><sample )"
             R"(sha256="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" )"
             R"(size="10" path="/Samples/&#x26;a.wav" relative="&#x26;a.wav" format="wav" )"
             R"(channels="2" rate="40e7700000000000" frames="48000" embed="0" /></media>)")});
    for (const Mode mode : {Mode::Instrument, Mode::Fx})
    {
        Decoded d = distinct();
        REQUIRE(d.decode(m1, mode));
        CHECK(d.migratedFromVersion == 1u);
        REQUIRE(d.state.samples[0].has_value());
        const MediaEntry &e = *d.state.samples[0];
        CHECK(e.sha256Hex == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        CHECK(e.fileSize == 10u);
        CHECK(e.absolutePath == "/Samples/&a.wav");
        CHECK(e.relativePath == "&a.wav");
        CHECK(e.format == "wav");
        CHECK(e.channels == 2u);
        CHECK(e.sampleRate == 48000.0);
        CHECK(e.frames == 48000);
        CHECK_FALSE(e.embed);
        CHECK(e.layer == 0u);
        CHECK(e.contentPath.empty());
        CHECK(e.audioId == 0u);
        for (std::size_t k = 1; k < gg::core::kLayers; ++k)
        {
            CHECK_FALSE(d.state.samples[k].has_value());
        }
        CHECK(std::ranges::none_of(d.state.rings, [](const auto &r) { return r.has_value(); }));
        CHECK(d.state.audio.empty());
        CHECK(d.state.memoryCapMiB == gg::core::kMemoryCapDefaultMiB);
        CHECK(d.state.newerMedia == std::nullopt);

        // Saved again, it is version 2's layer A.
        const auto medi = stateChunkPayload(encodeState(d.state), "MEDI");
        CHECK(medi ==
              std::string(
                  R"(<media version="2"><sample layer="A" )"
                  R"(sha256="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" )"
                  R"(size="10" path="/Samples/&#x26;a.wav" relative="&#x26;a.wav" )"
                  R"(format="wav" channels="2" rate="40e7700000000000" frames="48000" )"
                  R"(embed="0" content="" audio="0" /></media>)"));
    }

    // Version 1 knows neither `layer` nor the other attributes of version 2: they are ignored,
    // and the sample is still layer A's.
    Decoded d = distinct();
    REQUIRE(d.decode(stateContainer(
        {stateChunk("PTCH", kSubProject1State),
         stateChunk("MEDI", mediaWith(std::string(kGoodSample) +
                                      R"( layer="C" content="x.wav" audio="3")"))})));
    REQUIRE(d.state.samples[0].has_value());
    CHECK(d.state.samples[0]->layer == 0u);
    CHECK(d.state.samples[0]->contentPath.empty());
    CHECK(d.state.samples[0]->audioId == 0u);
    CHECK_FALSE(d.state.samples[2].has_value());
}

TEST_CASE("an embedded ring's payload comes back byte for byte", "[core]")
{
    // Every byte value, NULs included, and a megabyte of the RNG's bytes: AUDI is binary.
    std::string big(std::size_t{1} << 20, '\0');
    for (std::size_t i = 0; i < big.size(); ++i)
    {
        big[i] = static_cast<char>(
            gg::rng::hash({0x5eed'7e57'0000'000bull, 0, 0, gg::rng::Field::SignalNoise, i}));
    }
    SessionState s = someState(false);
    s.rings[0] = ringEntry(0, 1);
    s.rings[2] = ringEntry(2, 2);
    s.audio = {{2, big}, {1, payloadBytes("")}};
    const auto bytes = encodeState(s);
    CHECK(stateChunkPayload(bytes, "AUDI") == audioChunkPayload({{2, big}, {1, payloadBytes("")}}));
    Decoded d = distinct();
    REQUIRE(d.decode(bytes));
    REQUIRE(d.state.audio.size() == 2);
    CHECK(d.state.audio[0].id == 2u);
    CHECK(d.state.audio[0].bytes == big);
    CHECK(d.state.audio[1].id == 1u);
    CHECK(d.state.audio[1].bytes == payloadBytes(""));
    CHECK(d.state.rings == s.rings);

    // An empty payload is a payload too.
    s.audio = {{1, ""}, {2, std::string(1, '\0')}};
    REQUIRE(d.decode(encodeState(s)));
    CHECK(d.state.audio == s.audio);
}

TEST_CASE("an entry naming a missing payload loads with its audio missing", "[core]")
{
    // Review Focus 5, the controller's ruling (a): an embedded entry whose payload isn't there
    // (an older build that kept a newer MEDI but not its AUDI saved it, say) loads with its
    // layer's audio marked missing, and the rest of the state loads; a save then leaves the entry
    // out (R9), losing the reference, not the session.
    const auto ptch = *stateChunkPayload(encodeState(someState(false)), "PTCH");
    const std::string embeddedSample = sampleElement(edited(
        edited(kGoodSample2, R"(embed="0")", R"(embed="1")"), R"(audio="0")", R"(audio="5")"));
    const std::string ring = ringElement(kGoodRing); // names payload 1
    const auto audi = [](std::initializer_list<std::pair<uint32_t, std::string>> payloads) {
        return stateChunk("AUDI", audioChunkPayload(payloads));
    };
    using Flags = std::array<bool, gg::core::kLayers>;
    struct Case
    {
        const char *what;
        std::string state;
        Flags sampleMissing;
        Flags ringMissing;
        std::vector<AudioPayload> audio;
    };
    for (const Case &c : std::initializer_list<Case>{
             {"a sample, no AUDI",
              stateContainer(
                  {stateChunk("PTCH", ptch), stateChunk("MEDI", media2(embeddedSample))}),
              {true, false, false, false},
              {},
              {}},
             {"a sample, other payloads",
              stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", media2(embeddedSample)),
                              audi({{1, "x"}, {6, "y"}})}),
              {true, false, false, false},
              {},
              {}},
             {"a ring, no AUDI",
              stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", media2(ring))}),
              {},
              {false, true, false, false},
              {}},
             {"a ring, an empty AUDI",
              stateContainer(
                  {stateChunk("PTCH", ptch), stateChunk("MEDI", media2(ring)), audi({})}),
              {},
              {false, true, false, false},
              {}},
             {"a ring found, a sample not",
              stateContainer({stateChunk("PTCH", ptch),
                              stateChunk("MEDI", media2(embeddedSample + ring)), audi({{1, "x"}})}),
              {true, false, false, false},
              {},
              {{1, "x"}}},
             {"a sample found, a ring not",
              stateContainer({stateChunk("PTCH", ptch),
                              stateChunk("MEDI", media2(embeddedSample + ring)), audi({{5, "s"}})}),
              {},
              {false, true, false, false},
              {{5, "s"}}}})
    {
        INFO(c.what);
        Decoded d = distinct();
        REQUIRE(d.decode(c.state));
        CHECK(d.state.sampleAudioMissing == c.sampleMissing);
        CHECK(d.state.ringAudioMissing == c.ringMissing);
        CHECK(d.state.audio == c.audio);
        // The entries load as written, ids and all; so does the patch.
        const bool withSample = c.state.find("<sample ") != std::string::npos;
        const bool withRing = c.state.find("<ring ") != std::string::npos;
        CHECK(d.state.samples[0].has_value() == withSample);
        CHECK(d.state.rings[1].has_value() == withRing);
        if (withSample)
        {
            CHECK(d.state.samples[0]->embed);
            CHECK(d.state.samples[0]->audioId == 5u);
        }
        if (withRing)
        {
            CHECK(d.state.rings[1]->audioId == 1u);
        }
        CHECK(d.state.seed == someState(false).seed);
        CHECK(bitsOf(d.state.values) == bitsOf(someState(false).values));

        // Saved, an entry whose audio is missing is left out (in every build: no assert), and one
        // whose audio was found is kept with its payload.
        Decoded again = distinct();
        REQUIRE(again.decode(encodeState(d.state)));
        CHECK(again.state.samples[0].has_value() == (withSample && !c.sampleMissing[0]));
        CHECK(again.state.rings[1].has_value() == (withRing && !c.ringMissing[1]));
        CHECK(again.state.sampleAudioMissing == Flags{});
        CHECK(again.state.ringAudioMissing == Flags{});
        CHECK(again.state.audio == c.audio);
    }
    // With its payload, each loads, nothing missing.
    Decoded d = distinct();
    REQUIRE(d.decode(
        stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", media2(embeddedSample + ring)),
                        audi({{5, "s"}, {1, "r"}})})));
    CHECK(d.state.samples[0]->audioId == 5u);
    CHECK(d.state.rings[1]->audioId == 1u);
    CHECK(d.state.audio == std::vector<AudioPayload>{{5, "s"}, {1, "r"}});
    CHECK(d.state.sampleAudioMissing == Flags{});
    CHECK(d.state.ringAudioMissing == Flags{});
}

TEST_CASE("an unreferenced payload is ignored", "[core]")
{
    // Review Focus 5: a payload no entry names loads, and isn't kept.
    const auto ptch = *stateChunkPayload(encodeState(someState(false)), "PTCH");
    const auto with = [&](std::string_view medi, std::string audi) {
        return stateContainer(
            {stateChunk("PTCH", ptch), stateChunk("MEDI", medi), stateChunk("AUDI", audi)});
    };
    Decoded d = distinct();
    REQUIRE(d.decode(with(media2(ringElement(kGoodRing)),
                          audioChunkPayload({{9, "unnamed"}, {1, "named"}, {4, "unnamed"}}))));
    CHECK(d.state.audio == std::vector<AudioPayload>{{1, "named"}});
    REQUIRE(d.state.rings[1].has_value());
    CHECK(d.state.rings[1]->audioId == 1u);

    // With no entry at all, or an empty AUDI.
    for (const std::string &audi :
         {audioChunkPayload({{9, "unnamed"}}), audioChunkPayload({}), audioChunkPayload({{1, ""}})})
    {
        d = distinct();
        REQUIRE(d.decode(with(kEmptyMedia2, audi)));
        CHECK(noEntries(d.state));
    }

    // The writer doesn't prune: a payload in `audio` is written whether or not an entry names it.
    SessionState s = someState(false);
    s.audio = {{3, "unnamed"}};
    CHECK(stateChunkPayload(encodeState(s), "AUDI") == audioChunkPayload({{3, "unnamed"}}));
}

TEST_CASE("INST round-trips and clamps", "[core]")
{
    // Written as it is when in range, so it comes back as it went.
    for (const uint32_t cap : {64u, 65u, 1024u, 16383u, 16384u})
    {
        INFO(cap);
        SessionState s = someState(false);
        s.memoryCapMiB = cap;
        const auto bytes = encodeState(s);
        CHECK(stateChunkPayload(bytes, "INST") == instanceChunkPayload(std::to_string(cap)));
        Decoded d = distinct();
        REQUIRE(d.decode(bytes));
        CHECK(d.state.memoryCapMiB == cap);
        CHECK(encodeState(d.state) == bytes);
    }
    // Out of range, written clamped.
    for (const auto &[cap, written] : std::initializer_list<std::pair<uint32_t, uint32_t>>{
             {0, 64}, {63, 64}, {16385, 16384}, {0xffffffffu, 16384}})
    {
        INFO(cap);
        SessionState s = someState(false);
        s.memoryCapMiB = cap;
        CHECK(stateChunkPayload(encodeState(s), "INST") ==
              instanceChunkPayload(std::to_string(written)));
    }

    // Read: an integer clamped to the floor and the ceiling; absent, or not an integer, the
    // default; a newer version's known attribute read the same way.
    const auto ptch = *stateChunkPayload(encodeState(someState(false)), "PTCH");
    const auto loaded = [&](std::string_view inst) {
        Decoded d = distinct();
        REQUIRE(d.decode(stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", kEmptyMedia2),
                                         stateChunk("INST", inst)})));
        return d.state.memoryCapMiB;
    };
    CHECK(loaded(instanceChunkPayload("32")) == 64u);
    CHECK(loaded(instanceChunkPayload("0")) == 64u);
    CHECK(loaded(instanceChunkPayload("-5")) == 64u);
    CHECK(loaded(instanceChunkPayload("-99999999999999999999999")) == 64u);
    CHECK(loaded(instanceChunkPayload("20000")) == 16384u);
    CHECK(loaded(instanceChunkPayload("4294967296")) == 16384u);
    CHECK(loaded(instanceChunkPayload("99999999999999999999999")) == 16384u);
    CHECK(loaded(instanceChunkPayload("2048")) == 2048u);
    for (const std::string_view notInteger : {"", "abc", "1.5", "1024x", " 1024", "+1024", "1e3"})
    {
        INFO("'" << notInteger << "'");
        CHECK(loaded(instanceChunkPayload(notInteger)) == 1024u);
    }
    CHECK(loaded(R"(<instance version="1" />)") == 1024u);
    CHECK(loaded(R"(<instance version="2" memoryCapMiB="512" future="yes"><more /></instance>)") ==
          512u);
    CHECK(loaded(R"(<instance version="1" memoryCapMiB="128"><unknown /></instance>)") == 128u);

    // No INST: the default.
    Decoded d = distinct();
    REQUIRE(d.decode(stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", kEmptyMedia2)})));
    CHECK(d.state.memoryCapMiB == 1024u);
}

TEST_CASE("a state without AUDI and INST loads", "[core]")
{
    // A state M2 sub-project 1 saved: PTCH (patch version 2, its routes) and a version 1 MEDI
    // with one sample, and neither AUDI nor INST. Its MEDI is copied literally; its PTCH is
    // toStateXml's, which this sub-project doesn't change.
    const SessionState s = someState(false);
    const auto ptch = gg::core::toStateXml(s.values, s.seed, s.routes);
    const std::string medi = mediaWith(kGoodSample);
    const std::string state = stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", medi)});
    for (const Mode mode : {Mode::Instrument, Mode::Fx})
    {
        Decoded d = distinct();
        REQUIRE(d.decode(state, mode));
        CHECK(d.migratedFromVersion == 0u);
        CHECK(bitsOf(d.state.values) == bitsOf(s.values));
        CHECK(d.state.seed == s.seed);
        CHECK(sameRoutes(d.state.routes, s.routes));
        REQUIRE(d.state.samples[0].has_value());
        CHECK(d.state.samples[0]->absolutePath == "/a.wav");
        CHECK(d.state.samples[0]->sampleRate == 48000.0);
        CHECK(d.state.samples[0]->layer == 0u);
        CHECK(std::ranges::none_of(d.state.rings, [](const auto &r) { return r.has_value(); }));
        CHECK(d.state.audio.empty());
        CHECK(d.state.memoryCapMiB == gg::core::kMemoryCapDefaultMiB);
    }
    // With no sample too.
    Decoded d = distinct();
    REQUIRE(d.decode(stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", kEmptyMedia)})));
    CHECK(noEntries(d.state));
    CHECK(d.state.memoryCapMiB == gg::core::kMemoryCapDefaultMiB);
}

TEST_CASE("the FX writes its entries", "[core]")
{
    // In M1 the FX wrote an empty MEDI. Since sub-project 2 both products write what they hold:
    // the FX's rings, and its layers' samples and captures (a layer whose Source is Sample).
    SessionState s = someState(false);
    s.rings[0] = ringEntry(0, 1);
    s.rings[3] = ringEntry(3, 0);
    MediaEntry e = sampleEntry();
    e.layer = 1;
    s.samples[1] = e;
    s.audio = {{1, payloadBytes("ring A")}};
    const auto bytes = encodeState(s);
    const auto medi = stateChunkPayload(bytes, "MEDI");
    REQUIRE(medi.has_value());
    CHECK(medi->find(R"(<sample layer="B" )") != std::string::npos);
    CHECK(medi->find(R"(<ring layer="A" )") != std::string::npos);
    CHECK(medi->find(R"(<ring layer="D" )") != std::string::npos);
    CHECK(stateChunkPayload(bytes, "AUDI").has_value());
    Decoded d = distinct();
    REQUIRE(d.decode(bytes, Mode::Fx));
    CHECK(d.state.samples == s.samples);
    CHECK(d.state.rings == s.rings);
    CHECK(d.state.audio == s.audio);
    CHECK(encodeState(d.state) == bytes);
}

TEST_CASE("a malformed MEDI version 2 fails", "[core]")
{
    const auto ptch = *stateChunkPayload(encodeState(someState(false)), "PTCH");
    const std::string audi = stateChunk("AUDI", audioChunkPayload({{1, "x"}}));
    const auto state = [&](const std::string &medi) {
        return stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", medi), audi});
    };
    // The good sample and ring, the baseline every bad one edits.
    Decoded d = distinct();
    REQUIRE(d.decode(state(media2(sampleElement(kGoodSample2) + ringElement(kGoodRing)))));
    CHECK(d.state.samples[0].has_value());
    CHECK(d.state.rings[1].has_value());
    CHECK(d.state.rings[1]->splices == std::vector<int64_t>{1, 2});
    CHECK(d.state.rings[1]->channels == 2u);
    // Unknown elements and attributes are ignored, as in version 1; so is a filed ring's `audio`
    // and an embedded one's `path`, which encodeState never writes.
    REQUIRE(d.decode(
        state(media2("<capture />" + sampleElement(std::string(kGoodSample2) + R"( x="1")") +
                     ringElement(std::string(kGoodRing) + R"( path="/p.wav")") +
                     ringElement(edited(edited(kGoodRing, R"(embed="1")", R"(embed="0")"),
                                        R"(audio="1")", R"(path="" audio="7")")
                                     .replace(7, 1, "C"))))));
    CHECK(d.state.rings[1]->absolutePath.empty());
    REQUIRE(d.state.rings[2].has_value());
    CHECK(d.state.rings[2]->audioId == 0u);
    CHECK_FALSE(d.state.rings[2]->embed);
    d = distinct();

    const auto sample = [&](std::string_view from, std::string_view to) {
        return media2(sampleElement(edited(kGoodSample2, from, to)));
    };
    const auto ring = [&](std::string_view from, std::string_view to) {
        return media2(ringElement(edited(kGoodRing, from, to)));
    };
    const Decoded before = distinct();
    for (const std::string &bad : {
             sample(R"(layer="A" )", ""),
             sample(R"(layer="A")", R"(layer="E")"),
             sample(R"(layer="A")", R"(layer="a")"),
             sample(R"(layer="A")", R"(layer="0")"),
             sample(R"(layer="A")", R"(layer="AB")"),
             sample(R"(layer="A")", R"(layer="")"),
             sample(R"( content="")", ""),
             sample(R"( audio="0")", ""),
             sample(R"(audio="0")", R"(audio="1")"), // a payload, not embedded
             sample(R"(embed="0")", R"(embed="1")"), // embedded, audio 0
             sample(R"(embed="0")", R"(embed="2")"),
             sample(R"(audio="0")", R"(audio="-1")"),
             sample(R"(audio="0")", R"(audio="4294967296")"),
             sample(R"(audio="0")", R"(audio=" 0")"),
             sample(R"(sha256="e3b0)", R"(sha256="E3B0)"),
             sample(R"(rate="40e7700000000000")", R"(rate="48000")"),
             sample(R"(frames="48000")", R"(frames="-1")"),
             sample(R"(path="/a.wav" )", ""),
             media2(sampleElement(kGoodSample2) + sampleElement(kGoodSample2)), // two in layer A
             ring(R"(layer="B" )", ""),
             ring(R"(layer="B")", R"(layer="E")"),
             ring(R"(frames="10")", R"(frames="-1")"),
             ring(R"(frames="10" )", ""),
             ring(R"(rate="40e7700000000000")", R"(rate="40e770000000000")"),
             ring(R"(rate="40e7700000000000" )", ""),
             ring(R"( splices="1 2")", ""),
             ring(R"(splices="1 2")", R"(splices="2 1")"),
             ring(R"(splices="1 2")", R"(splices="1 1")"),
             ring(R"(splices="1 2")", R"(splices="1  2")"),
             ring(R"(splices="1 2")", R"(splices=" 1 2")"),
             ring(R"(splices="1 2")", R"(splices="1 2 ")"),
             ring(R"(splices="1 2")", R"(splices="1 10")"), // at the frame count
             ring(R"(splices="1 2")", R"(splices="-1 2")"),
             ring(R"(splices="1 2")", R"(splices="1,2")"),
             ring(R"(splices="1 2")", R"(splices="a")"),
             ring(R"(sha256="e3b0)", R"(sha256="e3b")"),
             ring(R"(size="10" )", ""),
             ring(R"(size="10")", R"(size="-1")"),
             ring(R"( embed="1")", ""),
             ring(R"(embed="1")", R"(embed="yes")"),
             ring(R"( audio="1")", ""),
             ring(R"(audio="1")", R"(audio="0")"),
             ring(R"(embed="1" audio="1")", R"(embed="0")"),          // filed, no path
             media2(ringElement(kGoodRing) + ringElement(kGoodRing)), // two in layer B
         })
    {
        INFO("MEDI '" << bad << "'");
        CHECK_FALSE(d.decode(state(bad)));
        CHECK(d.sameAs(before));
    }
}

TEST_CASE("a malformed AUDI fails", "[core]")
{
    const auto ptch = *stateChunkPayload(encodeState(someState(false)), "PTCH");
    const auto state = [&](const std::string &audi) {
        return stateContainer(
            {stateChunk("PTCH", ptch), stateChunk("MEDI", kEmptyMedia2), stateChunk("AUDI", audi)});
    };
    const std::string good = audioChunkPayload({{1, "abc"}, {2, ""}});
    Decoded d = distinct();
    REQUIRE(d.decode(state(good)));
    d = distinct();
    std::string longLength = good;
    longLength[4 + 4] = '\x04'; // the first payload's length one too long
    const Decoded before = distinct();
    for (const auto &[what, bad] : std::initializer_list<std::pair<const char *, std::string>>{
             {"empty", ""},
             {"a partial count", std::string("\x01\x00\x00", 3)},
             {"a count too large", audioChunkPayload({{1, "abc"}, {2, ""}}, 3)},
             {"a count of 2^32 - 1", audioChunkPayload({{1, "abc"}}, 0xffffffffu)},
             {"a count too small", audioChunkPayload({{1, "abc"}, {2, ""}}, 1)},
             {"trailing bytes", good + "x"},
             {"a payload past the end", longLength},
             {"a partial payload header", audioChunkPayload({{1, "abc"}}, 2) + "\x02\x00\x00\x00"},
             {"id 0", audioChunkPayload({{0, "abc"}})},
             {"an id twice", audioChunkPayload({{7, "abc"}, {7, "def"}})},
             {"a length of 2^64 - 1", gg::test::littleEndian(1, 4) + gg::test::littleEndian(1, 4) +
                                          std::string(8, '\xff') + "abc"}})
    {
        INFO(what);
        CHECK_FALSE(d.decode(state(bad)));
        CHECK(d.sameAs(before));
    }
}

TEST_CASE("a malformed INST fails", "[core]")
{
    const auto ptch = *stateChunkPayload(encodeState(someState(false)), "PTCH");
    const Decoded before = distinct();
    for (const std::string &bad : {
             std::string{},
             std::string{"not xml"},
             std::string{"<instance version=\"1\" memoryCapMiB=\"1024\">"},
             std::string{"<settings version=\"1\" memoryCapMiB=\"1024\" />"},
             std::string{"<instance memoryCapMiB=\"1024\" />"},
             std::string{"<instance version=\"0\" memoryCapMiB=\"1024\" />"},
             std::string{"<instance version=\"1x\" memoryCapMiB=\"1024\" />"},
             kDefaultInst + "junk",
             kDefaultInst + kDefaultInst,
             "<!-- c -->" + kDefaultInst,
             R"(<?xml version="1.0"?>)" + kDefaultInst,
             kDefaultInst + std::string("\0", 1),
             std::string{"<instance version=\"1\" memoryCapMiB=1024 />"},
         })
    {
        INFO("INST '" << bad << "'");
        Decoded d = distinct();
        CHECK_FALSE(
            d.decode(stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", kEmptyMedia2),
                                     stateChunk("INST", bad)})));
        CHECK(d.sameAs(before));
    }
}

TEST_CASE("a ring holds at most kMaxSplices marks", "[core]")
{
    // The engine's splices are a fixed array of kMaxSplices (32): more marks than that are
    // malformed, as an unordered list is, in the entry and in MEDI.
    STATIC_REQUIRE(gg::core::kMaxSplices == 32);
    RingEntry ring = ringEntry(1, 0);
    ring.splices.clear();
    for (int64_t k = 0; k < 32; ++k)
    {
        ring.splices.push_back(k * 1000);
    }
    REQUIRE(isValidRingEntry(ring));
    SessionState s = someState(false);
    s.rings[1] = ring;
    Decoded d = distinct();
    REQUIRE(d.decode(encodeState(s)));
    REQUIRE(d.state.rings[1].has_value());
    CHECK(d.state.rings[1]->splices.size() == 32);

    ring.splices.push_back(32 * 1000);
    CHECK_FALSE(isValidRingEntry(ring));

    // 33 marks in MEDI fail the state.
    std::string marks;
    for (int k = 0; k < 33; ++k)
    {
        marks += (k > 0 ? " " : "") + std::to_string(k);
    }
    const auto ptch = *stateChunkPayload(encodeState(someState(false)), "PTCH");
    const auto with = [&](const std::string &splices) {
        return stateContainer(
            {stateChunk("PTCH", ptch),
             stateChunk("MEDI", media2(ringElement(
                                    edited(edited(kGoodRing, R"(frames="10")", R"(frames="100")"),
                                           R"(splices="1 2")", R"(splices=")" + splices + "\"")))),
             stateChunk("AUDI", audioChunkPayload({{1, "x"}}))});
    };
    const Decoded before = distinct();
    d = distinct();
    CHECK_FALSE(d.decode(with(marks)));
    CHECK(d.sameAs(before));
    marks.resize(marks.rfind(' ')); // 32
    REQUIRE(d.decode(with(marks)));
    CHECK(d.state.rings[1]->splices.size() == 32);
}
