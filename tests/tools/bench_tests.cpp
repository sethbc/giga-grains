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
#include "Kernels.h"

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

    const nlohmann::json baseline = gg::bench::makeBaseline("linux-gcc12", runs);
    CHECK(baseline.at("schemaVersion").get<int>() == 2);
    CHECK(baseline.at("leg").get<std::string>() == "linux-gcc12");
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
    CHECK_THROWS_AS(gg::bench::makeBaseline("linux-gcc12", missing), std::invalid_argument);
    std::vector<nlohmann::json> anonymous = runs;
    anonymous[1].erase("machine");
    CHECK_THROWS_AS(gg::bench::makeBaseline("linux-gcc12", anonymous), std::invalid_argument);
    CHECK_THROWS_AS(gg::bench::makeBaseline("linux-gcc12", {}), std::invalid_argument);
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
    const int status = gg::bench::spaceMain(options, jsonPath.string(), output);
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
