// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Real-time safety (INTENT §14): gg::Engine::process never allocates. The allocation tripwire
// checks that in every build; the RTSan build also aborts on any allocation, lock or blocking
// call made inside the SST_CPPUTILS_NONBLOCKING process().

#include <cmath>
#include <cstdint>
#include <memory>
#include <new>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <sst/cpputils/rtsan_support.h>

#include "gg/engine/Engine.h"
#include "support/AllocTripwire.h"

using gg::test::AllocTripwire;

namespace
{
// Counts spawns without allocating: the sink runs inside process().
struct SpawnCounter
{
    uint64_t spawns{0};

    static void record(void *ctx, const gg::GrainSpec &)
    {
        ++static_cast<SpawnCounter *>(ctx)->spawns;
    }
};

// Over-aligned, so new Wide uses the align_val_t operators.
struct alignas(64) Wide
{
    char bytes[64];
};
} // namespace

TEST_CASE("tripwire detects allocation", "[rt]")
{
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
    auto source = std::make_shared<gg::SampleBuffer>(2, 48000, 48000.0);
    for (uint32_t c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < source->frames(); ++i)
        {
            const double phase = 2.0 * std::numbers::pi * (330.0 * i / 48000.0 + 0.25 * c);
            source->channel(c)[i] = static_cast<float>(std::sin(phase));
        }
    }
    engine.setSource(source);
    // 200 Hz of 50 ms grains: one spawns every 240 frames and about 10 overlap, so the armed
    // calls below spawn, render and retire grains.
    engine.setStreamParams({200.0, 0.05, 0.5, 0.5, 7.0, 0.5f});
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
    REQUIRE(counter.spawns > spawnsBefore); // grains spawned while armed
    REQUIRE(allocations == 0);
}

#if GG_USE_RTSAN
TEST_CASE("rtsan is active in this build", "[rt][rtsan]") { REQUIRE(SST_CPPUTILS_HAS_RTSAN == 1); }
#endif
