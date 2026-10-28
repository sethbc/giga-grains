// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The patch: the ten slice parameters (spec §4) and their versioned XML state, through gg-core's
// public API only (gg/core/ParamIds.h, gg/core/PatchState.h). This file never includes
// sst-plugininfra or tinyxml (ruling M1-l). The IDs, names, ranges and defaults are spelled out
// literally, from Global Constraints' parameter table, so a change to gg/core/ParamIds.h or
// ParamIds.cpp fails these tests. CLAP parameter IDs are frozen at the first public release and
// append-only (INTENT §18).

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <clap/clap.h>

#include "gg/core/ParamIds.h"
#include "gg/core/PatchState.h"
#include "gg/core/Rng.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

using gg::core::fromStateXml;
using gg::core::kParamCount;
using gg::core::Mode;
using gg::core::ParamSlot;
using gg::core::ParamValues;
using gg::core::toStateXml;

namespace
{
constexpr uint32_t kAutoMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;

struct Expected
{
    ParamSlot slot;
    clap_id id;
    std::string_view name;
    float minVal, maxVal, defaultVal;
    uint32_t flags;
};

// Global Constraints' parameter table, copied literally: the FX's. The instrument's differs only
// in Position's default (kInstrumentPositionDefault) and display.
constexpr std::array<Expected, kParamCount> kTable{{
    {ParamSlot::Mix, 0x000001, "Mix", 0.f, 1.f, 0.5f, kAutoMod},
    {ParamSlot::Level, 0x000002, "Level", -60.f, 12.f, 0.0f, kAutoMod},
    {ParamSlot::Position, 0x010001, "Position", 0.f, 1.f, 0.28967658f, kAutoMod},
    {ParamSlot::Size, 0x010002, "Size", -7.643856189774724f, 1.f, -2.736965594166206f, kAutoMod},
    {ParamSlot::Density, 0x010003, "Density", -2.f, 8.643856189774725f, 4.0f, kAutoMod},
    {ParamSlot::Distribution, 0x010004, "Distribution", 0.f, 1.f, 0.5f, kAutoMod},
    {ParamSlot::Pitch, 0x010005, "Pitch", -24.f, 24.f, 0.0f, kAutoMod},
    {ParamSlot::Spray, 0x010006, "Spray", 0.f, 1.f, 0.36033038f, kAutoMod},
    {ParamSlot::Stereo, 0x010007, "Stereo", 0.f, 1.f, 0.5f, kAutoMod},
    {ParamSlot::Freeze, 0x010008, "Freeze", 0.f, 1.f, 0.0f, kAutoMod | CLAP_PARAM_IS_STEPPED},
}};

/// The instrument's Position: 0–100% of the loaded file, 0% by default (spec §6.8).
constexpr float kInstrumentPositionDefault = 0.0f;

uint32_t bitsOf(float f) { return std::bit_cast<uint32_t>(f); }

ParamValues defaults()
{
    ParamValues v{};
    for (size_t i = 0; i < kParamCount; ++i)
    {
        v[i] = kTable[i].defaultVal;
    }
    return v;
}

// A random value in [lo, hi], from the engine's counter-based RNG (no std:: distributions).
float randomIn(float lo, float hi, uint64_t index)
{
    const auto h =
        gg::rng::hash({0x5eed'7e57'0000'0005ull, 0, 0, gg::rng::Field::SignalNoise, index});
    const double u = gg::rng::uniform01(h);
    return static_cast<float>(static_cast<double>(lo) +
                              (static_cast<double>(hi) - static_cast<double>(lo)) * u);
}

std::string replaceOnce(std::string s, std::string_view from, std::string_view to)
{
    const auto at = s.find(from);
    REQUIRE(at != std::string::npos);
    s.replace(at, from.size(), to);
    return s;
}

// What a load writes: the values, the seed and the migration report. A failed load must leave
// all three untouched: this is the bad-state snapshot, `migratedFromVersion` included (carry-over
// T5 105). The patch's `dirty` flag never reaches the public API: each fromStateXml loads into a
// patch of its own, so none outlives the call. session_state_tests.cpp snapshots the container
// the same way, and the plugin tests check that a rejected load never marks the host's state
// dirty.
struct Loaded
{
    ParamValues values{};
    uint64_t seed{0};
    uint32_t migratedFromVersion{0};

    bool load(std::string_view xml, Mode mode = Mode::Fx)
    {
        return fromStateXml(xml, values, seed, mode, &migratedFromVersion);
    }

    std::array<uint32_t, kParamCount> valueBits() const
    {
        std::array<uint32_t, kParamCount> b{};
        for (size_t i = 0; i < kParamCount; ++i)
        {
            b[i] = bitsOf(values[i]);
        }
        return b;
    }
    bool sameAs(const Loaded &o) const
    {
        return valueBits() == o.valueBits() && seed == o.seed &&
               migratedFromVersion == o.migratedFromVersion;
    }
    float operator[](ParamSlot s) const { return values[static_cast<size_t>(s)]; }
};

// Values, seed and migration report that differ from every load's result.
Loaded distinct()
{
    Loaded l;
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto &e = kTable[i];
        l.values[i] = e.minVal + 0.25f * (e.maxVal - e.minVal);
        REQUIRE(l.values[i] != e.defaultVal);
    }
    l.seed = 42;
    l.migratedFromVersion = 12345;
    return l;
}

// A state written by hand, the way PatchBase writes it: <p id> in decimal.
std::string handState(std::string_view params, std::string_view version = "1",
                      std::string_view extra = "")
{
    std::string s = R"(<patch id="org.sethbc.giga-grains.patch" version=")";
    s += version;
    s += R"(" name="Hand"><params>)";
    s += params;
    s += "</params>";
    s += extra;
    s += "</patch>";
    return s;
}
} // namespace

TEST_CASE("param ids, names, ranges and defaults are pinned", "[core]")
{
    STATIC_REQUIRE(kParamCount == 10);
    STATIC_REQUIRE(std::is_same_v<ParamValues, std::array<float, 10>>);

    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto &e = kTable[i];
        INFO("param " << e.name);
        REQUIRE(static_cast<size_t>(e.slot) == i);

        CHECK(gg::core::paramId(e.slot) == e.id);
        CHECK(e.id < 0xB00000u);
        CHECK(e.id != 0u);
        REQUIRE(gg::core::slotForId(e.id).has_value());
        CHECK(*gg::core::slotForId(e.id) == e.slot);

        const auto &m = gg::core::paramMeta(e.slot, Mode::Fx);
        CHECK(m.id == e.id);
        CHECK(m.name == e.name);
        CHECK(m.minVal == e.minVal);
        CHECK(m.maxVal == e.maxVal);
        CHECK(m.defaultVal == e.defaultVal);
        CHECK(m.flags == e.flags);
        // Static storage, built once: the same object every call.
        CHECK(&gg::core::paramMeta(e.slot, Mode::Fx) == &m);
    }
    CHECK(gg::core::paramId(ParamSlot::Count) == CLAP_INVALID_ID);

    // The instrument's table is the FX's but for Position's default (spec §6.8): the same IDs,
    // names, ranges and flags, so both products share one state format.
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto &e = kTable[i];
        INFO("instrument param " << e.name);
        const auto &m = gg::core::paramMeta(e.slot, Mode::Instrument);
        CHECK(m.id == e.id);
        CHECK(m.name == e.name);
        CHECK(m.minVal == e.minVal);
        CHECK(m.maxVal == e.maxVal);
        CHECK(m.defaultVal ==
              (e.slot == ParamSlot::Position ? kInstrumentPositionDefault : e.defaultVal));
        CHECK(m.flags == e.flags);
        CHECK(m.type == gg::core::paramMeta(e.slot, Mode::Fx).type);
        CHECK(&gg::core::paramMeta(e.slot, Mode::Instrument) == &m);
    }

    // Freeze is the only stepped parameter; it's an on/off switch.
    CHECK(gg::core::paramMeta(ParamSlot::Freeze, Mode::Fx).type ==
          sst::basic_blocks::params::ParamMetaData::INT);

    // IDs that aren't ours: the reserved index 0, scopes and indices one past the table, and
    // the "unknown id" of the bad-state test.
    for (clap_id unknown :
         {0x000000u, 0x000003u, 0x010000u, 0x010009u, 0x020001u, 0x777777u, CLAP_INVALID_ID})
    {
        INFO("id " << unknown);
        CHECK_FALSE(gg::core::slotForId(unknown).has_value());
    }
}

TEST_CASE("the state lists the ten params in slot order", "[core]")
{
    CHECK(gg::core::kPatchVersion == 1u);
    const auto state = toStateXml(defaults(), 0);
    CHECK(state.starts_with(R"(<patch id="org.sethbc.giga-grains.patch" version="1" )"));

    // Each param once, in slot order, its id in decimal, with its float bits.
    size_t from = 0;
    for (const auto &e : kTable)
    {
        INFO("param " << e.name);
        const auto tag = R"(<p id=")" + std::to_string(e.id) + R"(" )";
        const auto at = state.find(tag);
        REQUIRE(at != std::string::npos);
        CHECK(at >= from);
        CHECK(state.find(tag, at + 1) == std::string::npos);
        from = at;

        char bits[9];
        std::snprintf(bits, sizeof(bits), "%08x", static_cast<unsigned>(bitsOf(e.defaultVal)));
        CHECK(state.find(std::string(R"(bits=")") + bits + R"(")", at) < state.find("/>", at));
    }
    CHECK(state.find(R"(<engine seed="0" />)") != std::string::npos);

    // The defaults load back as the defaults.
    Loaded l = distinct();
    REQUIRE(l.load(state));
    CHECK(l.valueBits() == Loaded{defaults(), 0, 0}.valueBits());
    CHECK(l.seed == 0u);
    CHECK(l.migratedFromVersion == 0u);
}

TEST_CASE("a param missing from a state takes the product's default", "[core]")
{
    // A state whose Position isn't the default, with Position's element cut out.
    ParamValues saved = defaults();
    saved[static_cast<size_t>(ParamSlot::Position)] = 0.7f;
    saved[static_cast<size_t>(ParamSlot::Size)] = -1.0f;
    const auto full = toStateXml(saved, 4);
    const auto from = full.find(R"(<p id="65537" )");
    REQUIRE(from != std::string::npos);
    const auto to = full.find("/>", from);
    REQUIRE(to != std::string::npos);
    const auto missing = full.substr(0, from) + full.substr(to + 2);
    REQUIRE(missing.find(R"(id="65537")") == std::string::npos);

    Loaded instrument = distinct();
    REQUIRE(instrument.load(missing, Mode::Instrument));
    CHECK(bitsOf(instrument[ParamSlot::Position]) == bitsOf(kInstrumentPositionDefault));
    CHECK(instrument[ParamSlot::Size] == -1.0f);
    CHECK(instrument.seed == 4u);

    Loaded fx = distinct();
    REQUIRE(fx.load(missing, Mode::Fx));
    CHECK(bitsOf(fx[ParamSlot::Position]) == bitsOf(0.28967658f));
    CHECK(fx[ParamSlot::Size] == -1.0f);
    CHECK(fx.seed == 4u);

    // A state that has it loads its value in either product.
    for (const Mode mode : {Mode::Instrument, Mode::Fx})
    {
        Loaded l = distinct();
        REQUIRE(l.load(full, mode));
        CHECK(l.valueBits() == Loaded{saved, 4, 0}.valueBits());
    }
}

TEST_CASE("display text follows the engine mapping", "[core]")
{
    // Position and Spray display seconds = A (2^(8v) - 1); Size seconds = 2^v; Density
    // Hz = 2^v. The ends and the defaults have exact decimal texts.
    const auto text = [](ParamSlot s, float v) {
        auto t = gg::core::paramMeta(s, Mode::Fx).valueToString(v);
        REQUIRE(t.has_value());
        return *t;
    };
    CHECK(text(ParamSlot::Mix, 0.5f) == "50.00 %");
    CHECK(text(ParamSlot::Level, 0.f) == "0.00 dB");
    CHECK(text(ParamSlot::Level, -60.f) == "-60.00 dB");
    CHECK(text(ParamSlot::Position, 0.f) == "0.00 s");
    CHECK(text(ParamSlot::Position, 0.28967658f) == "0.25 s");
    CHECK(text(ParamSlot::Position, 1.f) == "16.00 s");
    CHECK(text(ParamSlot::Size, -7.643856189774724f) == "5.0 ms");
    CHECK(text(ParamSlot::Size, -2.736965594166206f) == "150.0 ms");
    CHECK(text(ParamSlot::Size, 1.f) == "2.00 s");
    CHECK(text(ParamSlot::Density, -2.f) == "0.25 Hz");
    CHECK(text(ParamSlot::Density, 4.f) == "16.00 Hz");
    CHECK(text(ParamSlot::Density, 8.643856189774725f) == "400.00 Hz");
    CHECK(text(ParamSlot::Distribution, 0.5f) == "50.00 %");
    CHECK(text(ParamSlot::Pitch, -24.f) == "-24.00 semitones");
    CHECK(text(ParamSlot::Spray, 0.f) == "0.00 s");
    CHECK(text(ParamSlot::Spray, 0.36033038f) == "0.05 s");
    CHECK(text(ParamSlot::Spray, 1.f) == "2.00 s");
    CHECK(text(ParamSlot::Stereo, 0.5f) == "50.00 %");
    CHECK(text(ParamSlot::Freeze, 0.f) == "Off");
    CHECK(text(ParamSlot::Freeze, 1.f) == "On");
}

TEST_CASE("value text round trip", "[core]")
{
    for (const auto &e : kTable)
    {
        const auto &m = gg::core::paramMeta(e.slot, Mode::Fx);
        for (float v : {e.minVal, e.defaultVal, e.maxVal})
        {
            INFO("param " << e.name << " at " << v);
            const auto shown = m.valueToString(v);
            REQUIRE(shown.has_value());
            INFO("shown as '" << *shown << "'");
            std::string err;
            const auto back = m.valueFromString(*shown, err);
            INFO("error '" << err << "'");
            REQUIRE(back.has_value());
            // Within the display precision: the parsed value shows the same text...
            CHECK(m.valueToString(*back) == shown);
            // ...and, at these points, whose texts are exact, it's the value up to float
            // rounding through pow and log2.
            CHECK(std::fabs(*back - v) <= 1e-5f * (e.maxVal - e.minVal));
            CHECK(*back >= e.minVal);
            CHECK(*back <= e.maxVal);
        }
    }
}

TEST_CASE("state round trips exactly", "[core]")
{
    // 1,000 random in-range values per param, then each param's min, default and max. Each
    // round is one state: every param set, saved, loaded, compared bit for bit (which implies
    // ==).
    constexpr size_t kRandomRounds = 1000;
    std::vector<ParamValues> rounds;
    for (size_t r = 0; r < kRandomRounds; ++r)
    {
        ParamValues v{};
        for (size_t i = 0; i < kParamCount; ++i)
        {
            v[i] = randomIn(kTable[i].minVal, kTable[i].maxVal, r * kParamCount + i);
        }
        rounds.push_back(v);
    }
    for (int which = 0; which < 3; ++which)
    {
        ParamValues v{};
        for (size_t i = 0; i < kParamCount; ++i)
        {
            const auto &e = kTable[i];
            v[i] = which == 0 ? e.minVal : which == 1 ? e.defaultVal : e.maxVal;
        }
        rounds.push_back(v);
    }

    size_t valuesChecked = 0;
    for (size_t r = 0; r < rounds.size(); ++r)
    {
        const auto state = toStateXml(rounds[r], 0x0123456789abcdefull);

        Loaded loaded = distinct();
        REQUIRE(loaded.load(state));
        for (size_t i = 0; i < kParamCount; ++i)
        {
            const float want = rounds[r][i];
            const float got = loaded.values[i];
            if (bitsOf(got) != bitsOf(want) || !(got == want))
            {
                INFO("round " << r << ", param " << kTable[i].name);
                FAIL_CHECK("value " << want << " came back as " << got);
            }
            ++valuesChecked;
        }
        CHECK(loaded.seed == 0x0123456789abcdefull);
        CHECK(loaded.migratedFromVersion == 0u);
        // Saving what was loaded writes the same state again.
        CHECK(toStateXml(loaded.values, loaded.seed) == state);
    }
    CHECK(valuesChecked == (kRandomRounds + 3) * kParamCount);
}

TEST_CASE("state keeps values its 12-decimal text can't", "[core]")
{
    // PatchBase writes v with 12 fixed decimals, which can't hold a float below about 1.5e-5
    // exactly. The state also carries each value's float bits, so these still come back
    // exactly. The first four can't survive the text; the last two are a float either side of
    // 2^-16, where the text's resolution meets the float spacing.
    const std::array<float, 6> tiny{1.2345678e-7f,
                                    -1.2345678e-7f,
                                    std::numeric_limits<float>::denorm_min(),
                                    std::numeric_limits<float>::min(),
                                    std::nextafter(0x1.0p-16f, 0.f),
                                    std::nextafter(0x1.0p-16f, 1.f)};

    size_t lostByText = 0;
    for (float t : tiny)
    {
        ParamValues saved{};
        for (size_t i = 0; i < kParamCount; ++i)
        {
            saved[i] = std::clamp(t, kTable[i].minVal, kTable[i].maxVal);
        }
        const auto state = toStateXml(saved, 0);
        Loaded loaded = distinct();
        REQUIRE(loaded.load(state));
        for (size_t i = 0; i < kParamCount; ++i)
        {
            INFO("value " << t << ", param " << kTable[i].name);
            CHECK(bitsOf(loaded.values[i]) == bitsOf(saved[i]));
        }

        // The text alone would have lost it: Pitch's range takes either sign.
        const auto at = state.find(R"(<p id="65541" v=")");
        REQUIRE(at != std::string::npos);
        const auto text = state.substr(at + 17, state.find('"', at + 17) - (at + 17));
        if (static_cast<float>(std::stod(text)) != t)
        {
            ++lostByText;
        }
    }
    CHECK(lostByText >= 4);
}

TEST_CASE("state seed round trips at its extremes", "[core]")
{
    for (uint64_t seed : {uint64_t{0}, uint64_t{1}, uint64_t{0x0123456789abcdefull},
                          std::numeric_limits<uint64_t>::max()})
    {
        INFO("seed " << seed);
        Loaded loaded = distinct();
        loaded.seed = 7;
        REQUIRE(loaded.load(toStateXml(defaults(), seed)));
        CHECK(loaded.seed == seed);
    }
}

TEST_CASE("patch rejects or sanitizes bad state", "[core]")
{
    const Loaded source = distinct();
    const uint64_t sourceSeed = 0x0123456789abcdefull;
    const auto good = toStateXml(source.values, sourceSeed);

    Loaded p = distinct();
    const Loaded before = distinct();

    SECTION("truncated XML fails and writes nothing")
    {
        // Every cut short of the root element's end tag.
        const auto end = good.rfind("</patch>");
        REQUIRE(end != std::string::npos);
        for (size_t n = 0; n < end + std::strlen("</patch>"); ++n)
        {
            INFO("cut at " << n << " of " << good.size());
            REQUIRE_FALSE(p.load(std::string_view(good).substr(0, n)));
            REQUIRE(p.sameAs(before));
        }
    }

    SECTION("another patch's id fails")
    {
        CHECK_FALSE(
            p.load(replaceOnce(good, R"(id="org.sethbc.giga-grains.patch")", R"(id="other")")));
        CHECK(p.sameAs(before));
        CHECK_FALSE(p.load(replaceOnce(good, R"(id="org.sethbc.giga-grains.patch")", "")));
        CHECK(p.sameAs(before));
    }

    SECTION("not a patch fails")
    {
        const std::string noParams =
            R"(<patch id="org.sethbc.giga-grains.patch" version="1" name="Hand"></patch>)";
        for (const std::string &bad :
             {std::string{}, std::string{"garbage"}, std::string{"<preset/>"},
              replaceOnce(good, R"(version="1")", ""),
              replaceOnce(good, R"(version="1")", R"(version="one")"), noParams})
        {
            INFO("state '" << bad << "'");
            CHECK_FALSE(p.load(bad));
            CHECK(p.sameAs(before));
        }
    }

    SECTION("an unknown param id is ignored")
    {
        // 0x777777, in decimal as PatchBase writes ids; and the hex spelling, which PatchBase's
        // %d reads as id 0, also unknown.
        auto withUnknown = replaceOnce(
            good, "<params>", R"(<params><p id="7829367" v="0.5" /><p id="0x777777" v="0.5" />)");
        REQUIRE(p.load(withUnknown));
        CHECK(p.valueBits() == source.valueBits());
        CHECK(p.seed == sourceSeed);
    }

    SECTION("out-of-range and non-numeric values are clamped or ignored")
    {
        REQUIRE(p.load(handState(R"(<p id="2" v="1e9" />)"
                                 R"(<p id="65541" v="-1e9" />)"
                                 R"(<p id="1" v="nan" />)"
                                 R"(<p id="65540" v="inf" />)"
                                 R"(<p id="65544" v="7" />)")));
        CHECK(p[ParamSlot::Level] == 12.f);
        CHECK(p[ParamSlot::Pitch] == -24.f);
        CHECK(p[ParamSlot::Freeze] == 1.f);
        // NaN and infinity: either unreadable (the param keeps its default) or clamped.
        const float mix = p[ParamSlot::Mix];
        CHECK(std::isfinite(mix));
        CHECK((mix == 0.5f || mix == 0.f || mix == 1.f));
        const float distribution = p[ParamSlot::Distribution];
        CHECK((distribution == 0.5f || distribution == 1.f));
        // Params the state doesn't mention take their defaults.
        CHECK(p[ParamSlot::Density] == 4.0f);
        CHECK(p.seed == 0u);
        CHECK(p.migratedFromVersion == 0u);
    }

    SECTION("float bits that disagree with v are ignored")
    {
        // A hand-edited v wins over stale bits; bits that can't be parsed are ignored too.
        REQUIRE(p.load(handState(R"(<p id="65541" v="3.0" bits="3f800000" />)"
                                 R"(<p id="2" v="-6.0" bits="7fc00000" />)"
                                 R"(<p id="1" v="0.25" bits="3e8" />)"
                                 R"(<p id="65542" v="1e9" bits="7f800000" />)")));
        CHECK(p[ParamSlot::Pitch] == 3.f);
        CHECK(p[ParamSlot::Level] == -6.f);
        CHECK(p[ParamSlot::Mix] == 0.25f);
        CHECK(p[ParamSlot::Spray] == 1.f);
    }

    SECTION("a newer version loads through the migration hooks")
    {
        auto newer = replaceOnce(good, R"(version="1")", R"(version="99")");
        REQUIRE(p.load(newer));
        CHECK(p.migratedFromVersion == 99u);
        CHECK(p.valueBits() == source.valueBits());
        CHECK(p.seed == sourceSeed);

        // The next load at the current version needs no migration.
        REQUIRE(p.load(good));
        CHECK(p.migratedFromVersion == 0u);
    }

    SECTION("a missing or malformed seed loads as seed 0")
    {
        REQUIRE(p.load(handState(R"(<p id="1" v="0.25" />)")));
        CHECK(p.seed == 0u);
        CHECK(p[ParamSlot::Mix] == 0.25f);

        for (std::string_view bad :
             {R"(<engine seed="-1" />)", R"(<engine seed="0x10" />)",
              R"(<engine seed="18446744073709551616" />)", R"(<engine seed="12abc" />)",
              R"(<engine seed="" />)", R"(<engine />)"})
        {
            INFO("engine element " << bad);
            p.seed = 9;
            REQUIRE(p.load(handState(R"(<p id="1" v="0.25" />)", "1", bad)));
            CHECK(p.seed == 0u);
        }
    }

    SECTION("the migration report is optional")
    {
        ParamValues values{};
        uint64_t seed = 5;
        REQUIRE(fromStateXml(good, values, seed, Mode::Fx));
        CHECK(seed == sourceSeed);
        CHECK(values[0] == source.values[0]);
    }
}
