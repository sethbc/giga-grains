// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "PresetSource.h"

#include <cassert>
#include <cstddef>
#include <exception>
#include <string_view>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "Render.h"
#include "SpecReaders.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Preset.h"
#include "gg/core/SessionState.h"

namespace gg::render
{
namespace
{
using namespace detail;

/// An optional string key, "" when absent.
std::string optionalString(const Object &o, std::string_view key)
{
    return o.has(key) ? o.string(key) : std::string{};
}

std::vector<std::string> readTags(const json &j)
{
    if (!j.is_array())
    {
        throw SpecError("tags must be a JSON array of strings");
    }
    std::vector<std::string> tags;
    for (std::size_t i = 0; i < j.size(); ++i)
    {
        if (!j[i].is_string())
        {
            throw SpecError("tags must be a JSON array of strings");
        }
        std::string tag = j[i].get<std::string>();
        if (!gg::core::isPresetTag(tag))
        {
            throw SpecError(fmt::format("tags[{}] '{}' is not in the vocabulary", i, tag));
        }
        tags.push_back(std::move(tag));
    }
    return tags;
}

/// "samples": a content-relative path per layer letter, in layer order.
std::vector<std::pair<uint8_t, std::string>> readSamplePaths(const json &j)
{
    const Object o(j, "samples", "samples.", {"A", "B", "C", "D"});
    static_assert(gg::core::kLayers == 4, "the letters above are the layers'");
    std::vector<std::pair<uint8_t, std::string>> samples;
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        const std::string letter(1, static_cast<char>('A' + layer));
        if (o.has(letter))
        {
            std::string path = o.string(letter);
            if (path.empty())
            {
                throw SpecError(fmt::format("samples.{} must not be empty", letter));
            }
            samples.emplace_back(*layerOfLetter(letter), std::move(path));
        }
    }
    return samples;
}

PresetSource readSource(const json &j)
{
    const Object o(
        j, "the preset source", "",
        {"product", "name", "author", "description", "tags", "params", "mod", "samples"});
    PresetSource source;
    const auto product = gg::core::modeNamed(o.string("product"));
    if (!product)
    {
        throw SpecError("product must be fx or instrument");
    }
    source.info.product = *product;
    source.info.name = o.string("name");
    if (source.info.name.empty())
    {
        throw SpecError("name must not be empty");
    }
    source.info.author = optionalString(o, "author");
    source.info.description = optionalString(o, "description");
    source.info.tags = readTags(o.member("tags"));
    if (o.has("params"))
    {
        // Every name the product's first, in the preset's wording; then the values, as a render
        // spec's.
        const json &params = o.member("params");
        if (params.is_object())
        {
            for (const auto &item : params.items())
            {
                if (!paramSlotNamedIn(item.key(), *product))
                {
                    throw SpecError(fmt::format("params.{} is not a parameter of {}", item.key(),
                                                productName(*product)));
                }
            }
        }
        source.params = readParams(params, *product);
    }
    if (o.has("mod"))
    {
        source.mod = readMod(o.member("mod"), *product);
    }
    if (o.has("samples"))
    {
        source.samples = readSamplePaths(o.member("samples"));
    }
    return source;
}
} // namespace

std::optional<PresetSource> parsePresetSource(const std::string &text, std::string &error)
{
    error.clear();
    try
    {
        return readSource(json::parse(text));
    }
    catch (const json::exception &e)
    {
        error = fmt::format("not valid JSON: {}", e.what());
    }
    catch (const SpecError &e)
    {
        error = e.what();
    }
    return std::nullopt;
}

std::string packPreset(const PresetSource &source,
                       const std::optional<std::filesystem::path> &contentDir, std::string &error)
{
    error.clear();
    // Task 14 resolves the samples against contentDir into MEDI version 2's content-relative
    // entries (spec §6.6, §6.12).
    static_cast<void>(contentDir);
    if (!source.samples.empty())
    {
        error = "samples need MEDI version 2 (after sub-project 2 merges)";
        return {};
    }
    const gg::core::Mode mode = source.info.product;
    gg::core::SessionState state = gg::core::initState(mode);
    for (const auto &[name, value] : source.params)
    {
        // parsePresetSource refused any name that isn't the product's (packPreset's precondition),
        // so this finds every one.
        const auto slot = paramSlotNamedIn(name, mode);
        assert(slot.has_value());
        state.values[static_cast<std::size_t>(slot.value_or(gg::core::ParamSlot{}))] =
            static_cast<float>(value);
    }
    try
    {
        state.routes = routesWith(state.routes, source.mod);
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return {};
    }
    return gg::core::encodePreset(source.info, state);
}
} // namespace gg::render
