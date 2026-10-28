// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The session state: the GGST container with its PTCH (the patch XML) and MEDI (the media
// manifest) chunks (spec §6.6, INTENT §18), through gg-core's public API only
// (gg/core/SessionState.h). Like patch_tests.cpp, this file never includes sst-plugininfra or
// tinyxml (ruling M1-l). The containers it can't get from encodeState (unknown chunks, missing
// ones, other versions) it builds byte by byte with support/StateContainer.h.

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
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/ParamIds.h"
#include "gg/core/PatchState.h"
#include "gg/core/Rng.h"
#include "gg/core/SessionState.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "support/StateContainer.h"

using gg::core::decodeState;
using gg::core::encodeState;
using gg::core::isValidMediaEntry;
using gg::core::kParamCount;
using gg::core::MediaEntry;
using gg::core::Mode;
using gg::core::ParamSlot;
using gg::core::ParamValues;
using gg::core::SessionState;
using gg::test::stateChunk;
using gg::test::stateChunkPayload;
using gg::test::stateContainer;

namespace
{
// A state sub-project 1 saved: toStateXml's output for kSubProject1Values and seed 77, copied
// literally. Every state saved before the container loads this way.
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

// The values kSubProject1State holds, slot by slot.
constexpr ParamValues kSubProject1Values{0.8f,  -6.0f, 0.1f,     -4.25f, 6.7f,
                                         0.25f, 7.0f,  1.23e-5f, 0.0f,   1.0f};

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
    if (withSample)
    {
        s.sample = sampleEntry();
    }
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
               state.sample == o.state.sample && migratedFromVersion == o.migratedFromVersion;
    }
};

// Values, a seed, a sample and a migration report that no decode here produces.
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
    d.state.sample = e;
    d.migratedFromVersion = 12345;
    return d;
}

constexpr std::string_view kEmptyMedia = R"(<media version="1" />)";

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

TEST_CASE("the container's layout is GGST, version 1, PTCH then MEDI", "[core]")
{
    STATIC_REQUIRE(gg::core::kMaxStateBytes == std::size_t{64} << 20);

    const SessionState s = someState(false);
    const auto bytes = encodeState(s);
    const auto ptch = gg::core::toStateXml(s.values, s.seed);

    // The magic, the version as a little-endian u32, then PTCH holding toStateXml's bytes
    // unchanged, then an empty MEDI, and nothing else.
    const std::string expected =
        stateContainer({stateChunk("PTCH", ptch), stateChunk("MEDI", kEmptyMedia)});
    CHECK(bytes == expected);
    CHECK(bytes.substr(0, 8) == std::string_view("GGST\x01\x00\x00\x00", 8));
    CHECK(bytes.substr(8, 12) ==
          std::string("PTCH") + gg::test::littleEndian(ptch.size(), 8)); // the length is LE

    // With a sample, MEDI holds one <sample>: the rate as its double's bits, embed 0.
    SessionState withSample = someState(false);
    MediaEntry e = sampleEntry();
    e.absolutePath = "/a.wav";
    e.relativePath = "";
    e.sampleRate = 48000.0;
    withSample.sample = e;
    const auto medi = stateChunkPayload(encodeState(withSample), "MEDI");
    REQUIRE(medi.has_value());
    CHECK(*medi == R"(<media version="1"><sample )"
                   R"(sha256="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" )"
                   R"(size="81985529216486895" path="/a.wav" relative="" format="flac" )"
                   R"(channels="6" rate="40e7700000000000" frames="140737488355327" embed="0" />)"
                   R"(</media>)");
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
            CHECK(d.state.sample == s.sample);
            CHECK(d.migratedFromVersion == 0u);
            // Saving what was loaded writes the same bytes again.
            CHECK(encodeState(d.state) == bytes);
        }
    }

    // Every field at its extremes, and a path in each of the UTF-8 lengths.
    SessionState s = someState(true);
    s.seed = std::numeric_limits<uint64_t>::max();
    s.sample->fileSize = std::numeric_limits<uint64_t>::max();
    s.sample->channels = std::numeric_limits<uint32_t>::max();
    s.sample->sampleRate = std::numeric_limits<double>::denorm_min();
    s.sample->frames = std::numeric_limits<int64_t>::min();
    s.sample->absolutePath = "C:\\Samples\\a\xc3\xa4\xe2\x9c\x93\xf0\x9f\x8e\xb5.aiff";
    s.sample->relativePath = "tab\there\nnewline";
    s.sample->format = "aiff";
    Decoded d = distinct();
    REQUIRE(d.decode(encodeState(s)));
    CHECK(d.state.sample == s.sample);
    CHECK(d.state.seed == s.seed);

    // A sample with every string empty, as a default MediaEntry has them, but for its hash.
    s.sample = MediaEntry{};
    s.sample->sha256Hex = std::string(64, '0');
    REQUIRE(d.decode(encodeState(s)));
    CHECK(d.state.sample == s.sample);
}

TEST_CASE("a pre-container state loads with no sample", "[core]")
{
    // The bare XML of sub-projects 1 and 2: its whole content is PTCH, with an empty MEDI.
    CHECK(gg::core::toStateXml(kSubProject1Values, 77) == kSubProject1State);

    Decoded d = distinct();
    REQUIRE(d.decode(kSubProject1State));
    CHECK(bitsOf(d.state.values) == bitsOf(kSubProject1Values));
    CHECK(d.state.seed == 77u);
    CHECK(d.state.sample == std::nullopt);
    CHECK(d.migratedFromVersion == 0u);

    // As before the container, bytes after the XML's NUL are ignored: an old state never fails
    // to load now that loaded then.
    Decoded nul = distinct();
    REQUIRE(nul.decode(std::string(kSubProject1State) + std::string("\0junk", 5)));
    CHECK(bitsOf(nul.state.values) == bitsOf(kSubProject1Values));
    CHECK(nul.state.sample == std::nullopt);

    // Saving it again writes a container.
    const auto bytes = encodeState(d.state);
    CHECK(bytes.starts_with("GGST"));
    CHECK(stateChunkPayload(bytes, "PTCH") == std::string(kSubProject1State));

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
        REQUIRE(gg::core::fromStateXml(noPosition, values, seed, mode));
        Decoded bare = distinct();
        REQUIRE(bare.decode(noPosition, mode));
        CHECK(bitsOf(bare.state.values) == bitsOf(values));
        Decoded wrapped = distinct();
        REQUIRE(wrapped.decode(
            stateContainer({stateChunk("PTCH", noPosition), stateChunk("MEDI", kEmptyMedia)}),
            mode));
        CHECK(bitsOf(wrapped.state.values) == bitsOf(values));
        CHECK(wrapped.state.values[at(ParamSlot::Position)] ==
              gg::core::paramMeta(ParamSlot::Position, mode).defaultVal);
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
    // whose payload looks like a chunk; and AUDI, reserved for M2.
    const std::string xtra = stateChunk("XTRA", std::string("\x00\xff GGST PTCH", 11));
    for (const auto &state :
         {stateContainer({stateChunk("PTCH", *ptch), xtra, stateChunk("MEDI", *medi)}),
          stateContainer({xtra, stateChunk("PTCH", *ptch), stateChunk("MEDI", *medi)}),
          stateContainer({stateChunk("PTCH", *ptch), stateChunk("MEDI", *medi), xtra}),
          stateContainer(
              {stateChunk("MEDI", *medi), stateChunk("XTRA", ""), stateChunk("PTCH", *ptch)}),
          stateContainer({stateChunk("PTCH", *ptch), stateChunk("XTRA", stateChunk("PTCH", "x")),
                          stateChunk("MEDI", *medi), stateChunk("AUDI", "fLaC")})})
    {
        Decoded d = distinct();
        REQUIRE(d.decode(state));
        CHECK(bitsOf(d.state.values) == bitsOf(s.values));
        CHECK(d.state.seed == s.seed);
        CHECK(d.state.sample == s.sample);
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
        for (std::size_t n = 0; n < good.size(); ++n)
        {
            INFO("cut at " << n << " of " << good.size());
            REQUIRE_FALSE(d.decode(std::string_view(good).substr(0, n)));
            REQUIRE(d.sameAs(before));
        }
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
        CHECK(d.state.sample.has_value());
        d = distinct();

        for (const std::string &bad : {
                 std::string{},
                 std::string{"<media version=\"1\">"},
                 std::string{"<media version=\"1\"><sample "},
                 std::string{"not xml"},
                 std::string{"<manifest version=\"1\" />"},
                 std::string{"<media version=\"2\" />"}, // newer than this build reads
                 std::string{"<media version=\"0\" />"},
                 std::string{"<media version=\"-1\" />"},
                 std::string(kEmptyMedia) + "<media version=\"1\" />", // trailing content
                 std::string(kEmptyMedia) + "junk",
                 std::string(kEmptyMedia) + "<!-- note -->",
                 "<!-- note -->" + std::string(kEmptyMedia),
                 R"(<?xml version="1.0" ?>)" + std::string(kEmptyMedia),
                 std::string{"<media />"},
                 std::string{"<media version=\"one\" />"},
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
        newer.replace(newer.find(R"(version="1")"), 11, R"(version="99")");
        REQUIRE(d.decode(stateContainer({stateChunk("PTCH", newer), stateChunk("MEDI", medi)})));
        CHECK(d.migratedFromVersion == 99u);
        CHECK(bitsOf(d.state.values) == bitsOf(s.values));
        CHECK(d.state.sample == s.sample);

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
        CHECK(out.sample == s.sample);
    }
}

namespace
{
// An entry from the engine's counter-based RNG: every field random, the strings any bytes but
// NUL (so often not valid UTF-8), of 0 to 47 bytes.
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
    e.frames = static_cast<int64_t>(next());
    return e;
}

bool sameEntry(const MediaEntry &a, const MediaEntry &b)
{
    // The rate by its bits, so a NaN counts as itself.
    return a.sha256Hex == b.sha256Hex && a.fileSize == b.fileSize &&
           a.absolutePath == b.absolutePath && a.relativePath == b.relativePath &&
           a.format == b.format && a.channels == b.channels &&
           std::bit_cast<uint64_t>(a.sampleRate) == std::bit_cast<uint64_t>(b.sampleRate) &&
           a.frames == b.frames && a.embed == b.embed;
}
} // namespace

TEST_CASE("a state nested deeper than 64 elements fails, in PTCH or MEDI", "[core]")
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
}

TEST_CASE("every valid entry round trips, whatever bytes its strings hold", "[core]")
{
    // Ruling R9: decodeState loads whatever encodeState writes. 2,000 random valid entries, then
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

    std::size_t failures = 0;
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        REQUIRE(isValidMediaEntry(entries[i]));
        SessionState s = someState(true);
        s.sample = entries[i];
        Decoded d = distinct();
        if (!d.decode(encodeState(s)) || !d.state.sample ||
            !sameEntry(*d.state.sample, entries[i]) || bitsOf(d.state.values) != bitsOf(s.values))
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
    CHECK_FALSE(broken([](MediaEntry &e) { e.embed = true; }));
    // Not valid UTF-8, but it round trips, so it's valid.
    CHECK(broken([](MediaEntry &e) { e.absolutePath = "/\xc3(\xff\xed\xa0\x80.wav"; }));
}

TEST_CASE("an invalid entry is left out and the state still loads", "[core]")
{
    // Ruling R9, the release-build half: encodeState writes an empty MEDI in place of an entry
    // that wouldn't decode, so the parameters survive. Debug builds assert the precondition
    // instead, so this runs only where NDEBUG is set (the rtsan and release presets).
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
        s.sample = invalid[i];
        const auto bytes = encodeState(s);
        CHECK(bytes == encodeState(someState(false)));
        Decoded d = distinct();
        REQUIRE(d.decode(bytes));
        CHECK(bitsOf(d.state.values) == bitsOf(s.values));
        CHECK(d.state.seed == s.seed);
        CHECK(d.state.sample == std::nullopt);
    }
#else
    SKIP("debug builds assert isValidMediaEntry in encodeState");
#endif
}
