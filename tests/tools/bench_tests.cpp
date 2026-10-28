// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// gg-bench's library: its statistics, its round order, its JSON (schema 2, carry-over [132]),
// the engine cases' grain counts, the ratios and the baseline verdict (spec §5, ruling 3). The
// bench_* tests in tests/CMakeLists.txt run the gg-bench executable itself.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
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

TEST_CASE("bench rounds vary each configuration's neighbours", "[bench]")
{
    // A full run's shape: 5 kernels x 3 block sizes, over 5 rounds.
    constexpr uint32_t n = 15;
    constexpr int rounds = 5;
    std::vector<std::vector<uint32_t>> orders;
    for (int r = 0; r < rounds; ++r)
    {
        orders.push_back(gg::bench::roundOrder(n, r));
    }

    for (const std::vector<uint32_t> &order : orders)
    {
        std::vector<uint32_t> sorted = order;
        std::sort(sorted.begin(), sorted.end());
        std::vector<uint32_t> identity(n);
        std::iota(identity.begin(), identity.end(), 0u);
        CHECK(sorted == identity); // a permutation of 0..n-1
    }

    // Every configuration follows at least 2 different configurations within rounds.
    std::vector<std::set<uint32_t>> predecessors(n);
    for (const std::vector<uint32_t> &order : orders)
    {
        for (std::size_t i = 1; i < order.size(); ++i)
        {
            predecessors[order[i]].insert(order[i - 1]);
        }
    }
    for (uint32_t k = 0; k < n; ++k)
    {
        INFO("configuration " << k);
        CHECK(predecessors[k].size() >= 2);
    }

    // No configuration runs twice back to back across a round seam.
    for (int r = 0; r + 1 < rounds; ++r)
    {
        INFO("rounds " << r << " and " << r + 1);
        CHECK(orders[r].back() != orders[r + 1].front());
    }

    CHECK(gg::bench::roundOrder(1, 3) == std::vector<uint32_t>{0});
    CHECK(gg::bench::roundOrder(0, 2).empty());
}

TEST_CASE("bench rejects bad options", "[bench]")
{
    CHECK_THROWS_AS(gg::bench::run({.cases = {"no-such-case"}}), std::invalid_argument);
    CHECK_THROWS_AS(gg::bench::run({.rounds = 0}), std::invalid_argument);
    CHECK_THROWS_AS(gg::bench::run({.runSeconds = 0.0}), std::invalid_argument);
    CHECK_THROWS_AS(gg::bench::run({.grains = 0}), std::invalid_argument);
}

TEST_CASE("bench rejects a run longer than an hour", "[bench]")
{
    CHECK_THROWS_AS(gg::bench::run({.runSeconds = 3600.5}), std::invalid_argument);
    CHECK_THROWS_AS(gg::bench::run({.runSeconds = 1e300}), std::invalid_argument);
}

TEST_CASE("engine cases report their grain counts", "[bench]")
{
    const gg::bench::Report report = gg::bench::run(
        {.runSeconds = 0.002,
         .rounds = 1,
         .grains = 50,
         .cases = {"engine-sample", "engine-sample-eco", "engine-live", "engine-full-pool"}});
    REQUIRE(report.results.size() == 12);
    for (const gg::bench::CaseResult &r : report.results)
    {
        INFO(r.name << " at block " << r.blockSize << ": " << r.grains << " grains");
        CHECK(r.kind == "engine");
        REQUIRE(r.nsPerGrainSample.size() == 1);
        CHECK(r.nsPerGrainSample[0] > 0.0);
        CHECK(r.cpuPctOneCoreAt48k > 0.0);
        if (r.name == "engine-sample" || r.name == "engine-sample-eco")
        {
            CHECK(r.grains >= 49.0);
            CHECK(r.grains <= 51.0);
        }
        else if (r.name == "engine-full-pool")
        {
            CHECK(r.grains >= 97.0); // capacity 50 + 48 = 98, against 500 grains of demand
            CHECK(r.grains <= 98.0);
        }
        else
        {
            CHECK(r.name == "engine-live");
            CHECK(r.grains > 45.0);
        }
    }
    CHECK(report.ratios.empty()); // no reference case, so no ratios
}

namespace
{
gg::bench::CaseResult result(std::string name, std::string kind, uint32_t blockSize,
                             std::vector<double> runs)
{
    gg::bench::CaseResult r;
    r.name = std::move(name);
    r.kind = std::move(kind);
    r.blockSize = blockSize;
    r.nsPerGrainSample = std::move(runs);
    return r;
}
} // namespace

TEST_CASE("ratios pair runs from the same round", "[bench]")
{
    gg::bench::Report report;
    report.results.push_back(result("reference", "kernel", 32, {1.0, 2.0, 2.0}));
    report.results.push_back(result("reference", "kernel", 64, {1.0, 1.0, 1.0}));
    report.results.push_back(result("hermite-simd", "kernel", 32, {9.0, 9.0, 9.0}));
    report.results.push_back(result("engine-sample", "engine", 32, {2.0, 4.0, 6.0}));
    report.results.push_back(result("engine-live", "engine", 64, {3.0, 5.0, 4.0}));
    report.results.push_back(result("engine-live", "engine", 256, {3.0, 5.0, 4.0}));

    const std::vector<gg::bench::Ratio> ratios = gg::bench::computeRatios(report);
    REQUIRE(ratios.size() == 2); // kernels get none; nor does an engine case with no reference
    CHECK(ratios[0].name == "engine-sample");
    CHECK(ratios[0].blockSize == 32);
    CHECK(ratios[0].runs == std::vector<double>{2.0, 2.0, 3.0});
    CHECK(ratios[0].median == 2.0);
    CHECK(ratios[0].iqr == Approx(0.5));
    CHECK(ratios[1].name == "engine-live");
    CHECK(ratios[1].blockSize == 64);
    CHECK(ratios[1].runs == std::vector<double>{3.0, 5.0, 4.0});
    CHECK(ratios[1].median == 4.0);

    // They go into the JSON.
    report.ratios = ratios;
    const nlohmann::json j = nlohmann::json::parse(gg::bench::toJson(report));
    REQUIRE(j.at("ratios").is_array());
    REQUIRE(j.at("ratios").size() == 2);
    const nlohmann::json &r0 = j.at("ratios").at(0);
    CHECK(r0.at("name").get<std::string>() == "engine-sample");
    CHECK(r0.at("blockSize").get<uint32_t>() == 32);
    CHECK(r0.at("runs").get<std::vector<double>>() == std::vector<double>{2.0, 2.0, 3.0});
    CHECK(r0.at("median").get<double>() == 2.0);
    CHECK(r0.at("iqr").get<double>() == Approx(0.5));
}

TEST_CASE("bench json survives invalid UTF-8", "[bench]")
{
    gg::bench::Report report;
    report.options.commit = "bad \xff\xfe utf-8";
    const nlohmann::json j = nlohmann::json::parse(gg::bench::toJson(report));
    CHECK(j.at("commit").is_string());
}

namespace
{
gg::bench::Ratio ratio(std::string name, uint32_t blockSize, double median)
{
    return {.name = std::move(name),
            .blockSize = blockSize,
            .runs = {median},
            .median = median,
            .iqr = 0.0};
}

nlohmann::json baselineEntry(const std::string &name, uint32_t blockSize, double ratio,
                             double tolerance)
{
    return {{"case", name}, {"blockSize", blockSize}, {"ratio", ratio}, {"tolerance", tolerance}};
}
} // namespace

TEST_CASE("baseline verdicts", "[bench]")
{
    const nlohmann::json baseline{
        {"schemaVersion", 1},
        {"leg", "dev"},
        {"commit", "abc"},
        {"ratios",
         {baselineEntry("engine-sample", 32, 1.8, 0.05),
          baselineEntry("engine-sample", 64, 1.8, 0.05)}},
    };
    gg::bench::Report report;
    report.ratios = {ratio("engine-sample", 32, 2.0), ratio("engine-sample", 64, 1.85),
                     ratio("engine-sample", 256, 1.0), ratio("engine-live", 32, 9.0)};

    const std::vector<gg::bench::Verdict> verdicts = gg::bench::compare(report, baseline);
    REQUIRE(verdicts.size() == 4);
    CHECK(verdicts[0].name == "engine-sample");
    CHECK(verdicts[0].blockSize == 32);
    CHECK(verdicts[0].ratio == 2.0);
    CHECK(verdicts[0].limit == Approx(1.89));
    CHECK(verdicts[0].regressed);
    CHECK(verdicts[1].blockSize == 64);
    CHECK(verdicts[1].ratio == 1.85);
    CHECK_FALSE(verdicts[1].regressed);
    // Missing from the baseline: reported, with no limit, and not regressed.
    CHECK(verdicts[2].blockSize == 256);
    CHECK(std::isnan(verdicts[2].limit));
    CHECK_FALSE(verdicts[2].regressed);
    CHECK(verdicts[3].name == "engine-live");
    CHECK(std::isnan(verdicts[3].limit));
    CHECK_FALSE(verdicts[3].regressed);

    // A malformed baseline is an error, even against a report with no ratios.
    nlohmann::json wrongVersion = baseline;
    wrongVersion["schemaVersion"] = 2;
    CHECK_THROWS_AS(gg::bench::compare({}, wrongVersion), std::invalid_argument);
    nlohmann::json noRatio = baseline;
    noRatio["ratios"][0].erase("ratio");
    CHECK_THROWS_AS(gg::bench::compare({}, noRatio), std::invalid_argument);
    nlohmann::json negativeTolerance = baseline;
    negativeTolerance["ratios"][0]["tolerance"] = -0.1;
    CHECK_THROWS_AS(gg::bench::compare({}, negativeTolerance), std::invalid_argument);
}

TEST_CASE("make baseline", "[bench]")
{
    std::vector<nlohmann::json> runs;
    for (const double r : {1.2, 1.0, 1.1})
    {
        gg::bench::Report report;
        report.options.commit = "abc";
        report.ratios = {ratio("engine-sample", 32, r), ratio("engine-live", 64, 2.0)};
        runs.push_back(nlohmann::json::parse(gg::bench::toJson(report)));
    }

    const nlohmann::json baseline = gg::bench::makeBaseline("linux-gcc12", runs);
    CHECK(baseline.at("schemaVersion").get<int>() == 1);
    CHECK(baseline.at("leg").get<std::string>() == "linux-gcc12");
    CHECK(baseline.at("commit").get<std::string>() == "abc");
    const nlohmann::json &ratios = baseline.at("ratios");
    REQUIRE(ratios.size() == 2);
    CHECK(ratios[0].at("case").get<std::string>() == "engine-sample");
    CHECK(ratios[0].at("blockSize").get<uint32_t>() == 32);
    CHECK(ratios[0].at("ratio").get<double>() == Approx(1.1));
    CHECK(ratios[0].at("tolerance").get<double>() == Approx(std::max(0.05, 0.4 / 1.1)));
    CHECK(ratios[1].at("case").get<std::string>() == "engine-live");
    CHECK(ratios[1].at("ratio").get<double>() == 2.0);
    CHECK(ratios[1].at("tolerance").get<double>() == 0.05); // no spread: the floor

    // The baseline it makes is one compare() accepts.
    gg::bench::Report report;
    report.ratios = {ratio("engine-sample", 32, 1.0)};
    const std::vector<gg::bench::Verdict> verdicts = gg::bench::compare(report, baseline);
    REQUIRE(verdicts.size() == 1);
    CHECK_FALSE(verdicts[0].regressed);

    // A ratio missing from one of the runs is an error, as is no run at all.
    runs[1]["ratios"].erase(1);
    CHECK_THROWS_AS(gg::bench::makeBaseline("linux-gcc12", runs), std::invalid_argument);
    CHECK_THROWS_AS(gg::bench::makeBaseline("linux-gcc12", {}), std::invalid_argument);
}
