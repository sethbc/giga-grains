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

/// The parameters: the slice's ten (spec §4), Space's eight (sub-project 4's spec §6.5) and the
/// Perform UI's ten (Texture, Feedback and eight macros; sub-project 5's spec §3.3, §4, §5), their
/// slots, CLAP IDs and metadata. Both products expose the same list, so they share one state format
/// (INTENT §2); only Position's metadata (sub-project 3's spec §6.8) and the last two macros' names
/// and Macro 7's default (sub-project 5's spec §3.3) differ between them, and the instrument's
/// plugin hides Mix, Dry to Reverb and Feedback, which have no dry signal or live ring to act on
/// there.
namespace gg::core
{
/// Which product: a Processor's configuration, and the parameter metadata it shows.
enum class Mode
{
    Instrument, ///< The wet signal alone, from a sample source; no input.
    Fx,         ///< The input feeds the live ring and the dry side of the Mix.
};

/// A parameter's index in the patch, the processor and the plugin's value store. Slots are the
/// code's names for the parameters; they're never saved. What's saved, and frozen at the first
/// public release, is the CLAP ID (INTENT §18). The slot order is the parameter list's order; the
/// AUv2 order is ascending CLAP ID (INTENT §18, §23 #60), frozen with the IDs: new slots are
/// appended with new IDs.
enum class ParamSlot : uint8_t
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

namespace detail
{
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
};
} // namespace detail

/// The CLAP ID of a slot; CLAP_INVALID_ID for ParamSlot::Count.
constexpr clap_id paramId(ParamSlot slot) noexcept
{
    const auto i = static_cast<size_t>(slot);
    return i < kParamCount ? detail::kParamIds[i] : CLAP_INVALID_ID;
}

/// The slot with this CLAP ID, if it's one of ours.
constexpr std::optional<ParamSlot> slotForId(clap_id id) noexcept
{
    for (size_t i = 0; i < kParamCount; ++i)
    {
        if (detail::kParamIds[i] == id)
        {
            return static_cast<ParamSlot>(i);
        }
    }
    return std::nullopt;
}

/// The slot's metadata in `mode`'s product: ID, name, range, default, CLAP flags (AUTOMATABLE and
/// MODULATABLE on all, STEPPED on Freeze and Macro 6), and the display text and text parsing. The
/// ranges and defaults are literal constants (INTENT §14), never computed at run time. Reverb Decay
/// shows "∞" from kDecayInfinite up, and "∞" parses to kDecayMax. The plugin adds the HIDDEN flag
/// itself (see the namespace's note).
///
/// Three parameters differ between the products, never in ID or range, so the state format is
/// shared:
/// - Position, in its default and display: the FX's is 0–16 s behind the write head, 0.25 s by
///   default; the instrument's is 0–100% of the loaded file, 0% by default. Both are CLAP values
///   0–1.
/// - Macro 7: "Macro Feedback", 0 by default, in the FX; "Macro Stereo", 0.5, in the instrument.
/// - Macro 8: "Macro Mix" in the FX, "Macro Level" in the instrument; 0.5 in both.
///
/// Static storage: both products' tables are built on the first call, which allocates, and every
/// later call returns the same objects. Make the first call off the audio thread; saving or
/// loading a state does. `slot` must be a parameter: ParamSlot::Count asserts, and in release
/// builds returns the last parameter's metadata rather than read out of bounds.
const sst::basic_blocks::params::ParamMetaData &paramMeta(ParamSlot slot, Mode mode);
} // namespace gg::core

#endif // GG_CORE_PARAMIDS_H
