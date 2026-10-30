// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_MODMATRIX_H
#define GG_CORE_MODMATRIX_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "gg/core/ParamIds.h"

/// The mod matrix (M2 sub-project 1's spec §6.8): 48 routes, each a source's value through a curve,
/// scaled by a depth and added to a target parameter's offset in its CLAP value domain. The routes
/// live in the patch, never in parameters. The twelve factory routes reproduce M1's macro recipes
/// (M1's macroOffsets, which they replaced; tests/core/mod_matrix_tests.cpp keeps a copy) bit for
/// bit.
///
/// Evaluation is plain float arithmetic, no libm and no allocation, so the Processor can run it on
/// the audio thread and its result is the same on every platform (INTENT §14).
namespace gg::core
{
/// A route's source (spec §6.8's table), each a value in [0, 1]. The enum's order is the index into
/// ModSourceValues; the names (modSourceName) are what the patch saves.
enum class ModSource : uint8_t
{
    None,
    Macro1,
    Macro2,
    Macro3,
    Macro4,
    Macro5,
    Macro6,
    Macro7,
    Macro8,
    Lfo1,
    Lfo2,
    Env1,
    Env2,
    Random1,
    Random2,
    Step,
    ModWheel,
    Aftertouch,
    PitchBend,
    Velocity,
    Key,
    EnvFollower,
    TransportBeat,
    TransportBar,
    TransportPlaying,
    Count
};

/// A route's curve, applied to the source's value u (spec §6.8): chosen so that M1's eight macro
/// recipes are expressible exactly.
enum class ModCurve : uint8_t
{
    Unipolar,          ///< u
    Bipolar,           ///< 2u − 1
    BipolarPositive,   ///< max(2u − 1, 0)
    UnipolarSquared,   ///< u²
    RampThreeQuarters, ///< min(u / 0.75, 1)
    TopQuarter,        ///< smoothstep(0.75, 1, u)
    Count
};

/// A route's targetLayer meaning every layer (A to D, in that order), and its sourceLayer meaning
/// the target's own layer.
inline constexpr uint8_t kAllLayers = 0xFF, kSameLayer = 0xFE;

/// One slot of the matrix.
struct ModRoute
{
    ModSource source{ModSource::None};
    /// A per-layer source's layer: 0 to 3 (A to D) or kSameLayer, the target's layer (layer A for
    /// a target without one). Out of range reads layer A. Ignored for a global source.
    uint8_t sourceLayer{kSameLayer};
    /// For a per-layer kind (Level and Layer A On included), layer A's slot, and targetLayer picks
    /// the layers; otherwise a global, and targetLayer is ignored. See isValidTarget.
    ParamSlot target{ParamSlot::Count};
    /// 0 to 3 (A to D) or kAllLayers. Out of range: the route does nothing.
    uint8_t targetLayer{kAllLayers};
    float depth{0.0f};
    ModCurve curve{ModCurve::Bipolar};

    friend bool operator==(const ModRoute &, const ModRoute &) = default;
};

/// The matrix's slots. Indexed 0 to 47 in code; people (the patch's XML, gg-render) number them
/// 1 to 48.
inline constexpr size_t kModSlots = 48;
using ModRoutes = std::array<ModRoute, kModSlots>;

/// Whether `s` has a value per layer: the LFOs, envelopes, randoms and Step. The others are global.
constexpr bool isPerLayerSource(ModSource s) noexcept
{
    return s >= ModSource::Lfo1 && s <= ModSource::Step;
}

/// Whether `s` moves with time alone, without an event: the per-layer sources, the Env Follower and
/// the transport's beat and bar phases.
constexpr bool isTimeVarying(ModSource s) noexcept
{
    return isPerLayerSource(s) || s == ModSource::EnvFollower || s == ModSource::TransportBeat ||
           s == ModSource::TransportBar;
}

/// Whether a route to `target` takes a layer: layer A's slot of a layer parameter, Level or Layer A
/// On.
constexpr bool isLayerTarget(ParamSlot target) noexcept
{
    const auto ref = layerOf(target);
    return (ref && ref->layer == 0) || target == ParamSlot::Level || target == ParamSlot::LayerAOn;
}

/// Whether `target` may be a route's target (spec §6.8): any parameter but the macros and the
/// modulators' own controls (LFO 1 Rate to Step 16, in every layer). A per-layer kind is named by
/// layer A's slot (Level and Layer A On for the per-layer globals), so layers B to D's slots, Level
/// B to D and Layer B to D On are refused: a route reaches them through its targetLayer.
constexpr bool isValidTarget(ParamSlot target) noexcept
{
    if (static_cast<size_t>(target) >= kParamCount || isMacro(target))
    {
        return false;
    }
    if (const auto ref = layerOf(target))
    {
        return ref->layer == 0 && ref->param < LayerParam::Lfo1Rate;
    }
    return !perLayerGlobalLayer(target) || target == ParamSlot::Level ||
           target == ParamSlot::LayerAOn;
}

/// The slot `target` (a layer target, as isLayerTarget) reaches on `layer`; a global target
/// unchanged. ParamSlot::Count for a layer out of range.
constexpr ParamSlot routeTargetSlot(ParamSlot target, uint8_t layer) noexcept
{
    if (target == ParamSlot::Level)
    {
        return levelSlot(layer);
    }
    if (target == ParamSlot::LayerAOn)
    {
        return layerOnSlot(layer);
    }
    if (const auto ref = layerOf(target))
    {
        return layerSlot(ref->param, layer);
    }
    return target;
}

/// The curve `c` of `v`, as ModCurve documents: M1's macroOffsets' exact float expressions. +0 for
/// ModCurve::Count or beyond.
float applyCurve(ModCurve c, float v) noexcept;

/// Every source's value on one layer, indexed by ModSource. The global sources hold the same
/// value in every layer's array; None's entry is unused.
struct ModSourceValues
{
    std::array<float, static_cast<size_t>(ModSource::Count)> v{};
};

/// Adds every route's contribution, depth × applyCurve(curve, value), to `offsets` with +=, in slot
/// order. A route is skipped when its source is None (or out of range), its target isn't valid
/// (isValidTarget) or, for a layer target, its targetLayer is out of range. A layer target goes to
/// its targetLayer, or to layers 0 to 3 in order for kAllLayers, each at routeTargetSlot; any other
/// target once, at itself. The value is sources[0]'s for a global source; for a per-layer source,
/// the sourceLayer's, or for kSameLayer the target's layer (layer 0 for a global target), and layer
/// 0's for a sourceLayer out of range.
///
/// Starting from an array of +0.0f, the factory routes give M1's macroOffsets' values bit for bit.
void applyRoutes(const ModRoutes &routes, const std::array<ModSourceValues, kLayers> &sources,
                 ParamValues &offsets) noexcept;

/// The factory routes for `mode`'s product (spec §6.8): slots 0 to 11 hold the twelve, in the
/// spec's order, and the rest are empty. Every layer target takes every layer; slot 11 is Macro 8
/// to Mix in the FX, to Level on every layer in the instrument.
ModRoutes factoryRoutes(Mode mode) noexcept;

/// A source's stable identifier, saved in the patch: "None", "Macro 1" to "Macro 8", "LFO 1",
/// "LFO 2", "Env 1", "Env 2", "Random 1", "Random 2", "Step", "Mod Wheel", "Aftertouch", "Pitch
/// Bend", "Velocity", "Key", "Env Follower", "Transport Beat", "Transport Bar", "Transport
/// Playing". Never changed once saved. "" for ModSource::Count or beyond.
const char *modSourceName(ModSource s) noexcept;

/// The source with this identifier, matched exactly (case-sensitive); nothing for any other text.
std::optional<ModSource> modSourceNamed(std::string_view name) noexcept;

/// A curve's stable identifier, saved in the patch: its enumerator's name ("Unipolar", "Bipolar",
/// "BipolarPositive", "UnipolarSquared", "RampThreeQuarters", "TopQuarter"). "" for
/// ModCurve::Count or beyond.
const char *modCurveName(ModCurve c) noexcept;

/// The curve with this identifier, matched exactly (case-sensitive); nothing for any other text.
std::optional<ModCurve> modCurveNamed(std::string_view name) noexcept;
} // namespace gg::core

#endif // GG_CORE_MODMATRIX_H
