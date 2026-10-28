// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/PatchState.h"

#include "Patch.h"

namespace gg::core
{
std::string toStateXml(const ParamValues &values, uint64_t seed)
{
    // The metadata never reaches the XML (only each param's ID and value do), so either
    // product's patch writes the same state.
    Patch patch(Mode::Fx);
    for (size_t i = 0; i < kParamCount; ++i)
    {
        patch.param(static_cast<ParamSlot>(i)).value = values[i];
    }
    patch.seed = seed;
    return patch.toState();
}

bool fromStateXml(std::string_view xml, ParamValues &values, uint64_t &seed, Mode mode,
                  uint32_t *migratedFromVersion)
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
    if (migratedFromVersion != nullptr)
    {
        *migratedFromVersion = patch.migratedFromVersion;
    }
    return true;
}
} // namespace gg::core
