// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// gg-bench's library: its statistics, its round order and its JSON (schema 2, carry-over
// [132]). The bench_cli test in tests/CMakeLists.txt runs the gg-bench executable itself.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "Bench.h"

using Catch::Approx;

TEST_CASE("bench json matches schema 2", "[bench]")
{
    const gg::bench::Report report =
        gg::bench::run({.runSeconds = 0.002, .rounds = 3, .grains = 50, .cases = {"hermite-simd"}});
    const nlohmann::json j = nlohmann::json::parse(gg::bench::toJson(report));

    REQUIRE(j.is_object());
    REQUIRE(j.at("schemaVersion").is_number_integer());
    CHECK(j.at("schemaVersion").get<int>() == 2);
    CHECK(j.at("machine").is_string());
    CHECK(j.at("compiler").is_string());
    CHECK(j.at("commit").is_string());
    CHECK(j.at("commit").get<std::string>() == "unknown");
    REQUIRE(j.at("runSeconds").is_number());
    CHECK(j.at("runSeconds").get<double>() == 0.002);
    REQUIRE(j.at("rounds").is_number_integer());
    CHECK(j.at("rounds").get<int>() == 3);
    REQUIRE(j.at("ratios").is_array());
    CHECK(j.at("ratios").empty());

    const nlohmann::json &results = j.at("results");
    REQUIRE(results.is_array());
    REQUIRE(results.size() == 3);
    std::vector<uint32_t> blockSizes;
    for (const nlohmann::json &r : results)
    {
        REQUIRE(r.is_object());
        REQUIRE(r.at("name").is_string());
        CHECK(r.at("name").get<std::string>() == "hermite-simd");
        REQUIRE(r.at("kind").is_string());
        CHECK(r.at("kind").get<std::string>() == "kernel");
        REQUIRE(r.at("blockSize").is_number_unsigned());
        blockSizes.push_back(r.at("blockSize").get<uint32_t>());
        REQUIRE(r.at("grains").is_number());
        CHECK(r.at("grains").get<double>() == 50.0);

        const nlohmann::json &runs = r.at("runs");
        REQUIRE(runs.is_array());
        REQUIRE(runs.size() == 3);
        for (const nlohmann::json &run : runs)
        {
            REQUIRE(run.is_number());
            CHECK(run.get<double>() > 0.0);
        }
        REQUIRE(r.at("median").is_number());
        REQUIRE(r.at("min").is_number());
        REQUIRE(r.at("iqr").is_number());
        REQUIRE(r.at("cpuPctOneCoreAt48k").is_number());
        const double median = r.at("median").get<double>();
        CHECK(r.at("min").get<double>() <= median);
        CHECK(r.at("iqr").get<double>() >= 0.0);
        CHECK(r.at("cpuPctOneCoreAt48k").get<double>() > 0.0);
    }
    CHECK(blockSizes == std::vector<uint32_t>{32, 64, 256});
}

TEST_CASE("bench statistics", "[bench]")
{
    const std::vector<double> v{4.0, 1.0, 3.0, 2.0}; // unsorted: quantile sorts its copy
    const double median = gg::bench::quantile(v, 0.5);
    const double iqr = gg::bench::quantile(v, 0.75) - gg::bench::quantile(v, 0.25);
    CHECK(median == Approx(2.5));
    CHECK(iqr == Approx(1.5));
    CHECK(gg::bench::quantile(v, 0.0) == 1.0);
    CHECK(gg::bench::quantile(v, 1.0) == 4.0);
    CHECK(gg::bench::quantile({7.0}, 0.25) == 7.0);
}

TEST_CASE("bench rounds rotate the order", "[bench]")
{
    using Order = std::vector<uint32_t>;
    CHECK(gg::bench::roundOrder(4, 0) == Order{0, 1, 2, 3});
    CHECK(gg::bench::roundOrder(4, 1) == Order{1, 2, 3, 0});
    CHECK(gg::bench::roundOrder(4, 2) == Order{2, 3, 0, 1});
    CHECK(gg::bench::roundOrder(4, 3) == Order{3, 0, 1, 2});
    CHECK(gg::bench::roundOrder(4, 4) == Order{0, 1, 2, 3});
    CHECK(gg::bench::roundOrder(1, 3) == Order{0});
}

TEST_CASE("bench rejects bad options", "[bench]")
{
    CHECK_THROWS_AS(gg::bench::run({.cases = {"no-such-case"}}), std::invalid_argument);
    CHECK_THROWS_AS(gg::bench::run({.rounds = 0}), std::invalid_argument);
    CHECK_THROWS_AS(gg::bench::run({.runSeconds = 0.0}), std::invalid_argument);
    CHECK_THROWS_AS(gg::bench::run({.grains = 0}), std::invalid_argument);
}
