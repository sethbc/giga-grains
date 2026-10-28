// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PATCHSTATE_H
#define GG_CORE_PATCHSTATE_H

#include <cstdint>
#include <string>
#include <string_view>

#include "gg/core/ParamIds.h"

/// The patch both products save: the ten parameter values and the engine seed, as versioned XML.
/// It is the payload of the session state's `PTCH` chunk, byte for byte
/// (gg/core/SessionState.h); a state saved before the container (sub-projects 1 and 2) is this
/// XML alone. gg-core writes and reads it with sst-plugininfra's PatchBase and tinyxml, which
/// stay private to gg-core's sources (ruling M1-l): nothing that includes this header sees them.
/// Main thread only: both functions allocate.
///
/// The format is PatchBase's, on one line (broken here for reading), the params in slot order
/// and their CLAP IDs in decimal:
///
///     <patch id="org.sethbc.giga-grains.patch" version="1" name="Init"><params>
///       <p id="1" v="0.500000000000" bits="3f000000" /> …
///       <p id="65537" v="0.289676576853" bits="3e94507d" /> …</params>
///     <engine seed="0" /></patch>
///
/// with two additions to PatchBase's:
/// - `bits`, on each `<p>`: the value's float bits as 8 hex digits. PatchBase writes `v` with 12
///   fixed decimals, which doesn't hold a float below about 1.5e-5 exactly; with `bits`, every
///   value comes back bit for bit, so a saved and reloaded state renders identically. `bits` is
///   used only when it agrees with `v` (to `v`'s 12 decimals), so a hand-edited `v` still wins.
/// - `<engine seed>`: the engine seed, in decimal.
namespace gg::core
{
/// The state format's version: `version` in the XML. Older and newer states load through the
/// migration hooks.
inline constexpr uint32_t kPatchVersion = 1;

/// The state of these values and this seed. Both products write the same format: only the IDs
/// and values are saved, never the metadata.
std::string toStateXml(const ParamValues &values, uint64_t seed);

/// Loads a state into `mode`'s product, all or nothing.
/// - Returns false, and writes nothing, unless the state is well-formed XML with a <patch> root,
///   this format's id, an integer version and a <params> element.
/// - Otherwise it writes every value and the seed, and returns true:
///   - the values go through the migration hooks when the version differs (newer versions
///     included);
///   - a param the state doesn't mention takes `mode`'s default (paramMeta), and unknown param
///     IDs are ignored;
///   - values are clamped to their range, and a NaN takes `mode`'s default;
///   - a missing or malformed seed is 0, the seed new instances start with (spec §4).
/// - `migratedFromVersion`, if given, is written on success only: the state's version when the
///   migration hooks ran, 0 when it was already kPatchVersion.
bool fromStateXml(std::string_view xml, ParamValues &values, uint64_t &seed, Mode mode,
                  uint32_t *migratedFromVersion = nullptr);
} // namespace gg::core

#endif // GG_CORE_PATCHSTATE_H
