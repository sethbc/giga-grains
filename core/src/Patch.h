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
#include <optional>
#include <string>
#include <string_view>

#include "sst/plugininfra/patch-support/patch_base.h"

#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/PatchState.h"
#include "gg/core/PresetInfo.h"

namespace gg::core
{
/// One parameter's value, with a copy of its metadata (paramMeta, in the patch's product).
struct Param : sst::plugininfra::patch_support::ParamBase
{
    using ParamBase::ParamBase;
};

/// The most bytes of a patch's name its `name` buffer keeps, its NUL aside.
inline constexpr size_t kMaxPatchNameBytes = 255;

/// The longest prefix of `name` that holds no NUL, is at most kMaxPatchNameBytes long and doesn't
/// end inside a UTF-8 character: a cut backs up over at most three continuation bytes (0x80 to
/// 0xbf) to the character's lead byte.
std::string patchName(std::string_view name);

/// The metadata of `root`'s <preset> element, its name `name`, into `out`: false, and `out`
/// untouched, when there is none or its `product` isn't modeNamed's. A missing attribute reads
/// empty; `tags` is split on spaces, and a word that isn't isPresetTag is dropped.
bool readPresetElement(const TiXmlElement &root, std::string_view name, PresetInfo &out);

/// The patch: every parameter's value, the engine seed, the mod matrix's routes, a name and, for
/// a preset, its metadata, and their versioned XML state (sst-plugininfra's PatchBase, plus the
/// `bits` attribute and the <engine seed>, <mod> and <preset> elements described in
/// gg/core/PatchState.h). Main thread only: constructing, saving and loading allocate.
///
/// The patch holds pointers to its own members (PatchBase's params and paramMap, and the state
/// hooks), so it can't be copied or moved.
struct Patch : sst::plugininfra::patch_support::PatchBase<Patch, Param>
{
    static constexpr uint32_t patchVersion = kPatchVersion;
    static constexpr const char id[] = "org.sethbc.giga-grains.patch";

    /// PatchBase's root `name` attribute, NUL-terminated. The additionalToState hook writes it
    /// with every `&` escaped (escapeAmpersands), and additionalFromState reads the attribute's
    /// first kMaxPatchNameBytes bytes back to a whole UTF-8 character (patchName).
    char name[256]{};
    /// Written as <engine seed="…"/> by the additionalToState hook. Missing or malformed, it loads
    /// as 0.
    uint64_t seed{0};
    /// Written as <mod> by the additionalToState hook (the routes isSavableRoute accepts), and
    /// read back by additionalFromState at version 2 and later; every slot empty otherwise, until
    /// the migration from version 1 sets the factory routes.
    ModRoutes routes{};
    /// The version the last successful fromState migrated from: 0 when the state was already at
    /// patchVersion.
    uint32_t migratedFromVersion{0};
    /// Written as <preset> by the additionalToState hook, after <mod>, when set (its `name` isn't
    /// written: the root's is the preset's). Read back by additionalFromState at any version,
    /// with `name` set to the patch's; empty when the state has no <preset>, or one whose
    /// `product` isn't a known word (gg/core/PatchState.h).
    std::optional<PresetInfo> preset;

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

    /// PatchBase calls it after the values (their exact `bits` included), the seed and the
    /// routes have loaded, when the state's version isn't patchVersion. From version 1 (or
    /// older), M1's state: the factory routes, and in the instrument Macro 7 folded into Stereo
    /// (fromStateXml in gg/core/PatchState.h). A newer version keeps what loaded.
    void migratePatchFromVersion(uint32_t fromVersion);
    /// Values need no migration; this notes whether a version-1 state held the instrument's Macro
    /// 7, which M1 otherwise loaded at its default (0.5).
    float migrateParamValueFromVersion(Param *p, float v, uint32_t version)
    {
        if (version < 2 && p == &param(ParamSlot::Macro7))
        {
            m1Macro7Loaded_ = true;
        }
        return v;
    }

  private:
    static constexpr size_t index(ParamSlot slot) noexcept
    {
        return std::min(static_cast<size_t>(slot), kParamCount - 1);
    }
    static std::array<Param, kParamCount> defaultValues(Mode mode);

    Mode mode_;
    std::array<Param, kParamCount> values;
    /// Whether a version-1 load set Macro 7 (migrateParamValueFromVersion). Only a new Patch
    /// loads (Patch::fromState loads into one), so it never carries over from an earlier load.
    bool m1Macro7Loaded_{false};
};
} // namespace gg::core

#endif // GG_CORE_PATCH_H
