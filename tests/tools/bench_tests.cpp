// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// gg-bench's library: its statistics, its round order, its JSON (schema 2, carry-over [132]),
// the engine cases' grain counts, the ratios, the baseline verdict (spec §5, ruling 3) and
// --space, Space's report-only cost. The bench_* tests in tests/CMakeLists.txt run the gg-bench
// executable itself.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "Bench.h"
#include "Kernels.h"
#include "support/Utf8.h"

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

    // The four-layer cases. engine-layers shares the grains out among its layers (13, 13, 12
    // and 12 of these 50; 500 each of the default 2,000, so its count matches X's), and
    // engine-layers-full gives each layer all of them (4 x 50). A layered case's count is the
    // total over its layers. 50 grains, not the default: at 2,000 the Debug build's warm-ups
    // take over a minute (the default's spread is checked by "engine-layers admits its grains
    // across four layers").
    const gg::bench::Report layers =
        gg::bench::run({.runSeconds = 0.002,
                        .rounds = 1,
                        .grains = 50,
                        .cases = {"engine-layers", "engine-layers-full"}});
    REQUIRE(layers.results.size() == 6);
    for (const gg::bench::CaseResult &r : layers.results)
    {
        INFO(r.name << " at block " << r.blockSize << ": " << r.grains << " grains");
        CHECK(r.kind == "engine");
        REQUIRE(r.nsPerGrainSample.size() == 1);
        CHECK(r.nsPerGrainSample[0] > 0.0);
        CHECK(r.cpuPctOneCoreAt48k > 0.0);
        CHECK((r.name == "engine-layers" || r.name == "engine-layers-full"));
        const double total = r.name == "engine-layers" ? 50.0 : 200.0;
        CHECK(r.grains >= total - 2.0);
        CHECK(r.grains <= total + 2.0);
    }
    CHECK(layers.ratios.empty());
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

TEST_CASE("bench json escapes quotes, backslashes and control characters", "[bench]")
{
    // Both documents, schema 2 and --space's, with them in the commit: they read back intact.
    const std::string commit = "a\"b\\c\x01"
                               "d\n"; // \x01, then d
    gg::bench::Report report;
    report.options.commit = commit;
    CHECK(nlohmann::json::parse(gg::bench::toJson(report)).at("commit").get<std::string>() ==
          commit);
    gg::bench::SpaceReport space;
    space.options.commit = commit;
    space.nsPerSample = {1.0};
    CHECK(nlohmann::json::parse(gg::bench::toJson(space)).at("commit").get<std::string>() ==
          commit);
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
    // Two CPU models, each with its own ratios: only this run's model is checked.
    const nlohmann::json baseline{
        {"schemaVersion", 2},
        {"leg", "dev"},
        {"machines",
         {{{"machine", "CPU A (x86_64)"},
           {"commit", "abc"},
           {"runs", 3},
           {"ratios",
            {baselineEntry("engine-sample", 32, 1.8, 0.05),
             baselineEntry("engine-sample", 64, 1.8, 0.05)}}},
          {{"machine", "CPU B (x86_64)"},
           {"commit", "def"},
           {"runs", 1},
           {"ratios", {baselineEntry("engine-sample", 32, 9.0, 0.10)}}}}},
    };
    gg::bench::Report report;
    report.machine = "CPU A (x86_64)";
    report.ratios = {ratio("engine-sample", 32, 2.0), ratio("engine-sample", 64, 1.85),
                     ratio("engine-sample", 256, 1.0), ratio("engine-live", 32, 9.0)};

    const gg::bench::Comparison c = gg::bench::compare(report, baseline);
    REQUIRE(c.found);
    CHECK(c.commit == "abc");
    CHECK(c.runs == 3);
    const std::vector<gg::bench::Verdict> &verdicts = c.verdicts;
    REQUIRE(verdicts.size() == 4);
    CHECK(verdicts[0].name == "engine-sample");
    CHECK(verdicts[0].blockSize == 32);
    CHECK(verdicts[0].ratio == 2.0);
    CHECK(verdicts[0].limit == Approx(1.89)); // CPU A's 1.8, not CPU B's 9.0
    CHECK(verdicts[0].regressed);
    CHECK(verdicts[1].blockSize == 64);
    CHECK(verdicts[1].ratio == 1.85);
    CHECK_FALSE(verdicts[1].regressed);
    // Missing from the machine's entry: reported, with no limit, and not regressed.
    CHECK(verdicts[2].blockSize == 256);
    CHECK(std::isnan(verdicts[2].limit));
    CHECK_FALSE(verdicts[2].regressed);
    CHECK(verdicts[3].name == "engine-live");
    CHECK(std::isnan(verdicts[3].limit));
    CHECK_FALSE(verdicts[3].regressed);

    // The other model gets its own limit.
    report.machine = "CPU B (x86_64)";
    const gg::bench::Comparison b = gg::bench::compare(report, baseline);
    REQUIRE(b.found);
    CHECK(b.commit == "def");
    REQUIRE(b.verdicts.size() == 4);
    CHECK(b.verdicts[0].limit == Approx(9.9));
    CHECK_FALSE(b.verdicts[0].regressed);
    CHECK(std::isnan(b.verdicts[1].limit));

    // A model the baseline doesn't have: no verdict at all.
    report.machine = "CPU C (arm64)";
    const gg::bench::Comparison none = gg::bench::compare(report, baseline);
    CHECK_FALSE(none.found);
    CHECK(none.verdicts.empty());

    // A malformed baseline is an error, even against a report with no ratios.
    const auto rejects = [](const nlohmann::json &bad) {
        CHECK_THROWS_AS(gg::bench::compare({}, bad), std::invalid_argument);
    };
    nlohmann::json bad = baseline;
    bad["schemaVersion"] = 1; // the per-leg format: accepted nowhere
    rejects(bad);
    bad = baseline;
    bad.erase("machines");
    rejects(bad);
    bad = baseline;
    bad["machines"][1]["machine"] = "CPU A (x86_64)"; // two entries for one model
    rejects(bad);
    bad = baseline;
    bad["machines"][0]["runs"] = 0;
    rejects(bad);
    bad = baseline;
    bad["machines"][0]["ratios"][0].erase("ratio");
    rejects(bad);
    bad = baseline;
    bad["machines"][1]["ratios"][0]["tolerance"] = -0.1;
    rejects(bad);
}

TEST_CASE("baseline entries the run lacks fail the verdict", "[bench]")
{
    // compare() accounts for every entry of the machine's baseline: one the run lacks is a
    // failure, named, never skipped (carry-over: "compare() must account for every baseline
    // entry"). A ratio the baseline lacks is still only "not in the baseline".
    const nlohmann::json baseline{
        {"schemaVersion", 2},
        {"leg", "dev"},
        {"machines",
         {{{"machine", "CPU A (x86_64)"},
           {"commit", "abc"},
           {"runs", 3},
           {"ratios",
            {baselineEntry("engine-sample", 32, 1.8, 0.05),
             baselineEntry("engine-sample", 64, 1.8, 0.05),
             baselineEntry("engine-live", 256, 2.0, 0.10)}}}}},
    };
    gg::bench::Report report;
    report.machine = "CPU A (x86_64)";
    report.ratios = {ratio("engine-sample", 32, 1.0), ratio("engine-full-pool", 32, 1.0)};

    const gg::bench::Comparison c = gg::bench::compare(report, baseline);
    REQUIRE(c.found);
    REQUIRE(c.verdicts.size() == 2);
    CHECK_FALSE(c.verdicts[0].regressed);
    CHECK(std::isnan(c.verdicts[1].limit)); // engine-full-pool: not in the baseline
    REQUIRE(c.missing.size() == 2);         // in the baseline's order
    CHECK(c.missing[0].name == "engine-sample");
    CHECK(c.missing[0].blockSize == 64);
    CHECK(std::isnan(c.missing[0].ratio));
    CHECK(c.missing[0].limit == Approx(1.89));
    CHECK(c.missing[1].name == "engine-live");
    CHECK(c.missing[1].blockSize == 256);
    CHECK(c.failures() == 2);

    // With every entry present and none regressed, nothing fails.
    report.ratios = {ratio("engine-sample", 32, 1.0), ratio("engine-sample", 64, 1.0),
                     ratio("engine-live", 256, 1.0)};
    const gg::bench::Comparison all = gg::bench::compare(report, baseline);
    CHECK(all.missing.empty());
    CHECK(all.failures() == 0);
    // A regression counts too.
    report.ratios[2].median = 3.0;
    CHECK(gg::bench::compare(report, baseline).failures() == 1);

    // A verdict over no ratios at all is an error, not "0 of 0".
    report.ratios.clear();
    CHECK_THROWS_AS(gg::bench::compare(report, baseline), std::invalid_argument);
    // Without an entry for the machine there is no verdict, so no error.
    report.machine = "CPU B (arm64)";
    CHECK_FALSE(gg::bench::compare(report, baseline).found);

    // An entry with no ratios, or with one (case, block size) twice, is a malformed baseline:
    // either would leave an entry unaccounted for.
    nlohmann::json bad = baseline;
    bad["machines"][0]["ratios"] = nlohmann::json::array();
    CHECK_THROWS_AS(gg::bench::checkBaseline(bad), std::invalid_argument);
    bad = baseline;
    bad["machines"][0]["ratios"].push_back(baselineEntry("engine-live", 256, 2.5, 0.10));
    CHECK_THROWS_AS(gg::bench::checkBaseline(bad), std::invalid_argument);
    CHECK_NOTHROW(gg::bench::checkBaseline(baseline));
}

TEST_CASE("the committed baselines are well formed", "[bench]")
{
    // CI checks each leg's run against its file, so each must pass checkBaseline(): an entry per
    // CPU model with every engine case at every block size, each once.
    const std::vector<std::string> legs{"linux-gcc13", "mac-universal", "windows-clangcl"};
    for (const std::string &leg : legs)
    {
        INFO(leg);
        std::ifstream in(std::filesystem::path{GG_BENCH_BASELINE_DIR} / (leg + ".json"),
                         std::ios::binary);
        REQUIRE(in);
        const nlohmann::json baseline = nlohmann::json::parse(in);
        CHECK_NOTHROW(gg::bench::checkBaseline(baseline));
        CHECK(baseline.at("leg").get<std::string>() == leg);
        for (const nlohmann::json &m : baseline.at("machines"))
        {
            INFO(m.at("machine").get<std::string>());
            // Exactly M1's 4 engine cases x 3 block sizes (a duplicate or a stray case would
            // pass a count of 12). engine-layers and engine-layers-full have no entries yet:
            // report-only until three full runs are merged in.
            std::set<std::pair<std::string, uint32_t>> found;
            for (const nlohmann::json &r : m.at("ratios"))
            {
                found.emplace(r.at("case").get<std::string>(), r.at("blockSize").get<uint32_t>());
            }
            std::set<std::pair<std::string, uint32_t>> expected;
            for (const char *name :
                 {"engine-sample", "engine-sample-eco", "engine-live", "engine-full-pool"})
            {
                for (const uint32_t block : {32u, 64u, 256u})
                {
                    expected.emplace(name, block);
                }
            }
            CHECK(m.at("ratios").size() == 12);
            CHECK(found == expected);
        }
    }
}

namespace
{
nlohmann::json benchRun(const std::string &machine, const std::string &commit,
                        std::vector<gg::bench::Ratio> ratios)
{
    gg::bench::Report report;
    report.machine = machine;
    report.options.commit = commit;
    report.ratios = std::move(ratios);
    return nlohmann::json::parse(gg::bench::toJson(report));
}
} // namespace

TEST_CASE("make baseline", "[bench]")
{
    // Two runs on CPU A, then one on CPU B, interleaved as CI artifacts might arrive.
    const std::vector<nlohmann::json> runs{
        benchRun("CPU A", "abc", {ratio("engine-sample", 32, 1.2), ratio("engine-live", 64, 2.0)}),
        benchRun("CPU B", "abc", {ratio("engine-sample", 32, 3.0)}),
        benchRun("CPU A", "def", {ratio("engine-sample", 32, 1.0), ratio("engine-live", 64, 2.0)}),
    };

    const nlohmann::json baseline = gg::bench::makeBaseline("linux-gcc13", runs);
    CHECK(baseline.at("schemaVersion").get<int>() == 2);
    CHECK(baseline.at("leg").get<std::string>() == "linux-gcc13");
    const nlohmann::json &machines = baseline.at("machines");
    REQUIRE(machines.size() == 2);

    const nlohmann::json &a = machines[0];
    CHECK(a.at("machine").get<std::string>() == "CPU A");
    CHECK(a.at("commit").get<std::string>() == "abc,def");
    CHECK(a.at("runs").get<int>() == 2);
    REQUIRE(a.at("ratios").size() == 2);
    CHECK(a["ratios"][0].at("case").get<std::string>() == "engine-sample");
    CHECK(a["ratios"][0].at("blockSize").get<uint32_t>() == 32);
    CHECK(a["ratios"][0].at("ratio").get<double>() == Approx(1.1));
    CHECK(a["ratios"][0].at("tolerance").get<double>() == Approx(0.4 / 1.1)); // above the floor
    CHECK(a["ratios"][1].at("case").get<std::string>() == "engine-live");
    CHECK(a["ratios"][1].at("ratio").get<double>() == 2.0);
    CHECK(a["ratios"][1].at("tolerance").get<double>() == 0.10); // no spread: the floor

    const nlohmann::json &b = machines[1];
    CHECK(b.at("machine").get<std::string>() == "CPU B");
    CHECK(b.at("commit").get<std::string>() == "abc");
    CHECK(b.at("runs").get<int>() == 1);
    REQUIRE(b.at("ratios").size() == 1);
    CHECK(b["ratios"][0].at("ratio").get<double>() == 3.0);
    CHECK(b["ratios"][0].at("tolerance").get<double>() == 0.10); // one run: the floor

    // The baseline it makes is one compare() accepts, per machine.
    gg::bench::Report report;
    report.machine = "CPU B";
    report.ratios = {ratio("engine-sample", 32, 3.2)};
    const gg::bench::Comparison c = gg::bench::compare(report, baseline);
    REQUIRE(c.found);
    REQUIRE(c.verdicts.size() == 1);
    CHECK(c.verdicts[0].limit == Approx(3.3));
    CHECK_FALSE(c.verdicts[0].regressed);

    // A ratio missing from one of a machine's runs is an error, as is a run with no machine, or
    // no run at all.
    std::vector<nlohmann::json> missing = runs;
    missing[2]["ratios"].erase(1);
    CHECK_THROWS_AS(gg::bench::makeBaseline("linux-gcc13", missing), std::invalid_argument);
    std::vector<nlohmann::json> anonymous = runs;
    anonymous[1].erase("machine");
    CHECK_THROWS_AS(gg::bench::makeBaseline("linux-gcc13", anonymous), std::invalid_argument);
    CHECK_THROWS_AS(gg::bench::makeBaseline("linux-gcc13", {}), std::invalid_argument);
}

TEST_CASE("merge baseline keeps the other models' entries", "[bench]")
{
    // A model added later needs only its own runs: the merge keeps every entry of the old file
    // the new runs don't replace (carry-over: "Adding a model to a baseline needs the old runs").
    const nlohmann::json old = gg::bench::makeBaseline(
        "linux-gcc13", {benchRun("CPU A", "old", {ratio("engine-sample", 32, 1.0)}),
                        benchRun("CPU B", "old", {ratio("engine-sample", 32, 2.0)}),
                        benchRun("CPU C", "old", {ratio("engine-sample", 32, 3.0)})});
    // New runs: CPU B again, and CPU D, never seen.
    const nlohmann::json made = gg::bench::makeBaseline(
        "linux-gcc13", {benchRun("CPU D", "new", {ratio("engine-sample", 32, 4.0)}),
                        benchRun("CPU B", "new", {ratio("engine-sample", 32, 2.5)}),
                        benchRun("CPU B", "new", {ratio("engine-sample", 32, 2.7)})});

    const nlohmann::json merged = gg::bench::mergeBaseline(old, made);
    CHECK(merged.at("schemaVersion").get<int>() == 2);
    CHECK(merged.at("leg").get<std::string>() == "linux-gcc13");
    const nlohmann::json &machines = merged.at("machines");
    REQUIRE(machines.size() == 4);
    // The old file's order, each replaced entry in its place; then the new models.
    CHECK(machines[0] == old["machines"][0]);  // CPU A, kept as it was
    CHECK(machines[1] == made["machines"][1]); // CPU B, from the new runs only
    CHECK(machines[1].at("runs").get<int>() == 2);
    CHECK(machines[1].at("commit").get<std::string>() == "new");
    CHECK(machines[1]["ratios"][0].at("ratio").get<double>() == Approx(2.6));
    CHECK(machines[2] == old["machines"][2]);  // CPU C, kept
    CHECK(machines[3] == made["machines"][0]); // CPU D, added
    CHECK_NOTHROW(gg::bench::checkBaseline(merged));

    // A merge into a different leg's file, or into a malformed one, is an error.
    nlohmann::json otherLeg = old;
    otherLeg["leg"] = "windows-clangcl";
    CHECK_THROWS_AS(gg::bench::mergeBaseline(otherLeg, made), std::invalid_argument);
    nlohmann::json bad = old;
    bad["machines"][0]["runs"] = 0;
    CHECK_THROWS_AS(gg::bench::mergeBaseline(bad, made), std::invalid_argument);
}

TEST_CASE("make baseline refuses what it can't write", "[bench]")
{
    // makeBaseline() checks what it makes as compare() would, so it never writes a file that
    // --baseline then rejects, such as a NaN tolerance written as null (carry-over:
    // "makeBaseline doesn't validate what it writes").
    const auto rejects = [](const std::vector<double> &medians) {
        std::vector<nlohmann::json> runs;
        for (const double m : medians)
        {
            runs.push_back(benchRun("CPU A", "abc", {ratio("engine-sample", 32, 1.0)}));
            runs.back()["ratios"][0]["median"] = m; // a NaN stays a NaN in a parsed json
        }
        INFO("medians " << medians.size() << ": " << medians.front() << ", ...");
        CHECK_THROWS_AS(gg::bench::makeBaseline("test", runs), std::invalid_argument);
    };
    rejects({std::nan("")});                                       // a NaN ratio
    rejects({1.0, std::nan(""), 1.0});                             // NaN among the runs
    rejects({std::numeric_limits<double>::infinity()});            // an infinite ratio
    rejects({0.0});                                                // ratio 0: tolerance 0/0
    rejects({-1.0});                                               // a negative ratio
    rejects({1e-300, 1e-300, std::numeric_limits<double>::max()}); // tolerance overflows to inf

    // Finite medians whose tolerance overflows pass the check on the runs; the check on the
    // baseline made catches it. So does an entry with no ratios.
    std::vector<nlohmann::json> overflow;
    for (const double m : {1e-300, 1e-300, std::numeric_limits<double>::max()})
    {
        overflow.push_back(benchRun("CPU A", "abc", {ratio("engine-sample", 32, m)}));
    }
    CHECK_THROWS_WITH(gg::bench::makeBaseline("test", overflow),
                      Catch::Matchers::ContainsSubstring("invalid baseline"));
    CHECK_THROWS_WITH(gg::bench::makeBaseline("test", {benchRun("CPU A", "abc", {})}),
                      Catch::Matchers::ContainsSubstring("has no ratios"));

    // The message names the run and the ratio.
    std::vector<nlohmann::json> runs{benchRun("CPU A", "abc", {ratio("engine-live", 64, 1.0)})};
    runs[0]["ratios"][0]["median"] = std::nan("");
    try
    {
        (void)gg::bench::makeBaseline("test", runs);
        FAIL("no exception");
    }
    catch (const std::invalid_argument &e)
    {
        const std::string what = e.what();
        INFO(what);
        CHECK(what.find("run 1") != std::string::npos);
        CHECK(what.find("engine-live at block 64") != std::string::npos);
    }
}

TEST_CASE("gg-bench --space prints a per-sample cost", "[bench]")
{
    // The output's shape, never its values: 2 rounds of 10 ms. (bench_space_cli runs
    // gg-bench --space --quick itself.)
    gg::bench::SpaceOptions options;
    options.runSeconds = 0.01;
    options.rounds = 2;
    const std::filesystem::path dir{GG_TEST_OUTPUT_DIR};
    std::filesystem::create_directories(dir);
    const std::filesystem::path jsonPath = dir / "bench_space_quick.json";
    std::filesystem::remove(jsonPath);

    std::string output;
    const int status = gg::bench::spaceMain(options, gg::test::utf8(jsonPath), output);
    INFO(output);
    CHECK(status == 0);
    CHECK(output.find("\nspace ") != std::string::npos); // the result's row
    CHECK(output.find("% core @48k") != std::string::npos);
    CHECK(output.find("settings: Diffusion 1, Amount 1, Size 0.5, Decay 1, Damping 0.3, "
                      "Modulation 0.25, Pre-delay 0, active") != std::string::npos);

    std::ifstream in(jsonPath, std::ios::binary);
    REQUIRE(in);
    const nlohmann::json j = nlohmann::json::parse(in);
    CHECK(j.at("benchmark").get<std::string>() == "space");
    CHECK(j.at("rounds").get<int>() == 2);
    CHECK(j.at("runSeconds").get<double>() == 0.01);
    const nlohmann::json &runs = j.at("runs");
    REQUIRE(runs.is_array());
    REQUIRE(runs.size() == 2);
    for (const nlohmann::json &run : runs)
    {
        CHECK(run.get<double>() > 0.0);
    }
    REQUIRE(j.at("nsPerSample").is_number());
    const double median = j.at("nsPerSample").get<double>();
    CHECK(median > 0.0);
    CHECK(j.at("min").get<double>() <= median);
    CHECK(j.at("iqr").get<double>() >= 0.0);
    CHECK(j.at("cpuPctOneCoreAt48k").get<double>() > 0.0);
    // Report-only: no baseline and no ratio.
    CHECK_FALSE(j.contains("ratios"));
}

namespace
{
/// FNV-1a over each float's bits, little end first, continuing from h.
uint64_t fnv1a(uint64_t h, const std::vector<float> &v)
{
    for (const float f : v)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        for (int byte = 0; byte < 4; ++byte)
        {
            h ^= (bits >> (8 * byte)) & 0xffu;
            h *= 0x100000001b3ull;
        }
    }
    return h;
}

/// A 64-bit LCG (Knuth's MMIX constants): the workload below depends on nothing but integer
/// arithmetic, so neither the engine's RNG nor a libm can move it.
struct Lcg
{
    uint64_t state;
    uint64_t next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return state;
    }
};
} // namespace

TEST_CASE("the reference kernel's output is pinned", "[bench]")
{
    // renderHermiteSimd is the CPU gate's yardstick: every committed baseline is a ratio against
    // its time, so what it computes must never move (carry-over: "The reference kernel isn't
    // self-contained"). Kernels.h holds its own copies of the window and the readers, apart from
    // the engine's; this pins their output, so a change to them fails here, not silently in the
    // ratios. The hashes were taken from the kernel as it was when the baselines were made, and
    // the scalar Hermite reference must match it bit for bit. The linear kernels are pinned too.
    constexpr int64_t kFrames = 48000;
    Lcg rng{0x1234};
    std::vector<float> srcL(kFrames), srcR(kFrames);
    for (int64_t i = 0; i < kFrames; ++i)
    {
        // [-0.5, 0.5): the top 24 bits, exact in float.
        srcL[static_cast<std::size_t>(i)] =
            static_cast<float>(static_cast<double>(rng.next() >> 40) / 16777216.0 - 0.5);
        srcR[static_cast<std::size_t>(i)] =
            static_cast<float>(static_cast<double>(rng.next() >> 40) / 16777216.0 - 0.5);
    }
    gg::bench::GrainBatch batch;
    batch.resize(64);
    for (std::size_t g = 0; g < batch.size(); ++g)
    {
        batch.pos[g] =
            static_cast<double>(rng.next() >> 11) / 9007199254740992.0 * kFrames; // [0, frames)
        batch.rate[g] = 0.5 + 1.5 * static_cast<double>(g) / 63.0;
        batch.envPhase[g] = static_cast<float>(g) / 64.0f;
        batch.envInc[g] = 1.0f / 4800.0f;
        batch.gain[g] = 0.25f + 0.01f * static_cast<float>(g);
    }
    // Grains across both ends of the source (the bounds-checked path) and one whose window ends
    // within the block.
    batch.pos[0] = -3.7;
    batch.pos[1] = 0.25;
    batch.pos[2] = kFrames - 100.6;
    batch.pos[3] = kFrames - 1.5;
    batch.envPhase[4] = 1.0f - 100.0f / 4800.0f;

    // Blocks of 256 and 37 samples (the SIMD steps, then a tail of 1) and of 1 (the tail alone).
    const auto hash = [&](gg::bench::Kernel kernel) {
        uint64_t h = 0xcbf29ce484222325ull;
        for (const uint32_t n : {256u, 37u, 1u})
        {
            std::vector<float> outL(n), outR(n);
            kernel(batch, srcL.data(), srcR.data(), kFrames, outL.data(), outR.data(), n);
            h = fnv1a(h, outL);
            h = fnv1a(h, outR);
        }
        return h;
    };
    // The window first: the kernels' output depends on libm's cos through it, so a changed
    // table names the library, and a changed kernel the code. detail::hann()'s 4,097 points.
    {
        const gg::bench::detail::HannTable &table = gg::bench::detail::hann();
        std::vector<float> points;
        for (uint32_t i = 0; i <= gg::bench::detail::HannTable::kSize; ++i)
        {
            points.push_back(
                table.at(static_cast<double>(i) / gg::bench::detail::HannTable::kSize));
        }
        REQUIRE(points.size() == 4097);
        CHECK(fnv1a(0xcbf29ce484222325ull, points) == 0x3688f1852a5b11f0ull);
    }
    CHECK(hash(gg::bench::renderHermiteSimd) == 0xcc738eaf2c70df7eull);
    CHECK(hash(gg::bench::renderHermiteScalar) == 0xcc738eaf2c70df7eull);
    CHECK(hash(gg::bench::renderLinearSimd) == 0x300874d5efa10a70ull);
    CHECK(hash(gg::bench::renderLinearScalar) == 0x300874d5efa10a70ull);
}

TEST_CASE("engine-sample-eco runs at the Eco tier", "[bench]")
{
    // Its timing alone (about 26 against 39 ns in Debug) doesn't prove it: every grain the case
    // admits, as its engine's event sink sees it with the tier the pool latches, must be Eco, and
    // the other cases' Normal (carry-over: "No test proves engine-sample-eco runs at Eco").
    // engine-sample is the control. engine-live is left out: it warms up for 16 s of audio, about
    // 5 s of a Debug build's time per block size, and is Normal by the same default.
    const std::vector<std::pair<std::string, uint32_t>> configurations{
        {"engine-sample-eco", 32}, {"engine-sample-eco", 64}, {"engine-sample-eco", 256},
        {"engine-sample", 32},     {"engine-sample", 64},     {"engine-sample", 256},
        {"engine-full-pool", 64},
    };
    for (const auto &[name, blockSize] : configurations)
    {
        INFO(name << " at block " << blockSize);
        const gg::bench::TierCounts tiers = gg::bench::admittedTiers(name, 50, blockSize);
        const bool eco = name == "engine-sample-eco";
        CHECK((eco ? tiers.eco : tiers.normal) >= 50); // at least one grain duration's worth
        CHECK((eco ? tiers.normal : tiers.eco) == 0);
    }
    CHECK_THROWS_AS(gg::bench::admittedTiers("reference", 50, 32), std::invalid_argument);
    CHECK_THROWS_AS(gg::bench::admittedTiers("no-such-case", 50, 32), std::invalid_argument);
    CHECK_THROWS_AS(gg::bench::admittedTiers("engine-sample", 50, 48), std::invalid_argument);
}

TEST_CASE("engine-layers admits its grains across four layers", "[bench]")
{
    // Spec §6.12: four engines, layers 0 to 3, each engine-sample's setup with 500 of the 2,000
    // grains, rendered through a SerialRunner and summed in layer order. After the warm-up (two
    // grain durations) each layer's engine sounds its share, all Normal, and every layer has
    // admitted at least one grain duration's worth.
    const gg::bench::TierCounts tiers = gg::bench::admittedTiers("engine-layers", 2000, 256);
    CHECK(tiers.normal >= 2000);
    CHECK(tiers.eco == 0);
    REQUIRE(tiers.activeGrains.size() == 4);
    uint32_t total = 0;
    for (std::size_t layer = 0; layer < tiers.activeGrains.size(); ++layer)
    {
        INFO("layer " << layer << ": " << tiers.activeGrains[layer] << " grains");
        CHECK(tiers.activeGrains[layer] >= 450);
        CHECK(tiers.activeGrains[layer] <= 550);
        total += tiers.activeGrains[layer];
    }
    CHECK(total >= 1996);
    CHECK(total <= 2004);

    // engine-layers-full: every layer gets all of the grains, so 4 x 50 here.
    const gg::bench::TierCounts full = gg::bench::admittedTiers("engine-layers-full", 50, 64);
    CHECK(full.normal >= 200);
    CHECK(full.eco == 0);
    REQUIRE(full.activeGrains.size() == 4);
    for (const uint32_t active : full.activeGrains)
    {
        CHECK(active >= 45);
        CHECK(active <= 55);
    }

    // A one-layer case reports its one engine.
    CHECK(gg::bench::admittedTiers("engine-sample", 50, 64).activeGrains.size() == 1);
}
