// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// ParamMap: the patch's values to the engine's parameters (Global Constraints' parameter table),
// to Space's (sub-project 4's spec §6.5), and the wet chain's soft limiter (spec §6.4).
//
// Every value that reaches the grain-event log is pinned bit for bit against the table's formula
// with exp2Det, so a mapping that used libm (or another formula) fails here; the literal targets
// (0.15 s, 16 Hz, 16 s and so on) are checked against std::exp2 within a tolerance.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gg/core/MathDet.h"
#include "gg/core/ModMatrix.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using gg::core::kParamCount;
using gg::core::Mode;
using gg::core::ParamSlot;
using gg::core::ParamValues;
using gg::core::softLimit;
using gg::core::toCloudParams;
using gg::core::toSpaceParams;
using gg::core::toWetParams;

namespace
{
ParamValues defaults(Mode mode = Mode::Fx)
{
    ParamValues v{};
    for (size_t i = 0; i < kParamCount; ++i)
    {
        v[i] = gg::core::paramMeta(static_cast<ParamSlot>(i), mode).defaultVal;
    }
    return v;
}

// mode's defaults with one slot changed.
ParamValues with(ParamSlot slot, float value, Mode mode = Mode::Fx)
{
    ParamValues v = defaults(mode);
    v[static_cast<size_t>(slot)] = value;
    return v;
}

double d(float v) { return static_cast<double>(v); }

// The brief's soft limiter, as written, in double: sign(x) × (0.5 + 0.5 × z/(1 + z)) with
// z = (|x| − 0.5)/0.5 above the knee.
double referenceLimit(double x)
{
    const double a = std::abs(x);
    if (a <= 0.5)
    {
        return x;
    }
    const double z = (a - 0.5) / 0.5;
    return std::copysign(0.5 + 0.5 * z / (1.0 + z), x);
}

uint32_t bitsOf(float f) { return std::bit_cast<uint32_t>(f); }
float fromBits(uint32_t b) { return std::bit_cast<float>(b); }

uint64_t bitsOf(double x) { return std::bit_cast<uint64_t>(x); }

double at(const ParamValues &v, ParamSlot slot) { return d(v[static_cast<size_t>(slot)]); }

// toCloudParams as it was before the instrument's Position (carry-over T6), copied: the FX's
// mapping must not move by a bit.
gg::CloudParams fxReference(const ParamValues &v)
{
    gg::CloudParams p;
    p.densityHz = gg::exp2Det(at(v, ParamSlot::Density));
    p.distribution = at(v, ParamSlot::Distribution);
    p.grainDurationSec = gg::exp2Det(at(v, ParamSlot::Size));
    p.position = 16.0 / 255.0 * (gg::exp2Det(8.0 * at(v, ParamSlot::Position)) - 1.0);
    p.spray = 2.0 / 255.0 * (gg::exp2Det(8.0 * at(v, ParamSlot::Spray)) - 1.0);
    p.pitchSemitones = at(v, ParamSlot::Pitch);
    p.stereo = at(v, ParamSlot::Stereo);
    p.texture = at(v, ParamSlot::Texture); // sub-project 5's Texture, read as it is
    p.gain = static_cast<float>(1.0 / std::sqrt(std::max(1.0, p.densityHz * p.grainDurationSec)));
    return p;
}

// Every field but position and sprayInSeconds, bit for bit.
bool sameApartFromPosition(const gg::CloudParams &a, const gg::CloudParams &b)
{
    return bitsOf(a.densityHz) == bitsOf(b.densityHz) &&
           bitsOf(a.distribution) == bitsOf(b.distribution) &&
           bitsOf(a.grainDurationSec) == bitsOf(b.grainDurationSec) &&
           bitsOf(a.spray) == bitsOf(b.spray) &&
           bitsOf(a.pitchSemitones) == bitsOf(b.pitchSemitones) &&
           bitsOf(a.stereo) == bitsOf(b.stereo) && bitsOf(a.texture) == bitsOf(b.texture) &&
           bitsOf(a.gain) == bitsOf(b.gain) && a.quality == b.quality;
}

bool sameBits(const gg::CloudParams &a, const gg::CloudParams &b)
{
    return sameApartFromPosition(a, b) && bitsOf(a.position) == bitsOf(b.position) &&
           a.sprayInSeconds == b.sprayInSeconds;
}

// Every field, bit for bit (+0 and -0 differ).
bool sameCloudBits(const gg::CloudParams &a, const gg::CloudParams &b)
{
    return sameBits(a, b) && bitsOf(a.pitchSpreadSemitones) == bitsOf(b.pitchSpreadSemitones) &&
           bitsOf(a.sizeSpreadOctaves) == bitsOf(b.sizeSpreadOctaves) &&
           bitsOf(a.smearJitterSec) == bitsOf(b.smearJitterSec) &&
           bitsOf(a.smearDetuneSemitones) == bitsOf(b.smearDetuneSemitones) &&
           bitsOf(a.busSplit) == bitsOf(b.busSplit);
}

// Points spread over every slot's range at once: the ends, mode's defaults and 200 mixtures.
std::vector<ParamValues> samplePoints(Mode mode)
{
    std::vector<ParamValues> points;
    ParamValues lo{};
    ParamValues hi{};
    for (size_t s = 0; s < kParamCount; ++s)
    {
        const auto &meta = gg::core::paramMeta(static_cast<ParamSlot>(s), mode);
        lo[s] = meta.minVal;
        hi[s] = meta.maxVal;
    }
    points.push_back(lo);
    points.push_back(hi);
    points.push_back(defaults(mode));
    for (size_t i = 0; i < 200; ++i)
    {
        ParamValues v{};
        for (size_t s = 0; s < kParamCount; ++s)
        {
            const auto frac = static_cast<float>((i * 37 + s * 11) % 101) / 100.0f;
            v[s] = lo[s] + (hi[s] - lo[s]) * frac;
        }
        points.push_back(v);
    }
    return points;
}

// toCloudParams as it was before Smear (M2 sub-project 1's mapping, with sub-project 3's Quality),
// copied, for any layer: at Smear 0 the mapping must not move by a bit. busSplit is the one field
// Smear's task adds that doesn't depend on Smear: it is Bus Split, as it is.
gg::CloudParams preSmearReference(const ParamValues &v, Mode mode, uint8_t layer)
{
    using gg::core::LayerParam;
    const auto param = [&v, layer](LayerParam p) { return at(v, gg::core::layerSlot(p, layer)); };
    gg::CloudParams p;
    p.densityHz = gg::exp2Det(param(LayerParam::Density));
    p.distribution = param(LayerParam::Distribution);
    p.grainDurationSec = gg::exp2Det(param(LayerParam::Size));
    if (gg::core::builtKind(gg::core::sourceKindOf(v, layer), mode) == gg::SourceMode::Sample)
    {
        p.position = param(LayerParam::Position);
        p.sprayInSeconds = true;
    }
    else
    {
        p.position = 16.0 / 255.0 * (gg::exp2Det(8.0 * param(LayerParam::Position)) - 1.0);
    }
    p.spray = 2.0 / 255.0 * (gg::exp2Det(8.0 * param(LayerParam::Spray)) - 1.0);
    p.pitchSemitones = param(LayerParam::Pitch);
    p.stereo = param(LayerParam::Stereo);
    p.texture = param(LayerParam::Texture);
    p.pitchSpreadSemitones = param(LayerParam::PitchSpread);
    p.sizeSpreadOctaves = param(LayerParam::SizeSpread);
    p.busSplit = param(LayerParam::BusSplit);
    // M2 sub-project 3's Quality: the nearest step, clamped to 0..2 (a NaN is Normal).
    const double quality = param(LayerParam::Quality);
    p.quality = std::isnan(quality)
                    ? gg::Tier::Normal
                    : static_cast<gg::Tier>(std::lround(std::clamp(quality, 0.0, 2.0)));
    p.gain = static_cast<float>(1.0 / std::sqrt(std::max(1.0, p.densityHz * p.grainDurationSec)));
    return p;
}

// The plan's Smear law (M2 sub-project 4's Global Constraints; its spec §6.1), written out: the
// effective density for D Hz, S seconds and Smear s.
double smearDensity(double D, double S, double s)
{
    const double T = 8.0 + 24.0 * s;
    return D + s * (std::min(400.0, std::max(D, T / S)) - D);
}

// v with layer `layer`'s Smear, Density and Size set (Density log2 Hz, Size log2 seconds).
ParamValues smeared(ParamValues v, uint8_t layer, float smear, float density, float size)
{
    using gg::core::LayerParam;
    using gg::core::layerSlot;
    v[static_cast<size_t>(layerSlot(LayerParam::Smear, layer))] = smear;
    v[static_cast<size_t>(layerSlot(LayerParam::Density, layer))] = density;
    v[static_cast<size_t>(layerSlot(LayerParam::Size, layer))] = size;
    return v;
}

// log2 of the defaults' 16 Hz and 150 ms, and of the slots' ends: 400 Hz and 5 ms.
constexpr float kLog2Of16Hz = 4.0f;
constexpr float kLog2Of150Ms = -2.736965594166206f;
constexpr float kLog2Of400Hz = 8.643856189774725f;
constexpr float kLog2Of5Ms = -7.643856189774724f;
} // namespace

TEST_CASE("param map follows the taper table", "[core]")
{
    // The brief's pins.
    SECTION("Size -2.736965594166206f is 0.15 s")
    {
        const auto p = toCloudParams(with(ParamSlot::Size, -2.736965594166206f), Mode::Fx);
        REQUIRE_THAT(p.grainDurationSec, WithinRel(0.15, 1e-6));
    }
    SECTION("Density 4 is exactly 16 Hz")
    {
        REQUIRE(toCloudParams(with(ParamSlot::Density, 4.0f), Mode::Fx).densityHz == 16.0);
    }
    SECTION("Position 0 is 0 s and 1 is 16 s")
    {
        REQUIRE_THAT(toCloudParams(with(ParamSlot::Position, 0.0f), Mode::Fx).position,
                     WithinAbs(0.0, 1e-9));
        REQUIRE_THAT(toCloudParams(with(ParamSlot::Position, 1.0f), Mode::Fx).position,
                     WithinAbs(16.0, 1e-9));
    }
    SECTION("the gain at 16 Hz and 0.15 s is 1/sqrt(2.4)")
    {
        // The defaults are Density 16 Hz and Size 0.15 s.
        const auto p = toCloudParams(defaults(), Mode::Fx);
        REQUIRE(p.densityHz == 16.0);
        REQUIRE_THAT(p.grainDurationSec, WithinRel(0.15, 1e-6));
        REQUIRE_THAT(static_cast<double>(p.gain), WithinRel(1.0 / std::sqrt(2.4), 1e-6));
    }

    // Every slot at its min, default and max: the table's formula, bit for bit with exp2Det
    // where the value reaches the grain-event log, and the expected value in real units.
    for (size_t i = 0; i < kParamCount; ++i)
    {
        const auto slot = static_cast<ParamSlot>(i);
        const auto &meta = gg::core::paramMeta(slot, Mode::Fx);
        for (const float v : {meta.minVal, meta.defaultVal, meta.maxVal})
        {
            INFO("slot " << i << " at " << v);
            const ParamValues values = with(slot, v);
            const auto cloud = toCloudParams(values, Mode::Fx);
            const auto wet = toWetParams(values, Mode::Fx);
            const auto space = toSpaceParams(values, Mode::Fx);
            switch (slot)
            {
            case ParamSlot::Mix:
                REQUIRE(wet.mix == v);
                break;
            case ParamSlot::Level:
            {
                const double gain = gg::exp2Det(d(v) * 3.321928094887362 / 20.0);
                REQUIRE(wet.levelGain == static_cast<float>(gain));
                REQUIRE_THAT(static_cast<double>(wet.levelGain),
                             WithinRel(std::pow(10.0, d(v) / 20.0), 1e-6));
                break;
            }
            case ParamSlot::Position:
            {
                const double s = 16.0 / 255.0 * (gg::exp2Det(8.0 * d(v)) - 1.0);
                REQUIRE(cloud.position == s);
                REQUIRE_THAT(cloud.position,
                             WithinAbs(16.0 / 255.0 * (std::exp2(8.0 * d(v)) - 1.0), 1e-12));
                break;
            }
            case ParamSlot::Size:
                REQUIRE(cloud.grainDurationSec == gg::exp2Det(d(v)));
                REQUIRE_THAT(cloud.grainDurationSec, WithinRel(std::exp2(d(v)), 1e-12));
                break;
            case ParamSlot::Density:
                REQUIRE(cloud.densityHz == gg::exp2Det(d(v)));
                REQUIRE_THAT(cloud.densityHz, WithinRel(std::exp2(d(v)), 1e-12));
                break;
            case ParamSlot::Distribution:
                REQUIRE(cloud.distribution == d(v));
                break;
            case ParamSlot::Pitch:
                REQUIRE(cloud.pitchSemitones == d(v));
                break;
            case ParamSlot::Spray:
            {
                const double s = 2.0 / 255.0 * (gg::exp2Det(8.0 * d(v)) - 1.0);
                REQUIRE(cloud.spray == s);
                REQUIRE_THAT(cloud.spray,
                             WithinAbs(2.0 / 255.0 * (std::exp2(8.0 * d(v)) - 1.0), 1e-12));
                break;
            }
            case ParamSlot::Stereo:
                REQUIRE(cloud.stereo == d(v));
                break;
            case ParamSlot::Freeze:
                REQUIRE(wet.freeze == (v > 0.5f));
                break;
            case ParamSlot::Diffusion:
                REQUIRE(space.diffusion == v);
                break;
            case ParamSlot::ReverbAmount:
                REQUIRE(space.amount == v);
                break;
            case ParamSlot::ReverbDecay:
                REQUIRE(space.decayLog2 == v);
                REQUIRE(space.infinite == (v >= gg::core::kDecayInfinite));
                break;
            case ParamSlot::ReverbSize:
                REQUIRE(space.roomSize == 2.0f * v - 1.0f);
                break;
            case ParamSlot::ReverbDamping:
                REQUIRE(space.hfDamping == v);
                break;
            case ParamSlot::ReverbPredelay:
                REQUIRE(space.predelaySeconds == v);
                break;
            case ParamSlot::ReverbModulation:
                REQUIRE(space.modulation == v);
                break;
            case ParamSlot::DryToReverb:
                REQUIRE(space.drySend == v);
                break;
            case ParamSlot::Texture:
                // Read as it is (sub-project 5's spec §4); every other field is the defaults'.
                {
                    REQUIRE(cloud.texture == d(v));
                    auto others = cloud;
                    others.texture = 0.0;
                    REQUIRE(sameCloudBits(others, toCloudParams(defaults(Mode::Fx), Mode::Fx)));
                    break;
                }
            case ParamSlot::Feedback:
            {
                // Read as it is on a layer that reads its ring, in either product, and 0 on a
                // Sample layer, which has no ring to feed (sub-project 5's spec §5; M2
                // sub-project 1's spec §6.4); every other wet field is the defaults'. `values`
                // has the FX's default Source, Live.
                REQUIRE(wet.feedback == v);
                REQUIRE(toWetParams(values, Mode::Instrument).feedback == v);
                auto sampleLayer = values;
                sampleLayer[static_cast<size_t>(ParamSlot::Source)] = 0.0f; // Sample
                REQUIRE(toWetParams(sampleLayer, Mode::Fx).feedback == 0.0f);
                REQUIRE(toWetParams(sampleLayer, Mode::Instrument).feedback == 0.0f);
                const auto w0 = toWetParams(defaults(Mode::Fx), Mode::Fx);
                REQUIRE(w0.feedback == 0.0f);
                REQUIRE(wet.levelGain == w0.levelGain);
                REQUIRE(wet.mix == w0.mix);
                REQUIRE(wet.freeze == w0.freeze);
                REQUIRE(sameCloudBits(cloud, toCloudParams(defaults(Mode::Fx), Mode::Fx)));
                break;
            }
            case ParamSlot::Source:
            {
                // M2 sub-project 1's spec §6.4: layer A's kind picks Position's mapping and
                // Spray's sprayInSeconds (Sample: the fraction; Live and, in the FX, the unbuilt
                // kinds: seconds behind the head), and whether Feedback reaches the ring.
                const auto c0 = toCloudParams(defaults(Mode::Fx), Mode::Fx);
                const bool sample = v == 0.0f;
                REQUIRE(sameApartFromPosition(cloud, c0));
                REQUIRE(cloud.sprayInSeconds == sample);
                REQUIRE(cloud.position == (sample ? at(values, ParamSlot::Position) : c0.position));
                REQUIRE(bitsOf(wet.levelGain) ==
                        bitsOf(toWetParams(defaults(), Mode::Fx).levelGain));
                break;
            }
            case ParamSlot::PitchSpread:
            case ParamSlot::SizeSpread:
            {
                // Read as they are (spec §6.6); every other field is the defaults'.
                REQUIRE((slot == ParamSlot::PitchSpread ? cloud.pitchSpreadSemitones
                                                        : cloud.sizeSpreadOctaves) == d(v));
                auto c0 = toCloudParams(defaults(Mode::Fx), Mode::Fx);
                c0.pitchSpreadSemitones = cloud.pitchSpreadSemitones;
                c0.sizeSpreadOctaves = cloud.sizeSpreadOctaves;
                REQUIRE(sameCloudBits(cloud, c0));
                break;
            }
            case ParamSlot::Smear:
            {
                // M2 sub-project 4's spec §6.1: Smear raises the density by its law and sets the
                // jitter and the detune; the gain follows the raised density (checked after the
                // switch); every other field is the defaults'.
                auto c0 = toCloudParams(defaults(Mode::Fx), Mode::Fx);
                REQUIRE(cloud.densityHz == smearDensity(16.0, c0.grainDurationSec, d(v)));
                REQUIRE(cloud.smearJitterSec == 0.5 * d(v) * c0.grainDurationSec);
                REQUIRE(cloud.smearDetuneSemitones == 0.5 * d(v));
                c0.densityHz = cloud.densityHz;
                c0.gain = cloud.gain;
                c0.smearJitterSec = cloud.smearJitterSec;
                c0.smearDetuneSemitones = cloud.smearDetuneSemitones;
                REQUIRE(sameCloudBits(cloud, c0));
                break;
            }
            case ParamSlot::BusSplit:
            {
                // The extremes spec §6.3: read as it is; every other field is the defaults'.
                REQUIRE(cloud.busSplit == d(v));
                auto c0 = toCloudParams(defaults(Mode::Fx), Mode::Fx);
                c0.busSplit = cloud.busSplit;
                REQUIRE(sameCloudBits(cloud, c0));
                break;
            }
            case ParamSlot::Macro1:
            case ParamSlot::Macro2:
            case ParamSlot::Macro3:
            case ParamSlot::Macro4:
            case ParamSlot::Macro5:
            case ParamSlot::Macro6:
            case ParamSlot::Macro7:
            case ParamSlot::Macro8:
            default:
            {
                // The macros act through the mod matrix before these functions: none of them moves
                // a field here. Nor does any other of M2's new slots for layer A (M2 sub-project
                // 1's spec §6.9: its modulators, layers B to D and the layers' globals; the
                // layer-aware functions read those, "the layer-aware maps read each layer's
                // slots").
                const ParamValues base = defaults(Mode::Fx);
                const auto c0 = toCloudParams(base, Mode::Fx);
                const auto w0 = toWetParams(base, Mode::Fx);
                const auto s0 = toSpaceParams(base, Mode::Fx);
                REQUIRE(cloud.densityHz == c0.densityHz);
                REQUIRE(cloud.distribution == c0.distribution);
                REQUIRE(cloud.grainDurationSec == c0.grainDurationSec);
                REQUIRE(cloud.position == c0.position);
                REQUIRE(cloud.spray == c0.spray);
                REQUIRE(cloud.pitchSemitones == c0.pitchSemitones);
                REQUIRE(cloud.stereo == c0.stereo);
                REQUIRE(cloud.texture == c0.texture);
                REQUIRE(cloud.gain == c0.gain);
                REQUIRE(wet.levelGain == w0.levelGain);
                REQUIRE(wet.mix == w0.mix);
                REQUIRE(wet.freeze == w0.freeze);
                REQUIRE(wet.feedback == w0.feedback);
                REQUIRE(space.diffusion == s0.diffusion);
                REQUIRE(space.amount == s0.amount);
                REQUIRE(space.drySend == s0.drySend);
                REQUIRE(space.decayLog2 == s0.decayLog2);
                REQUIRE(space.infinite == s0.infinite);
                REQUIRE(space.roomSize == s0.roomSize);
                REQUIRE(space.hfDamping == s0.hfDamping);
                REQUIRE(space.modulation == s0.modulation);
                REQUIRE(space.predelaySeconds == s0.predelaySeconds);
                break;
            }
            case ParamSlot::Count:
                FAIL("not a parameter");
            }
            // The latched per-grain gain (spec §6.4), from this point's density and size.
            const double overlap = cloud.densityHz * cloud.grainDurationSec;
            REQUIRE(cloud.gain == static_cast<float>(1.0 / std::sqrt(std::max(1.0, overlap))));
        }
    }

    // A dense sweep of each exponential taper, bit for bit against exp2Det. At the three points
    // above a platform's std::exp2 can agree with exp2Det (Apple's does); across a sweep it
    // doesn't, so a mapping through libm fails here.
    for (const ParamSlot slot :
         {ParamSlot::Density, ParamSlot::Size, ParamSlot::Position, ParamSlot::Spray})
    {
        const auto &meta = gg::core::paramMeta(slot, Mode::Fx);
        int mismatches = 0;
        for (int i = 0; i <= 1000; ++i)
        {
            const float v =
                meta.minVal + (meta.maxVal - meta.minVal) * static_cast<float>(i) / 1000.0f;
            const auto p = toCloudParams(with(slot, v), Mode::Fx);
            const double e8 = gg::exp2Det(8.0 * d(v));
            const bool same = slot == ParamSlot::Density ? p.densityHz == gg::exp2Det(d(v))
                              : slot == ParamSlot::Size  ? p.grainDurationSec == gg::exp2Det(d(v))
                              : slot == ParamSlot::Position
                                  ? p.position == 16.0 / 255.0 * (e8 - 1.0)
                                  : p.spray == 2.0 / 255.0 * (e8 - 1.0);
            mismatches += same ? 0 : 1;
        }
        INFO("slot " << static_cast<int>(slot));
        REQUIRE(mismatches == 0);
    }

    // The ends of the tapers in real units.
    using Ends = std::array<double, 3>; // at min, default, max
    const auto ends = [](ParamSlot slot, auto field) {
        const auto &meta = gg::core::paramMeta(slot, Mode::Fx);
        return Ends{field(with(slot, meta.minVal)), field(with(slot, meta.defaultVal)),
                    field(with(slot, meta.maxVal))};
    };
    const auto position = ends(ParamSlot::Position, [](const ParamValues &v) {
        return toCloudParams(v, Mode::Fx).position;
    });
    REQUIRE_THAT(position[0], WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(position[1], WithinRel(0.25, 1e-6));
    REQUIRE_THAT(position[2], WithinRel(16.0, 1e-12));
    const auto size = ends(ParamSlot::Size, [](const ParamValues &v) {
        return toCloudParams(v, Mode::Fx).grainDurationSec;
    });
    REQUIRE_THAT(size[0], WithinRel(0.005, 1e-6));
    REQUIRE_THAT(size[1], WithinRel(0.15, 1e-6));
    REQUIRE(size[2] == 2.0);
    const auto density = ends(ParamSlot::Density, [](const ParamValues &v) {
        return toCloudParams(v, Mode::Fx).densityHz;
    });
    REQUIRE(density[0] == 0.25);
    REQUIRE(density[1] == 16.0);
    REQUIRE_THAT(density[2], WithinRel(400.0, 1e-6));
    const auto spray = ends(ParamSlot::Spray,
                            [](const ParamValues &v) { return toCloudParams(v, Mode::Fx).spray; });
    REQUIRE_THAT(spray[0], WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(spray[1], WithinRel(0.05, 1e-6));
    REQUIRE_THAT(spray[2], WithinRel(2.0, 1e-12));
    const auto level = ends(ParamSlot::Level, [](const ParamValues &v) {
        return static_cast<double>(toWetParams(v, Mode::Fx).levelGain);
    });
    REQUIRE_THAT(level[0], WithinRel(0.001, 1e-6));
    REQUIRE(level[1] == 1.0);
    REQUIRE_THAT(level[2], WithinRel(3.981071705534972, 1e-6));

    // Freeze is on above 0.5 only.
    REQUIRE_FALSE(toWetParams(with(ParamSlot::Freeze, 0.5f), Mode::Fx).freeze);
    REQUIRE(toWetParams(with(ParamSlot::Freeze, std::nextafter(0.5f, 1.0f)), Mode::Fx).freeze);
}

TEST_CASE("space params follow the table", "[core][space]")
{
    using gg::core::kDecayInfinite;
    using gg::core::kDecayMax;
    using gg::core::SpaceParams;

    SECTION("the defaults")
    {
        // Diffusion, Amount and Dry to Reverb 0 (S4); Decay 2 s; Size 50%; Damping 30%;
        // Pre-delay 0; Modulation 25%.
        for (const Mode mode : {Mode::Fx, Mode::Instrument})
        {
            const SpaceParams p = toSpaceParams(defaults(mode), mode);
            CHECK(p.diffusion == 0.0f);
            CHECK(p.amount == 0.0f);
            CHECK(p.drySend == 0.0f);
            CHECK(p.decayLog2 == 1.0f);
            CHECK_FALSE(p.infinite);
            CHECK(p.roomSize == 0.0f);
            CHECK(p.hfDamping == 0.3f);
            CHECK(p.modulation == 0.25f);
            CHECK(p.predelaySeconds == 0.0f);
        }
    }

    SECTION("Size is Reverb 2's room size, 2 Size - 1")
    {
        CHECK(toSpaceParams(with(ParamSlot::ReverbSize, 0.5f), Mode::Fx).roomSize == 0.0f);
        CHECK(toSpaceParams(with(ParamSlot::ReverbSize, 0.0f), Mode::Fx).roomSize == -1.0f);
        CHECK(toSpaceParams(with(ParamSlot::ReverbSize, 1.0f), Mode::Fx).roomSize == 1.0f);
        CHECK(toSpaceParams(with(ParamSlot::ReverbSize, 0.75f), Mode::Fx).roomSize == 0.5f);
    }

    SECTION("Decay is infinite from log2 60 up")
    {
        const auto decay = [](float v) {
            return toSpaceParams(with(ParamSlot::ReverbDecay, v), Mode::Fx);
        };
        CHECK_FALSE(decay(5.9f).infinite);
        CHECK(decay(5.9f).decayLog2 == 5.9f);
        CHECK(decay(5.9069f).infinite);
        CHECK(decay(kDecayInfinite).infinite);
        CHECK_FALSE(decay(std::nextafter(kDecayInfinite, 0.0f)).infinite);
        CHECK(decay(kDecayMax).infinite);
        CHECK_FALSE(decay(-2.0f).infinite);
        CHECK(decay(-2.0f).decayLog2 == -2.0f);
        // log2 60 to the float, and the top 2% of the travel.
        CHECK_THAT(static_cast<double>(kDecayInfinite), WithinAbs(std::log2(60.0), 1e-6));
        CHECK_THAT(static_cast<double>(kDecayMax - kDecayInfinite),
                   WithinRel(0.02 * (static_cast<double>(kDecayMax) + 2.0), 1e-5));
    }

    SECTION("Dry to Reverb sends the FX's input; the instrument has none")
    {
        for (const float v : {0.0f, 0.25f, 0.5f, 1.0f})
        {
            INFO("Dry to Reverb " << v);
            CHECK(toSpaceParams(with(ParamSlot::DryToReverb, v), Mode::Fx).drySend == v);
            CHECK(toSpaceParams(with(ParamSlot::DryToReverb, v, Mode::Instrument), Mode::Instrument)
                      .drySend == 0.0f);
        }
    }

    SECTION("the instrument maps everything else as the FX does")
    {
        for (const auto &v : samplePoints(Mode::Instrument))
        {
            const auto i = toSpaceParams(v, Mode::Instrument);
            const auto f = toSpaceParams(v, Mode::Fx);
            CHECK(bitsOf(i.diffusion) == bitsOf(f.diffusion));
            CHECK(bitsOf(i.amount) == bitsOf(f.amount));
            CHECK(bitsOf(i.decayLog2) == bitsOf(f.decayLog2));
            CHECK(i.infinite == f.infinite);
            CHECK(bitsOf(i.roomSize) == bitsOf(f.roomSize));
            CHECK(bitsOf(i.hfDamping) == bitsOf(f.hfDamping));
            CHECK(bitsOf(i.modulation) == bitsOf(f.modulation));
            CHECK(bitsOf(i.predelaySeconds) == bitsOf(f.predelaySeconds));
        }
    }

    SECTION("the Space slots move neither the cloud nor the wet chain")
    {
        // Nothing reads them yet but toSpaceParams (Task 6 wires the Processor).
        for (const Mode mode : {Mode::Fx, Mode::Instrument})
        {
            const auto cloud = toCloudParams(defaults(mode), mode);
            const auto wet = toWetParams(defaults(mode), mode);
            for (size_t s = static_cast<size_t>(ParamSlot::Diffusion); s < kParamCount; ++s)
            {
                const auto slot = static_cast<ParamSlot>(s);
                if (slot == ParamSlot::Texture)
                {
                    continue; // the grains' envelope, a cloud field (the taper table test)
                }
                if (slot == ParamSlot::Source || slot == ParamSlot::PitchSpread ||
                    slot == ParamSlot::SizeSpread || slot == ParamSlot::Quality ||
                    slot == ParamSlot::Smear || slot == ParamSlot::BusSplit)
                {
                    // Layer A's cloud fields of M2 sub-project 1 (spec §6.4, §6.6): Source picks
                    // Position's mapping, the spreads are read as they are ("layer-aware" below);
                    // sub-project 3's Quality, the tier ("Quality maps ..." below); and
                    // sub-project 4's (its spec §6.1, §6.3): Smear and Bus Split (the taper table
                    // test and the smear tests).
                    continue;
                }
                const auto &meta = gg::core::paramMeta(slot, mode);
                for (const float v : {meta.minVal, meta.maxVal})
                {
                    INFO("slot " << s << " at " << v);
                    const auto values = with(slot, v, mode);
                    CHECK(sameCloudBits(toCloudParams(values, mode), cloud));
                    const auto w = toWetParams(values, mode);
                    CHECK(bitsOf(w.levelGain) == bitsOf(wet.levelGain));
                    CHECK(bitsOf(w.mix) == bitsOf(wet.mix));
                    CHECK(w.freeze == wet.freeze);
                    // Only the FX's Feedback moves feedback; the instrument's is 0 whatever the
                    // value, as it has no ring to feed.
                    if (slot != ParamSlot::Feedback || mode == Mode::Instrument)
                    {
                        CHECK(bitsOf(w.feedback) == bitsOf(wet.feedback));
                    }
                }
            }
        }
    }
}

TEST_CASE("the instrument's Position is a fraction and its Spray is seconds", "[core]")
{
    SECTION("Position is the fraction of the source itself")
    {
        REQUIRE(toCloudParams(with(ParamSlot::Position, 0.25f, Mode::Instrument), Mode::Instrument)
                    .position == 0.25);
        REQUIRE(toCloudParams(with(ParamSlot::Position, 0.0f, Mode::Instrument), Mode::Instrument)
                    .position == 0.0);
        REQUIRE(toCloudParams(with(ParamSlot::Position, 1.0f, Mode::Instrument), Mode::Instrument)
                    .position == 1.0);
        // Its default is the start of the file.
        REQUIRE(toCloudParams(defaults(Mode::Instrument), Mode::Instrument).position == 0.0);
    }

    SECTION("Spray is the FX's seconds, drawn in seconds")
    {
        const auto instrument = toCloudParams(defaults(Mode::Instrument), Mode::Instrument);
        const auto fx = toCloudParams(defaults(Mode::Fx), Mode::Fx);
        REQUIRE(instrument.sprayInSeconds);
        REQUIRE_FALSE(fx.sprayInSeconds);
        REQUIRE(bitsOf(instrument.spray) == bitsOf(fx.spray));
        REQUIRE_THAT(instrument.spray, WithinRel(0.05, 1e-6));
        for (int i = 0; i <= 100; ++i)
        {
            const float v = static_cast<float>(i) / 100.0f;
            INFO("Spray " << v);
            REQUIRE(toCloudParams(with(ParamSlot::Spray, v, Mode::Instrument), Mode::Instrument)
                        .spray == 2.0 / 255.0 * (gg::exp2Det(8.0 * d(v)) - 1.0));
        }
    }

    SECTION("everything else maps as in the FX")
    {
        for (const auto &v : samplePoints(Mode::Instrument))
        {
            REQUIRE(sameApartFromPosition(toCloudParams(v, Mode::Instrument),
                                          toCloudParams(v, Mode::Fx)));
        }
    }

    SECTION("the FX's mapping is unchanged, bit for bit")
    {
        // On a layer that reads its ring, the FX's default kind (M2 sub-project 1's spec §6.4),
        // at M1's tier, Normal (M2 sub-project 3's Quality at its default), and at Smear 0, M1's
        // (Smear's own law: "smear's amplitudes and gain follow the law").
        for (auto v : samplePoints(Mode::Fx))
        {
            v[static_cast<size_t>(ParamSlot::Source)] = 1.0f;  // Live
            v[static_cast<size_t>(ParamSlot::Quality)] = 1.0f; // Normal
            v[static_cast<size_t>(ParamSlot::Smear)] = 0.0f;
            // The fields after M1's are read as they are (M2's spreads, Bus Split); Smear 0
            // leaves its two amplitudes at 0.
            auto expected = fxReference(v);
            expected.pitchSpreadSemitones = at(v, ParamSlot::PitchSpread);
            expected.sizeSpreadOctaves = at(v, ParamSlot::SizeSpread);
            expected.busSplit = at(v, ParamSlot::BusSplit);
            REQUIRE(sameCloudBits(toCloudParams(v, Mode::Fx), expected));
        }
    }

    SECTION("the instrument's Position displays as a percentage")
    {
        const auto &meta = gg::core::paramMeta(ParamSlot::Position, Mode::Instrument);
        REQUIRE(meta.valueToString(0.25f) == std::optional<std::string>("25.00 %"));
        REQUIRE(meta.valueToString(0.0f) == std::optional<std::string>("0.00 %"));
        REQUIRE(meta.valueToString(1.0f) == std::optional<std::string>("100.00 %"));
        // The FX's still shows seconds behind the write head.
        REQUIRE(gg::core::paramMeta(ParamSlot::Position, Mode::Fx).valueToString(0.28967658f) ==
                std::optional<std::string>("0.25 s"));
    }
}

TEST_CASE("the layer-aware maps read each layer's slots", "[core][layers]")
{
    using gg::SourceMode;
    using gg::core::builtKind;
    using gg::core::LayerParam;
    using gg::core::layerSlot;
    using gg::core::SourceKind;
    const auto slotOf = [](LayerParam p, uint8_t layer) {
        return static_cast<size_t>(layerSlot(p, layer));
    };

    SECTION("the built kinds, and the unbuilt ones' fallback")
    {
        // M2 sub-project 1's spec §6.4: Sample and Live are built in both products, and Spectral
        // since sub-project 4's; Resample and Oscillator render as the product's default kind.
        for (const Mode mode : {Mode::Fx, Mode::Instrument})
        {
            CHECK(builtKind(SourceKind::Sample, mode) == SourceMode::Sample);
            CHECK(builtKind(SourceKind::Live, mode) == SourceMode::Live);
        }
        CHECK(builtKind(SourceKind::Resample, Mode::Fx) == SourceMode::Live);
        CHECK(builtKind(SourceKind::Spectral, Mode::Fx) == SourceMode::Spectral);
        CHECK(builtKind(SourceKind::Spectral, Mode::Instrument) == SourceMode::Spectral);
        CHECK(builtKind(SourceKind::Oscillator, Mode::Fx) == SourceMode::Live);
        CHECK(builtKind(SourceKind::Resample, Mode::Instrument) == SourceMode::Sample);
        CHECK(builtKind(SourceKind::Oscillator, Mode::Instrument) == SourceMode::Sample);
    }

    SECTION("Source rounds to the nearest kind")
    {
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            INFO("layer " << int{layer});
            ParamValues v = defaults();
            const auto at = [&](float x) {
                v[slotOf(LayerParam::Source, layer)] = x;
                return gg::core::sourceKindOf(v, layer);
            };
            CHECK(at(0.0f) == SourceKind::Sample);
            CHECK(at(0.49f) == SourceKind::Sample);
            CHECK(at(0.5f) == SourceKind::Live);
            CHECK(at(1.0f) == SourceKind::Live);
            CHECK(at(2.0f) == SourceKind::Resample);
            CHECK(at(2.6f) == SourceKind::Spectral);
            CHECK(at(4.0f) == SourceKind::Oscillator);
        }
    }

    SECTION("each layer's cloud comes from its own slots")
    {
        // The instrument's layer B reads B Position as the fraction of its sample, with its own
        // Density, Size, Pitch, Spray, Stereo, Texture and spreads; layer A's slots don't move it.
        ParamValues v = defaults(Mode::Instrument);
        v[slotOf(LayerParam::Position, 1)] = 0.75f;
        v[slotOf(LayerParam::Density, 1)] = 2.0f;
        v[slotOf(LayerParam::Size, 1)] = -1.0f;
        v[slotOf(LayerParam::Pitch, 1)] = 7.0f;
        v[slotOf(LayerParam::Stereo, 1)] = 0.25f;
        v[slotOf(LayerParam::Texture, 1)] = 0.5f;
        v[slotOf(LayerParam::PitchSpread, 1)] = 3.0f;
        v[slotOf(LayerParam::SizeSpread, 1)] = 0.5f;
        v[static_cast<size_t>(ParamSlot::Position)] = 0.125f; // layer A's
        const gg::CloudParams b = toCloudParams(v, Mode::Instrument, 1);
        CHECK(b.position == 0.75);
        CHECK(b.sprayInSeconds);
        CHECK(b.densityHz == 4.0);
        CHECK(b.grainDurationSec == 0.5);
        CHECK(b.pitchSemitones == 7.0);
        CHECK(b.stereo == 0.25);
        CHECK(b.texture == 0.5);
        CHECK(b.pitchSpreadSemitones == 3.0);
        CHECK(b.sizeSpreadOctaves == 0.5);
        CHECK(b.gain == static_cast<float>(1.0 / std::sqrt(2.0)));
        CHECK(toCloudParams(v, Mode::Instrument, 0).position == 0.125);
        // Every layer at the defaults maps as layer A does, bit for bit.
        for (const Mode mode : {Mode::Fx, Mode::Instrument})
        {
            for (uint8_t layer = 1; layer < gg::core::kLayers; ++layer)
            {
                INFO("layer " << int{layer});
                CHECK(sameBits(toCloudParams(defaults(mode), mode, layer),
                               toCloudParams(defaults(mode), mode)));
            }
        }
    }

    SECTION("Position and Spray follow the layer's kind, not the product")
    {
        // An FX layer set to Sample reads the fraction; an instrument layer set to Live reads
        // seconds behind the head, as the FX's do (fxReference).
        ParamValues fx = defaults(Mode::Fx);
        fx[slotOf(LayerParam::Source, 2)] = 0.0f;
        fx[slotOf(LayerParam::Position, 2)] = 0.4f;
        const gg::CloudParams c = toCloudParams(fx, Mode::Fx, 2);
        CHECK(c.position == d(0.4f));
        CHECK(c.sprayInSeconds);
        ParamValues inst = defaults(Mode::Instrument);
        inst[static_cast<size_t>(ParamSlot::Source)] = 1.0f;
        inst[static_cast<size_t>(ParamSlot::Position)] = 0.28967658f;
        const gg::CloudParams live = toCloudParams(inst, Mode::Instrument);
        CHECK_FALSE(live.sprayInSeconds);
        CHECK(sameBits(live, fxReference(inst)));
        CHECK_THAT(live.position, WithinAbs(0.25, 1e-7));
    }

    SECTION("each layer's wet values: its Level, its Freeze, its Feedback by kind")
    {
        ParamValues v = defaults(Mode::Fx);
        v[static_cast<size_t>(ParamSlot::LevelC)] = -6.0f;
        v[slotOf(LayerParam::Freeze, 2)] = 1.0f;
        v[slotOf(LayerParam::Feedback, 2)] = 0.5f;
        v[static_cast<size_t>(ParamSlot::Mix)] = 0.75f;
        const auto c = toWetParams(v, Mode::Fx, 2);
        CHECK(c.levelGain == static_cast<float>(gg::exp2Det(-6.0 * 3.321928094887362 / 20.0)));
        CHECK(c.mix == 0.75f);
        CHECK(c.freeze);
        CHECK(c.feedback == 0.5f);
        // Layer A's are untouched by layer C's.
        const auto a = toWetParams(v, Mode::Fx, 0);
        CHECK(a.levelGain == 1.0f);
        CHECK_FALSE(a.freeze);
        CHECK(a.feedback == 0.0f);
        // A Sample layer feeds nothing back, in either product; a Live one does in either.
        v[slotOf(LayerParam::Source, 2)] = 0.0f;
        CHECK(toWetParams(v, Mode::Fx, 2).feedback == 0.0f);
        v[slotOf(LayerParam::Source, 2)] = 1.0f;
        CHECK(toWetParams(v, Mode::Instrument, 2).feedback == 0.5f);
        // The unbuilt kinds feed back as the product's default kind does.
        v[slotOf(LayerParam::Source, 2)] = 2.0f;
        CHECK(toWetParams(v, Mode::Fx, 2).feedback == 0.5f);
        CHECK(toWetParams(v, Mode::Instrument, 2).feedback == 0.0f);
        // A Spectral layer takes none, in either product (the extremes spec §6.2).
        v[slotOf(LayerParam::Source, 2)] = 3.0f;
        CHECK(toWetParams(v, Mode::Fx, 2).feedback == 0.0f);
        CHECK(toWetParams(v, Mode::Instrument, 2).feedback == 0.0f);
    }

    SECTION("Diffusion, Layer On and the morph")
    {
        ParamValues v = defaults(Mode::Fx);
        v[slotOf(LayerParam::Diffusion, 3)] = 0.6f;
        CHECK(gg::core::layerDiffusion(v, 3) == 0.6f);
        CHECK(gg::core::layerDiffusion(v, 0) == 0.0f);
        // A on, B to D off by default (spec §6.9); on above 0.5.
        CHECK(gg::core::layerOn(v, 0));
        CHECK_FALSE(gg::core::layerOn(v, 1));
        CHECK_FALSE(gg::core::layerOn(v, 2));
        CHECK_FALSE(gg::core::layerOn(v, 3));
        v[static_cast<size_t>(ParamSlot::LayerCOn)] = 0.5f;
        CHECK_FALSE(gg::core::layerOn(v, 2));
        v[static_cast<size_t>(ParamSlot::LayerCOn)] = 1.0f;
        CHECK(gg::core::layerOn(v, 2));
        const auto rest = gg::core::toMorphParams(v);
        CHECK(rest.x == 0.5f);
        CHECK(rest.y == 0.5f);
        v[static_cast<size_t>(ParamSlot::MorphX)] = 0.2f;
        v[static_cast<size_t>(ParamSlot::MorphY)] = 0.9f;
        const auto moved = gg::core::toMorphParams(v);
        CHECK(moved.x == 0.2f);
        CHECK(moved.y == 0.9f);
    }
}

TEST_CASE("bus, delay and limiter readers map their slots", "[core]")
{
    // M2 sub-project 4's spec §6.7: the Extremes' forty parameters as the chain will read them.
    // Every exp2 is exp2Det's, bit for bit; the literal targets are checked within a tolerance.
    using gg::core::BusFilterType;
    using gg::core::LayerParam;
    using gg::core::layerSlot;
    using gg::core::LimiterMode;
    const auto slotOf = [](LayerParam p, uint8_t layer) {
        return static_cast<size_t>(layerSlot(p, layer));
    };
    const auto exp2Of = [](float v) { return static_cast<float>(gg::exp2Det(d(v))); };

    SECTION("the defaults")
    {
        for (const Mode mode : {Mode::Fx, Mode::Instrument})
        {
            const ParamValues v = defaults(mode);
            for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
            {
                INFO("layer " << int{layer});
                CHECK(gg::core::layerSmear(v, layer) == 0.0f);
                CHECK(gg::core::layerBusSplit(v, layer) == 0.0f);
                const gg::core::BusParams b = gg::core::busParams(v, layer);
                for (size_t bus = 0; bus < 2; ++bus)
                {
                    CHECK(b.type[bus] == BusFilterType::Off);
                    CHECK(b.cutoffHz[bus] == exp2Of(9.965784284662087f));
                    CHECK_THAT(b.cutoffHz[bus], WithinRel(1000.0f, 1e-5f));
                    CHECK(b.resonance[bus] == 0.0f);
                }
            }
            const gg::core::DelayParams delay = gg::core::delayParams(v);
            CHECK(delay.amount == 0.0f);
            CHECK(delay.timeSeconds == exp2Of(-1.415037499278844f));
            CHECK_THAT(delay.timeSeconds, WithinRel(0.375f, 1e-5f));
            CHECK(delay.feedback == 0.4f);
            CHECK(delay.damping == 0.3f);
            CHECK_FALSE(delay.pingPong);
            const gg::core::LimiterParams limiter = gg::core::limiterParams(v);
            CHECK(limiter.mode == LimiterMode::Off);
            CHECK(limiter.ceilingGain ==
                  static_cast<float>(gg::exp2Det(-1.0 * 3.321928094887362 / 20.0)));
            CHECK_THAT(limiter.ceilingGain, WithinRel(0.8912509f, 1e-6f));
            CHECK(limiter.releaseSeconds == exp2Of(-3.321928094887362f));
            CHECK_THAT(limiter.releaseSeconds, WithinRel(0.1f, 1e-5f));
        }
    }

    SECTION("each layer's Smear, Bus Split and bus filters come from its own slots")
    {
        ParamValues v = defaults();
        v[slotOf(LayerParam::Smear, 2)] = 0.3f;
        v[slotOf(LayerParam::BusSplit, 2)] = 0.7f;
        v[slotOf(LayerParam::Bus1Filter, 2)] = 1.0f;
        v[slotOf(LayerParam::Bus1Cutoff, 2)] = 12.0f;
        v[slotOf(LayerParam::Bus1Resonance, 2)] = 0.25f;
        v[slotOf(LayerParam::Bus2Filter, 2)] = 3.0f;
        v[slotOf(LayerParam::Bus2Cutoff, 2)] = 9.965784284662087f;
        v[slotOf(LayerParam::Bus2Resonance, 2)] = 0.9f;
        CHECK(gg::core::layerSmear(v, 2) == 0.3f);
        CHECK(gg::core::layerBusSplit(v, 2) == 0.7f);
        const gg::core::BusParams c = gg::core::busParams(v, 2);
        CHECK(c.type[0] == BusFilterType::LowPass);
        CHECK(c.cutoffHz[0] == exp2Of(12.0f));
        CHECK(c.cutoffHz[0] == 4096.0f);
        CHECK(c.resonance[0] == 0.25f);
        CHECK(c.type[1] == BusFilterType::BandPass);
        CHECK_THAT(c.cutoffHz[1], WithinRel(1000.0f, 1e-5f));
        CHECK(c.resonance[1] == 0.9f);
        // Layer A's are untouched by layer C's.
        CHECK(gg::core::layerSmear(v, 0) == 0.0f);
        CHECK(gg::core::layerBusSplit(v, 0) == 0.0f);
        CHECK(gg::core::busParams(v, 0).type[0] == BusFilterType::Off);
        CHECK(gg::core::busParams(v, 0).resonance[1] == 0.0f);
    }

    SECTION("the filter type rounds to the nearest kind, halves up")
    {
        ParamValues v = defaults();
        const auto at = [&](float x) {
            v[slotOf(LayerParam::Bus2Filter, 1)] = x;
            return gg::core::busParams(v, 1).type[1];
        };
        CHECK(at(0.0f) == BusFilterType::Off);
        CHECK(at(0.49f) == BusFilterType::Off);
        CHECK(at(0.5f) == BusFilterType::LowPass);
        CHECK(at(2.0f) == BusFilterType::HighPass);
        CHECK(at(2.5f) == BusFilterType::BandPass);
        CHECK(at(3.0f) == BusFilterType::BandPass);
        CHECK(at(4.0f) == BusFilterType::Notch);
    }

    SECTION("the delay's and the limiter's globals")
    {
        ParamValues v = defaults();
        v[static_cast<size_t>(ParamSlot::DelayAmount)] = 0.6f;
        v[static_cast<size_t>(ParamSlot::DelayTime)] = -1.0f;
        v[static_cast<size_t>(ParamSlot::DelayFeedback)] = 0.8f;
        v[static_cast<size_t>(ParamSlot::DelayDamping)] = 0.1f;
        v[static_cast<size_t>(ParamSlot::DelayPingPong)] = 1.0f;
        v[static_cast<size_t>(ParamSlot::LimiterMode)] = 2.0f;
        v[static_cast<size_t>(ParamSlot::LimiterCeiling)] = -6.0f;
        v[static_cast<size_t>(ParamSlot::LimiterRelease)] = -1.0f;
        const gg::core::DelayParams delay = gg::core::delayParams(v);
        CHECK(delay.amount == 0.6f);
        CHECK(delay.timeSeconds == 0.5f);
        CHECK(delay.feedback == 0.8f);
        CHECK(delay.damping == 0.1f);
        CHECK(delay.pingPong);
        const gg::core::LimiterParams limiter = gg::core::limiterParams(v);
        CHECK(limiter.mode == LimiterMode::Peak);
        CHECK(limiter.ceilingGain ==
              static_cast<float>(gg::exp2Det(-6.0 * 3.321928094887362 / 20.0)));
        CHECK(limiter.releaseSeconds == 0.5f);
        // Ping-pong is on above 0.5; the mode rounds halves up.
        v[static_cast<size_t>(ParamSlot::DelayPingPong)] = 0.5f;
        CHECK_FALSE(gg::core::delayParams(v).pingPong);
        v[static_cast<size_t>(ParamSlot::LimiterMode)] = 0.5f;
        CHECK(gg::core::limiterParams(v).mode == LimiterMode::Soft);
        v[static_cast<size_t>(ParamSlot::LimiterMode)] = 0.49f;
        CHECK(gg::core::limiterParams(v).mode == LimiterMode::Off);
        v[static_cast<size_t>(ParamSlot::LimiterCeiling)] = 0.0f;
        CHECK(gg::core::limiterParams(v).ceilingGain == 1.0f);
    }
}

TEST_CASE("smear 0 is M1's cloud params bit for bit", "[core]")
{
    // M2 sub-project 4's spec §6.1: at Smear 0 nothing changes, so every M1 and wave-1 grain is
    // bit for bit. Over every slot's range at once (samplePoints, every Source kind among them),
    // with each layer's Smear set to 0, every layer maps as before, the jitter and the detune are
    // +0 (so the scheduler draws neither), and the material changes nothing for a Sample or a
    // Live layer (a Spectral layer's Position takes the material's units).
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        INFO("mode " << (mode == Mode::Fx ? "fx" : "instrument"));
        const std::vector<ParamValues> points = samplePoints(mode);
        for (size_t i = 0; i < points.size(); ++i)
        {
            ParamValues v = points[i];
            for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
            {
                v[static_cast<size_t>(gg::core::layerSlot(gg::core::LayerParam::Smear, layer))] =
                    0.0f;
            }
            for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
            {
                INFO("point " << i << ", layer " << int{layer});
                const gg::CloudParams p = toCloudParams(v, mode, layer);
                REQUIRE(sameCloudBits(p, preSmearReference(v, mode, layer)));
                REQUIRE(bitsOf(p.smearJitterSec) == bitsOf(0.0));
                REQUIRE(bitsOf(p.smearDetuneSemitones) == bitsOf(0.0));
                if (gg::core::builtKind(gg::core::sourceKindOf(v, layer), mode) ==
                    gg::SourceMode::Spectral)
                {
                    continue; // a Spectral layer's Position takes its material's units
                }
                for (const gg::SourceMode material : {gg::SourceMode::Sample, gg::SourceMode::Live})
                {
                    REQUIRE(sameCloudBits(toCloudParams(v, mode, layer, material), p));
                }
            }
        }
        // Layer A's two-argument form is the same mapping.
        REQUIRE(sameCloudBits(toCloudParams(defaults(mode), mode),
                              preSmearReference(defaults(mode), mode, 0)));
    }
}

TEST_CASE("smear raises the density toward the overlap target", "[core]")
{
    // D 16 Hz, S 0.15 s (the defaults): at Smear 1 the target overlap is 32, so D_eff = 32 / S;
    // at 0.5 it is 20, and D_eff is half-way from D to 20 / S.
    using Catch::Approx;
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            INFO("layer " << int{layer});
            const ParamValues base = defaults(mode);
            const auto density = [&](float smear, float d) {
                return toCloudParams(smeared(base, layer, smear, d, kLog2Of150Ms), mode, layer)
                    .densityHz;
            };
            CHECK(density(1.0f, kLog2Of16Hz) == Approx(32.0 / 0.15));
            CHECK(density(0.5f, kLog2Of16Hz) == Approx(16.0 + 0.5 * (20.0 / 0.15 - 16.0)));
            CHECK(density(0.0f, kLog2Of16Hz) == 16.0);
            // A density already above the target (about 294 Hz against 213 Hz at Smear 1, and
            // 133 Hz at 0.5) is left alone, bit for bit.
            const double D = gg::exp2Det(d(8.2f));
            CHECK(density(1.0f, 8.2f) == D);
            CHECK(density(0.5f, 8.2f) == D);
            // Never below D, and rising with Smear.
            double last = 16.0;
            for (int k = 1; k <= 10; ++k)
            {
                const double now = density(static_cast<float>(k) / 10.0f, kLog2Of16Hz);
                CHECK(now > last);
                last = now;
            }
        }
    }
}

TEST_CASE("smear's density cap binds at tiny sizes", "[core]")
{
    // At S 5 ms the target 32 / S is 6400 Hz: the Density parameter's 400 Hz ceiling caps it, so
    // the floor isn't reached, and the gain stays finite (the plan's Review Focus 5).
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        const ParamValues base = defaults(mode);
        const auto at = [&](float smear, float density) {
            return toCloudParams(smeared(base, 1, smear, density, kLog2Of5Ms), mode, 1);
        };
        const gg::CloudParams top = at(1.0f, kLog2Of400Hz);
        CHECK(top.densityHz == 400.0);
        CHECK(std::isfinite(top.gain));
        CHECK(top.gain > 0.0f);
        CHECK(top.gain == static_cast<float>(1.0 / std::sqrt(400.0 * top.grainDurationSec)));
        CHECK_THAT(top.gain, WithinRel(static_cast<float>(1.0 / std::sqrt(2.0)), 1e-5f));
        // From 16 Hz the cap binds too: D_eff runs to 400 Hz at Smear 1, half-way at 0.5.
        const gg::CloudParams low = at(1.0f, kLog2Of16Hz);
        CHECK(low.densityHz == 400.0);
        CHECK(std::isfinite(low.gain));
        CHECK(at(0.5f, kLog2Of16Hz).densityHz == 208.0);
    }
}

TEST_CASE("smear's amplitudes and gain follow the law", "[core]")
{
    // jitter 0.5 × s × S seconds, detune 0.5 × s semitones, D_eff by the law and the latched
    // gain 1 / sqrt(max(1, D_eff × S)), bit for bit, over Smear, Density and Size, in every layer
    // of both products; the material changes none of it for a Sample or a Live layer.
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        const ParamValues base = defaults(mode);
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            for (const float smear : {0.1f, 0.25f, 0.5f, 0.75f, 1.0f})
            {
                for (const float density : {-2.0f, 0.0f, kLog2Of16Hz, 6.5f, kLog2Of400Hz})
                {
                    for (const float size : {kLog2Of5Ms, -4.0f, kLog2Of150Ms, 0.0f, 1.0f})
                    {
                        INFO("layer " << int{layer} << ", smear " << smear << ", density "
                                      << density << ", size " << size);
                        const ParamValues v = smeared(base, layer, smear, density, size);
                        const gg::CloudParams p = toCloudParams(v, mode, layer);
                        const double s = d(smear);
                        const double D = gg::exp2Det(d(density));
                        const double S = gg::exp2Det(d(size));
                        const double Deff = smearDensity(D, S, s);
                        REQUIRE(bitsOf(p.grainDurationSec) == bitsOf(S));
                        REQUIRE(bitsOf(p.densityHz) == bitsOf(Deff));
                        REQUIRE(bitsOf(p.smearJitterSec) == bitsOf(0.5 * s * S));
                        REQUIRE(bitsOf(p.smearDetuneSemitones) == bitsOf(0.5 * s));
                        REQUIRE(bitsOf(p.gain) == bitsOf(static_cast<float>(
                                                      1.0 / std::sqrt(std::max(1.0, Deff * S)))));
                        // D_eff stays between D and the 400 Hz ceiling.
                        REQUIRE(p.densityHz >= std::min(D, 400.0));
                        REQUIRE(p.densityHz <= std::max(D, 400.0));
                        // Smear moves no other field.
                        gg::CloudParams expected = preSmearReference(v, mode, layer);
                        expected.densityHz = Deff;
                        expected.smearJitterSec = 0.5 * s * S;
                        expected.smearDetuneSemitones = 0.5 * s;
                        expected.gain =
                            static_cast<float>(1.0 / std::sqrt(std::max(1.0, Deff * S)));
                        REQUIRE(sameCloudBits(p, expected));
                        for (const gg::SourceMode material :
                             {gg::SourceMode::Sample, gg::SourceMode::Live})
                        {
                            REQUIRE(sameCloudBits(toCloudParams(v, mode, layer, material), p));
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("the cloud's bus split is Bus Split", "[core]")
{
    // The extremes spec §6.3: busSplit is each layer's Bus Split, as it is.
    ParamValues v = defaults();
    v[static_cast<size_t>(gg::core::layerSlot(gg::core::LayerParam::BusSplit, 2))] = 0.7f;
    CHECK(toCloudParams(v, Mode::Fx, 2).busSplit == d(0.7f));
    CHECK(toCloudParams(v, Mode::Fx, 0).busSplit == 0.0);
    CHECK(toCloudParams(defaults(Mode::Instrument), Mode::Instrument, 3).busSplit == 0.0);
}

TEST_CASE("Quality maps to the layer's tier and Budget to units", "[core][layers]")
{
    // M2 sub-project 3's spec §6.1, §6.4: each layer's Quality, rounded to the nearest step, is
    // its CloudParams::quality (0 Eco, 1 Normal, 2 HQ); Budget, a count of Normal stereo grains
    // rounded and clamped to 256..16,384, is that many times kBudgetUnitsPerGrain weight units.
    using gg::Tier;
    using gg::core::budgetUnits;
    using gg::core::kBudgetUnitsPerGrain;
    using gg::core::LayerParam;
    using gg::core::layerSlot;
    STATIC_REQUIRE(kBudgetUnitsPerGrain == 4u);
    STATIC_REQUIRE(static_cast<int>(Tier::Eco) == 0);
    STATIC_REQUIRE(static_cast<int>(Tier::Normal) == 1);
    STATIC_REQUIRE(static_cast<int>(Tier::HQ) == 2);

    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        INFO((mode == Mode::Fx ? "fx" : "instrument"));
        // The default is Normal on every layer, as M1's.
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            CHECK(toCloudParams(defaults(mode), mode, layer).quality == Tier::Normal);
        }
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            INFO("layer " << int{layer});
            const auto quality = [&](float x) {
                ParamValues v = defaults(mode);
                // Clamped: GCC 13's -Warray-bounds sees layerSlot's Count sentinel otherwise
                // (CLAUDE.md, "GCC 13's own diagnostics").
                const size_t slot = std::min<size_t>(
                    static_cast<size_t>(layerSlot(LayerParam::Quality, layer)), kParamCount - 1);
                v[slot] = x;
                // Only this layer's tier moves.
                for (uint8_t other = 0; other < gg::core::kLayers; ++other)
                {
                    if (other != layer)
                    {
                        CHECK(toCloudParams(v, mode, other).quality == Tier::Normal);
                    }
                }
                return toCloudParams(v, mode, layer).quality;
            };
            CHECK(quality(0.0f) == Tier::Eco);
            CHECK(quality(0.4f) == Tier::Eco);
            CHECK(quality(0.5f) == Tier::Normal);
            CHECK(quality(1.0f) == Tier::Normal);
            CHECK(quality(1.49f) == Tier::Normal);
            CHECK(quality(1.5f) == Tier::HQ);
            CHECK(quality(2.0f) == Tier::HQ);
            // Out of range (the Processor clamps first): the nearest end.
            CHECK(quality(-3.0f) == Tier::Eco);
            CHECK(quality(7.0f) == Tier::HQ);
        }
    }

    const auto budget = [](float x) { return budgetUnits(with(ParamSlot::Budget, x)); };
    CHECK(budgetUnits(defaults()) == 4096u * 4u);
    CHECK(budgetUnits(defaults(Mode::Instrument)) == 4096u * 4u);
    CHECK(budget(4096.0f) == 16384u);
    CHECK(budget(256.0f) == 256u * 4u);
    CHECK(budget(16384.0f) == 16384u * 4u);
    CHECK(budget(1000.4f) == 1000u * 4u);
    CHECK(budget(1000.5f) == 1001u * 4u);
    CHECK(budget(100.0f) == 256u * 4u);
    CHECK(budget(1e9f) == 16384u * 4u);
    CHECK(budget(-1e9f) == 256u * 4u);
    CHECK(budget(std::numeric_limits<float>::infinity()) == 16384u * 4u);
    CHECK(budget(-std::numeric_limits<float>::infinity()) == 256u * 4u);
    CHECK(budget(std::numeric_limits<float>::quiet_NaN()) == 4096u * 4u);
}

TEST_CASE("soft limiter is continuous and bounded", "[core]")
{
    constexpr float kBelowOne = 0x1.fffffep-1f; // the largest float below 1
    const float inf = std::numeric_limits<float>::infinity();
    const float maxFloat = std::numeric_limits<float>::max();

    // Linear, bit for bit, up to the knee, both signs and both zeros included.
    REQUIRE(softLimit(0.5f) == 0.5f);
    REQUIRE(softLimit(-0.5f) == -0.5f);
    REQUIRE(bitsOf(softLimit(0.0f)) == bitsOf(0.0f));
    REQUIRE(bitsOf(softLimit(-0.0f)) == bitsOf(-0.0f));
    // Big loops assert only on a failure: a REQUIRE per point would dominate the run time.
    for (uint32_t b = 0; b <= bitsOf(0.5f); b += 4099)
    {
        const float x = fromBits(b);
        if (bitsOf(softLimit(x)) != b || bitsOf(softLimit(-x)) != bitsOf(-x))
        {
            INFO("x = " << x);
            REQUIRE(bitsOf(softLimit(x)) == b);
            REQUIRE(bitsOf(softLimit(-x)) == bitsOf(-x));
        }
    }

    // Monotonic over [-100, 100]: every float from the knee to 100 (the curve is odd, checked
    // below), and a uniform sweep across the whole interval.
    {
        float previous = softLimit(0.5f);
        for (uint32_t b = bitsOf(0.5f) + 1; b <= bitsOf(100.0f); ++b)
        {
            const float y = softLimit(fromBits(b));
            if (!(y >= previous))
            {
                INFO("x = " << fromBits(b));
                REQUIRE(y >= previous);
            }
            previous = y;
        }
        previous = softLimit(-100.0f);
        for (int i = -100 * 4096 + 1; i <= 100 * 4096; ++i)
        {
            const float x = static_cast<float>(i) / 4096.0f;
            const float y = softLimit(x);
            if (!(y >= previous))
            {
                INFO("x = " << x);
                REQUIRE(y >= previous);
            }
            previous = y;
        }
    }

    // The brief's formula, to float rounding, and odd.
    for (int i = 0; i <= 200000; ++i)
    {
        const float x = 0.5f + static_cast<float>(i) * 0.0005f; // 0.5 .. 100.5
        const double y = static_cast<double>(softLimit(x));
        if (!(std::abs(y - referenceLimit(d(x))) <= 2.5e-7) || softLimit(-x) != -softLimit(x))
        {
            INFO("x = " << x);
            REQUIRE_THAT(y, WithinAbs(referenceLimit(d(x)), 2.5e-7));
            REQUIRE(softLimit(-x) == -softLimit(x));
        }
    }

    // Continuous at the knee: just above it the curve is still within a few ulp of the line.
    const float above = std::nextafter(0.5f, 1.0f);
    REQUIRE_THAT(static_cast<double>(softLimit(above)), WithinAbs(d(above), 1e-7));

    // |softLimit(x)| < 1 for every x: a stride through every float bit pattern, and the extremes.
    for (uint64_t b = 0; b <= 0xffff'ffffull; b += 997)
    {
        const float x = fromBits(static_cast<uint32_t>(b));
        if (!std::isnan(x))
        {
            const float y = softLimit(x);
            if (!(std::abs(y) < 1.0f))
            {
                INFO("x = " << x);
                REQUIRE(std::abs(y) < 1.0f);
            }
        }
    }
    for (const float x : {100.0f, 1e6f, 0x1p24f, 1e30f, maxFloat, inf})
    {
        INFO("x = " << x);
        REQUIRE(softLimit(x) < 1.0f);
        REQUIRE(softLimit(x) > 0.99f);
        REQUIRE(softLimit(-x) == -softLimit(x));
    }
    REQUIRE(softLimit(inf) == kBelowOne);
    REQUIRE(softLimit(-inf) == -kBelowOne);
    // NaN, which nothing upstream should produce, is silence rather than a NaN on the output.
    REQUIRE(softLimit(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
}

namespace
{
// The macros' offsets as the Processor computes them since the matrix replaced macroOffsets (M2
// sub-project 1's spec §6.8): the factory routes over the macros' values in `m`.
ParamValues factoryOffsets(const ParamValues &m, Mode mode)
{
    std::array<gg::core::ModSourceValues, gg::core::kLayers> sources{};
    for (auto &layer : sources)
    {
        for (size_t i = 0; i < gg::core::kMacroSlots.size(); ++i)
        {
            layer.v[static_cast<size_t>(gg::core::ModSource::Macro1) + i] =
                m[static_cast<size_t>(gg::core::kMacroSlots[i])];
        }
    }
    ParamValues o{};
    gg::core::applyRoutes(gg::core::factoryRoutes(mode), sources, o);
    return o;
}

// The recipes' expected offsets (sub-project 5's spec §3.2), written out in double from the
// table: one macro at `value`, the other macros at rest. A layer parameter's or Level's offset
// reaches every layer (M2 sub-project 1's spec §6.8: the factory routes target every layer).
ParamValues expectedOffsets(ParamSlot macro, double value, Mode mode)
{
    std::array<double, kParamCount> o{};
    const double b = 2.0 * value - 1.0;
    const double bPlus = std::max(b, 0.0);
    const double u = value;
    const auto add = [&o](ParamSlot slot, double x) {
        if (!gg::core::isLayerTarget(slot))
        {
            o[static_cast<size_t>(slot)] += x;
            return;
        }
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            o[static_cast<size_t>(gg::core::routeTargetSlot(slot, layer))] += x;
        }
    };
    switch (macro)
    {
    case ParamSlot::Macro1:
        add(ParamSlot::Size, 3.0 * b);
        add(ParamSlot::Density, -1.5 * b);
        break;
    case ParamSlot::Macro2:
        add(ParamSlot::Density, 4.0 * b);
        add(ParamSlot::Distribution, 0.25 * bPlus);
        break;
    case ParamSlot::Macro3:
        add(ParamSlot::Pitch, 24.0 * b);
        break;
    case ParamSlot::Macro4:
        add(ParamSlot::Spray, 0.5 * b);
        add(ParamSlot::Stereo, 0.3 * bPlus);
        break;
    case ParamSlot::Macro5:
    {
        add(ParamSlot::Texture, std::min(u / 0.75, 1.0));
        const double s = std::clamp((u - 0.75) / (1.0 - 0.75), 0.0, 1.0);
        add(ParamSlot::Diffusion, s * s * (3.0 - 2.0 * s));
        break;
    }
    case ParamSlot::Macro6:
        add(ParamSlot::Freeze, u);
        break;
    case ParamSlot::Macro7:
        add(ParamSlot::Feedback, 0.9 * u * u); // both products (M2 sub-project 1's spec §6.9)
        break;
    case ParamSlot::Macro8:
        if (mode == Mode::Fx)
        {
            add(ParamSlot::Mix, 0.5 * b);
        }
        else
        {
            add(ParamSlot::Level, 12.0 * b);
        }
        break;
    default:
        FAIL("not a macro");
    }
    ParamValues f{};
    for (size_t s = 0; s < kParamCount; ++s)
    {
        f[s] = static_cast<float>(o[s]);
    }
    return f;
}
} // namespace

TEST_CASE("the factory routes' offsets follow the macro recipes", "[core]")
{
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        for (const ParamSlot macro : gg::core::kMacroSlots)
        {
            for (const float v : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
            {
                INFO("mode " << static_cast<int>(mode) << ", macro " << static_cast<int>(macro)
                             << " at " << v);
                // Every macro rests but this one; the other slots' values are ignored.
                ParamValues m = defaults(mode);
                m[static_cast<size_t>(macro)] = v;
                const ParamValues offsets = factoryOffsets(m, mode);
                const ParamValues expected = expectedOffsets(macro, d(v), mode);
                for (size_t s = 0; s < kParamCount; ++s)
                {
                    INFO("slot " << s);
                    REQUIRE_THAT(d(offsets[s]), WithinAbs(d(expected[s]), 1e-6));
                }
            }
        }
    }

    SECTION("the offsets of several macros add up")
    {
        // Macros 1 and 2 both move Density; Macro 4 moves Stereo, which the instrument's Macro 7
        // no longer does (it is Macro Feedback in both products since M2 sub-project 1).
        ParamValues m = defaults(Mode::Instrument);
        m[static_cast<size_t>(ParamSlot::Macro1)] = 1.0f;
        m[static_cast<size_t>(ParamSlot::Macro2)] = 0.75f;
        m[static_cast<size_t>(ParamSlot::Macro4)] = 1.0f;
        m[static_cast<size_t>(ParamSlot::Macro7)] = 0.25f;
        const ParamValues o = factoryOffsets(m, Mode::Instrument);
        REQUIRE_THAT(d(o[static_cast<size_t>(ParamSlot::Density)]), WithinAbs(-1.5 + 2.0, 1e-6));
        REQUIRE_THAT(d(o[static_cast<size_t>(ParamSlot::Stereo)]), WithinAbs(0.3, 1e-6));
        REQUIRE_THAT(d(o[static_cast<size_t>(ParamSlot::Feedback)]),
                     WithinAbs(0.9 * 0.25 * 0.25, 1e-6));
        REQUIRE_THAT(d(o[static_cast<size_t>(ParamSlot::Size)]), WithinAbs(3.0, 1e-6));
    }

    SECTION("the macros' own offsets are 0")
    {
        for (const Mode mode : {Mode::Fx, Mode::Instrument})
        {
            ParamValues m{};
            m.fill(1.0f);
            const ParamValues o = factoryOffsets(m, mode);
            for (const ParamSlot macro : gg::core::kMacroSlots)
            {
                REQUIRE(o[static_cast<size_t>(macro)] == 0.0f);
            }
        }
    }
}

TEST_CASE("resting macros offset nothing through the factory routes", "[core]")
{
    // Spec §3.1: a bipolar macro at 0.5 and a unipolar one at 0 contribute exactly 0, so a
    // resting preset is bit-identical to its base values. Every entry is +0.0f, the sign
    // included: base + (+0.0f) is base, bit for bit, for every base but −0.
    for (const Mode mode : {Mode::Fx, Mode::Instrument})
    {
        const ParamValues o = factoryOffsets(defaults(mode), mode);
        for (size_t s = 0; s < kParamCount; ++s)
        {
            INFO("mode " << static_cast<int>(mode) << ", slot " << s);
            REQUIRE(bitsOf(o[s]) == bitsOf(0.0f));
        }
    }
}

TEST_CASE("only LFO 1 Rate to Step 16 are modulator controls", "[core]")
{
    // The Processor writes a modulator control's effective value as base + modulation (no route
    // targets one), so a parameter past Step 16 must not be taken for one (the bus filters and
    // Smear have routable targets).
    using gg::core::isModulatorControlSlot;
    using gg::core::kLayers;
    using gg::core::LayerParam;
    for (uint8_t L = 0; L < kLayers; ++L)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(LayerParam::Count); ++i)
        {
            const auto p = static_cast<LayerParam>(i);
            const bool expected = p >= LayerParam::Lfo1Rate && p <= LayerParam::Step16;
            INFO("layer " << int(L) << ", parameter " << i);
            CHECK(isModulatorControlSlot(gg::core::layerSlot(p, L)) == expected);
        }
    }
    for (const LayerParam p :
         {LayerParam::Smear, LayerParam::BusSplit, LayerParam::Bus1Filter, LayerParam::Bus1Cutoff,
          LayerParam::Bus1Resonance, LayerParam::Bus2Filter, LayerParam::Bus2Cutoff,
          LayerParam::Bus2Resonance})
    {
        CHECK_FALSE(isModulatorControlSlot(gg::core::layerSlot(p, 2)));
    }
    CHECK_FALSE(isModulatorControlSlot(gg::core::ParamSlot::Mix));
    CHECK(isModulatorControlSlot(gg::core::ParamSlot::Lfo1Rate));
}

TEST_CASE("smoothstep", "[core]")
{
    using gg::core::smoothstep;
    STATIC_REQUIRE(smoothstep(0.75f, 1.0f, 0.875f) == 0.5f);
    REQUIRE(smoothstep(0.75f, 1.0f, 0.0f) == 0.0f);
    REQUIRE(smoothstep(0.75f, 1.0f, 0.75f) == 0.0f);
    REQUIRE(smoothstep(0.75f, 1.0f, 0.5f) == 0.0f);
    REQUIRE(smoothstep(0.75f, 1.0f, 1.0f) == 1.0f);
    REQUIRE(smoothstep(0.75f, 1.0f, 2.0f) == 1.0f);
    REQUIRE(smoothstep(2.0f, 4.0f, 3.0f) == 0.5f);
    // s²(3 − 2s) at s = 0.25.
    REQUIRE_THAT(d(smoothstep(0.0f, 1.0f, 0.25f)), WithinAbs(0.15625, 1e-7));
    // Monotonic across the edge.
    float previous = 0.0f;
    for (int i = 0; i <= 400; ++i)
    {
        const float x = 0.7f + 0.35f * static_cast<float>(i) / 400.0f;
        const float y = smoothstep(0.75f, 1.0f, x);
        REQUIRE(y >= previous);
        previous = y;
    }
}
