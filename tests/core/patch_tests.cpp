// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The patch: the ten slice parameters (spec §4) and their versioned XML state. The IDs, names,
// ranges and defaults are spelled out literally, from Global Constraints' parameter table, so a
// change to gg/core/ParamIds.h or ParamIds.cpp fails these tests. CLAP parameter IDs are frozen
// at the first public release and append-only (INTENT §18).

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <clap/clap.h>

#include "gg/core/ParamIds.h"
#include "gg/core/Patch.h"
#include "gg/core/Rng.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

using gg::core::kParamCount;
using gg::core::ParamSlot;
using gg::core::Patch;

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

// Global Constraints' parameter table, copied literally.
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

uint32_t bitsOf(float f) { return std::bit_cast<uint32_t>(f); }

ParamSlot slotAt(size_t i) { return static_cast<ParamSlot>(i); }

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

// Everything a load must leave untouched when it fails.
struct Snapshot
{
    std::array<uint32_t, kParamCount> valueBits{};
    uint64_t seed{0};
    std::string name;

    explicit Snapshot(const Patch &p) : seed(p.seed), name(p.name)
    {
        for (size_t i = 0; i < kParamCount; ++i)
        {
            valueBits[i] = bitsOf(p.param(slotAt(i)).value);
        }
    }
    bool operator==(const Snapshot &) const = default;
};

// A patch whose every value, seed and name differ from a fresh one's.
void makeDistinct(Patch &p)
{
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto &e = kTable[i];
        p.param(e.slot).value = e.minVal + 0.25f * (e.maxVal - e.minVal);
        REQUIRE(p.param(e.slot).value != e.defaultVal);
    }
    p.seed = 42;
    std::strncpy(p.name, "Before", sizeof(p.name) - 1);
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

        const auto &m = gg::core::paramMeta(e.slot);
        CHECK(m.id == e.id);
        CHECK(m.name == e.name);
        CHECK(m.minVal == e.minVal);
        CHECK(m.maxVal == e.maxVal);
        CHECK(m.defaultVal == e.defaultVal);
        CHECK(m.flags == e.flags);
        // Static storage, built once: the same object every call.
        CHECK(&gg::core::paramMeta(e.slot) == &m);
    }

    // Freeze is the only stepped parameter; it's an on/off switch.
    CHECK(gg::core::paramMeta(ParamSlot::Freeze).type ==
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

TEST_CASE("a new patch holds the ten params in slot order at their defaults", "[core]")
{
    Patch p;
    REQUIRE(p.params.size() == kParamCount);
    REQUIRE(p.paramMap.size() == kParamCount);
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto &e = kTable[i];
        INFO("param " << e.name);
        CHECK(p.params[i] == &p.param(e.slot));
        CHECK(p.params[i]->meta.id == e.id);
        CHECK(p.paramMap.at(e.id) == &p.param(e.slot));
        CHECK(p.param(e.slot).value == e.defaultVal);
    }
    CHECK(p.seed == 0u);
    CHECK(std::string_view(p.name) == "Init");
    CHECK(Patch::patchVersion == 1u);
    CHECK(std::string_view(Patch::id) == "org.sethbc.giga-grains.patch");
}

TEST_CASE("display text follows the engine mapping", "[core]")
{
    // Position and Spray display seconds = A (2^(8v) - 1); Size seconds = 2^v; Density
    // Hz = 2^v. The ends and the defaults have exact decimal texts.
    const auto text = [](ParamSlot s, float v) {
        auto t = gg::core::paramMeta(s).valueToString(v);
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
        const auto &m = gg::core::paramMeta(e.slot);
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
    // round is one state: every param set, saved, loaded into a new patch, compared bit for bit
    // (which implies ==).
    constexpr size_t kRandomRounds = 1000;
    std::vector<std::array<float, kParamCount>> rounds;
    for (size_t r = 0; r < kRandomRounds; ++r)
    {
        std::array<float, kParamCount> v{};
        for (size_t i = 0; i < kParamCount; ++i)
        {
            v[i] = randomIn(kTable[i].minVal, kTable[i].maxVal, r * kParamCount + i);
        }
        rounds.push_back(v);
    }
    for (int which = 0; which < 3; ++which)
    {
        std::array<float, kParamCount> v{};
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
        Patch saved;
        saved.seed = 0x0123456789abcdefull;
        for (size_t i = 0; i < kParamCount; ++i)
        {
            saved.param(slotAt(i)).value = rounds[r][i];
        }
        const auto state = saved.toState();

        Patch loaded;
        REQUIRE(loaded.fromState(state));
        for (size_t i = 0; i < kParamCount; ++i)
        {
            const float want = rounds[r][i];
            const float got = loaded.param(slotAt(i)).value;
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
        CHECK(loaded.toState() == state);
    }
    CHECK(valuesChecked == (kRandomRounds + 3) * kParamCount);
}

TEST_CASE("state keeps values its 12-decimal text can't", "[core]")
{
    // PatchBase writes v with 12 fixed decimals, which can't hold a float below about 1.5e-5
    // exactly. The patch also writes each value's float bits, so these still come back exactly.
    // The first four can't survive the text; the last two are a float either side of 2^-16,
    // where the text's resolution meets the float spacing.
    const std::array<float, 6> tiny{1.2345678e-7f,
                                    -1.2345678e-7f,
                                    std::numeric_limits<float>::denorm_min(),
                                    std::numeric_limits<float>::min(),
                                    std::nextafter(0x1.0p-16f, 0.f),
                                    std::nextafter(0x1.0p-16f, 1.f)};

    size_t lostByText = 0;
    for (float t : tiny)
    {
        Patch saved;
        for (size_t i = 0; i < kParamCount; ++i)
        {
            const auto &e = kTable[i];
            saved.param(e.slot).value = std::clamp(t, e.minVal, e.maxVal);
        }
        const auto state = saved.toState();
        Patch loaded;
        REQUIRE(loaded.fromState(state));
        for (size_t i = 0; i < kParamCount; ++i)
        {
            INFO("value " << t << ", param " << kTable[i].name);
            CHECK(bitsOf(loaded.param(slotAt(i)).value) == bitsOf(saved.param(slotAt(i)).value));
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
        Patch saved;
        saved.seed = seed;
        Patch loaded;
        loaded.seed = 7;
        REQUIRE(loaded.fromState(saved.toState()));
        CHECK(loaded.seed == seed);
    }
}

TEST_CASE("patch rejects or sanitizes bad state", "[core]")
{
    Patch source;
    makeDistinct(source);
    source.seed = 0x0123456789abcdefull;
    const auto good = source.toState();

    Patch p;
    makeDistinct(p);
    const Snapshot before(p);

    SECTION("truncated XML fails and leaves the patch untouched")
    {
        // Every cut short of the root element's end tag.
        const auto end = good.rfind("</patch>");
        REQUIRE(end != std::string::npos);
        for (size_t n = 0; n < end + std::strlen("</patch>"); ++n)
        {
            INFO("cut at " << n << " of " << good.size());
            REQUIRE_FALSE(p.fromState(good.substr(0, n)));
            REQUIRE(Snapshot(p) == before);
        }
    }

    SECTION("another patch's id fails")
    {
        CHECK_FALSE(p.fromState(
            replaceOnce(good, R"(id="org.sethbc.giga-grains.patch")", R"(id="other")")));
        CHECK(Snapshot(p) == before);
        CHECK_FALSE(p.fromState(replaceOnce(good, R"(id="org.sethbc.giga-grains.patch")", "")));
        CHECK(Snapshot(p) == before);
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
            CHECK_FALSE(p.fromState(bad));
            CHECK(Snapshot(p) == before);
        }
    }

    SECTION("an unknown param id is ignored")
    {
        // 0x777777, in decimal as PatchBase writes ids; and the hex spelling, which PatchBase's
        // %d reads as id 0, also unknown.
        auto withUnknown = replaceOnce(
            good, "<params>", R"(<params><p id="7829367" v="0.5" /><p id="0x777777" v="0.5" />)");
        REQUIRE(p.fromState(withUnknown));
        for (size_t i = 0; i < kParamCount; ++i)
        {
            INFO("param " << kTable[i].name);
            CHECK(bitsOf(p.param(slotAt(i)).value) == bitsOf(source.param(slotAt(i)).value));
        }
        CHECK(p.seed == source.seed);
    }

    SECTION("out-of-range and non-numeric values are clamped or ignored")
    {
        REQUIRE(p.fromState(handState(R"(<p id="2" v="1e9" />)"
                                      R"(<p id="65541" v="-1e9" />)"
                                      R"(<p id="1" v="nan" />)"
                                      R"(<p id="65540" v="inf" />)"
                                      R"(<p id="65544" v="7" />)")));
        CHECK(p.param(ParamSlot::Level).value == 12.f);
        CHECK(p.param(ParamSlot::Pitch).value == -24.f);
        CHECK(p.param(ParamSlot::Freeze).value == 1.f);
        // NaN and infinity: either unreadable (the param keeps its default) or clamped.
        const float mix = p.param(ParamSlot::Mix).value;
        CHECK(std::isfinite(mix));
        CHECK((mix == 0.5f || mix == 0.f || mix == 1.f));
        const float distribution = p.param(ParamSlot::Distribution).value;
        CHECK((distribution == 0.5f || distribution == 1.f));
        // Params the state doesn't mention take their defaults.
        CHECK(p.param(ParamSlot::Density).value == 4.0f);
        CHECK(p.seed == 0u);
    }

    SECTION("float bits that disagree with v are ignored")
    {
        // A hand-edited v wins over stale bits; bits that can't be parsed are ignored too.
        REQUIRE(p.fromState(handState(R"(<p id="65541" v="3.0" bits="3f800000" />)"
                                      R"(<p id="2" v="-6.0" bits="7fc00000" />)"
                                      R"(<p id="1" v="0.25" bits="3e8" />)"
                                      R"(<p id="65542" v="1e9" bits="7f800000" />)")));
        CHECK(p.param(ParamSlot::Pitch).value == 3.f);
        CHECK(p.param(ParamSlot::Level).value == -6.f);
        CHECK(p.param(ParamSlot::Mix).value == 0.25f);
        CHECK(p.param(ParamSlot::Spray).value == 1.f);
    }

    SECTION("a newer version loads through the migration hooks")
    {
        auto newer = replaceOnce(good, R"(version="1")", R"(version="99")");
        REQUIRE(p.fromState(newer));
        CHECK(p.migratedFromVersion == 99u);
        for (size_t i = 0; i < kParamCount; ++i)
        {
            INFO("param " << kTable[i].name);
            CHECK(bitsOf(p.param(slotAt(i)).value) == bitsOf(source.param(slotAt(i)).value));
        }
        CHECK(p.seed == source.seed);

        // The next load at the current version needs no migration.
        REQUIRE(p.fromState(good));
        CHECK(p.migratedFromVersion == 0u);
    }

    SECTION("a missing or malformed seed loads as seed 0")
    {
        REQUIRE(p.fromState(handState(R"(<p id="1" v="0.25" />)")));
        CHECK(p.seed == 0u);
        CHECK(p.param(ParamSlot::Mix).value == 0.25f);
        CHECK(std::string_view(p.name) == "Hand");

        for (std::string_view bad :
             {R"(<engine seed="-1" />)", R"(<engine seed="0x10" />)",
              R"(<engine seed="18446744073709551616" />)", R"(<engine seed="12abc" />)",
              R"(<engine seed="" />)", R"(<engine />)"})
        {
            INFO("engine element " << bad);
            p.seed = 9;
            REQUIRE(p.fromState(handState(R"(<p id="1" v="0.25" />)", "1", bad)));
            CHECK(p.seed == 0u);
        }
    }
}
