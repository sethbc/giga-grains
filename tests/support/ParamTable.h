// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_TESTS_SUPPORT_PARAMTABLE_H
#define GG_TESTS_SUPPORT_PARAMTABLE_H

// Global Constraints' parameter table, spelled out literally once for the tests that pin it
// (tests/core/patch_tests.cpp through gg-core, tests/plugin/params_tests.cpp through the
// plugins): every parameter's CLAP ID, name, range, default, whether it's stepped and whether it's
// automatable, in slot order, the FX's. The instrument's differs only in every layer's Position
// default and display, every layer's Source default and Macro 8's name; each test carries those
// differences itself. The IDs are frozen at the first public release and append-only (INTENT §18),
// and the order is the AUv2 parameter order. Nothing here is computed from gg/core/ParamIds.h, so a
// change there fails the tests.
//
// Wave-2 sub-projects (M2 programme design §7.1) append their rows here, inside their block.

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <clap/id.h>

#include "gg/core/ParamIds.h"

namespace gg::test
{
/// One row of the table, as it's spelled below.
struct ParamRow
{
    clap_id id;
    const char *name;
    float min;
    float max;
    float def;
    bool stepped;
};

/// One parameter's expected metadata, at its slot in paramTable(). `automatable`: its CLAP flags
/// carry AUTOMATABLE and MODULATABLE, as every parameter's do but kFixedIds'.
struct ExpectedParamRow
{
    clap_id id;
    std::string name;
    float min;
    float max;
    float def;
    bool stepped;
    bool automatable;
};

/// The top of every layer's Position (M2 sub-project 2's spec §6.3): log2(300 × 255 / 16 + 1) / 8,
/// where the Live mapping (16/255)(2^(8v) − 1) gives the longest ring, 300 s.
inline constexpr float kPositionMax = 1.5279342f;

// The 28 M1 slots in slot order, layer A's eleven M1 parameters named with their letter since M2
// sub-project 1 (spec §6.9).
inline constexpr std::array<ParamRow, 28> kM1Rows{{
    {0x000001, "Mix", 0.0f, 1.0f, 0.5f, false},
    {0x000002, "Level", -60.0f, 12.0f, 0.0f, false},
    {0x010001, "A Position", 0.0f, kPositionMax, 0.28967658f, false},
    {0x010002, "A Size", -7.643856189774724f, 1.0f, -2.736965594166206f, false},
    {0x010003, "A Density", -2.0f, 8.643856189774725f, 4.0f, false},
    {0x010004, "A Distribution", 0.0f, 1.0f, 0.5f, false},
    {0x010005, "A Pitch", -24.0f, 24.0f, 0.0f, false},
    {0x010006, "A Spray", 0.0f, 1.0f, 0.36033038f, false},
    {0x010007, "A Stereo", 0.0f, 1.0f, 0.5f, false},
    {0x010008, "A Freeze", 0.0f, 1.0f, 0.0f, true},
    {0x01000B, "A Diffusion", 0.0f, 1.0f, 0.0f, false},
    {0x000003, "Reverb Amount", 0.0f, 1.0f, 0.0f, false},
    {0x000004, "Reverb Decay", -2.0f, 6.068255709804611f, 1.0f, false},
    {0x000005, "Reverb Size", 0.0f, 1.0f, 0.5f, false},
    {0x000006, "Reverb Damping", 0.0f, 1.0f, 0.3f, false},
    {0x000007, "Reverb Pre-delay", 0.0f, 0.5f, 0.0f, false},
    {0x000008, "Reverb Modulation", 0.0f, 1.0f, 0.25f, false},
    {0x000009, "Dry to Reverb", 0.0f, 1.0f, 0.0f, false},
    // The Perform UI (sub-project 5's spec §3.3, §4, §5).
    {0x010009, "A Texture", 0.0f, 1.0f, 0.0f, false},
    {0x01000A, "A Feedback", 0.0f, 0.95f, 0.0f, false},
    {0x000011, "Macro Size", 0.0f, 1.0f, 0.5f, false},
    {0x000012, "Macro Density", 0.0f, 1.0f, 0.5f, false},
    {0x000013, "Macro Pitch", 0.0f, 1.0f, 0.5f, false},
    {0x000014, "Macro Spray", 0.0f, 1.0f, 0.5f, false},
    {0x000015, "Macro Texture", 0.0f, 1.0f, 0.0f, false},
    {0x000016, "Macro Freeze", 0.0f, 1.0f, 0.0f, true},
    {0x000017, "Macro Feedback", 0.0f, 1.0f, 0.0f, false},
    {0x000018, "Macro Mix", 0.0f, 1.0f, 0.5f, false},
}};
// Layer A's new parameters in index order: M2 sub-project 1's 37 (its spec §6.9; rates in log2
// Hz, 0.01 to 50 Hz, envelope times in log2 seconds, 1 ms to 10 s) here, then sub-project 2's five
// (kLayerASourcesRows) and sub-project 3's Quality (kLayerAEngineRows), in their reserved blocks.
inline constexpr float kRateMin = -6.643856189774724f, kRateMax = 5.643856189774724f;
inline constexpr float kTimeMin = -9.965784284662087f, kTimeMax = 3.321928094887362f;
inline constexpr std::array<ParamRow, 37> kLayerANewRows{{
    {0x01000C, "A Source", 0.0f, 4.0f, 1.0f, true},
    {0x01000D, "A Pitch Spread", 0.0f, 24.0f, 0.0f, false},
    {0x01000E, "A Size Spread", 0.0f, 2.0f, 0.0f, false},
    {0x01000F, "A LFO 1 Rate", kRateMin, kRateMax, 0.0f, false},
    {0x010010, "A LFO 1 Shape", 0.0f, 4.0f, 0.0f, true},
    {0x010011, "A LFO 2 Rate", kRateMin, kRateMax, -2.0f, false},
    {0x010012, "A LFO 2 Shape", 0.0f, 4.0f, 1.0f, true},
    {0x010013, "A Env 1 Attack", kTimeMin, kTimeMax, -6.643856189774724f, false},
    {0x010014, "A Env 1 Decay", kTimeMin, kTimeMax, -2.321928094887362f, false},
    {0x010015, "A Env 1 Sustain", 0.0f, 1.0f, 1.0f, false},
    {0x010016, "A Env 1 Release", kTimeMin, kTimeMax, -2.321928094887362f, false},
    {0x010017, "A Env 2 Attack", kTimeMin, kTimeMax, -1.0f, false},
    {0x010018, "A Env 2 Decay", kTimeMin, kTimeMax, 0.0f, false},
    {0x010019, "A Env 2 Sustain", 0.0f, 1.0f, 0.5f, false},
    {0x01001A, "A Env 2 Release", kTimeMin, kTimeMax, 0.0f, false},
    {0x01001B, "A Random 1 Rate", kRateMin, kRateMax, 1.0f, false},
    {0x01001C, "A Random 1 Smooth", 0.0f, 1.0f, 0.0f, false},
    {0x01001D, "A Random 2 Rate", kRateMin, kRateMax, -1.0f, false},
    {0x01001E, "A Random 2 Smooth", 0.0f, 1.0f, 0.5f, false},
    {0x01001F, "A Step Rate", kRateMin, kRateMax, 2.0f, false},
    {0x010020, "A Step Length", 1.0f, 16.0f, 16.0f, true},
    {0x010021, "A Step 1", 0.0f, 1.0f, 0.0f, false},
    {0x010022, "A Step 2", 0.0f, 1.0f, 0.0f, false},
    {0x010023, "A Step 3", 0.0f, 1.0f, 0.0f, false},
    {0x010024, "A Step 4", 0.0f, 1.0f, 0.0f, false},
    {0x010025, "A Step 5", 0.0f, 1.0f, 0.0f, false},
    {0x010026, "A Step 6", 0.0f, 1.0f, 0.0f, false},
    {0x010027, "A Step 7", 0.0f, 1.0f, 0.0f, false},
    {0x010028, "A Step 8", 0.0f, 1.0f, 0.0f, false},
    {0x010029, "A Step 9", 0.0f, 1.0f, 0.0f, false},
    {0x01002A, "A Step 10", 0.0f, 1.0f, 0.0f, false},
    {0x01002B, "A Step 11", 0.0f, 1.0f, 0.0f, false},
    {0x01002C, "A Step 12", 0.0f, 1.0f, 0.0f, false},
    {0x01002D, "A Step 13", 0.0f, 1.0f, 0.0f, false},
    {0x01002E, "A Step 14", 0.0f, 1.0f, 0.0f, false},
    {0x01002F, "A Step 15", 0.0f, 1.0f, 0.0f, false},
    {0x010030, "A Step 16", 0.0f, 1.0f, 0.0f, false},
}};
// Layer A's five ring parameters (M2 sub-project 2's spec §6.3) in index order, in its reserved
// block (0x40 to 0x4F; 0x45 to 0x4F unused): Ring Length and Loop Length in log2 seconds (1 s and
// 50 ms to 300 s), Overdub 0..1 and Splice stepped 0..32.
inline constexpr float kRingLengthMax = 8.228818690495881f;  // log2 300
inline constexpr float kLoopLengthMin = -4.321928094887363f; // log2 0.05
inline constexpr std::array<ParamRow, 5> kLayerASourcesRows{{
    {0x010040, "A Ring Length", 0.0f, kRingLengthMax, 4.0f, false},
    {0x010041, "A Loop", 0.0f, 1.0f, 0.0f, true},
    {0x010042, "A Loop Length", kLoopLengthMin, kRingLengthMax, 0.0f, false},
    {0x010043, "A Overdub", 0.0f, 1.0f, 0.0f, false},
    {0x010044, "A Splice", 0.0f, 32.0f, 0.0f, true},
}};
// Layer A's Quality (M2 sub-project 3, Engine scale and quality; its spec §6.8), in its reserved
// block: 0 Eco, 1 Normal, 2 HQ.
inline constexpr std::array<ParamRow, 1> kLayerAEngineRows{{
    {0x010050, "A Quality", 0.0f, 2.0f, 1.0f, true},
}};
// The parameters that are neither automatable nor modulatable: every layer's Ring Length, which the
// main thread resizes (M2 sub-project 2's spec §5, §6.3).
inline constexpr std::array<clap_id, 4> kFixedIds{0x010040, 0x020040, 0x030040, 0x040040};
// The new globals, the last slots: M2 sub-project 1's nine, then sub-project 3's Budget, in its
// reserved block (its spec §6.4, §6.8), a count of Normal stereo grains.
inline constexpr std::array<ParamRow, 10> kNewGlobalRows{{
    {0x000019, "Morph X", 0.0f, 1.0f, 0.5f, false},
    {0x00001A, "Morph Y", 0.0f, 1.0f, 0.5f, false},
    {0x00001B, "Layer A On", 0.0f, 1.0f, 1.0f, true},
    {0x00001C, "Layer B On", 0.0f, 1.0f, 0.0f, true},
    {0x00001D, "Layer C On", 0.0f, 1.0f, 0.0f, true},
    {0x00001E, "Layer D On", 0.0f, 1.0f, 0.0f, true},
    {0x00001F, "Level B", -60.0f, 12.0f, 0.0f, false},
    {0x000020, "Level C", -60.0f, 12.0f, 0.0f, false},
    {0x000021, "Level D", -60.0f, 12.0f, 0.0f, false},
    {0x000050, "Budget", 256.0f, 16384.0f, 4096.0f, true},
}};

// All 243 in slot order: the rows above, with layers B to D generated from layer A's rows in index
// order (the M1 ones found by ID), each with its letter for "A" and its scope for 1. Built on the
// first call.
inline const std::array<ExpectedParamRow, gg::core::kParamCount> &paramTable()
{
    static const std::array<ExpectedParamRow, gg::core::kParamCount> table = [] {
        // Plain checks, not Catch2's: a test may build it before any test case, at static
        // initialization.
        const auto expect = [](bool ok) {
            if (!ok)
            {
                throw std::logic_error("the expected parameter table is malformed");
            }
        };
        std::array<ExpectedParamRow, gg::core::kParamCount> t{};
        std::size_t slot = 0;
        const auto push = [&t, &slot, &expect](clap_id id, std::string name, const ParamRow &r) {
            expect(slot < gg::core::kParamCount);
            const bool automatable =
                std::find(kFixedIds.begin(), kFixedIds.end(), id) == kFixedIds.end();
            t[slot++] = {id, std::move(name), r.min, r.max, r.def, r.stepped, automatable};
        };
        for (const ParamRow &r : kM1Rows)
        {
            push(r.id, r.name, r);
        }
        for (const ParamRow &r : kLayerANewRows)
        {
            push(r.id, r.name, r);
        }
        for (const ParamRow &r : kLayerASourcesRows)
        {
            push(r.id, r.name, r);
        }
        for (const ParamRow &r : kLayerAEngineRows)
        {
            push(r.id, r.name, r);
        }
        std::vector<ParamRow> layerA;
        for (clap_id index = 0x01; index <= 0x0B; ++index)
        {
            const auto row =
                std::find_if(kM1Rows.begin(), kM1Rows.end(),
                             [index](const ParamRow &r) { return r.id == (0x010000u | index); });
            expect(row != kM1Rows.end());
            layerA.push_back(*row);
        }
        layerA.insert(layerA.end(), kLayerANewRows.begin(), kLayerANewRows.end());
        layerA.insert(layerA.end(), kLayerASourcesRows.begin(), kLayerASourcesRows.end());
        layerA.insert(layerA.end(), kLayerAEngineRows.begin(), kLayerAEngineRows.end());
        expect(layerA.size() == gg::core::kLayerParamCount);
        for (const char letter : {'B', 'C', 'D'})
        {
            const auto scope = static_cast<clap_id>(letter - 'A' + 1);
            for (const ParamRow &r : layerA)
            {
                expect(r.name[0] == 'A' && r.name[1] == ' ');
                push((scope << 16) | (r.id & 0xFFFFu), std::string(1, letter) + (r.name + 1), r);
            }
        }
        for (const ParamRow &r : kNewGlobalRows)
        {
            push(r.id, r.name, r);
        }
        expect(slot == gg::core::kParamCount);
        return t;
    }();
    return table;
}
} // namespace gg::test

#endif // GG_TESTS_SUPPORT_PARAMTABLE_H
