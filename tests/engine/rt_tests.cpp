// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Real-time safety (INTENT §14): gg::Engine::process never allocates. The allocation tripwire
// checks that in every build but ASan's, where it is compiled out: there these tests still run
// their other checks, and the tripwire's own test SKIPs. The RTSan build also aborts on any
// allocation, lock or blocking call made inside the SST_CPPUTILS_NONBLOCKING process().

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <new>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <sst/cpputils/rtsan_support.h>

#include "gg/engine/Engine.h"
#include "gg/source/PreparedSource.h"
#include "support/AllocTripwire.h"

using gg::test::AllocTripwire;

namespace
{
// Counts spawns, the Eco ones, the HQ ones and the ones reading the ring, without allocating: the
// sink runs inside process().
struct SpawnCounter
{
    uint64_t spawns{0};
    uint64_t eco{0};
    uint64_t hq{0};
    uint64_t live{0};
    uint64_t steals{0};

    static void record(void *ctx, uint32_t, const gg::GrainSpec &g)
    {
        auto *counter = static_cast<SpawnCounter *>(ctx);
        ++counter->spawns;
        counter->eco += g.tier == gg::Tier::Eco ? 1 : 0;
        counter->hq += g.tier == gg::Tier::HQ ? 1 : 0;
        counter->live += g.sourceSlot == gg::Engine::kLiveSlot ? 1 : 0;
    }
    static void steal(void *ctx, uint32_t, uint64_t, int64_t)
    {
        ++static_cast<SpawnCounter *>(ctx)->steals;
    }
};

// One second of a stereo sine at 48 kHz, the right channel a quarter turn ahead.
std::shared_ptr<const gg::SampleBuffer> stereoSine(double hz)
{
    auto source = std::make_shared<gg::SampleBuffer>(2, 48000, 48000.0);
    for (uint32_t c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < source->frames(); ++i)
        {
            const double phase = 2.0 * std::numbers::pi * (hz * i / 48000.0 + 0.25 * c);
            source->channel(c)[i] = static_cast<float>(std::sin(phase));
        }
    }
    return source;
}

// Over-aligned, so new Wide uses the align_val_t operators.
struct alignas(64) Wide
{
    char bytes[64];
};
} // namespace

TEST_CASE("tripwire detects allocation", "[rt]")
{
    if (!gg::test::AllocTripwire::available())
    {
        SKIP("allocation tripwire is off under ASan; RTSan covers this");
    }
    // The pointers are volatile so the optimizer can't elide the new/delete pairs (C++14
    // allows it), which it does in the RelWithDebInfo RTSan build.
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        int *volatile p = new int(1);
        delete p;
    }
    REQUIRE(AllocTripwire::count() == 1);

    // Outside a Scope nothing is counted.
    {
        int *volatile p = new int(2);
        delete p;
    }
    REQUIRE(AllocTripwire::count() == 1);

    // Every operator new variant is counted, and an inner Scope ending leaves the outer armed.
    AllocTripwire::resetCount();
    REQUIRE(AllocTripwire::count() == 0);
    {
        AllocTripwire::Scope armed;
        {
            AllocTripwire::Scope nested;
            int *volatile array = new int[3];
            delete[] array;
        }
        Wide *volatile aligned = new Wide;
        delete aligned;
        Wide *volatile alignedArray = new Wide[2];
        delete[] alignedArray;
        int *volatile nothrow = new (std::nothrow) int(4);
        delete nothrow;
        int *volatile nothrowArray = new (std::nothrow) int[5];
        delete[] nothrowArray;
        Wide *volatile alignedNothrow = new (std::nothrow) Wide;
        delete alignedNothrow;
        Wide *volatile alignedNothrowArray = new (std::nothrow) Wide[2];
        delete[] alignedNothrowArray;
    }
    REQUIRE(AllocTripwire::count() == 7);
}

TEST_CASE("engine process does not allocate", "[rt]")
{
    gg::Engine engine(gg::EngineConfig{48000.0, 4096, 4096, 3});
    engine.setSource(stereoSine(330.0));
    // 200 Hz of 50 ms grains: one spawns every 240 frames on average and about 10 overlap, so
    // the armed calls below spawn, render and retire grains. Poisson onsets and full-width pan
    // put the scheduler's logDet draws and the pan gains under the tripwire.
    engine.setCloudParams({.densityHz = 200.0,
                           .distribution = 1.0,
                           .grainDurationSec = 0.05,
                           .position = 0.5,
                           .spray = 0.5,
                           .pitchSemitones = 7.0,
                           .stereo = 1.0,
                           .gain = 0.5f});
    SpawnCounter counter;
    engine.setEventSink(&SpawnCounter::record, &counter);

    std::vector<float> left(64), right(64);
    float *out[2] = {left.data(), right.data()};
    for (int i = 0; i < 50; ++i) // warm up until grains are sounding
    {
        engine.process(out, 2, 64);
    }
    const uint32_t activeBefore = engine.activeGrains();
    const uint64_t spawnsBefore = counter.spawns;

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int i = 0; i < 100; ++i)
        {
            engine.process(out, 2, 64);
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    REQUIRE(activeBefore > 0);
    REQUIRE(engine.activeGrains() > 0);
    const uint64_t spawnedArmed = counter.spawns - spawnsBefore;
    REQUIRE(spawnedArmed > 0); // grains spawned while armed
    // Grains retired while armed too: without retirement the pool would have grown by every
    // grain spawned.
    REQUIRE(static_cast<int64_t>(spawnedArmed) >
            static_cast<int64_t>(engine.activeGrains()) - static_cast<int64_t>(activeBefore));
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("engine process does not allocate with a full pool or odd block sizes", "[rt]")
{
    // Capacity 4 against about 20 overlapping grains keeps the pool full, so new grains go
    // through admit()'s full-pool path: dropped, or, when a grain has ended earlier in the same
    // block (it ends 118 frames before the next onset), rendered up to the onset, retired and
    // replaced. maxBlockSize 256 makes the 1000-frame calls oversized, so process() splits them,
    // and is long enough for a grain's end and the next onset to share a block.
    gg::Engine engine(gg::EngineConfig{48000.0, 256, 4, 5});
    engine.setSource(stereoSine(220.0));
    engine.setCloudParams({.densityHz = 400.0,
                           .grainDurationSec = 0.05,
                           .position = 0.5,
                           .spray = 0.5,
                           .pitchSemitones = -5.0,
                           .gain = 0.7f});
    SpawnCounter counter;
    engine.setEventSink(&SpawnCounter::record, &counter);

    std::vector<float> left(1000), right(1000);
    float *out[2] = {left.data(), right.data()};
    engine.process(out, 2, 1000); // warm up until the pool is full
    const uint64_t spawnsBefore = counter.spawns;
    const uint64_t droppedBefore = engine.droppedGrains();
    const int64_t clockBefore = engine.sampleClock();

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int i = 0; i < 20; ++i)
        {
            engine.process(out, 2, 0);
            engine.process(out, 2, 1);
            engine.process(out, 2, 1000);
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    REQUIRE(engine.sampleClock() == clockBefore + 20 * 1001);
    REQUIRE(counter.spawns > spawnsBefore);          // admitted into a full pool while armed
    REQUIRE(engine.droppedGrains() > droppedBefore); // and dropped while armed
    if (gg::test::AllocTripwire::available())        // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("engine process does not allocate when the pool's storage fills", "[rt]")
{
    // Storage holds 2 × capacity grains and is compacted once per block, so it fills only when
    // more than capacity grains finish within one block. Capacity 16 against 4 kHz of 5 ms
    // grains admits a grain each time one ends, about one every 15 samples, so a 1000-frame call
    // (one block: maxBlockSize is 4096) admits about 66, more than the 32 slots: storage fills
    // mid-block, and admit() renders the pool to the onset and compacts it early. Grains as long
    // as a second could never fill it, since only 16 of them end per second.
    constexpr uint32_t kCapacity = 16;
    gg::Engine engine(gg::EngineConfig{48000.0, 4096, kCapacity, 7});
    engine.setSource(stereoSine(220.0));
    engine.setCloudParams({.densityHz = 4000.0,
                           .distribution = 1.0,
                           .grainDurationSec = 0.005,
                           .position = 0.5,
                           .spray = 0.5,
                           .pitchSemitones = 3.0,
                           .stereo = 1.0,
                           .gain = 0.5f});
    SpawnCounter counter;
    engine.setEventSink(&SpawnCounter::record, &counter);

    std::vector<float> left(1000), right(1000);
    float *out[2] = {left.data(), right.data()};
    engine.process(out, 2, 1000); // warm up until the pool is saturated
    const uint64_t droppedBefore = engine.droppedGrains();

    uint64_t mostSpawnedInOneCall = 0;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int i = 0; i < 20; ++i)
        {
            engine.process(out, 2, 0);
            engine.process(out, 2, 1);
            const uint64_t before = counter.spawns;
            engine.process(out, 2, 1000);
            const uint64_t spawned = counter.spawns - before;
            mostSpawnedInOneCall = spawned > mostSpawnedInOneCall ? spawned : mostSpawnedInOneCall;
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    // More than 2 × capacity spawns in one block can't all be stored without an early compaction.
    REQUIRE(mostSpawnedInOneCall > 2 * kCapacity);
    REQUIRE(engine.droppedGrains() > droppedBefore); // saturated while armed
    if (gg::test::AllocTripwire::available())        // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("engine live process does not allocate", "[rt]")
{
    // Live mode: every call writes its input into the ring (allocated by the constructor) before
    // the grains read it. Grains 10 ms behind the head and Freeze toggled while armed put the
    // admission clamp, the head fade and the freeze ramp under the tripwire. maxBlockSize 256
    // makes the 1000-frame calls oversized; mono, stereo and missing inputs all run.
    gg::EngineConfig config{48000.0, 256, 4096, 3};
    config.liveSeconds = 16.0;
    gg::Engine engine(config);
    engine.setSourceMode(gg::SourceMode::Live);
    engine.setCloudParams({.densityHz = 200.0,
                           .distribution = 1.0,
                           .grainDurationSec = 0.05,
                           .position = 0.01,
                           .spray = 0.02,
                           .pitchSemitones = 7.0,
                           .stereo = 1.0,
                           .gain = 0.5f});
    SpawnCounter counter;
    engine.setEventSink(&SpawnCounter::record, &counter);

    std::vector<float> inLeft(1000), inRight(1000);
    for (std::size_t i = 0; i < inLeft.size(); ++i)
    {
        inLeft[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * i / 48000.0));
        inRight[i] = -inLeft[i];
    }
    const float *in[2] = {inLeft.data(), inRight.data()};
    std::vector<float> left(1000), right(1000);
    float *out[2] = {left.data(), right.data()};
    for (int i = 0; i < 24; ++i) // warm up until grains are sounding
    {
        engine.process(in, 2, out, 2, 1000);
    }
    const uint64_t spawnsBefore = counter.spawns;
    const int64_t headBefore = engine.liveWriteHead();

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int i = 0; i < 40; ++i)
        {
            engine.setFreeze(i % 4 == 1 || i % 4 == 2);
            engine.setSourceMode(gg::SourceMode::Live);
            engine.process(in, 2, out, 2, 0);
            engine.process(in, 2, out, 2, 1);
            engine.process(in, 2, out, 2, 1000);
            engine.process(in, 1, out, 2, 100);
            engine.process(nullptr, 0, out, 2, 100);
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    REQUIRE(counter.spawns > spawnsBefore);
    REQUIRE(engine.activeGrains() > 0);
    REQUIRE(engine.liveWriteHead() > headBefore);
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("engine process does not allocate at the Eco tier", "[rt]")
{
    // Ruling 2: each grain latches its tier, so the Eco grains spawned while armed read through
    // the linear kernels alongside Normal grains still sounding. Both outputs and one, sample and
    // live sources, and a quality change back to Normal and to Eco again inside the armed span.
    gg::CloudParams params{.densityHz = 200.0,
                           .distribution = 1.0,
                           .grainDurationSec = 0.05,
                           .position = 0.5,
                           .spray = 0.5,
                           .pitchSemitones = 7.0,
                           .stereo = 1.0,
                           .gain = 0.5f};
    std::vector<float> inLeft(1000), inRight(1000);
    for (std::size_t i = 0; i < inLeft.size(); ++i)
    {
        inLeft[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * i / 48000.0));
        inRight[i] = -inLeft[i];
    }
    const float *in[2] = {inLeft.data(), inRight.data()};
    std::vector<float> left(1000), right(1000);
    float *out[2] = {left.data(), right.data()};

    gg::EngineConfig config{48000.0, 256, 4096, 9};
    config.liveSeconds = 1.0;
    gg::Engine engine(config);
    engine.setSource(stereoSine(330.0));
    SECTION("sample source") {}
    SECTION("live ring")
    {
        engine.setSourceMode(gg::SourceMode::Live);
        params.position = 0.01;
        params.spray = 0.02;
    }
    engine.setCloudParams(params); // Normal: these grains are still sounding when Eco starts
    SpawnCounter counter;
    engine.setEventSink(&SpawnCounter::record, &counter);
    for (int i = 0; i < 4; ++i) // warm up until grains are sounding
    {
        engine.process(in, 2, out, 2, 1000);
    }
    gg::CloudParams eco = params;
    eco.quality = gg::Tier::Eco;
    engine.setCloudParams(eco);
    engine.process(in, 2, out, 2, 1); // the next armed call renders both tiers' grains
    const uint64_t ecoBefore = counter.eco;
    const uint64_t normalBefore = counter.spawns - counter.eco;

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int i = 0; i < 20; ++i)
        {
            engine.setCloudParams(i == 10 || i == 11 ? params : eco);
            engine.process(in, 2, out, 2, 0);
            engine.process(in, 2, out, 2, 1);
            engine.process(in, 2, out, 2, 1000);
            engine.process(in, 2, out, 1, 300);
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    REQUIRE(counter.eco > ecoBefore);                     // Eco grains spawned while armed
    REQUIRE(counter.spawns - counter.eco > normalBefore); // and Normal ones
    REQUIRE(engine.activeGrains() > 0);
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("engine process does not allocate at the HQ tier", "[rt]")
{
    // The HQ grains read through the 16-tap sinc kernels (spec §6.2), up a fifth so they are
    // stretched, alongside Normal grains still sounding: both outputs and one, sample and live
    // sources, calls of 0, 1, 300 and 1,000 samples, and a quality change to Normal and back to
    // HQ inside the armed span.
    gg::CloudParams params{.densityHz = 200.0,
                           .distribution = 1.0,
                           .grainDurationSec = 0.05,
                           .position = 0.5,
                           .spray = 0.5,
                           .pitchSemitones = 7.0,
                           .stereo = 1.0,
                           .gain = 0.5f};
    std::vector<float> inLeft(1000), inRight(1000);
    for (std::size_t i = 0; i < inLeft.size(); ++i)
    {
        inLeft[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * i / 48000.0));
        inRight[i] = -inLeft[i];
    }
    const float *in[2] = {inLeft.data(), inRight.data()};
    std::vector<float> left(1000), right(1000);
    float *out[2] = {left.data(), right.data()};

    gg::EngineConfig config{48000.0, 256, 4096, 9};
    config.liveSeconds = 1.0;
    gg::Engine engine(config);
    engine.setSource(stereoSine(330.0));
    SECTION("sample source") {}
    SECTION("live ring")
    {
        engine.setSourceMode(gg::SourceMode::Live);
        params.position = 0.01;
        params.spray = 0.02;
    }
    engine.setCloudParams(params); // Normal: these grains are still sounding when HQ starts
    SpawnCounter counter;
    engine.setEventSink(&SpawnCounter::record, &counter);
    for (int i = 0; i < 4; ++i) // warm up until grains are sounding
    {
        engine.process(in, 2, out, 2, 1000);
    }
    gg::CloudParams hq = params;
    hq.quality = gg::Tier::HQ;
    engine.setCloudParams(hq);
    engine.process(in, 2, out, 2, 1); // the next armed call renders both tiers' grains
    const uint64_t hqBefore = counter.hq;
    const uint64_t normalBefore = counter.spawns - counter.hq;

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int i = 0; i < 20; ++i)
        {
            engine.setCloudParams(i == 10 || i == 11 ? params : hq);
            engine.process(in, 2, out, 2, 0);
            engine.process(in, 2, out, 2, 1);
            engine.process(in, 2, out, 2, 1000);
            engine.process(in, 2, out, 1, 300);
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    REQUIRE(counter.hq > hqBefore);                      // HQ grains spawned while armed
    REQUIRE(counter.spawns - counter.hq > normalBefore); // and Normal ones
    REQUIRE(engine.activeGrains() > 0);
    REQUIRE(
        std::all_of(left.begin(), left.begin() + 300, [](float x) { return std::isfinite(x); }));
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("engine process does not allocate on every rung", "[rt]")
{
    // The overload ladder (spec §6.5) with the tripwire armed from a reset: HQ draws of 0.1 s at
    // 400 Hz into a quota of 40 units. Grains 0 to 2 are admitted at HQ (36 units), grain 3 is
    // lowered to Normal (40), the next ones drop while grain 0 is young, and from its half-way
    // point each admission steals one. The quota is set inside the armed span too, sample and
    // live sources, both outputs and one, odd block sizes.
    gg::CloudParams params{.densityHz = 400.0,
                           .grainDurationSec = 0.1,
                           .position = 0.5,
                           .spray = 0.5,
                           .pitchSemitones = 3.0,
                           .stereo = 1.0,
                           .gain = 0.5f,
                           .quality = gg::Tier::HQ};
    std::vector<float> inLeft(1000), inRight(1000);
    for (std::size_t i = 0; i < inLeft.size(); ++i)
    {
        inLeft[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * i / 48000.0));
        inRight[i] = -inLeft[i];
    }
    const float *in[2] = {inLeft.data(), inRight.data()};
    std::vector<float> left(1000), right(1000);
    float *out[2] = {left.data(), right.data()};

    gg::EngineConfig config{48000.0, 256, 4096, 13};
    config.liveSeconds = 1.0;
    gg::Engine engine(config);
    engine.setSource(stereoSine(330.0));
    SECTION("sample source") {}
    SECTION("live ring")
    {
        engine.setSourceMode(gg::SourceMode::Live);
        params.position = 0.2;
        params.spray = 0.1;
    }
    engine.setCloudParams(params);
    SpawnCounter counter;
    engine.setEventSink(&SpawnCounter::record, &counter);
    engine.setStealSink(&SpawnCounter::steal, &counter);
    engine.process(in, 2, out, 2, 1000); // the ring fills, and the mode applies
    const uint64_t hqBefore = counter.hq;

    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        engine.reset();
        engine.setQuota(40);
        for (int i = 0; i < 20; ++i)
        {
            engine.process(in, 2, out, 2, 0);
            engine.process(in, 2, out, 2, 1);
            engine.process(in, 2, out, 2, 1000);
            engine.process(in, 2, out, 1, 300);
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    // reset() zeroed the counters, so each counts the armed span alone.
    REQUIRE(counter.hq > hqBefore);      // rung 1 at the draw's tier
    REQUIRE(engine.loweredGrains() > 0); // rung 2
    REQUIRE(engine.droppedGrains() > 0); // rung 3's drop
    REQUIRE(engine.stolenGrains() > 0);  // and its steal
    REQUIRE(counter.steals == engine.stolenGrains());
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("engine adopts and hands back sources without allocating", "[engine][rt]")
{
    // Prepared off the audio thread; adopted, rendered and handed back with the tripwire armed.
    const auto a = gg::PreparedSource::make(stereoSine(330.0));
    const auto b = gg::PreparedSource::make(stereoSine(220.0));
    gg::Engine engine(gg::EngineConfig{48000.0, 4096, 4096, 3});
    engine.setCloudParams({.densityHz = 200.0,
                           .distribution = 1.0,
                           .grainDurationSec = 0.05,
                           .position = 0.5,
                           .spray = 0.5,
                           .pitchSemitones = 7.0,
                           .stereo = 1.0,
                           .gain = 0.5f});
    SpawnCounter counter;
    engine.setEventSink(&SpawnCounter::record, &counter);
    REQUIRE(engine.adoptSource(a.get()));

    std::vector<float> left(64), right(64);
    float *out[2] = {left.data(), right.data()};
    for (int i = 0; i < 50; ++i) // warm up until A's grains are sounding
    {
        engine.process(out, 2, 64);
    }
    REQUIRE(engine.activeGrains() > 0);
    const uint64_t spawnsBefore = counter.spawns;

    bool adoptedB = false;
    bool refusedA = false;
    const gg::PreparedSource *retiredA = nullptr;
    bool adoptedAAgain = false;
    const gg::PreparedSource *retiredB = nullptr;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        adoptedB = engine.adoptSource(b.get());
        refusedA = !engine.adoptSource(a.get()); // A still has grains: two sources are in play
        for (int i = 0; i < 100 && retiredA == nullptr; ++i)
        {
            engine.process(out, 2, 64);
            retiredA = engine.takeRetiredSource();
        }
        adoptedAAgain = engine.adoptSource(a.get());
        for (int i = 0; i < 100 && retiredB == nullptr; ++i)
        {
            engine.process(out, 2, 64);
            retiredB = engine.takeRetiredSource();
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    REQUIRE(adoptedB);
    REQUIRE(refusedA);
    REQUIRE(retiredA == a.get());
    REQUIRE(adoptedAAgain);
    REQUIRE(retiredB == b.get());
    REQUIRE(counter.spawns > spawnsBefore);   // grains spawned and rendered while armed
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

TEST_CASE("engine source kind switch does not allocate", "[engine][rt]")
{
    // Carry-over row 118: the source-kind switch, both ways, with the tripwire armed (spec
    // §6.4). 0.2 s grains at 200 Hz and a switch every 501 frames, so grains of both kinds sound
    // across every switch and the renderer dispatches sample and ring grains in one pool.
    gg::EngineConfig config{48000.0, 256, 4096, 5};
    config.liveSeconds = 1.0;
    gg::Engine engine(config);
    engine.setSource(stereoSine(330.0));
    engine.setCloudParams({.densityHz = 200.0,
                           .distribution = 1.0,
                           .grainDurationSec = 0.2,
                           .position = 0.05,
                           .spray = 0.02,
                           .pitchSemitones = 3.0,
                           .stereo = 1.0,
                           .gain = 0.5f});
    SpawnCounter counter;
    engine.setEventSink(&SpawnCounter::record, &counter);

    std::vector<float> inLeft(500), inRight(500);
    for (std::size_t i = 0; i < inLeft.size(); ++i)
    {
        inLeft[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * i / 48000.0));
        inRight[i] = -inLeft[i];
    }
    const float *in[2] = {inLeft.data(), inRight.data()};
    std::vector<float> left(500), right(500);
    float *out[2] = {left.data(), right.data()};
    for (int i = 0; i < 20; ++i) // warm up until sample grains are sounding
    {
        engine.process(in, 2, out, 2, 500);
    }
    REQUIRE(engine.activeGrains() > 0);
    const uint64_t spawnsBefore = counter.spawns;
    const uint64_t liveBefore = counter.live;

    bool alternated = true;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        for (int i = 0; i < 20; ++i)
        {
            const gg::SourceMode mode = i % 2 == 0 ? gg::SourceMode::Live : gg::SourceMode::Sample;
            engine.setSourceMode(mode);
            engine.process(in, 2, out, 2, 1);
            alternated = alternated && engine.sourceMode() == mode;
            engine.process(in, 2, out, 2, 500);
        }
    }
    const uint64_t allocations = AllocTripwire::count();

    REQUIRE(alternated);
    REQUIRE(counter.live > liveBefore);                                 // ring grains spawned
    REQUIRE(counter.spawns - counter.live > spawnsBefore - liveBefore); // and sample grains
    REQUIRE(engine.activeGrains() > 0);
    if (gg::test::AllocTripwire::available()) // off under ASan; RTSan covers this
    {
        REQUIRE(allocations == 0);
    }
}

#if GG_USE_RTSAN
TEST_CASE("rtsan is active in this build", "[rt][rtsan]") { REQUIRE(SST_CPPUTILS_HAS_RTSAN == 1); }
#endif
