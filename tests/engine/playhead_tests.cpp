// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The playhead (M3 playhead spec H3, H5, §3a HB1, HB3, HB4): scanFold's three edges, and the
// closed-form offset x(t) = x0 + r (t - tau0) with its restart, its rebase and canon.

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "gg/grain/Playhead.h"

namespace
{
constexpr double kSampleRate = 48000.0;
// 375 / 512: at 48 kHz and 1x, r = 2^-16 units a sample, exactly.
constexpr double kDyadicUnitsPerSecond = kSampleRate / 65536.0;

uint64_t bits(double v) { return std::bit_cast<uint64_t>(v); }

// A window [lo, hi] of the given unitsPerSecond.
gg::ScanGeometry geometry(double unitsPerSecond, double lo = 0.0, double hi = 1.0)
{
    return gg::ScanGeometry{unitsPerSecond, lo, hi};
}
} // namespace

TEST_CASE("scanFold wraps bounces and stops", "[engine][playhead]")
{
    using gg::ScanEdge;
    CHECK(gg::scanFold(1.25, 0.0, 1.0, ScanEdge::Wrap) == 0.25);
    CHECK(gg::scanFold(-0.25, 0.0, 1.0, ScanEdge::Wrap) == 0.75);
    CHECK(gg::scanFold(1.25, 0.0, 1.0, ScanEdge::Bounce) == 0.75);
    CHECK(gg::scanFold(2.25, 0.0, 1.0, ScanEdge::Bounce) == 0.25);
    CHECK(gg::scanFold(-0.25, 0.0, 1.0, ScanEdge::Bounce) == 0.25);
    CHECK(gg::scanFold(1.25, 0.0, 1.0, ScanEdge::Stop) == 1.0);
    CHECK(gg::scanFold(-0.25, 0.0, 1.0, ScanEdge::Stop) == 0.0);

    // Over a window that doesn't start at 0 (a ring's window in seconds, say).
    CHECK(gg::scanFold(5.5, 2.0, 4.0, ScanEdge::Wrap) == 3.5);
    CHECK(gg::scanFold(5.5, 2.0, 4.0, ScanEdge::Bounce) == 2.5);
    CHECK(gg::scanFold(5.5, 2.0, 4.0, ScanEdge::Stop) == 4.0);
    // Inside the window every edge is the identity.
    for (const auto edge : {ScanEdge::Wrap, ScanEdge::Bounce, ScanEdge::Stop})
    {
        CHECK(bits(gg::scanFold(0.3, 0.0, 1.0, edge)) == bits(0.3));
    }
}

TEST_CASE("scanFold leaves the window's ends", "[engine][playhead]")
{
    using gg::ScanEdge;
    CHECK(gg::scanFold(1.0, 0.0, 1.0, ScanEdge::Wrap) == 0.0);
    CHECK(gg::scanFold(0.0, 0.0, 1.0, ScanEdge::Wrap) == 0.0);
    CHECK(gg::scanFold(1.0, 0.0, 1.0, ScanEdge::Bounce) == 1.0);
    CHECK(gg::scanFold(2.0, 0.0, 1.0, ScanEdge::Bounce) == 0.0);
    CHECK(gg::scanFold(1.0, 0.0, 1.0, ScanEdge::Stop) == 1.0);

    // An empty or reversed window gives lo, at every edge.
    for (const auto edge : {ScanEdge::Wrap, ScanEdge::Bounce, ScanEdge::Stop})
    {
        CHECK(gg::scanFold(0.7, 0.5, 0.5, edge) == 0.5);
        CHECK(gg::scanFold(0.7, 0.5, 0.25, edge) == 0.5);
    }

    // No NaN out: a non-finite point gives lo.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(gg::scanFold(nan, 0.0, 1.0, ScanEdge::Stop) == 0.0);
    for (const auto edge : {ScanEdge::Wrap, ScanEdge::Bounce, ScanEdge::Stop})
    {
        CHECK(gg::scanFold(nan, 0.0, 1.0, edge) == 0.0);
        CHECK(gg::scanFold(inf, 0.0, 1.0, edge) == 0.0);
        CHECK(gg::scanFold(0.5, 0.0, nan, edge) == 0.0);
    }

    // A point a rounding below an end stays in [lo, hi): mod's result of b is taken as 0.
    const double justBelow = -std::numeric_limits<double>::denorm_min();
    const double wrapped = gg::scanFold(justBelow, 0.0, 1.0, ScanEdge::Wrap);
    CHECK(wrapped >= 0.0);
    CHECK(wrapped < 1.0);
}

TEST_CASE("a default playhead is at rest with a restart pending", "[engine][playhead]")
{
    gg::Playhead p;
    CHECK(p.edge() == gg::ScanEdge::Wrap);
    CHECK_FALSE(p.moving());
    CHECK(p.offsetAt(0.0) == 0.0);
    CHECK(p.offsetAt(123456.75) == 0.0);

    // Speed 0 over a ring (unitsPerSecond -1) gives r = -0.0: still at rest, x(t) still 0.0.
    p.sync(4096, 0.0, gg::ScanEdge::Wrap, geometry(-1.0, 0.0, 16.0), kSampleRate, 2.0);
    CHECK_FALSE(p.moving());
    CHECK(p.offsetAt(4096.0) == 0.0);
    CHECK(p.offsetAt(100000.5) == 0.0);
}

TEST_CASE("the offset is linear between rebases", "[engine][playhead]")
{
    gg::Playhead p;
    p.sync(0, 1.0, gg::ScanEdge::Wrap, geometry(1.0), kSampleRate, 0.5);
    CHECK(p.moving());
    CHECK(p.offsetAt(0.0) == 0.0);
    CHECK(p.offsetAt(48000.0) == 1.0);
    CHECK(bits(p.offsetAt(24000.5)) == bits(24000.5 / 48000.0));

    // tau0 is the sync's sample: a restart at 1000 measures from there.
    gg::Playhead q;
    q.sync(1000, 2.0, gg::ScanEdge::Wrap, geometry(1.0), kSampleRate, 0.5);
    CHECK(q.offsetAt(1000.0) == 0.0);
    CHECK(bits(q.offsetAt(25000.0)) == bits(1.0));
    CHECK(q.offsetAt(-23000.0) == -1.0); // the line extends backwards too
}

TEST_CASE("a sync with nothing changed changes nothing", "[engine][playhead]")
{
    // A speed and a length that aren't dyadic, so any rebase would round differently.
    const double speed = 8.0 * 0.37 * 0.37 * 0.37;
    const auto g = geometry(kSampleRate / (3.0 * kSampleRate + 17.0));
    gg::Playhead once;
    once.sync(0, speed, gg::ScanEdge::Bounce, g, kSampleRate, 0.3);
    gg::Playhead often;
    for (int64_t t = 0; t <= 3200; t += 32)
    {
        often.sync(t, speed, gg::ScanEdge::Bounce, g, kSampleRate, 0.3);
    }
    CHECK(bits(often.offsetAt(4000.25)) == bits(once.offsetAt(4000.25)));
    CHECK(bits(often.offsetAt(1.0e7)) == bits(once.offsetAt(1.0e7)));
}

TEST_CASE("a window change alone never rebases", "[engine][playhead]")
{
    // A ring's window grows sample by sample (spec HB1): a sync with a new lo or hi and the same
    // rate and edge must do nothing, or the playhead would depend on the host's blocks.
    const double speed = 0.75;
    gg::Playhead fixed;
    fixed.sync(0, speed, gg::ScanEdge::Stop, geometry(-1.0, 0.0, 4.0), kSampleRate, 1.0);
    gg::Playhead growing;
    growing.sync(0, speed, gg::ScanEdge::Stop, geometry(-1.0, 0.0, 4.0), kSampleRate, 1.0);
    for (int64_t t = 64; t <= 96000; t += 64)
    {
        const double hi = 4.0 + static_cast<double>(t) / kSampleRate;
        growing.sync(t, speed, gg::ScanEdge::Stop, geometry(-1.0, 0.0, hi), kSampleRate, 1.0);
    }
    CHECK(bits(growing.offsetAt(100000.5)) == bits(fixed.offsetAt(100000.5)));
}

TEST_CASE("a rebase continues from the same point", "[engine][playhead]")
{
    // Wrap over a window large enough that canon doesn't fold, at a dyadic rate (2^-16 units a
    // sample at 1x) so the slopes compare exactly.
    const auto g = geometry(kDyadicUnitsPerSecond, 0.0, 1000.0);
    gg::Playhead p;
    p.sync(0, 1.0, gg::ScanEdge::Wrap, g, kSampleRate, 0.5);
    const double before = p.offsetAt(9600.0);
    const double slopeBefore = p.offsetAt(9600.0 + 4800.0) - before;
    p.sync(9600, 2.0, gg::ScanEdge::Wrap, g, kSampleRate, 0.5);
    CHECK(bits(p.offsetAt(9600.0)) == bits(before));
    const double slopeAfter = p.offsetAt(9600.0 + 4800.0) - p.offsetAt(9600.0);
    CHECK(slopeAfter == 2.0 * slopeBefore);

    // An edge change rebases too, with the new edge's canon: Bounce reduces over 2L.
    // x = t / 65536, so x(229376) = 3.5: Wrap's canon would give 0.5, Bounce's (over 2L = 2) 1.5.
    gg::Playhead b;
    b.sync(0, 1.0, gg::ScanEdge::Wrap, geometry(kDyadicUnitsPerSecond), kSampleRate, 0.0);
    b.sync(229376, 1.0, gg::ScanEdge::Bounce, geometry(kDyadicUnitsPerSecond), kSampleRate, 0.0);
    CHECK(b.edge() == gg::ScanEdge::Bounce);
    CHECK(b.offsetAt(229376.0) == 1.5);
    CHECK(gg::scanFold(0.0 + b.offsetAt(229376.0), 0.0, 1.0, gg::ScanEdge::Bounce) ==
          gg::scanFold(3.5, 0.0, 1.0, gg::ScanEdge::Bounce));
    CHECK(b.offsetAt(229376.0 + 32768.0) == 2.0); // the same slope after
}

TEST_CASE("stop leaves the edge on a reversal", "[engine][playhead]")
{
    // Stop over [0, 1] from Position 0.5 at 1x of a 1-unit-per-second source: x passes 0.5 (the
    // point reaches 1.0) at 24000.
    const auto g = geometry(1.0);
    gg::Playhead p;
    p.sync(0, 1.0, gg::ScanEdge::Stop, g, kSampleRate, 0.5);
    const int64_t t1 = 36000;
    CHECK(gg::scanFold(0.5 + p.offsetAt(static_cast<double>(t1)), 0.0, 1.0, gg::ScanEdge::Stop) ==
          1.0);
    const int64_t t2 = 72000; // x = 1.5, the point held at 1.0
    p.sync(t2, -1.0, gg::ScanEdge::Stop, g, kSampleRate, 0.5);
    CHECK(p.offsetAt(static_cast<double>(t2)) == 0.5); // canon: the point at 1.0
    const double next = p.offsetAt(static_cast<double>(t2) + 480.0);
    CHECK(next < 0.5);
    CHECK(gg::scanFold(0.5 + next, 0.0, 1.0, gg::ScanEdge::Stop) < 1.0);
}

TEST_CASE("a restart returns to zero", "[engine][playhead]")
{
    gg::Playhead p;
    p.sync(0, 0.6, gg::ScanEdge::Bounce, geometry(0.37), kSampleRate, 0.25);
    p.sync(5000, 1.3, gg::ScanEdge::Bounce, geometry(0.37), kSampleRate, 0.25);
    REQUIRE(p.offsetAt(9000.0) != 0.0);
    p.requestRestart();
    // Nothing moves until the sync takes the restart.
    CHECK(p.offsetAt(9000.0) != 0.0);
    p.sync(9001, 1.3, gg::ScanEdge::Bounce, geometry(0.37), kSampleRate, 0.25);
    CHECK(p.offsetAt(9001.0) == 0.0);
    CHECK(bits(p.offsetAt(9001.0)) == bits(0.0));
    CHECK(p.offsetAt(9002.0) > 0.0);

    // A restart at speed 0 leaves the playhead at rest.
    p.requestRestart();
    p.sync(20000, 0.0, gg::ScanEdge::Wrap, geometry(0.37), kSampleRate, 0.25);
    CHECK_FALSE(p.moving());
    CHECK(p.offsetAt(30000.0) == 0.0);
}
