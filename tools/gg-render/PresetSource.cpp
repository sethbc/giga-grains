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
#include "gg/core/Mutate.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Preset.h"
#include "gg/core/PresetInfo.h"
#include "gg/core/SessionState.h"
#include "gg/core/Tuning.h"
#include "gg/media/ContentPaths.h"
#include "gg/media/Sample.h"

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
        {"product", "name", "author", "description", "tags", "params", "mod", "samples", "tuning"});
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
                const auto slot = paramSlotNamedIn(item.key(), *product);
                if (!slot)
                {
                    throw SpecError(fmt::format("params.{} is not a parameter of {}", item.key(),
                                                productName(*product)));
                }
                // A preset load keeps the instance's ring parameters (the program manager's
                // ruling) and its MIDI settings (M3 sub-project 1), so a source setting one would
                // mean nothing.
                if (gg::core::isMidiSetting(*slot))
                {
                    throw SpecError(fmt::format(
                        "params.{} is a MIDI setting, which a preset doesn't set (a load keeps "
                        "the instance's)",
                        item.key()));
                }
                if (gg::core::isRingParam(*slot))
                {
                    throw SpecError(fmt::format(
                        "params.{} is a ring parameter, which a preset doesn't set (a load keeps "
                        "the instance's)",
                        item.key()));
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
    if (o.has("tuning"))
    {
        source.tuning = readTuning(o.member("tuning"));
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
                       const std::optional<std::filesystem::path> &contentDir, std::string &error,
                       const std::filesystem::path &sourceDir)
{
    error.clear();
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
    // The tuning's texts, as TUNE keeps them; tuning-library must accept the pair (a preset that
    // never loads is no preset).
    if (source.tuning)
    {
        try
        {
            gg::core::TuningFiles files =
                resolveTuningSpec(*source.tuning, sourceDir, std::nullopt);
            if (const auto table = gg::core::tuningFromScala(files.scl, files.kbm); !table)
            {
                error = fmt::format("tuning: {}", table.error());
                return {};
            }
            state.tuning = std::move(files);
        }
        catch (const BadInput &e)
        {
            error = e.what();
            return {};
        }
    }
    // Each sample by its content-relative reference (MEDI version 2's, spec §6.6, §6.12): the
    // file under contentDir, checked and read for its fingerprint, format, channels, rate and
    // frames; no absolute or sample-folder path, which would be this machine's.
    for (const auto &[layer, reference] : source.samples)
    {
        const char letter = static_cast<char>('A' + layer);
        if (!contentDir)
        {
            error =
                fmt::format("the samples need --content-dir (layer {}'s is {})", letter, reference);
            return {};
        }
        if (!gg::media::isContentRelativePath(reference))
        {
            error =
                fmt::format("layer {}'s sample {} isn't a valid content-relative path (relative, "
                            "with / separators, no .. segment, no backslash)",
                            letter, reference);
            return {};
        }
        const std::filesystem::path roots[] = {*contentDir};
        const auto file = gg::media::resolveContentPath(reference, roots);
        if (!file)
        {
            error = fmt::format("layer {}'s sample {} was not found under --content-dir {}", letter,
                                reference, utf8FromPath(*contentDir));
            return {};
        }
        gg::media::LoadedSample loaded =
            gg::media::loadSample(*file, {}, gg::media::kDefaultDecodeBound);
        if (!loaded.decoded)
        {
            error = fmt::format("layer {}'s sample {}: {}", letter, reference, loaded.error);
            return {};
        }
        gg::core::MediaEntry entry = std::move(loaded.entry);
        entry.absolutePath.clear();
        entry.relativePath.clear();
        entry.contentPath = reference;
        entry.layer = layer;
        state.samples[layer] = std::move(entry);
    }
    return gg::core::encodePreset(source.info, state);
}

std::string mutatePreset(std::string_view bytes, float amount, uint32_t counter, std::string &error)
{
    error.clear();
    if (!(amount >= 0.0f && amount <= 1.0f))
    {
        error = fmt::format("the amount must be from 0 to 1, not {}", amount);
        return {};
    }
    gg::core::PresetInfo info;
    if (!gg::core::presetInfoOf(bytes, info))
    {
        error = "it isn't a preset file this version reads";
        return {};
    }
    const gg::core::Mode mode = info.product;
    gg::core::SessionState state;
    if (!gg::core::readPreset(bytes, mode, info, state))
    {
        error = "the preset doesn't load";
        return {};
    }
    const gg::core::LockSet locks = gg::core::defaultMutateLocks(state.values, mode);
    gg::core::mutate(state.values, locks, amount, 0, counter, mode, state.values);
    return gg::core::encodePreset(info, state);
}
} // namespace gg::render
