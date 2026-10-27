// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "gg/source/SampleBuffer.h"

TEST_CASE("sample buffer holds separate zeroed channels", "[source]")
{
    gg::SampleBuffer buf(2, 100, 44100.0);
    REQUIRE(buf.channels() == 2);
    REQUIRE(buf.frames() == 100);
    REQUIRE(buf.sampleRate() == 44100.0);

    bool allZero = true;
    for (uint32_t c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < 100; ++i)
        {
            allZero = allZero && buf.channel(c)[i] == 0.0f;
        }
    }
    REQUIRE(allZero);

    for (int64_t i = 0; i < 100; ++i)
    {
        buf.channel(0)[i] = 1.0f;
    }
    bool rightUntouched = true;
    for (int64_t i = 0; i < 100; ++i)
    {
        rightUntouched = rightUntouched && buf.channel(1)[i] == 0.0f;
    }
    REQUIRE(rightUntouched);

    const gg::SampleBuffer &cbuf = buf;
    REQUIRE(cbuf.channel(0) == buf.channel(0));
    REQUIRE(cbuf.channel(0)[99] == 1.0f);
    REQUIRE(buf.channel(2) == nullptr);
    REQUIRE(cbuf.channel(2) == nullptr);
}

TEST_CASE("sample buffer accepts empty shapes and rejects invalid ones", "[source]")
{
    const gg::SampleBuffer empty(2, 0, 48000.0);
    REQUIRE(empty.channels() == 2);
    REQUIRE(empty.frames() == 0);

    const gg::SampleBuffer noChannels(0, 10, 48000.0);
    REQUIRE(noChannels.channel(0) == nullptr);

    REQUIRE_THROWS_AS(gg::SampleBuffer(1, -1, 48000.0), std::invalid_argument);
    REQUIRE_THROWS_AS(gg::SampleBuffer(1, 10, 0.0), std::invalid_argument);
    REQUIRE_THROWS_AS(gg::SampleBuffer(1, 10, -48000.0), std::invalid_argument);
    REQUIRE_THROWS_AS(gg::SampleBuffer(1, 10, std::numeric_limits<double>::quiet_NaN()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(gg::SampleBuffer(1, 10, std::numeric_limits<double>::infinity()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(gg::SampleBuffer(std::numeric_limits<uint32_t>::max(),
                                       std::numeric_limits<int64_t>::max(), 48000.0),
                      std::length_error);
}
