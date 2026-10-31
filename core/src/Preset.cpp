// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/Preset.h"

#include <algorithm>
#include <array>
#include <utility>

#include "PatchXml.h"
#include "StateChunks.h"
#include "gg/core/ModMatrix.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

namespace gg::core
{
namespace
{
// Spec §6.7's vocabulary: the four focus tags (texture to extreme), the twenty character tags
// (pad to dry), then the four use tags (midi to sidechain).
constexpr std::array<std::string_view, 28> kTagVocabulary{
    "texture", "playable", "rhythmic", "extreme", "pad",        "drone",      "freeze",
    "smear",   "stretch",  "glitch",   "keys",    "pluck",      "percussive", "voice",
    "strings", "winds",    "metal",    "noise",   "dark",       "bright",     "wide",
    "mono",    "reverb",   "dry",      "midi",    "live-input", "sample",     "sidechain"};
} // namespace

std::span<const std::string_view> presetTagVocabulary() noexcept { return kTagVocabulary; }

bool isPresetTag(std::string_view word) noexcept
{
    return std::find(kTagVocabulary.begin(), kTagVocabulary.end(), word) != kTagVocabulary.end();
}

const char *modeName(Mode mode) noexcept { return mode == Mode::Instrument ? "instrument" : "fx"; }

std::optional<Mode> modeNamed(std::string_view name) noexcept
{
    for (const Mode mode : {Mode::Instrument, Mode::Fx})
    {
        if (name == modeName(mode))
        {
            return mode;
        }
    }
    return std::nullopt;
}

std::string encodePreset(const PresetInfo &info, const SessionState &state)
{
    SessionState preset = state;
    preset.name = info.name;
    preset.preset = info;
    preset.seed = 0;       // ignored on load: the instance keeps its own (spec §6.1, D6)
    preset.edited = false; // a session's mark alone (D-Q2)
    return encodeState(preset);
}

bool readPreset(std::string_view bytes, Mode mode, PresetInfo &info, SessionState &out)
{
    SessionState loaded;
    if (bytes.size() > kMaxStateBytes || !decodeState(bytes, mode, loaded) || !loaded.preset ||
        loaded.preset->product != mode)
    {
        return false;
    }
    info = *loaded.preset;
    out = std::move(loaded);
    return true;
}

bool presetInfoOf(std::string_view bytes, PresetInfo &info)
{
    if (bytes.size() > kMaxStateBytes)
    {
        return false;
    }
    // A pre-container state is PTCH alone, as decodeState reads it.
    if (bytes.starts_with('<'))
    {
        return presetInfoOfXml(bytes, info);
    }
    std::string_view patch;
    std::string_view media;
    return findStateChunks(bytes, patch, media) && patch.find('\0') == std::string_view::npos &&
           presetInfoOfXml(patch, info);
}

SessionState initState(Mode mode)
{
    SessionState state;
    for (size_t i = 0; i < kParamCount; ++i)
    {
        state.values[i] = paramMeta(static_cast<ParamSlot>(i), mode).defaultVal;
    }
    state.routes = factoryRoutes(mode);
    return state;
}
} // namespace gg::core
