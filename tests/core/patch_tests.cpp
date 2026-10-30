// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The patch: the slice's ten parameters (spec §4), Space's eight (sub-project 4's spec §6.5), the
// Perform UI's ten (sub-project 5's spec §3.3, §4, §5) and M2's layers (M2 sub-project 1's spec
// §6.9: 48 per layer, A to D, and nine globals; 218 in all), and their versioned XML state, through
// gg-core's public API (gg/core/ParamIds.h, gg/core/PatchState.h) and its tinyxml-free internal
// core/src/PatchXml.h, for each product's patch XML. This file never includes
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <clap/clap.h>

#include "PatchXml.h"
#include "gg/core/ParamIds.h"
#include "gg/core/PatchState.h"
#include "gg/core/SessionState.h"
#include "gg/core/Rng.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "support/DeepXml.h"
#include "support/StateContainer.h"

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
    std::string name;
    float minVal, maxVal, defaultVal;
    uint32_t flags;
};

/// One row of Global Constraints' parameter table, as the plan spells it.
struct Row
{
    clap_id id;
    std::string_view name;
    float minVal, maxVal, defaultVal;
    uint32_t flags;
};
constexpr uint32_t kStepped = kAutoMod | CLAP_PARAM_IS_STEPPED;

// Global Constraints' parameter table, copied literally: the FX's. The instrument's differs only
// in every layer's Position default (kInstrumentPositionDefault) and display and Source default
// (kInstrumentSourceDefault), and in Macro 8's name (kInstrumentMacros).
//
// The 28 M1 slots, in slot order: layer A's eleven M1 parameters among them, named with their
// layer's letter since M2 sub-project 1 (spec §6.9).
constexpr std::array<Row, 28> kM1Rows{{
    {0x000001, "Mix", 0.f, 1.f, 0.5f, kAutoMod},
    {0x000002, "Level", -60.f, 12.f, 0.0f, kAutoMod},
    {0x010001, "A Position", 0.f, 1.f, 0.28967658f, kAutoMod},
    {0x010002, "A Size", -7.643856189774724f, 1.f, -2.736965594166206f, kAutoMod},
    {0x010003, "A Density", -2.f, 8.643856189774725f, 4.0f, kAutoMod},
    {0x010004, "A Distribution", 0.f, 1.f, 0.5f, kAutoMod},
    {0x010005, "A Pitch", -24.f, 24.f, 0.0f, kAutoMod},
    {0x010006, "A Spray", 0.f, 1.f, 0.36033038f, kAutoMod},
    {0x010007, "A Stereo", 0.f, 1.f, 0.5f, kAutoMod},
    {0x010008, "A Freeze", 0.f, 1.f, 0.0f, kStepped},
    {0x01000B, "A Diffusion", 0.f, 1.f, 0.0f, kAutoMod},
    {0x000003, "Reverb Amount", 0.f, 1.f, 0.0f, kAutoMod},
    {0x000004, "Reverb Decay", -2.f, 6.068255709804611f, 1.0f, kAutoMod},
    {0x000005, "Reverb Size", 0.f, 1.f, 0.5f, kAutoMod},
    {0x000006, "Reverb Damping", 0.f, 1.f, 0.3f, kAutoMod},
    {0x000007, "Reverb Pre-delay", 0.f, 0.5f, 0.0f, kAutoMod},
    {0x000008, "Reverb Modulation", 0.f, 1.f, 0.25f, kAutoMod},
    {0x000009, "Dry to Reverb", 0.f, 1.f, 0.0f, kAutoMod},
    // The Perform UI (sub-project 5's spec §3.3, §4, §5).
    {0x010009, "A Texture", 0.f, 1.f, 0.0f, kAutoMod},
    {0x01000A, "A Feedback", 0.f, 0.95f, 0.0f, kAutoMod},
    {0x000011, "Macro Size", 0.f, 1.f, 0.5f, kAutoMod},
    {0x000012, "Macro Density", 0.f, 1.f, 0.5f, kAutoMod},
    {0x000013, "Macro Pitch", 0.f, 1.f, 0.5f, kAutoMod},
    {0x000014, "Macro Spray", 0.f, 1.f, 0.5f, kAutoMod},
    {0x000015, "Macro Texture", 0.f, 1.f, 0.0f, kAutoMod},
    {0x000016, "Macro Freeze", 0.f, 1.f, 0.0f, kStepped},
    {0x000017, "Macro Feedback", 0.f, 1.f, 0.0f, kAutoMod},
    {0x000018, "Macro Mix", 0.f, 1.f, 0.5f, kAutoMod},
}};

// Layer A's 37 new parameters (M2 sub-project 1's spec §6.9), slots 28 to 64, in index order.
// Rates are log2 Hz (0.01 Hz to 50 Hz), envelope times log2 seconds (1 ms to 10 s).
constexpr float kRateMin = -6.643856189774724f, kRateMax = 5.643856189774724f;
constexpr float kTimeMin = -9.965784284662087f, kTimeMax = 3.321928094887362f;
constexpr std::array<Row, 37> kLayerANewRows{{
    {0x01000C, "A Source", 0.f, 4.f, 1.0f, kStepped},
    {0x01000D, "A Pitch Spread", 0.f, 24.f, 0.0f, kAutoMod},
    {0x01000E, "A Size Spread", 0.f, 2.f, 0.0f, kAutoMod},
    {0x01000F, "A LFO 1 Rate", kRateMin, kRateMax, 0.0f, kAutoMod},
    {0x010010, "A LFO 1 Shape", 0.f, 4.f, 0.0f, kStepped},
    {0x010011, "A LFO 2 Rate", kRateMin, kRateMax, -2.0f, kAutoMod},
    {0x010012, "A LFO 2 Shape", 0.f, 4.f, 1.0f, kStepped},
    {0x010013, "A Env 1 Attack", kTimeMin, kTimeMax, -6.643856189774724f, kAutoMod},
    {0x010014, "A Env 1 Decay", kTimeMin, kTimeMax, -2.321928094887362f, kAutoMod},
    {0x010015, "A Env 1 Sustain", 0.f, 1.f, 1.0f, kAutoMod},
    {0x010016, "A Env 1 Release", kTimeMin, kTimeMax, -2.321928094887362f, kAutoMod},
    {0x010017, "A Env 2 Attack", kTimeMin, kTimeMax, -1.0f, kAutoMod},
    {0x010018, "A Env 2 Decay", kTimeMin, kTimeMax, 0.0f, kAutoMod},
    {0x010019, "A Env 2 Sustain", 0.f, 1.f, 0.5f, kAutoMod},
    {0x01001A, "A Env 2 Release", kTimeMin, kTimeMax, 0.0f, kAutoMod},
    {0x01001B, "A Random 1 Rate", kRateMin, kRateMax, 1.0f, kAutoMod},
    {0x01001C, "A Random 1 Smooth", 0.f, 1.f, 0.0f, kAutoMod},
    {0x01001D, "A Random 2 Rate", kRateMin, kRateMax, -1.0f, kAutoMod},
    {0x01001E, "A Random 2 Smooth", 0.f, 1.f, 0.5f, kAutoMod},
    {0x01001F, "A Step Rate", kRateMin, kRateMax, 2.0f, kAutoMod},
    {0x010020, "A Step Length", 1.f, 16.f, 16.0f, kStepped},
    {0x010021, "A Step 1", 0.f, 1.f, 0.0f, kAutoMod},
    {0x010022, "A Step 2", 0.f, 1.f, 0.0f, kAutoMod},
    {0x010023, "A Step 3", 0.f, 1.f, 0.0f, kAutoMod},
    {0x010024, "A Step 4", 0.f, 1.f, 0.0f, kAutoMod},
    {0x010025, "A Step 5", 0.f, 1.f, 0.0f, kAutoMod},
    {0x010026, "A Step 6", 0.f, 1.f, 0.0f, kAutoMod},
    {0x010027, "A Step 7", 0.f, 1.f, 0.0f, kAutoMod},
    {0x010028, "A Step 8", 0.f, 1.f, 0.0f, kAutoMod},
    {0x010029, "A Step 9", 0.f, 1.f, 0.0f, kAutoMod},
    {0x01002A, "A Step 10", 0.f, 1.f, 0.0f, kAutoMod},
    {0x01002B, "A Step 11", 0.f, 1.f, 0.0f, kAutoMod},
    {0x01002C, "A Step 12", 0.f, 1.f, 0.0f, kAutoMod},
    {0x01002D, "A Step 13", 0.f, 1.f, 0.0f, kAutoMod},
    {0x01002E, "A Step 14", 0.f, 1.f, 0.0f, kAutoMod},
    {0x01002F, "A Step 15", 0.f, 1.f, 0.0f, kAutoMod},
    {0x010030, "A Step 16", 0.f, 1.f, 0.0f, kAutoMod},
}};

// The nine new globals (spec §6.9), the last slots.
constexpr std::array<Row, 9> kNewGlobalRows{{
    {0x000019, "Morph X", 0.f, 1.f, 0.5f, kAutoMod},
    {0x00001A, "Morph Y", 0.f, 1.f, 0.5f, kAutoMod},
    {0x00001B, "Layer A On", 0.f, 1.f, 1.0f, kStepped},
    {0x00001C, "Layer B On", 0.f, 1.f, 0.0f, kStepped},
    {0x00001D, "Layer C On", 0.f, 1.f, 0.0f, kStepped},
    {0x00001E, "Layer D On", 0.f, 1.f, 0.0f, kStepped},
    {0x00001F, "Level B", -60.f, 12.f, 0.0f, kAutoMod},
    {0x000020, "Level C", -60.f, 12.f, 0.0f, kAutoMod},
    {0x000021, "Level D", -60.f, 12.f, 0.0f, kAutoMod},
}};

// All 218, in slot order: the rows above, with layers B to D generated from layer A's 48 rows in
// index order (the M1 ones found by ID), each with its letter for "A" and its scope for 1.
std::array<Expected, kParamCount> buildExpected()
{
    // Plain checks, not Catch2's: this runs before any test case, at static initialization.
    const auto expect = [](bool ok) {
        if (!ok)
        {
            throw std::logic_error("the expected parameter table is malformed");
        }
    };
    std::array<Expected, kParamCount> t{};
    size_t slot = 0;
    const auto push = [&t, &slot, &expect](clap_id id, std::string name, const Row &r) {
        expect(slot < kParamCount);
        t[slot] = {static_cast<ParamSlot>(slot),
                   id,
                   std::move(name),
                   r.minVal,
                   r.maxVal,
                   r.defaultVal,
                   r.flags};
        ++slot;
    };
    for (const Row &r : kM1Rows)
    {
        push(r.id, std::string(r.name), r);
    }
    for (const Row &r : kLayerANewRows)
    {
        push(r.id, std::string(r.name), r);
    }
    std::vector<Row> layerA;
    for (clap_id index = 0x01; index <= 0x0B; ++index)
    {
        const auto at = std::find_if(kM1Rows.begin(), kM1Rows.end(),
                                     [index](const Row &r) { return r.id == (0x010000u | index); });
        expect(at != kM1Rows.end());
        layerA.push_back(*at);
    }
    layerA.insert(layerA.end(), kLayerANewRows.begin(), kLayerANewRows.end());
    expect(layerA.size() == 48);
    for (const char letter : {'B', 'C', 'D'})
    {
        const auto scope = static_cast<clap_id>(letter - 'A' + 1);
        for (const Row &r : layerA)
        {
            expect(r.name.starts_with("A "));
            push((scope << 16) | (r.id & 0xFFFFu),
                 std::string(1, letter) + std::string(r.name.substr(1)), r);
        }
    }
    for (const Row &r : kNewGlobalRows)
    {
        push(r.id, std::string(r.name), r);
    }
    expect(slot == kParamCount);
    return t;
}
const std::array<Expected, kParamCount> kTable = buildExpected();

/// Where the instrument's table differs from kTable but for Position and Source: Macro 8's name
/// (Global Constraints). Macro 7 is "Macro Feedback", 0, in both products.
struct InstrumentDifference
{
    ParamSlot slot;
    std::string_view name;
    float defaultVal;
};
constexpr std::array<InstrumentDifference, 1> kInstrumentMacros{{
    {ParamSlot::Macro8, "Macro Level", 0.5f},
}};

/// A layer parameter's index in its scope, or 0 for a global.
clap_id layerIndexOf(const Expected &e)
{
    const clap_id scope = e.id >> 16;
    return scope >= 1 && scope <= 4 ? (e.id & 0xFFFFu) : 0u;
}

/// Decay's ∞ region starts at log2 60 and its top is 2% of its travel above that (Global
/// Constraints).
constexpr float kDecayInfinite = 5.906890595608519f;
constexpr float kDecayMax = 6.068255709804611f;

/// The instrument's Position: 0–100% of the loaded file, 0% by default (spec §6.8), in every layer.
constexpr float kInstrumentPositionDefault = 0.0f;

/// The instrument's Source: Sample, in every layer (M2 sub-project 1's spec §6.9).
constexpr float kInstrumentSourceDefault = 0.0f;

uint32_t bitsOf(float f) { return std::bit_cast<uint32_t>(f); }

/// A param's name and default in `mode`'s product.
std::string_view nameIn(const Expected &e, Mode mode)
{
    if (mode == Mode::Instrument)
    {
        for (const auto &d : kInstrumentMacros)
        {
            if (d.slot == e.slot)
            {
                return d.name;
            }
        }
    }
    return e.name;
}
float defaultIn(const Expected &e, Mode mode)
{
    if (mode == Mode::Instrument)
    {
        if (layerIndexOf(e) == 0x01)
        {
            return kInstrumentPositionDefault;
        }
        if (layerIndexOf(e) == 0x0C)
        {
            return kInstrumentSourceDefault;
        }
        for (const auto &d : kInstrumentMacros)
        {
            if (d.slot == e.slot)
            {
                return d.defaultVal;
            }
        }
    }
    return e.defaultVal;
}

/// The FX's defaults, or `mode`'s.
ParamValues defaults(Mode mode = Mode::Fx)
{
    ParamValues v{};
    for (size_t i = 0; i < kParamCount; ++i)
    {
        v[i] = defaultIn(kTable[i], mode);
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
        // A quarter of the way up, or three quarters where the default sits a quarter up
        // (Reverb Modulation's).
        l.values[i] = e.minVal + 0.25f * (e.maxVal - e.minVal);
        if (l.values[i] == e.defaultVal)
        {
            l.values[i] = e.minVal + 0.75f * (e.maxVal - e.minVal);
        }
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
    STATIC_REQUIRE(kParamCount == 218);
    STATIC_REQUIRE(std::is_same_v<ParamValues, std::array<float, 218>>);
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<ParamSlot>, uint16_t>);
    STATIC_REQUIRE(std::is_same_v<decltype(gg::core::kFeedbackMax), const float>);
    STATIC_REQUIRE(gg::core::kFeedbackMax == 0.95f);
    STATIC_REQUIRE(std::is_same_v<decltype(gg::core::kDecayInfinite), const float>);
    STATIC_REQUIRE(std::is_same_v<decltype(gg::core::kDecayMax), const float>);
    STATIC_REQUIRE(gg::core::kDecayInfinite == kDecayInfinite);
    STATIC_REQUIRE(gg::core::kDecayMax == kDecayMax);

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

    // The instrument's table is the FX's but for every layer's Position default (spec §6.8) and
    // Source default, and Macro 8's name (M2 sub-project 1's spec §6.9): the same IDs, ranges and
    // flags, so both products share one state format.
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto &e = kTable[i];
        INFO("instrument param " << e.name);
        const auto &m = gg::core::paramMeta(e.slot, Mode::Instrument);
        CHECK(m.id == e.id);
        CHECK(m.name == nameIn(e, Mode::Instrument));
        CHECK(m.minVal == e.minVal);
        CHECK(m.maxVal == e.maxVal);
        CHECK(m.defaultVal == defaultIn(e, Mode::Instrument));
        CHECK(m.flags == e.flags);
        CHECK(m.type == gg::core::paramMeta(e.slot, Mode::Fx).type);
        CHECK(&gg::core::paramMeta(e.slot, Mode::Instrument) == &m);
    }

    // The stepped parameters, and only they, are integers: the switches (each layer's Freeze,
    // Macro Freeze, the Layer On switches) and the choices (each layer's Source, LFO shapes and
    // Step Length).
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        size_t steppedCount = 0;
        for (size_t i = 0; i < kParamCount; ++i)
        {
            const auto slot = static_cast<ParamSlot>(i);
            const bool stepped = (kTable[i].flags & CLAP_PARAM_IS_STEPPED) != 0;
            steppedCount += stepped ? 1 : 0;
            INFO("param " << kTable[i].name);
            CHECK((gg::core::paramMeta(slot, mode).type ==
                   sst::basic_blocks::params::ParamMetaData::INT) == stepped);
        }
        CHECK(steppedCount == 4 * 5 + 1 + 4);
    }

    // Each layer parameter's CLAP module names its layer; a global's is empty.
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto &e = kTable[i];
        INFO("param " << e.name);
        const auto scope = e.id >> 16;
        const std::string want = scope == 0 ? "" : std::string("Layer ") + e.name.substr(0, 1);
        CHECK(std::string_view(gg::core::paramModule(e.slot)) == want);
    }

    // The macros, in order.
    STATIC_REQUIRE(gg::core::kMacroSlots == std::array{ParamSlot::Macro1, ParamSlot::Macro2,
                                                       ParamSlot::Macro3, ParamSlot::Macro4,
                                                       ParamSlot::Macro5, ParamSlot::Macro6,
                                                       ParamSlot::Macro7, ParamSlot::Macro8});
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto slot = static_cast<ParamSlot>(i);
        INFO("param " << kTable[i].name);
        CHECK(gg::core::isMacro(slot) ==
              (std::find(gg::core::kMacroSlots.begin(), gg::core::kMacroSlots.end(), slot) !=
               gg::core::kMacroSlots.end()));
    }
    STATIC_REQUIRE_FALSE(gg::core::isMacro(ParamSlot::Count));

    // Sub-project 5's reserved IDs are ours now: Texture, Feedback and the eight macros.
    for (clap_id id : {0x010009u, 0x01000Au, 0x000011u, 0x000012u, 0x000013u, 0x000014u, 0x000015u,
                       0x000016u, 0x000017u, 0x000018u})
    {
        INFO("id " << id);
        CHECK(gg::core::slotForId(id).has_value());
    }

    // IDs that aren't ours: the reserved index 0, the never-used 0x0A to 0x10 of scope 0, scopes
    // and indices one past the table, the reserved blocks, and the "unknown id" of the bad-state
    // test.
    for (clap_id unknown : {0x000000u, 0x00000Au, 0x000010u, 0x000022u, 0x010000u, 0x010031u,
                            0x020000u, 0x020031u, 0x040031u, 0x050001u, 0x000040u, 0x01004Fu,
                            0x030060u, 0x04007Fu, 0x777777u, CLAP_INVALID_ID})
    {
        INFO("id " << unknown);
        CHECK_FALSE(gg::core::slotForId(unknown).has_value());
    }
}

TEST_CASE("the id table has the next free indices and the reserved blocks", "[core]")
{
    // M2 sub-project 1's spec §6.9: no index of any scope falls in 0x40 to 0x7F (wave 2's), and
    // the recorded next-free indices are the table's largest plus one, in the layer scopes and
    // in scope 0.
    STATIC_REQUIRE(gg::core::kReservedIndexBegin == 0x40u);
    STATIC_REQUIRE(gg::core::kReservedIndexEnd == 0x7Fu);
    STATIC_REQUIRE(gg::core::kNextFreeLayerIndex == 0x31u);
    STATIC_REQUIRE(gg::core::kNextFreeGlobalIndex == 0x22u);
    uint32_t maxLayerIndex = 0;
    uint32_t maxGlobalIndex = 0;
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto slot = static_cast<ParamSlot>(i);
        const clap_id id = gg::core::paramId(slot);
        const uint32_t index = id & 0xFFFFu;
        INFO("param " << kTable[i].name << ", id " << id);
        CHECK((index < gg::core::kReservedIndexBegin || index > gg::core::kReservedIndexEnd));
        CHECK((id >> 16) <= 4u);
        uint32_t &maxIndex = (id >> 16) == 0 ? maxGlobalIndex : maxLayerIndex;
        maxIndex = std::max(maxIndex, index);
    }
    CHECK(maxLayerIndex + 1 == gg::core::kNextFreeLayerIndex);
    CHECK(maxGlobalIndex + 1 == gg::core::kNextFreeGlobalIndex);

    // The helpers between a flat slot and (layer parameter, layer).
    using gg::core::LayerParam;
    using gg::core::LayerRef;
    STATIC_REQUIRE(gg::core::kLayers == 4);
    STATIC_REQUIRE(gg::core::kLayerParamCount == 48);
    STATIC_REQUIRE(gg::core::layerParamIndex(LayerParam::Position) == 0x01u);
    STATIC_REQUIRE(gg::core::layerParamIndex(LayerParam::Diffusion) == 0x0Bu);
    STATIC_REQUIRE(gg::core::layerParamIndex(LayerParam::Source) == 0x0Cu);
    STATIC_REQUIRE(gg::core::layerParamIndex(LayerParam::Step16) == 0x30u);
    STATIC_REQUIRE(gg::core::slotForId(0x020001) == ParamSlot::B_Position);
    STATIC_REQUIRE(gg::core::layerOf(ParamSlot::D_Step16) == LayerRef{LayerParam::Step16, 3});
    STATIC_REQUIRE(gg::core::layerOf(ParamSlot::Feedback) == LayerRef{LayerParam::Feedback, 0});
    STATIC_REQUIRE(gg::core::layerOf(ParamSlot::Source) == LayerRef{LayerParam::Source, 0});
    STATIC_REQUIRE(gg::core::layerOf(ParamSlot::B_Position) == LayerRef{LayerParam::Position, 1});
    STATIC_REQUIRE(gg::core::layerOf(ParamSlot::Mix) == std::nullopt);
    STATIC_REQUIRE(gg::core::layerOf(ParamSlot::LevelB) == std::nullopt);
    STATIC_REQUIRE(gg::core::layerOf(ParamSlot::LayerAOn) == std::nullopt);
    STATIC_REQUIRE(gg::core::layerOf(ParamSlot::Count) == std::nullopt);
    STATIC_REQUIRE(gg::core::layerSlot(LayerParam::Diffusion, 0) == ParamSlot::Diffusion);
    STATIC_REQUIRE(gg::core::layerSlot(LayerParam::Texture, 0) == ParamSlot::Texture);
    STATIC_REQUIRE(gg::core::layerSlot(LayerParam::Step16, 0) == ParamSlot::Step16);
    STATIC_REQUIRE(gg::core::layerSlot(LayerParam::Size, 2) == ParamSlot::C_Size);
    STATIC_REQUIRE(gg::core::layerSlot(LayerParam::Size, 4) == ParamSlot::Count);
    STATIC_REQUIRE(gg::core::layerSlot(LayerParam::Count, 0) == ParamSlot::Count);
    STATIC_REQUIRE(gg::core::levelSlot(0) == ParamSlot::Level);
    STATIC_REQUIRE(gg::core::levelSlot(2) == ParamSlot::LevelC);
    STATIC_REQUIRE(gg::core::levelSlot(4) == ParamSlot::Count);
    STATIC_REQUIRE(gg::core::layerOnSlot(0) == ParamSlot::LayerAOn);
    STATIC_REQUIRE(gg::core::layerOnSlot(3) == ParamSlot::LayerDOn);
    STATIC_REQUIRE(gg::core::layerOnSlot(4) == ParamSlot::Count);
    STATIC_REQUIRE(gg::core::perLayerGlobalLayer(ParamSlot::LayerDOn) == 3);
    STATIC_REQUIRE(gg::core::perLayerGlobalLayer(ParamSlot::Level) == 0);
    STATIC_REQUIRE(gg::core::perLayerGlobalLayer(ParamSlot::LevelB) == 1);
    STATIC_REQUIRE(gg::core::perLayerGlobalLayer(ParamSlot::Mix) == std::nullopt);
    STATIC_REQUIRE(gg::core::perLayerGlobalLayer(ParamSlot::Size) == std::nullopt);

    // Every layer slot is (parameter, layer) with ID (layer + 1) << 16 | index, both ways; each
    // (parameter, layer) is one slot.
    std::vector<bool> seen(kParamCount, false);
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        for (size_t p = 0; p < gg::core::kLayerParamCount; ++p)
        {
            const auto param = static_cast<LayerParam>(p);
            const ParamSlot slot = gg::core::layerSlot(param, layer);
            INFO("layer " << int{layer} << ", param " << p);
            REQUIRE(slot != ParamSlot::Count);
            const auto s = static_cast<size_t>(slot);
            CHECK_FALSE(seen[s]);
            seen[s] = true;
            CHECK(gg::core::paramId(slot) == ((layer + 1u) << 16 | (p + 1)));
            CHECK(gg::core::layerOf(slot) == LayerRef{param, layer});
            CHECK(kTable[s].name.starts_with(std::string(1, static_cast<char>('A' + layer)) + " "));
        }
    }
    for (size_t s = 0; s < kParamCount; ++s)
    {
        const auto slot = static_cast<ParamSlot>(s);
        INFO("param " << kTable[s].name);
        CHECK(gg::core::layerOf(slot).has_value() == seen[s]);
        CHECK(gg::core::perLayerGlobalLayer(slot).has_value() ==
              (slot == ParamSlot::Level || slot == ParamSlot::LevelB || slot == ParamSlot::LevelC ||
               slot == ParamSlot::LevelD || slot == ParamSlot::LayerAOn ||
               slot == ParamSlot::LayerBOn || slot == ParamSlot::LayerCOn ||
               slot == ParamSlot::LayerDOn));
    }
}

TEST_CASE("the state lists every param in slot order", "[core]")
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

TEST_CASE("a state saved before Space loads at the defaults", "[core][space]")
{
    // A default FX state as main wrote it before Space (5eefffc): toStateXml's output for the
    // FX's defaults and seed 0, copied literally. It names the ten params of sub-projects 1 to 3
    // only.
    constexpr std::string_view kPreSpaceState =
        R"(<patch id="org.sethbc.giga-grains.patch" version="1" name="Init"><params>)"
        R"(<p id="1" v="0.500000000000" bits="3f000000" />)"
        R"(<p id="2" v="0.000000000000" bits="00000000" />)"
        R"(<p id="65537" v="0.289676576853" bits="3e94507d" />)"
        R"(<p id="65538" v="-2.736965656281" bits="c02f2a72" />)"
        R"(<p id="65539" v="4.000000000000" bits="40800000" />)"
        R"(<p id="65540" v="0.500000000000" bits="3f000000" />)"
        R"(<p id="65541" v="0.000000000000" bits="00000000" />)"
        R"(<p id="65542" v="0.360330373049" bits="3eb87d39" />)"
        R"(<p id="65543" v="0.500000000000" bits="3f000000" />)"
        R"(<p id="65544" v="0.000000000000" bits="00000000" />)"
        R"(</params><engine seed="0" /></patch>)";

    // Into values that differ from every default, so each one is seen to be written. The ten it
    // names come back as written (the FX's defaults, Position included in either product); the
    // eighteen it doesn't take the product's defaults. No migration: the version is still 1.
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        INFO((mode == Mode::Fx ? "fx" : "instrument"));
        ParamValues want = defaults(mode);
        want[static_cast<size_t>(ParamSlot::Position)] =
            kTable[static_cast<size_t>(ParamSlot::Position)].defaultVal;
        Loaded l = distinct();
        REQUIRE(l.load(kPreSpaceState, mode));
        CHECK(l.valueBits() == Loaded{want, 0, 0}.valueBits());
        CHECK(l.seed == 0u);
        CHECK(l.migratedFromVersion == 0u);
        for (size_t i = static_cast<size_t>(ParamSlot::Diffusion); i < kParamCount; ++i)
        {
            INFO("param " << kTable[i].name);
            CHECK(bitsOf(l.values[i]) == bitsOf(defaultIn(kTable[i], mode)));
        }

        // Saving it writes today's state: the ten as they were, then the eighteen defaults.
        const auto resaved = toStateXml(l.values, l.seed);
        const auto oldParams = kPreSpaceState.substr(0, kPreSpaceState.find("</params>"));
        CHECK(resaved.starts_with(oldParams));
        CHECK(resaved == toStateXml(want, 0));
    }
}

TEST_CASE("a state saved before the Perform UI loads at the defaults", "[core]")
{
    // A default FX state as main wrote it before the Perform UI (c79e045): toStateXml's output
    // for the FX's defaults and seed 0, as encodeState writes it in PTCH, copied literally. It
    // names the eighteen params of sub-projects 1 to 4 only.
    constexpr std::string_view kPrePerformState =
        R"(<patch id="org.sethbc.giga-grains.patch" version="1" name="Init"><params>)"
        R"(<p id="1" v="0.500000000000" bits="3f000000" />)"
        R"(<p id="2" v="0.000000000000" bits="00000000" />)"
        R"(<p id="65537" v="0.289676576853" bits="3e94507d" />)"
        R"(<p id="65538" v="-2.736965656281" bits="c02f2a72" />)"
        R"(<p id="65539" v="4.000000000000" bits="40800000" />)"
        R"(<p id="65540" v="0.500000000000" bits="3f000000" />)"
        R"(<p id="65541" v="0.000000000000" bits="00000000" />)"
        R"(<p id="65542" v="0.360330373049" bits="3eb87d39" />)"
        R"(<p id="65543" v="0.500000000000" bits="3f000000" />)"
        R"(<p id="65544" v="0.000000000000" bits="00000000" />)"
        R"(<p id="65547" v="0.000000000000" bits="00000000" />)"
        R"(<p id="3" v="0.000000000000" bits="00000000" />)"
        R"(<p id="4" v="1.000000000000" bits="3f800000" />)"
        R"(<p id="5" v="0.500000000000" bits="3f000000" />)"
        R"(<p id="6" v="0.300000011921" bits="3e99999a" />)"
        R"(<p id="7" v="0.000000000000" bits="00000000" />)"
        R"(<p id="8" v="0.250000000000" bits="3e800000" />)"
        R"(<p id="9" v="0.000000000000" bits="00000000" />)"
        R"(</params><engine seed="0" /></patch>)";

    // The eighteen come back as written; Texture and Feedback load at 0 and the macros at rest,
    // {0.5, 0.5, 0.5, 0.5, 0, 0, 0, 0.5}, in either product, and the rest at the product's
    // defaults.
    constexpr std::array<float, 10> kFxNew{0.f, 0.f, 0.5f, 0.5f, 0.5f, 0.5f, 0.f, 0.f, 0.f, 0.5f};
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        INFO((mode == Mode::Fx ? "fx" : "instrument"));
        ParamValues want = defaults(mode);
        // The FX's eighteen, as the state names them.
        want[static_cast<size_t>(ParamSlot::Position)] =
            kTable[static_cast<size_t>(ParamSlot::Position)].defaultVal;
        for (size_t k = 0; k < kFxNew.size(); ++k)
        {
            want[static_cast<size_t>(ParamSlot::Texture) + k] = kFxNew[k];
        }

        Loaded l = distinct();
        REQUIRE(l.load(kPrePerformState, mode));
        CHECK(l.valueBits() == Loaded{want, 0, 0}.valueBits());
        CHECK(l.seed == 0u);
        CHECK(l.migratedFromVersion == 0u);

        // As the PTCH chunk of a session state, as the plugins saved it, with no sample.
        const auto withOldPatch =
            gg::test::stateContainer({gg::test::stateChunk("PTCH", kPrePerformState),
                                      gg::test::stateChunk("MEDI", R"(<media version="1" />)")});
        gg::core::SessionState decoded;
        REQUIRE(gg::core::decodeState(withOldPatch, mode, decoded));
        CHECK(Loaded{decoded.values, decoded.seed, 0}.valueBits() ==
              Loaded{want, 0, 0}.valueBits());
        CHECK(decoded.seed == 0u);

        // Saving it writes today's state: the eighteen as they were, then the ten new defaults.
        const auto resaved = toStateXml(l.values, l.seed);
        const auto oldParams = kPrePerformState.substr(0, kPrePerformState.find("</params>"));
        CHECK(resaved.starts_with(oldParams));
        CHECK(resaved == toStateXml(want, 0));
        if (mode == Mode::Fx)
        {
            CHECK(resaved == toStateXml(defaults(), 0));
        }
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
    // Space's (sub-project 4's spec §6.5). Decay is log2 seconds, in ms below 1 s and ∞ over the
    // top 2% of its travel; Pre-delay is seconds, shown in ms.
    CHECK(text(ParamSlot::Diffusion, 0.f) == "0.00 %");
    CHECK(text(ParamSlot::Diffusion, 1.f) == "100.00 %");
    CHECK(text(ParamSlot::ReverbAmount, 0.5f) == "50.00 %");
    CHECK(text(ParamSlot::ReverbDecay, -2.f) == "250.0 ms");
    CHECK(text(ParamSlot::ReverbDecay, 1.f) == "2.00 s");
    CHECK(text(ParamSlot::ReverbDecay, 5.9f) == "59.71 s");
    CHECK(text(ParamSlot::ReverbDecay, 5.95f) == "∞");
    CHECK(text(ParamSlot::ReverbDecay, kDecayMax) == "∞");
    CHECK(text(ParamSlot::ReverbSize, 0.5f) == "50.00 %");
    CHECK(text(ParamSlot::ReverbDamping, 0.3f) == "30.00 %");
    CHECK(text(ParamSlot::ReverbPredelay, 0.f) == "0.0 ms");
    CHECK(text(ParamSlot::ReverbPredelay, 0.025f) == "25.0 ms");
    CHECK(text(ParamSlot::ReverbPredelay, 0.5f) == "500.0 ms");
    CHECK(text(ParamSlot::ReverbModulation, 0.25f) == "25.00 %");
    CHECK(text(ParamSlot::DryToReverb, 0.f) == "0.00 %");
    // The Perform UI's (sub-project 5's spec §3.3, §4, §5): percentages, and Macro Freeze a
    // switch like Freeze.
    CHECK(text(ParamSlot::Texture, 0.f) == "0.00 %");
    CHECK(text(ParamSlot::Texture, 1.f) == "100.00 %");
    CHECK(text(ParamSlot::Feedback, 0.f) == "0.00 %");
    CHECK(text(ParamSlot::Feedback, 0.5f) == "50.00 %");
    CHECK(text(ParamSlot::Feedback, 0.95f) == "95.00 %");
    CHECK(text(ParamSlot::Macro1, 0.5f) == "50.00 %");
    CHECK(text(ParamSlot::Macro5, 0.f) == "0.00 %");
    CHECK(text(ParamSlot::Macro6, 0.f) == "Off");
    CHECK(text(ParamSlot::Macro6, 1.f) == "On");
    CHECK(text(ParamSlot::Macro7, 0.f) == "0.00 %");
    CHECK(text(ParamSlot::Macro8, 1.f) == "100.00 %");
    // The layers' (M2 sub-project 1's spec §6.9): stepped choices by name, rates in Hz, envelope
    // times in ms below 1 s, the spreads in semitones and octaves; every layer alike.
    CHECK(text(ParamSlot::Source, 0.f) == "Sample");
    CHECK(text(ParamSlot::Source, 1.f) == "Live");
    CHECK(text(ParamSlot::D_Source, 4.f) == "Oscillator");
    CHECK(text(ParamSlot::Lfo1Shape, 2.f) == "Saw Up");
    CHECK(text(ParamSlot::B_Lfo2Shape, 4.f) == "Square");
    CHECK(text(ParamSlot::Lfo1Rate, 0.f) == "1.00 Hz");
    CHECK(text(ParamSlot::Lfo2Rate, -2.f) == "0.25 Hz");
    CHECK(text(ParamSlot::StepRate, 5.643856189774724f) == "50.00 Hz");
    CHECK(text(ParamSlot::Random1Rate, -6.643856189774724f) == "0.01 Hz");
    CHECK(text(ParamSlot::Env1Attack, -6.643856189774724f) == "10.0 ms");
    CHECK(text(ParamSlot::Env1Attack, -9.965784284662087f) == "1.0 ms");
    CHECK(text(ParamSlot::Env2Decay, 0.f) == "1.00 s");
    CHECK(text(ParamSlot::C_Env2Release, 3.321928094887362f) == "10.00 s");
    CHECK(text(ParamSlot::Env1Sustain, 1.f) == "100.00 %");
    CHECK(text(ParamSlot::StepLength, 16.f) == "16");
    CHECK(text(ParamSlot::PitchSpread, 12.f) == "12.00 st");
    CHECK(text(ParamSlot::SizeSpread, 1.f) == "1.00 oct");
    CHECK(text(ParamSlot::B_Size, -2.736965594166206f) == "150.0 ms");
    CHECK(text(ParamSlot::B_Position, 0.28967658f) == "0.25 s");
    CHECK(text(ParamSlot::MorphX, 0.5f) == "50.00 %");
    CHECK(text(ParamSlot::LayerBOn, 0.f) == "Off");
    CHECK(text(ParamSlot::LayerAOn, 1.f) == "On");
    CHECK(text(ParamSlot::LevelD, 0.f) == "0.00 dB");
}

TEST_CASE("Decay shows infinity over exactly the infinite region", "[core][space]")
{
    // "∞" from kDecayInfinite (log2 60) up, to the float: the region toSpaceParams treats as
    // infinite. "∞" parses back to the top of the range. Both products alike.
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        const auto &m = gg::core::paramMeta(ParamSlot::ReverbDecay, mode);
        const auto text = [&m](float v) { return m.valueToString(v).value_or("<none>"); };
        CHECK(text(kDecayInfinite) == "∞");
        const float below = std::nextafter(kDecayInfinite, 0.f);
        CHECK(text(below) == "60.00 s");
        // Every float within 64 of the boundary on either side, then a sweep of the range.
        float v = below;
        for (int i = 0; i < 64; ++i)
        {
            v = std::nextafter(v, 0.f);
        }
        for (int i = 0; i < 128; ++i, v = std::nextafter(v, 10.f))
        {
            if ((text(v) == "∞") != (v >= kDecayInfinite))
            {
                INFO("Decay " << v << " shows '" << text(v) << "'");
                FAIL_CHECK("∞ shown off the infinite region");
            }
        }
        for (int i = 0; i <= 1000; ++i)
        {
            const float s = -2.f + (kDecayMax + 2.f) * static_cast<float>(i) / 1000.f;
            if ((text(s) == "∞") != (s >= kDecayInfinite))
            {
                INFO("Decay " << s << " shows '" << text(s) << "'");
                FAIL_CHECK("∞ shown off the infinite region");
            }
        }

        std::string err;
        const auto back = m.valueFromString("∞", err);
        REQUIRE(back.has_value());
        CHECK(bitsOf(*back) == bitsOf(kDecayMax));
        const auto two = m.valueFromString("2.00 s", err);
        REQUIRE(two.has_value());
        CHECK(std::fabs(*two - 1.f) < 1e-6f);
    }
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

TEST_CASE("both products' patches write the same state", "[core]")
{
    // toStateXml writes the FX's Patch whatever the product (core/src/PatchXml.h): right only
    // while the instrument's Patch writes the same bytes. 1,000 rounds of random values and
    // seeds: in range, then any bits at all (NaNs and infinities included).
    for (uint64_t r = 0; r < 1000; ++r)
    {
        ParamValues v{};
        for (size_t i = 0; i < kParamCount; ++i)
        {
            const uint64_t index = 0x205'0000ull + r * kParamCount + i;
            v[i] =
                r < 500
                    ? randomIn(kTable[i].minVal, kTable[i].maxVal, index)
                    : std::bit_cast<float>(static_cast<uint32_t>(gg::rng::hash(
                          {0x5eed'7e57'0000'0205ull, 0, 0, gg::rng::Field::SignalNoise, index})));
        }
        const uint64_t seed =
            gg::rng::hash({0x5eed'7e57'0000'0205ull, 0, 1, gg::rng::Field::SignalNoise, r});
        INFO("round " << r);
        const std::string fx = gg::core::patchXml(v, seed, Mode::Fx);
        REQUIRE(gg::core::patchXml(v, seed, Mode::Instrument) == fx);
        REQUIRE(toStateXml(v, seed) == fx);
    }
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

    SECTION("nesting deeper than 64 elements fails, without overflowing tinyxml's stack")
    {
        std::string deep;
        for (int i = 0; i < 1'000'000; ++i)
        {
            deep += "<a>";
        }
        CHECK_FALSE(p.load(replaceOnce(good, "<params>", "<params>" + deep)));
        CHECK(p.sameAs(before));
        std::string sixtyFive; // <patch><params> and 63 more
        for (int i = 0; i < 63; ++i)
        {
            sixtyFive += "<a>";
        }
        CHECK_FALSE(p.load(replaceOnce(good, "<params>", "<params>" + sixtyFive)));
        CHECK(p.sameAs(before));
    }

    SECTION("XML that tinyxml would nest deeper than it looks fails")
    {
        // The shapes a scan of the bytes can miscount (tests/support/DeepXml.h): each would
        // overflow tinyxml's stack. A process killed here is the failure.
        for (const gg::test::DeepXml &shape : gg::test::deepXmlShapes())
            DYNAMIC_SECTION(shape.name)
            {
                CHECK_FALSE(
                    p.load(shape.head + replaceOnce(good, "<params>", "<params>" + shape.body)));
                CHECK(p.sameAs(before));
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
