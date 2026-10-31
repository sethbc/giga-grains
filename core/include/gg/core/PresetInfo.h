// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PRESETINFO_H
#define GG_CORE_PRESETINFO_H

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gg/core/ParamIds.h"

/// A preset's metadata (M2 sub-project 5's spec §6.1): what makes a session state a preset,
/// saved inside its patch (gg/core/PatchState.h) as the root's `name` and a <preset> element. The
/// file and its readers are gg/core/Preset.h's.
namespace gg::core
{
struct PresetInfo
{
    /// The patch's name, the root's `name` attribute: at most 255 bytes, cut at a UTF-8
    /// boundary when written.
    std::string name;
    /// The plugin the preset is for: `product`, modeName's word.
    Mode product{Mode::Fx};
    std::string author;
    std::string description;
    /// Words of presetTagVocabulary, in the order saved.
    std::vector<std::string> tags;

    friend bool operator==(const PresetInfo &, const PresetInfo &) = default;
};

/// A preset file's extension.
inline constexpr std::string_view kPresetExtension = ".ggpreset";

/// The load key of the INIT preset (the plugin's defaults), which has no file.
inline constexpr std::string_view kInitLoadKey = "init";

/// The tag vocabulary (spec §6.7), lowercase ASCII with hyphens: the four focus tags (texture,
/// playable, rhythmic, extreme), then the character tags, then the use tags.
std::span<const std::string_view> presetTagVocabulary() noexcept;

/// Whether `word` is in presetTagVocabulary, exactly (case included).
bool isPresetTag(std::string_view word) noexcept;

/// `mode`'s word, as <preset product> saves it: "fx" or "instrument".
const char *modeName(Mode mode) noexcept;

/// The product modeName names `name`; nothing for any other text.
std::optional<Mode> modeNamed(std::string_view name) noexcept;
} // namespace gg::core

#endif // GG_CORE_PRESETINFO_H
