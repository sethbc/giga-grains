// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/grain/GrainPool.h"

namespace
{
gg::GrainSpec grain(uint64_t index, int64_t onsetSample, double durationSamples)
{
    return {index, onsetSample, 0.25, 0.0, 1.0, durationSamples, 1.0f};
}

std::vector<uint64_t> indices(const gg::GrainPool &pool)
{
    std::vector<uint64_t> v;
    for (uint32_t i = 0; i < pool.size(); ++i)
    {
        v.push_back(pool.at(i).index);
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
        REQUIRE(pool.spawn(grain(i, static_cast<int64_t>(i) * 5, kDurations[i])));
    }
    REQUIRE(pool.size() == 10);

    for (const int64_t now : {int64_t{0}, int64_t{50}, int64_t{90}, int64_t{150}, int64_t{260},
                              int64_t{400}, int64_t{600}})
    {
        pool.retireFinished(now);

        std::vector<uint64_t> expected;
        int64_t earliest = std::numeric_limits<int64_t>::max();
        for (uint64_t i = 0; i < 10; ++i)
        {
            const int64_t end =
                gg::grainEndSample(grain(i, static_cast<int64_t>(i) * 5, kDurations[i]));
            if (end > now)
            {
                expected.push_back(i);
                earliest = end < earliest ? end : earliest;
            }
        }
        const auto remaining = indices(pool);
        INFO("now = " << now);
        REQUIRE(remaining == expected);
        bool increasing = true;
        for (std::size_t i = 1; i < remaining.size(); ++i)
        {
            increasing = increasing && remaining[i - 1] < remaining[i];
        }
        REQUIRE(increasing);
        REQUIRE(pool.earliestEnd() == earliest);
    }
    REQUIRE(pool.size() == 0);
}

TEST_CASE("grain end bounds the window", "[pool]")
{
    // Every sample at or after grainEndSample has elapsed time >= durationSamples, where the
    // window is zero, for any onset fraction in [0, 1).
    for (const double frac : {0.0, 0.25, 0.999999})
    {
        for (const double dur : {16.0, 16.5, 4800.0, 4800.999})
        {
            const gg::GrainSpec g{0, 1000, frac, 0.0, 1.0, dur, 1.0f};
            const int64_t end = gg::grainEndSample(g);
            const double elapsed = static_cast<double>(end - g.onsetSample) - g.onsetFrac;
            REQUIRE(elapsed >= dur);
            REQUIRE(end - g.onsetSample <= static_cast<int64_t>(dur) + 2);
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
        if (pool.spawn(grain(i, 0, 100)))
        {
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
    REQUIRE(pool.earliestEnd() == std::numeric_limits<int64_t>::max());
    REQUIRE(pool.spawn(grain(9, 0, 100)));

    gg::GrainPool none(0);
    REQUIRE_FALSE(none.spawn(grain(0, 0, 100)));
    REQUIRE(none.size() == 0);
}
