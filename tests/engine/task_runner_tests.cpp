// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The task runners (spec §6.7): SerialRunner, the loop the engine falls back to when the host
// offers no thread pool or declines a request, and the tests' StubPool, which models a host pool
// with N worker threads (and one that declines every other request). The later thread-pool
// invariance test renders through both, so these check that the stub runs every index exactly
// once, returns only after all have finished, really runs them on several threads at once, and
// allocates nothing on the caller's thread in run().

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "gg/engine/TaskRunner.h"
#include "support/AllocTripwire.h"
#include "support/StubPool.h"

using gg::test::AllocTripwire;

namespace
{
constexpr uint32_t kMaxTasks = 16;

// What one run() saw, recorded without allocating: the tasks run inside run().
struct Record
{
    std::array<std::atomic<uint32_t>, kMaxTasks> hits{};
    std::array<std::atomic<bool>, kMaxTasks> done{};
    std::array<std::thread::id, kMaxTasks> thread{};
    std::array<uint32_t, kMaxTasks> order{};
    std::atomic<uint32_t> calls{0};

    void reset()
    {
        for (uint32_t i = 0; i < kMaxTasks; ++i)
        {
            hits[i] = 0;
            done[i] = false;
            thread[i] = {};
            order[i] = 0;
        }
        calls = 0;
    }
};

// Records the index's arrival order, its thread and a hit.
void recordTask(void *ctx, uint32_t index) noexcept
{
    auto *r = static_cast<Record *>(ctx);
    if (index >= kMaxTasks)
        return;
    r->order[r->calls.fetch_add(1)] = index;
    r->thread[index] = std::this_thread::get_id();
    r->hits[index].fetch_add(1);
}

// Takes a while before marking its index done, so that a run() that returned before every task
// had finished leaves a flag unset.
void slowTask(void *ctx, uint32_t index) noexcept
{
    auto *r = static_cast<Record *>(ctx);
    if (index >= kMaxTasks)
        return;
    std::this_thread::sleep_for(std::chrono::microseconds(300 * (index % 3)));
    r->thread[index] = std::this_thread::get_id();
    r->hits[index].fetch_add(1);
    r->done[index].store(true);
}

// Counts every index into a running tally, for many calls in a row.
struct Tally
{
    std::array<std::atomic<uint32_t>, kMaxTasks> hits{};
};

void tallyTask(void *ctx, uint32_t index) noexcept
{
    auto *t = static_cast<Tally *>(ctx);
    if (index < kMaxTasks)
        t->hits[index].fetch_add(1, std::memory_order_relaxed);
}

// Waits until `expected` tasks are running at once, or a generous deadline shared by all of them
// passes (so a stub that ran them one after another fails in seconds instead of hanging).
struct Rendezvous
{
    explicit Rendezvous(uint32_t n) : expected(n) {}

    const uint32_t expected;
    const std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::now() +
                                                         std::chrono::seconds(5)};
    std::atomic<uint32_t> arrived{0};
    std::atomic<uint32_t> metAll{0};
};

void rendezvousTask(void *ctx, uint32_t) noexcept
{
    auto *r = static_cast<Rendezvous *>(ctx);
    r->arrived.fetch_add(1);
    while (r->arrived.load() < r->expected && std::chrono::steady_clock::now() < r->deadline)
        std::this_thread::yield();
    if (r->arrived.load() >= r->expected)
        r->metAll.fetch_add(1);
}
} // namespace

TEST_CASE("the serial runner runs every index once in order", "[engine][threads]")
{
    gg::SerialRunner serial;
    gg::TaskRunner &runner = serial;
    Record r;

    const uint64_t before = AllocTripwire::count();
    {
        AllocTripwire::Scope armed;
        runner.run(5, recordTask, &r);
    }
    const uint64_t allocations = AllocTripwire::count() - before;

    REQUIRE(r.calls.load() == 5);
    for (uint32_t i = 0; i < 5; ++i)
    {
        CHECK(r.order[i] == i);
        CHECK(r.hits[i].load() == 1);
        CHECK(r.thread[i] == std::this_thread::get_id());
    }
    if (AllocTripwire::available()) // off under ASan; RTSan covers the engine's callers
        CHECK(allocations == 0);

    r.reset();
    runner.run(0, recordTask, &r);
    CHECK(r.calls.load() == 0);
}

TEST_CASE("the stub pool runs every index exactly once", "[engine][threads]")
{
    const unsigned workers = GENERATE(1u, 2u, 8u);
    CAPTURE(workers);
    gg::test::StubPool pool(workers);
    gg::TaskRunner &runner = pool;
    Record r;

    SECTION("count 7, and run returns only after every task has finished")
    {
        const uint64_t before = AllocTripwire::count();
        {
            AllocTripwire::Scope armed;
            runner.run(7, slowTask, &r);
        }
        const uint64_t allocations = AllocTripwire::count() - before;

        for (uint32_t i = 0; i < 7; ++i)
        {
            CAPTURE(i);
            CHECK(r.done[i].load());
            CHECK(r.hits[i].load() == 1);
        }
        for (uint32_t i = 7; i < kMaxTasks; ++i)
            CHECK(r.hits[i].load() == 0);
        if (AllocTripwire::available()) // off under ASan
            CHECK(allocations == 0);
    }

    SECTION("count 0 runs nothing")
    {
        runner.run(0, slowTask, &r);
        for (uint32_t i = 0; i < kMaxTasks; ++i)
            CHECK(r.hits[i].load() == 0);
    }

    SECTION("thousands of calls in a row each run every index once")
    {
        constexpr uint32_t kCalls = 2000;
        Tally t;
        const uint64_t before = AllocTripwire::count();
        {
            AllocTripwire::Scope armed;
            for (uint32_t c = 0; c < kCalls; ++c)
                runner.run(1 + c % 9, tallyTask, &t);
        }
        const uint64_t allocations = AllocTripwire::count() - before;

        // Index i runs in every call whose count 1 + c % 9 exceeds it.
        for (uint32_t i = 0; i < kMaxTasks; ++i)
        {
            uint32_t expected = 0;
            for (uint32_t c = 0; c < kCalls; ++c)
                expected += i < 1 + c % 9 ? 1 : 0;
            CAPTURE(i);
            CHECK(t.hits[i].load() == expected);
        }
        if (AllocTripwire::available()) // off under ASan
            CHECK(allocations == 0);
    }
}

TEST_CASE("the stub pool runs tasks on its worker threads at once", "[engine][threads]")
{
    // The caller and every worker take indices, so workers + 1 tasks can all be running at
    // once; each waits for the others. A stub that ran them in turn would time out.
    const unsigned workers = GENERATE(1u, 2u, 8u);
    CAPTURE(workers);
    gg::test::StubPool pool(workers);
    Rendezvous rv(workers + 1);
    pool.run(rv.expected, rendezvousTask, &rv);
    CHECK(rv.arrived.load() == rv.expected);
    CHECK(rv.metAll.load() == rv.expected);
}

TEST_CASE("the stub pool declining every other call still runs every task", "[engine][threads]")
{
    const unsigned workers = GENERATE(1u, 2u, 8u);
    CAPTURE(workers);
    gg::test::StubPool pool(workers, /*declineEveryOther=*/true);
    Record r;

    for (uint32_t call = 1; call <= 6; ++call)
    {
        CAPTURE(call);
        r.reset();
        pool.run(7, slowTask, &r);
        for (uint32_t i = 0; i < 7; ++i)
        {
            CAPTURE(i);
            CHECK(r.done[i].load());
            CHECK(r.hits[i].load() == 1);
        }
        if (call % 2 == 0)
        {
            // Declined: the whole list ran on the caller's thread, as CLAP's fallback says.
            for (uint32_t i = 0; i < 7; ++i)
                CHECK(r.thread[i] == std::this_thread::get_id());
        }
    }

    // An accepted call uses the workers: the rendezvous needs every thread at once. The next
    // call is the 7th, which is accepted.
    Rendezvous rv(workers + 1);
    pool.run(rv.expected, rendezvousTask, &rv);
    CHECK(rv.metAll.load() == rv.expected);
}
