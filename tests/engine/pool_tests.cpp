// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Rng.h"
#include "gg/core/Window.h"
#include "gg/grain/GrainPool.h"
#include "gg/grain/Tier.h"

namespace
{
// Gain and pan vary with the index, so the latched channel gains differ from row to row, and so
// does the tier.
gg::GrainSpec grain(uint64_t index, int64_t onsetSample, double durationSamples)
{
    const auto k = static_cast<float>(index % 8);
    return {index,
            onsetSample,
            0.25,
            0.0,
            1.0,
            durationSamples,
            0.5f + 0.0625f * k,
            0.25f * k - 0.875f,
            index % 3 == 0 ? gg::Tier::Eco : gg::Tier::Normal};
}

std::vector<uint64_t> indices(const gg::GrainPool &pool)
{
    std::vector<uint64_t> v;
    for (uint32_t i = 0; i < pool.size(); ++i)
    {
        v.push_back(pool.spec(i).index);
    }
    return v;
}
} // namespace

TEST_CASE("pool keeps spawn order through retirement", "[pool]")
{
    // Staggered durations, so grains finish in an order unrelated to their spawn order.
    constexpr double kDurations[10] = {100, 20, 300, 40, 16, 500, 60, 200, 30, 80};
    gg::GrainPool pool(16);
    for (uint64_t i = 0; i < 10; ++i)
    {
        const auto onset = static_cast<int64_t>(i) * 5;
        REQUIRE(pool.canAdmit(onset));
        REQUIRE(pool.spawn(grain(i, onset, kDurations[i])));
    }
    REQUIRE(pool.size() == 10);

    for (const int64_t now : {int64_t{0}, int64_t{50}, int64_t{90}, int64_t{150}, int64_t{260},
                              int64_t{400}, int64_t{600}})
    {
        pool.retireFinished(now);

        std::vector<uint64_t> expected;
        for (uint64_t i = 0; i < 10; ++i)
        {
            const int64_t end =
                gg::grainEndSample(grain(i, static_cast<int64_t>(i) * 5, kDurations[i]));
            if (end > now)
            {
                expected.push_back(i);
            }
        }
        INFO("now = " << now);
        REQUIRE(indices(pool) == expected);
        // Every array moved together: each row still holds its grain's fields and end.
        const gg::GrainRows &rows = pool.rows();
        for (uint32_t i = 0; i < pool.size(); ++i)
        {
            const uint64_t k = expected[i];
            const gg::GrainSpec g = grain(k, static_cast<int64_t>(k) * 5, kDurations[k]);
            REQUIRE(pool.spec(i) == g);
            REQUIRE(rows.index[i] == k);
            REQUIRE(rows.endSample[i] == gg::grainEndSample(g));
            REQUIRE(rows.tier[i] == g.tier);
            // The values spawn latches for the renderers (spec §3).
            REQUIRE(rows.invDuration[i] == 1.0 / g.durationSamples);
            REQUIRE(rows.gainL[i] == g.gain * gg::panGains(g.pan).left);
            REQUIRE(rows.gainR[i] == g.gain * gg::panGains(g.pan).right);
        }
    }
    REQUIRE(pool.size() == 0);
}

TEST_CASE("grain end bounds the window", "[pool]")
{
    // Every sample at or after grainEndSample has elapsed time >= durationSamples, where the
    // window is zero, for any onset fraction in [0, 1). The window is evaluated as the renderers
    // do, at e × (1 / duration): it is zero there, and at e == duration exactly, so no sample a
    // renderer skips for the grain's end could have sounded (the SIMD kernels' runs rely on it).
    const gg::HannTable hann;
    for (const double frac : {0.0, 0.25, 0.999999})
    {
        for (const double dur : {16.0, 16.5, 4800.0, 4800.999})
        {
            const gg::GrainSpec g{0, 1000, frac, 0.0, 1.0, dur, 1.0f, 0.0f};
            const int64_t end = gg::grainEndSample(g);
            const double elapsed = static_cast<double>(end - g.onsetSample) - g.onsetFrac;
            REQUIRE(elapsed >= dur);
            REQUIRE(end - g.onsetSample <= static_cast<int64_t>(dur) + 2);
            const double invDuration = 1.0 / dur;
            for (int64_t t = end; t < end + 4; ++t)
            {
                const double e = static_cast<double>(t - g.onsetSample) - g.onsetFrac;
                REQUIRE(e >= dur);
                REQUIRE(hann.at(e * invDuration) == 0.0f);
            }
            REQUIRE(hann.at(dur * invDuration) == 0.0f);
        }
    }
}

TEST_CASE("pool full drops deterministically", "[pool]")
{
    gg::GrainPool pool(4);
    REQUIRE(pool.capacity() == 4);
    int accepted = 0;
    int rejected = 0;
    for (uint64_t i = 0; i < 6; ++i)
    {
        if (pool.canAdmit(0))
        {
            REQUIRE(pool.spawn(grain(i, 0, 100)));
            ++accepted;
        }
        else
        {
            ++rejected;
        }
    }
    REQUIRE(accepted == 4);
    REQUIRE(rejected == 2);
    REQUIRE(pool.size() == 4);
    REQUIRE(indices(pool) == std::vector<uint64_t>{0, 1, 2, 3});

    pool.clear();
    REQUIRE(pool.size() == 0);
    REQUIRE_FALSE(pool.storageFull());
    REQUIRE(pool.canAdmit(0)); // time may restart after a clear
    REQUIRE(pool.spawn(grain(9, 0, 100)));
    REQUIRE(indices(pool) == std::vector<uint64_t>{9});

    gg::GrainPool none(0);
    REQUIRE_FALSE(none.canAdmit(0));
    REQUIRE(none.storageFull());
    REQUIRE_FALSE(none.spawn(grain(0, 0, 100)));
    REQUIRE(none.size() == 0);
}

TEST_CASE("pool admits by sounding grains, not stored ones", "[pool]")
{
    // Four grains that end at 100 fill the admission limit until then; at 100 they no longer
    // sound, so more are admitted although none has been retired. Storage holds 2 × capacity.
    gg::GrainPool pool(4);
    std::vector<gg::GrainSpec> spawned;
    for (uint64_t i = 0; i < 4; ++i)
    {
        const gg::GrainSpec g = grain(i, 0, 98);
        REQUIRE(gg::grainEndSample(g) == 100);
        REQUIRE(pool.canAdmit(0));
        REQUIRE(pool.spawn(g));
        spawned.push_back(g);
    }
    REQUIRE_FALSE(pool.canAdmit(99));
    REQUIRE(pool.canAdmit(100));
    for (uint64_t i = 4; i < 8; ++i)
    {
        const gg::GrainSpec g = grain(i, 100, 98);
        REQUIRE(pool.spawn(g));
        spawned.push_back(g);
    }
    REQUIRE(pool.size() == 8);
    REQUIRE(pool.storageFull());
    REQUIRE_FALSE(pool.spawn(grain(8, 100, 98)));
    REQUIRE(pool.size() == 8);
    for (uint32_t i = 0; i < 8; ++i)
    {
        REQUIRE(pool.spec(i) == spawned[i]);
    }

    pool.retireFinished(100);
    REQUIRE(pool.size() == 4);
    REQUIRE_FALSE(pool.storageFull());
    REQUIRE(indices(pool) == std::vector<uint64_t>{4, 5, 6, 7});
    for (uint32_t i = 0; i < 4; ++i)
    {
        REQUIRE(pool.spec(i) == spawned[4 + i]);
    }
    REQUIRE_FALSE(pool.canAdmit(100));
}

TEST_CASE("pool admission matches a brute-force count", "[pool]")
{
    // 20,000 grains with random durations in [1, 500] and onsets advancing by 0, 1, 2 or 3
    // samples, against capacity 64. The reference admits a grain when fewer than 64 of the grains
    // it has admitted still sound at the onset (end > onset), by a linear scan over the ends it
    // hasn't yet seen pass. The pool runs as the engine drives it: canAdmit, an early retirement
    // when storage is full, and a retirement at each block's end sample, which is past every
    // onset checked in the block, and at or before the next onset. Blocks are 1 to 3,000 samples
    // long; the longer ones admit more than 64 grains, so storage fills within them.
    constexpr uint32_t kCapacity = 64;
    constexpr uint64_t kGrains = 20000;
    using gg::rng::Field;
    const auto blockLength = [](uint64_t b) {
        return 1 + static_cast<int64_t>(gg::rng::hash({7, 0, 2, Field::SignalNoise, b}) % 3000);
    };
    gg::GrainPool pool(kCapacity);
    std::vector<int64_t> referenceEnds; // admitted grains' ends > the last onset checked
    std::vector<uint64_t> expected;
    std::vector<uint64_t> admitted;
    int64_t onset = 0;
    uint64_t block = 0;
    int64_t blockEnd = blockLength(block);
    uint64_t earlyRetirements = 0;
    uint64_t endsRetiredAtBlockEnd = 0; // ends in (last onset, block end], which canAdmit never saw
    for (uint64_t k = 0; k < kGrains; ++k)
    {
        const uint64_t hd = gg::rng::hash({7, 0, 0, Field::SignalNoise, k});
        const uint64_t hs = gg::rng::hash({7, 0, 1, Field::SignalNoise, k});
        const double dur = 1.0 + static_cast<double>(hd % 500);
        onset += static_cast<int64_t>(hs % 4);
        const gg::GrainSpec g = grain(k, onset, dur);

        while (onset >= blockEnd)
        {
            pool.retireFinished(blockEnd);
            // The next onset is >= blockEnd, so these ends would be pruned there anyway.
            endsRetiredAtBlockEnd +=
                std::erase_if(referenceEnds, [&](int64_t end) { return end <= blockEnd; });
            blockEnd += blockLength(++block);
        }

        std::erase_if(referenceEnds, [&](int64_t end) { return end <= onset; });
        if (referenceEnds.size() < kCapacity)
        {
            referenceEnds.push_back(gg::grainEndSample(g));
            expected.push_back(k);
        }

        if (pool.canAdmit(onset))
        {
            if (pool.storageFull())
            {
                pool.retireFinished(onset);
                ++earlyRetirements;
            }
            REQUIRE(pool.spawn(g));
            admitted.push_back(k);
        }
        REQUIRE(pool.size() <= 2 * kCapacity);
    }
    REQUIRE(admitted == expected);
    // Guard against a vacuous pass: the limit bit, storage filled within a block, and block-end
    // retirements removed grains whose ends canAdmit hadn't yet forgotten.
    REQUIRE(admitted.size() < kGrains);
    REQUIRE(earlyRetirements > 0);
    REQUIRE(endsRetiredAtBlockEnd > 0);
}
