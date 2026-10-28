// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_PATCH_H
#define GG_CORE_PATCH_H

// Internal to gg-core (ruling M1-l). This header brings in sst-plugininfra's PatchBase and
// tinyxml, which compile only with char8_t off (-fno-char8_t, MSVC /Zc:char8_t-): gg-core links
// them PRIVATE, so only gg-core's own sources may include it. Everything else uses
// gg/core/PatchState.h.

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "sst/plugininfra/patch-support/patch_base.h"

#include "gg/core/ParamIds.h"
#include "gg/core/PatchState.h"

namespace gg::core
{
/// One parameter's value, with a copy of its metadata (paramMeta, in the patch's product).
struct Param : sst::plugininfra::patch_support::ParamBase
{
    using ParamBase::ParamBase;
};

/// The patch: the ten parameter values, the engine seed and a name, and their versioned XML
/// state (sst-plugininfra's PatchBase, plus the `bits` attribute and the <engine seed> element
/// described in gg/core/PatchState.h). Main thread only: constructing, saving and loading
/// allocate.
///
/// The patch holds pointers to its own members (PatchBase's params and paramMap, and the state
/// hooks), so it can't be copied or moved.
struct Patch : sst::plugininfra::patch_support::PatchBase<Patch, Param>
{
    static constexpr uint32_t patchVersion = kPatchVersion;
    static constexpr const char id[] = "org.sethbc.giga-grains.patch";

    char name[256]{};
    /// Written as <engine seed="…"/> by the additionalToState hook. Missing or malformed, it loads
    /// as 0.
    uint64_t seed{0};
    /// The version the last successful fromState migrated from: 0 when the state was already at
    /// patchVersion.
    uint32_t migratedFromVersion{0};

    /// The params are pushed in ParamSlot order, with `mode`'s metadata (paramMeta) and at its
    /// defaults; the name is "Init".
    explicit Patch(Mode mode);
    Patch(const Patch &) = delete;
    Patch &operator=(const Patch &) = delete;

    /// `slot` must be a parameter: ParamSlot::Count is clamped to the last one.
    Param &param(ParamSlot slot) noexcept { return values[index(slot)]; }
    const Param &param(ParamSlot slot) const noexcept { return values[index(slot)]; }

    /// Loads a state written by toState, all or nothing, as fromStateXml describes. It hides
    /// PatchBase::fromState, which resets the patch before it parses and so can't leave it
    /// untouched when a load fails. dawExtraStateFrom, if set, is called as PatchBase calls it;
    /// onResetToInit isn't.
    bool fromState(const std::string &xml);

    void migratePatchFromVersion(uint32_t fromVersion) { migratedFromVersion = fromVersion; }
    float migrateParamValueFromVersion(Param *, float v, uint32_t) { return v; }

  private:
    static constexpr size_t index(ParamSlot slot) noexcept
    {
        return std::min(static_cast<size_t>(slot), kParamCount - 1);
    }
    static std::array<Param, kParamCount> defaultValues(Mode mode);

    Mode mode_;
    std::array<Param, kParamCount> values;
};
} // namespace gg::core

#endif // GG_CORE_PATCH_H
