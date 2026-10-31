// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PARAMIDS_H
#define GG_CORE_PARAMIDS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <clap/id.h>

namespace sst::basic_blocks::params
{
struct ParamMetaData;
}

/// The parameters: the slice's ten (spec §4), Space's eight (sub-project 4's spec §6.5), the
/// Perform UI's ten (Texture, Feedback and eight macros; sub-project 5's spec §3.3, §4, §5),
/// M2's layers (M2 sub-project 1's spec §6.9): 48 parameters per layer, A to D, and nine new
/// globals, M2 sub-project 2's five ring parameters per layer (its spec §6.3), and M2 sub-project
/// 3's Quality, per layer, and Budget (its spec §6.8): 54 per layer and 243 in all. Their slots,
/// CLAP IDs and metadata. Both products expose the same list, so
/// they share one state format (INTENT §2); only Position's metadata (sub-project 3's spec §6.8),
/// Source's default and Macro 8's name differ between them, and the instrument's plugin hides Mix
/// and Dry to Reverb, which have no dry signal to act on there.
namespace gg::core
{
/// Which product: a Processor's configuration, and the parameter metadata it shows.
enum class Mode
{
    Instrument, ///< The wet signal alone, from a sample source; no input.
    Fx,         ///< The input feeds the live ring and the dry side of the Mix.
};

/// The 54 parameters of a layer (M2 sub-project 1's spec §6.9, M2 sub-project 2's five ring
/// parameters in its reserved block, its spec §6.3, and M2 sub-project 3's Quality in its reserved
/// block, its spec §6.8), one row each, in index order:
/// `X(name, index, "Display Name")`. A layer parameter's CLAP ID is `(layer + 1) << 16 | index`,
/// the same index in each of the scopes 1 to 4, and it is displayed with its layer's letter ("A
/// Size" ... "D Size"). The enum LayerParam, layers B to D's ParamSlot entries and their IDs are
/// generated from it, so no name is typed four times. Append-only, as the IDs are. A wave-2
/// sub-project (M2 programme design §7.1) appends `X(Name, 0x4N, "Name")`, its index inside its
/// reserved block, at the end of GG_LAYER_PARAMS_M2, or adds a list GG_LAYER_PARAMS_M2_<n> to
/// GG_LAYER_PARAMS and GG_LAYER_PARAMS_SINCE_M1, as sub-project 3's GG_LAYER_PARAMS_M2_ENGINE is,
/// and its index to the reserved-block static_assert below. The index column must stay strictly
/// ascending (validIndexColumn): a list whose block lies below 0x50 goes between
/// GG_LAYER_PARAMS_M2 and GG_LAYER_PARAMS_M2_ENGINE in both macros, not after the latter. Rows stay
/// in index order, and an index left unused stays unused: an index is read from this column
/// (layerParamIndex, slotForId), never from a row's position. The GG_LAYER_PARAMS* macros stay
/// defined on purpose: ParamIds.cpp and tools/gg-render/RenderSpec.cpp expand them too.
#define GG_LAYER_PARAMS_M1(X)                                                                      \
    X(Position, 0x01, "Position")                                                                  \
    X(Size, 0x02, "Size")                                                                          \
    X(Density, 0x03, "Density")                                                                    \
    X(Distribution, 0x04, "Distribution")                                                          \
    X(Pitch, 0x05, "Pitch")                                                                        \
    X(Spray, 0x06, "Spray")                                                                        \
    X(Stereo, 0x07, "Stereo")                                                                      \
    X(Freeze, 0x08, "Freeze")                                                                      \
    X(Texture, 0x09, "Texture")                                                                    \
    X(Feedback, 0x0A, "Feedback")                                                                  \
    X(Diffusion, 0x0B, "Diffusion")
#define GG_LAYER_PARAMS_M2(X)                                                                      \
    X(Source, 0x0C, "Source")                                                                      \
    X(PitchSpread, 0x0D, "Pitch Spread")                                                           \
    X(SizeSpread, 0x0E, "Size Spread")                                                             \
    X(Lfo1Rate, 0x0F, "LFO 1 Rate")                                                                \
    X(Lfo1Shape, 0x10, "LFO 1 Shape")                                                              \
    X(Lfo2Rate, 0x11, "LFO 2 Rate")                                                                \
    X(Lfo2Shape, 0x12, "LFO 2 Shape")                                                              \
    X(Env1Attack, 0x13, "Env 1 Attack")                                                            \
    X(Env1Decay, 0x14, "Env 1 Decay")                                                              \
    X(Env1Sustain, 0x15, "Env 1 Sustain")                                                          \
    X(Env1Release, 0x16, "Env 1 Release")                                                          \
    X(Env2Attack, 0x17, "Env 2 Attack")                                                            \
    X(Env2Decay, 0x18, "Env 2 Decay")                                                              \
    X(Env2Sustain, 0x19, "Env 2 Sustain")                                                          \
    X(Env2Release, 0x1A, "Env 2 Release")                                                          \
    X(Random1Rate, 0x1B, "Random 1 Rate")                                                          \
    X(Random1Smooth, 0x1C, "Random 1 Smooth")                                                      \
    X(Random2Rate, 0x1D, "Random 2 Rate")                                                          \
    X(Random2Smooth, 0x1E, "Random 2 Smooth")                                                      \
    X(StepRate, 0x1F, "Step Rate")                                                                 \
    X(StepLength, 0x20, "Step Length")                                                             \
    X(Step1, 0x21, "Step 1")                                                                       \
    X(Step2, 0x22, "Step 2")                                                                       \
    X(Step3, 0x23, "Step 3")                                                                       \
    X(Step4, 0x24, "Step 4")                                                                       \
    X(Step5, 0x25, "Step 5")                                                                       \
    X(Step6, 0x26, "Step 6")                                                                       \
    X(Step7, 0x27, "Step 7")                                                                       \
    X(Step8, 0x28, "Step 8")                                                                       \
    X(Step9, 0x29, "Step 9")                                                                       \
    X(Step10, 0x2A, "Step 10")                                                                     \
    X(Step11, 0x2B, "Step 11")                                                                     \
    X(Step12, 0x2C, "Step 12")                                                                     \
    X(Step13, 0x2D, "Step 13")                                                                     \
    X(Step14, 0x2E, "Step 14")                                                                     \
    X(Step15, 0x2F, "Step 15")                                                                     \
    X(Step16, 0x30, "Step 16")
/// M2 sub-project 2's (Sources and persistence; its spec §6.3), in its reserved block 0x40-0x4F;
/// 0x45-0x4F stay unused.
#define GG_LAYER_PARAMS_M2_SOURCES(X)                                                              \
    X(RingLength, 0x40, "Ring Length")                                                             \
    X(Loop, 0x41, "Loop")                                                                          \
    X(LoopLength, 0x42, "Loop Length")                                                             \
    X(Overdub, 0x43, "Overdub")                                                                    \
    X(Splice, 0x44, "Splice")
/// M2 sub-project 3's (Engine scale and quality; its spec §6.1, §6.8), in its block 0x50-0x5F: a
/// layer's interpolation tier, stepped 0 Eco, 1 Normal, 2 HQ (gg::Tier).
#define GG_LAYER_PARAMS_M2_ENGINE(X) X(Quality, 0x50, "Quality")
#define GG_LAYER_PARAMS(X)                                                                         \
    GG_LAYER_PARAMS_M1(X)                                                                          \
    GG_LAYER_PARAMS_M2(X) GG_LAYER_PARAMS_M2_SOURCES(X) GG_LAYER_PARAMS_M2_ENGINE(X)
/// The rows after layer A's eleven M1 ones: layer A's new slots, in order.
#define GG_LAYER_PARAMS_SINCE_M1(X)                                                                \
    GG_LAYER_PARAMS_M2(X) GG_LAYER_PARAMS_M2_SOURCES(X) GG_LAYER_PARAMS_M2_ENGINE(X)

/// One layer's parameter, whichever layer: its value is its row's position in GG_LAYER_PARAMS, and
/// its index in the layer's scope is the row's index column (layerParamIndex).
enum class LayerParam : uint8_t
{
#define GG_LAYER_PARAM_ENUM(name, index, display) name,
    GG_LAYER_PARAMS(GG_LAYER_PARAM_ENUM)
#undef GG_LAYER_PARAM_ENUM
    Count
};

/// The layers, A to D: scopes 1 to 4.
inline constexpr uint8_t kLayers = 4;

/// The parameters per layer.
inline constexpr size_t kLayerParamCount = 54;
static_assert(static_cast<size_t>(LayerParam::Count) == kLayerParamCount);

/// The indices a scope can hold: 0x00 (reserved) to 0x7F, the top of wave 2's reserved blocks
/// (kReservedIndexEnd).
inline constexpr uint32_t kScopeIndices = 0x80;

namespace detail
{
/// Each row's index, by its LayerParam value: GG_LAYER_PARAMS's index column.
inline constexpr std::array<uint32_t, kLayerParamCount> kLayerParamIndices{
#define GG_LAYER_PARAM_INDEX(name, index, display) (index),
    GG_LAYER_PARAMS(GG_LAYER_PARAM_INDEX)
#undef GG_LAYER_PARAM_INDEX
};

/// Whether `indices`, a list's index column, can be a layer list: every index in 0x01 to 0x7F,
/// strictly ascending (rows in index order, none repeated).
template <size_t N> constexpr bool validIndexColumn(const std::array<uint32_t, N> &indices) noexcept
{
    for (size_t i = 0; i < N; ++i)
    {
        if (indices[i] == 0 || indices[i] >= kScopeIndices ||
            (i > 0 && indices[i] <= indices[i - 1]))
        {
            return false;
        }
    }
    return true;
}

/// The reverse of an index column: each index 0x00 to 0x7F's row position, or N where no row has
/// that index. `indices` must be a valid column (validIndexColumn).
template <size_t N>
constexpr std::array<size_t, kScopeIndices>
rowsByIndex(const std::array<uint32_t, N> &indices) noexcept
{
    std::array<size_t, kScopeIndices> t{};
    t.fill(N);
    for (size_t i = 0; i < N; ++i)
    {
        t[indices[i] % kScopeIndices] = i;
    }
    return t;
}

static_assert(validIndexColumn(kLayerParamIndices));

/// The layer scopes' rows by index: kLayerParamCount where an index is no row's.
inline constexpr auto kLayerRowsByIndex = rowsByIndex(kLayerParamIndices);
} // namespace detail

/// The parameter's index in its layer's scope, from its row's index column: 0x01 to 0x30 for M2
/// sub-project 1's rows, 0x40 to 0x7F for wave 2's (index 0 is reserved); 0 for
/// LayerParam::Count.
constexpr uint32_t layerParamIndex(LayerParam p) noexcept
{
    const auto i = static_cast<size_t>(p);
    return i < kLayerParamCount ? detail::kLayerParamIndices[i] : 0u;
}

// Each row's ID pin: its LayerParam's index is the one its row names.
#define GG_LAYER_PARAM_INDEX_CHECK(name, index, display)                                           \
    static_assert(layerParamIndex(LayerParam::name) == (index));
GG_LAYER_PARAMS(GG_LAYER_PARAM_INDEX_CHECK)
#undef GG_LAYER_PARAM_INDEX_CHECK

/// A parameter's index in the patch, the processor and the plugin's value store. Slots are the
/// code's names for the parameters; they're never saved. What's saved, and frozen at the first
/// public release, is the CLAP ID (INTENT §18). The slot order is the parameter list's order; the
/// AUv2 order is ascending CLAP ID (INTENT §18, §23 #60), frozen with the IDs: new slots are
/// appended with new IDs.
///
/// The 28 M1 slots come first, in their order (layer A's eleven M1 parameters among them, under
/// their M1 names), then layer A's 43 new parameters (M2 sub-project 1's 37, sub-project 2's five
/// and sub-project 3's Quality), then layers B, C and D in full (`B_Position` ... `D_Quality`, 54
/// each, in row order), then the nine new globals (M2 sub-project 1's spec §6.9) and Budget (M2
/// sub-project 3's spec §6.8). A layer row appended to GG_LAYER_PARAMS moves the later layers'
/// slots, never an ID.
enum class ParamSlot : uint16_t
{
    Mix,
    Level,
    Position,
    Size,
    Density,
    Distribution,
    Pitch,
    Spray,
    Stereo,
    Freeze,
    // Space (sub-project 4's spec §6.5).
    Diffusion,
    ReverbAmount,
    ReverbDecay,
    ReverbSize,
    ReverbDamping,
    ReverbPredelay,
    ReverbModulation,
    DryToReverb,
    // The Perform UI (sub-project 5's spec §3.3, §4, §5).
    Texture,
    Feedback,
    Macro1,
    Macro2,
    Macro3,
    Macro4,
    Macro5,
    Macro6,
    Macro7,
    Macro8,
// Layers (M2 sub-project 1's spec §6.9): layer A's new parameters, then layers B to D in full.
#define GG_LAYER_A_SLOT(name, index, display) name,
#define GG_LAYER_B_SLOT(name, index, display) B_##name,
#define GG_LAYER_C_SLOT(name, index, display) C_##name,
#define GG_LAYER_D_SLOT(name, index, display) D_##name,
    GG_LAYER_PARAMS_SINCE_M1(GG_LAYER_A_SLOT)
    GG_LAYER_PARAMS(GG_LAYER_B_SLOT) GG_LAYER_PARAMS(GG_LAYER_C_SLOT)
        GG_LAYER_PARAMS(GG_LAYER_D_SLOT)
#undef GG_LAYER_A_SLOT
#undef GG_LAYER_B_SLOT
#undef GG_LAYER_C_SLOT
#undef GG_LAYER_D_SLOT
        // The layers' globals (M2 sub-project 1's spec §6.9).
        MorphX,
    MorphY,
    LayerAOn,
    LayerBOn,
    LayerCOn,
    LayerDOn,
    LevelB,
    LevelC,
    LevelD,
    // M2 sub-project 3's (its spec §6.4, §6.8): the count of Normal stereo grains the layers share.
    Budget,
    Count
};

inline constexpr size_t kParamCount = static_cast<size_t>(ParamSlot::Count);

/// One value per parameter, indexed by ParamSlot: the patch's values as the processor, the
/// plugin and the saved state (gg/core/PatchState.h) hold them. Floats end to end.
using ParamValues = std::array<float, kParamCount>;

/// Reverb Decay, in log2 seconds, from which the reverb holds its tail forever: log2 60. Decay's
/// values from here to kDecayMax, the top 2% of its travel, are "∞" (sub-project 4's spec §6.4).
inline constexpr float kDecayInfinite = 5.906890595608519f;

/// The top of Reverb Decay's range, in log2 seconds: kDecayInfinite plus 2% of the travel from
/// its bottom, log2 0.25 s = -2.
inline constexpr float kDecayMax = 6.068255709804611f;

/// The top of Feedback's range: the live ring takes back at most 95% of the limited wet signal
/// (sub-project 5's spec §5).
inline constexpr float kFeedbackMax = 0.95f;

/// The ring's lengths, in seconds (M2 sub-project 2's spec §6.3, D3): Ring Length runs from
/// kRingMinSeconds to kRingMaxSeconds, Loop Length from kLoopMinSeconds to kRingMaxSeconds, each
/// in log2 seconds, and kRingMaxSeconds is also where Position's range ends (kPositionMax).
inline constexpr double kRingMaxSeconds = 300.0, kRingMinSeconds = 1.0, kLoopMinSeconds = 0.05;

/// The most splice marks a ring holds, and the top of Splice's range (spec §6.3): Splice 0 is the
/// whole window, 1 to kMaxSplices a span between marks. The engine's LiveSource::kMaxSplices is
/// the same number.
inline constexpr uint32_t kMaxSplices = 32;

/// The top of every layer's Position, in both products (spec §6.3): log2(kRingMaxSeconds × 255 /
/// 16 + 1) / 8, rounded to the nearest float, where the Live mapping (16/255)(2^(8v) − 1) gives
/// kRingMaxSeconds (299.99994 s). Its values from 0 to 1 map as in M1; a Sample layer's fraction
/// stops at 1.
inline constexpr float kPositionMax = 1.5279342f;

/// The eight macros, in order (sub-project 5's spec §3.2, §3.3).
inline constexpr std::array<ParamSlot, 8> kMacroSlots{
    ParamSlot::Macro1, ParamSlot::Macro2, ParamSlot::Macro3, ParamSlot::Macro4,
    ParamSlot::Macro5, ParamSlot::Macro6, ParamSlot::Macro7, ParamSlot::Macro8,
};

/// Whether `slot` is one of the eight macros.
constexpr bool isMacro(ParamSlot slot) noexcept
{
    return slot >= ParamSlot::Macro1 && slot <= ParamSlot::Macro8;
}

/// The next free index below the reserved blocks in every layer scope (1 to 4) and in scope 0 (M2
/// sub-project 1's spec §6.9): a new parameter outside wave 2's blocks takes it, and a test pins
/// each as one past the table's largest index below kReservedIndexBegin.
inline constexpr uint32_t kNextFreeLayerIndex = 0x31, kNextFreeGlobalIndex = 0x22;

/// The indices reserved in every scope for M2's wave 2 (spec §6.9): 0x40-0x4F for sub-project 2,
/// 0x50-0x5F for sub-project 3 and 0x60-0x7F for sub-project 4. No parameter of sub-project 1
/// takes one; sub-project 2's five take 0x40-0x44 of each layer scope (its spec §6.3), and
/// sub-project 3's Quality and Budget take 0x50 (a static_assert below pins them).
inline constexpr uint32_t kReservedIndexBegin = 0x40, kReservedIndexEnd = 0x7F;

/// A layer's Source kind: what its grains read (M2 sub-project 1's spec §6.9). Layer A's
/// parameter value, 0 to 4.
enum class SourceKind : uint8_t
{
    Sample,
    Live,
    Resample,
    Spectral,
    Oscillator,
};

/// An LFO's shape (spec §6.9), its parameter's value, 0 to 4.
enum class LfoShape : uint8_t
{
    Sine,
    Triangle,
    SawUp,
    SawDown,
    Square,
};

namespace detail
{
/// The first slot of layer A's new parameters, and of layer B's 54.
inline constexpr size_t kLayerANewFirst = static_cast<size_t>(ParamSlot::Source);
inline constexpr size_t kLayerBFirst = static_cast<size_t>(ParamSlot::B_Position);
inline constexpr size_t kLayersEnd = static_cast<size_t>(ParamSlot::MorphX);
static_assert(kLayerANewFirst == 28);
static_assert(kLayerBFirst == kLayerANewFirst + kLayerParamCount - 11);
static_assert(kLayersEnd == kLayerBFirst + 3 * kLayerParamCount);

/// Layer A's eleven M1 parameters keep their M1 slots, which aren't contiguous.
inline constexpr std::array<ParamSlot, 11> kLayerAM1Slots{
#define GG_LAYER_A_M1_SLOT(name, index, display) ParamSlot::name,
    GG_LAYER_PARAMS_M1(GG_LAYER_A_M1_SLOT)
#undef GG_LAYER_A_M1_SLOT
};

/// `id = (scope << 16) | index`: scope 0 is global, scopes 1 to 4 are layers A to D, and index 0
/// is reserved. Every ID is below 0xB00000 (ruling T8-d). Append-only: never renumber, never
/// reuse an ID.
inline constexpr std::array<clap_id, kParamCount> kParamIds{
    0x000001, // Mix
    0x000002, // Level
    0x010001, // Position
    0x010002, // Size
    0x010003, // Density
    0x010004, // Distribution
    0x010005, // Pitch
    0x010006, // Spray
    0x010007, // Stereo
    0x010008, // Freeze
    // Space (sub-project 4's spec §6.5): the next free IDs in each scope, past those sub-project
    // 5's spec had reserved (below).
    0x01000B, // Diffusion
    0x000003, // ReverbAmount
    0x000004, // ReverbDecay
    0x000005, // ReverbSize
    0x000006, // ReverbDamping
    0x000007, // ReverbPredelay
    0x000008, // ReverbModulation
    0x000009, // DryToReverb
    // The Perform UI (sub-project 5's spec §3.3): the IDs its spec reserved.
    0x010009, // Texture
    0x01000A, // Feedback
    0x000011, // Macro1
    0x000012, // Macro2
    0x000013, // Macro3
    0x000014, // Macro4
    0x000015, // Macro5
    0x000016, // Macro6
    0x000017, // Macro7
    0x000018, // Macro8
// Layers (M2 sub-project 1's spec §6.9): layer A's new parameters in scope 1, then layers B to D
// in scopes 2 to 4, at scope 1's indices.
#define GG_LAYER_A_ID(name, index, display) 0x010000u | (index),
#define GG_LAYER_B_ID(name, index, display) 0x020000u | (index),
#define GG_LAYER_C_ID(name, index, display) 0x030000u | (index),
#define GG_LAYER_D_ID(name, index, display) 0x040000u | (index),
    GG_LAYER_PARAMS_SINCE_M1(GG_LAYER_A_ID) GG_LAYER_PARAMS(GG_LAYER_B_ID)
        GG_LAYER_PARAMS(GG_LAYER_C_ID) GG_LAYER_PARAMS(GG_LAYER_D_ID)
#undef GG_LAYER_A_ID
#undef GG_LAYER_B_ID
#undef GG_LAYER_C_ID
#undef GG_LAYER_D_ID
    // The layers' globals (spec §6.9).
    0x000019, // MorphX
    0x00001A, // MorphY
    0x00001B, // LayerAOn
    0x00001C, // LayerBOn
    0x00001D, // LayerCOn
    0x00001E, // LayerDOn
    0x00001F, // LevelB
    0x000020, // LevelC
    0x000021, // LevelD
    // M2 sub-project 3's spec §6.8: in its reserved block.
    0x000050, // Budget
};

/// Scope 0's slots by index, 0x00 to 0x7F, so that wave 2's reserved blocks work as in the layer
/// scopes; ParamSlot::Count where an index isn't one.
inline constexpr auto kGlobalSlots = [] {
    std::array<ParamSlot, kScopeIndices> t{};
    t.fill(ParamSlot::Count);
    for (size_t i = 0; i < kParamCount; ++i)
    {
        if ((kParamIds[i] >> 16) == 0)
        {
            t.at(kParamIds[i] & 0xFFFFu) = static_cast<ParamSlot>(i);
        }
    }
    return t;
}();

// The reserved blocks' indices in use: 0x40-0x44, M2 sub-project 2's, in the layer scopes (Ring
// Length, Loop, Loop Length, Overdub, Splice), and 0x50, M2 sub-project 3's, in every scope
// (Quality in the layer scopes, Budget in scope 0). A wave-2 row adds its index here.
static_assert([] {
    for (const clap_id id : kParamIds)
    {
        const clap_id scope = id >> 16;
        const uint32_t index = id & 0xFFFFu;
        const bool sources = scope >= 1 && index >= 0x40u && index <= 0x44u;
        if (index >= kReservedIndexBegin && index <= kReservedIndexEnd && index != 0x50u &&
            !sources)
        {
            return false;
        }
    }
    for (clap_id scope = 0; scope <= kLayers; ++scope)
    {
        for (uint32_t index = 0x40u; index <= 0x50u; ++index)
        {
            if ((index > 0x44u && index < 0x50u) || (scope == 0 && index != 0x50u))
            {
                continue;
            }
            bool used = false;
            for (const clap_id id : kParamIds)
            {
                used = used || id == (scope << 16 | index);
            }
            if (!used)
            {
                return false;
            }
        }
    }
    return true;
}());
} // namespace detail

/// The CLAP ID of a slot; CLAP_INVALID_ID for ParamSlot::Count.
constexpr clap_id paramId(ParamSlot slot) noexcept
{
    const auto i = static_cast<size_t>(slot);
    return i < kParamCount ? detail::kParamIds[i] : CLAP_INVALID_ID;
}

/// Layer `layer`'s (0 to 3, A to D) slot of `p`; ParamSlot::Count for a layer or parameter out of
/// range.
constexpr ParamSlot layerSlot(LayerParam p, uint8_t layer) noexcept
{
    const auto i = static_cast<size_t>(p);
    if (i >= kLayerParamCount || layer >= kLayers)
    {
        return ParamSlot::Count;
    }
    if (layer == 0)
    {
        return i < detail::kLayerAM1Slots.size()
                   ? detail::kLayerAM1Slots[i]
                   : static_cast<ParamSlot>(detail::kLayerANewFirst + i -
                                            detail::kLayerAM1Slots.size());
    }
    return static_cast<ParamSlot>(detail::kLayerBFirst + (layer - 1u) * kLayerParamCount + i);
}

/// A layer parameter's slot, as (parameter, layer 0 to 3).
struct LayerRef
{
    LayerParam param;
    uint8_t layer;

    friend constexpr bool operator==(const LayerRef &, const LayerRef &) = default;
};

/// The (parameter, layer) of a layer parameter's slot; nothing for a global (Level B to D and the
/// Layer On switches included: see perLayerGlobalLayer) or ParamSlot::Count.
constexpr std::optional<LayerRef> layerOf(ParamSlot slot) noexcept
{
    const auto s = static_cast<size_t>(slot);
    if (s < detail::kLayerANewFirst)
    {
        for (size_t i = 0; i < detail::kLayerAM1Slots.size(); ++i)
        {
            if (detail::kLayerAM1Slots[i] == slot)
            {
                return LayerRef{static_cast<LayerParam>(i), 0};
            }
        }
        return std::nullopt;
    }
    if (s < detail::kLayerBFirst)
    {
        return LayerRef{
            static_cast<LayerParam>(detail::kLayerAM1Slots.size() + s - detail::kLayerANewFirst),
            0};
    }
    if (s < detail::kLayersEnd)
    {
        const size_t k = s - detail::kLayerBFirst;
        return LayerRef{static_cast<LayerParam>(k % kLayerParamCount),
                        static_cast<uint8_t>(1 + k / kLayerParamCount)};
    }
    return std::nullopt;
}

/// Layer `layer`'s Level (0 to 3: Level, Level B, Level C, Level D); ParamSlot::Count out of
/// range.
constexpr ParamSlot levelSlot(uint8_t layer) noexcept
{
    if (layer >= kLayers)
    {
        return ParamSlot::Count;
    }
    return layer == 0 ? ParamSlot::Level
                      : static_cast<ParamSlot>(static_cast<size_t>(ParamSlot::LevelB) + layer - 1u);
}

/// Layer `layer`'s on/off switch (0 to 3: Layer A On to Layer D On); ParamSlot::Count out of
/// range.
constexpr ParamSlot layerOnSlot(uint8_t layer) noexcept
{
    return layer < kLayers
               ? static_cast<ParamSlot>(static_cast<size_t>(ParamSlot::LayerAOn) + layer)
               : ParamSlot::Count;
}

/// The layer (0 to 3) of a per-layer global, a Level or a Layer On switch; nothing for any other
/// slot.
constexpr std::optional<uint8_t> perLayerGlobalLayer(ParamSlot slot) noexcept
{
    for (uint8_t layer = 0; layer < kLayers; ++layer)
    {
        if (slot == levelSlot(layer) || slot == layerOnSlot(layer))
        {
            return layer;
        }
    }
    return std::nullopt;
}

/// The slot with this CLAP ID, if it's one of ours: a table by index in each scope, the layer
/// scopes' from GG_LAYER_PARAMS's index column, so an index no row has (a gap, a reserved block's
/// unused index) is nothing.
constexpr std::optional<ParamSlot> slotForId(clap_id id) noexcept
{
    const uint32_t scope = id >> 16;
    const uint32_t index = id & 0xFFFFu;
    if (scope >= 1 && scope <= kLayers)
    {
        if (index < kScopeIndices && detail::kLayerRowsByIndex[index] < kLayerParamCount)
        {
            return layerSlot(static_cast<LayerParam>(detail::kLayerRowsByIndex[index]),
                             static_cast<uint8_t>(scope - 1));
        }
        return std::nullopt;
    }
    if (scope == 0 && index < detail::kGlobalSlots.size() &&
        detail::kGlobalSlots[index] != ParamSlot::Count)
    {
        return detail::kGlobalSlots[index];
    }
    return std::nullopt;
}

// Every slot's ID leads back to it, and each layer slot's (parameter, layer) to it.
static_assert([] {
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto slot = static_cast<ParamSlot>(i);
        if (slotForId(paramId(slot)) != slot)
        {
            return false;
        }
        if (const auto ref = layerOf(slot);
            ref && (layerSlot(ref->param, ref->layer) != slot ||
                    paramId(slot) != ((ref->layer + 1u) << 16 | layerParamIndex(ref->param))))
        {
            return false;
        }
    }
    return true;
}());

/// The CLAP module of a slot's parameter, for hosts that show paths: "Layer A" to "Layer D" for a
/// layer parameter, "" for a global (spec §6.9).
const char *paramModule(ParamSlot slot) noexcept;

/// The slot's metadata in `mode`'s product: ID, name, range, default, CLAP flags (AUTOMATABLE and
/// MODULATABLE on all but every layer's Ring Length, which has neither: the main thread resizes the
/// ring, M2 sub-project 2's spec §6.3; STEPPED on the switches, the stepped choices and the count:
/// Freeze, Macro 6, Source, the LFO shapes, Step Length, Loop, Splice, the Layer On switches,
/// Quality and Budget), and the display text and text parsing.
/// A layer parameter's name carries its layer's letter ("A Size"). The ranges and defaults are
/// literal constants (INTENT §14), never computed at run time. Reverb Decay shows "∞" from
/// kDecayInfinite up, and "∞" parses to kDecayMax. The plugin adds the HIDDEN flag itself (see the
/// namespace's note).
///
/// Three parameters differ between the products, never in ID or range, so the state format is
/// shared (spec §6.9):
/// - Position, in its default and display, in every layer: the FX's is 0–300 s behind the write
///   head, 0.25 s by default; the instrument's is 0–100% of the loaded file, 0% by default, and
///   "100%" above 1. Both are CLAP values 0 to kPositionMax (M2 sub-project 2's spec §6.3).
/// - Source, in its default, in every layer: Live in the FX, Sample in the instrument.
/// - Macro 8: "Macro Mix" in the FX, "Macro Level" in the instrument; 0.5 in both.
/// Macro 7 is "Macro Feedback", 0 by default, in both.
///
/// Static storage: both products' tables are built on the first call, which allocates, and every
/// later call returns the same objects. Make the first call off the audio thread; saving or
/// loading a state does. `slot` must be a parameter: ParamSlot::Count asserts, and in release
/// builds returns the last parameter's metadata rather than read out of bounds.
const sst::basic_blocks::params::ParamMetaData &paramMeta(ParamSlot slot, Mode mode);
} // namespace gg::core

#endif // GG_CORE_PARAMIDS_H
