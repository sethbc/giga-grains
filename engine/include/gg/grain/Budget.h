// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_BUDGET_H
#define GG_GRAIN_BUDGET_H

#include <cstdint>
#include <limits>

#include "gg/grain/Tier.h"

namespace gg
{
/// What each sounding grain costs per output sample, in integer weight units, by its tier and
/// whether it reads two channels (M2 engine spec §6.4): kGrainWeights[tier][stereo], index 0 mono,
/// 1 stereo (a stereo or wider sample, or the live ring). Normal stereo, X's case, is the unit of
/// 4. Provisional (M5 calibrates them), but they never change at run time. HQ's row is Normal's
/// times r, rounded, with r the median of three `gg-bench --quick` runs' engine-sample-hq over
/// engine-sample, median ns per grain-sample at block 256, on an Apple M5 Max (2026-10-30, ruling
/// ES-T8b): 6.7856, 6.6045 and 7.4324, so r = 6.7856 and {round(3r), round(4r)} = {20, 27}.
inline constexpr uint8_t kGrainWeights[3][2] = {
    {2, 3},   // Eco
    {3, 4},   // Normal
    {20, 27}, // HQ
};

/// One "grain" of Budget: a Budget of N grains is 4N weight units.
inline constexpr uint32_t kNormalStereoWeight = 4;

/// The quota of an engine that has none set: nothing is ever lowered, stolen or dropped for
/// weight (Engine::setQuota).
inline constexpr uint64_t kUnlimitedQuota = std::numeric_limits<uint64_t>::max();

/// The fade a stolen grain is given, in output samples (spec §6.5): with more than this left it
/// ends at onset + kStealFade, fading with the raised cosine (ruling EF-8; GrainKernels.h's
/// stealFaded); with at most this left it keeps its end and doesn't fade (ruling ES-T6a;
/// GrainPool::steal).
inline constexpr int64_t kStealFade = 256;

/// The fade a grain cut at its sample's edge is given, in output samples (carry-over row 300,
/// rulings EF-2 and EF-6; GrainPool::spawn): a raised cosine over this many samples before the
/// edge, or over all it has when that is fewer. Its own constant beside kStealFade: the shortest
/// of 256, 512 and 1024 at which every measured case (rates 0.5 to 4, every tier, every edge
/// phase, short spans, gains 1 and 4) scores at most 12 on the goldens' click detector; at 256
/// the worst was 2.81.
inline constexpr int64_t kEdgeFade = 256;

/// A grain may be stolen once it has sounded this fraction of its durationSamples (spec §6.5,
/// D4): until then a draw that finds no room drops.
inline constexpr double kStealThreshold = 0.5;

/// The largest EngineConfig::grainCapacity (spec §6.6).
inline constexpr uint32_t kMaxGrainCapacity = 16384;

/// kGrainWeights[t][stereo]. A tier outside the enum, which nothing makes, weighs as Normal, as
/// the renderer reads it.
constexpr uint32_t grainWeight(Tier t, bool stereo) noexcept
{
    const auto tier = static_cast<uint8_t>(t);
    return kGrainWeights[tier < 3 ? tier : 1][stereo ? 1 : 0];
}

static_assert(grainWeight(Tier::Normal, true) == kNormalStereoWeight);
} // namespace gg

#endif // GG_GRAIN_BUDGET_H
