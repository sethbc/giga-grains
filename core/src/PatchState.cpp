// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/PatchState.h"

#include <cstring>
#include <utility>

#include "Patch.h"
#include "PatchXml.h"

namespace gg::core
{
std::string patchXml(const ParamValues &values, uint64_t seed, const ModRoutes &routes, Mode mode,
                     std::string_view name, const PresetInfo *preset)
{
    Patch patch(mode);
    const std::string kept = patchName(name);
    std::memset(patch.name, 0, sizeof(patch.name));
    std::memcpy(patch.name, kept.data(), kept.size());
    if (preset != nullptr)
    {
        patch.preset = *preset;
    }
    for (size_t i = 0; i < kParamCount; ++i)
    {
        patch.param(static_cast<ParamSlot>(i)).value = values[i];
    }
    patch.seed = seed;
    patch.routes = routes;
    return patch.toState();
}

std::string toStateXml(const ParamValues &values, uint64_t seed, const ModRoutes &routes,
                       std::string_view name, const PresetInfo *preset)
{
    // The metadata never reaches the XML (only each param's ID and value, and the routes, do), so
    // either product's patch writes the same state (patchXml's test holds them to it).
    return patchXml(values, seed, routes, Mode::Fx, name, preset);
}

bool fromStateXml(std::string_view xml, ParamValues &values, uint64_t &seed, ModRoutes &routes,
                  Mode mode, uint32_t *migratedFromVersion, std::string *name,
                  std::optional<PresetInfo> *preset)
{
    // Patch::fromState is itself all or nothing, and clamps; on false, `patch` is untouched and
    // nothing here is written. A param the state doesn't mention keeps the patch's default,
    // mode's.
    Patch patch(mode);
    if (!patch.fromState(std::string(xml)))
    {
        return false;
    }
    for (size_t i = 0; i < kParamCount; ++i)
    {
        values[i] = patch.param(static_cast<ParamSlot>(i)).value;
    }
    seed = patch.seed;
    routes = patch.routes;
    if (migratedFromVersion != nullptr)
    {
        *migratedFromVersion = patch.migratedFromVersion;
    }
    if (name != nullptr)
    {
        *name = patch.name;
    }
    if (preset != nullptr)
    {
        *preset = std::move(patch.preset);
    }
    return true;
}
} // namespace gg::core
