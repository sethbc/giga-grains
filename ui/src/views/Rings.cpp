// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/Rings.h"

#include <cmath>

namespace gg::ui
{
using gg::core::ModSource;

namespace
{
uint8_t familyOf(ModSource s) noexcept
{
    if (s <= ModSource::Macro8)
    {
        return 0;
    }
    if (gg::core::isPerLayerSource(s))
    {
        return 1;
    }
    if (s == ModSource::EnvFollower)
    {
        return 3;
    }
    return s >= ModSource::TransportBeat ? 4 : 2;
}
} // namespace

std::vector<RingSpec> ringsFor(const gg::core::ModRoutes &routes, gg::core::ParamSlot target,
                               uint8_t layer, float rangeMin, float rangeMax) noexcept
{
    std::vector<RingSpec> out;
    const float range = rangeMax - rangeMin;
    if (!(range > 0.0f) || !std::isfinite(range))
    {
        return out;
    }
    const bool layered = gg::core::isLayerTarget(target);
    for (std::size_t i = 0; i < routes.size(); ++i)
    {
        const gg::core::ModRoute &r = routes[i];
        if (r.source == ModSource::None || r.source >= ModSource::Count || r.target != target ||
            (layered && r.targetLayer != gg::core::kAllLayers && r.targetLayer != layer))
        {
            continue;
        }
        out.push_back({static_cast<uint8_t>(i + 1), r.depth / range,
                       r.curve == gg::core::ModCurve::Bipolar, familyOf(r.source)});
    }
    return out;
}
} // namespace gg::ui
