// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The playhead (M3 playhead spec H3, H5, §3a HB1, HB3, HB4): scanFold's three edges, and the
// closed-form offset x(t) = x0 + r (t - tau0) with its restart, its rebase and canon. Then the
// playhead in the scheduler and the engine (§4.2 to §4.7, HB1 to HB9): every stream scans, a
// Sample's point folded in the scheduler, a ring's at the grain's onset, the smear's at each hop.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/Rng.h"
#include "gg/engine/Engine.h"
#include "gg/grain/AmpEnvelope.h"
#include "gg/grain/CloudScheduler.h"
#include "gg/grain/Playhead.h"
#include "gg/source/LiveSource.h"
#include "gg/source/PreparedSource.h"
#include "gg/spectral/SpectralSource.h"

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

TEST_CASE("a source change at speed 0 never rebases", "[engine][playhead]")
{
    // Speed 0 over a Sample (unitsPerSecond +1) gives r = +0.0 and over a ring (-1) r = -0.0. The
    // two must compare equal, or the switch would rebase and Stop's canon, with Position outside
    // the window, would move x0 off 0 at rest.
    gg::Playhead p;
    p.sync(0, 0.0, gg::ScanEdge::Stop, geometry(1.0, 0.0, 1.0), kSampleRate, 2.0);
    p.sync(256, 0.0, gg::ScanEdge::Stop, geometry(-1.0, 0.0, 1.0), kSampleRate, 2.0);
    CHECK_FALSE(p.moving());
    CHECK(bits(p.offsetAt(256.0)) == bits(0.0));
    p.sync(512, 0.0, gg::ScanEdge::Stop, geometry(1.0, 0.0, 1.0), kSampleRate, 2.0);
    CHECK_FALSE(p.moving());
    CHECK(bits(p.offsetAt(10000.0)) == bits(0.0));
}

// ---- The playhead in the scheduler and the engine (spec §4.2 to §4.7, HB1 to HB9) ----

namespace
{
// `frames` frames of noise at half scale in `channels` channels, at 48 kHz (channel c from
// stream c).
std::shared_ptr<gg::SampleBuffer> noiseBuffer(uint32_t channels, int64_t frames, uint64_t seed)
{
    auto buf = std::make_shared<gg::SampleBuffer>(channels, frames, kSampleRate);
    for (uint32_t c = 0; c < channels; ++c)
    {
        for (int64_t i = 0; i < frames; ++i)
        {
            const auto k = static_cast<uint64_t>(i);
            buf->channel(c)[i] = 0.5f * static_cast<float>(gg::rng::bipolar(gg::rng::hash(
                                            {seed, 0, c, gg::rng::Field::SignalNoise, k})));
        }
    }
    return buf;
}

// Two channels of audio; NaN where nothing wrote.
struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

Stereo unwritten(int64_t frames)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    return {std::vector<float>(static_cast<std::size_t>(frames), nan),
            std::vector<float>(static_cast<std::size_t>(frames), nan)};
}

Stereo noiseInput(int64_t frames, uint64_t seed)
{
    const auto buf = noiseBuffer(2, frames, seed);
    return {std::vector<float>(buf->channel(0), buf->channel(0) + frames),
            std::vector<float>(buf->channel(1), buf->channel(1) + frames)};
}

// Renders [from, to) of `out` in calls of `block` frames, with `in` as the input when given.
void run(gg::Engine &engine, Stereo &out, int64_t from, int64_t to, uint32_t block,
         const Stereo *in = nullptr)
{
    for (int64_t done = from; done < to;)
    {
        const auto n = static_cast<uint32_t>(std::min<int64_t>(block, to - done));
        float *o[2] = {out.left.data() + done, out.right.data() + done};
        if (in != nullptr)
        {
            const float *i[2] = {in->left.data() + done, in->right.data() + done};
            engine.process(i, 2, o, 2, n);
        }
        else
        {
            engine.process(o, 2, n);
        }
        done += n;
    }
}

bool sameBits(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

bool allFinite(const std::vector<float> &v)
{
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

// The grains the engine spawns, recorded into preallocated storage (the sink runs on the audio
// thread).
struct GrainLog
{
    static constexpr std::size_t kCapacity = 8192;
    std::vector<gg::GrainSpec> grains;
    bool overflowed{false};

    GrainLog() { grains.reserve(kCapacity); }

    static void record(void *ctx, uint32_t, const gg::GrainSpec &g)
    {
        auto *log = static_cast<GrainLog *>(ctx);
        if (log->grains.size() < kCapacity)
        {
            log->grains.push_back(g);
        }
        else
        {
            log->overflowed = true;
        }
    }
};

// A grain's exact onset, in engine samples.
double onsetOf(const gg::GrainSpec &g) { return static_cast<double>(g.onsetSample) + g.onsetFrac; }

// The playhead's rate over a Sample of `frames` frames at 48 kHz, as Playhead::sync computes it
// from the engine's geometry (R / (F - 1), spec HB2).
double sampleRate1x(double speed, int64_t frames)
{
    return speed * (kSampleRate / static_cast<double>(frames - 1)) / kSampleRate + 0.0;
}

// The same over a ring (unitsPerSecond -1).
double ringRate(double speed) { return speed * -1.0 / kSampleRate + 0.0; }

// Periodic 64 Hz (a 750-sample period, so every onset is a whole sample), 50 ms grains, no
// spray, at `position`, scanning at `speed`.
gg::CloudParams scanParams(double position, double speed, gg::ScanEdge edge = gg::ScanEdge::Wrap)
{
    gg::CloudParams p{
        .densityHz = 64.0, .grainDurationSec = 0.05, .position = position, .gain = 0.5f};
    p.scanSpeed = speed;
    p.scanEdge = edge;
    return p;
}

gg::EngineConfig ringConfig(double liveSeconds, bool spectral = false, uint32_t maxBlock = 4096)
{
    gg::EngineConfig config;
    config.maxBlockSize = maxBlock;
    config.liveSeconds = liveSeconds;
    config.spectral = spectral;
    config.seed = 7;
    return config;
}
} // namespace

TEST_CASE("scan zero draws exactly today's grains", "[engine][playhead]")
{
    // The at-rest rule (spec §4.7): speed 0, the default, is today's draw bit for bit, through
    // rebases at speed 0 (an edge change), a geometry change (a new source, a ring) and restarts.
    gg::CloudParams base{.densityHz = 211.0,
                         .distribution = 0.6,
                         .grainDurationSec = 0.03,
                         .position = 0.4,
                         .spray = 0.2,
                         .stereo = 0.5,
                         .pitchSpreadSemitones = 3.0,
                         .smearJitterSec = 0.01};

    SECTION("the scheduler")
    {
        for (const auto mode :
             {gg::SourceMode::Sample, gg::SourceMode::Live, gg::SourceMode::Spectral})
        {
            for (const double zero : {0.0, -0.0})
            {
                INFO("mode " << static_cast<int>(mode) << ", speed " << zero);
                gg::CloudScheduler today;
                today.setSourceMode(mode, 2.0);
                today.setParams(base); // never given the fields, never synced
                gg::CloudScheduler synced;
                synced.setSourceMode(mode, 2.0);
                gg::CloudParams p = base;
                p.scanSpeed = zero;
                synced.setParams(p);
                std::vector<gg::GrainDraw> want;
                std::vector<gg::GrainDraw> got;
                const uint8_t keys[] = {60, 64};
                for (int64_t t = 0; t < 96000; t += 256)
                {
                    if (t == 24064)
                    {
                        p.scanEdge = gg::ScanEdge::Bounce; // a rebase at speed 0
                        synced.setParams(p);
                    }
                    if (t == 48128)
                    {
                        p.scanEdge = gg::ScanEdge::Stop;
                        synced.setParams(p);
                        today.forceNextOnset();
                        synced.forceNextOnset(); // a restart
                    }
                    if (t == 72192)
                    {
                        today.setHeldKeys(keys);
                        synced.setHeldKeys(keys); // a restart
                    }
                    // A geometry that changes (a new source, then a ring's growing window).
                    const gg::ScanGeometry g =
                        t < 30000 ? gg::ScanGeometry{0.25, 0.0, 1.0}
                                  : gg::ScanGeometry{-1.0, 0.0, static_cast<double>(t) / 48000.0};
                    synced.syncPlayhead(t, g);
                    today.schedule(t, 256, 3, 1, 0,
                                   [&](const gg::GrainDraw &d) { want.push_back(d); });
                    synced.schedule(t, 256, 3, 1, 0,
                                    [&](const gg::GrainDraw &d) { got.push_back(d); });
                    REQUIRE(synced.scanOffsetAt(static_cast<double>(t) + 100.5) == 0.0);
                }
                REQUIRE(want.size() > 300);
                REQUIRE(got == want);
                REQUIRE(std::none_of(got.begin(), got.end(),
                                     [](const gg::GrainDraw &d) { return d.scanning; }));
            }
        }
    }

    SECTION("the engine")
    {
        constexpr int64_t kFrames = 96000;
        const auto input = noiseInput(kFrames, 11);
        const auto first = gg::PreparedSource::make(noiseBuffer(2, 3 * 48000, 5));
        const auto second = gg::PreparedSource::make(noiseBuffer(1, 48000 + 17, 6));
        for (const auto mode :
             {gg::SourceMode::Sample, gg::SourceMode::Live, gg::SourceMode::Spectral})
        {
            INFO("mode " << static_cast<int>(mode));
            const auto renderOne = [&](bool fields, GrainLog &log) {
                gg::Engine engine(ringConfig(2.0, true));
                REQUIRE(engine.adoptSource(first.get()));
                engine.setSourceMode(mode);
                gg::CloudParams p = base;
                if (fields)
                {
                    p.scanSpeed = 0.0;
                    p.scanEdge = gg::ScanEdge::Wrap;
                }
                engine.setCloudParams(p);
                engine.setEventSink(&GrainLog::record, &log);
                Stereo out = unwritten(kFrames);
                run(engine, out, 0, 24000, 333, &input);
                if (fields)
                {
                    p.scanEdge = gg::ScanEdge::Stop; // a rebase at speed 0
                    engine.setCloudParams(p);
                }
                run(engine, out, 24000, 48000, 333, &input);
                REQUIRE(engine.adoptSource(second.get())); // a geometry change
                run(engine, out, 48000, kFrames, 333, &input);
                while (engine.takeRetiredSource() != nullptr)
                {
                }
                return out;
            };
            GrainLog todayLog;
            GrainLog fieldsLog;
            const Stereo today = renderOne(false, todayLog);
            const Stereo fields = renderOne(true, fieldsLog);
            REQUIRE(allFinite(today.left));
            REQUIRE_FALSE(todayLog.overflowed);
            REQUIRE(todayLog.grains.size() > 300);
            REQUIRE(fieldsLog.grains == todayLog.grains);
            REQUIRE(sameBits(fields.left, today.left));
            REQUIRE(sameBits(fields.right, today.right));
        }
    }
}

TEST_CASE("a sample scan moves the grains' start at the speed", "[engine][playhead]")
{
    // Spec H1, HB2, HB3: at 1x over a source at the output rate, the point is
    // Position + onset / (F - 1) of the source.
    constexpr int64_t kF = 4 * 48000;
    gg::Engine engine(gg::EngineConfig{});
    engine.setSource(noiseBuffer(1, kF, 3));
    engine.setCloudParams(scanParams(0.25, 1.0));
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    Stereo out = unwritten(2 * 48000);
    run(engine, out, 0, 2 * 48000, 256);
    REQUIRE(allFinite(out.left));
    REQUIRE(log.grains.size() == 128);
    const double span = static_cast<double>(kF - 1);
    for (const gg::GrainSpec &g : log.grains)
    {
        INFO("grain " << g.index);
        const double expected = (0.25 + onsetOf(g) / span) * span;
        REQUIRE(std::abs(g.startPos - expected) < 1e-9);
    }
}

TEST_CASE("a scan from the sample's end wraps to its start", "[engine][playhead]")
{
    constexpr int64_t kF = 4 * 48000;
    gg::Engine engine(gg::EngineConfig{});
    engine.setSource(noiseBuffer(1, kF, 3));
    engine.setCloudParams(scanParams(1.0, 1.0, gg::ScanEdge::Wrap));
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    Stereo out = unwritten(4800);
    run(engine, out, 0, 4800, 256);
    REQUIRE(log.grains.size() >= 2);
    REQUIRE(log.grains[0].startPos == static_cast<double>(kF - 1));
    REQUIRE(log.grains[1].onsetSample == 750);
    REQUIRE(log.grains[1].startPos < 1000.0);
    REQUIRE(std::abs(log.grains[1].startPos - 750.0) < 1e-6);
}

TEST_CASE("a frozen ring scans backward", "[engine][playhead]")
{
    // Spec H1: on a frozen ring, speed -1 plays the material backward: the lag behind the
    // window's end grows by a second each second, folded into the window [0, W] (Wrap; the loop
    // makes W 1 s).
    constexpr int64_t kFreeze = 96000;
    constexpr int64_t kFrames = kFreeze + 72000;
    const auto input = noiseInput(kFrames, 4);
    gg::EngineConfig config = ringConfig(4.0);
    gg::Engine engine(config);
    engine.setSourceMode(gg::SourceMode::Live);
    engine.live().setLoop(true, 48000);
    gg::CloudParams p = scanParams(0.5, 0.0);
    p.grainDurationSec = 0.02;
    engine.setCloudParams(p);
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    Stereo out = unwritten(kFrames);
    run(engine, out, 0, kFreeze, 512, &input);
    engine.setFreeze(true);
    p.scanSpeed = -1.0;
    engine.setCloudParams(p);
    run(engine, out, kFreeze, kFrames, 512, &input);
    REQUIRE_FALSE(log.overflowed);
    const auto head = static_cast<double>(engine.liveWriteHead());
    const double r = ringRate(-1.0);
    int checked = 0;
    int wrapped = 0;
    for (const gg::GrainSpec &g : log.grains)
    {
        if (g.onsetSample < kFreeze + 1000)
        {
            continue; // before the freeze's ramp has ended, the head still moves
        }
        const double unfolded = 0.5 + r * (onsetOf(g) - static_cast<double>(kFreeze));
        const double expected = gg::scanFold(unfolded, 0.0, 1.0, gg::ScanEdge::Wrap);
        if (expected < 0.05 || expected > 0.95)
        {
            continue; // the admission clamp's margins at the window's ends
        }
        INFO("grain " << g.index << " at " << g.onsetSample);
        const double lag = (head - g.startPos) / kSampleRate;
        REQUIRE(std::abs(lag - expected) < 1e-9);
        ++checked;
        wrapped += unfolded > 1.0 ? 1 : 0;
    }
    REQUIRE(checked > 60);
    REQUIRE(wrapped > 10);
}

TEST_CASE("a moving ring never crosses the head", "[engine][playhead]")
{
    // Spec H1, §4.3: on a moving ring, speed +2 with Stop takes the lag from Position to 0 at
    // 2 s a second (toward the head), where it holds: the grains then start at the admission
    // clamp's newest bound, never past it.
    gg::Engine engine(ringConfig(4.0));
    engine.setSourceMode(gg::SourceMode::Live);
    engine.setCloudParams(scanParams(0.5, 2.0, gg::ScanEdge::Stop));
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    constexpr int64_t kFrames = 48000;
    const auto input = noiseInput(kFrames, 9);
    Stereo out = unwritten(kFrames);
    run(engine, out, 0, kFrames, 256, &input);
    REQUIRE(log.grains.size() == 64);
    const double r = ringRate(2.0);
    int atHead = 0;
    for (const gg::GrainSpec &g : log.grains)
    {
        INFO("grain " << g.index << " at " << g.onsetSample);
        // The head R(t0) = t0 + 1 while recording; the clamp's newest start for a grain at rate
        // 1 is rho - 3 - M (Engine::liveGrain).
        const double rho = onsetOf(g) + 1.0;
        const double hi = rho - 3.0 - static_cast<double>(gg::LiveSource::kHeadMargin);
        REQUIRE(g.startPos <= hi);
        const double lag = std::clamp(0.5 + r * onsetOf(g), 0.0, 4.0);
        if (lag * kSampleRate > rho - hi + 1.0)
        {
            REQUIRE(std::abs(g.startPos - (rho - lag * kSampleRate)) < 1e-6);
        }
        if (onsetOf(g) >= 12000.0)
        {
            REQUIRE(g.startPos == hi); // the lag held at 0
            ++atHead;
        }
    }
    REQUIRE(atHead == 48);
}

TEST_CASE("a growing splice window never rebases the playhead", "[engine][playhead]")
{
    // Spec HB1: the last splice, selected while the head records, grows sample by sample; the
    // window is never a rebase trigger, so the render is the same at any block size.
    constexpr int64_t kSplice = 24000;
    constexpr int64_t kFrames = 72000;
    const auto input = noiseInput(kFrames, 12);
    const auto renderAt = [&](uint32_t block, GrainLog &log) {
        gg::Engine engine(ringConfig(4.0));
        engine.setSourceMode(gg::SourceMode::Live);
        gg::CloudParams p = scanParams(0.1, -0.5, gg::ScanEdge::Wrap);
        p.spray = 0.05;
        p.distribution = 0.5;
        engine.setCloudParams(p);
        engine.setEventSink(&GrainLog::record, &log);
        Stereo out = unwritten(kFrames);
        run(engine, out, 0, kSplice, block, &input);
        REQUIRE(engine.live().addSplice(12000));
        engine.live().selectSplice(1);
        p.scanSpeed = -0.75; // a rebase at the splice's sample
        engine.setCloudParams(p);
        run(engine, out, kSplice, kFrames, block, &input);
        return out;
    };
    GrainLog refLog;
    const Stereo ref = renderAt(4096, refLog);
    REQUIRE(allFinite(ref.left));
    REQUIRE(refLog.grains.size() > 50);
    for (const uint32_t block : {1u, 7u, 32u, 256u, 333u})
    {
        INFO("block size " << block);
        GrainLog log;
        const Stereo r = renderAt(block, log);
        REQUIRE(log.grains == refLog.grains);
        REQUIRE(sameBits(r.left, ref.left));
        REQUIRE(sameBits(r.right, ref.right));
    }
}

TEST_CASE("a new sample keeps the playhead's fraction", "[engine][playhead]")
{
    // Spec HB8: a new source changes the geometry, so the next piece rebases: the point keeps its
    // fraction, and moves at the new length's rate.
    constexpr int64_t kF1 = 2 * 48000;
    constexpr int64_t kF2 = 2 * kF1;
    constexpr int64_t kSwap = 24000;
    const auto a = gg::PreparedSource::make(noiseBuffer(1, kF1, 1));
    const auto b = gg::PreparedSource::make(noiseBuffer(1, kF2, 2));
    gg::Engine engine(gg::EngineConfig{});
    REQUIRE(engine.adoptSource(a.get()));
    engine.setCloudParams(scanParams(0.1, 1.0));
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    Stereo out = unwritten(48000);
    run(engine, out, 0, kSwap, 256);
    REQUIRE(engine.adoptSource(b.get()));
    run(engine, out, kSwap, 48000, 256);
    REQUIRE(log.grains.size() == 64);
    const double r1 = sampleRate1x(1.0, kF1);
    const double r2 = sampleRate1x(1.0, kF2);
    const double atSwap = r1 * static_cast<double>(kSwap);
    for (const gg::GrainSpec &g : log.grains)
    {
        INFO("grain " << g.index << " at " << g.onsetSample);
        const bool after = g.onsetSample >= kSwap;
        const double span = static_cast<double>((after ? kF2 : kF1) - 1);
        const double expected =
            0.1 +
            (after ? atSwap + r2 * (onsetOf(g) - static_cast<double>(kSwap)) : r1 * onsetOf(g));
        REQUIRE(std::abs(g.startPos / span - expected) < 1e-12);
    }
    // Half the fraction per second after the swap: (F1 - 1) / (F2 - 1) of it.
    const auto &g0 = log.grains[40];
    const auto &g1 = log.grains[41];
    const double slope =
        (g1.startPos - g0.startPos) / static_cast<double>(kF2 - 1) / (onsetOf(g1) - onsetOf(g0));
    REQUIRE(std::abs(slope / r1 - 0.5) < 1e-4);
    while (engine.takeRetiredSource() != nullptr)
    {
    }
}

TEST_CASE("a one-frame sample holds the playhead", "[engine][playhead]")
{
    gg::Engine engine(gg::EngineConfig{});
    auto one = std::make_shared<gg::SampleBuffer>(1, 1, kSampleRate);
    one->channel(0)[0] = 0.5f;
    engine.setSource(one);
    engine.setCloudParams(scanParams(0.7, 1.0));
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    Stereo out = unwritten(24000);
    run(engine, out, 0, 24000, 256);
    REQUIRE(allFinite(out.left));
    REQUIRE(log.grains.size() == 32);
    for (const gg::GrainSpec &g : log.grains)
    {
        REQUIRE(g.startPos == 0.0);
    }
}

TEST_CASE("scan zero holds the point", "[engine][playhead]")
{
    // Spec HB10: speed returned to 0 holds the point where it stopped; a forced onset (the first
    // key after none) restarts the playhead, back to Position.
    constexpr int64_t kF = 4 * 48000;
    gg::Engine engine(gg::EngineConfig{});
    engine.setSource(noiseBuffer(1, kF, 3));
    gg::CloudParams p = scanParams(0.1, 1.0);
    engine.setCloudParams(p);
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    Stereo out = unwritten(96000);
    run(engine, out, 0, 48000, 256);
    p.scanSpeed = 0.0;
    engine.setCloudParams(p);
    run(engine, out, 48000, 72000, 256);
    const uint8_t keys[] = {60};
    engine.setHeldKeys(keys);
    run(engine, out, 72000, 96000, 256);
    const double span = static_cast<double>(kF - 1);
    const double held = 0.1 + sampleRate1x(1.0, kF) * 48000.0;
    int heldCount = 0;
    int restarted = 0;
    for (const gg::GrainSpec &g : log.grains)
    {
        if (g.onsetSample < 48000)
        {
            continue;
        }
        INFO("grain " << g.index << " at " << g.onsetSample);
        if (g.onsetSample < 72000)
        {
            REQUIRE(std::abs(g.startPos / span - held) < 1e-12);
            ++heldCount;
        }
        else
        {
            REQUIRE(g.startPos == 0.1 * span);
            ++restarted;
        }
    }
    REQUIRE(heldCount == 32);
    REQUIRE(restarted == 32);
}

TEST_CASE("each voice restarts its playhead", "[engine][playhead][voice]")
{
    // Spec H4: each voice's playhead restarts at its start, from Position.
    constexpr int64_t kF = 4 * 48000;
    gg::Engine engine(gg::EngineConfig{});
    engine.setSource(noiseBuffer(1, kF, 3));
    engine.setVoiceMode(true);
    gg::VoiceParams v;
    v.cloud = scanParams(0.1, 1.0);
    v.amp = {.at = 0, .level = 1.0, .stage = gg::AmpEnvelope::Stage::Sustain, .sustain = 1.0};
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    Stereo out = unwritten(96000);
    engine.startVoice(0, 0, v);
    run(engine, out, 0, 48000, 256);
    engine.startVoice(1, 1, v);
    run(engine, out, 48000, 96000, 256);
    const double span = static_cast<double>(kF - 1);
    const double r = sampleRate1x(1.0, kF);
    bool firstOf[2] = {true, true};
    int counts[2] = {0, 0};
    for (const gg::GrainSpec &g : log.grains)
    {
        REQUIRE(g.voice < 2);
        INFO("voice " << g.voice << ", grain " << g.index << " at " << g.onsetSample);
        const double start = g.voice == 0 ? 0.0 : 48000.0;
        if (firstOf[g.voice])
        {
            REQUIRE(onsetOf(g) == start);
            REQUIRE(g.startPos == 0.1 * span);
            firstOf[g.voice] = false;
        }
        REQUIRE(std::abs(g.startPos / span - (0.1 + r * (onsetOf(g) - start))) < 1e-12);
        ++counts[g.voice];
    }
    REQUIRE(counts[0] == 128);
    REQUIRE(counts[1] == 64);
}

TEST_CASE("a source-mode switch restarts the playhead", "[engine][playhead]")
{
    // Spec HB9: an offset in a fraction of a file and one in seconds of lag don't correspond,
    // so a switch starts reading at Position.
    constexpr int64_t kF = 4 * 48000;
    gg::Engine engine(ringConfig(4.0));
    engine.setSource(noiseBuffer(1, kF, 3));
    engine.setCloudParams(scanParams(0.25, 1.0));
    GrainLog log;
    engine.setEventSink(&GrainLog::record, &log);
    Stereo out = unwritten(60000);
    run(engine, out, 0, 48000, 256);
    engine.setSourceMode(gg::SourceMode::Live);
    run(engine, out, 48000, 60000, 256);
    const double r = ringRate(1.0);
    int live = 0;
    for (const gg::GrainSpec &g : log.grains)
    {
        if (g.sourceSlot != gg::Engine::kLiveSlot)
        {
            REQUIRE(g.onsetSample < 48000);
            continue;
        }
        INFO("grain " << g.index << " at " << g.onsetSample);
        const double rho = onsetOf(g) + 1.0; // the head while recording
        const double lag = (rho - g.startPos) / kSampleRate;
        if (live == 0)
        {
            REQUIRE(g.onsetSample == 48000);
            REQUIRE(lag == 0.25);
        }
        if (g.onsetSample < 58000)
        {
            REQUIRE(std::abs(lag - (0.25 + r * (onsetOf(g) - 48000.0))) < 1e-9);
        }
        ++live;
    }
    REQUIRE(live == 16);
}

namespace
{
// The smear the engine should write, driven beside it in calls of one hop (so every hop falls on
// a call's first frame), at the closed-form point of each hop: Position while x(t) is 0, else
// scanFold(Position + x(t), 0, hi(t), edge).
struct SmearReference
{
    double position;
    double rate;
    gg::ScanEdge edge;
    int64_t tau0{0};
};

double smearPoint(const SmearReference &s, int64_t t, double hi)
{
    const double x = 0.0 + s.rate * (static_cast<double>(t) - static_cast<double>(s.tau0));
    return x == 0.0 ? s.position : gg::scanFold(s.position + x, 0.0, hi, s.edge);
}

bool sameRing(const gg::LiveSource &a, const gg::LiveSource &b)
{
    const gg::LiveSource::RingView x = a.view();
    const gg::LiveSource::RingView y = b.view();
    const auto floats = static_cast<std::size_t>(2 * x.capacity);
    return x.capacity == y.capacity && a.head() == b.head() &&
           std::memcmp(x.slots, y.slots, floats * sizeof(float)) == 0;
}

bool anyNonZero(const gg::LiveSource &ring)
{
    const gg::LiveSource::RingView v = ring.view();
    const auto floats = static_cast<std::size_t>(2 * v.capacity);
    return std::any_of(v.slots, v.slots + floats, [](float x) { return x != 0.0f; });
}

// Spectral grains: Position in the material's units.
gg::CloudParams smearParams(double position, double speed)
{
    gg::CloudParams p{.densityHz = 50.0,
                      .grainDurationSec = 0.05,
                      .position = position,
                      .spray = 0.05,
                      .stereo = 0.5,
                      .gain = 0.5f};
    p.scanSpeed = speed;
    return p;
}
} // namespace

TEST_CASE("the smear follows the layer playhead", "[engine][playhead][spectral]")
{
    // Spec H9, HB5: the smear analyses the material at the layer playhead's point, evaluated at
    // each hop's own sample. Checked against a SpectralSource driven at the closed-form point.
    constexpr uint32_t kHop = 1024;
    constexpr int64_t kFrames = 72 * kHop;
    gg::EngineConfig config = ringConfig(1.0, true, kHop);
    config.layer = 2;

    SECTION("over a sample, wrapping")
    {
        constexpr int64_t kF = 2 * 48000;
        const auto buf = noiseBuffer(2, kF, 8);
        gg::Engine engine(config);
        engine.setSource(buf);
        engine.setSourceMode(gg::SourceMode::Spectral);
        engine.setCloudParams(smearParams(0.9, 1.0));
        REQUIRE(engine.spectral()->hop() == kHop);
        gg::SpectralSource smear(kSampleRate, kHop);
        smear.setActive(true);
        gg::SpectralSource still(kSampleRate, kHop); // Position alone, for a non-vacuous check
        still.setActive(true);
        gg::SpectralMaterial m;
        m.sample[0] = buf->channel(0);
        m.sample[1] = buf->channel(1);
        m.sampleFrames = kF;
        m.sampleMono = false;
        m.sampleRate = kSampleRate;
        const SmearReference ref{0.9, sampleRate1x(1.0, kF), gg::ScanEdge::Wrap};
        Stereo out = unwritten(kFrames);
        int wrapped = 0;
        for (int64_t t = 0; t < kFrames; t += kHop)
        {
            run(engine, out, t, t + kHop, kHop);
            const double point = smearPoint(ref, t, 1.0);
            wrapped += point < 0.5 ? 1 : 0;
            smear.process(m, point, config.seed, config.layer, t, kHop);
            still.process(m, 0.9, config.seed, config.layer, t, kHop);
        }
        REQUIRE(wrapped > 10);
        REQUIRE(anyNonZero(smear.ring()));
        REQUIRE(sameRing(engine.spectral()->ring(), smear.ring()));
        REQUIRE_FALSE(sameRing(engine.spectral()->ring(), still.ring()));
    }

    SECTION("over the ring, backward")
    {
        const auto input = noiseInput(kFrames, 14);
        gg::Engine engine(config);
        engine.setSourceMode(gg::SourceMode::Spectral);
        engine.setCloudParams(smearParams(0.3, -1.0));
        gg::LiveSource ring;
        ring.prepare(kSampleRate, config.liveSeconds, kHop);
        gg::SpectralSource smear(kSampleRate, kHop);
        smear.setActive(true);
        gg::SpectralMaterial m;
        m.ring = &ring;
        const SmearReference ref{0.3, ringRate(-1.0), gg::ScanEdge::Wrap};
        Stereo out = unwritten(kFrames);
        for (int64_t t = 0; t < kFrames; t += kHop)
        {
            run(engine, out, t, t + kHop, kHop, &input);
            const float *in[2] = {input.left.data() + t, input.right.data() + t};
            ring.write(in, 2, 0, t, kHop);
            const double hi = static_cast<double>(ring.windowFramesAt(t)) / kSampleRate;
            smear.process(m, smearPoint(ref, t, hi), config.seed, config.layer, t, kHop);
        }
        REQUIRE(anyNonZero(smear.ring()));
        REQUIRE(sameRing(engine.spectral()->ring(), smear.ring()));
    }
}

TEST_CASE("a voice start restarts the smear's point", "[engine][playhead][spectral][voice]")
{
    // Spec H9, HB5: in Voice mode the layer playhead restarts at each voice's start, so the
    // smear's analysis point returns to Position there.
    constexpr uint32_t kHop = 1024;
    constexpr int64_t kSecond = 47 * kHop;
    constexpr int64_t kFrames = 80 * kHop;
    constexpr int64_t kF = 4 * 48000;
    gg::EngineConfig config = ringConfig(1.0, true, kHop);
    const auto buf = noiseBuffer(2, kF, 10);
    gg::Engine engine(config);
    engine.setSource(buf);
    engine.setSourceMode(gg::SourceMode::Spectral);
    engine.setCloudParams(smearParams(0.2, 1.0));
    engine.setVoiceMode(true);
    gg::VoiceParams v;
    v.cloud = smearParams(0.2, 1.0);
    v.amp = {.at = 0, .level = 1.0, .stage = gg::AmpEnvelope::Stage::Sustain, .sustain = 1.0};
    gg::SpectralSource smear(kSampleRate, kHop);
    smear.setActive(true);
    gg::SpectralSource unrestarted(kSampleRate, kHop); // never restarted
    unrestarted.setActive(true);
    gg::SpectralMaterial m;
    m.sample[0] = buf->channel(0);
    m.sample[1] = buf->channel(1);
    m.sampleFrames = kF;
    m.sampleMono = false;
    SmearReference ref{0.2, sampleRate1x(1.0, kF), gg::ScanEdge::Wrap};
    const SmearReference freeRef = ref;
    Stereo out = unwritten(kFrames);
    for (int64_t t = 0; t < kFrames; t += kHop)
    {
        if (t == 0 || t == kSecond)
        {
            engine.startVoice(t == 0 ? 0u : 1u, t == 0 ? 0u : 1u, v);
            ref.tau0 = t;
        }
        run(engine, out, t, t + kHop, kHop);
        smear.process(m, smearPoint(ref, t, 1.0), config.seed, config.layer, t, kHop);
        unrestarted.process(m, smearPoint(freeRef, t, 1.0), config.seed, config.layer, t, kHop);
    }
    REQUIRE(anyNonZero(smear.ring()));
    REQUIRE(sameRing(engine.spectral()->ring(), smear.ring()));
    REQUIRE_FALSE(sameRing(engine.spectral()->ring(), unrestarted.ring()));
}

TEST_CASE("an edge change at rest never moves the playhead", "[engine][playhead]")
{
    // The controller's ruling on the at-rest rule: canon of an offset that is exactly 0 is 0 at
    // every edge, so a playhead that hasn't moved stays at Position whatever the window, even
    // under Stop with Position outside it.
    gg::Playhead p;
    p.sync(0, 0.0, gg::ScanEdge::Wrap, geometry(1.0, 0.0, 1.0), kSampleRate, 2.0);
    p.sync(256, 0.0, gg::ScanEdge::Stop, geometry(1.0, 0.0, 1.0), kSampleRate, 2.0);
    CHECK(p.edge() == gg::ScanEdge::Stop);
    CHECK_FALSE(p.moving());
    CHECK(bits(p.offsetAt(256.0)) == bits(0.0));
    CHECK(bits(p.offsetAt(100000.5)) == bits(0.0));
    p.sync(512, 0.0, gg::ScanEdge::Bounce, geometry(-1.0, 0.0, 0.5), kSampleRate, 2.0);
    CHECK_FALSE(p.moving());
    CHECK(bits(p.offsetAt(1000.0)) == bits(0.0));
}

TEST_CASE("scan zero with stop reads position outside a short window", "[engine][playhead]")
{
    // Live, Position 1 s behind the head with the loop narrowing the window to 0.5 s: at speed 0
    // a switch from Wrap to Stop must leave every grain and every sample as they were, also once
    // the window widens past Position.
    constexpr int64_t kFrames = 72000;
    const auto input = noiseInput(kFrames, 21);
    const auto renderOne = [&](bool change, GrainLog &log) {
        gg::Engine engine(ringConfig(4.0));
        engine.setSourceMode(gg::SourceMode::Live);
        engine.live().setLoop(true, 24000);
        gg::CloudParams p = scanParams(1.0, 0.0, gg::ScanEdge::Wrap);
        p.spray = 0.1;
        engine.setCloudParams(p);
        engine.setEventSink(&GrainLog::record, &log);
        Stereo out = unwritten(kFrames);
        run(engine, out, 0, 30000, 256, &input);
        if (change)
        {
            p.scanEdge = gg::ScanEdge::Stop;
            engine.setCloudParams(p);
        }
        run(engine, out, 30000, 48000, 256, &input);
        // The loop then widens to 2 s: Position is inside the window again, so a Stop canon
        // taken at the edge change (which would hold the point at the old 0.5 s edge) is heard.
        engine.live().setLoop(true, 96000);
        run(engine, out, 48000, kFrames, 256, &input);
        REQUIRE(bits(engine.cloudParams().position) == bits(1.0));
        return out;
    };
    GrainLog stillLog;
    GrainLog changedLog;
    const Stereo still = renderOne(false, stillLog);
    const Stereo changed = renderOne(true, changedLog);
    REQUIRE(allFinite(still.left));
    REQUIRE(stillLog.grains.size() == 96);
    REQUIRE(changedLog.grains == stillLog.grains);
    REQUIRE(sameBits(changed.left, still.left));
    REQUIRE(sameBits(changed.right, still.right));
}
