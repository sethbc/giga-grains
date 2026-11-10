// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The sync clock (M3 sub-project 7's spec R1, R2, RB1 to RB5, §4.2): a closed-form beat line,
// beatAt(t) = beatA + (t - a) × k, relocked only at a discontinuity (a jump, a tempo change, a
// play-state change or a meter change), so a steady host transport anchors it once.

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Rng.h"
#include "gg/core/SyncClock.h"
#include "gg/core/Transport.h"

using gg::core::SyncClock;
using gg::core::SyncLine;
using gg::core::Transport;

namespace
{
constexpr double kRate = 48000.0;
constexpr int64_t kCell = 32;

Transport playingAt(double beat, double tempo = 120.0)
{
    return Transport{.hasBeats = true, .beatPosition = beat, .tempoBpm = tempo, .playing = true};
}

Transport stoppedAt(double beat, double tempo = 120.0)
{
    return Transport{.hasBeats = true, .beatPosition = beat, .tempoBpm = tempo, .playing = false};
}

Transport noTimeline(double tempo)
{
    // A position the clock must never take: hasBeats is false.
    return Transport{.hasBeats = false, .beatPosition = 50.0, .tempoBpm = tempo, .playing = true};
}

/// A clock locked to a playing 120 BPM host at beat 0 at sample 0.
SyncClock steadyClock()
{
    SyncClock c(kRate);
    c.observe(playingAt(0.0), 0);
    c.resolveAt(0);
    return c;
}

/// The beat a host at 120 BPM from beat 0 reports at s, on CLAP's 2^-31 grid.
double hostBeat120(int64_t s)
{
    const double truth = static_cast<double>(s) * 120.0 / (60.0 * kRate);
    return static_cast<double>(std::llround(truth * 0x1.0p31)) / 0x1.0p31;
}

/// Bitwise equality, so a test can't pass on -0 == 0 or fail on NaN by accident.
bool same(double x, double y) { return std::bit_cast<uint64_t>(x) == std::bit_cast<uint64_t>(y); }
} // namespace

TEST_CASE("sync clock steady host never relocks", "[core][temposync]")
{
    SyncClock clock(kRate);
    SyncClock reference(kRate);
    reference.observe(playingAt(hostBeat120(0)), 0);
    reference.resolveAt(0);

    constexpr std::array<int64_t, 6> kSizes{1, 7, 32, 64, 256, 333};
    int64_t s = 0;
    bool resolved = false;
    int64_t boundaries = 0;
    for (uint64_t call = 0; call < 2000; ++call)
    {
        const uint64_t h = gg::rng::hash({7, 0, 0, gg::rng::Field::SignalNoise, call});
        const int64_t n = kSizes[h % kSizes.size()];
        clock.observe(playingAt(hostBeat120(s)), s);
        const int64_t first = ((s + kCell - 1) / kCell) * kCell;
        for (int64_t c = first; c < s + n; c += kCell)
        {
            clock.resolveAt(c);
            resolved = true;
            ++boundaries;
            REQUIRE(clock.epoch() == 1);
            REQUIRE(clock.locked());
            REQUIRE(same(clock.beatAt(c), reference.beatAt(c)));
        }
        s += n;
    }
    REQUIRE(resolved);
    REQUIRE(boundaries > 1000);
    REQUIRE(clock.epoch() == 1);
}

TEST_CASE("sync clock jump relocks at the first boundary", "[core][temposync]")
{
    SyncClock clock = steadyClock();
    const uint32_t epoch = clock.epoch();
    const int64_t s = 48013;
    const double h = clock.beatAt(s) + 0.5;
    clock.observe(playingAt(h), s);
    REQUIRE(clock.pending());
    REQUIRE(clock.pendingChangesLine());

    // A boundary before s doesn't resolve it.
    clock.resolveAt(48000);
    REQUIRE(clock.pending());
    REQUIRE(clock.epoch() == epoch);

    clock.resolveAt(48032);
    REQUIRE_FALSE(clock.pending());
    REQUIRE(same(clock.beatAt(48032), h + 19.0 * clock.beatsPerSample()));
    REQUIRE(clock.anchorSample() == 48032);
    REQUIRE(clock.epoch() == epoch + 1);
    REQUIRE(clock.locked());
}

TEST_CASE("sync clock one sample of jitter is steady", "[core][temposync]")
{
    SyncClock clock = steadyClock();
    const uint32_t epoch = clock.epoch();
    const double k = clock.beatsPerSample();
    const int64_t s = 48013;
    const double before = clock.beatAt(48032);

    clock.observe(playingAt(clock.beatAt(s) + k), s);
    REQUIRE_FALSE(clock.pendingChangesLine());
    clock.resolveAt(48032);
    REQUIRE(clock.epoch() == epoch);
    REQUIRE(same(clock.beatAt(48032), before));
    REQUIRE(clock.anchorSample() == 0);

    // The steady band is one cell of beats: 31 samples' worth is steady, 33 is a jump.
    clock.observe(playingAt(clock.beatAt(s) - 31.0 * k), s);
    REQUIRE_FALSE(clock.pendingChangesLine());
    clock.observe(playingAt(clock.beatAt(s) + 33.0 * k), s);
    REQUIRE(clock.pendingChangesLine());
}

TEST_CASE("sync clock tempo change is continuous", "[core][temposync]")
{
    SyncClock clock = steadyClock();
    const uint32_t epoch = clock.epoch();
    const int64_t b = 96000;
    const double before = clock.beatAt(b);

    clock.observe(playingAt(clock.beatAt(b), 90.0), b);
    REQUIRE(clock.pendingChangesLine());
    clock.resolveAt(b);
    REQUIRE(same(clock.beatAt(b), before));
    REQUIRE(clock.epoch() == epoch + 1);
    REQUIRE(clock.tempo() == 90.0);
    REQUIRE(clock.locked());
    REQUIRE(std::fabs((clock.beatAt(b + 48000) - clock.beatAt(b)) -
                      48000.0 * 90.0 / (60.0 * kRate)) <= 1e-12);

    // The host steady at the new tempo leaves the line alone.
    const int64_t s = b + 4801;
    clock.observe(playingAt(clock.beatAt(s), 90.0), s);
    REQUIRE_FALSE(clock.pendingChangesLine());
    clock.resolveAt(b + 4832);
    REQUIRE(clock.epoch() == epoch + 1);
    REQUIRE(clock.anchorSample() == b);
}

TEST_CASE("sync clock stop runs on and play relocks", "[core][temposync]")
{
    SyncClock clock = steadyClock();
    const int64_t b0 = 192000; // beat 8 at 120 BPM
    clock.observe(stoppedAt(clock.beatAt(b0)), b0);
    REQUIRE(clock.pendingChangesLine());
    const uint32_t epoch = clock.epoch();
    clock.resolveAt(b0);
    REQUIRE(clock.epoch() == epoch + 1);
    REQUIRE_FALSE(clock.locked());
    const double atStop = clock.beatAt(b0);
    REQUIRE(std::fabs(atStop - 8.0) <= 1e-12);

    // While stopped the host's position is ignored: the line runs on at the last tempo.
    for (int64_t s = b0 + 100; s < b0 + 24000; s += 1000)
    {
        clock.observe(stoppedAt(3.0), s);
        REQUIRE_FALSE(clock.pendingChangesLine());
        clock.resolveAt(((s + kCell - 1) / kCell) * kCell);
    }
    REQUIRE(clock.epoch() == epoch + 1);
    REQUIRE(std::fabs(clock.beatAt(b0 + 24000) - (atStop + 1.0)) <= 1e-12);
    REQUIRE_FALSE(clock.locked());

    // Play relocks to the host's position, whatever it is.
    const int64_t s = b0 + 30001;
    const int64_t b = b0 + 30016;
    clock.observe(playingAt(0.0), s);
    REQUIRE(clock.pendingChangesLine());
    clock.resolveAt(b);
    REQUIRE(same(clock.beatAt(b), 0.0 + static_cast<double>(b - s) * clock.beatsPerSample()));
    REQUIRE(clock.locked());
    REQUIRE(clock.epoch() == epoch + 2);
}

TEST_CASE("sync clock without a timeline keeps its position", "[core][temposync]")
{
    SyncClock clock(kRate);
    clock.observe(noTimeline(100.0), 0);
    clock.resolveAt(0);
    REQUIRE_FALSE(clock.locked());
    REQUIRE(clock.tempo() == 100.0);
    REQUIRE(clock.epoch() == 1);
    for (int64_t s = 1; s < 96000; s += 997)
    {
        clock.observe(noTimeline(100.0), s);
        REQUIRE_FALSE(clock.pendingChangesLine());
        clock.resolveAt(((s + kCell - 1) / kCell) * kCell);
    }
    for (const int64_t t : {int64_t{0}, int64_t{4800}, int64_t{48000}, int64_t{95999}})
        REQUIRE(std::fabs(clock.beatAt(t) - static_cast<double>(t) * 100.0 / (60.0 * kRate)) <=
                1e-12);
    REQUIRE_FALSE(clock.locked());
    REQUIRE(clock.epoch() == 1);

    // A new tempo rebases: the beat is continuous at the boundary, the slope bends.
    const int64_t b = 96000;
    const double before = clock.beatAt(b);
    clock.observe(noTimeline(140.0), b);
    REQUIRE(clock.pendingChangesLine());
    clock.resolveAt(b);
    REQUIRE(same(clock.beatAt(b), before));
    REQUIRE(clock.tempo() == 140.0);
    REQUIRE(clock.epoch() == 2);
    REQUIRE_FALSE(clock.locked());
    REQUIRE(std::fabs((clock.beatAt(b + 48000) - before) - 48000.0 * 140.0 / (60.0 * kRate)) <=
            1e-12);
}

TEST_CASE("sync clock latest observation wins", "[core][temposync]")
{
    SyncClock clock = steadyClock();
    const uint32_t epoch = clock.epoch();
    const int64_t s = 48001;
    const double h1 = clock.beatAt(s) + 0.25;
    const double h2 = clock.beatAt(s + 1) + 0.5;
    clock.observe(playingAt(h1), s);     // a 1-sample call with no boundary
    clock.observe(playingAt(h2), s + 1); // the next one replaces it
    clock.resolveAt(48032);
    REQUIRE(same(clock.beatAt(48032), h2 + 30.0 * clock.beatsPerSample()));
    REQUIRE(clock.epoch() == epoch + 1);
    REQUIRE_FALSE(clock.pending());
}

namespace
{
void requireSameClock(const SyncClock &x, const SyncClock &y)
{
    REQUIRE(same(x.tempo(), y.tempo()));
    REQUIRE(same(x.beatsPerSample(), y.beatsPerSample()));
    REQUIRE(same(x.barBeats(), y.barBeats()));
    REQUIRE(same(x.barOffset(), y.barOffset()));
    REQUIRE(x.locked() == y.locked());
    REQUIRE(x.epoch() == y.epoch());
    REQUIRE(x.anchorSample() == y.anchorSample());
    REQUIRE(same(x.anchorBeat(), y.anchorBeat()));
    REQUIRE(x.pending() == y.pending());
    REQUIRE(x.pendingChangesLine() == y.pendingChangesLine());
    REQUIRE(same(x.beatAt(12345), y.beatAt(12345)));
    REQUIRE(x.nextLineSample(SyncLine::Bar, 1) == y.nextLineSample(SyncLine::Bar, 1));
    REQUIRE(x.nextLineSample(SyncLine::Beat, 1) == y.nextLineSample(SyncLine::Beat, 1));
}
} // namespace

TEST_CASE("sync clock reset", "[core][temposync]")
{
    SyncClock clock = steadyClock();
    clock.observe(playingAt(17.0, 133.0), 48013);
    clock.resolveAt(48032);
    Transport meter = playingAt(clock.beatAt(96000), 133.0);
    meter.beatsPerBar = 7;
    meter.beatUnit = 8;
    meter.barStart = 0.5;
    clock.observe(meter, 96000);
    clock.resolveAt(96000);
    clock.observe(playingAt(3.0), 96001); // left pending
    REQUIRE(clock.pending());

    clock.reset();
    const SyncClock fresh(kRate);
    requireSameClock(clock, fresh);
    REQUIRE(clock.tempo() == 120.0);
    REQUIRE(clock.epoch() == 0);
    REQUIRE_FALSE(clock.locked());
    REQUIRE_FALSE(clock.pending());
    REQUIRE(clock.barBeats() == 4.0);
    REQUIRE(clock.barOffset() == 0.0);

    // After a reset the first observation takes the host's position, stopped or not.
    SyncClock seeded(kRate);
    clock.observe(stoppedAt(10.0), 0);
    clock.resolveAt(0);
    seeded.observe(stoppedAt(10.0), 0);
    seeded.resolveAt(0);
    requireSameClock(clock, seeded);
    REQUIRE(clock.beatAt(0) == 10.0);
    REQUIRE_FALSE(clock.locked());
    REQUIRE(clock.epoch() == 1);
}

TEST_CASE("sync clock meter and lines", "[core][temposync]")
{
    SyncClock clock(kRate);
    Transport t = playingAt(0.0);
    t.beatsPerBar = 6;
    t.beatUnit = 8;
    t.barStart = 1.5;
    clock.observe(t, 0);
    clock.resolveAt(0);
    REQUIRE(clock.barBeats() == 3.0);
    REQUIRE(clock.barOffset() == 1.5);
    REQUIRE(clock.nextLineSample(SyncLine::Bar, 0) == 36000);
    REQUIRE(clock.nextLineSample(SyncLine::Beat, 1) == 24000);
    REQUIRE(clock.nextLineSample(SyncLine::Beat, 0) == 0);

    // 3/4 with no offset: an epoch, no position change, bar lines every 72,000 samples.
    const uint32_t epoch = clock.epoch();
    const int64_t anchor = clock.anchorSample();
    const double anchorBeat = clock.anchorBeat();
    Transport threeFour = playingAt(clock.beatAt(96000));
    threeFour.beatsPerBar = 3;
    threeFour.beatUnit = 4;
    clock.observe(threeFour, 96000);
    REQUIRE(clock.pendingChangesLine());
    clock.resolveAt(96000);
    REQUIRE(clock.epoch() == epoch + 1);
    REQUIRE(clock.anchorSample() == anchor);
    REQUIRE(same(clock.anchorBeat(), anchorBeat));
    REQUIRE(clock.barBeats() == 3.0);
    REQUIRE(clock.barOffset() == 0.0);
    const int64_t bar1 = clock.nextLineSample(SyncLine::Bar, 96000); // beat 6
    REQUIRE(bar1 == 144000);
    const int64_t bar2 = clock.nextLineSample(SyncLine::Bar, bar1 + 1);
    REQUIRE(bar2 - bar1 == 72000);
    REQUIRE(clock.nextLineSample(SyncLine::Bar, bar2 + 1) - bar2 == 72000);

    // The same meter again changes nothing.
    threeFour.beatPosition = clock.beatAt(96001);
    clock.observe(threeFour, 96001);
    REQUIRE_FALSE(clock.pendingChangesLine());

    // A meter that isn't one is 4/4.
    Transport bad = playingAt(clock.beatAt(97000));
    bad.beatsPerBar = 0;
    bad.beatUnit = 3;
    clock.observe(bad, 97000);
    clock.resolveAt(97024);
    REQUIRE(clock.barBeats() == 4.0);
}

TEST_CASE("sync clock lines at negative beats", "[core][temposync]")
{
    SyncClock clock(kRate);
    clock.observe(playingAt(-4.25), 0);
    clock.resolveAt(0);
    REQUIRE(clock.nextLineSample(SyncLine::Beat, 0) == 6000);
    REQUIRE(clock.nextLineSample(SyncLine::Bar, 0) == 6000); // beat -4 is a 4/4 bar line
    REQUIRE(clock.nextLineSample(SyncLine::Beat, 6000) == 6000);
    REQUIRE(clock.nextLineSample(SyncLine::Beat, 6001) == 30000);
    REQUIRE(clock.nextLineSample(SyncLine::Bar, 6001) == 102000);
}

TEST_CASE("sync clock lines round half away from zero", "[core][temposync]")
{
    // 120 BPM at 32,768 Hz is 2^-14 beats per sample, so every value below is exact.
    SyncClock up(32768.0);
    up.observe(playingAt(-0x1.0p-15), 0); // beat 0 at sample 0.5
    up.resolveAt(0);
    REQUIRE(up.beatsPerSample() == 0x1.0p-14);
    REQUIRE(up.nextLineSample(SyncLine::Beat, 0) == 1);
    REQUIRE(up.nextLineSample(SyncLine::Beat, 2) == 16385);

    SyncClock down(32768.0);
    down.observe(playingAt(0x1.0p-15), 0); // beat 0 at sample -0.5
    down.resolveAt(0);
    REQUIRE(down.nextLineSample(SyncLine::Beat, -1) == -1);
    REQUIRE(down.nextLineSample(SyncLine::Beat, 0) == 16384);
}

TEST_CASE("sync clock tempo is sanitized", "[core][temposync]")
{
    SyncClock clock(kRate);
    const double kInf = std::numeric_limits<double>::infinity();
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(), kInf, -kInf, -5.0, 0.0})
    {
        clock.observe(noTimeline(bad), 0);
        REQUIRE_FALSE(clock.pendingChangesLine());
        clock.resolveAt(0);
        REQUIRE(clock.tempo() == 120.0);
    }
    clock.observe(noTimeline(0.5), 0);
    clock.resolveAt(0);
    REQUIRE(clock.tempo() == 1.0);
    clock.observe(noTimeline(5000.0), 0);
    clock.resolveAt(0);
    REQUIRE(clock.tempo() == 1000.0);

    SyncClock slow(kRate);
    slow.observe(playingAt(0.0, 1.0), 0);
    slow.resolveAt(0);
    REQUIRE(slow.tempo() == 1.0);
    REQUIRE(slow.nextLineSample(SyncLine::Bar, 48000) == 11520000); // 4 beats of 60 s
}

TEST_CASE("sync clock takes a non-finite host position as no timeline", "[core][temposync]")
{
    SyncClock clock = steadyClock();
    const uint32_t epoch = clock.epoch();
    const double before = clock.beatAt(48032);
    clock.observe(playingAt(std::numeric_limits<double>::quiet_NaN()), 48013);
    REQUIRE(clock.pendingChangesLine()); // it unlocks: the line runs on
    clock.resolveAt(48032);
    REQUIRE(same(clock.beatAt(48032), before));
    REQUIRE(std::fabs(clock.beatAt(96000) - 4.0) <= 1e-12);
    REQUIRE_FALSE(clock.locked());
    REQUIRE(clock.epoch() == epoch + 1);
}
