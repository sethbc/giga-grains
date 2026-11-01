// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/Mutate.h"

#include <algorithm>
#include <bit>
#include <cstdint>

#include <clap/ext/params.h>

#include "gg/core/Rng.h"

#include "sst/basic-blocks/params/ParamMetadata.h"

namespace gg::core
{
namespace
{
using md_t = sst::basic_blocks::params::ParamMetaData;

/// The structural locks, the same in both products (D3).
LockSet buildStructuralLocks() noexcept
{
    LockSet l;
    auto lock = [&l](ParamSlot s) { l.set(static_cast<size_t>(s)); };
    lock(ParamSlot::Mix);
    lock(ParamSlot::ReverbAmount);
    lock(ParamSlot::DryToReverb);
    lock(ParamSlot::MorphX);
    lock(ParamSlot::MorphY);
    for (const ParamSlot s : kMacroSlots)
    {
        lock(s);
    }
    for (uint8_t layer = 0; layer < kLayers; ++layer)
    {
        lock(levelSlot(layer));
        lock(layerOnSlot(layer));
        lock(layerSlot(LayerParam::Source, layer));
        lock(layerSlot(LayerParam::Freeze, layer));
        lock(layerSlot(LayerParam::StepLength, layer));
    }
    return l;
}

constexpr size_t kDecaySlot = static_cast<size_t>(ParamSlot::ReverbDecay);
/// The largest float below kDecayInfinite (a positive float's bits, one less).
constexpr float kBelowInfinite = std::bit_cast<float>(std::bit_cast<uint32_t>(kDecayInfinite) - 1u);

/// Slot `s`'s draw for press `counter` of `seed`, on `stream`.
double draw(uint64_t seed, uint32_t stream, uint32_t counter, size_t s) noexcept
{
    return rng::uniform01(rng::hash({seed, kMutateLayer, stream, rng::Field::PositionJitter,
                                     static_cast<uint64_t>(counter) * kParamCount + s}));
}
} // namespace

LockSet structuralLocks(Mode) noexcept
{
    static const LockSet kLocks = buildStructuralLocks();
    return kLocks;
}

LockSet offLayerLocks(const ParamValues &values) noexcept
{
    LockSet l;
    for (uint8_t layer = 0; layer < kLayers; ++layer)
    {
        // Layer On's slot from LayerAOn, not layerOnSlot, whose ParamSlot::Count arm GCC 13
        // reports as -Warray-bounds even behind the loop's bound. NaN counts as off.
        const float on = values[static_cast<size_t>(ParamSlot::LayerAOn) + layer];
        if (!(on >= 0.5f))
        {
            for (size_t p = 0; p < kLayerParamCount; ++p)
            {
                l.set(static_cast<size_t>(layerSlot(static_cast<LayerParam>(p), layer)));
            }
        }
    }
    return l;
}

LockSet defaultMutateLocks(const ParamValues &values, Mode mode) noexcept
{
    return structuralLocks(mode) | offLayerLocks(values);
}

void mutate(const ParamValues &in, const LockSet &locks, float amount, uint64_t seed,
            uint32_t counter, Mode mode, ParamValues &out) noexcept
{
    // Clamped to [0, 1]; NaN fails both comparisons and is 0.
    const float a = amount > 0.0f ? (amount < 1.0f ? amount : 1.0f) : 0.0f;
    for (size_t s = 0; s < kParamCount; ++s)
    {
        const float v = in[s];
        if (locks.test(s))
        {
            out[s] = v;
            continue;
        }
        const md_t &meta = paramMeta(static_cast<ParamSlot>(s), mode);
        // A press is host gestures, recorded as automation: a parameter no host may automate
        // (Ring Length, which the main thread resizes the ring for) is never moved (ruling
        // SP-M8). Its draw is skipped too; every other slot's draws are keyed by its own index,
        // so they are unchanged.
        if ((meta.flags & CLAP_PARAM_IS_AUTOMATABLE) == 0)
        {
            out[s] = v;
            continue;
        }
        const double u = draw(seed, kMutateStream, counter, s);
        if (meta.type == md_t::FLOAT)
        {
            // D4: relative jitter, up to a × range either way. 2u − 1 is exact (rng::bipolar).
            const float range = meta.maxVal - meta.minVal;
            const float jitter = static_cast<float>(2.0 * u - 1.0);
            out[s] = std::clamp(v + a * range * jitter, meta.minVal, meta.maxVal);
            // The director's D-Q3: a press never moves a finite Decay into the infinite zone; it
            // stops at the float just below it. No new draw: the cap follows the law's result.
            // From the zone itself the law applies as it is, in or out.
            if (s == kDecaySlot && v < kDecayInfinite && out[s] >= kDecayInfinite)
            {
                out[s] = kBelowInfinite;
            }
        }
        else if (u < static_cast<double>(a))
        {
            // A stepped parameter: a fresh step, uniformly among its steps.
            const int steps = static_cast<int>(meta.maxVal - meta.minVal) + 1;
            const double pick = draw(seed, kMutatePickStream, counter, s);
            const int step = std::min(static_cast<int>(pick * steps), steps - 1);
            out[s] = std::min(meta.minVal + static_cast<float>(step), meta.maxVal);
        }
        else
        {
            out[s] = v;
        }
    }
}
} // namespace gg::core
