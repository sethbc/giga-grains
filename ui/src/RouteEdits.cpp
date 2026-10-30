// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/RouteEdits.h"

#include <cmath>

#include <clap/ext/params.h>
#include <sst/basic-blocks/params/ParamMetadata.h>

namespace gg::ui
{
using gg::core::ModRoute;
using gg::core::ModSource;
using gg::core::ParamSlot;

std::pair<ParamSlot, uint8_t> routeTargetOf(ParamSlot controlSlot) noexcept
{
    if (const auto ref = gg::core::layerOf(controlSlot))
    {
        return {gg::core::layerSlot(ref->param, 0), ref->layer};
    }
    if (const auto layer = gg::core::perLayerGlobalLayer(controlSlot))
    {
        return {controlSlot == gg::core::levelSlot(*layer) ? ParamSlot::Level : ParamSlot::LayerAOn,
                *layer};
    }
    return {controlSlot, 0};
}

DropResult addRouteForDrop(gg::core::ModRoutes &routes, ModSource source, ParamSlot target,
                           uint8_t targetLayer,
                           const sst::basic_blocks::params::ParamMetaData &meta)
{
    if (source == ModSource::None || source >= ModSource::Count)
    {
        return {false, 0, "there is no source to route"};
    }
    if (gg::core::isMacro(target))
    {
        return {false, 0, "a macro can't be a target"};
    }
    if (!gg::core::isValidTarget(target))
    {
        return {false, 0, "that control can't be a target"};
    }
    const bool layered = gg::core::isLayerTarget(target);
    if (layered && targetLayer >= gg::core::kLayers && targetLayer != gg::core::kAllLayers)
    {
        return {false, 0, "that control can't be a target"};
    }
    uint8_t firstEmpty = 0;
    for (std::size_t i = 0; i < routes.size(); ++i)
    {
        const ModRoute &r = routes[i];
        if (r.source == ModSource::None)
        {
            if (firstEmpty == 0)
            {
                firstEmpty = static_cast<uint8_t>(i + 1);
            }
        }
        else if (r.source == source && r.target == target &&
                 (!layered || r.targetLayer == targetLayer ||
                  r.targetLayer == gg::core::kAllLayers))
        {
            return {false, static_cast<uint8_t>(i + 1), {}};
        }
    }
    if (firstEmpty == 0)
    {
        return {false, 0, "the matrix is full: 48 routes"};
    }
    const bool stepped = (meta.flags & CLAP_PARAM_IS_STEPPED) != 0 ||
                         meta.type != sst::basic_blocks::params::ParamMetaData::FLOAT;
    ModRoute &route = routes[firstEmpty - 1u];
    route.source = source;
    route.sourceLayer = gg::core::kSameLayer;
    route.target = target;
    route.targetLayer = layered ? targetLayer : gg::core::kAllLayers;
    route.depth = 0.25f * (meta.maxVal - meta.minVal);
    route.curve = stepped ? gg::core::ModCurve::Unipolar : gg::core::ModCurve::Bipolar;
    return {true, firstEmpty, {}};
}

void setDepth(gg::core::ModRoutes &routes, uint8_t slot, float depth)
{
    if (slot >= 1 && slot <= routes.size() && std::isfinite(depth))
    {
        routes[slot - 1u].depth = depth;
    }
}

void clearSlot(gg::core::ModRoutes &routes, uint8_t slot)
{
    if (slot >= 1 && slot <= routes.size())
    {
        routes[slot - 1u] = ModRoute{};
    }
}
} // namespace gg::ui
