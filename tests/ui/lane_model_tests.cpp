// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// LaneModel: the canvas's JUCE-free geometry (spec §6.2-6.3): where a playing grain's capsule
// lies on the lane, where the scan window lies, and the inverse mappings a drag needs. These
// tests need no editor or JUCE (one compares the tapers with gg-core's ParamMap), so they also
// run in the sanitizer builds.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/Rng.h"
#include "gg/ui/LaneModel.h"
#include "gg/ui/LaneState.h"

using Catch::Matchers::WithinAbs;
using gg::ui::DragAxis;
using gg::ui::DragTarget;
using gg::ui::LaneGeometry;
using gg::ui::LaneGrain;
using gg::ui::ScanRectValues;
using gg::ui::WindowRect;

namespace
{
constexpr double kRate = 48000.0;

/// An FX lane: the 16 s live ring across `width` pixels, the head at the right edge.
LaneGeometry fxLane(float width = 1600.0f, float height = 100.0f, float pixelScale = 1.0f)
{
    return {.width = width,
            .height = height,
            .pixelScale = pixelScale,
            .fxLane = true,
            .sampleRate = kRate,
            .ringSeconds = 16.0,
            .sourceFrames = 0};
}

/// An instrument lane over a file of `frames` frames.
LaneGeometry instrumentLane(int64_t frames, float width = 800.0f, float height = 100.0f)
{
    return {.width = width,
            .height = height,
            .pixelScale = 1.0f,
            .fxLane = false,
            .sampleRate = kRate,
            .ringSeconds = 16.0,
            .sourceFrames = frames};
}

/// A grain at onset `onset` reading from `startPos`, 100 ms at rate 1, centred, gain 0.5.
LaneGrain grainAt(int64_t onset, double startPos)
{
    return {.onsetSample = onset,
            .startPos = startPos,
            .rate = 1.0f,
            .durationSamples = 4800.0f,
            .gain = 0.5f,
            .pan = 0.0f};
}

/// `g` moved down to start at `top`, as the canvas's lanes B to D are.
LaneGeometry atTop(LaneGeometry g, float top)
{
    g.top = top;
    return g;
}

/// The FX taper's inverse, in double: the Position value for `seconds` behind the head.
double fxPositionValue(double seconds) { return std::log2(1.0 + 255.0 * seconds / 16.0) / 8.0; }
} // namespace

TEST_CASE("the fx lane puts the head at the right edge", "[ui]")
{
    const LaneGeometry g = fxLane();
    const int64_t head = 600 * 48000; // ten minutes in: ring time is absolute
    const int64_t onset = head - 100;

    // 8 s behind the head is the middle of the 16 s lane; the capsule spans the grain's read
    // extent, 100 ms = 10 px further on.
    const auto mid =
        gg::ui::grainCapsule(grainAt(onset, static_cast<double>(head - 8 * 48000)), onset, head, g);
    REQUIRE(mid.has_value());
    REQUIRE_THAT(mid->x0, WithinAbs(800.0, 0.5));
    REQUIRE_THAT(mid->x1, WithinAbs(810.0, 0.5));

    // A grain reading at the head ends there, at the right edge; 16 s back is the left edge.
    const auto atHead =
        gg::ui::grainCapsule(grainAt(onset, static_cast<double>(head - 4800)), onset, head, g);
    REQUIRE(atHead.has_value());
    REQUIRE_THAT(atHead->x1, WithinAbs(1600.0, 1e-3));
    const auto oldest = gg::ui::grainCapsule(grainAt(onset, static_cast<double>(head - 16 * 48000)),
                                             onset, head, g);
    REQUIRE(oldest.has_value());
    REQUIRE_THAT(oldest->x0, WithinAbs(0.0, 1e-3));

    // The rate scales the read extent; a reverse grain spans the same extent backwards.
    LaneGrain fast = grainAt(onset, static_cast<double>(head - 8 * 48000));
    fast.rate = 2.0f;
    const auto f = gg::ui::grainCapsule(fast, onset, head, g);
    REQUIRE(f.has_value());
    REQUIRE_THAT(f->x0, WithinAbs(800.0, 1e-3));
    REQUIRE_THAT(f->x1, WithinAbs(820.0, 1e-3));
    LaneGrain reverse = fast;
    reverse.rate = -2.0f;
    const auto r = gg::ui::grainCapsule(reverse, onset, head, g);
    REQUIRE(r.has_value());
    REQUIRE_THAT(r->x0, WithinAbs(780.0, 1e-3));
    REQUIRE_THAT(r->x1, WithinAbs(800.0, 1e-3));
}

TEST_CASE("a grain scrolls left with time", "[ui]")
{
    const LaneGeometry g = fxLane();
    const int64_t head = 100 * 48000;
    const LaneGrain grain = grainAt(head, static_cast<double>(head - 4 * 48000));

    // Not yet sounding: nothing to draw.
    REQUIRE_FALSE(gg::ui::grainCapsule(grain, head - 1, head - 1, g).has_value());

    const auto start = gg::ui::grainCapsule(grain, head, head, g);
    REQUIRE(start.has_value());
    REQUIRE_THAT(start->x0, WithinAbs(1200.0, 1e-3));

    // 50 ms later the head has moved on 50 ms, so the same ring frames sit 5 px further left.
    const auto later = gg::ui::grainCapsule(grain, head + 2400, head + 2400, g);
    REQUIRE(later.has_value());
    REQUIRE_THAT(later->x0, WithinAbs(1195.0, 1e-3));
    REQUIRE_THAT(later->x1 - later->x0, WithinAbs(start->x1 - start->x0, 1e-3));

    // Drawn while onset <= now < onset + duration, and not after.
    REQUIRE(gg::ui::grainCapsule(grain, head + 4799, head + 4799, g).has_value());
    REQUIRE_FALSE(gg::ui::grainCapsule(grain, head + 4800, head + 4800, g).has_value());

    // The instrument lane is static: x is the position across the file, whatever the time.
    const LaneGeometry inst = instrumentLane(48000);
    LaneGrain sampleGrain = grainAt(1000, 12000.0);
    sampleGrain.rate = 2.0f;
    sampleGrain.durationSamples = 1200.0f;
    for (const int64_t now : {int64_t{1000}, int64_t{1500}, int64_t{2199}})
    {
        INFO("now " << now);
        const auto c = gg::ui::grainCapsule(sampleGrain, now, 123456789, inst);
        REQUIRE(c.has_value());
        REQUIRE_THAT(c->x0, WithinAbs(200.0, 1e-3)); // 12000 / 48000 of 800 px
        REQUIRE_THAT(c->x1, WithinAbs(240.0, 1e-3)); // + 2 × 1200 frames
    }
    REQUIRE_FALSE(gg::ui::grainCapsule(sampleGrain, 2200, 0, inst).has_value());
}

TEST_CASE("rate maps to y", "[ui]")
{
    const LaneGeometry g = fxLane(1600.0f, 100.0f, 1.0f);
    const int64_t head = 48000 * 20;
    LaneGrain grain = grainAt(head, static_cast<double>(head - 48000));

    // The vertical axis is pitch (D1): y = height/2 − 12·log2(rate) × (height/2 − radius) / 48,
    // so ±48 semitones span the lane and the capsule stays on it at either extreme.
    struct Case
    {
        float rate, y;
    };
    for (const Case c :
         {Case{1.0f, 50.0f}, Case{16.0f, 3.0f}, Case{1.0f / 16.0f, 97.0f}, Case{2.0f, 38.25f},
          Case{0.5f, 61.75f}, Case{256.0f, 3.0f}, Case{1.0f / 256.0f, 97.0f}})
    {
        INFO("rate " << c.rate);
        grain.rate = c.rate;
        const auto capsule = gg::ui::grainCapsule(grain, head, head, g);
        REQUIRE(capsule.has_value());
        REQUIRE_THAT(capsule->y, WithinAbs(c.y, 1e-4));
        REQUIRE(capsule->radius == 3.0f);
    }
    grain.rate = 1.0f;

    // The radius is 3 px at the pixel scale, at least 1.5 px.
    grain.rate = 1.0f / 16.0f; // four octaves down: the lane's bottom edge less the radius
    const auto hiDpi = gg::ui::grainCapsule(grain, head, head, fxLane(1600.0f, 100.0f, 2.0f));
    REQUIRE(hiDpi.has_value());
    REQUIRE(hiDpi->radius == 6.0f);
    REQUIRE_THAT(hiDpi->y, WithinAbs(94.0, 1e-4));
    const auto tiny = gg::ui::grainCapsule(grain, head, head, fxLane(1600.0f, 100.0f, 0.25f));
    REQUIRE(tiny.has_value());
    REQUIRE(tiny->radius == 1.5f);

    // A lane too short for the capsule keeps it centred.
    const auto flat = gg::ui::grainCapsule(grain, head, head, fxLane(1600.0f, 4.0f, 1.0f));
    REQUIRE(flat.has_value());
    REQUIRE(flat->y == 2.0f);

    // Brightness is the gain, clamped to 0..1.
    struct GainCase
    {
        float gain, intensity;
    };
    for (const GainCase c :
         {GainCase{0.4f, 0.4f}, GainCase{1.0f, 1.0f}, GainCase{2.5f, 1.0f}, GainCase{-1.0f, 0.0f}})
    {
        INFO("gain " << c.gain);
        grain.gain = c.gain;
        const auto capsule = gg::ui::grainCapsule(grain, head, head, g);
        REQUIRE(capsule.has_value());
        REQUIRE(capsule->intensity == c.intensity);
    }
}

TEST_CASE("a grain the lane cannot place is not drawn", "[ui]")
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    const int64_t head = 48000 * 20;
    const LaneGrain ok = grainAt(head, static_cast<double>(head - 48000));
    REQUIRE(gg::ui::grainCapsule(ok, head, head, fxLane()).has_value());

    LaneGrain bad = ok;
    bad.startPos = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_FALSE(gg::ui::grainCapsule(bad, head, head, fxLane()).has_value());
    bad = ok;
    bad.rate = kNaN;
    REQUIRE_FALSE(gg::ui::grainCapsule(bad, head, head, fxLane()).has_value());
    bad = ok;
    bad.durationSamples = kNaN;
    REQUIRE_FALSE(gg::ui::grainCapsule(bad, head, head, fxLane()).has_value());
    bad = ok;
    bad.durationSamples = 0.0f;
    REQUIRE_FALSE(gg::ui::grainCapsule(bad, head, head, fxLane()).has_value());

    // A NaN gain draws dark rather than not at all; a NaN pan, no longer drawn (D1), changes
    // nothing: the grain at rate 1 is centred.
    bad = ok;
    bad.gain = kNaN;
    bad.pan = kNaN;
    const auto dark = gg::ui::grainCapsule(bad, head, head, fxLane());
    REQUIRE(dark.has_value());
    REQUIRE(dark->intensity == 0.0f);
    REQUIRE(dark->y == 50.0f);

    // A degenerate lane draws nothing.
    LaneGeometry g = fxLane();
    g.width = 0.0f;
    REQUIRE_FALSE(gg::ui::grainCapsule(ok, head, head, g).has_value());
    g = fxLane();
    g.sampleRate = 0.0;
    REQUIRE_FALSE(gg::ui::grainCapsule(ok, head, head, g).has_value());
    g = fxLane();
    g.ringSeconds = 0.0;
    REQUIRE_FALSE(gg::ui::grainCapsule(ok, head, head, g).has_value());
    REQUIRE_FALSE(gg::ui::grainCapsule(ok, head, head, instrumentLane(0)).has_value());

    // Extreme clocks don't overflow.
    const LaneGrain early = grainAt(std::numeric_limits<int64_t>::min(), 0.0);
    REQUIRE_FALSE(gg::ui::grainCapsule(early, std::numeric_limits<int64_t>::max(), head, fxLane())
                      .has_value());
}

TEST_CASE("the scan window follows the position and spray tapers", "[ui]")
{
    // FX (ruling PU-T8a): Position 0.28967658 is 0.25 s behind the head and Spray 0.36033038 is
    // 50 ms, so the window runs from 0.25 s to 0.3 s behind the head, one-sided as the engine
    // draws a live grain: on 1600 px for 16 s, its right edge (Position) at 1575 px, 5 px wide.
    const LaneGeometry g = fxLane();
    const auto w = gg::ui::scanWindow(0.28967658f, 0.36033038f, g);
    REQUIRE_THAT(w.x1, WithinAbs(1575.0, 1e-3));
    REQUIRE_THAT(w.x1 - w.x0, WithinAbs(5.0, 1e-3));

    // The ends of the tapers: Position 0 is the head, 1 is 16 s back; Spray 1 is 2 s further.
    const auto atHead = gg::ui::scanWindow(0.0f, 0.0f, g);
    REQUIRE(atHead.x0 == 1600.0f);
    REQUIRE(atHead.x1 == 1600.0f);
    const auto oldest = gg::ui::scanWindow(1.0f, 1.0f, g);
    REQUIRE_THAT(oldest.x1, WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(oldest.x0, WithinAbs(-200.0, 1e-3));
    // Values outside 0..1 (or NaN) are clamped first.
    const auto clamped = gg::ui::scanWindow(1.5f, -1.0f, g);
    const auto top = gg::ui::scanWindow(1.0f, 0.0f, g);
    REQUIRE(clamped.x0 == top.x0);
    REQUIRE(clamped.x1 == top.x1);
    const auto nan = gg::ui::scanWindow(std::numeric_limits<float>::quiet_NaN(),
                                        std::numeric_limits<float>::quiet_NaN(), g);
    REQUIRE(nan.x0 == 1600.0f);
    REQUIRE(nan.x1 == 1600.0f);

    // Instrument: Position is the fraction of the file and Spray is seconds either side, so on a
    // 2 s file across 800 px, Position 0.25 with 50 ms of Spray is centred at 200 px, ±20 px.
    const LaneGeometry inst = instrumentLane(96000);
    const auto iw = gg::ui::scanWindow(0.25f, 0.36033038f, inst);
    REQUIRE_THAT(0.5 * (iw.x0 + iw.x1), WithinAbs(200.0, 1e-3));
    REQUIRE_THAT(iw.x1 - iw.x0, WithinAbs(40.0, 1e-3));
}

TEST_CASE("the scan window's tapers match ParamMap's", "[ui]")
{
    // LaneModel keeps its own copy of ParamMap's FX Position and Spray tapers (the lane maths
    // needs nothing else of gg-core): the window's seconds must be toCloudParams's, so the copies
    // can't drift. On the 1600 px, 16 s lane a pixel is 10 ms.
    const LaneGeometry g = fxLane();
    const auto secondsOf = [&](float position, float spray) {
        gg::core::ParamValues values{};
        values[static_cast<std::size_t>(gg::core::ParamSlot::Position)] = position;
        values[static_cast<std::size_t>(gg::core::ParamSlot::Spray)] = spray;
        // A layer reading its ring, as the FX's layers do by default (M2 sub-project 1's §6.4).
        values[static_cast<std::size_t>(gg::core::ParamSlot::Source)] =
            static_cast<float>(gg::core::SourceKind::Live);
        const auto cloud = gg::core::toCloudParams(values, gg::core::Mode::Fx);
        const auto w = gg::ui::scanWindow(position, spray, g);
        INFO("position " << position << ", spray " << spray);
        REQUIRE_THAT((1600.0 - w.x1) / 100.0, WithinAbs(cloud.position, 1e-5));
        REQUIRE_THAT((w.x1 - w.x0) / 100.0, WithinAbs(cloud.spray, 1e-5));
        return cloud;
    };
    const auto brief = secondsOf(0.28967658f, 0.36033038f);
    REQUIRE_THAT(brief.position, WithinAbs(0.25, 1e-7));
    REQUIRE_THAT(brief.spray, WithinAbs(0.05, 1e-7));
    for (int i = 0; i <= 20; ++i)
    {
        const float v = static_cast<float>(i) / 20.0f;
        secondsOf(v, 1.0f - v);
    }
}

TEST_CASE("positionValueAtX inverts scanWindow across the lane", "[ui]")
{
    for (const LaneGeometry g : {fxLane(), fxLane(317.0f), instrumentLane(96000)})
    {
        INFO((g.fxLane ? "fx" : "instrument") << " lane, " << g.width << " px");
        for (int i = 0; i <= 1000; ++i)
        {
            const float v = static_cast<float>(i) / 1000.0f;
            INFO("position " << v);
            const auto w = gg::ui::scanWindow(v, 0.0f, g);
            REQUIRE(w.x0 == w.x1);
            REQUIRE_THAT(gg::ui::positionValueAtX(w.x1, g), WithinAbs(v, 1e-5));
        }
        // And x -> value -> x, everywhere on the lane.
        for (int i = 0; i <= 100; ++i)
        {
            const float x = g.width * static_cast<float>(i) / 100.0f;
            INFO("x " << x);
            const float v = gg::ui::positionValueAtX(x, g);
            REQUIRE(v >= 0.0f);
            REQUIRE(v <= 1.0f);
            REQUIRE_THAT(gg::ui::scanWindow(v, 0.0f, g).x1, WithinAbs(x, 1e-2));
        }
    }

    // The FX value matches the taper's inverse, and Position stays the window's right edge
    // whatever the Spray.
    REQUIRE_THAT(gg::ui::positionValueAtX(1575.0f, fxLane()),
                 WithinAbs(fxPositionValue(0.25), 1e-6));
    REQUIRE_THAT(gg::ui::positionValueAtX(gg::ui::scanWindow(0.5f, 0.8f, fxLane()).x1, fxLane()),
                 WithinAbs(0.5, 1e-5));

    // Spray: the window's extent back to the value, its full width on the FX lane and its
    // half-width on the instrument's.
    for (const LaneGeometry g : {fxLane(), instrumentLane(96000)})
    {
        INFO((g.fxLane ? "fx" : "instrument") << " lane");
        for (int i = 0; i <= 1000; ++i)
        {
            const float s = static_cast<float>(i) / 1000.0f;
            INFO("spray " << s);
            const auto w = gg::ui::scanWindow(0.5f, s, g);
            const float extent = g.fxLane ? w.x1 - w.x0 : 0.5f * (w.x1 - w.x0);
            REQUIRE_THAT(gg::ui::sprayValueForExtent(extent, g), WithinAbs(s, 1e-4));
        }
    }
    // 50 ms is 5 px on the FX lane (full width) and 20 px on the 2 s instrument lane (half).
    REQUIRE_THAT(gg::ui::sprayValueForExtent(5.0f, fxLane()), WithinAbs(0.36033038, 1e-6));
    REQUIRE_THAT(gg::ui::sprayValueForExtent(20.0f, instrumentLane(96000)),
                 WithinAbs(0.36033038, 1e-6));
    REQUIRE(gg::ui::sprayValueForExtent(-3.0f, fxLane()) == 0.0f);
    REQUIRE(gg::ui::sprayValueForExtent(1e9f, fxLane()) == 1.0f);
}

TEST_CASE("positionValueAtX clamps x outside the lane", "[ui]")
{
    // A drag past either end pins Position to its range: left of the FX lane is the oldest
    // audio (1), right of it the head (0); the instrument runs the other way.
    REQUIRE(gg::ui::positionValueAtX(-50.0f, fxLane()) == 1.0f);
    REQUIRE(gg::ui::positionValueAtX(1e9f, fxLane()) == 0.0f);
    REQUIRE(gg::ui::positionValueAtX(-50.0f, instrumentLane(96000)) == 0.0f);
    REQUIRE(gg::ui::positionValueAtX(1e9f, instrumentLane(96000)) == 1.0f);
    REQUIRE(gg::ui::positionValueAtX(-1e30f, fxLane()) == 1.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(gg::ui::positionValueAtX(nan, fxLane()) == 0.0f);
    REQUIRE(gg::ui::positionValueAtX(nan, instrumentLane(96000)) == 0.0f);
    REQUIRE(gg::ui::sprayValueForExtent(nan, fxLane()) == 0.0f);
}

TEST_CASE("hit testing", "[ui]")
{
    const LaneGeometry inst = instrumentLane(96000);
    const LaneGeometry fx = fxLane();

    // Instrument, a wide window: each edge grabs within the tolerance either side, the rest is
    // the body.
    const gg::ui::Window wide{100.0f, 200.0f};
    REQUIRE(gg::ui::hitTest(100.0f, wide, 5.0f, inst) == DragTarget::LeftEdge);
    REQUIRE(gg::ui::hitTest(96.0f, wide, 5.0f, inst) == DragTarget::LeftEdge);
    REQUIRE(gg::ui::hitTest(104.0f, wide, 5.0f, inst) == DragTarget::LeftEdge);
    REQUIRE(gg::ui::hitTest(94.0f, wide, 5.0f, inst) == DragTarget::None);
    REQUIRE(gg::ui::hitTest(150.0f, wide, 5.0f, inst) == DragTarget::Body);
    REQUIRE(gg::ui::hitTest(106.0f, wide, 5.0f, inst) == DragTarget::Body);
    REQUIRE(gg::ui::hitTest(194.0f, wide, 5.0f, inst) == DragTarget::Body);
    REQUIRE(gg::ui::hitTest(196.0f, wide, 5.0f, inst) == DragTarget::RightEdge);
    REQUIRE(gg::ui::hitTest(200.0f, wide, 5.0f, inst) == DragTarget::RightEdge);
    REQUIRE(gg::ui::hitTest(205.0f, wide, 5.0f, inst) == DragTarget::RightEdge);
    REQUIRE(gg::ui::hitTest(206.0f, wide, 5.0f, inst) == DragTarget::None);
    REQUIRE(gg::ui::hitTest(std::numeric_limits<float>::quiet_NaN(), wide, 5.0f, inst) ==
            DragTarget::None);

    // A narrow window keeps its middle third for the body: the edges reach in at most a third
    // of its width, and still the full tolerance outside it.
    const gg::ui::Window narrow{100.0f, 106.0f};
    REQUIRE(gg::ui::hitTest(97.0f, narrow, 5.0f, inst) == DragTarget::LeftEdge);
    REQUIRE(gg::ui::hitTest(101.0f, narrow, 5.0f, inst) == DragTarget::LeftEdge);
    REQUIRE(gg::ui::hitTest(103.0f, narrow, 5.0f, inst) == DragTarget::Body);
    REQUIRE(gg::ui::hitTest(105.0f, narrow, 5.0f, inst) == DragTarget::RightEdge);
    REQUIRE(gg::ui::hitTest(110.0f, narrow, 5.0f, inst) == DragTarget::RightEdge);

    // A window of no width (Spray 0): its edges either side, the body on it.
    const gg::ui::Window point{100.0f, 100.0f};
    REQUIRE(gg::ui::hitTest(98.0f, point, 5.0f, inst) == DragTarget::LeftEdge);
    REQUIRE(gg::ui::hitTest(100.0f, point, 5.0f, inst) == DragTarget::Body);
    REQUIRE(gg::ui::hitTest(102.0f, point, 5.0f, inst) == DragTarget::RightEdge);
    REQUIRE(gg::ui::hitTest(106.0f, point, 5.0f, inst) == DragTarget::None);

    // Tolerance 0: only the window itself.
    REQUIRE(gg::ui::hitTest(150.0f, wide, 0.0f, inst) == DragTarget::Body);
    REQUIRE(gg::ui::hitTest(99.0f, wide, 0.0f, inst) == DragTarget::None);

    // FX (ruling PU-T8a): the right edge is Position, so it and its tolerance are the body; the
    // left (older) edge alone sets Spray.
    REQUIRE(gg::ui::hitTest(96.0f, wide, 5.0f, fx) == DragTarget::LeftEdge);
    REQUIRE(gg::ui::hitTest(104.0f, wide, 5.0f, fx) == DragTarget::LeftEdge);
    REQUIRE(gg::ui::hitTest(94.0f, wide, 5.0f, fx) == DragTarget::None);
    REQUIRE(gg::ui::hitTest(150.0f, wide, 5.0f, fx) == DragTarget::Body);
    REQUIRE(gg::ui::hitTest(196.0f, wide, 5.0f, fx) == DragTarget::Body);
    REQUIRE(gg::ui::hitTest(200.0f, wide, 5.0f, fx) == DragTarget::Body);
    REQUIRE(gg::ui::hitTest(205.0f, wide, 5.0f, fx) == DragTarget::Body);
    REQUIRE(gg::ui::hitTest(206.0f, wide, 5.0f, fx) == DragTarget::None);
    REQUIRE(gg::ui::hitTest(105.0f, narrow, 5.0f, fx) == DragTarget::Body);
    REQUIRE(gg::ui::hitTest(101.0f, narrow, 5.0f, fx) == DragTarget::LeftEdge);
    // Spray 0: the left edge just left of Position, the body from it rightwards.
    REQUIRE(gg::ui::hitTest(98.0f, point, 5.0f, fx) == DragTarget::LeftEdge);
    REQUIRE(gg::ui::hitTest(100.0f, point, 5.0f, fx) == DragTarget::Body);
    REQUIRE(gg::ui::hitTest(102.0f, point, 5.0f, fx) == DragTarget::Body);
    REQUIRE(gg::ui::hitTest(106.0f, point, 5.0f, fx) == DragTarget::None);
    REQUIRE(gg::ui::hitTest(std::numeric_limits<float>::quiet_NaN(), wide, 5.0f, fx) ==
            DragTarget::None);
}

// M2's lanes (spec §6.3): four stacked lanes, each at its own top, with pitch on the vertical
// axis (D1) and the scan window grown into a rectangle with four draggable edges (D2).

namespace
{
/// A uniform double in [0, 1) from a counter, for the tests' random grains (UI tests only).
double unit(uint64_t seed, uint64_t index)
{
    return static_cast<double>(gg::rng::splitmix64(seed * 0x100000001B3ull + index) >> 11) *
           0x1.0p-53;
}
} // namespace

TEST_CASE("pitch maps to the lane's vertical axis", "[ui]")
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();
    constexpr float r = 3.0f;
    for (const LaneGeometry &g : {fxLane(), atTop(instrumentLane(48000), 200.0f)})
    {
        INFO("lane at top " << g.top);
        const float centre = g.top + 50.0f;
        const float reach = 50.0f - r; // the half-height less the radius
        // Rate 1 is the centre; an octave is 12/48 of the reach either way.
        CHECK(gg::ui::pitchY(1.0f, g, r) == centre);
        CHECK_THAT(gg::ui::pitchY(2.0f, g, r), WithinAbs(centre - 12.0f / 48.0f * reach, 1e-3));
        CHECK_THAT(gg::ui::pitchY(0.5f, g, r), WithinAbs(centre + 12.0f / 48.0f * reach, 1e-3));
        // Four octaves (48 semitones) reach the lane's edge less the radius; past them it clamps.
        CHECK_THAT(gg::ui::pitchY(16.0f, g, r), WithinAbs(g.top + r, 1e-3));
        CHECK_THAT(gg::ui::pitchY(1.0f / 16.0f, g, r), WithinAbs(g.top + 100.0f - r, 1e-3));
        CHECK(gg::ui::pitchY(1.0e6f, g, r) == g.top + r);
        CHECK(gg::ui::pitchY(1.0e-6f, g, r) == g.top + 100.0f - r);
        // A rate that isn't positive and finite draws centred.
        for (const float rate : {0.0f, -0.0f, -1.0f, -2.0f, kNaN, kInf, -kInf})
        {
            INFO("rate " << rate);
            CHECK(gg::ui::pitchY(rate, g, r) == centre);
        }
        // A larger radius keeps a larger capsule on the lane.
        CHECK_THAT(gg::ui::pitchY(16.0f, g, 6.0f), WithinAbs(g.top + 6.0f, 1e-3));
    }
    // A lane shorter than the capsule keeps it centred.
    const LaneGeometry flat = atTop(fxLane(1600.0f, 4.0f), 40.0f);
    CHECK(gg::ui::pitchY(16.0f, flat, r) == 42.0f);
    CHECK(gg::ui::pitchY(0.25f, flat, r) == 42.0f);
}

TEST_CASE("a capsule stays in its lane", "[ui]")
{
    // Four lanes of 100 px stacked at tops 0, 100, 200 and 300 (spec §6.3): whatever a grain's
    // rate, its capsule's y ± radius lies inside its own lane (Review Focus 3).
    const int64_t head = 48000 * 20;
    int drawn = 0;
    for (uint64_t i = 0; i < 1000; ++i)
    {
        const auto lane = static_cast<float>(i % 4);
        const float scale = std::array<float, 3>{0.5f, 1.0f, 2.0f}[i % 3];
        const LaneGeometry g = atTop(i % 2 == 0 ? fxLane(1600.0f, 100.0f, scale)
                                                : instrumentLane(96000, 800.0f, 100.0f),
                                     100.0f * lane);
        LaneGrain grain =
            i % 2 == 0 ? grainAt(head, static_cast<double>(head - 48000)) : grainAt(head, 48000.0);
        // Rates from 2^-20 to 2^20, either sign, and now and then one that isn't finite.
        const double octaves = 40.0 * unit(1, i) - 20.0;
        const double sign = unit(2, i) < 0.25 ? -1.0 : 1.0;
        grain.rate = static_cast<float>(sign * std::exp2(octaves));
        if (i % 97 == 0)
        {
            grain.rate = std::numeric_limits<float>::infinity();
        }
        grain.pan = static_cast<float>(4.0 * unit(3, i) - 2.0); // no longer drawn
        INFO("grain " << i << ", rate " << grain.rate << ", lane " << lane);
        const auto c = gg::ui::grainCapsule(grain, head, head, g);
        if (!c.has_value())
        {
            CHECK_FALSE(std::isfinite(grain.rate));
            continue;
        }
        ++drawn;
        CHECK(c->y - c->radius >= g.top);
        CHECK(c->y + c->radius <= g.top + g.height);
        // y is pitchY's, at the capsule's radius.
        CHECK(c->y == gg::ui::pitchY(grain.rate, g, c->radius));
    }
    CHECK(drawn > 980);
}

TEST_CASE("the scan rectangle's four edges", "[ui]")
{
    // x as M1's scan window, y from Pitch ± Pitch Spread on the pitch axis (D2): Pitch +12 with
    // a spread of 6 runs from +18 (y0, the higher pitch) to +6 semitones (y1), on lanes of
    // 100 px at top 100: the centre at 150, 47 px (the half-height less the radius) for 48
    // semitones.
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    for (const LaneGeometry &g : {atTop(fxLane(), 100.0f), atTop(instrumentLane(96000), 100.0f)})
    {
        INFO((g.fxLane ? "Live" : "Sample") << " lane");
        const ScanRectValues v{0.28967658f, 0.36033038f, 12.0f, 6.0f};
        const WindowRect rect = gg::ui::scanRect(v, g);
        const gg::ui::Window w = gg::ui::scanWindow(v.position, v.spray, g);
        CHECK(rect.x0 == w.x0);
        CHECK(rect.x1 == w.x1);
        CHECK_THAT(rect.y0, WithinAbs(150.0 - 18.0 * 47.0 / 48.0, 1e-3));
        CHECK_THAT(rect.y1, WithinAbs(150.0 - 6.0 * 47.0 / 48.0, 1e-3));
        // Its middle is where a grain at Pitch's rate (an octave up) lies.
        CHECK_THAT(0.5f * (rect.y0 + rect.y1), WithinAbs(gg::ui::pitchY(2.0f, g, 3.0f), 1e-3));

        // Pitch 0, spread 0: a band of no height at the lane's centre.
        const WindowRect band = gg::ui::scanRect({0.5f, 0.5f, 0.0f, 0.0f}, g);
        CHECK(band.y0 == 150.0f);
        CHECK(band.y1 == 150.0f);
        CHECK(band.x0 == gg::ui::scanWindow(0.5f, 0.5f, g).x0);
        CHECK(band.x1 == gg::ui::scanWindow(0.5f, 0.5f, g).x1);

        // The ends: Pitch ±24 with a spread of 24 reach the lane's edge less the radius.
        const WindowRect high = gg::ui::scanRect({0.5f, 0.5f, 24.0f, 24.0f}, g);
        CHECK_THAT(high.y0, WithinAbs(103.0, 1e-3));
        CHECK_THAT(high.y1, WithinAbs(150.0, 1e-3));
        const WindowRect low = gg::ui::scanRect({0.5f, 0.5f, -24.0f, 24.0f}, g);
        CHECK_THAT(low.y0, WithinAbs(150.0, 1e-3));
        CHECK_THAT(low.y1, WithinAbs(197.0, 1e-3));
        // Pitch is clamped to ±24 and the spread to 0..24 first, NaN to 0.
        const WindowRect over = gg::ui::scanRect({0.5f, 0.5f, 30.0f, 40.0f}, g);
        CHECK(over.y0 == high.y0);
        CHECK(over.y1 == high.y1);
        const WindowRect negative = gg::ui::scanRect({0.5f, 0.5f, -3.0f, -5.0f}, g);
        CHECK(negative.y0 == negative.y1);
        const WindowRect nan = gg::ui::scanRect({0.5f, 0.5f, kNaN, kNaN}, g);
        CHECK(nan.y0 == 150.0f);
        CHECK(nan.y1 == 150.0f);
    }
    // At twice the pixel scale the radius is 6 px, so the edge is 6 px in.
    const WindowRect hiDpi =
        gg::ui::scanRect({0.5f, 0.5f, 24.0f, 24.0f}, atTop(fxLane(1600.0f, 100.0f, 2.0f), 100.0f));
    CHECK_THAT(hiDpi.y0, WithinAbs(106.0, 1e-3));
}

TEST_CASE("the ghost rectangle shows when any of the four differs", "[ui]")
{
    // Spec §6.3: the rectangle at the effective values, shown when any of Position, Spray, Pitch
    // and Pitch Spread differs from its base value by more than kGhostThreshold in its own 0..1
    // domain (Pitch over its 48 semitones, the spread over its 24).
    for (const LaneGeometry &g : {atTop(fxLane(), 100.0f), atTop(instrumentLane(48000), 300.0f)})
    {
        INFO((g.fxLane ? "Live" : "Sample") << " lane");
        const ScanRectValues base{0.4f, 0.3f, 5.0f, 6.0f};
        CHECK_FALSE(gg::ui::ghostRect(base, base, g).has_value());
        const auto showsAt = [&](const ScanRectValues &effective) {
            const auto ghost = gg::ui::ghostRect(base, effective, g);
            REQUIRE(ghost.has_value());
            const WindowRect expected = gg::ui::scanRect(effective, g);
            CHECK(ghost->x0 == expected.x0);
            CHECK(ghost->x1 == expected.x1);
            CHECK(ghost->y0 == expected.y0);
            CHECK(ghost->y1 == expected.y1);
        };
        showsAt({0.5f, 0.3f, 5.0f, 6.0f});  // Position alone
        showsAt({0.4f, 0.8f, 5.0f, 6.0f});  // Spray alone
        showsAt({0.4f, 0.3f, 7.0f, 6.0f});  // Pitch alone
        showsAt({0.4f, 0.3f, 5.0f, 12.0f}); // Pitch Spread alone
        // Under the threshold in every one: 0.004 semitones of Pitch is 8.3e-5 of its domain,
        // 0.002 of the spread 8.3e-5 of its.
        CHECK_FALSE(gg::ui::ghostRect(base, {0.4f + 5.0e-5f, 0.3f - 5.0e-5f, 5.004f, 6.002f}, g)
                        .has_value());
        // Just over it, in one: 0.006 semitones of Pitch (1.25e-4), 0.003 of the spread.
        showsAt({0.4f, 0.3f, 5.006f, 6.0f});
        showsAt({0.4f, 0.3f, 5.0f, 6.003f});
        showsAt({0.4f, 0.3f, 4.994f, 6.0f});
    }
}

TEST_CASE("a NaN effective value shows no ghost", "[ui]")
{
    // Review Focus 3: a snapshot with a non-finite effective value (a NaN Pitch after a bad host
    // event) shows no ghost, even when another of the four differs.
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();
    const LaneGeometry g = atTop(fxLane(), 100.0f);
    const ScanRectValues base{0.4f, 0.3f, 5.0f, 6.0f};
    const ScanRectValues moved{0.6f, 0.8f, 9.0f, 12.0f};
    REQUIRE(gg::ui::ghostRect(base, moved, g).has_value());
    for (const float bad : {kNaN, kInf, -kInf})
    {
        for (int field = 0; field < 4; ++field)
        {
            INFO("value " << bad << " in field " << field);
            ScanRectValues effective = moved;
            ScanRectValues badBase = base;
            std::array<float *, 4> e{&effective.position, &effective.spray, &effective.pitch,
                                     &effective.pitchSpread};
            std::array<float *, 4> b{&badBase.position, &badBase.spray, &badBase.pitch,
                                     &badBase.pitchSpread};
            *e[static_cast<std::size_t>(field)] = bad;
            CHECK_FALSE(gg::ui::ghostRect(base, effective, g).has_value());
            // A non-finite base value can't place the comparison either.
            *b[static_cast<std::size_t>(field)] = bad;
            CHECK_FALSE(gg::ui::ghostRect(badBase, moved, g).has_value());
        }
    }
    CHECK_FALSE(gg::ui::ghostRect(base, {kNaN, kNaN, kNaN, kNaN}, g).has_value());
}

TEST_CASE("hitTestRect names every region", "[ui]")
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();
    // Lanes of 200 px at top 100, so y runs 100 .. 300; an 800 px instrument lane and a 1600 px
    // FX lane. The rectangle spans x 100 .. 200 and y 150 .. 250; the tolerance is 5 px.
    const LaneGeometry inst = atTop(instrumentLane(96000, 800.0f, 200.0f), 100.0f);
    const LaneGeometry fx = atTop(fxLane(1600.0f, 200.0f), 100.0f);
    const WindowRect rect{100.0f, 200.0f, 150.0f, 250.0f};
    struct Case
    {
        float x, y;
        DragTarget expected;
        const char *what;
    };
    const auto check = [](const LaneGeometry &g, const WindowRect &w,
                          std::initializer_list<Case> cases) {
        for (const Case &c : cases)
        {
            INFO((g.fxLane ? "Live" : "Sample")
                 << " lane: " << c.what << " at (" << c.x << ", " << c.y << ")");
            CHECK(gg::ui::hitTestRect(c.x, c.y, w, 5.0f, g) == c.expected);
        }
    };
    using enum DragTarget;
    check(inst, rect,
          {
              {150.0f, 200.0f, Body, "the middle"},
              {106.0f, 156.0f, Body, "inside, past both edges' tolerance"},
              {194.0f, 244.0f, Body, "inside, before both edges' tolerance"},
              {96.0f, 200.0f, LeftEdge, "the left edge, outside"},
              {104.0f, 200.0f, LeftEdge, "the left edge, inside"},
              {94.0f, 200.0f, None, "left of the left edge's tolerance"},
              {196.0f, 200.0f, RightEdge, "the right edge, inside"},
              {205.0f, 200.0f, RightEdge, "the right edge, outside"},
              {206.0f, 200.0f, None, "right of the right edge's tolerance"},
              {150.0f, 146.0f, TopEdge, "the top edge, outside"},
              {150.0f, 154.0f, TopEdge, "the top edge, inside"},
              {150.0f, 144.0f, None, "above the top edge's tolerance"},
              {150.0f, 246.0f, BottomEdge, "the bottom edge, inside"},
              {150.0f, 255.0f, BottomEdge, "the bottom edge, outside"},
              {150.0f, 256.0f, None, "below the bottom edge's tolerance"},
              // A corner, within the tolerance of both edges, takes the horizontal edge.
              {100.0f, 150.0f, LeftEdge, "the top-left corner"},
              {200.0f, 150.0f, RightEdge, "the top-right corner"},
              {100.0f, 250.0f, LeftEdge, "the bottom-left corner"},
              {200.0f, 250.0f, RightEdge, "the bottom-right corner"},
              {96.0f, 146.0f, LeftEdge, "the top-left corner, outside both"},
              {104.0f, 154.0f, LeftEdge, "the top-left corner, inside both"},
              {204.0f, 254.0f, RightEdge, "the bottom-right corner, outside both"},
              {94.0f, 146.0f, None, "beside the top-left corner"},
              {96.0f, 144.0f, None, "above the top-left corner"},
              {kNaN, 200.0f, None, "a NaN x"},
              {150.0f, kNaN, None, "a NaN y"},
              {kInf, 200.0f, None, "an infinite x"},
              {150.0f, -kInf, None, "an infinite y"},
          });
    // The FX lane: M1's x rules, so the right side is the body (Position, ruling PU-T8a) and its
    // corners take the top and bottom edges, which run to it.
    check(fx, rect,
          {
              {150.0f, 200.0f, Body, "the middle"},
              {96.0f, 200.0f, LeftEdge, "the left edge, outside"},
              {104.0f, 200.0f, LeftEdge, "the left edge, inside"},
              {94.0f, 200.0f, None, "left of the left edge's tolerance"},
              {200.0f, 200.0f, Body, "the right side"},
              {205.0f, 200.0f, Body, "the right side's tolerance"},
              {206.0f, 200.0f, None, "right of the right side's tolerance"},
              {150.0f, 146.0f, TopEdge, "the top edge, outside"},
              {150.0f, 154.0f, TopEdge, "the top edge, inside"},
              {150.0f, 144.0f, None, "above the top edge's tolerance"},
              {150.0f, 246.0f, BottomEdge, "the bottom edge, inside"},
              {150.0f, 255.0f, BottomEdge, "the bottom edge, outside"},
              {150.0f, 256.0f, None, "below the bottom edge's tolerance"},
              {100.0f, 150.0f, LeftEdge, "the top-left corner"},
              {100.0f, 250.0f, LeftEdge, "the bottom-left corner"},
              {200.0f, 150.0f, TopEdge, "the top-right corner"},
              {205.0f, 254.0f, BottomEdge, "the bottom-right corner, outside both"},
              {kNaN, 200.0f, None, "a NaN x"},
              {150.0f, kNaN, None, "a NaN y"},
          });
    // A band of no height (Pitch Spread 0): the body only on its own row, an edge either side.
    check(inst, {100.0f, 200.0f, 200.0f, 200.0f},
          {
              {150.0f, 200.0f, Body, "the band"},
              {150.0f, 198.0f, TopEdge, "just above the band"},
              {150.0f, 202.0f, BottomEdge, "just below the band"},
              {150.0f, 206.0f, None, "below the band's tolerance"},
          });
    // A short rectangle keeps its middle third for the body, as M1's narrow window does.
    check(inst, {100.0f, 200.0f, 200.0f, 206.0f},
          {
              {150.0f, 201.0f, TopEdge, "the top edge"},
              {150.0f, 203.0f, Body, "the middle third"},
              {150.0f, 205.0f, BottomEdge, "the bottom edge"},
          });
    // Outside the lane grabs nothing, even within an edge's tolerance: a rectangle at the lane's
    // top (y 100) and its left (x 0) edges.
    check(inst, {-50.0f, 200.0f, 102.0f, 250.0f},
          {
              {1.0f, 100.0f, TopEdge, "the top edge, on the lane"},
              {1.0f, 99.0f, None, "the top edge's tolerance, above the lane"},
              {0.0f, 200.0f, Body, "the body, on the lane"},
              {-1.0f, 200.0f, None, "the body, left of the lane"},
          });
    check(inst, {700.0f, 800.0f, 150.0f, 299.0f},
          {
              {801.0f, 200.0f, None, "the right edge's tolerance, right of the lane"},
              {750.0f, 301.0f, None, "the bottom edge's tolerance, below the lane"},
              {750.0f, 300.0f, BottomEdge, "the bottom edge, on the lane's last row"},
          });
}

TEST_CASE("the inverses round-trip", "[ui]")
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    for (const LaneGeometry &g : {atTop(fxLane(), 100.0f), atTop(instrumentLane(96000), 0.0f),
                                  atTop(fxLane(1600.0f, 57.0f, 2.0f), 231.0f)})
    {
        INFO("lane at top " << g.top << ", " << g.height << " px");
        for (int p = -48; p <= 48; ++p)
        {
            for (int s = 0; s <= 48; ++s)
            {
                const ScanRectValues v{0.5f, 0.5f, 0.5f * static_cast<float>(p),
                                       0.5f * static_cast<float>(s)};
                INFO("pitch " << v.pitch << ", spread " << v.pitchSpread);
                const WindowRect rect = gg::ui::scanRect(v, g);
                const float centre = 0.5f * (rect.y0 + rect.y1);
                CHECK_THAT(gg::ui::pitchValueAtY(centre, g), WithinAbs(v.pitch, 1e-3));
                CHECK_THAT(gg::ui::pitchSpreadForExtent(0.5f * (rect.y1 - rect.y0), g),
                           WithinAbs(v.pitchSpread, 1e-3));
            }
        }
        // A grain at Pitch's rate lies where pitchValueAtY reads Pitch back.
        const float radius = std::max(1.5f, 3.0f * g.pixelScale);
        for (int p = -24; p <= 24; ++p)
        {
            const float rate = std::exp2(static_cast<float>(p) / 12.0f);
            CHECK_THAT(gg::ui::pitchValueAtY(gg::ui::pitchY(rate, g, radius), g),
                       WithinAbs(p, 1e-3));
        }
        // Clamped: past the lane's edges Pitch pins to ±24 and the spread to 0..24.
        CHECK(gg::ui::pitchValueAtY(g.top - 1000.0f, g) == 24.0f);
        CHECK(gg::ui::pitchValueAtY(g.top + g.height + 1000.0f, g) == -24.0f);
        CHECK(gg::ui::pitchValueAtY(kNaN, g) == 0.0f);
        CHECK(gg::ui::pitchSpreadForExtent(1.0e9f, g) == 24.0f);
        CHECK(gg::ui::pitchSpreadForExtent(0.0f, g) == 0.0f);
        CHECK(gg::ui::pitchSpreadForExtent(-3.0f, g) == 0.0f);
        CHECK(gg::ui::pitchSpreadForExtent(kNaN, g) == 0.0f);
    }
    // A lane too short for the capsule can't place a pitch: 0, as a degenerate lane gives.
    const LaneGeometry flat = fxLane(1600.0f, 4.0f);
    CHECK(gg::ui::pitchValueAtY(0.0f, flat) == 0.0f);
    CHECK(gg::ui::pitchSpreadForExtent(1.0f, flat) == 0.0f);
}

TEST_CASE("the axis lock decides once", "[ui]")
{
    // A body drag's first 4 px pick its axis (D2): Position horizontally, Pitch vertically.
    CHECK(gg::ui::decideAxis(3.0f, 1.0f, 4.0f) == DragAxis::Undecided);
    CHECK(gg::ui::decideAxis(5.0f, 1.0f, 4.0f) == DragAxis::Horizontal);
    CHECK(gg::ui::decideAxis(1.0f, 5.0f, 4.0f) == DragAxis::Vertical);
    CHECK(gg::ui::decideAxis(4.0f, 4.0f, 4.0f) == DragAxis::Horizontal); // a tie
    CHECK(gg::ui::decideAxis(-5.0f, 1.0f, 4.0f) == DragAxis::Horizontal);
    CHECK(gg::ui::decideAxis(1.0f, -5.0f, 4.0f) == DragAxis::Vertical);
    CHECK(gg::ui::decideAxis(-4.0f, 4.0f, 4.0f) == DragAxis::Horizontal);
    CHECK(gg::ui::decideAxis(0.0f, 0.0f, 4.0f) == DragAxis::Undecided);
    CHECK(gg::ui::decideAxis(0.0f, -3.9f, 4.0f) == DragAxis::Undecided);
    // A NaN movement is no movement.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(gg::ui::decideAxis(nan, 5.0f, 4.0f) == DragAxis::Vertical);
    CHECK(gg::ui::decideAxis(5.0f, nan, 4.0f) == DragAxis::Horizontal);
    CHECK(gg::ui::decideAxis(nan, nan, 4.0f) == DragAxis::Undecided);
}

// LaneState: the canvas's UI-time bookkeeping (JUCE-free, so it runs under the sanitizers too):
// the engine clock extrapolated between the snapshots, and the grains still playing.

TEST_CASE("the ghost window shows where the effective values differ", "[ui]")
{
    // Spec §6.2: the scan window at the effective values, shown when Position's or Spray's
    // effective value is more than kGhostThreshold from its base value, on either lane.
    STATIC_REQUIRE(gg::ui::kGhostThreshold == 1.0e-4f);
    for (const LaneGeometry &lane : {fxLane(), instrumentLane(48000)})
    {
        const gg::ui::ScanValues base{0.4f, 0.3f};
        const auto window = [&lane](float position, float spray) {
            return gg::ui::scanWindow(position, spray, lane);
        };
        // Equal: none.
        CHECK_FALSE(gg::ui::ghostWindow(base, base, lane).has_value());
        // Spray half its domain up (Macro Spray at its top): the effective window, wider.
        const auto sprayed = gg::ui::ghostWindow(base, {0.4f, 0.8f}, lane);
        REQUIRE(sprayed.has_value());
        CHECK(sprayed->x0 == window(0.4f, 0.8f).x0);
        CHECK(sprayed->x1 == window(0.4f, 0.8f).x1);
        CHECK(sprayed->x0 < window(0.4f, 0.3f).x0);
        // Position moved alone: the window at the effective Position.
        const auto moved = gg::ui::ghostWindow(base, {0.5f, 0.3f}, lane);
        REQUIRE(moved.has_value());
        CHECK(moved->x0 == window(0.5f, 0.3f).x0);
        CHECK(moved->x1 == window(0.5f, 0.3f).x1);
        // Differences under the threshold, in both values: none; just over it, in one: shown.
        CHECK_FALSE(gg::ui::ghostWindow(base, {0.4f + 5.0e-5f, 0.3f - 5.0e-5f}, lane).has_value());
        CHECK(gg::ui::ghostWindow(base, {0.4f, 0.3f + 2.0e-4f}, lane).has_value());
        CHECK(gg::ui::ghostWindow(base, {0.4f - 2.0e-4f, 0.3f}, lane).has_value());
    }
}

TEST_CASE("a peak column reads only the frames the ring has written", "[ui]")
{
    // The FX lane's peak strip: 100 columns of 480 frames each. The strip wraps, so a frame at
    // or past the written head would read a bucket a ring's length old.
    constexpr int kColumns = 100;
    constexpr double kPerColumn = 480.0;
    constexpr int64_t kWritten = 1'000'000;
    constexpr int64_t kOldest = kWritten - 768'000 + 64; // a 16 s strip at 48 kHz
    // No default argument for the column count: a local may not appear in one (GCC 12 rejects
    // it, constexpr or not).
    const auto spanOf = [](int columns, int x, int64_t head) {
        return gg::ui::peakColumnFrames(x, columns, kPerColumn, head, kOldest, kWritten);
    };
    const auto span = [&spanOf](int x, int64_t head) { return spanOf(kColumns, x, head); };

    // The head at the written frames: column x reads its own 480 frames, up to the head.
    for (const int x : {0, 50, 99})
    {
        const gg::ui::FrameSpan s = span(x, kWritten);
        CHECK(s.first == kWritten - (kColumns - x) * 480);
        CHECK(s.end == kWritten - (kColumns - x - 1) * 480);
    }

    // The head extrapolated 1,000 frames past the last published block: no column reads past
    // the written frames. Columns 98 and 99 lie wholly past them, so read nothing, and column
    // 97, which straddles them, stops there.
    constexpr int64_t kHead = kWritten + 1000;
    for (int x = 0; x < kColumns; ++x)
    {
        const gg::ui::FrameSpan s = span(x, kHead);
        CHECK(s.end <= kWritten);
        CHECK(s.first <= s.end);
    }
    CHECK(span(99, kHead).first == span(99, kHead).end);
    CHECK(span(98, kHead).first == span(98, kHead).end);
    CHECK(span(97, kHead).first == kWritten - 440);
    CHECK(span(97, kHead).end == kWritten);
    CHECK(span(96, kHead).end == kWritten - 440);

    // A lane longer than the strip (2,000 columns, 20 s): the oldest columns start at the
    // strip's oldest whole bucket, or read nothing.
    CHECK(spanOf(2000, 0, kWritten).first == kOldest);
    CHECK(spanOf(2000, 0, kWritten).end == kOldest);
    const gg::ui::FrameSpan inside = spanOf(2000, 1999, kWritten);
    CHECK(inside.first == kWritten - 480);
    CHECK(inside.end == kWritten);
}

TEST_CASE("the lane clock extrapolates the published clock", "[ui]")
{
    gg::ui::LaneClock clock;
    CHECK_FALSE(clock.valid());
    CHECK_FALSE(clock.update(48000, 10.0, kRate)); // the first frame: now is the clock
    CHECK(clock.valid());
    CHECK(clock.now() == 48000);

    // No new block for 10 ms: now runs on at the sample rate.
    CHECK_FALSE(clock.update(48000, 10.010, kRate));
    CHECK(clock.now() == 48480);

    // A block lands that is behind the extrapolation: now never steps back.
    CHECK_FALSE(clock.update(48256, 10.011, kRate));
    CHECK(clock.now() == 48480);
    // ... and runs on from the new clock once that passes it.
    CHECK_FALSE(clock.update(48256, 10.020, kRate));
    CHECK(clock.now() == 48256 + 432); // 9 ms past the new block

    // The host stops calling process: the extrapolation stops after kMaxExtrapolationSeconds.
    CHECK_FALSE(clock.update(48256, 20.0, kRate));
    const auto held = clock.now();
    CHECK(held == 48256 + static_cast<int64_t>(
                              std::llround(gg::ui::LaneClock::kMaxExtrapolationSeconds * kRate)));
    CHECK_FALSE(clock.update(48256, 30.0, kRate));
    CHECK(clock.now() == held);

    // A wall clock that goes backwards adds nothing.
    CHECK_FALSE(clock.update(48256, 5.0, kRate));
    CHECK(clock.now() == held);
}

TEST_CASE("the lane clock restarts with a new activation", "[ui]")
{
    gg::ui::LaneClock clock;
    clock.update(480000, 1.0, kRate);
    // A reactivation's Processor starts again from 0: update says so, and now follows at once.
    CHECK(clock.update(64, 1.5, kRate));
    CHECK(clock.now() == 64);
    CHECK_FALSE(clock.update(64, 1.5, kRate));
    CHECK_FALSE(clock.update(128, 1.6, kRate));
    CHECK(clock.now() == 128);

    // Inactive: no clock until the next update, which starts afresh.
    clock.reset();
    CHECK_FALSE(clock.valid());
    CHECK(clock.now() == 0);
    CHECK_FALSE(clock.update(32, 2.0, kRate));
    CHECK(clock.now() == 32);

    // A sample rate that isn't positive and finite extrapolates nothing.
    CHECK_FALSE(clock.update(32, 3.0, 0.0));
    CHECK(clock.now() == 32);
    CHECK_FALSE(clock.update(32, 4.0, std::numeric_limits<double>::quiet_NaN()));
    CHECK(clock.now() == 32);
}

TEST_CASE("the write head extrapolates unless frozen", "[ui]")
{
    CHECK(gg::ui::headAtNow(1000, 5000, 5480, false) == 1480);
    CHECK(gg::ui::headAtNow(1000, 5000, 5480, true) == 1000);
    // A now behind the snapshot (a snapshot newer than the last clock read) never moves it back.
    CHECK(gg::ui::headAtNow(1000, 5000, 4000, false) == 1000);
}

namespace
{
gg::ui::LaneGrain liveGrain(int64_t onset, float duration, double startPos = 0.0)
{
    return {.onsetSample = onset,
            .startPos = startPos,
            .rate = 1.0f,
            .durationSamples = duration,
            .gain = 1.0f,
            .pan = 0.0f};
}

std::vector<double> startPositions(const gg::ui::LiveGrains &grains)
{
    std::vector<double> out;
    grains.forEach([&out](const gg::ui::LaneGrain &g) { out.push_back(g.startPos); });
    return out;
}
} // namespace

TEST_CASE("live grains are kept until they end", "[ui]")
{
    gg::ui::LiveGrains grains;
    CHECK(grains.size() == 0);
    grains.push(liveGrain(100, 50.0f, 1.0));  // plays 100 .. 149
    grains.push(liveGrain(120, 100.0f, 2.0)); // plays 120 .. 219
    grains.push(liveGrain(400, 10.0f, 3.0));  // not started yet: kept
    grains.prune(149);
    CHECK(startPositions(grains) == std::vector<double>{1.0, 2.0, 3.0});
    grains.prune(150); // the first has ended
    CHECK(startPositions(grains) == std::vector<double>{2.0, 3.0});
    grains.prune(220);
    CHECK(startPositions(grains) == std::vector<double>{3.0});

    // A grain that can never play (no duration, or a NaN one) goes at the first prune.
    grains.push(liveGrain(500, 0.0f, 4.0));
    grains.push(liveGrain(500, std::numeric_limits<float>::quiet_NaN(), 5.0));
    grains.prune(0);
    CHECK(startPositions(grains) == std::vector<double>{3.0});

    grains.clear();
    CHECK(grains.size() == 0);
}

TEST_CASE("live grains drop the oldest when full", "[ui]")
{
    STATIC_REQUIRE(gg::ui::LiveGrains::kCapacity == 8192); // INTENT §17 states the number
    gg::ui::LiveGrains grains;
    const auto n = gg::ui::LiveGrains::kCapacity;
    for (std::size_t i = 0; i < n + 3; ++i)
    {
        grains.push(liveGrain(0, 1.0e6f, static_cast<double>(i)));
    }
    REQUIRE(grains.size() == n);
    const auto kept = startPositions(grains);
    CHECK(kept.front() == 3.0); // the three oldest went
    CHECK(kept.back() == static_cast<double>(n + 2));

    // Pruning a full list keeps its order; a push while it is still full drops the oldest again.
    grains.prune(0);
    CHECK(grains.size() == n);
    grains.push(liveGrain(10, 1.0f, -1.0)); // ends at 11
    CHECK(grains.size() == n);
    CHECK(startPositions(grains).back() == -1.0);
    grains.prune(11);
    CHECK(grains.size() == n - 1);
    CHECK(startPositions(grains).front() == 4.0);
}
