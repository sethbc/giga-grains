// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Mutate's model (M2 sub-project 5's spec §6.9; D3 and D4, option 1 each): relative jitter in each
// parameter's CLAP domain, a fresh step for the stepped ones, the default locks, and determinism
// from (seed, counter).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Mutate.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Rng.h"

#include "support/AllocTripwire.h"

#include "sst/basic-blocks/params/ParamMetadata.h"

using gg::core::defaultMutateLocks;
using gg::core::kLayerParamCount;
using gg::core::kLayers;
using gg::core::kMacroSlots;
using gg::core::kParamCount;
using gg::core::layerOf;
using gg::core::layerOnSlot;
using gg::core::LayerParam;
using gg::core::layerSlot;
using gg::core::LockSet;
using gg::core::Mode;
using gg::core::mutate;
using gg::core::offLayerLocks;
using gg::core::paramMeta;
using gg::core::ParamSlot;
using gg::core::ParamValues;
using gg::core::structuralLocks;
using gg::test::AllocTripwire;
using md_t = sst::basic_blocks::params::ParamMetaData;

namespace
{
constexpr size_t idx(ParamSlot s) { return static_cast<size_t>(s); }

constexpr uint64_t kSeed = 0x5EED'0000'1234'ABCDull;

ParamValues defaults(Mode mode)
{
    ParamValues v{};
    for (size_t s = 0; s < kParamCount; ++s)
    {
        v[s] = paramMeta(static_cast<ParamSlot>(s), mode).defaultVal;
    }
    return v;
}

bool sameBytes(const ParamValues &a, const ParamValues &b)
{
    return std::memcmp(a.data(), b.data(), sizeof(float) * kParamCount) == 0;
}

bool stepped(ParamSlot s, Mode mode) { return paramMeta(s, mode).type != md_t::FLOAT; }

ParamValues mutated(const ParamValues &in, const LockSet &locks, float amount, uint64_t seed,
                    uint32_t counter, Mode mode)
{
    ParamValues out{};
    mutate(in, locks, amount, seed, counter, mode, out);
    return out;
}
} // namespace

TEST_CASE("mutate gives the same bytes from the same inputs", "[core][mutate]")
{
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        const ParamValues in = defaults(mode);
        const LockSet locks = defaultMutateLocks(in, mode);
        for (uint32_t counter : {0u, 1u, 77u, 0xFFFFFFFFu})
        {
            const ParamValues a = mutated(in, locks, 0.4f, kSeed, counter, mode);
            const ParamValues b = mutated(in, locks, 0.4f, kSeed, counter, mode);
            CHECK(sameBytes(a, b));
            CHECK_FALSE(sameBytes(a, in));
        }
    }
}

TEST_CASE("mutate differs with the counter and the seed", "[core][mutate]")
{
    const ParamValues in = defaults(Mode::Fx);
    const LockSet locks = defaultMutateLocks(in, Mode::Fx);
    const ParamValues base = mutated(in, locks, 0.3f, kSeed, 5, Mode::Fx);
    CHECK_FALSE(sameBytes(base, mutated(in, locks, 0.3f, kSeed, 6, Mode::Fx)));
    CHECK_FALSE(sameBytes(base, mutated(in, locks, 0.3f, kSeed + 1, 5, Mode::Fx)));
}

TEST_CASE("mutate follows the jitter law with its RNG key", "[core][mutate]")
{
    // D4's law, recomputed here from the key the header names, for layer A's Size.
    const Mode mode = Mode::Fx;
    const ParamValues in = defaults(mode);
    const LockSet locks = defaultMutateLocks(in, mode);
    const auto s = idx(ParamSlot::Size);
    const auto &meta = paramMeta(ParamSlot::Size, mode);
    for (uint32_t counter = 0; counter < 32; ++counter)
    {
        const double u = gg::rng::uniform01(gg::rng::hash(
            {kSeed, gg::core::kMutateLayer, gg::core::kMutateStream, gg::rng::Field::PositionJitter,
             static_cast<uint64_t>(counter) * kParamCount + s}));
        const float range = meta.maxVal - meta.minVal;
        const float expected = std::clamp(in[s] + 0.25f * range * static_cast<float>(2.0 * u - 1.0),
                                          meta.minVal, meta.maxVal);
        CHECK(mutated(in, locks, 0.25f, kSeed, counter, mode)[s] == expected);
    }
}

TEST_CASE("mutate at amount 0 changes nothing", "[core][mutate]")
{
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        const ParamValues in = defaults(mode);
        for (const float amount : {0.0f, -0.5f, std::numeric_limits<float>::quiet_NaN()})
        {
            for (uint32_t counter = 0; counter < 50; ++counter)
            {
                CHECK(sameBytes(mutated(in, LockSet{}, amount, kSeed, counter, mode), in));
            }
        }
    }
}

TEST_CASE("mutate keeps every result within its range", "[core][mutate]")
{
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        ParamValues lows{}, highs{};
        for (size_t s = 0; s < kParamCount; ++s)
        {
            lows[s] = paramMeta(static_cast<ParamSlot>(s), mode).minVal;
            highs[s] = paramMeta(static_cast<ParamSlot>(s), mode).maxVal;
        }
        for (const ParamValues &in : {defaults(mode), lows, highs})
        {
            for (const float amount : {0.15f, 1.0f, 7.0f})
            {
                for (uint32_t counter = 0; counter < 200; ++counter)
                {
                    const ParamValues out = mutated(in, LockSet{}, amount, kSeed, counter, mode);
                    for (size_t s = 0; s < kParamCount; ++s)
                    {
                        // One assertion per failure only: 785,000 passing ones are slow.
                        if (!(out[s] >= lows[s] && out[s] <= highs[s]))
                        {
                            FAIL("slot " << s << " "
                                         << paramMeta(static_cast<ParamSlot>(s), mode).name
                                         << " is " << out[s]);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("mutate leaves locked slots untouched", "[core][mutate]")
{
    const Mode mode = Mode::Fx;
    ParamValues in = defaults(mode);
    LockSet locks;
    for (size_t s = 0; s < kParamCount; s += 3)
    {
        locks.set(s);
    }
    for (uint32_t counter = 0; counter < 100; ++counter)
    {
        const ParamValues out = mutated(in, locks, 1.0f, kSeed, counter, mode);
        for (size_t s = 0; s < kParamCount; ++s)
        {
            if (locks.test(s))
            {
                REQUIRE(std::memcmp(&out[s], &in[s], sizeof(float)) == 0);
            }
        }
    }

    // With every slot locked, nothing moves, in place too.
    LockSet all;
    all.set();
    const ParamValues before = in;
    mutate(in, all, 1.0f, kSeed, 3, mode, in);
    CHECK(sameBytes(in, before));
}

TEST_CASE("mutate gives a stepped slot whole steps in range", "[core][mutate]")
{
    const Mode mode = Mode::Fx;
    const ParamValues in = defaults(mode);
    for (const ParamSlot slot : {ParamSlot::Source, ParamSlot::Lfo1Shape, ParamSlot::C_Lfo2Shape,
                                 ParamSlot::Freeze, ParamSlot::StepLength})
    {
        const auto &meta = paramMeta(slot, mode);
        INFO("slot " << meta.name);
        REQUIRE(stepped(slot, mode));
        const int steps = static_cast<int>(meta.maxVal - meta.minVal) + 1;
        std::vector<int> seen(static_cast<size_t>(steps), 0);
        for (uint32_t counter = 0; counter < 2000; ++counter)
        {
            const float v = mutated(in, LockSet{}, 1.0f, kSeed, counter, mode)[idx(slot)];
            if (!(v == std::floor(v) && v >= meta.minVal && v <= meta.maxVal))
            {
                FAIL("press " << counter << " gives " << v);
            }
            ++seen[static_cast<size_t>(v - meta.minVal)];
        }
        for (int step = 0; step < steps; ++step)
        {
            INFO("step " << step);
            CHECK(seen[static_cast<size_t>(step)] > 0);
        }
    }

    // At amount 0.5 a new step is drawn half the time, and it is the old one a fifth of the time,
    // so Source (five steps) moves on 40% of presses.
    int moved = 0;
    for (uint32_t counter = 0; counter < 10000; ++counter)
    {
        moved += mutated(in, LockSet{}, 0.5f, kSeed, counter, mode)[idx(ParamSlot::Source)] !=
                 in[idx(ParamSlot::Source)];
    }
    CHECK(moved > 3700);
    CHECK(moved < 4300);
}

TEST_CASE("mutate's default locks are the structure and the off layers", "[core][mutate]")
{
    // D3: the structure stays locked, in either product.
    const std::vector<ParamSlot> structural{
        ParamSlot::Mix,          ParamSlot::Level,        ParamSlot::LevelB,
        ParamSlot::LevelC,       ParamSlot::LevelD,       ParamSlot::LayerAOn,
        ParamSlot::LayerBOn,     ParamSlot::LayerCOn,     ParamSlot::LayerDOn,
        ParamSlot::Source,       ParamSlot::B_Source,     ParamSlot::C_Source,
        ParamSlot::D_Source,     ParamSlot::Freeze,       ParamSlot::B_Freeze,
        ParamSlot::C_Freeze,     ParamSlot::D_Freeze,     ParamSlot::StepLength,
        ParamSlot::B_StepLength, ParamSlot::C_StepLength, ParamSlot::D_StepLength,
        ParamSlot::Macro1,       ParamSlot::Macro2,       ParamSlot::Macro3,
        ParamSlot::Macro4,       ParamSlot::Macro5,       ParamSlot::Macro6,
        ParamSlot::Macro7,       ParamSlot::Macro8,       ParamSlot::MorphX,
        ParamSlot::MorphY,       ParamSlot::ReverbAmount, ParamSlot::DryToReverb,
    };
    LockSet expectedStructural;
    for (const ParamSlot s : structural)
    {
        expectedStructural.set(idx(s));
    }
    REQUIRE(expectedStructural.count() == 33);
    for (const ParamSlot s : kMacroSlots)
    {
        CHECK(expectedStructural.test(idx(s)));
    }
    CHECK(structuralLocks(Mode::Fx) == expectedStructural);
    CHECK(structuralLocks(Mode::Instrument) == expectedStructural);

    auto layerLocks = [](uint8_t layer) {
        LockSet l;
        for (size_t p = 0; p < kLayerParamCount; ++p)
        {
            l.set(idx(layerSlot(static_cast<LayerParam>(p), layer)));
        }
        return l;
    };

    // The FX with layer B on: layers C and D (off by default) are locked whole, and A, B and
    // Space's five voiced parameters move.
    ParamValues values = defaults(Mode::Fx);
    values[idx(ParamSlot::LayerBOn)] = 1.0f;
    const LockSet bOn = defaultMutateLocks(values, Mode::Fx);
    CHECK(offLayerLocks(values) == (layerLocks(2) | layerLocks(3)));
    CHECK(bOn == (expectedStructural | layerLocks(2) | layerLocks(3)));
    CHECK(bOn.count() == 123);
    LockSet unlocked = ~bOn;
    CHECK(unlocked.count() == 95);
    for (const ParamSlot s :
         {ParamSlot::ReverbDecay, ParamSlot::ReverbSize, ParamSlot::ReverbDamping,
          ParamSlot::ReverbPredelay, ParamSlot::ReverbModulation})
    {
        CHECK(unlocked.test(idx(s)));
        unlocked.reset(idx(s));
    }
    for (size_t s = 0; s < kParamCount; ++s)
    {
        if (unlocked.test(s))
        {
            const auto ref = layerOf(static_cast<ParamSlot>(s));
            INFO("slot " << s);
            REQUIRE(ref);
            CHECK(ref->layer <= 1);
            CHECK(ref->param != LayerParam::Source);
            CHECK(ref->param != LayerParam::Freeze);
            CHECK(ref->param != LayerParam::StepLength);
        }
    }

    // With layer B off (the default), its 48 slots are locked too.
    values[idx(ParamSlot::LayerBOn)] = 0.0f;
    const LockSet bOff = defaultMutateLocks(values, Mode::Fx);
    CHECK(bOff == (bOn | layerLocks(1)));
    CHECK(bOff.count() == 168);
    CHECK((bOff & ~bOn).count() == 45);

    // Layer A off locks its 48, the M1 slots among them; Layer On is read as "below 0.5".
    values[idx(ParamSlot::LayerAOn)] = 0.49f;
    CHECK(offLayerLocks(values) == (layerLocks(0) | layerLocks(1) | layerLocks(2) | layerLocks(3)));
    values[idx(ParamSlot::LayerAOn)] = 0.5f;
    CHECK(offLayerLocks(values) == (layerLocks(1) | layerLocks(2) | layerLocks(3)));
    for (uint8_t layer = 0; layer < kLayers; ++layer)
    {
        CHECK_FALSE(layerLocks(layer).test(idx(layerOnSlot(layer))));
    }
}

TEST_CASE("mutate's jitter is centred and bounded by the amount", "[core][mutate]")
{
    const Mode mode = Mode::Fx;
    const ParamValues in = defaults(mode);
    const LockSet locks = defaultMutateLocks(in, mode);
    const auto s = idx(ParamSlot::Size);
    const auto &meta = paramMeta(ParamSlot::Size, mode);
    const float range = meta.maxVal - meta.minVal;
    const float start = in[s];

    double sum = 0.0;
    float lo = start, hi = start;
    constexpr uint32_t kCounters = 10000;
    for (uint32_t counter = 0; counter < kCounters; ++counter)
    {
        const float v = mutated(in, locks, 0.1f, kSeed, counter, mode)[s];
        sum += v;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    // Size is log2 seconds, so these are octaves.
    CHECK(std::abs(sum / kCounters - start) < 0.02);
    CHECK(hi - start <= 0.1f * range);
    CHECK(start - lo <= 0.1f * range);
    CHECK(hi - start > 0.09f * range);
    CHECK(start - lo > 0.09f * range);

    // At amount 1 the whole range is reached.
    lo = start;
    hi = start;
    for (uint32_t counter = 0; counter < kCounters; ++counter)
    {
        const float v = mutated(in, locks, 1.0f, kSeed, counter, mode)[s];
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    CHECK(lo - meta.minVal < 0.01f * range);
    CHECK(meta.maxVal - hi < 0.01f * range);
}

TEST_CASE("mutate is allocation-free", "[core][mutate][rt]")
{
    const Mode mode = Mode::Fx;
    const ParamValues in = defaults(mode); // builds paramMeta's tables first
    ParamValues out{};
    float sink = 0.0f;

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (uint32_t counter = 0; counter < 100; ++counter)
        {
            const LockSet locks = defaultMutateLocks(in, mode);
            mutate(in, locks, 0.5f, kSeed, counter, mode, out);
            sink += out[idx(ParamSlot::Size)];
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    CHECK(std::isfinite(sink));
    if (AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

// ---- The director's decision D-Q3 (2026-10-30): Mutate never moves Decay into infinity ----

namespace
{
/// The FX's defaults with Reverb Decay at `decay` and nothing locked.
ParamValues withDecay(float decay)
{
    ParamValues v{};
    for (std::size_t s = 0; s < kParamCount; ++s)
    {
        v[s] = paramMeta(static_cast<ParamSlot>(s), Mode::Fx).defaultVal;
    }
    v[static_cast<std::size_t>(ParamSlot::ReverbDecay)] = decay;
    return v;
}
} // namespace

TEST_CASE("a press from a finite Decay never reaches the infinite zone", "[core][mutate]")
{
    const auto decay = static_cast<std::size_t>(ParamSlot::ReverbDecay);
    // From just below the zone, at full amount: a draw past it is likely, and must be capped.
    for (const float start : {gg::core::kDecayInfinite - 0.001f, 5.0f, 1.0f})
    {
        CAPTURE(start);
        const ParamValues in = withDecay(start);
        std::size_t capped = 0;
        for (uint32_t counter = 0; counter < 1000; ++counter)
        {
            ParamValues out{};
            mutate(in, LockSet{}, 1.0f, 99, counter, Mode::Fx, out);
            REQUIRE(out[decay] < gg::core::kDecayInfinite);
            capped += out[decay] == std::nextafter(gg::core::kDecayInfinite, 0.0f) ? 1 : 0;
        }
        if (start > 5.5f)
        {
            CHECK(capped > 0); // the cap was needed, and is the float just below the zone
        }
    }
}

TEST_CASE("a press from an infinite Decay may still move it, by its usual law", "[core][mutate]")
{
    const auto decay = static_cast<std::size_t>(ParamSlot::ReverbDecay);
    const ParamValues in = withDecay(gg::core::kDecayMax);
    std::size_t moved = 0;
    std::size_t left = 0;
    for (uint32_t counter = 0; counter < 1000; ++counter)
    {
        ParamValues out{};
        mutate(in, LockSet{}, 1.0f, 99, counter, Mode::Fx, out);
        moved += out[decay] != in[decay] ? 1 : 0;
        left += out[decay] < gg::core::kDecayInfinite ? 1 : 0;
        // The usual law: within amount x range of where it was, clamped.
        const auto &meta = paramMeta(ParamSlot::ReverbDecay, Mode::Fx);
        CHECK(out[decay] >= meta.minVal);
        CHECK(out[decay] <= meta.maxVal);
    }
    CHECK(moved > 0);
    CHECK(left > 0);
}
