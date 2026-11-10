// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_RENDER_PRESETSOURCE_H
#define GG_RENDER_PRESETSOURCE_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
/// - "tuning" (optional) is the render spec's: `{"scl": text}`, `{"sclFile": path}`, `{"kbm": …}`
///   or `{"kbmFile": …}` (RenderSpec.h's TuningSpec); a file is read by packPreset, relative to the
///   source's own directory, and TUNE keeps its bytes and name.
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
    /// The preset's Scala tuning (TUNE), or none.
    std::optional<TuningSpec> tuning;

    friend bool operator==(const PresetSource &, const PresetSource &) = default;
};

/// Parses and validates a preset source. On failure returns nullopt and sets `error` to a message
/// naming the offending key ("product must be fx or instrument", "tags[1] 'Loud' is not in the
/// vocabulary", "params.Foo is not a parameter of Giga Grains FX", "params.A Loop is a ring
/// parameter, which a preset doesn't set …" (gg::core::isRingParam: a preset load keeps the
/// instance's), "unknown key bogus"; a route's or a value's as the render spec's parser words it).
std::optional<PresetSource> parsePresetSource(const std::string &json, std::string &error);

/// The preset file of `source`, which parsePresetSource made (so every param name is its
/// product's): encodePreset(source.info, state), state being
/// initState(source.info.product) with each of the source's params set (as float, as the Processor
/// takes it) and its routes set on the factory twelve (routesWith in Render.h), and each of its
/// samples as MEDI version 2's content-relative reference (spec §6.6, §6.12): the file
/// gg::media::resolveContentPath finds for it under `contentDir`, the content folder, loaded
/// (gg::media::loadSample) for its entry's fingerprint, format, channels, rate and frames, with
/// `contentPath` the reference, its layer, and no absolute or sample-folder path. On failure
/// returns "" and sets `error`: a source naming a sample without `contentDir` ("the samples need
/// --content-dir"), a reference that isn't gg::media::isContentRelativePath ("isn't a valid
/// content-relative path"), one with no file under it ("not found"), or a file that doesn't
/// decode. A source never sets a ring parameter: parsePresetSource refuses one.
/// A source's tuning goes into the preset as TUNE (resolveTuningSpec in Render.h: `sourceDir` is
/// the base of its relative file paths), and a pair tuning-library refuses is an error.
std::string packPreset(const PresetSource &source,
                       const std::optional<std::filesystem::path> &contentDir, std::string &error,
                       const std::filesystem::path &sourceDir = {});

/// `gg-render --mutate-preset` (spec §11, the listening pack's Mutate ladder): the preset file
/// `bytes`, of either product, with its values moved by one Mutate press of the plugin's,
/// gg::core::mutate with seed 0, `counter`, `amount` and defaultMutateLocks of its values (so
/// its structure stays: gg/core/Mutate.h); its name, metadata, routes and samples unchanged,
/// written again by encodePreset. Deterministic: the same bytes, amount and counter give the same
/// file, and amount 0 gives `bytes` back. On failure returns "" and sets `error`: an amount
/// outside [0, 1] (or NaN), or bytes that aren't a preset file this version reads (a session
/// state among them).
std::string mutatePreset(std::string_view bytes, float amount, uint32_t counter,
                         std::string &error);
} // namespace gg::render

#endif // GG_RENDER_PRESETSOURCE_H
