// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The publish structures (spec §6.4): the grain event queue, the latest-wins snapshot buffer and
// the peak strip of the live ring, which the audio thread fills for the editor without
// allocating, locking or waiting; and the Processor filling them while publishing is on.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gg/core/GrainQueue.h"
#include "gg/core/LatestValue.h"
#include "gg/core/LivePeaks.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Processor.h"
#include "gg/core/Rng.h"
#include "gg/core/UiSnapshot.h"
#include "gg/source/LiveSource.h"
#include "support/AllocTripwire.h"

using gg::core::GrainQueue;
using gg::core::GrainVis;
using gg::core::LatestValue;
using gg::core::LivePeaks;
using gg::core::Mode;
using gg::core::ParamEvent;
using gg::core::ParamSlot;
using gg::core::Processor;
using gg::core::SnapshotBuffer;
using gg::core::SpscQueue;
using gg::core::UiSnapshot;
using gg::test::AllocTripwire;

namespace
{
GrainVis grain(int64_t onset)
{
    GrainVis g;
    g.onsetSample = onset;
    g.startPos = static_cast<double>(onset) + 0.25;
    g.rate = 1.5f;
    g.durationSamples = 4800.0f;
    g.gain = 0.5f;
    g.pan = -0.25f;
    return g;
}

bool sameGrain(const GrainVis &a, const GrainVis &b)
{
    return a.onsetSample == b.onsetSample && a.startPos == b.startPos && a.rate == b.rate &&
           a.durationSamples == b.durationSamples && a.gain == b.gain && a.pan == b.pan &&
           a.layer == b.layer;
}

// The peak a bucket should hold: max(|l|, |r|) over frames [from, to) of the test signal.
float expectedPeak(const std::vector<float> &l, const std::vector<float> &r, size_t from, size_t to)
{
    float peak = 0.0f;
    for (size_t i = from; i < to && i < l.size(); ++i)
    {
        peak = std::max({peak, std::fabs(l[i]), std::fabs(r[i])});
    }
    return peak;
}
} // namespace

TEST_CASE("the grain queue is FIFO", "[core][publish]")
{
    auto queue = std::make_unique<GrainQueue>();
    GrainVis out;
    CHECK_FALSE(queue->pop(out));

    for (int64_t i = 0; i < 10; ++i)
    {
        REQUIRE(queue->push(grain(i)));
    }
    for (int64_t i = 0; i < 4; ++i)
    {
        REQUIRE(queue->pop(out));
        CHECK(sameGrain(out, grain(i)));
    }
    // Interleaved pushes join the end; the index wraps the storage many times over.
    for (int64_t i = 10; i < 3 * GrainQueue::kCapacity; ++i)
    {
        REQUIRE(queue->push(grain(i)));
        REQUIRE(queue->pop(out));
        CHECK(out.onsetSample == i - 6);
    }
    for (int64_t i = 3 * GrainQueue::kCapacity - 6; i < 3 * GrainQueue::kCapacity; ++i)
    {
        REQUIRE(queue->pop(out));
        CHECK(sameGrain(out, grain(i)));
    }
    CHECK_FALSE(queue->pop(out));
    CHECK(queue->dropped() == 0);
}

TEST_CASE("the grain queue drops when full and counts it", "[core][publish]")
{
    STATIC_REQUIRE(GrainQueue::kCapacity == 4096);
    auto queue = std::make_unique<GrainQueue>();
    for (int64_t i = 0; i < GrainQueue::kCapacity; ++i)
    {
        REQUIRE(queue->push(grain(i)));
    }
    CHECK(queue->dropped() == 0);
    CHECK_FALSE(queue->push(grain(GrainQueue::kCapacity)));
    CHECK(queue->dropped() == 1);

    GrainVis out;
    for (int64_t i = 0; i < GrainQueue::kCapacity; ++i)
    {
        REQUIRE(queue->pop(out));
        CHECK(sameGrain(out, grain(i)));
    }
    CHECK_FALSE(queue->pop(out)); // the dropped one never entered

    // Room again; the count of drops stays.
    CHECK(queue->push(grain(7)));
    CHECK(queue->dropped() == 1);
}

TEST_CASE("the grain queue clear empties it from the consumer side", "[core][publish]")
{
    auto queue = std::make_unique<GrainQueue>();
    for (int64_t i = 0; i < 100; ++i)
    {
        REQUIRE(queue->push(grain(i)));
    }
    queue->clear();
    GrainVis out;
    CHECK_FALSE(queue->pop(out));
    REQUIRE(queue->push(grain(500)));
    REQUIRE(queue->pop(out));
    CHECK(out.onsetSample == 500);
    CHECK(queue->dropped() == 0); // clearing isn't dropping
}

TEST_CASE("the spsc queue takes any trivially copyable type and capacity", "[core][publish]")
{
    struct Change
    {
        uint32_t id;
        double value;
        uint8_t kind;
    };
    SpscQueue<Change, 4> queue;
    STATIC_REQUIRE(SpscQueue<Change, 4>::kCapacity == 4);
    for (uint32_t i = 0; i < 4; ++i)
    {
        REQUIRE(queue.push({i, 0.5 * i, 1}));
    }
    CHECK_FALSE(queue.push({9, 9.0, 9}));
    CHECK_FALSE(queue.push({9, 9.0, 9}));
    CHECK(queue.dropped() == 2);
    Change out{};
    for (uint32_t i = 0; i < 4; ++i)
    {
        REQUIRE(queue.pop(out));
        CHECK(out.id == i);
        CHECK(out.value == 0.5 * i);
    }
    CHECK_FALSE(queue.pop(out));
}

TEST_CASE("the grain queue hands grains across threads in order", "[core][publish]")
{
    // The producer outruns the consumer at times, so some pushes drop; every grain popped is
    // whole, in order, and popped + dropped == pushed.
    constexpr int64_t kPushes = 200000;
    auto queue = std::make_unique<GrainQueue>();
    std::atomic<bool> done{false};
    int64_t popped = 0;
    int64_t last = -1;
    bool torn = false;
    bool unordered = false;
    std::thread consumer([&] {
        GrainVis out;
        for (;;)
        {
            const bool finished = done.load(std::memory_order_acquire);
            while (queue->pop(out))
            {
                torn = torn || !sameGrain(out, grain(out.onsetSample));
                unordered = unordered || out.onsetSample <= last;
                last = out.onsetSample;
                ++popped;
            }
            if (finished)
            {
                break;
            }
        }
    });
    uint64_t accepted = 0;
    for (int64_t i = 0; i < kPushes; ++i)
    {
        accepted += queue->push(grain(i)) ? 1 : 0;
    }
    done.store(true, std::memory_order_release);
    consumer.join();
    CHECK_FALSE(torn);
    CHECK_FALSE(unordered);
    CHECK(static_cast<uint64_t>(popped) == accepted);
    CHECK(accepted + queue->dropped() == static_cast<uint64_t>(kPushes));
}

TEST_CASE("the grain queue is allocation-free", "[core][publish][rt]")
{
    auto queue = std::make_unique<GrainQueue>();
    GrainVis out;
    bool pushedAll = true;
    bool poppedAll = true;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int64_t i = 0; i < GrainQueue::kCapacity + 10; ++i)
        {
            pushedAll = queue->push(grain(i)) && pushedAll;
        }
        for (int64_t i = 0; i < GrainQueue::kCapacity; ++i)
        {
            poppedAll = queue->pop(out) && poppedAll;
        }
        queue->clear();
    }
    const uint64_t allocations = AllocTripwire::count();
    REQUIRE_NO_ALLOCATIONS(allocations);
    CHECK_FALSE(pushedAll); // the last ten dropped
    CHECK(poppedAll);
    CHECK(queue->dropped() == 10);
}

TEST_CASE("latest value hands over the newest snapshot", "[core][publish]")
{
    using Snapshot = std::array<uint64_t, 4>;
    LatestValue<Snapshot> latest;
    Snapshot got{};

    SECTION("one thread")
    {
        CHECK_FALSE(latest.take(got));
        latest.push({1, 1, 1, 1});
        latest.push({2, 2, 2, 2});
        REQUIRE(latest.take(got));
        CHECK(got == Snapshot{2, 2, 2, 2});
        CHECK_FALSE(latest.take(got));
        latest.push({3, 3, 3, 3});
        REQUIRE(latest.take(got));
        CHECK(got == Snapshot{3, 3, 3, 3});
        latest.push({4, 4, 4, 4});
        latest.push({5, 5, 5, 5});
        latest.push({6, 6, 6, 6});
        REQUIRE(latest.take(got));
        CHECK(got == Snapshot{6, 6, 6, 6});
        CHECK_FALSE(latest.take(got));
    }

    SECTION("two threads: never torn, never older, and the last is never lost")
    {
        constexpr uint64_t kPushes = 200000;
        std::atomic<bool> done{false};
        bool torn = false;
        bool older = false;
        uint64_t last = 0;
        std::thread consumer([&] {
            for (;;)
            {
                const bool finished = done.load(std::memory_order_acquire);
                if (latest.take(got))
                {
                    torn = torn || got[0] != got[1] || got[0] != got[2] || got[0] != got[3];
                    older = older || got[0] <= last;
                    last = got[0];
                }
                if (finished)
                {
                    break;
                }
            }
        });
        for (uint64_t i = 1; i <= kPushes; ++i)
        {
            latest.push({i, i, i, i});
        }
        done.store(true, std::memory_order_release);
        consumer.join();
        CHECK_FALSE(torn);
        CHECK_FALSE(older);
        CHECK(last == kPushes);
    }
}

TEST_CASE("the snapshot buffer hands over a ui snapshot without allocating", "[core][publish][rt]")
{
    SnapshotBuffer buffer;
    UiSnapshot pushed;
    pushed.clock = 123456789012LL;
    pushed.writeHead = 987654321098LL;
    pushed.frozen = true;
    pushed.activeGrains = 42;
    pushed.basePosition = 0.25f;
    pushed.baseSpray = 0.5f;
    pushed.effectivePosition = 0.75f;
    pushed.effectiveSpray = 0.125f;
    pushed.feedback = 0.9f;
    UiSnapshot got;
    bool took = false;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        buffer.push(pushed);
        took = buffer.take(got);
    }
    const uint64_t allocations = AllocTripwire::count();
    REQUIRE_NO_ALLOCATIONS(allocations);
    REQUIRE(took);
    CHECK(got.clock == pushed.clock);
    CHECK(got.writeHead == pushed.writeHead);
    CHECK(got.frozen);
    CHECK(got.activeGrains == 42);
    CHECK(got.basePosition == 0.25f);
    CHECK(got.baseSpray == 0.5f);
    CHECK(got.effectivePosition == 0.75f);
    CHECK(got.effectiveSpray == 0.125f);
    CHECK(got.feedback == 0.9f);
}

TEST_CASE("live peaks bucket by absolute frame", "[core][publish]")
{
    // 200 frames of a quiet signal, with a peak of 0.7 (in R, negative) at frame 70.
    std::vector<float> l(200);
    std::vector<float> r(200);
    for (size_t i = 0; i < l.size(); ++i)
    {
        l[i] = 0.1f * static_cast<float>((i * 37) % 11) / 11.0f;
        r[i] = -0.05f * static_cast<float>((i * 53) % 13) / 13.0f;
    }
    l[10] = 0.3f; // the loudest of the first bucket
    r[70] = -0.7f;
    l[150] = -0.4f;

    LivePeaks peaks;
    peaks.prepare(4800);
    CHECK(peaks.frames() == 4800);
    CHECK(peaks.buckets() == 4800 / LivePeaks::kBucket + 1);

    SECTION("one write")
    {
        peaks.write(l.data(), r.data(), 0, 200);
        for (int64_t f = 0; f < 64; ++f)
        {
            CHECK(peaks.at(f) == 0.3f);
        }
        CHECK(peaks.at(0) == expectedPeak(l, r, 0, 64));
        for (int64_t f = 64; f < 128; ++f)
        {
            CHECK(peaks.at(f) == 0.7f);
        }
        CHECK(peaks.at(128) == 0.4f);
        CHECK(peaks.at(191) == 0.4f);
        CHECK(peaks.at(192) == expectedPeak(l, r, 192, 200)); // the partial last bucket
        CHECK(peaks.at(199) == peaks.at(192));
        CHECK(peaks.at(-1) == 0.0f); // before the ring started: silence
    }

    SECTION("a write split across a bucket boundary gives the same buckets")
    {
        LivePeaks whole;
        whole.prepare(4800);
        whole.write(l.data(), r.data(), 0, 200);

        // Two calls, the cut inside the bucket that holds the peak.
        peaks.write(l.data(), r.data(), 0, 100);
        peaks.write(l.data() + 100, r.data() + 100, 100, 100);
        for (int64_t f = 0; f < 200; ++f)
        {
            CHECK(peaks.at(f) == whole.at(f));
        }

        // One frame at a time, and blocks of 0, from a fresh start.
        LivePeaks single;
        single.prepare(4800);
        for (int64_t f = 0; f < 200; ++f)
        {
            single.write(l.data() + f, r.data() + f, f, 0);
            single.write(l.data() + f, r.data() + f, f, 1);
        }
        for (int64_t f = 0; f < 200; ++f)
        {
            CHECK(single.at(f) == whole.at(f));
        }
    }

    SECTION("non-finite input counts as 0")
    {
        std::vector<float> bad = l;
        bad[10] = std::numeric_limits<float>::quiet_NaN();
        bad[11] = std::numeric_limits<float>::infinity();
        bad[12] = -std::numeric_limits<float>::infinity();
        std::vector<float> cleaned = l;
        cleaned[10] = 0.0f;
        cleaned[11] = 0.0f;
        cleaned[12] = 0.0f;
        peaks.write(bad.data(), r.data(), 0, 200);
        CHECK(peaks.at(0) == expectedPeak(cleaned, r, 0, 64));
        CHECK(std::isfinite(peaks.at(0)));
    }
}

TEST_CASE("live peaks wrap the ring", "[core][publish]")
{
    // A ring of 256 frames: 4 buckets, plus one, so the last frames() frames always fit.
    constexpr int64_t kRing = 256;
    LivePeaks peaks;
    peaks.prepare(kRing);
    REQUIRE(peaks.frames() == kRing);
    REQUIRE(peaks.buckets() == 5);
    const int64_t span = static_cast<int64_t>(peaks.buckets()) * LivePeaks::kBucket; // 320

    // Bucket k's frames all hold (k + 1) / 100 in L; R is silent. The first pass is loud, so a
    // bucket that kept its old peak instead of starting again would show it.
    constexpr int64_t kFrames = 3 * 320 + 100;
    std::vector<float> l(kFrames);
    std::vector<float> r(kFrames, 0.0f);
    for (int64_t f = 0; f < kFrames; ++f)
    {
        const int64_t k = f / LivePeaks::kBucket;
        l[static_cast<size_t>(f)] = f < span ? 0.9f : static_cast<float>(k % 50 + 1) / 100.0f;
    }

    // Written in blocks of 48, which straddle the bucket boundaries.
    for (int64_t t = 0; t < kFrames; t += 48)
    {
        const auto n = static_cast<uint32_t>(std::min<int64_t>(48, kFrames - t));
        peaks.write(l.data() + t, r.data() + t, t, n);
    }

    // Every frame of the last frames() reads its own bucket's peak.
    for (int64_t f = kFrames - kRing; f < kFrames; ++f)
    {
        const int64_t k = f / LivePeaks::kBucket;
        CHECK(peaks.at(f) == static_cast<float>(k % 50 + 1) / 100.0f);
    }
    // Storage is reused every buckets() × kBucket frames: an old frame reads the newer bucket
    // that took its slot, which started again rather than keep the first pass's 0.9.
    CHECK(peaks.at(0) == peaks.at(3 * span));
    CHECK(peaks.at(0) == 0.16f);                  // bucket 15's
    CHECK(peaks.at(kFrames - 1 - span) == 0.17f); // bucket 11's slot, now bucket 16's
}

TEST_CASE("live peaks write and read without allocating", "[core][publish][rt]")
{
    std::vector<float> l(1000, 0.25f);
    std::vector<float> r(1000, -0.5f);
    LivePeaks peaks;
    peaks.prepare(16 * 48000); // the FX's ring at 48 kHz
    float sum = 0.0f;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int64_t t = 0; t < 100000; t += 1000)
        {
            peaks.write(l.data(), r.data(), t, 1000);
            peaks.write(l.data(), r.data(), t + 1000, 0);
            sum += peaks.at(t);
        }
    }
    const uint64_t allocations = AllocTripwire::count();
    REQUIRE_NO_ALLOCATIONS(allocations);
    CHECK(sum == 100 * 0.5f);
}

TEST_CASE("live peaks reset silences every bucket and keeps the size", "[core][publish]")
{
    LivePeaks peaks;
    peaks.reset(); // before prepare(): nothing to silence
    CHECK(peaks.buckets() == 0);
    peaks.prepare(1000);
    const uint32_t buckets = peaks.buckets();
    const std::vector<float> loud(1000, 0.5f);
    peaks.write(loud.data(), loud.data(), 0, 1000);
    peaks.write(loud.data(), loud.data(), 1000, 40); // ends inside bucket 16
    REQUIRE(peaks.at(1039) == 0.5f);
    peaks.reset();
    CHECK(peaks.buckets() == buckets);
    CHECK(peaks.frames() == 1000);
    for (int64_t f = 0; f < 1100; ++f)
    {
        REQUIRE(peaks.at(f) == 0.0f);
    }
    // Ring time restarts at 0: the next write fills its buckets afresh.
    const std::vector<float> soft(64, 0.25f);
    peaks.write(soft.data(), soft.data(), 0, 10);
    CHECK(peaks.at(0) == 0.25f);
    CHECK(peaks.at(64) == 0.0f);
}

TEST_CASE("live peaks unprepared are silent and ignore writes", "[core][publish]")
{
    LivePeaks peaks;
    CHECK(peaks.frames() == 0);
    CHECK(peaks.buckets() == 0);
    const std::array<float, 4> l{1.0f, 1.0f, 1.0f, 1.0f};
    peaks.write(l.data(), l.data(), 0, 4);
    CHECK(peaks.at(0) == 0.0f);
}

// ---- The Processor publishes (spec §6.4) ----

namespace
{
constexpr double kRate = 48000.0;

// Records the spawned grains into preallocated storage: the sink runs inside process().
struct SpecLog
{
    static constexpr std::size_t kCapacity = 16384;
    std::vector<gg::GrainSpec> grains;

    SpecLog() { grains.reserve(kCapacity); }

    static void record(void *ctx, uint32_t, const gg::GrainSpec &g)
    {
        auto *log = static_cast<SpecLog *>(ctx);
        if (log->grains.size() < kCapacity)
        {
            log->grains.push_back(g);
        }
    }
};

struct Channels
{
    std::vector<float> left;
    std::vector<float> right;
};

// Stereo noise in ±amp from gg::rng, the two channels independent.
Channels noiseInput(int64_t frames, float amp, uint64_t seed)
{
    Channels s{std::vector<float>(static_cast<std::size_t>(frames)),
               std::vector<float>(static_cast<std::size_t>(frames))};
    for (int64_t i = 0; i < frames; ++i)
    {
        using gg::rng::Field;
        const auto k = static_cast<uint64_t>(i);
        const auto j = static_cast<std::size_t>(i);
        s.left[j] = amp * static_cast<float>(
                              gg::rng::bipolar(gg::rng::hash({seed, 0, 0, Field::SignalNoise, k})));
        s.right[j] = amp * static_cast<float>(gg::rng::bipolar(
                               gg::rng::hash({seed, 0, 1, Field::SignalNoise, k})));
    }
    return s;
}

bool sameSamples(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

// Runs `input` through p in `block`-frame calls, from frame `from` to `to`; returns the output.
Channels run(Processor &p, const Channels &input, size_t from, size_t to, uint32_t block)
{
    Channels out{std::vector<float>(input.left.size()), std::vector<float>(input.left.size())};
    for (size_t i = from; i < to; i += block)
    {
        const auto n = static_cast<uint32_t>(std::min<size_t>(block, to - i));
        const float *in[2] = {input.left.data() + i, input.right.data() + i};
        float *o[2] = {out.left.data() + i, out.right.data() + i};
        p.process(in, 2, o, 2, n, {});
    }
    return out;
}

GrainVis visOf(const gg::GrainSpec &g)
{
    return {g.onsetSample,
            g.startPos,
            static_cast<float>(g.rate),
            static_cast<float>(g.durationSamples),
            g.gain,
            g.pan};
}
} // namespace

TEST_CASE("publishing off costs nothing", "[core][publish]")
{
    // Publishing is off by default: the external sink still sees every grain, but nothing is
    // queued, no snapshot is published, the clock stays at 0 and the peaks stay silent. And the
    // audio is bit for bit the same with publishing on (spec §8: no cost while the editor is
    // closed, and publishing never touches the audio).
    constexpr int64_t kFrames = 48000;
    const Channels input = noiseInput(kFrames, 0.5f, 7);
    Processor off({kRate, 512, 3, Mode::Fx});
    Processor on({kRate, 512, 3, Mode::Fx});
    on.setPublishing(true);
    SpecLog offLog;
    SpecLog onLog;
    off.setEventSink(&SpecLog::record, &offLog);
    on.setEventSink(&SpecLog::record, &onLog);
    for (Processor *p : {&off, &on})
    {
        p->setBase(ParamSlot::Density, 6.0f);
        p->setBase(ParamSlot::Feedback, 0.5f); // the ring input is built: the peaks would see it
    }
    const Channels a = run(off, input, 0, kFrames, 512);
    const Channels b = run(on, input, 0, kFrames, 512);

    REQUIRE(offLog.grains.size() > 10);
    CHECK(offLog.grains.size() == onLog.grains.size());
    GrainVis g;
    CHECK_FALSE(off.grainQueue().pop(g));
    UiSnapshot snap;
    CHECK_FALSE(off.snapshot().take(snap));
    CHECK(off.publishedClock().load() == 0);
    CHECK(off.livePeaks().frames() == static_cast<int64_t>(std::ceil(16.0 * kRate)));

    CHECK(on.grainQueue().pop(g));
    CHECK(on.snapshot().take(snap));
    CHECK(sameSamples(a.left, b.left));
    CHECK(sameSamples(a.right, b.right));
}

TEST_CASE("the snapshot carries effective position and spray", "[core][publish]")
{
    Processor p({kRate, 512, 3, Mode::Fx});
    p.setPublishing(true);
    p.setBase(ParamSlot::Macro4, 1.0f); // Macro Spray: Spray + 0.5 (spec §3.2)
    p.setBase(ParamSlot::Position, 0.75f);
    const Channels input = noiseInput(4000, 0.5f, 9);
    run(p, input, 0, 1000, 333);

    UiSnapshot s;
    REQUIRE(p.snapshot().take(s));
    CHECK(s.clock == 1000);
    CHECK(p.publishedClock().load() == 1000);
    CHECK(s.writeHead == p.engine().liveWriteHead());
    CHECK(s.writeHead == 1000);
    CHECK_FALSE(s.frozen);
    CHECK(s.activeGrains == p.engine().activeGrains());
    CHECK(s.activeGrains > 0);
    CHECK(s.basePosition == p.base(ParamSlot::Position));
    CHECK(s.effectivePosition == p.effective(ParamSlot::Position));
    CHECK(s.baseSpray == p.base(ParamSlot::Spray));
    CHECK_THAT(s.effectiveSpray, Catch::Matchers::WithinAbs(s.baseSpray + 0.5f, 1e-6));
    CHECK(s.feedback == p.feedbackAt(1000));
    // Only the newest block's snapshot is there: taking again finds nothing new.
    CHECK_FALSE(p.snapshot().take(s));

    // Freeze engages (seen off at the first call, so armed) and the next snapshot says so;
    // the write head stops after the freeze ramp, and Feedback shows as set.
    p.setBase(ParamSlot::Freeze, 1.0f);
    p.setBase(ParamSlot::Feedback, 0.25f);
    run(p, input, 1000, 4000, 512);
    REQUIRE(p.snapshot().take(s));
    CHECK(s.clock == 4000);
    CHECK(p.publishedClock().load() == 4000);
    CHECK(s.frozen);
    CHECK(s.writeHead == p.engine().liveWriteHead());
    CHECK(s.writeHead < 4000);
    CHECK(s.feedback == 0.25f);

    // A 0-frame call publishes too: the editor's clock is the sample clock, unchanged.
    p.process(nullptr, 0, nullptr, 0, 0, {});
    REQUIRE(p.snapshot().take(s));
    CHECK(s.clock == 4000);
}

TEST_CASE("the instrument publishes snapshots and its rings' peaks", "[core][publish]")
{
    // M2 sub-project 1's spec §6.2: the instrument's layers have rings too, fed from its input
    // (none here), so the head advances over silence.
    Processor p({kRate, 512, 3, Mode::Instrument});
    p.setPublishing(true);
    CHECK(p.livePeaks().frames() == static_cast<int64_t>(std::ceil(16.0 * kRate)));
    const std::array<gg::core::NoteEvent, 1> note{{{0, gg::core::NoteEventType::On, 0, 60}}};
    float l[512];
    float r[512];
    float *out[2] = {l, r};
    p.process(nullptr, 0, out, 2, 512, {}, note);
    UiSnapshot s;
    REQUIRE(p.snapshot().take(s));
    CHECK(s.clock == 512);
    CHECK(s.writeHead == 512);
    CHECK(s.feedback == 0.0f);
    CHECK(p.livePeaks().at(0) == 0.0f);
}

TEST_CASE("grain events reach the queue", "[core][publish]")
{
    // Density 6 (64 Hz) for 1 s: the queue holds exactly the grains the external sink saw, in
    // spawn order, with GrainVis's fields.
    Processor p({kRate, 512, 3, Mode::Fx});
    p.setPublishing(true);
    SpecLog log;
    p.setEventSink(&SpecLog::record, &log);
    p.setBase(ParamSlot::Density, 6.0f);
    const Channels input = noiseInput(48000, 0.5f, 13);
    run(p, input, 0, 48000, 480);

    std::vector<GrainVis> queued;
    GrainVis g;
    while (p.grainQueue().pop(g))
    {
        queued.push_back(g);
    }
    REQUIRE(log.grains.size() > 32);
    REQUIRE(queued.size() == log.grains.size());
    CHECK(p.grainQueue().dropped() == 0);
    for (size_t i = 0; i < queued.size(); ++i)
    {
        INFO("grain " << i);
        REQUIRE(sameGrain(queued[i], visOf(log.grains[i])));
    }
}

TEST_CASE("live peaks follow the input", "[core][publish]")
{
    // A 0.8 burst of 64 frames at 1.0 s over silence in the left channel, and a −0.6 one at
    // 52032 in the right: each bucket reads its burst's magnitude and the neighbours 0 (the
    // ring's time is the sample clock until a freeze).
    constexpr size_t kFrames = 72000;
    Channels input{std::vector<float>(kFrames, 0.0f), std::vector<float>(kFrames, 0.0f)};
    std::fill_n(input.left.begin() + 48000, 64, 0.8f);
    std::fill_n(input.right.begin() + 52032, 64, -0.6f);
    Processor p({kRate, 512, 3, Mode::Fx});
    p.setPublishing(true);
    run(p, input, 0, kFrames, 500);
    CHECK(p.livePeaks().at(48000) == 0.8f);
    CHECK(p.livePeaks().at(48063) == 0.8f);
    CHECK(p.livePeaks().at(47999) == 0.0f);
    CHECK(p.livePeaks().at(48064) == 0.0f);
    CHECK(p.livePeaks().at(52032) == 0.6f);
    CHECK(p.livePeaks().at(52031) == 0.0f);
    CHECK(p.livePeaks().at(52096) == 0.0f);
}

TEST_CASE("live peaks of a mono input and of a missing one", "[core][publish]")
{
    // A mono input feeds both ring channels: its strip is, bucket for bucket, the strip of a
    // stereo call with the same samples on both channels. From frame 60000 on the input is
    // missing (a null pointer) while the buffer behind it holds a loud signal: those buckets
    // read 0, so the strip is of what the engine was handed.
    constexpr size_t kFrames = 72000;
    constexpr size_t kMissingFrom = 60000;
    Channels x = noiseInput(kFrames, 0.5f, 19);
    std::fill_n(x.left.begin() + 48000, 64, 0.8f);
    std::fill(x.left.begin() + kMissingFrom, x.left.end(), 0.9f); // never handed in
    Processor mono({kRate, 512, 3, Mode::Fx});
    Processor stereo({kRate, 512, 3, Mode::Fx});
    float o[2][500];
    float *out[2] = {o[0], o[1]};
    for (size_t i = 0; i < kFrames; i += 500)
    {
        const bool present = i < kMissingFrom;
        const float *in1[1] = {x.left.data() + i};
        const float *in2[2] = {x.left.data() + i, x.left.data() + i};
        mono.process(present ? in1 : nullptr, 1, out, 1, 500, {});
        stereo.process(present ? in2 : nullptr, 2, out, 2, 500, {});
    }
    constexpr int64_t kB = LivePeaks::kBucket;
    for (int64_t f = 0; f < static_cast<int64_t>(kFrames); f += kB)
    {
        INFO("frame " << f);
        REQUIRE(mono.livePeaks().at(f) == stereo.livePeaks().at(f));
    }
    CHECK(mono.livePeaks().at(48000) == 0.8f);
    CHECK(mono.livePeaks().at(30000) > 0.25f); // the noise is there
    const int64_t firstMissing = (static_cast<int64_t>(kMissingFrom) + kB - 1) / kB * kB;
    for (int64_t f = firstMissing; f < static_cast<int64_t>(kFrames); f += kB)
    {
        INFO("frame " << f);
        REQUIRE(mono.livePeaks().at(f) == 0.0f);
    }
}

TEST_CASE("live peaks follow the ring when publishing turns on late", "[core][publish]")
{
    // Ruling PU-T9b: the strip follows the ring itself, publishing or not, so an editor that
    // opens late reads the ring's recent audio, not an older pass's peaks. reset() silences it
    // with the ring.
    constexpr size_t kFrames = 49000;
    Channels input{std::vector<float>(kFrames, 0.0f), std::vector<float>(kFrames, 0.0f)};
    std::fill_n(input.left.begin() + 24000, 64, 0.8f);
    Processor p({kRate, 512, 3, Mode::Fx});
    run(p, input, 0, 48000, 512); // publishing off: the burst goes by unseen
    p.setPublishing(true);
    run(p, input, 48000, kFrames, 500);
    CHECK(p.livePeaks().at(24000) == 0.8f);
    CHECK(p.livePeaks().at(23999) == 0.0f);
    CHECK(p.livePeaks().at(24064) == 0.0f);

    p.reset();
    for (int64_t f = 0; f < p.livePeaks().frames(); f += LivePeaks::kBucket)
    {
        REQUIRE(p.livePeaks().at(f) == 0.0f);
    }
    run(p, input, 0, 24064, 512); // ring time from 0 again
    CHECK(p.livePeaks().at(24000) == 0.8f);
}

TEST_CASE("live peaks during the freeze ramp", "[core][publish]")
{
    // Freeze engaged at C: the head keeps writing for the L-sample ramp, then stops. The frames
    // written during the ramp carry their peaks (the input's, before the write gain): a 0.8 burst
    // early in it and a 0.7 one in its last 32 frames. Once the head stops nothing is written,
    // so a 0.6 burst while frozen shows nowhere, and the frames at and after the head read what
    // was last written there: in a fresh ring, nothing.
    constexpr int64_t kC = 24000;
    const int64_t kL = gg::LiveSource::freezeRampSamples(kRate);
    REQUIRE(kL > 256);
    constexpr size_t kFrames = 48000;
    Channels input{std::vector<float>(kFrames, 0.0f), std::vector<float>(kFrames, 0.0f)};
    std::fill_n(input.left.begin() + kC + 128, 64, 0.8f);
    std::fill_n(input.left.begin() + kC + kL - 32, 32, 0.7f);
    std::fill_n(input.left.begin() + kC + kL + 512, 64, 0.6f);
    Processor p({kRate, 512, 3, Mode::Fx});
    p.setPublishing(true);
    run(p, input, 0, kC, 512); // live; Freeze seen off, so armed
    p.setBase(ParamSlot::Freeze, 1.0f);
    run(p, input, kC, kFrames, 512);

    const int64_t head = p.engine().liveWriteHead();
    REQUIRE(head == kC + kL);
    CHECK(p.livePeaks().at(kC + 128) == 0.8f);
    CHECK(p.livePeaks().at(kC + 191) == 0.8f);
    CHECK(p.livePeaks().at(head - 1) == 0.7f);
    CHECK(p.livePeaks().at(head - 32) == 0.7f);
    constexpr int64_t kB = LivePeaks::kBucket;
    for (int64_t f = (head + kB - 1) / kB * kB; f < static_cast<int64_t>(kFrames); f += kB)
    {
        INFO("frame " << f);
        REQUIRE(p.livePeaks().at(f) == 0.0f);
    }
}

TEST_CASE("live peaks follow ring time across a freeze", "[core][publish]")
{
    // Frozen, the ring's head stops while the sample clock goes on: the peaks are indexed by
    // ring time, the time GrainVis::startPos and the snapshot's write head are in, so a burst
    // after the freeze lands at the write head, not at the sample clock.
    constexpr size_t kFrames = 96000;
    Channels input{std::vector<float>(kFrames, 0.0f), std::vector<float>(kFrames, 0.0f)};
    Processor p({kRate, 512, 3, Mode::Fx});
    p.setPublishing(true);
    run(p, input, 0, 24000, 512); // live; Freeze seen off, so armed
    p.setBase(ParamSlot::Freeze, 1.0f);
    run(p, input, 24000, 48000, 512);
    p.setBase(ParamSlot::Freeze, 0.0f);
    run(p, input, 48000, 60000, 512); // past the unfreeze ramp
    const int64_t head = p.engine().liveWriteHead();
    const int64_t clock = p.engine().sampleClock();
    REQUIRE(clock == 60000);
    REQUIRE(head < clock - 10000);
    std::fill_n(input.right.begin() + 60000, 64, -0.8f);
    run(p, input, 60000, kFrames, 512);
    // The burst's buckets read 0.8, those a bucket clear of it 0, and the sample clock's frame
    // (where indexing by output time would have put it) is silent.
    constexpr int64_t kB = gg::core::LivePeaks::kBucket;
    CHECK(p.livePeaks().at(head) == 0.8f);
    CHECK(p.livePeaks().at(head + 63) == 0.8f);
    CHECK(p.livePeaks().at(head - kB) == 0.0f);
    CHECK(p.livePeaks().at(head + 2 * kB) == 0.0f);
    CHECK(p.livePeaks().at(clock) == 0.0f);
}
