// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The preset goldens' click scores (M2 sub-project 5's spec §10): one golden per factory preset,
// tests/golden/presets/cases/<fx|inst>-NN-<slug>.json. They don't join the click-injection table
// (click_injection_tests.cpp, which reads golden/cases/ alone): in its place, this cheap check
// reads each case's committed expected metrics and requires its click score under the limit
// (metricsVersion 2's 15, Tolerances::maxClickScore), its peak under -1 dBFS and no non-finite
// or denormal sample, so a regenerated golden can't creep up to the limit unnoticed. It also
// requires each case to have both expected files, and the cases and the factory sources
// (content/presets/<Product>/NN Name.json) to match one to one through each case's "preset" key.

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "gg/metrics/Metrics.h"
#include "support/Utf8.h"

using gg::test::utf8;

namespace
{
std::filesystem::path presetGoldenDir() { return std::filesystem::path{GG_GOLDEN_DIR} / "presets"; }

std::string readText(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    INFO(utf8(path));
    REQUIRE(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Every preset golden case's name, sorted: the glob the golden_preset_* CTests use.
std::vector<std::string> presetCases()
{
    std::vector<std::string> names;
    for (const auto &entry : std::filesystem::directory_iterator{presetGoldenDir() / "cases"})
    {
        if (entry.path().extension() == ".json")
        {
            names.push_back(utf8(entry.path().stem()));
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}
} // namespace

TEST_CASE("every preset golden's expected click score is under the limit", "[golden][presets]")
{
    const std::vector<std::string> cases = presetCases();
    // 24 per product (the counts test pins the sources; this pins the cases).
    REQUIRE(cases.size() == 48);
    const gg::metrics::Tolerances tolerances{};
    for (const auto &name : cases)
    {
        INFO(name);
        const auto metrics = gg::metrics::metricsFromJson(
            readText(presetGoldenDir() / "expected" / (name + ".metrics.json")));
        REQUIRE(metrics.has_value());
        CHECK(metrics->clickScore < tolerances.maxClickScore);
        CHECK(metrics->peakDb < -1.0);
        CHECK(metrics->nonFinite == 0);
        CHECK(metrics->denormals == 0);
        // Not silent: some 10 ms frame is louder than -40 dB.
        CHECK(std::ranges::any_of(metrics->rmsEnvelopeDb, [](double db) { return db > -40.0; }));
        CHECK(std::filesystem::is_regular_file(presetGoldenDir() / "expected" /
                                               (name + ".events.jsonl")));
    }
}

TEST_CASE("the preset goldens and the factory presets match one to one", "[golden][presets]")
{
    // Each case's "preset" is "presets/<Product>/NN Name.ggpreset" (resolved through
    // --media-dir, into which the fixture presets_built copies gg-presets' output), so it names
    // the source content/presets/<Product>/NN Name.json; its file name is <fx|inst>-NN-<slug>.
    std::set<std::string> named;
    for (const auto &name : presetCases())
    {
        INFO(name);
        const auto spec =
            nlohmann::json::parse(readText(presetGoldenDir() / "cases" / (name + ".json")));
        REQUIRE(spec.contains("preset"));
        const std::filesystem::path preset{spec["preset"].get<std::string>()};
        const std::string product = utf8(preset.parent_path().filename());
        const std::string prefix = product == "Giga Grains FX" ? "fx-" : "inst-";
        CHECK(utf8(preset.parent_path().parent_path()) == "presets");
        CHECK(preset.extension() == ".ggpreset");
        CHECK((product == "Giga Grains" || product == "Giga Grains FX"));
        CHECK(spec["mode"] == (product == "Giga Grains FX" ? "fx" : "instrument"));
        // The case's NN is the preset's.
        CHECK(name.starts_with(prefix + utf8(preset.stem()).substr(0, 2) + "-"));
        const auto source = std::filesystem::path{GG_FACTORY_PRESET_DIR} / product /
                            (utf8(preset.stem()) + ".json");
        INFO(utf8(source));
        CHECK(std::filesystem::is_regular_file(source));
        CHECK(named.insert(utf8(source)).second);
    }
    CHECK(named.size() == 48);
}
