// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_RNG_H
#define GG_CORE_RNG_H

#include <cstdint>

#include "gg/core/FpContract.h"

/// The engine's only source of randomness (INTENT §14): a stateless, counter-based generator.
/// Every random value is a pure function of a Key, so it doesn't depend on block size, call
/// order or thread. The constants and the mixing order below are part of the determinism
/// contract; changing any of them changes every render.
namespace gg::rng
{
/// One SplitMix64 step from state x (Steele, Lea & Flood 2014), used as a 64-bit mixer.
constexpr uint64_t splitmix64(uint64_t x) noexcept
{
    uint64_t z = x + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// Which random quantity a draw is for. The values are part of the determinism contract:
/// append only, never renumber.
enum class Field : uint32_t
{
    PositionJitter = 1,
    Onset = 2,       ///< the Cloud scheduler's interval to the next grain onset
    Pan = 3,         ///< a grain's stereo pan
    Note = 4,        ///< which held key a grain plays (spec §6.1)
    PitchSpread = 5, ///< a grain's pitch spread, bipolar (M2 spec §6.6)
    SizeSpread = 6,  ///< a grain's size spread, bipolar (M2 spec §6.6)
    ModRandom1 = 7,  ///< the first random modulation source (M2 spec §6.8)
    ModRandom2 = 8,  ///< the second random modulation source (M2 spec §6.8)
    SignalNoise = 100,
};

/// Identifies one random draw.
struct Key
{
    uint64_t seed;
    uint32_t layer;
    uint32_t stream;
    Field field;
    uint64_t index;
};

/// Mixes the Key's members in one at a time, in a fixed order, so the same number in a different
/// member gives a different hash.
constexpr uint64_t hash(const Key &k) noexcept
{
    uint64_t h = splitmix64(k.seed);
    h = splitmix64(h ^ k.layer);
    h = splitmix64(h ^ k.stream);
    h = splitmix64(h ^ static_cast<uint32_t>(k.field));
    h = splitmix64(h ^ k.index);
    return h;
}

/// The top 53 bits of h as a double in [0, 1). Exact: every result is a multiple of 2^-53.
constexpr double uniform01(uint64_t h) noexcept { return static_cast<double>(h >> 11) * 0x1.0p-53; }

/// uniform01 mapped to [-1, 1). Exact, like uniform01: 2u is exact (a power-of-two scaling), and
/// 2u − 1, a multiple of 2^-52 in [-1, 1), is representable, so the subtraction can't round
/// either. A fused multiply-add would give the same bits, but the guard keeps it from being
/// formed at all, as in every header whose arithmetic must match across platforms.
constexpr double bipolar(uint64_t h) noexcept
{
    GG_FP_CONTRACT_OFF
    return 2.0 * uniform01(h) - 1.0;
}
} // namespace gg::rng

#endif // GG_CORE_RNG_H
