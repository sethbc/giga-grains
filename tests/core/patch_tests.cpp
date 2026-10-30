// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The patch: the slice's ten parameters (spec §4), Space's eight (sub-project 4's spec §6.5), the
// Perform UI's ten (sub-project 5's spec §3.3, §4, §5) and M2's layers (M2 sub-project 1's spec
// §6.9: 48 per layer, A to D, and nine globals; 218 in all), M2 sub-project 2's five ring
// parameters per layer (its spec §6.3; 53 per layer, 238 in all), and their versioned XML state,
// through gg-core's public API (gg/core/ParamIds.h, gg/core/PatchState.h) and its tinyxml-free
// internal core/src/PatchXml.h, for each product's patch XML. This file never includes
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
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <clap/clap.h>

#include "PatchXml.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/PatchState.h"
#include "gg/core/Processor.h"
#include "gg/core/SessionState.h"
#include "gg/core/Rng.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/SampleBuffer.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "support/DeepXml.h"
#include "support/ParamTable.h"
#include "support/StateContainer.h"

using gg::core::factoryRoutes;
using gg::core::fromStateXml;
using gg::core::kAllLayers;
using gg::core::kModSlots;
using gg::core::kParamCount;
using gg::core::kSameLayer;
using gg::core::ModCurve;
using gg::core::Mode;
using gg::core::ModRoute;
using gg::core::ModRoutes;
using gg::core::ModSource;
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

constexpr uint32_t kStepped = kAutoMod | CLAP_PARAM_IS_STEPPED;

// Global Constraints' parameter table, spelled out literally in tests/support/ParamTable.h (the
// FX's), with each slot and its CLAP flags (no AUTOMATABLE or MODULATABLE on kFixedIds', every
// layer's Ring Length). The instrument's differs only in every layer's
// Position default (kInstrumentPositionDefault) and display and Source default
// (kInstrumentSourceDefault), and in Macro 8's name (kInstrumentMacros).
std::array<Expected, kParamCount> buildExpected()
{
    const auto &rows = gg::test::paramTable();
    std::array<Expected, kParamCount> t{};
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto &r = rows[i];
        const uint32_t autoMod = r.automatable ? kAutoMod : 0u;
        t[i] = {static_cast<ParamSlot>(i),
                r.id,
                r.name,
                r.min,
                r.max,
                r.def,
                r.stepped ? autoMod | CLAP_PARAM_IS_STEPPED : autoMod};
    }
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

/// Whether two matrices are the same, every depth bit for bit (so -0 isn't +0).
bool sameRoutes(const ModRoutes &a, const ModRoutes &b)
{
    for (size_t k = 0; k < kModSlots; ++k)
    {
        if (!(a[k] == b[k]) || bitsOf(a[k].depth) != bitsOf(b[k].depth))
        {
            return false;
        }
    }
    return true;
}

/// The FX's factory routes: the routes most of these tests save.
const ModRoutes kFxRoutes = factoryRoutes(Mode::Fx);

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
    ModRoutes routes{};

    bool load(std::string_view xml, Mode mode = Mode::Fx)
    {
        return fromStateXml(xml, values, seed, routes, mode, &migratedFromVersion);
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
               migratedFromVersion == o.migratedFromVersion && sameRoutes(routes, o.routes);
    }
    float operator[](ParamSlot s) const { return values[static_cast<size_t>(s)]; }
};

// Values, seed, migration report and routes that differ from every load's result.
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
    // Every slot in use, unlike any factory route.
    for (size_t k = 0; k < kModSlots; ++k)
    {
        l.routes[k] = {
            ModSource::Macro8,   2, ParamSlot::Pitch, 1, 0.125f * static_cast<float>(k + 1),
            ModCurve::TopQuarter};
    }
    return l;
}

// A state written by hand, the way PatchBase writes it: <p id> in decimal.
std::string handState(std::string_view params, std::string_view version = "2",
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
    STATIC_REQUIRE(kParamCount == 238);
    STATIC_REQUIRE(std::is_same_v<ParamValues, std::array<float, 238>>);
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

    // The stepped parameters, and only they, are integers: the switches (each layer's Freeze and
    // Loop, Macro Freeze, the Layer On switches) and the choices (each layer's Source, LFO shapes,
    // Step Length and Splice).
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
        CHECK(steppedCount == 4 * 7 + 1 + 4);
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
    for (clap_id unknown :
         {0x000000u, 0x00000Au, 0x000010u, 0x000022u, 0x010000u, 0x010031u, 0x020000u, 0x020031u,
          0x040031u, 0x050001u, 0x000040u, 0x01004Fu, 0x010045u, 0x040045u, 0x000041u, 0x030060u,
          0x04007Fu, 0x777777u, CLAP_INVALID_ID})
    {
        INFO("id " << unknown);
        CHECK_FALSE(gg::core::slotForId(unknown).has_value());
    }
}

TEST_CASE("the id table has the next free indices and the reserved blocks", "[core]")
{
    // M2 sub-project 1's spec §6.9: no index of this sub-project's falls in 0x40 to 0x7F (wave
    // 2's), and the recorded next-free indices are the next free index below the reserved blocks,
    // one past the table's largest index below them, in the layer scopes and in scope 0. Wave 2's
    // rows, inside their blocks, are left out of both checks.
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
        CHECK(index <= gg::core::kReservedIndexEnd);
        CHECK((id >> 16) <= 4u);
        if (index < gg::core::kReservedIndexBegin)
        {
            uint32_t &maxIndex = (id >> 16) == 0 ? maxGlobalIndex : maxLayerIndex;
            maxIndex = std::max(maxIndex, index);
        }
    }
    STATIC_REQUIRE(gg::core::kNextFreeLayerIndex <= gg::core::kReservedIndexBegin);
    STATIC_REQUIRE(gg::core::kNextFreeGlobalIndex <= gg::core::kReservedIndexBegin);
    CHECK(maxLayerIndex + 1 == gg::core::kNextFreeLayerIndex);
    CHECK(maxGlobalIndex + 1 == gg::core::kNextFreeGlobalIndex);

    // The helpers between a flat slot and (layer parameter, layer).
    using gg::core::LayerParam;
    using gg::core::LayerRef;
    STATIC_REQUIRE(gg::core::kLayers == 4);
    STATIC_REQUIRE(gg::core::kLayerParamCount == 53);
    STATIC_REQUIRE(gg::core::layerParamIndex(LayerParam::Position) == 0x01u);
    STATIC_REQUIRE(gg::core::layerParamIndex(LayerParam::Diffusion) == 0x0Bu);
    STATIC_REQUIRE(gg::core::layerParamIndex(LayerParam::Source) == 0x0Cu);
    STATIC_REQUIRE(gg::core::layerParamIndex(LayerParam::Step16) == 0x30u);
    STATIC_REQUIRE(gg::core::layerParamIndex(LayerParam::RingLength) == 0x40u);
    STATIC_REQUIRE(gg::core::layerParamIndex(LayerParam::Splice) == 0x44u);
    STATIC_REQUIRE(gg::core::slotForId(0x040044) == ParamSlot::D_Splice);
    STATIC_REQUIRE(gg::core::layerOf(ParamSlot::RingLength) == LayerRef{LayerParam::RingLength, 0});
    STATIC_REQUIRE(gg::core::layerSlot(LayerParam::Splice, 1) == ParamSlot::B_Splice);
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
            CHECK(gg::core::paramId(slot) ==
                  ((layer + 1u) << 16 | gg::core::layerParamIndex(param)));
            CHECK(gg::core::paramId(slot) == kTable[s].id);
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

TEST_CASE("a layer index no row has is no parameter, and a gap in the index column is kept",
          "[core]")
{
    // M2 programme design §7.1: wave 2 appends layer rows inside its reserved blocks, so the index
    // column has gaps (0x31 to 0x3F, and each block's unused indices). The index is read from the
    // column, never from a row's position. The mechanism, on a column with gaps:
    using gg::core::detail::rowsByIndex;
    using gg::core::detail::validIndexColumn;
    constexpr std::array<uint32_t, 4> kGapped{0x01, 0x02, 0x41, 0x60};
    STATIC_REQUIRE(validIndexColumn(kGapped));
    constexpr auto rows = rowsByIndex(kGapped);
    STATIC_REQUIRE(rows.size() == 0x80);
    STATIC_REQUIRE(rows[0x01] == 0);
    STATIC_REQUIRE(rows[0x02] == 1);
    STATIC_REQUIRE(rows[0x41] == 2);
    STATIC_REQUIRE(rows[0x60] == 3);
    for (uint32_t index = 0; index < rows.size(); ++index)
    {
        INFO("index " << index);
        if (index != 0x01 && index != 0x02 && index != 0x41 && index != 0x60)
        {
            CHECK(rows[index] == kGapped.size());
        }
    }
    // Index 0, an index past 0x7F, a repeat and rows out of index order are no column.
    STATIC_REQUIRE_FALSE(validIndexColumn(std::array<uint32_t, 2>{0x00, 0x01}));
    STATIC_REQUIRE_FALSE(validIndexColumn(std::array<uint32_t, 2>{0x01, 0x80}));
    STATIC_REQUIRE_FALSE(validIndexColumn(std::array<uint32_t, 2>{0x02, 0x02}));
    STATIC_REQUIRE_FALSE(validIndexColumn(std::array<uint32_t, 2>{0x03, 0x02}));

    // The table's own: every index of every scope (0x00 to 0x7F) that no ID in the table has is no
    // parameter, the reserved blocks' and 0x31 to 0x3F among them, and every ID in the table leads
    // to its slot.
    std::array<std::array<bool, 0x80>, 5> used{};
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const clap_id id = kTable[i].id;
        INFO("param " << kTable[i].name << ", id " << id);
        REQUIRE((id >> 16) <= 4u);
        REQUIRE((id & 0xFFFFu) < 0x80u);
        used[id >> 16][id & 0xFFFFu] = true;
        REQUIRE(gg::core::slotForId(id) == kTable[i].slot);
        CHECK(gg::core::paramId(*gg::core::slotForId(id)) == id);
    }
    size_t unused = 0;
    for (clap_id scope = 0; scope <= 4; ++scope)
    {
        for (clap_id index = 0; index < 0x80; ++index)
        {
            const clap_id id = scope << 16 | index;
            INFO("id " << id);
            CHECK(gg::core::slotForId(id).has_value() == used[scope][index]);
            unused += used[scope][index] ? 0 : 1;
        }
    }
    CHECK(unused == 5 * 0x80 - kParamCount);
}

TEST_CASE("the ring parameters are pinned by name and ID", "[core]")
{
    // M2 sub-project 2's spec §6.3: five per layer at 0x40 to 0x44 of each layer's scope.
    struct Pin
    {
        const char *name;
        clap_id id;
    };
    constexpr std::array<Pin, 20> kPins{{
        {"A Ring Length", 0x010040}, {"A Loop", 0x010041},        {"A Loop Length", 0x010042},
        {"A Overdub", 0x010043},     {"A Splice", 0x010044},      {"B Ring Length", 0x020040},
        {"B Loop", 0x020041},        {"B Loop Length", 0x020042}, {"B Overdub", 0x020043},
        {"B Splice", 0x020044},      {"C Ring Length", 0x030040}, {"C Loop", 0x030041},
        {"C Loop Length", 0x030042}, {"C Overdub", 0x030043},     {"C Splice", 0x030044},
        {"D Ring Length", 0x040040}, {"D Loop", 0x040041},        {"D Loop Length", 0x040042},
        {"D Overdub", 0x040043},     {"D Splice", 0x040044},
    }};
    for (const Pin &pin : kPins)
    {
        INFO("param " << pin.name);
        const auto row = std::find_if(kTable.begin(), kTable.end(),
                                      [&pin](const Expected &e) { return e.name == pin.name; });
        REQUIRE(row != kTable.end());
        CHECK(row->id == pin.id);
        REQUIRE(gg::core::slotForId(pin.id).has_value());
        CHECK(*gg::core::slotForId(pin.id) == row->slot);
        for (const Mode mode : {Mode::Fx, Mode::Instrument})
        {
            const auto &m = gg::core::paramMeta(row->slot, mode);
            CHECK(m.id == pin.id);
            CHECK(m.name == pin.name);
        }
    }
    STATIC_REQUIRE(gg::core::paramId(ParamSlot::RingLength) == 0x010040u);
    STATIC_REQUIRE(gg::core::paramId(ParamSlot::Splice) == 0x010044u);
    STATIC_REQUIRE(gg::core::paramId(ParamSlot::B_RingLength) == 0x020040u);
    STATIC_REQUIRE(gg::core::paramId(ParamSlot::D_Splice) == 0x040044u);
    // Ring Length is neither automatable nor modulatable (spec §5: the main thread resizes the
    // ring); the other four are both, and Loop and Splice are stepped.
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        using gg::core::LayerParam;
        INFO("layer " << int{layer});
        const auto flags = [layer](LayerParam p) {
            return gg::core::paramMeta(gg::core::layerSlot(p, layer), Mode::Fx).flags;
        };
        CHECK(flags(LayerParam::RingLength) == 0u);
        CHECK(flags(LayerParam::Loop) == kStepped);
        CHECK(flags(LayerParam::LoopLength) == kAutoMod);
        CHECK(flags(LayerParam::Overdub) == kAutoMod);
        CHECK(flags(LayerParam::Splice) == kStepped);
    }
}

TEST_CASE("the next free indices are unchanged", "[core]")
{
    // M2 sub-project 2's rows sit in its reserved block (programme design §7.1), so the next free
    // indices below the blocks don't move, 0x45 to 0x4F stay unused, the other sub-projects'
    // blocks (0x50 to 0x7F) stay empty, and scope 0's block takes nothing.
    STATIC_REQUIRE(gg::core::kNextFreeLayerIndex == 0x31u);
    STATIC_REQUIRE(gg::core::kNextFreeGlobalIndex == 0x22u);
    STATIC_REQUIRE(gg::core::kReservedIndexBegin == 0x40u);
    STATIC_REQUIRE(gg::core::kReservedIndexEnd == 0x7Fu);
    for (clap_id scope = 0; scope <= 4; ++scope)
    {
        for (clap_id index = 0x40; index <= 0x7F; ++index)
        {
            const clap_id id = scope << 16 | index;
            INFO("id " << id);
            const bool ours = scope >= 1 && index <= 0x44;
            CHECK(gg::core::slotForId(id).has_value() == ours);
        }
    }
}

TEST_CASE("a version 2 state without the ring parameters loads them at their defaults", "[core]")
{
    // A state saved before M2 sub-project 2 (spec §6.3: no version bump) has no ring parameters:
    // they load at their defaults, A Ring Length 4.0 (16 s) among them, in either product, and
    // every other value as saved.
    using gg::core::LayerParam;
    constexpr std::array<LayerParam, 5> kRing{LayerParam::RingLength, LayerParam::Loop,
                                              LayerParam::LoopLength, LayerParam::Overdub,
                                              LayerParam::Splice};
    ParamValues saved = defaults();
    saved[static_cast<size_t>(ParamSlot::Size)] = -1.0f;
    saved[static_cast<size_t>(ParamSlot::B_Pitch)] = 7.0f;
    std::string state = toStateXml(saved, 3, kFxRoutes);
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        for (const LayerParam p : kRing)
        {
            const auto tag = R"(<p id=")" +
                             std::to_string(gg::core::paramId(gg::core::layerSlot(p, layer))) +
                             R"(" )";
            const auto from = state.find(tag);
            REQUIRE(from != std::string::npos);
            const auto to = state.find("/>", from);
            REQUIRE(to != std::string::npos);
            state.erase(from, to + 2 - from);
            REQUIRE(state.find(tag) == std::string::npos);
        }
    }
    REQUIRE(state.find(R"(version="2")") != std::string::npos);
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        INFO((mode == Mode::Fx ? "FX" : "instrument"));
        // The ring parameters' defaults are the same in both products, and `saved` holds them.
        Loaded l = distinct();
        REQUIRE(l.load(state, mode));
        CHECK(l.valueBits() == Loaded{saved, 3, 0}.valueBits());
        CHECK(bitsOf(l[ParamSlot::RingLength]) == bitsOf(4.0f));
        CHECK(bitsOf(l[ParamSlot::D_RingLength]) == bitsOf(4.0f));
        CHECK(bitsOf(l[ParamSlot::LoopLength]) == bitsOf(0.0f));
        CHECK(l.migratedFromVersion == 0u);
    }
}

TEST_CASE("the state lists every param in slot order", "[core]")
{
    CHECK(gg::core::kPatchVersion == 2u);
    const auto state = toStateXml(defaults(), 0, kFxRoutes);
    CHECK(state.starts_with(R"(<patch id="org.sethbc.giga-grains.patch" version="2" )"));

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
    // Then the engine seed, then the routes, and the end of the patch.
    const auto engine = state.find(R"(</params><engine seed="0" /><mod><route slot="1" )");
    REQUIRE(engine != std::string::npos);
    CHECK(state.ends_with("</mod></patch>"));

    // The defaults load back as the defaults.
    Loaded l = distinct();
    REQUIRE(l.load(state));
    CHECK(l.valueBits() == Loaded{defaults(), 0, 0}.valueBits());
    CHECK(l.seed == 0u);
    CHECK(l.migratedFromVersion == 0u);
    CHECK(sameRoutes(l.routes, kFxRoutes));
}

TEST_CASE("a param missing from a state takes the product's default", "[core]")
{
    // A state whose Position isn't the default, with Position's element cut out.
    ParamValues saved = defaults();
    saved[static_cast<size_t>(ParamSlot::Position)] = 0.7f;
    saved[static_cast<size_t>(ParamSlot::Size)] = -1.0f;
    const auto full = toStateXml(saved, 4, kFxRoutes);
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
    // others take the product's defaults. It is a version 1 state, so it migrates: the factory
    // routes, and in the instrument its Macro 7, absent, is M1's default (0.5), which leaves
    // Stereo where it was (spec §6.10).
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
        CHECK(l.migratedFromVersion == 1u);
        CHECK(sameRoutes(l.routes, factoryRoutes(mode)));
        for (size_t i = static_cast<size_t>(ParamSlot::Diffusion); i < kParamCount; ++i)
        {
            INFO("param " << kTable[i].name);
            CHECK(bitsOf(l.values[i]) == bitsOf(defaultIn(kTable[i], mode)));
        }

        // Saving it writes today's state, at version 2: the ten as they were, then the others'
        // defaults, then the routes.
        const auto resaved = toStateXml(l.values, l.seed, l.routes);
        const auto oldParams =
            replaceOnce(std::string(kPreSpaceState.substr(0, kPreSpaceState.find("</params>"))),
                        R"(version="1")", R"(version="2")");
        CHECK(resaved.starts_with(oldParams));
        CHECK(resaved == toStateXml(want, 0, factoryRoutes(mode)));
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
    // defaults. A version 1 state, it migrates as kPreSpaceState does above.
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
        CHECK(l.migratedFromVersion == 1u);
        CHECK(sameRoutes(l.routes, factoryRoutes(mode)));

        // As the PTCH chunk of a session state, as the plugins saved it, with no sample.
        const auto withOldPatch =
            gg::test::stateContainer({gg::test::stateChunk("PTCH", kPrePerformState),
                                      gg::test::stateChunk("MEDI", R"(<media version="1" />)")});
        gg::core::SessionState decoded;
        REQUIRE(gg::core::decodeState(withOldPatch, mode, decoded));
        CHECK(Loaded{decoded.values, decoded.seed, 0}.valueBits() ==
              Loaded{want, 0, 0}.valueBits());
        CHECK(decoded.seed == 0u);
        CHECK(sameRoutes(decoded.routes, factoryRoutes(mode)));

        // Saving it writes today's state, at version 2: the eighteen as they were, then the
        // others' defaults, then the routes.
        const auto resaved = toStateXml(l.values, l.seed, l.routes);
        const auto oldParams =
            replaceOnce(std::string(kPrePerformState.substr(0, kPrePerformState.find("</params>"))),
                        R"(version="1")", R"(version="2")");
        CHECK(resaved.starts_with(oldParams));
        CHECK(resaved == toStateXml(want, 0, factoryRoutes(mode)));
        if (mode == Mode::Fx)
        {
            CHECK(resaved == toStateXml(defaults(), 0, kFxRoutes));
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
    // The ring's (M2 sub-project 2's spec §6.3): the lengths in log2 seconds, Overdub a
    // percentage, Loop a switch and Splice a count; Position reaches the longest ring.
    CHECK(text(ParamSlot::RingLength, 0.f) == "1.00 s");
    CHECK(text(ParamSlot::RingLength, 4.f) == "16.00 s");
    CHECK(text(ParamSlot::B_RingLength, 8.228818690495881f) == "300.00 s");
    CHECK(text(ParamSlot::LoopLength, -4.321928094887363f) == "50.0 ms");
    CHECK(text(ParamSlot::C_LoopLength, 0.f) == "1.00 s");
    CHECK(text(ParamSlot::Loop, 0.f) == "Off");
    CHECK(text(ParamSlot::Loop, 1.f) == "On");
    CHECK(text(ParamSlot::Overdub, 0.5f) == "50.00 %");
    CHECK(text(ParamSlot::Splice, 0.f) == "0");
    CHECK(text(ParamSlot::D_Splice, 32.f) == "32");
    CHECK(text(ParamSlot::Position, gg::core::kPositionMax) == "300.00 s");
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
        const auto &routes = r % 2 == 0 ? kFxRoutes : factoryRoutes(Mode::Instrument);
        const auto state = toStateXml(rounds[r], 0x0123456789abcdefull, routes);

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
        CHECK(sameRoutes(loaded.routes, routes));
        // Saving what was loaded writes the same state again.
        CHECK(toStateXml(loaded.values, loaded.seed, loaded.routes) == state);
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
        const auto &routes = r % 2 == 0 ? kFxRoutes : factoryRoutes(Mode::Instrument);
        const std::string fx = gg::core::patchXml(v, seed, routes, Mode::Fx);
        REQUIRE(gg::core::patchXml(v, seed, routes, Mode::Instrument) == fx);
        REQUIRE(toStateXml(v, seed, routes) == fx);
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
        const auto state = toStateXml(saved, 0, kFxRoutes);
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
        REQUIRE(loaded.load(toStateXml(defaults(), seed, kFxRoutes)));
        CHECK(loaded.seed == seed);
    }
}

TEST_CASE("patch rejects or sanitizes bad state", "[core]")
{
    const Loaded source = distinct();
    const uint64_t sourceSeed = 0x0123456789abcdefull;
    const auto good = toStateXml(source.values, sourceSeed, source.routes);

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
              replaceOnce(good, R"(version="2")", ""),
              replaceOnce(good, R"(version="2")", R"(version="one")"), noParams})
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
        CHECK(sameRoutes(p.routes, source.routes));
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
        // Params the state doesn't mention take their defaults, and with no <mod> every slot is
        // empty.
        CHECK(p[ParamSlot::Density] == 4.0f);
        CHECK(p.seed == 0u);
        CHECK(p.migratedFromVersion == 0u);
        CHECK(sameRoutes(p.routes, ModRoutes{}));
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
        auto newer = replaceOnce(good, R"(version="2")", R"(version="99")");
        REQUIRE(p.load(newer));
        CHECK(p.migratedFromVersion == 99u);
        CHECK(p.valueBits() == source.valueBits());
        CHECK(p.seed == sourceSeed);
        CHECK(sameRoutes(p.routes, source.routes));

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
            REQUIRE(p.load(handState(R"(<p id="1" v="0.25" />)", "2", bad)));
            CHECK(p.seed == 0u);
        }
    }

    SECTION("the migration report is optional")
    {
        ParamValues values{};
        uint64_t seed = 5;
        ModRoutes routes{};
        REQUIRE(fromStateXml(good, values, seed, routes, Mode::Fx));
        CHECK(seed == sourceSeed);
        CHECK(values[0] == source.values[0]);
        CHECK(sameRoutes(routes, source.routes));
    }
}

// ---- Patch version 2: the routes, and the migration from version 1 (M2 sub-project 1's spec
// §6.10).

namespace
{
/// A float's bits as PatchBase's states write them: 8 lowercase hex digits.
std::string hexBits(float f)
{
    char text[9];
    std::snprintf(text, sizeof(text), "%08x", static_cast<unsigned>(bitsOf(f)));
    return text;
}

// An M1 FX state at the FX's defaults, as toStateXml wrote it at 1f8cb33 (version 1, the 28 M1
// params in slot order, no <mod>), written by hand in that form: the defaults' v texts and bits
// as kPrePerformState has them, then Texture, Feedback and the macros at rest.
constexpr std::string_view kM1FxState =
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
    R"(<p id="65545" v="0.000000000000" bits="00000000" />)"
    R"(<p id="65546" v="0.000000000000" bits="00000000" />)"
    R"(<p id="17" v="0.500000000000" bits="3f000000" />)"
    R"(<p id="18" v="0.500000000000" bits="3f000000" />)"
    R"(<p id="19" v="0.500000000000" bits="3f000000" />)"
    R"(<p id="20" v="0.500000000000" bits="3f000000" />)"
    R"(<p id="21" v="0.000000000000" bits="00000000" />)"
    R"(<p id="22" v="0.000000000000" bits="00000000" />)"
    R"(<p id="23" v="0.000000000000" bits="00000000" />)"
    R"(<p id="24" v="0.500000000000" bits="3f000000" />)"
    R"(</params><engine seed="0" /></patch>)";

// An M1 instrument state in the same form: the instrument's M1 defaults (Position 0), but Stereo
// (65543) at 0.3 and Macro 7 (23, M1's Macro Stereo) at 0.8, and seed 7.
constexpr std::string_view kM1InstrumentState =
    R"(<patch id="org.sethbc.giga-grains.patch" version="1" name="Init"><params>)"
    R"(<p id="1" v="0.500000000000" bits="3f000000" />)"
    R"(<p id="2" v="0.000000000000" bits="00000000" />)"
    R"(<p id="65537" v="0.000000000000" bits="00000000" />)"
    R"(<p id="65538" v="-2.736965656281" bits="c02f2a72" />)"
    R"(<p id="65539" v="4.000000000000" bits="40800000" />)"
    R"(<p id="65540" v="0.500000000000" bits="3f000000" />)"
    R"(<p id="65541" v="0.000000000000" bits="00000000" />)"
    R"(<p id="65542" v="0.360330373049" bits="3eb87d39" />)"
    R"(<p id="65543" v="0.300000011921" bits="3e99999a" />)"
    R"(<p id="65544" v="0.000000000000" bits="00000000" />)"
    R"(<p id="65547" v="0.000000000000" bits="00000000" />)"
    R"(<p id="3" v="0.000000000000" bits="00000000" />)"
    R"(<p id="4" v="1.000000000000" bits="3f800000" />)"
    R"(<p id="5" v="0.500000000000" bits="3f000000" />)"
    R"(<p id="6" v="0.300000011921" bits="3e99999a" />)"
    R"(<p id="7" v="0.000000000000" bits="00000000" />)"
    R"(<p id="8" v="0.250000000000" bits="3e800000" />)"
    R"(<p id="9" v="0.000000000000" bits="00000000" />)"
    R"(<p id="65545" v="0.000000000000" bits="00000000" />)"
    R"(<p id="65546" v="0.000000000000" bits="00000000" />)"
    R"(<p id="17" v="0.500000000000" bits="3f000000" />)"
    R"(<p id="18" v="0.500000000000" bits="3f000000" />)"
    R"(<p id="19" v="0.500000000000" bits="3f000000" />)"
    R"(<p id="20" v="0.500000000000" bits="3f000000" />)"
    R"(<p id="21" v="0.000000000000" bits="00000000" />)"
    R"(<p id="22" v="0.000000000000" bits="00000000" />)"
    R"(<p id="23" v="0.800000011921" bits="3f4ccccd" />)"
    R"(<p id="24" v="0.500000000000" bits="3f000000" />)"
    R"(</params><engine seed="7" /></patch>)";

/// The instrument's M1 fold of Macro 7 (Macro Stereo) into Stereo, in the spec's float
/// expression and order.
float foldedStereo(float stereo, float macro7)
{
    return std::clamp(stereo + 0.5f * (2.0f * macro7 - 1.0f), 0.0f, 1.0f);
}

/// The slots a route may target (isValidTarget), in slot order.
std::vector<ParamSlot> validTargets()
{
    std::vector<ParamSlot> t;
    for (size_t i = 0; i < kParamCount; ++i)
    {
        if (gg::core::isValidTarget(static_cast<ParamSlot>(i)))
        {
            t.push_back(static_cast<ParamSlot>(i));
        }
    }
    return t;
}

/// 48 routes, every slot in use, drawn from the engine's RNG: any source, curve and valid target,
/// every layer choice, and depths that the 12-decimal text can't hold among them.
ModRoutes randomRoutes(uint64_t round)
{
    static const std::vector<ParamSlot> targets = validTargets();
    constexpr std::array<float, 8> kSpecial{-0.0f,
                                            0.0f,
                                            std::numeric_limits<float>::denorm_min(),
                                            -1.2345678e-7f,
                                            std::numeric_limits<float>::max(),
                                            std::numeric_limits<float>::lowest(),
                                            72.0f,
                                            -0.001f};
    const auto draw = [round](uint32_t field, uint64_t k) {
        return gg::rng::hash(
            {0x5eed'7e57'0000'0009ull + round, 0, field, gg::rng::Field::SignalNoise, k});
    };
    constexpr std::array<uint8_t, 5> kSourceLayers{0, 1, 2, 3, kSameLayer};
    constexpr std::array<uint8_t, 5> kTargetLayers{0, 1, 2, 3, kAllLayers};
    ModRoutes r{};
    for (uint64_t k = 0; k < kModSlots; ++k)
    {
        ModRoute &route = r[k];
        route.source =
            static_cast<ModSource>(1 + draw(0, k) % (static_cast<uint64_t>(ModSource::Count) - 1));
        route.sourceLayer = kSourceLayers[draw(1, k) % kSourceLayers.size()];
        route.target = targets[draw(2, k) % targets.size()];
        route.targetLayer = kTargetLayers[draw(3, k) % kTargetLayers.size()];
        route.curve = static_cast<ModCurve>(draw(4, k) % static_cast<uint64_t>(ModCurve::Count));
        route.depth = k < kSpecial.size() ? kSpecial[k] : randomIn(-100.0f, 100.0f, draw(5, k));
    }
    return r;
}

/// A <route> element with these attributes, in this order.
std::string routeXml(const std::vector<std::pair<std::string, std::string>> &attributes)
{
    std::string s = "<route";
    for (const auto &[name, value] : attributes)
    {
        s += " " + name + "=\"" + value + "\"";
    }
    return s + " />";
}

/// The attributes of a good route in slot 5: LFO 1 on layer B, to Pitch on layer C, depth -2.5,
/// UnipolarSquared.
std::vector<std::pair<std::string, std::string>> slot5Attributes()
{
    return {{"slot", "5"},        {"source", "LFO 1"},         {"sourceLayer", "B"},
            {"target", "65541"},  {"targetLayer", "C"},        {"depth", "-2.500000000000"},
            {"bits", "c0200000"}, {"curve", "UnipolarSquared"}};
}
const ModRoute kSlot5Route{ModSource::Lfo1,          1, ParamSlot::Pitch, 2, -2.5f,
                           ModCurve::UnipolarSquared};

/// Three routes, in slots 1 to 3.
ModRoutes threeRoutes()
{
    ModRoutes r{};
    r[0] = {ModSource::Macro1, kSameLayer, ParamSlot::Size, kAllLayers, 3.0f, ModCurve::Bipolar};
    r[1] = {ModSource::Lfo2, 3, ParamSlot::Density, 0, -1.5f, ModCurve::Unipolar};
    r[2] = {ModSource::ModWheel, kSameLayer, ParamSlot::Mix,
            kAllLayers,          0.25f,      ModCurve::BipolarPositive};
    return r;
}

/// One second of the instrument over a mono noise sample, one note held from sample 0, with
/// every base value set to `values` and a modulation of `stereoMod` on Stereo from sample 0.
struct InstrumentRender
{
    std::vector<float> left, right;
    float effectiveStereo{0.0f};
};
InstrumentRender renderInstrument(const ParamValues &values, float stereoMod)
{
    constexpr double kRate = 48000.0;
    constexpr int64_t kFrames = 48000;
    auto buffer = std::make_shared<gg::SampleBuffer>(1, kFrames, kRate);
    for (int64_t i = 0; i < kFrames; ++i)
    {
        const double u = gg::rng::uniform01(
            gg::rng::hash({0x5eed'7e57'0000'0019ull, 0, 0, gg::rng::Field::SignalNoise,
                           static_cast<uint64_t>(i)}));
        buffer->channel(0)[i] = static_cast<float>(0.5 * (2.0 * u - 1.0));
    }
    gg::core::Processor p({kRate, 512, 42, Mode::Instrument});
    p.setSourceNow(gg::PreparedSource::make(std::move(buffer)));
    for (size_t i = 0; i < kParamCount; ++i)
    {
        p.setBase(static_cast<ParamSlot>(i), values[i]);
    }
    InstrumentRender r;
    r.left.assign(static_cast<size_t>(kFrames), 0.0f);
    r.right.assign(static_cast<size_t>(kFrames), 0.0f);
    const gg::core::NoteEvent on{0, gg::core::NoteEventType::On, 0, 60};
    const gg::core::ParamEvent mod{0, ParamSlot::Stereo, true, static_cast<double>(stereoMod)};
    for (int64_t done = 0; done < kFrames;)
    {
        const auto n = static_cast<uint32_t>(std::min<int64_t>(512, kFrames - done));
        float *out[2] = {r.left.data() + done, r.right.data() + done};
        const bool first = done == 0;
        p.process(nullptr, 0, out, 2, n,
                  first && stereoMod != 0.0f ? std::span(&mod, 1)
                                             : std::span<const gg::core::ParamEvent>{},
                  first ? std::span(&on, 1) : std::span<const gg::core::NoteEvent>{});
        done += n;
    }
    r.effectiveStereo = p.effective(ParamSlot::Stereo);
    return r;
}

bool sameSamples(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}
} // namespace

TEST_CASE("routes round-trip through the patch", "[core]")
{
    // 100 rounds of 48 routes, every slot in use, in either product: each comes back as written,
    // its depth bit for bit, and saving what was loaded writes the same state again.
    for (uint64_t round = 0; round < 100; ++round)
    {
        INFO("round " << round);
        const ModRoutes routes = randomRoutes(round);
        for (const ModRoute &route : routes)
        {
            REQUIRE(gg::core::isSavableRoute(route));
        }
        const auto state = toStateXml(defaults(), round, routes);
        for (const Mode mode : {Mode::Fx, Mode::Instrument})
        {
            Loaded l = distinct();
            REQUIRE(l.load(state, mode));
            CHECK(l.migratedFromVersion == 0u);
            CHECK(sameRoutes(l.routes, routes));
            CHECK(toStateXml(l.values, l.seed, l.routes) == state);
        }
    }

    SECTION("empty slots are left out, and a route it can't write is left out with them")
    {
        ModRoutes routes = threeRoutes();
        routes[47] = kSlot5Route;
        ModRoutes unsavable = routes;
        unsavable[10] = {ModSource::Count, kSameLayer, ParamSlot::Size,
                         kAllLayers,       1.0f,       ModCurve::Bipolar};
        unsavable[11] = {ModSource::Macro1, kSameLayer, ParamSlot::Macro2,
                         kAllLayers,        1.0f,       ModCurve::Bipolar};
        unsavable[12] = {ModSource::Macro1, kSameLayer, ParamSlot::B_Size,
                         kAllLayers,        1.0f,       ModCurve::Bipolar};
        unsavable[13] = {ModSource::Macro1, kAllLayers, ParamSlot::Size,
                         kAllLayers,        1.0f,       ModCurve::Bipolar};
        unsavable[14] = {ModSource::Macro1, kSameLayer, ParamSlot::Size,
                         kSameLayer,        1.0f,       ModCurve::Bipolar};
        unsavable[15] = {ModSource::Macro1,
                         kSameLayer,
                         ParamSlot::Size,
                         kAllLayers,
                         std::numeric_limits<float>::quiet_NaN(),
                         ModCurve::Bipolar};
        unsavable[16] = {ModSource::Macro1,
                         kSameLayer,
                         ParamSlot::Size,
                         kAllLayers,
                         std::numeric_limits<float>::infinity(),
                         ModCurve::Bipolar};
        unsavable[17] = {ModSource::Macro1, kSameLayer, ParamSlot::Size,
                         kAllLayers,        1.0f,       ModCurve::Count};
        unsavable[18] = {ModSource::Macro1, 4,    ParamSlot::Size,
                         kAllLayers,        1.0f, ModCurve::Bipolar};
        unsavable[19] = {ModSource::Macro1, kSameLayer, ParamSlot::Count,
                         kAllLayers,        1.0f,       ModCurve::Bipolar};
        for (size_t k = 10; k < 20; ++k)
        {
            INFO("slot " << k + 1);
            CHECK_FALSE(gg::core::isSavableRoute(unsavable[k]));
        }

        const auto state = toStateXml(defaults(), 0, routes);
        CHECK(toStateXml(defaults(), 0, unsavable) == state);
        size_t written = 0;
        for (size_t at = state.find("<route "); at != std::string::npos;
             at = state.find("<route ", at + 1))
        {
            ++written;
        }
        CHECK(written == 4);
        CHECK(state.find(R"(<route slot="48" )") != std::string::npos);

        Loaded l = distinct();
        REQUIRE(l.load(state));
        CHECK(sameRoutes(l.routes, routes));

        // With every slot empty, <mod> is empty.
        CHECK(toStateXml(defaults(), 0, ModRoutes{})
                  .ends_with(R"(<engine seed="0" /><mod /></patch>)"));
    }
}

TEST_CASE("a route's target is saved by its CLAP ID", "[core]")
{
    // Names aren't frozen, IDs are (INTENT §18): a route names its target by the slot's CLAP ID
    // in decimal, as <p id> does, so a renamed parameter never breaks a saved route.
    ModRoutes routes{};
    routes[0] = {ModSource::Macro1, kSameLayer, ParamSlot::Size,
                 kAllLayers,        3.0f,       ModCurve::Bipolar};
    routes[1] = {ModSource::Velocity, kSameLayer, ParamSlot::Level, 2, 6.0f, ModCurve::Unipolar};
    routes[2] = {ModSource::Key, kSameLayer, ParamSlot::LayerAOn, 3, 1.0f, ModCurve::Unipolar};
    routes[3] = {ModSource::Macro8, kSameLayer, ParamSlot::Mix,
                 kAllLayers,        0.5f,       ModCurve::Bipolar};
    const auto state = toStateXml(defaults(), 0, routes);
    CHECK(state.find(
              R"(<mod><route slot="1" source="Macro 1" sourceLayer="same" target="65538" )"
              R"(targetLayer="all" depth="3.000000000000" bits="40400000" curve="Bipolar" />)") !=
          std::string::npos);
    CHECK(state.find(R"(<route slot="2" source="Velocity" sourceLayer="same" target="2" )"
                     R"(targetLayer="C" )") != std::string::npos);
    CHECK(state.find(R"(<route slot="3" source="Key" sourceLayer="same" target="27" )"
                     R"(targetLayer="D" )") != std::string::npos);
    CHECK(state.find(R"(<route slot="4" source="Macro 8" sourceLayer="same" target="1" )") !=
          std::string::npos);
    // No parameter's name is written.
    CHECK(state.find("Size") == std::string::npos);
    CHECK(state.find("Level") == std::string::npos);

    // A route written by hand for 65538 is A Size, whatever its name.
    const auto hand =
        handState("", "2",
                  R"(<mod><route slot="7" source="Env 2" sourceLayer="A" target="65538" )"
                  R"(targetLayer="B" depth="-1.000000000000" bits="bf800000" curve="TopQuarter" />)"
                  R"(</mod>)");
    Loaded l = distinct();
    REQUIRE(l.load(hand));
    ModRoutes want{};
    want[6] = {ModSource::Env2, 0, ParamSlot::Size, 1, -1.0f, ModCurve::TopQuarter};
    CHECK(sameRoutes(l.routes, want));
    CHECK(l.routes[6].target == ParamSlot::Size);
    CHECK(gg::core::paramId(l.routes[6].target) == 0x010002u);
}

TEST_CASE("a version 1 FX state migrates to the factory routes", "[core]")
{
    // The literal state's bits are the floats' (std::bit_cast).
    REQUIRE(hexBits(0.5f) == "3f000000");
    REQUIRE(hexBits(0.28967658f) == "3e94507d");
    REQUIRE(hexBits(-2.736965594166206f) == "c02f2a72");
    REQUIRE(hexBits(0.36033038f) == "3eb87d39");
    REQUIRE(hexBits(0.3f) == "3e99999a");
    REQUIRE(hexBits(0.8f) == "3f4ccccd");

    Loaded l = distinct();
    REQUIRE(l.load(kM1FxState, Mode::Fx));
    CHECK(l.migratedFromVersion == 1u);
    CHECK(sameRoutes(l.routes, factoryRoutes(Mode::Fx)));
    // Every value as before: the 28 as saved (the FX's defaults) and M2's at their defaults.
    CHECK(l.valueBits() == Loaded{defaults(Mode::Fx), 0, 0}.valueBits());
    CHECK(l.seed == 0u);

    // The FX's Macro 7 was Macro Feedback in M1 already: untouched, and so is Stereo.
    std::string withMacro7(kM1FxState);
    withMacro7 = replaceOnce(withMacro7, R"(<p id="23" v="0.000000000000" bits="00000000" />)",
                             R"(<p id="23" v="0.800000011921" bits="3f4ccccd" />)");
    withMacro7 = replaceOnce(withMacro7, R"(<p id="65543" v="0.500000000000" bits="3f000000" />)",
                             R"(<p id="65543" v="0.300000011921" bits="3e99999a" />)");
    Loaded m = distinct();
    REQUIRE(m.load(withMacro7, Mode::Fx));
    CHECK(bitsOf(m[ParamSlot::Macro7]) == bitsOf(0.8f));
    CHECK(bitsOf(m[ParamSlot::Stereo]) == bitsOf(0.3f));
    CHECK(sameRoutes(m.routes, factoryRoutes(Mode::Fx)));

    // Any <mod> in a version 1 state is ignored: the migration decides version 1's routes.
    Loaded withMod = distinct();
    REQUIRE(
        withMod.load(replaceOnce(std::string(kM1FxState), "</patch>",
                                 routeXml(slot5Attributes()).insert(0, "<mod>") + "</mod></patch>"),
                     Mode::Fx));
    CHECK(sameRoutes(withMod.routes, factoryRoutes(Mode::Fx)));
}

TEST_CASE("a version 1 instrument state folds Macro 7 into Stereo", "[core]")
{
    Loaded l = distinct();
    REQUIRE(l.load(kM1InstrumentState, Mode::Instrument));
    CHECK(l.migratedFromVersion == 1u);
    CHECK(l.seed == 7u);
    CHECK(sameRoutes(l.routes, factoryRoutes(Mode::Instrument)));

    // Stereo's base takes Macro 7's M1 recipe, bit for bit; Macro 7 (Macro Feedback now) rests
    // at 0; every other value is as saved, or M2's default.
    const float folded = foldedStereo(0.3f, 0.8f);
    CHECK(folded > 0.59f);
    CHECK(folded < 0.61f);
    ParamValues want = defaults(Mode::Instrument);
    want[static_cast<size_t>(ParamSlot::Stereo)] = folded;
    want[static_cast<size_t>(ParamSlot::Macro7)] = 0.0f;
    CHECK(bitsOf(l[ParamSlot::Stereo]) == bitsOf(folded));
    CHECK(bitsOf(l[ParamSlot::Macro7]) == bitsOf(0.0f));
    CHECK(l.valueBits() == Loaded{want, 0, 0}.valueBits());

    // The fold clamps to Stereo's range, 0 to 1.
    const auto withValues = [](std::string_view stereo, std::string_view macro7) {
        std::string s(kM1InstrumentState);
        s = replaceOnce(s, R"(<p id="65543" v="0.300000011921" bits="3e99999a" />)", stereo);
        s = replaceOnce(s, R"(<p id="23" v="0.800000011921" bits="3f4ccccd" />)", macro7);
        return s;
    };
    Loaded high = distinct();
    REQUIRE(high.load(withValues(R"(<p id="65543" v="0.900000000000" />)",
                                 R"(<p id="23" v="1.000000000000" bits="3f800000" />)"),
                      Mode::Instrument));
    CHECK(high[ParamSlot::Stereo] == 1.0f);
    CHECK(high[ParamSlot::Macro7] == 0.0f);
    Loaded low = distinct();
    REQUIRE(low.load(withValues(R"(<p id="65543" v="0.100000000000" />)",
                                R"(<p id="23" v="0.000000000000" bits="00000000" />)"),
                     Mode::Instrument));
    CHECK(low[ParamSlot::Stereo] == 0.0f);

    // A state with no Macro 7 (saved before the Perform UI) folds M1's default for it, 0.5,
    // which leaves Stereo exactly where it was; so does a NaN, which M1 loaded as its default.
    // Out-of-range values are clamped first, as M1 loaded them.
    Loaded absent = distinct();
    REQUIRE(absent.load(withValues(R"(<p id="65543" v="0.300000011921" bits="3e99999a" />)", ""),
                        Mode::Instrument));
    CHECK(bitsOf(absent[ParamSlot::Stereo]) == bitsOf(0.3f));
    CHECK(absent[ParamSlot::Macro7] == 0.0f);
    Loaded nan = distinct();
    REQUIRE(nan.load(withValues(R"(<p id="65543" v="0.300000011921" bits="3e99999a" />)",
                                R"(<p id="23" v="nan" bits="7fc00000" />)"),
                     Mode::Instrument));
    CHECK(bitsOf(nan[ParamSlot::Stereo]) == bitsOf(0.3f));
    CHECK(nan[ParamSlot::Macro7] == 0.0f);
    Loaded over = distinct();
    REQUIRE(over.load(withValues(R"(<p id="65543" v="1.5" />)", R"(<p id="23" v="-3" />)"),
                      Mode::Instrument));
    CHECK(bitsOf(over[ParamSlot::Stereo]) == bitsOf(foldedStereo(1.0f, 0.0f)));

    // At version 2 nothing folds: Macro 7 is Macro Feedback.
    Loaded v2 = distinct();
    REQUIRE(
        v2.load(replaceOnce(std::string(kM1InstrumentState), R"(version="1")", R"(version="2")"),
                Mode::Instrument));
    CHECK(v2.migratedFromVersion == 0u);
    CHECK(bitsOf(v2[ParamSlot::Stereo]) == bitsOf(0.3f));
    CHECK(bitsOf(v2[ParamSlot::Macro7]) == bitsOf(0.8f));
    CHECK(sameRoutes(v2.routes, ModRoutes{}));
}

TEST_CASE("the render after the migration is bit for bit the M1 render", "[core]")
{
    // M1 rendered kM1InstrumentState with its values as saved and its Macro 7's recipe, Macro
    // Stereo's offset 0.5 × (2v − 1) on Stereo (M1's macroOffsets). With Macro 4 at rest that
    // offset was Stereo's whole macro offset, added after the base exactly as a modulation is, so
    // a modulation of it stands in for M1's recipe here. Macro 7 at 0.8 now drives Feedback,
    // which does nothing in the instrument on a Sample layer. The migrated state, with that
    // offset in Stereo's base and Macro 7 at 0, must render the same bytes.
    Loaded migrated = distinct();
    REQUIRE(migrated.load(kM1InstrumentState, Mode::Instrument));
    ParamValues m1 = defaults(Mode::Instrument);
    m1[static_cast<size_t>(ParamSlot::Stereo)] = 0.3f;
    m1[static_cast<size_t>(ParamSlot::Macro7)] = 0.8f;
    const float m1Offset = 0.5f * (2.0f * 0.8f - 1.0f);

    const InstrumentRender after = renderInstrument(migrated.values, 0.0f);
    const InstrumentRender before = renderInstrument(m1, m1Offset);
    const float peak =
        *std::max_element(after.left.begin(), after.left.end(),
                          [](float a, float b) { return std::fabs(a) < std::fabs(b); });
    REQUIRE(std::fabs(peak) > 0.01f);
    CHECK(bitsOf(after.effectiveStereo) == bitsOf(before.effectiveStereo));
    CHECK(bitsOf(after.effectiveStereo) == bitsOf(foldedStereo(0.3f, 0.8f)));
    CHECK(sameSamples(after.left, before.left));
    CHECK(sameSamples(after.right, before.right));

    // Stereo matters to this render: without Macro 7's offset it differs.
    const InstrumentRender unfolded = renderInstrument(m1, 0.0f);
    CHECK_FALSE(
        (sameSamples(unfolded.left, after.left) && sameSamples(unfolded.right, after.right)));
}

TEST_CASE("a malformed route loads as an empty slot", "[core]")
{
    const ModRoutes three = threeRoutes();
    const auto good = toStateXml(defaults(), 9, three);
    const auto withRoute = [&good](const std::string &route) {
        return replaceOnce(good, "</mod>", route + "</mod>");
    };

    // The good route in slot 5 loads (the control), its depth from `bits`, or from `depth` when
    // `bits` is malformed or disagrees.
    ModRoutes withSlot5 = three;
    withSlot5[4] = kSlot5Route;
    {
        Loaded l = distinct();
        REQUIRE(l.load(withRoute(routeXml(slot5Attributes()))));
        CHECK(sameRoutes(l.routes, withSlot5));
        for (const std::string bits : {"zz", "3f800000", "c020000", ""})
        {
            INFO("bits " << bits);
            auto a = slot5Attributes();
            a[6].second = bits;
            REQUIRE(l.load(withRoute(routeXml(a))));
            CHECK(sameRoutes(l.routes, withSlot5));
        }
    }

    // One attribute changed, or left out: that route alone is an empty slot, and the load goes
    // on.
    const std::vector<std::pair<std::string, std::vector<std::string>>> bad{
        {"slot", {"0", "49", "-1", "x", "", "4.5", "1e1", "5x", "4294967301"}},
        {"source", {"Nope", "macro 1", "LFO1", ""}},
        {"sourceLayer", {"E", "b", "all", "1", ""}},
        {"target",
         {"abc", "", "999999", "131074", "65551", "23", "65538x", "-65538", "0x10002",
          "4294967295"}},
        {"targetLayer", {"E", "c", "same", "2", ""}},
        {"depth", {"x", "nan", "inf", "-inf", "", "1e999"}},
        {"curve", {"Nope", "bipolar", "Count", ""}},
    };
    for (const auto &[name, values] : bad)
    {
        const auto attributes = slot5Attributes();
        const auto at = std::find_if(attributes.begin(), attributes.end(),
                                     [&name](const auto &a) { return a.first == name; });
        REQUIRE(at != attributes.end());
        std::vector<std::vector<std::pair<std::string, std::string>>> variants;
        for (const auto &v : values)
        {
            auto a = attributes;
            a[static_cast<size_t>(at - attributes.begin())].second = v;
            variants.push_back(a);
        }
        auto missing = attributes;
        missing.erase(missing.begin() + (at - attributes.begin()));
        variants.push_back(missing);
        for (const auto &a : variants)
        {
            const auto route = routeXml(a);
            INFO("route " << route);
            Loaded l = distinct();
            REQUIRE(l.load(withRoute(route)));
            CHECK(sameRoutes(l.routes, three));
            CHECK(l.migratedFromVersion == 0u);
            CHECK(l.seed == 9u);
        }
    }

    // Slot 48 is the last: it loads.
    {
        auto a = slot5Attributes();
        a[0].second = "48";
        ModRoutes want = three;
        want[47] = kSlot5Route;
        Loaded l = distinct();
        REQUIRE(l.load(withRoute(routeXml(a))));
        CHECK(sameRoutes(l.routes, want));
    }

    // Of two routes naming one slot, the later wins.
    {
        auto a = slot5Attributes();
        a[0].second = "2";
        ModRoutes later = three;
        later[1] = kSlot5Route;
        Loaded l = distinct();
        REQUIRE(l.load(withRoute(routeXml(a))));
        CHECK(sameRoutes(l.routes, later));
        REQUIRE(l.load(replaceOnce(good, "<mod>", "<mod>" + routeXml(a))));
        CHECK(sameRoutes(l.routes, three));
        // A later route that is malformed still names its slot: it leaves it empty.
        a[1].second = "Nope";
        ModRoutes emptied = three;
        emptied[1] = {};
        REQUIRE(l.load(withRoute(routeXml(a))));
        CHECK(sameRoutes(l.routes, emptied));
    }
}

TEST_CASE("a version 3 state with an extra element loads", "[core]")
{
    // A newer version loads through PatchBase's newer-version path (spec §6.10): an element this
    // build doesn't know, unknown attributes on a <route> and unknown elements inside <mod> are
    // ignored, and the rest loads.
    Loaded source = distinct();
    for (size_t i = 0; i < kParamCount; ++i)
    {
        source.values[i] = randomIn(kTable[i].minVal, kTable[i].maxVal, 0x93'0000ull + i);
    }
    source.routes = randomRoutes(1234);
    source.routes[20] = {}; // an empty slot among them
    const auto good = toStateXml(source.values, 77, source.routes);
    auto newer = replaceOnce(good, R"(version="2")", R"(version="3")");
    newer =
        replaceOnce(newer, "</patch>", R"(<layers count="4"><layer id="A" /></layers></patch>)");
    newer = replaceOnce(newer, "<mod>", R"(<mod><lfo rate="2" /><route />)");
    newer = replaceOnce(newer, R"(<route slot="1" )", R"(<route shape="7" slot="1" )");
    newer = replaceOnce(newer, "</mod>", R"(<macro id="9" /></mod>)");

    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        Loaded l = distinct();
        REQUIRE(l.load(newer, mode));
        CHECK(l.migratedFromVersion == 3u);
        CHECK(l.seed == 77u);
        CHECK(l.valueBits() == source.valueBits());
        CHECK(sameRoutes(l.routes, source.routes));
    }
}

TEST_CASE("an empty <mod> is 48 empty slots", "[core]")
{
    const auto good = toStateXml(defaults(), 3, threeRoutes());
    const auto from = good.find("<mod>");
    REQUIRE(from != std::string::npos);
    const auto to = good.find("</mod>");
    REQUIRE(to != std::string::npos);
    const auto modElement = good.substr(from, to + std::strlen("</mod>") - from);
    for (const std::string replacement : {"<mod />", "<mod></mod>", ""})
    {
        INFO("<mod> as '" << replacement << "'");
        Loaded l = distinct();
        REQUIRE(l.load(replaceOnce(good, modElement, replacement)));
        CHECK(l.migratedFromVersion == 0u);
        CHECK(sameRoutes(l.routes, ModRoutes{}));
        CHECK(l.valueBits() == Loaded{defaults(), 0, 0}.valueBits());
        CHECK(l.seed == 3u);
    }
}
