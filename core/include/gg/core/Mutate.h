// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_MUTATE_H
#define GG_CORE_MUTATE_H

#include <bitset>
#include <cstdint>

#include "gg/core/ParamIds.h"

/// Mutate (M2 sub-project 5's spec §6.9; the director's D3 and D4, option 1 each): a parameter
/// move, deterministic from the seed and a press counter. Every unlocked parameter moves in its
/// CLAP value domain by relative jitter, so a log-domain parameter (Size, Density, the rates)
/// moves in octaves; the result is clamped to the parameter's range. The plugin applies the
/// result on the main thread as host gestures. Mutate never touches the mod routes, the sample,
/// the seed or the locks.
///
/// Pure: plain float arithmetic, no libm, no allocation once paramMeta's tables exist (its first
/// call builds them: make it before relying on that).
namespace gg::core
{
/// One lock per slot: a set bit keeps that parameter where it is. The plugin keeps the set for its
/// life; it isn't state. The Mutate panel shows a layer's 16 Step values as one row, so they lock
/// and unlock together there, though the set itself is per slot.
using LockSet = std::bitset<kParamCount>;

/// The counter RNG's layer and stream for Mutate's draws: a layer no engine uses, so Mutate needs
/// no rng::Field of its own and its draws are a key apart from every grain's. Each slot's first
/// draw is keyed (seed, kMutateLayer, kMutateStream, Field::PositionJitter, counter × kParamCount
/// + slot). A stepped slot's second draw, the step it picks, has the same key but for its stream,
/// kMutatePickStream.
inline constexpr uint32_t kMutateLayer = 0xFFFFFFFFu;
inline constexpr uint32_t kMutateStream = 0xFFFFFFFFu;
inline constexpr uint32_t kMutatePickStream = 0xFFFFFFFEu;

/// D3's fixed set, "the structure stays, the sound moves": Mix, Level and Level B to D, Layer A to
/// D On, every layer's Source, Freeze and Step Length, the eight macros, Morph X and Y, Reverb
/// Amount and Dry to Reverb. The same in both products (the instrument's hidden Mix and Dry to
/// Reverb never move).
LockSet structuralLocks(Mode mode) noexcept;

/// Every one of the 48 parameters of each layer whose Layer On value is below 0.5 (or NaN).
LockSet offLayerLocks(const ParamValues &values) noexcept;

/// The default locks for `values`: structuralLocks(mode) | offLayerLocks(values). Everything else
/// moves: each on layer's other parameters and Space's Decay, Size, Damping, Pre-delay and
/// Modulation.
LockSet defaultMutateLocks(const ParamValues &values, Mode mode) noexcept;

/// `out` = `in` mutated by `amount` (clamped to [0, 1]; NaN is 0), press `counter` of `seed`. For
/// each unlocked slot s, with u the first draw (above):
/// - a continuous parameter (ParamMetaData::FLOAT): out = clamp(in + amount × (max − min) × (2u −
///   1), min, max); Reverb Decay below kDecayInfinite then stops at the float just below it, so
///   a press never makes a finite reverb infinite (the director's D-Q3), while from the infinite
///   zone the law applies as it is;
/// - a stepped one (INT or BOOL, max − min + 1 steps, a BOOL's 2): if u < amount, the step min +
///   floor(u' × steps), u' the second draw, clamped to max (it may be the step it was on); else
///   unchanged.
/// A locked slot is copied. `out` may be `in`: each slot reads only its own value.
void mutate(const ParamValues &in, const LockSet &locks, float amount, uint64_t seed,
            uint32_t counter, Mode mode, ParamValues &out) noexcept;
} // namespace gg::core

#endif // GG_CORE_MUTATE_H
