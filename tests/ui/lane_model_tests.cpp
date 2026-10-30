// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// LaneModel: the canvas's JUCE-free geometry (spec §6.2-6.3): where a playing grain's capsule
// lies on the lane, where the scan window lies, and the inverse mappings a drag needs. These
// tests need no editor or JUCE (one compares the tapers with gg-core's ParamMap), so they also
// run in the sanitizer builds.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/ui/LaneModel.h"
#include "gg/ui/LaneState.h"

using Catch::Matchers::WithinAbs;
using gg::ui::DragTarget;
using gg::ui::LaneGeometry;
using gg::ui::LaneGrain;

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

TEST_CASE("pan maps to y", "[ui]")
{
    const LaneGeometry g = fxLane(1600.0f, 100.0f, 1.0f);
    const int64_t head = 48000 * 20;
    LaneGrain grain = grainAt(head, static_cast<double>(head - 48000));

    // y = height/2 + pan × (height/2 − radius): the capsule stays on the lane at either extreme.
    struct Case
    {
        float pan, y;
    };
    for (const Case c : {Case{0.0f, 50.0f}, Case{1.0f, 97.0f}, Case{-1.0f, 3.0f}, Case{0.5f, 73.5f},
                         Case{-0.25f, 38.25f}, Case{5.0f, 97.0f}, Case{-5.0f, 3.0f}})
    {
        INFO("pan " << c.pan);
        grain.pan = c.pan;
        const auto capsule = gg::ui::grainCapsule(grain, head, head, g);
        REQUIRE(capsule.has_value());
        REQUIRE_THAT(capsule->y, WithinAbs(c.y, 1e-4));
        REQUIRE(capsule->radius == 3.0f);
    }

    // The radius is 3 px at the pixel scale, at least 1.5 px.
    grain.pan = 1.0f;
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

    // A NaN gain or pan draws dark and centred rather than not at all.
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
    // LaneModel keeps its own copy of ParamMap's FX Position and Spray tapers (gg-ui-core doesn't
    // link gg-core): the window's seconds must be toCloudParams's, so the copies can't drift. On
    // the 1600 px, 16 s lane a pixel is 10 ms.
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
