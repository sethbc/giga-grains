// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// ParamMap: the patch's values to the engine's parameters (Global Constraints' parameter table),
// and the wet chain's soft limiter (spec §6.4).
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

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gg/core/MathDet.h"
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
           bitsOf(a.stereo) == bitsOf(b.stereo) && bitsOf(a.gain) == bitsOf(b.gain) &&
           a.quality == b.quality;
}

bool sameBits(const gg::CloudParams &a, const gg::CloudParams &b)
{
    return sameApartFromPosition(a, b) && bitsOf(a.position) == bitsOf(b.position) &&
           a.sprayInSeconds == b.sprayInSeconds;
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
            const auto wet = toWetParams(values);
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
        return static_cast<double>(toWetParams(v).levelGain);
    });
    REQUIRE_THAT(level[0], WithinRel(0.001, 1e-6));
    REQUIRE(level[1] == 1.0);
    REQUIRE_THAT(level[2], WithinRel(3.981071705534972, 1e-6));

    // Freeze is on above 0.5 only.
    REQUIRE_FALSE(toWetParams(with(ParamSlot::Freeze, 0.5f)).freeze);
    REQUIRE(toWetParams(with(ParamSlot::Freeze, std::nextafter(0.5f, 1.0f))).freeze);
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
        for (const auto &v : samplePoints(Mode::Fx))
        {
            REQUIRE(sameBits(toCloudParams(v, Mode::Fx), fxReference(v)));
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
