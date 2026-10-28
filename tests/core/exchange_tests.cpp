// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The SourceExchange (spec §6.4): the main thread offers prepared sources, the audio thread polls
// the newest and hands back the ones no grain reads, and only the main thread frees them.

#include <cstdint>
#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "gg/core/SourceExchange.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/SampleBuffer.h"
#include "support/AllocTripwire.h"

using gg::PreparedSource;
using gg::core::SourceExchange;
using gg::test::AllocTripwire;

namespace
{
std::shared_ptr<const PreparedSource> makeSource(uint32_t channels = 2)
{
    return PreparedSource::make(std::make_shared<gg::SampleBuffer>(channels, 64, 48000.0));
}
} // namespace

TEST_CASE("exchange hands an offer to the audio side once", "[core]")
{
    SourceExchange exchange;
    const PreparedSource *polled = nullptr;
    CHECK_FALSE(exchange.poll(polled)); // nothing offered yet

    auto a = makeSource();
    const PreparedSource *raw = a.get();
    exchange.offer(std::move(a));
    CHECK(exchange.held() == 1);
    REQUIRE(exchange.poll(polled));
    CHECK(polled == raw);
    CHECK_FALSE(exchange.poll(polled)); // once
}

TEST_CASE("exchange keeps the newest offer and frees the one never taken", "[core]")
{
    SourceExchange exchange;
    auto a = makeSource();
    auto b = makeSource(1);
    const std::weak_ptr<const PreparedSource> weakA = a;
    const PreparedSource *rawB = b.get();
    exchange.offer(std::move(a));
    exchange.offer(std::move(b));
    // A was never polled, so the audio side can't have it: the main thread frees it at once.
    CHECK(weakA.expired());
    CHECK(exchange.held() == 1);

    const PreparedSource *polled = nullptr;
    REQUIRE(exchange.poll(polled));
    CHECK(polled == rawB);
    CHECK_FALSE(exchange.poll(polled));
}

TEST_CASE("exchange offers no source as nullptr", "[core]")
{
    // A restored session whose sample is missing leaves the instrument silent (spec §6.4): the
    // offer of no source must reach the audio side, distinct from no offer at all.
    SourceExchange exchange;
    exchange.offer(nullptr);
    CHECK(exchange.held() == 0);
    const PreparedSource *polled = makeSource().get();
    REQUIRE(exchange.poll(polled));
    CHECK(polled == nullptr);
    CHECK_FALSE(exchange.poll(polled));
    exchange.retire(nullptr); // ignored
    exchange.collect();
    CHECK(exchange.held() == 0);
}

TEST_CASE("exchange frees a retired source on collect, in order, never on the audio side", "[core]")
{
    SourceExchange exchange;
    auto a = makeSource();
    auto b = makeSource();
    const std::weak_ptr<const PreparedSource> weakA = a;
    const std::weak_ptr<const PreparedSource> weakB = b;
    exchange.offer(std::move(a));

    // The audio side's calls neither allocate nor free.
    const PreparedSource *first = nullptr;
    bool polledFirst = false;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        polledFirst = exchange.poll(first);
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    REQUIRE(polledFirst);

    exchange.offer(std::move(b));
    const PreparedSource *second = nullptr;
    bool polledSecond = false;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        polledSecond = exchange.poll(second);
        exchange.retire(first);
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    REQUIRE(polledSecond);
    // Retired, but still alive: only collect() frees it, on the main thread.
    CHECK_FALSE(weakA.expired());
    CHECK(exchange.held() == 2);

    exchange.collect();
    CHECK(weakA.expired());
    CHECK_FALSE(weakB.expired());
    CHECK(exchange.held() == 1);

    exchange.retire(second);
    CHECK_FALSE(weakB.expired());
    exchange.collect();
    CHECK(weakB.expired());
    CHECK(exchange.held() == 0);
}

TEST_CASE("exchange frees what it still holds when destroyed", "[core]")
{
    std::weak_ptr<const PreparedSource> polledWeak;
    std::weak_ptr<const PreparedSource> offeredWeak;
    {
        SourceExchange exchange;
        auto a = makeSource();
        polledWeak = a;
        exchange.offer(std::move(a));
        const PreparedSource *polled = nullptr;
        REQUIRE(exchange.poll(polled)); // taken, never retired: the engine still read it
        auto b = makeSource();
        offeredWeak = b;
        exchange.offer(std::move(b)); // never taken
        CHECK_FALSE(polledWeak.expired());
    }
    CHECK(polledWeak.expired());
    CHECK(offeredWeak.expired());
}
