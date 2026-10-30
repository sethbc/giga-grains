// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/core/ModMatrix.h"

#include <algorithm>

#include "gg/core/ParamMap.h"

namespace gg::core
{
namespace
{
constexpr std::array<const char *, static_cast<size_t>(ModSource::Count)> kSourceNames{
    "None",    "Macro 1",      "Macro 2",        "Macro 3",       "Macro 4",
    "Macro 5", "Macro 6",      "Macro 7",        "Macro 8",       "LFO 1",
    "LFO 2",   "Env 1",        "Env 2",          "Random 1",      "Random 2",
    "Step",    "Mod Wheel",    "Aftertouch",     "Pitch Bend",    "Velocity",
    "Key",     "Env Follower", "Transport Beat", "Transport Bar", "Transport Playing",
};

constexpr std::array<const char *, static_cast<size_t>(ModCurve::Count)> kCurveNames{
    "Unipolar", "Bipolar", "BipolarPositive", "UnipolarSquared", "RampThreeQuarters", "TopQuarter",
};

/// Layer `layer`'s value of `source`, or layer 0's for a layer out of range.
float sourceValue(const std::array<ModSourceValues, kLayers> &sources, ModSource source,
                  uint8_t layer) noexcept
{
    return sources[layer < kLayers ? layer : 0].v[static_cast<size_t>(source)];
}
} // namespace

float applyCurve(ModCurve c, float v) noexcept
{
    // macroOffsets' expressions, term for term (gg/core/ParamMap.cpp).
    switch (c)
    {
    case ModCurve::Unipolar:
        return v;
    case ModCurve::Bipolar:
        return 2.0f * v - 1.0f;
    case ModCurve::BipolarPositive:
        return std::max(2.0f * v - 1.0f, 0.0f);
    case ModCurve::UnipolarSquared:
        return v * v;
    case ModCurve::RampThreeQuarters:
        return std::min(v / 0.75f, 1.0f);
    case ModCurve::TopQuarter:
        return smoothstep(0.75f, 1.0f, v);
    case ModCurve::Count:
        break;
    }
    return 0.0f;
}

void applyRoutes(const ModRoutes &routes, const std::array<ModSourceValues, kLayers> &sources,
                 ParamValues &offsets) noexcept
{
    for (const ModRoute &r : routes)
    {
        if (r.source == ModSource::None || r.source >= ModSource::Count || !isValidTarget(r.target))
        {
            continue;
        }
        const bool perLayer = isPerLayerSource(r.source);
        // The source layer for a route evaluated on target layer `layer`.
        const auto from = [&r, perLayer](uint8_t layer) -> uint8_t {
            if (!perLayer)
            {
                return 0;
            }
            return r.sourceLayer == kSameLayer ? layer : r.sourceLayer;
        };
        const auto add = [&](ParamSlot slot, uint8_t layer) {
            offsets[static_cast<size_t>(slot)] +=
                r.depth * applyCurve(r.curve, sourceValue(sources, r.source, from(layer)));
        };
        if (!isLayerTarget(r.target))
        {
            add(r.target, 0);
        }
        else if (r.targetLayer == kAllLayers)
        {
            for (uint8_t layer = 0; layer < kLayers; ++layer)
            {
                add(routeTargetSlot(r.target, layer), layer);
            }
        }
        else if (r.targetLayer < kLayers)
        {
            add(routeTargetSlot(r.target, r.targetLayer), r.targetLayer);
        }
    }
}

ModRoutes factoryRoutes(Mode mode) noexcept
{
    const auto route = [](ModSource source, ParamSlot target, float depth, ModCurve curve) {
        return ModRoute{source, kSameLayer, target, kAllLayers, depth, curve};
    };
    // Spec §6.8's table, in slot order. Each target's contributions arrive in macroOffsets' order
    // (Density: Macro 1's, then Macro 2's), so the sums match it bit for bit.
    ModRoutes r{};
    r[0] = route(ModSource::Macro1, ParamSlot::Size, 3.0f, ModCurve::Bipolar);
    r[1] = route(ModSource::Macro1, ParamSlot::Density, -1.5f, ModCurve::Bipolar);
    r[2] = route(ModSource::Macro2, ParamSlot::Density, 4.0f, ModCurve::Bipolar);
    r[3] = route(ModSource::Macro2, ParamSlot::Distribution, 0.25f, ModCurve::BipolarPositive);
    r[4] = route(ModSource::Macro3, ParamSlot::Pitch, 24.0f, ModCurve::Bipolar);
    r[5] = route(ModSource::Macro4, ParamSlot::Spray, 0.5f, ModCurve::Bipolar);
    r[6] = route(ModSource::Macro4, ParamSlot::Stereo, 0.3f, ModCurve::BipolarPositive);
    r[7] = route(ModSource::Macro5, ParamSlot::Texture, 1.0f, ModCurve::RampThreeQuarters);
    r[8] = route(ModSource::Macro5, ParamSlot::Diffusion, 1.0f, ModCurve::TopQuarter);
    r[9] = route(ModSource::Macro6, ParamSlot::Freeze, 1.0f, ModCurve::Unipolar);
    r[10] = route(ModSource::Macro7, ParamSlot::Feedback, 0.9f, ModCurve::UnipolarSquared);
    r[11] = mode == Mode::Instrument
                ? route(ModSource::Macro8, ParamSlot::Level, 12.0f, ModCurve::Bipolar)
                : route(ModSource::Macro8, ParamSlot::Mix, 0.5f, ModCurve::Bipolar);
    return r;
}

const char *modSourceName(ModSource s) noexcept
{
    const auto i = static_cast<size_t>(s);
    return i < kSourceNames.size() ? kSourceNames[i] : "";
}

std::optional<ModSource> modSourceNamed(std::string_view name) noexcept
{
    for (size_t i = 0; i < kSourceNames.size(); ++i)
    {
        if (name == kSourceNames[i])
        {
            return static_cast<ModSource>(i);
        }
    }
    return std::nullopt;
}

const char *modCurveName(ModCurve c) noexcept
{
    const auto i = static_cast<size_t>(c);
    return i < kCurveNames.size() ? kCurveNames[i] : "";
}

std::optional<ModCurve> modCurveNamed(std::string_view name) noexcept
{
    for (size_t i = 0; i < kCurveNames.size(); ++i)
    {
        if (name == kCurveNames[i])
        {
            return static_cast<ModCurve>(i);
        }
    }
    return std::nullopt;
}
} // namespace gg::core
