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

/// The slice's ten parameters (spec §4): their slots, CLAP IDs and metadata. Both products expose
/// the same list, so they share one state format (INTENT §2).
namespace gg::core
{
/// A parameter's index in the patch, the processor and the plugin's value store. Slots are the
/// code's names for the parameters; they're never saved. What's saved, and frozen at the first
/// public release, is the CLAP ID (INTENT §18).
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
    Count
};

inline constexpr size_t kParamCount = static_cast<size_t>(ParamSlot::Count);

/// One value per parameter, indexed by ParamSlot: the patch's values as the processor, the
/// plugin and the saved state (gg/core/PatchState.h) hold them. Floats end to end.
using ParamValues = std::array<float, kParamCount>;

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

/// The slot's metadata: ID, name, range, default, CLAP flags (AUTOMATABLE and MODULATABLE on
/// all, STEPPED on Freeze), and the display text and text parsing. The ranges and defaults are
/// literal constants (INTENT §14), never computed at run time.
///
/// Static storage: the table is built on the first call, which allocates, and every later call
/// returns the same objects. Make the first call off the audio thread; saving or loading a state
/// does. `slot` must be a parameter: ParamSlot::Count asserts, and in release builds returns the
/// last parameter's metadata rather than read out of bounds.
const sst::basic_blocks::params::ParamMetaData &paramMeta(ParamSlot slot);
} // namespace gg::core

#endif // GG_CORE_PARAMIDS_H
