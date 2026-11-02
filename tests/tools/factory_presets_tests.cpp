// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The factory preset sources, content/presets/<Product>/NN Name.json (M2 sub-project 5's spec
// §6.6, §10's `[build]` item): the counts pinned at their final values. Each product folder holds
// exactly 24 sources and nothing else, named "NN Name.json" with NN 01 to 24 (so unique, and the
// browser's and the VST3 program list's order), each parsing as its folder's product with the
// name its file gives; names are unique across both products; exactly 8 FX presets carry `midi`
// and no instrument preset does; every preset has a focus tag, no `voice` (the content has no
// voices) and at least one route of its own beyond the factory twelve.

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "PresetSource.h"
#include "gg/core/ParamIds.h"
#include "support/Utf8.h"

using gg::core::Mode;
using gg::test::utf8;

namespace
{
std::string readText(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool hasTag(const gg::render::PresetSource &source, const std::string &tag)
{
    return std::ranges::find(source.info.tags, tag) != source.info.tags.end();
}

/// The number of `product`'s sources tagged `tag`, after checking every file in its folder.
struct ProductCounts
{
    std::size_t presets{0};
    std::size_t midi{0};
    std::size_t texture{0};
    std::size_t playable{0};
    std::size_t rhythmic{0};
    std::size_t extreme{0};
    std::size_t multiSample{0}; ///< Sources naming two or more samples (layers).
};

ProductCounts checkProduct(const std::string &folder, Mode mode, std::set<std::string> &names)
{
    const std::regex fileName{R"(^([0-9]{2}) ([^/\\]+)\.json$)"};
    std::map<int, std::string> byNumber;
    ProductCounts counts;
    for (const auto &entry :
         std::filesystem::directory_iterator{std::filesystem::path{GG_FACTORY_PRESET_DIR} / folder})
    {
        const std::string file = utf8(entry.path().filename());
        INFO(folder << "/" << file);
        std::smatch match;
        REQUIRE(std::regex_match(file, match, fileName));
        const int number = std::stoi(match[1].str());
        CHECK(byNumber.emplace(number, file).second);

        std::string error;
        const auto source = gg::render::parsePresetSource(readText(entry.path()), error);
        INFO(error);
        REQUIRE(source.has_value());
        CHECK(source->info.product == mode);
        CHECK(source->info.name == match[2].str());
        CHECK(names.insert(source->info.name).second);
        CHECK((hasTag(*source, "texture") || hasTag(*source, "playable") ||
               hasTag(*source, "rhythmic") || hasTag(*source, "extreme")));
        CHECK_FALSE(hasTag(*source, "voice"));
        // A route of its own: slots 13 to 48, beyond the factory twelve.
        CHECK(std::ranges::any_of(source->mod, [](const auto &route) { return route.slot > 12; }));
        CHECK_FALSE(source->info.description.empty());
        ++counts.presets;
        counts.texture += hasTag(*source, "texture") ? 1 : 0;
        counts.playable += hasTag(*source, "playable") ? 1 : 0;
        counts.rhythmic += hasTag(*source, "rhythmic") ? 1 : 0;
        counts.extreme += hasTag(*source, "extreme") ? 1 : 0;
        counts.multiSample += source->samples.size() >= 2 ? 1 : 0;
        if (hasTag(*source, "midi"))
        {
            ++counts.midi;
        }
    }
    // NN runs 01 to 24, one file each.
    CHECK(byNumber.size() == 24);
    if (!byNumber.empty())
    {
        CHECK(byNumber.begin()->first == 1);
        CHECK(byNumber.rbegin()->first == static_cast<int>(byNumber.size()));
    }
    return counts;
}
} // namespace

TEST_CASE("the factory presets are 24 per product, named NN Name, 8 FX ones midi",
          "[build][presets]")
{
    std::set<std::string> names;
    const ProductCounts fx = checkProduct("Giga Grains FX", Mode::Fx, names);
    const ProductCounts inst = checkProduct("Giga Grains", Mode::Instrument, names);
    CHECK(fx.presets == 24);
    CHECK(fx.midi == 8);
    CHECK(inst.presets == 24);
    CHECK(inst.midi == 0);
    CHECK(names.size() == 48);
}

TEST_CASE("each product's 24 factory presets split 8 texture, 6 playable, 4 rhythmic, 6 extreme",
          "[build][presets]")
{
    // D1 (spec §6.6): the focus split, one focus tag each (checked above), pinned so that a later
    // edit can't drift it. Most instrument presets are multi-layer: at least 13 name two or more
    // samples.
    std::set<std::string> names;
    for (const auto &[folder, mode] :
         {std::pair<std::string, Mode>{"Giga Grains FX", Mode::Fx},
          std::pair<std::string, Mode>{"Giga Grains", Mode::Instrument}})
    {
        INFO(folder);
        const ProductCounts counts = checkProduct(folder, mode, names);
        CHECK(counts.texture == 8);
        CHECK(counts.playable == 6);
        CHECK(counts.rhythmic == 4);
        CHECK(counts.extreme == 6);
        if (mode == Mode::Instrument)
        {
            CHECK(counts.multiSample >= 13);
        }
    }
}
