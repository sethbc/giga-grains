// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_VOICEFILTER_H
#define GG_GRAIN_VOICEFILTER_H

#include <cstdint>

namespace gg
{
/// The voice filter's type (M3 sub-project 9's spec FB8): BusFilterType's values, which gg-core
/// pins with a static_assert. Off is no filter.
enum class VoiceFilterType : uint8_t
{
    Off,
    LowPass,
    HighPass,
    BandPass,
    Notch,
};

/// A lane's state (FB6): Free; Owned by a spawning voice; Draining, its voice finished or
/// replaced, its grains or its filter's ring-out remaining.
enum class LaneState : uint8_t
{
    Free,
    Owned,
    Draining,
};

/// Lanes a layer (F2): twice the voices, so a new voice always finds a Free lane or a Draining
/// one to merge with.
inline constexpr uint32_t kVoiceLanes = 32;

/// A lane's scratch length in frames: the control grid (Processor::kControlGrid), since the
/// engine's blocks never cross a grid sample while the filter is engaged (FB3).
inline constexpr uint32_t kLaneFrames = 32;

/// A Draining lane's filter state counts as rung out below this magnitude, about -120 dB (F9).
inline constexpr float kLaneFreeLevel = 0x1.0p-20f;

/// A Draining lane is freed at the latest this long past its last grain's end (FB17).
inline constexpr double kLaneCapSeconds = 4.0;
} // namespace gg

#endif // GG_GRAIN_VOICEFILTER_H
