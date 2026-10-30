// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PATCHSTATE_H
#define GG_CORE_PATCHSTATE_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/PresetInfo.h"

/// The patch both products save: every parameter's value, the engine seed, the mod matrix's
/// routes, a name and, for a preset, its metadata, as versioned XML.
/// It is the payload of the session state's `PTCH` chunk, byte for byte
/// (gg/core/SessionState.h); a state saved before the container (sub-projects 1 and 2) is this
/// XML alone. gg-core writes and reads it with sst-plugininfra's PatchBase and tinyxml, which
/// stay private to gg-core's sources (ruling M1-l): nothing that includes this header sees them.
/// Main thread only: both functions allocate.
///
/// The format is PatchBase's, on one line (broken here for reading), the params in slot order
/// and their CLAP IDs in decimal:
///
///     <patch id="org.sethbc.giga-grains.patch" version="2" name="Init"><params>
///       <p id="1" v="0.500000000000" bits="3f000000" /> …
///       <p id="65537" v="0.289676576853" bits="3e94507d" /> …</params>
///     <engine seed="0" /><mod>
///       <route slot="1" source="Macro 1" sourceLayer="same" target="65538" targetLayer="all"
///         depth="3.000000000000" bits="40400000" curve="Bipolar" /> …</mod>
///     <preset product="fx" author="Giga Grains" description="A held, glassy freeze"
///       tags="texture freeze midi" /></patch>
///
/// with four additions to PatchBase's:
/// - `bits`, on each `<p>`: the value's float bits as 8 hex digits. PatchBase writes `v` with 12
///   fixed decimals, which doesn't hold a float below about 1.5e-5 exactly; with `bits`, every
///   value comes back bit for bit, so a saved and reloaded state renders identically. `bits` is
///   used only when it agrees with `v` (to `v`'s 12 decimals), so a hand-edited `v` still wins.
/// - `<engine seed>`: the engine seed, in decimal.
/// - `<mod>` (since version 2, M2 sub-project 1's spec §6.10): the mod matrix's routes
///   (gg/core/ModMatrix.h), one `<route>` per slot in use, in slot order; an empty slot (source
///   None) is left out. Its attributes:
///   - `slot`: 1 to 48 (kModSlots), in decimal;
///   - `source` and `curve`: their stable identifiers (modSourceName, modCurveName);
///   - `target`: the target slot's CLAP ID in decimal, as `<p id>` writes it (layer A's for a
///     per-layer kind, isValidTarget), never its name: IDs are frozen, names aren't;
///   - `sourceLayer`: `A` to `D`, or `same` (kSameLayer); `targetLayer`: `A` to `D`, or `all`
///     (kAllLayers);
///   - `depth` and `bits`: the depth as `<p>`'s `v` and `bits` write a value, and read back the
///     same way (exact when `bits` agrees with `depth`).
/// - `<preset>` (M2 sub-project 5's spec §6.1): a preset's metadata (gg/core/PresetInfo.h),
///   after `<mod>`, only in a preset. What makes a state a preset; a reader that doesn't know it
///   ignores it, and it is read at any version. Its attributes:
///   - `product`: modeName's word, `fx` or `instrument`; any other reads as no preset;
///   - `author` and `description`: their bytes to the first NUL, escaped as `name` is;
///   - `tags`: the tags in their order, separated by single spaces; read back split on spaces,
///     dropping every word that isn't isPresetTag (never a failure).
///   A missing attribute reads empty. The preset's name is the root's.
///
/// The root's `name` is PatchBase's: at most 255 bytes, a longer one cut on write and on read to
/// its first 255 bytes, backed up to a whole UTF-8 character; written with every `&` as `&#x26;`
/// (tinyxml would otherwise read a literal `&#x41;` back as `A`), so any text but NUL comes back
/// unchanged. A state without it reads as "Init".
///
/// Version 1 is M1's: the 28 M1 params, no `<mod>`. Version 2 adds M2's params (each absent from
/// an older state takes its default) and `<mod>`.
namespace gg::core
{
/// The state format's version: `version` in the XML. Older and newer states load through the
/// migration hooks.
inline constexpr uint32_t kPatchVersion = 2;

/// Whether toStateXml writes `route`, so that it reads back as itself: a source other than None
/// (and in range), a valid curve, a valid target (isValidTarget), a sourceLayer of 0 to 3 or
/// kSameLayer, a targetLayer of 0 to 3 or kAllLayers, and a finite depth. Any other route is
/// left out, and so loads as an empty slot.
bool isSavableRoute(const ModRoute &route) noexcept;

/// The state of these values, this seed, these routes and this name (patchName's cut of it, as
/// above), with `preset`'s <preset> element when it is given (its `name` aside: the root's is
/// the preset's). Both products write the same format: only the IDs and values are saved, never
/// the metadata.
std::string toStateXml(const ParamValues &values, uint64_t seed, const ModRoutes &routes,
                       std::string_view name = "Init", const PresetInfo *preset = nullptr);

/// Loads a state into `mode`'s product, all or nothing.
/// - Returns false, and writes nothing, unless the state is well-formed XML with a <patch> root,
///   this format's id, an integer version and a <params> element.
/// - Otherwise it writes every value, the seed and the routes, and returns true:
///   - the values go through the migration hooks when the version differs (newer versions
///     included);
///   - a param the state doesn't mention takes `mode`'s default (paramMeta), and unknown param
///     IDs are ignored;
///   - values are clamped to their range, and a NaN takes `mode`'s default;
///   - a missing or malformed seed is 0, the seed new instances start with (spec §4);
///   - at version 2 and later, the routes are `<mod>`'s; a missing `<mod>` is 48 empty slots. A
///     `<route>` is left an empty slot, and the load goes on, when its `slot` isn't 1 to 48, its
///     `source` or `curve` isn't a known identifier, its `target` isn't a decimal CLAP ID of a
///     valid target, a layer attribute isn't one of those above, or its `depth` isn't a finite
///     number. Of two routes naming one slot, the later wins (an empty slot, if it is the
///     malformed one). Unknown attributes, and elements other than `<route>` inside `<mod>`, are
///     ignored (a newer version's).
/// - **The migration from version 1** (M1's state; spec §6.10): any `<mod>` is ignored and the
///   routes are factoryRoutes(mode), slots 1 to 12. In the instrument only, M1's Macro 7 (Macro
///   Stereo) is folded into Stereo's base, `clamp(stereo + 0.5f * (2.0f * v - 1.0f), 0, 1)` with
///   `v` the saved Macro 7 (or M1's default for it, 0.5, when the state has none or it is NaN),
///   both clamped to 0–1 first as M1 loaded them, and Macro 7 (Macro Feedback since version 2) is
///   set to 0, its rest: the render is M1's. The FX's Macro 7 was Macro Feedback already, and is
///   untouched.
/// - `migratedFromVersion`, if given, is written on success only: the state's version when the
///   migration hooks ran, 0 when it was already kPatchVersion.
/// - `name` and `preset`, if given, are written on success only: the root's name ("Init" when it
///   has none), and the <preset> element's metadata with that name, or nothing when there is no
///   <preset> or its `product` isn't known. Neither changes how the rest loads.
bool fromStateXml(std::string_view xml, ParamValues &values, uint64_t &seed, ModRoutes &routes,
                  Mode mode, uint32_t *migratedFromVersion = nullptr, std::string *name = nullptr,
                  std::optional<PresetInfo> *preset = nullptr);
} // namespace gg::core

#endif // GG_CORE_PATCHSTATE_H
