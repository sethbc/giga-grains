// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The publish structures (spec §6.4): the grain event queue, the latest-wins snapshot buffer and
// the peak strip of the live ring, which the audio thread fills for the editor without
// allocating, locking or waiting.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/GrainQueue.h"
#include "gg/core/LatestValue.h"
#include "gg/core/LivePeaks.h"
#include "gg/core/UiSnapshot.h"
#include "support/AllocTripwire.h"

using gg::core::GrainQueue;
using gg::core::GrainVis;
using gg::core::LatestValue;
using gg::core::LivePeaks;
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
           a.durationSamples == b.durationSamples && a.gain == b.gain && a.pan == b.pan;
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

TEST_CASE("live peaks unprepared are silent and ignore writes", "[core][publish]")
{
    LivePeaks peaks;
    CHECK(peaks.frames() == 0);
    CHECK(peaks.buckets() == 0);
    const std::array<float, 4> l{1.0f, 1.0f, 1.0f, 1.0f};
    peaks.write(l.data(), l.data(), 0, 4);
    CHECK(peaks.at(0) == 0.0f);
}
