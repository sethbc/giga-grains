// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <cmath>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Rng.h"

TEST_CASE("splitmix64 matches reference vector", "[rng]")
{
    REQUIRE(gg::rng::splitmix64(0) == 0xE220A8397B1DCDAFull);
    STATIC_REQUIRE(gg::rng::splitmix64(0) == 0xE220A8397B1DCDAFull);
}

TEST_CASE("hash is order-sensitive and stable", "[rng]")
{
    using namespace gg::rng;
    auto a = hash({1, 0, 0, Field::PositionJitter, 7});
    REQUIRE(a == hash({1, 0, 0, Field::PositionJitter, 7}));
    REQUIRE(a != hash({1, 0, 0, Field::PositionJitter, 8}));
    REQUIRE(a != hash({2, 0, 0, Field::PositionJitter, 7}));
    REQUIRE(a != hash({1, 1, 0, Field::PositionJitter, 7}));
    REQUIRE(a != hash({1, 0, 1, Field::PositionJitter, 7}));
    REQUIRE(a != hash({1, 0, 0, Field::SignalNoise, 7}));
    STATIC_REQUIRE(hash({1, 0, 0, Field::PositionJitter, 7}) ==
                   hash({1, 0, 0, Field::PositionJitter, 7}));
}

TEST_CASE("uniform01 range and mean", "[rng]")
{
    using namespace gg::rng;
    REQUIRE(uniform01(0) == 0.0);
    REQUIRE(uniform01(~0ull) == 1.0 - 0x1.0p-53);

    constexpr uint64_t kDraws = 100000;
    bool allInRange = true;
    double sum = 0.0;
    for (uint64_t i = 0; i < kDraws; ++i)
    {
        const double u = uniform01(hash({1, 0, 0, Field::PositionJitter, i}));
        allInRange = allInRange && u >= 0.0 && u < 1.0;
        sum += u;
    }
    REQUIRE(allInRange);
    REQUIRE(std::abs(sum / kDraws - 0.5) < 0.005);
}

TEST_CASE("bipolar range and mean", "[rng]")
{
    using namespace gg::rng;
    REQUIRE(bipolar(0) == -1.0);
    REQUIRE(bipolar(~0ull) == 1.0 - 0x1.0p-52);

    constexpr uint64_t kDraws = 100000;
    bool allInRange = true;
    double sum = 0.0;
    for (uint64_t i = 0; i < kDraws; ++i)
    {
        const double b = bipolar(hash({1, 0, 0, Field::PositionJitter, i}));
        allInRange = allInRange && b >= -1.0 && b < 1.0;
        sum += b;
    }
    REQUIRE(allInRange);
    REQUIRE(std::abs(sum / kDraws) < 0.01);
}
