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
using gg::core::SourceOffer;
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
    SourceOffer polled;
    CHECK_FALSE(exchange.poll(polled)); // nothing offered yet

    auto a = makeSource();
    const PreparedSource *raw = a.get();
    exchange.offer(std::move(a));
    CHECK(exchange.held() == 1);
    REQUIRE(exchange.poll(polled));
    CHECK(polled.source == raw);
    CHECK_FALSE(polled.hasValues);
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

    SourceOffer polled;
    REQUIRE(exchange.poll(polled));
    CHECK(polled.source == rawB);
    CHECK_FALSE(exchange.poll(polled));
}

TEST_CASE("exchange offers no source as nullptr", "[core]")
{
    // A restored session whose sample is missing leaves the instrument silent (spec §6.4): the
    // offer of no source must reach the audio side, distinct from no offer at all.
    SourceExchange exchange;
    exchange.offer(nullptr);
    CHECK(exchange.held() == 0);
    SourceOffer polled;
    polled.source = makeSource().get();
    REQUIRE(exchange.poll(polled));
    CHECK(polled.source == nullptr);
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
    SourceOffer first;
    bool polledFirst = false;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        polledFirst = exchange.poll(first);
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    REQUIRE(polledFirst);

    exchange.offer(std::move(b));
    SourceOffer second;
    bool polledSecond = false;
    AllocTripwire::resetCount();
    {
        AllocTripwire::Scope armed;
        polledSecond = exchange.poll(second);
        exchange.retire(first.source);
    }
    REQUIRE_NO_ALLOCATIONS(AllocTripwire::count());
    REQUIRE(polledSecond);
    // Retired, but still alive: only collect() frees it, on the main thread.
    CHECK_FALSE(weakA.expired());
    CHECK(exchange.held() == 2);
    CHECK(exchange.awaitingCollect());

    exchange.collect();
    CHECK_FALSE(exchange.awaitingCollect());
    CHECK(weakA.expired());
    CHECK_FALSE(weakB.expired());
    CHECK(exchange.held() == 1);

    exchange.retire(second.source);
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
        SourceOffer polled;
        REQUIRE(exchange.poll(polled)); // taken, never retired: the engine still read it
        auto b = makeSource();
        offeredWeak = b;
        exchange.offer(std::move(b)); // never taken
        CHECK_FALSE(polledWeak.expired());
    }
    CHECK(polledWeak.expired());
    CHECK(offeredWeak.expired());
}

TEST_CASE("exchange hands values over with their source, as one offer", "[core]")
{
    // D8: a session restored while active offers its sample and its parameter values together.
    SourceExchange exchange;
    gg::core::ParamValues values{};
    values[0] = 0.25f;
    values[3] = 7.0f;
    auto a = makeSource();
    const PreparedSource *rawA = a.get();
    exchange.offer(std::move(a), &values);
    SourceOffer polled;
    REQUIRE(exchange.poll(polled));
    CHECK(polled.source == rawA);
    REQUIRE(polled.hasValues);
    CHECK(polled.values == values);

    // Taken: a later offer without values brings none.
    exchange.offer(makeSource());
    REQUIRE(exchange.poll(polled));
    CHECK_FALSE(polled.hasValues);
}

TEST_CASE("exchange carries untaken values forward into a newer offer", "[core]")
{
    // A session restored while the audio thread isn't processing, then a load: the load's offer
    // replaces the restore's, whose source is freed, but the restore's values still arrive,
    // with the newest source. Twice over, too.
    SourceExchange exchange;
    gg::core::ParamValues values{};
    values[2] = 3.0f;
    auto a = makeSource();
    const std::weak_ptr<const PreparedSource> weakA = a;
    exchange.offer(std::move(a), &values);
    auto b = makeSource();
    exchange.offer(std::move(b));
    auto c = makeSource();
    const PreparedSource *rawC = c.get();
    exchange.offer(std::move(c));
    CHECK(weakA.expired());
    CHECK(exchange.held() == 1);
    SourceOffer polled;
    REQUIRE(exchange.poll(polled));
    CHECK(polled.source == rawC);
    REQUIRE(polled.hasValues);
    CHECK(polled.values == values);

    // Newer values replace carried ones.
    gg::core::ParamValues newer = values;
    newer[2] = 5.0f;
    exchange.offer(nullptr, &values);
    exchange.offer(nullptr, &newer);
    exchange.offer(nullptr);
    REQUIRE(exchange.poll(polled));
    REQUIRE(polled.hasValues);
    CHECK(polled.values == newer);

    // Once taken, they aren't carried again.
    exchange.offer(nullptr);
    REQUIRE(exchange.poll(polled));
    CHECK_FALSE(polled.hasValues);
}

TEST_CASE("exchange offers values that keep the current source (R19)", "[core]")
{
    // A session restored while active whose sample is the one already playing: only its values
    // go over, and the audio side keeps its source.
    SourceExchange exchange;
    gg::core::ParamValues values{};
    values[1] = 2.0f;
    exchange.offerValues(values);
    CHECK(exchange.held() == 0);
    SourceOffer polled;
    REQUIRE(exchange.poll(polled));
    CHECK(polled.keepSource);
    CHECK(polled.source == nullptr);
    REQUIRE(polled.hasValues);
    CHECK(polled.values == values);

    // A plain offer after it is no keep.
    exchange.offer(nullptr);
    REQUIRE(exchange.poll(polled));
    CHECK_FALSE(polled.keepSource);
    CHECK_FALSE(polled.hasValues);
}

TEST_CASE("exchange passes an untaken source on into a keep-source offer (R19)", "[core]")
{
    // A load offered A and the audio side hasn't taken it when a restore of A's own sample
    // offers its values: A must still reach the audio side, now with the values, or the loader
    // would believe A plays while the engine never got it.
    SourceExchange exchange;
    auto a = makeSource();
    const std::weak_ptr<const PreparedSource> weakA = a;
    const PreparedSource *rawA = a.get();
    exchange.offer(std::move(a));
    gg::core::ParamValues values{};
    values[4] = 1.5f;
    exchange.offerValues(values);
    CHECK_FALSE(weakA.expired()); // not freed: passed on
    CHECK(exchange.held() == 1);
    SourceOffer polled;
    REQUIRE(exchange.poll(polled));
    CHECK(polled.source == rawA);
    CHECK_FALSE(polled.keepSource);
    REQUIRE(polled.hasValues);
    CHECK(polled.values == values);

    // An untaken keep-source offer replaced by another keeps as well, with the newer values.
    gg::core::ParamValues newer = values;
    newer[4] = 2.5f;
    exchange.offerValues(values);
    exchange.offerValues(newer);
    REQUIRE(exchange.poll(polled));
    CHECK(polled.keepSource);
    CHECK(polled.values == newer);

    // An untaken keep-source offer replaced by a source offer: the source, with its values
    // carried forward.
    auto b = makeSource();
    const PreparedSource *rawB = b.get();
    exchange.offerValues(values);
    exchange.offer(std::move(b));
    REQUIRE(exchange.poll(polled));
    CHECK(polled.source == rawB);
    CHECK_FALSE(polled.keepSource);
    REQUIRE(polled.hasValues);
    CHECK(polled.values == values);
}

TEST_CASE("exchange holds a source given to the engine directly until it is retired", "[core]")
{
    SourceExchange exchange;
    auto a = makeSource();
    const std::weak_ptr<const PreparedSource> weakA = a;
    const PreparedSource *raw = exchange.hold(std::move(a));
    CHECK(exchange.held() == 1);
    SourceOffer polled;
    CHECK_FALSE(exchange.poll(polled)); // held, not offered
    CHECK(exchange.hold(nullptr) == nullptr);
    CHECK(exchange.held() == 1);
    exchange.collect();
    CHECK_FALSE(weakA.expired());
    exchange.retire(raw);
    exchange.collect();
    CHECK(weakA.expired());
    CHECK(exchange.held() == 0);
}
