// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PATCH_H
#define GG_CORE_PATCH_H

#include <array>
#include <cstdint>
#include <string>

#include "sst/plugininfra/patch-support/patch_base.h"

#include "gg/core/ParamIds.h"

namespace gg::core
{
/// One parameter's value, with a copy of its metadata (paramMeta).
struct Param : sst::plugininfra::patch_support::ParamBase
{
    using ParamBase::ParamBase;
};

/// The patch: the ten parameter values, the engine seed and a name, and their versioned XML
/// state (sst-plugininfra's PatchBase). Main thread only: constructing, saving and loading
/// allocate.
///
/// The state is PatchBase's, on one line (broken here for reading), the params in slot order and
/// their IDs in decimal:
///
///     <patch id="org.sethbc.giga-grains.patch" version="1" name="Init"><params>
///       <p id="1" v="0.500000000000" bits="3f000000" /> …
///       <p id="65537" v="0.289676576853" bits="3e94507d" /> …</params>
///     <engine seed="0" /></patch>
///
/// with two additions:
/// - `bits`, on each `<p>`: the value's float bits as 8 hex digits. PatchBase writes `v` with 12
///   fixed decimals, which doesn't hold a float below about 1.5e-5 exactly; with `bits`, every
///   value comes back bit for bit, so a saved and reloaded state renders identically. `bits` is
///   used only when it agrees with `v` (to `v`'s 12 decimals), so a hand-edited `v` still wins.
/// - `<engine seed>`: the engine seed, in decimal. Missing or malformed, it loads as 0, the seed
///   new instances start with (spec §4).
///
/// The patch holds pointers to its own members (PatchBase's params and paramMap, and the state
/// hooks), so it can't be copied or moved.
struct Patch : sst::plugininfra::patch_support::PatchBase<Patch, Param>
{
    static constexpr uint32_t patchVersion = 1;
    static constexpr const char id[] = "org.sethbc.giga-grains.patch";

    char name[256]{};
    /// Written as <engine seed="…"/> by the additionalToState hook.
    uint64_t seed{0};
    /// The version the last successful fromState migrated from: 0 when the state was already at
    /// patchVersion.
    uint32_t migratedFromVersion{0};

    /// The params are pushed in ParamSlot order, at their defaults; the name is "Init".
    Patch();
    Patch(const Patch &) = delete;
    Patch &operator=(const Patch &) = delete;

    Param &param(ParamSlot slot) noexcept { return values[static_cast<size_t>(slot)]; }
    const Param &param(ParamSlot slot) const noexcept { return values[static_cast<size_t>(slot)]; }

    /// Loads a state written by toState, all or nothing. It hides PatchBase::fromState, which
    /// resets the patch before it parses and so can't leave it untouched when a load fails.
    /// - Returns false, and changes nothing, unless the state is well-formed XML with a <patch>
    ///   root, this patch's id, an integer version and a <params> element.
    /// - Otherwise it loads, through the migration hooks when the version differs (newer
    ///   versions included), and returns true. Params the state doesn't mention take their
    ///   defaults; unknown param IDs are ignored; values are clamped to their range, and a NaN
    ///   takes the default.
    /// - dawExtraStateFrom, if set, is called as PatchBase calls it; onResetToInit isn't.
    bool fromState(const std::string &xml);

    void migratePatchFromVersion(uint32_t fromVersion) { migratedFromVersion = fromVersion; }
    float migrateParamValueFromVersion(Param *, float v, uint32_t) { return v; }

  private:
    static std::array<Param, kParamCount> defaultValues();

    std::array<Param, kParamCount> values;
};
} // namespace gg::core

#endif // GG_CORE_PATCH_H
