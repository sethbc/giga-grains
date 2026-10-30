// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Rng.h"
#include "gg/core/Window.h"
#include "gg/grain/Budget.h"
#include "gg/grain/Envelope.h"
#include "gg/grain/GrainPool.h"
#include "gg/grain/Tier.h"

namespace
{
// Gain, pan and texture vary with the index, so the latched channel gains and envelope shapes
// differ from row to row, and so does the tier.
gg::GrainSpec grain(uint64_t index, int64_t onsetSample, double durationSamples)
{
    const auto k = static_cast<float>(index % 8);
    return {.index = index,
            .onsetSample = onsetSample,
            .onsetFrac = 0.25,
            .startPos = 0.0,
            .rate = 1.0,
            .durationSamples = durationSamples,
            .texture = 0.125 * static_cast<double>(index % 9),
            .gain = 0.5f + 0.0625f * k,
            .pan = 0.25f * k - 0.875f,
            .tier = index % 3 == 0 ? gg::Tier::Eco : gg::Tier::Normal};
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
            // The Texture envelope's shape (spec §4).
            const gg::EnvelopeShape e = gg::envelopeShape(g.texture, g.durationSamples);
            REQUIRE(rows.texture[i] == g.texture);
            REQUIRE(rows.envAttackEnd[i] == e.attackEnd);
            REQUIRE(rows.envReleaseStart[i] == e.releaseStart);
            REQUIRE(rows.envAttackScale[i] == e.attackScale);
            REQUIRE(rows.envReleaseScale[i] == e.releaseScale);
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
    // The same holds through the Texture warp at its steepest release, Texture 1's gated anchor.
    const gg::HannTable hann;
    for (const double frac : {0.0, 0.25, 0.999999})
    {
        for (const double dur : {16.0, 16.5, 4800.0, 4800.999})
        {
            const gg::GrainSpec g{.index = 0,
                                  .onsetSample = 1000,
                                  .onsetFrac = frac,
                                  .startPos = 0.0,
                                  .rate = 1.0,
                                  .durationSamples = dur,
                                  .gain = 1.0f,
                                  .pan = 0.0f};
            const int64_t end = gg::grainEndSample(g);
            const double elapsed = static_cast<double>(end - g.onsetSample) - g.onsetFrac;
            REQUIRE(elapsed >= dur);
            REQUIRE(end - g.onsetSample <= static_cast<int64_t>(dur) + 2);
            const double invDuration = 1.0 / dur;
            const gg::EnvelopeShape gated = gg::envelopeShape(1.0, dur);
            for (int64_t t = end; t < end + 4; ++t)
            {
                const double e = static_cast<double>(t - g.onsetSample) - g.onsetFrac;
                REQUIRE(e >= dur);
                REQUIRE(hann.at(e * invDuration) == 0.0f);
                REQUIRE(hann.at(gg::warpPhase(e * invDuration, gated)) == 0.0f);
            }
            REQUIRE(hann.at(dur * invDuration) == 0.0f);
            REQUIRE(hann.at(gg::warpPhase(dur * invDuration, gated)) == 0.0f);
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

namespace
{
// A grain of the given weight, onset and window length, for the ledger's tests.
gg::GrainSpec weighted(uint64_t index, int64_t onsetSample, double durationSamples, uint8_t weight)
{
    gg::GrainSpec g = grain(index, onsetSample, durationSamples);
    g.weight = weight;
    return g;
}

uint64_t draw(uint32_t stream, uint64_t k)
{
    return gg::rng::hash({11, 0, stream, gg::rng::Field::SignalNoise, k});
}
} // namespace

TEST_CASE("the weighted ledger matches a brute-force sum", "[pool]")
{
    // 1,000 grains of weights 1 to 12 and lengths 16 to 4,000 samples, onsets advancing by 0 to
    // 19 samples, into a pool large enough that storage never fills. At 200 onsets (one every
    // fifth grain) soundingWeight and soundingCount must equal the sum and count, by a scan, of
    // the spawned grains whose end is after the onset; the pool is also retired at some of them,
    // as the engine retires at block ends, which must change neither answer.
    constexpr uint32_t kGrains = 1000;
    gg::GrainPool pool(kGrains);
    std::vector<gg::GrainSpec> spawned;
    int64_t onset = 0;
    uint32_t checks = 0;
    uint64_t largest = 0;
    for (uint64_t k = 0; k < kGrains; ++k)
    {
        onset += static_cast<int64_t>(draw(0, k) % 20);
        const auto weight = static_cast<uint8_t>(1 + draw(1, k) % 12);
        const double dur = 16.0 + static_cast<double>(draw(2, k) % 3985);
        if (k % 5 == 0)
        {
            uint64_t sum = 0;
            uint32_t count = 0;
            for (const gg::GrainSpec &g : spawned)
            {
                if (gg::grainEndSample(g) > onset)
                {
                    sum += g.weight;
                    ++count;
                }
            }
            INFO("grain " << k << " at onset " << onset);
            REQUIRE(pool.soundingWeight(onset) == sum);
            REQUIRE(pool.soundingCount(onset) == count);
            REQUIRE(pool.soundingWeight(onset) == sum); // forgetting twice changes nothing
            largest = std::max(largest, sum);
            ++checks;
            if (draw(3, k) % 4 == 0)
            {
                pool.retireFinished(onset);
            }
        }
        const gg::GrainSpec g = weighted(k, onset, dur, weight);
        REQUIRE(pool.spawn(g));
        spawned.push_back(g);
    }
    REQUIRE(checks == 200);
    REQUIRE(largest > 500); // many grains overlapped
}

TEST_CASE("a steal moves the end and keeps the heap and earliestEnd correct", "[pool]")
{
    // 300 grains with onsets 0 to 299 and lengths 2,000 to 5,999 samples. From onset 1,000 on,
    // in steps of 7 samples, a grain chosen at random among those still sounding is stolen: its
    // fade's start is latched, and with more than 256 samples left its end moves to onset + 256
    // and its scale is latched, while with at most 256 left its end and scale stay (ruling
    // ES-T6a). The heap's sum and count, and the fading sum of the faded grains alone, must
    // still match a scanned model with the moved ends. Retiring at
    // each step must remove exactly the grains whose (moved) end has passed, keeping spawn
    // order; oldestSounding must return the first unstolen sounding grain.
    constexpr uint32_t kGrains = 300;
    gg::GrainPool pool(kGrains);
    struct Model
    {
        uint64_t index;
        int64_t end;
        uint8_t weight;
        bool stolen;
        bool faded;
    };
    std::vector<Model> model;
    for (uint64_t k = 0; k < kGrains; ++k)
    {
        const auto weight = static_cast<uint8_t>(1 + draw(4, k) % 12);
        const gg::GrainSpec g = weighted(k, static_cast<int64_t>(k),
                                         2000.0 + static_cast<double>(draw(5, k) % 4000), weight);
        REQUIRE(pool.spawn(g));
        model.push_back({k, gg::grainEndSample(g), weight, false, false});
    }
    uint32_t steals = 0;
    uint32_t shortSteals = 0;
    for (int64_t onset = 1000; onset < 7000; onset += 7)
    {
        // The model: what still sounds, its weight, and the first unstolen sounding grain.
        uint64_t sum = 0;
        uint64_t fading = 0;
        uint32_t count = 0;
        std::vector<std::size_t> sounding;
        std::size_t oldest = model.size();
        for (std::size_t m = 0; m < model.size(); ++m)
        {
            if (model[m].end > onset)
            {
                sum += model[m].weight;
                fading += model[m].faded ? model[m].weight : 0;
                ++count;
                sounding.push_back(m);
                if (!model[m].stolen && oldest == model.size())
                {
                    oldest = m;
                }
            }
        }
        INFO("onset " << onset);
        REQUIRE(pool.soundingWeight(onset) == sum);
        REQUIRE(pool.fadingWeight(onset) == fading);
        REQUIRE(pool.soundingCount(onset) == count);
        uint32_t row = 0;
        const bool found = pool.oldestSounding(onset, row);
        REQUIRE(found == (oldest != model.size()));
        if (found)
        {
            REQUIRE(pool.rows().index[row] == model[oldest].index);
            REQUIRE(pool.rows().fadeStart[row] == gg::kNeverStolen);
        }

        // Steal a random sounding grain not stolen before.
        std::vector<std::size_t> candidates;
        for (const std::size_t m : sounding)
        {
            if (!model[m].stolen)
            {
                candidates.push_back(m);
            }
        }
        if (!candidates.empty() && draw(6, static_cast<uint64_t>(onset)) % 3 != 0)
        {
            const std::size_t m =
                candidates[draw(7, static_cast<uint64_t>(onset)) % candidates.size()];
            uint32_t r = 0;
            while (pool.rows().index[r] != model[m].index)
            {
                ++r;
            }
            const int64_t left = model[m].end - onset;
            const bool fades = left > gg::kStealFade;
            pool.steal(r, onset);
            model[m].end = fades ? onset + gg::kStealFade : model[m].end;
            model[m].stolen = true;
            model[m].faded = fades;
            REQUIRE(pool.rows().endSample[r] == model[m].end);
            REQUIRE(pool.rows().fadeStart[r] == onset);
            REQUIRE(pool.rows().invFade[r] ==
                    (fades ? 1.0f / static_cast<float>(gg::kStealFade) : 0.0f));
            ++steals;
            shortSteals += fades ? 0 : 1;
        }

        // Retire at the next onset, as the engine does at a block's end before it: exactly the
        // grains whose end has passed go, the rest stay in spawn order with their rows.
        const int64_t now = onset + 7;
        pool.retireFinished(now);
        std::vector<uint64_t> expected;
        for (const Model &g : model)
        {
            if (g.end > now)
            {
                expected.push_back(g.index);
            }
        }
        REQUIRE(indices(pool) == expected);
        const gg::GrainRows &rows = pool.rows();
        for (uint32_t i = 0; i < pool.size(); ++i)
        {
            const Model &g = model[rows.index[i]];
            REQUIRE(rows.endSample[i] == g.end);
            REQUIRE(rows.weight[i] == g.weight);
            REQUIRE((rows.fadeStart[i] != gg::kNeverStolen) == g.stolen);
            REQUIRE((rows.invFade[i] != 0.0f) == g.faded);
        }
    }
    REQUIRE(pool.size() == 0);
    REQUIRE(steals > 200);
    REQUIRE(shortSteals > 0); // some grains had at most the fade left
}

TEST_CASE("a steal of a grain with less than the fade left keeps its end and does not fade",
          "[pool]")
{
    // Ruling ES-T6a: a grain with at most kStealFade samples left already ends no later than a
    // fade would, so a steal only marks it stolen (fadeStart, so it is never stolen again): its
    // end, its scale (invFade 0) and its ledger entry stay, and its weight stays in the ordinary
    // ledger, not the fading one, until its end. With one sample more it fades over kStealFade.
    gg::GrainPool pool(4);
    // Ends at 1,000 + 498 + 2 = 1,500, at 1,001 + 4,000 + 2 = 5,003 and at 5,004.
    REQUIRE(pool.spawn(weighted(0, 1000, 498.0, 4)));
    REQUIRE(pool.spawn(weighted(1, 1001, 4000.0, 3)));
    REQUIRE(pool.spawn(weighted(2, 1002, 4000.0, 2)));
    REQUIRE(pool.rows().fadeStart[0] == gg::kNeverStolen);
    REQUIRE(pool.rows().invFade[0] == 0.0f);
    REQUIRE(pool.soundingWeight(1400) == 9);

    // 100 samples left, fewer than kStealFade.
    uint32_t row = 9;
    REQUIRE(pool.oldestSounding(1400, row));
    REQUIRE(row == 0);
    pool.steal(row, 1400);
    REQUIRE(pool.rows().endSample[0] == 1500);
    REQUIRE(pool.rows().fadeStart[0] == 1400);
    REQUIRE(pool.rows().invFade[0] == 0.0f);
    REQUIRE(pool.soundingWeight(1499) == 9);
    REQUIRE(pool.fadingWeight(1499) == 0);
    REQUIRE(pool.soundingWeight(1500) == 5);
    REQUIRE(pool.fadingWeight(1500) == 0);

    // A stolen grain is never the oldest again; the next one is.
    REQUIRE(pool.oldestSounding(1500, row));
    REQUIRE(row == 1);
    // Exactly kStealFade left: still no fade, the end stays.
    pool.steal(1, 5003 - gg::kStealFade);
    REQUIRE(pool.rows().endSample[1] == 5003);
    REQUIRE(pool.rows().fadeStart[1] == 5003 - gg::kStealFade);
    REQUIRE(pool.rows().invFade[1] == 0.0f);
    REQUIRE(pool.fadingWeight(5003 - gg::kStealFade) == 0);
    // One more, kStealFade + 1 left: it fades, its end one sample earlier, at 5,003.
    REQUIRE(pool.oldestSounding(5003 - gg::kStealFade, row));
    REQUIRE(row == 2);
    pool.steal(2, 5003 - gg::kStealFade);
    REQUIRE(pool.rows().endSample[2] == 5003);
    REQUIRE(pool.rows().fadeStart[2] == 5003 - gg::kStealFade);
    REQUIRE(pool.rows().invFade[2] == 1.0f / 256.0f);
    REQUIRE(static_cast<float>(gg::kStealFade) * pool.rows().invFade[2] == 1.0f);
    REQUIRE(pool.soundingWeight(5002) == 5);
    REQUIRE(pool.fadingWeight(5002) == 2);
    REQUIRE_FALSE(pool.oldestSounding(5002, row));
    REQUIRE(pool.soundingCount(5002) == 2);
    pool.retireFinished(5003);
    REQUIRE(pool.size() == 0);
    REQUIRE(pool.soundingWeight(5003) == 0);
    REQUIRE(pool.fadingWeight(5003) == 0);
}
