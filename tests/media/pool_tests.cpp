// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The process-wide sample pool (gg/media/SamplePool.h, spec §6.5): prepared sources shared by
// (fingerprint, size, rate) and held weakly. Each test case runs in its own process, so the pool
// starts empty; each clears it anyway.

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/media/Sample.h"
#include "gg/media/SamplePool.h"
#include "gg/source/SampleBuffer.h"

namespace
{
using gg::media::PoolKey;
using gg::media::SamplePool;

std::shared_ptr<gg::SampleBuffer> stereoBuffer(double rate)
{
    auto buffer = std::make_shared<gg::SampleBuffer>(2, 4410, rate);
    for (uint32_t c = 0; c < 2; ++c)
    {
        for (int64_t n = 0; n < buffer->frames(); ++n)
        {
            buffer->channel(c)[n] = float(std::sin(0.01 * double(n) + c));
        }
    }
    return buffer;
}

const PoolKey kKey{std::string(64, 'a'), 17640, 48000.0};
} // namespace

TEST_CASE("prepare with one key shares one source", "[media]")
{
    auto &pool = SamplePool::process();
    pool.clear();
    const auto decoded = stereoBuffer(44100.0);
    const auto first = gg::media::prepare(decoded, 48000.0, nullptr, &kKey);
    const auto second = gg::media::prepare(decoded, 48000.0, nullptr, &kKey);
    REQUIRE(first != nullptr);
    REQUIRE(first == second);
    REQUIRE(pool.alive() == 1);
    REQUIRE(pool.find(kKey) == first);

    // Another instance's decode of the same file shares it too, unconverted and unbuilt.
    const auto again = gg::media::prepare(stereoBuffer(44100.0), 48000.0, nullptr, &kKey);
    REQUIRE(again == first);
    // Without a key nothing is shared.
    const auto unshared = gg::media::prepare(decoded, 48000.0);
    REQUIRE(unshared != first);
    REQUIRE(pool.alive() == 1);
    pool.clear();
}

TEST_CASE("the pool's key compares the fingerprint, the size and the rate", "[media]")
{
    auto &pool = SamplePool::process();
    pool.clear();
    const auto decoded = stereoBuffer(44100.0);
    const auto at48 = gg::media::prepare(decoded, 48000.0, nullptr, &kKey);

    PoolKey otherRate = kKey;
    otherRate.rate = 96000.0;
    const auto at96 = gg::media::prepare(decoded, 96000.0, nullptr, &otherRate);
    REQUIRE(at96 != at48);
    REQUIRE(at96->buffer().sampleRate() == 96000.0);

    // The rate bit for bit: the next double up is another rate.
    PoolKey nextRate = kKey;
    nextRate.rate = std::nextafter(48000.0, 1e9);
    REQUIRE(pool.find(nextRate) == nullptr);
    PoolKey otherHash = kKey;
    otherHash.sha256Hex[0] = 'b';
    REQUIRE(pool.find(otherHash) == nullptr);
    PoolKey otherSize = kKey;
    otherSize.fileSize += 1;
    REQUIRE(pool.find(otherSize) == nullptr);

    REQUIRE(pool.alive() == 2);
    pool.clear();
    REQUIRE(pool.alive() == 0);
}

TEST_CASE("the pool holds nothing alive once its holders let go", "[media]")
{
    auto &pool = SamplePool::process();
    pool.clear();
    auto first = gg::media::prepare(stereoBuffer(48000.0), 48000.0, nullptr, &kKey);
    auto second = pool.find(kKey);
    REQUIRE(second == first);
    REQUIRE(pool.alive() == 1);
    first.reset();
    REQUIRE(pool.alive() == 1);
    second.reset();
    REQUIRE(pool.alive() == 0);
    REQUIRE(pool.find(kKey) == nullptr);

    // A new prepare builds afresh, and the pool holds that one.
    const auto rebuilt = gg::media::prepare(stereoBuffer(48000.0), 48000.0, nullptr, &kKey);
    REQUIRE(pool.find(kKey) == rebuilt);
    REQUIRE(pool.alive() == 1);
    pool.clear();
}

TEST_CASE("insert keeps the source weakly and does not replace a live entry of the same key",
          "[media]")
{
    auto &pool = SamplePool::process();
    pool.clear();
    auto a = gg::PreparedSource::make(stereoBuffer(48000.0));
    auto b = gg::PreparedSource::make(stereoBuffer(48000.0));
    REQUIRE(pool.insert(kKey, a) == a);
    REQUIRE(pool.find(kKey) == a);
    // The second insert gets the first's source back, and the pool still holds the first.
    REQUIRE(pool.insert(kKey, b) == a);
    REQUIRE(pool.find(kKey) == a);
    REQUIRE(pool.alive() == 1);
    // Once the first has gone, the key is free again.
    a.reset();
    REQUIRE(pool.alive() == 0);
    REQUIRE(pool.insert(kKey, b) == b);
    REQUIRE(pool.find(kKey) == b);
    REQUIRE(pool.alive() == 1);
    b.reset();
    REQUIRE(pool.alive() == 0);
    pool.clear();
}

TEST_CASE("concurrent prepares of one key end up sharing one source", "[media]")
{
    auto &pool = SamplePool::process();
    pool.clear();
    constexpr int kThreads = 8;
    std::vector<std::shared_ptr<const gg::PreparedSource>> got(kThreads);
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&, t] {
            const auto decoded = stereoBuffer(44100.0);
            while (!go.load())
            {
                std::this_thread::yield();
            }
            got[std::size_t(t)] = gg::media::prepare(decoded, 48000.0, nullptr, &kKey);
        });
    }
    go.store(true);
    for (auto &thread : threads)
    {
        thread.join();
    }
    for (const auto &source : got)
    {
        REQUIRE(source != nullptr);
        REQUIRE(source == got[0]);
    }
    REQUIRE(pool.find(kKey) == got[0]);
    REQUIRE(pool.alive() == 1);
    pool.clear();
}
