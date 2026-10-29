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

/// The parameters: the slice's ten (spec §4) and Space's eight (sub-project 4's spec §6.5), their
/// slots, CLAP IDs and metadata. Both products expose the same list, so they share one state format
/// (INTENT §2); only Position's metadata differs between them (sub-project 3's spec §6.8), and the
/// instrument's plugin hides Mix and Dry to Reverb, which have no dry signal to act on there.
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
/// public release, is the CLAP ID (INTENT §18). The slot order is the parameter list's order, and
/// so the AUv2 parameter order, also frozen at the first public release: new slots are appended.
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
    // Space (sub-project 4's spec §6.5): the next free IDs in each scope. Sub-project 5's spec
    // reserves 0x000011-0x000018 (the macros), 0x010009 (Texture) and 0x01000A (Feedback).
    0x01000B, // Diffusion
    0x000003, // ReverbAmount
    0x000004, // ReverbDecay
    0x000005, // ReverbSize
    0x000006, // ReverbDamping
    0x000007, // ReverbPredelay
    0x000008, // ReverbModulation
    0x000009, // DryToReverb
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
/// MODULATABLE on all, STEPPED on Freeze), and the display text and text parsing. The ranges and
/// defaults are literal constants (INTENT §14), never computed at run time. Reverb Decay shows
/// "∞" from kDecayInfinite up, and "∞" parses to kDecayMax. The plugin adds the HIDDEN flag
/// itself (see the namespace's note).
///
/// Only Position differs between the products, and only in its default and display: the FX's is
/// 0–16 s behind the write head, 0.25 s by default; the instrument's is 0–100% of the loaded
/// file, 0% by default. Both are CLAP values 0–1 with the same ID, so the state format is shared.
///
/// Static storage: both products' tables are built on the first call, which allocates, and every
/// later call returns the same objects. Make the first call off the audio thread; saving or
/// loading a state does. `slot` must be a parameter: ParamSlot::Count asserts, and in release
/// builds returns the last parameter's metadata rather than read out of bounds.
const sst::basic_blocks::params::ParamMetaData &paramMeta(ParamSlot slot, Mode mode);
} // namespace gg::core

#endif // GG_CORE_PARAMIDS_H
