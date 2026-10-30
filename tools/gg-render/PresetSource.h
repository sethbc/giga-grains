// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_RENDER_PRESETSOURCE_H
#define GG_RENDER_PRESETSOURCE_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "RenderSpec.h"
#include "gg/core/PresetInfo.h"

/// A preset source (M2 sub-project 5's spec §6.6): the JSON a factory or test preset is written
/// in, in gg-render's vocabulary, which `gg-render --pack-preset` compiles into a preset file
/// (gg/core/Preset.h):
///
/// ```json
/// {"product":"fx","name":"Glass Hold","author":"Giga Grains",
///  "description":"A held, glassy freeze that repitches to the keys",
///  "tags":["texture","freeze","midi"],
///  "params":{"Size":-2.0,"Density":4.0,"B Source":1,"Layer B On":1,"Reverb Amount":0.35},
///  "mod":[{"slot":13,"source":"LFO 1","target":"Position","targetLayer":"all","depth":0.1,
///          "curve":"Bipolar"}],
///  "samples":{"A":"Samples/Voices/alto-ah.flac"}}
/// ```
///
/// - "product" ("fx" or "instrument"), "name" (non-empty) and "tags" (an array of words of the
///   tag vocabulary, gg::core::presetTagVocabulary) are required; "author" and "description"
///   (strings) are optional, and so are the rest. No other key is allowed.
/// - "params" and "mod" are read by the render spec's own readers (RenderSpec.cpp), with the
///   product's parameter names, so a name the product displays is the only way to address a
///   parameter, and a wrong one is an error.
/// - "samples" maps layer letters ("A" to "D") to content-relative paths (UTF-8, non-empty),
///   resolved against --content-dir.
namespace gg::render
{
struct PresetSource
{
    /// The preset's metadata: product, name, author, description and tags.
    core::PresetInfo info;
    /// Base values by parameter name, in name order (as the render spec's reader gives them; each
    /// names its own slot); a parameter left out takes its default.
    std::vector<std::pair<std::string, double>> params;
    /// Routes on top of the product's factory twelve (gg::core::factoryRoutes), in the source's
    /// order.
    std::vector<RouteSpec> mod;
    /// Layers' samples by layer (0..3 for "A" to "D"), in layer order: content-relative paths.
    std::vector<std::pair<uint8_t, std::string>> samples;

    friend bool operator==(const PresetSource &, const PresetSource &) = default;
};

/// Parses and validates a preset source. On failure returns nullopt and sets `error` to a message
/// naming the offending key ("product must be fx or instrument", "tags[1] 'Loud' is not in the
/// vocabulary", "params.Foo is not a parameter of Giga Grains FX", "unknown key bogus"; a route's
/// or a value's as the render spec's parser words it).
std::optional<PresetSource> parsePresetSource(const std::string &json, std::string &error);

/// The preset file of `source`: encodePreset(source.info, state), state being
/// initState(source.info.product) with each of the source's params set (as float, as the Processor
/// takes it) and its routes set on the factory twelve (routesWith in Render.h). `contentDir`, the
/// folder the samples' content-relative paths are taken from, is for the samples alone. On
/// failure returns "" and sets `error`; until MEDI version 2 has content-relative references
/// (after sub-project 2 merges), a source naming any sample is the error "samples need MEDI
/// version 2 (after sub-project 2 merges)".
std::string packPreset(const PresetSource &source,
                       const std::optional<std::filesystem::path> &contentDir, std::string &error);
} // namespace gg::render

#endif // GG_RENDER_PRESETSOURCE_H
