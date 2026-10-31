// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PRESET_H
#define GG_CORE_PRESET_H

#include <string>
#include <string_view>

#include "gg/core/ParamIds.h"
#include "gg/core/PresetInfo.h"
#include "gg/core/SessionState.h"

/// The preset file (M2 sub-project 5's spec §6.1): a session state's container
/// (gg/core/SessionState.h), PTCH then MEDI and never AUDI, whose PTCH carries the preset's name
/// and its <preset> element (gg/core/PatchState.h). A state without the element is a session,
/// not a preset. Main thread only: every function here allocates.
namespace gg::core
{
/// The preset file of `state` with `info`: encodeState of `state` with its name `info.name`, its
/// preset `info` and its seed 0 (a preset's seed is ignored on load). The sample is `state.sample`
/// if any; its manifest, MEDI, is written as encodeState writes it.
std::string encodePreset(const PresetInfo &info, const SessionState &state);

/// Loads a preset file for `mode`'s product, all or nothing: false, and `info` and `out`
/// untouched, when `bytes` is longer than kMaxStateBytes, when decodeState fails on it, when its
/// PTCH has no <preset> element (a session state), or when the preset's product isn't `mode`.
/// Otherwise `out` is the state decodeState reads (its name and preset included) and `info` its
/// preset, the name included.
bool readPreset(std::string_view bytes, Mode mode, PresetInfo &info, SessionState &out);

/// A preset file's metadata alone, of either product, for discovery and the browser: the
/// container's PTCH found as decodeState finds it, under the same length cap and nesting
/// pre-scan, and only its root's `name` and its <preset> element read; no values are built, so
/// values that wouldn't load don't stop it. False, and `info` untouched, when the container or
/// PTCH isn't readable, its root isn't this format's <patch>, or there is no <preset> element
/// with a known product. The MEDI chunk isn't read: a preset whose MEDI is malformed is listed
/// all the same, and fails only when it is loaded (readPreset).
bool presetInfoOf(std::string_view bytes, PresetInfo &info);

/// The INIT state of `mode`'s product: every parameter at its default (paramMeta),
/// factoryRoutes(mode), seed 0, no sample, the name "Init" and no preset.
SessionState initState(Mode mode);
} // namespace gg::core

#endif // GG_CORE_PRESET_H
