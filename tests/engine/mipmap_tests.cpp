// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gg/source/InterleavedStereo.h"
#include "gg/source/Mipmap.h"
#include "gg/source/PreparedSource.h"
#include "gg/source/SampleBuffer.h"

namespace
{
// A sine at `hz` in every channel, channel c phase-shifted by c/4 turn.
std::shared_ptr<gg::SampleBuffer> makeSine(uint32_t channels, int64_t frames, double hz = 440.0)
{
    constexpr double kRate = 48000.0;
    auto buf = std::make_shared<gg::SampleBuffer>(channels, frames, kRate);
    for (uint32_t c = 0; c < channels; ++c)
    {
        for (int64_t i = 0; i < frames; ++i)
        {
            const double phase = 2.0 * std::numbers::pi * (hz * i / kRate + 0.25 * c);
            buf->channel(c)[i] = static_cast<float>(0.8 * std::sin(phase));
        }
    }
    return buf;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// The taps' magnitude response in dB at f cycles per input sample.
double responseDb(double f)
{
    std::complex<double> sum{0.0, 0.0};
    for (std::size_t k = 0; k < gg::kHalfband.size(); ++k)
    {
        sum += static_cast<double>(gg::kHalfband[k]) *
               std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(k));
    }
    return 20.0 * std::log10(std::abs(sum));
}

// The half-band sum written out by hand, as Mipmap.h states it: out[m] = Σ h[k] × in[2m + k − 27]
// for k = 0 … 54 in order, in float, taps outside [0, inFrames) zero.
float halfbandByHand(const float *in, int64_t inFrames, int64_t inStride, int64_t m)
{
    float sum = 0.0f;
    for (int64_t k = 0; k < gg::kHalfbandTaps; ++k)
    {
        const int64_t i = 2 * m + k - (gg::kHalfbandTaps - 1) / 2;
        const float x = (i >= 0 && i < inFrames) ? in[inStride * i] : 0.0f;
        const float product = gg::kHalfband[static_cast<std::size_t>(k)] * x;
        sum = sum + product;
    }
    return sum;
}
} // namespace

TEST_CASE("the half-band response meets its pins", "[source][interp]")
{
    // Spec §6.3: within 0.1 dB of unity to 0.4 of the level's new rate (0.2 of the input rate),
    // at least 80 dB down from 0.6 of it (0.3 of the input rate) to the input's Nyquist.
    REQUIRE(gg::kHalfbandTaps == 55);
    REQUIRE(gg::kHalfband[27] == 0.5f);
    for (std::size_t k = 0; k < gg::kHalfband.size(); ++k)
    {
        INFO("tap " << k);
        REQUIRE(gg::kHalfband[k] == gg::kHalfband[gg::kHalfband.size() - 1 - k]); // linear phase
        if (k != 27 && (k % 2) == 1)
        {
            REQUIRE(gg::kHalfband[k] == 0.0f); // a half-band's even offsets are zero
        }
    }
    constexpr int kPoints = 4000;
    for (int j = 0; j <= kPoints; ++j)
    {
        const double pass = 0.2 * j / kPoints;
        INFO("passband f = " << pass);
        REQUIRE(std::abs(responseDb(pass)) <= 0.1);
    }
    for (int j = 0; j <= kPoints; ++j)
    {
        const double stop = 0.3 + 0.2 * j / kPoints;
        INFO("stopband f = " << stop);
        REQUIRE(responseDb(stop) <= -80.0);
    }
}

TEST_CASE("the level count follows the length", "[source][interp]")
{
    // A level is built while it would hold at least kMinLevelFrames frames, up to level 4.
    REQUIRE(gg::kMinLevelFrames == 64);
    REQUIRE(gg::kMaxMipLevels == 5);
    CHECK(gg::mipLevelCount(0) == 1);
    CHECK(gg::mipLevelCount(63) == 1);
    CHECK(gg::mipLevelCount(64) == 1);
    CHECK(gg::mipLevelCount(127) == 2); // ceil(127 / 2) = 64
    CHECK(gg::mipLevelCount(128) == 2);
    CHECK(gg::mipLevelCount(1000) == 4); // ceil(1000 / 16) = 63 < 64
    CHECK(gg::mipLevelCount(1008) == 4);
    CHECK(gg::mipLevelCount(1009) == 5); // ceil(1009 / 16) = 64
    CHECK(gg::mipLevelCount(1024) == 5);
    CHECK(gg::mipLevelCount(2000) == 5);
    CHECK(gg::mipLevelCount(int64_t{1} << 40) == 5);
    CHECK(gg::mipLevelFrames(1000, 0) == 1000);
    CHECK(gg::mipLevelFrames(1000, 3) == 125);
    CHECK(gg::mipLevelFrames(1000, 4) == 63);
    CHECK(gg::mipLevelFrames(1001, 1) == 501);
}

TEST_CASE("levels are built from the one above with the expected frames", "[source][interp]")
{
    // 2,002 frames: odd lengths from level 1 on, so each level's ceil shows.
    const auto source = gg::PreparedSource::make(makeSine(2, 2002, 3000.0));
    REQUIRE(source->levelCount() == 5);
    const int64_t expected[5] = {2002, 1001, 501, 251, 126};
    for (uint32_t n = 0; n < 5; ++n)
    {
        INFO("level " << n);
        REQUIRE(source->level(n).frames == expected[n]);
        REQUIRE(source->level(n).decimation == (1u << n));
    }

    // Level 1 from level 0's planar channels, and each level above from the pairs of the one
    // below, sum by sum.
    for (uint32_t n = 1; n < 5; ++n)
    {
        const gg::PreparedSource::Level &above = source->level(n - 1);
        const gg::PreparedSource::Level &level = source->level(n);
        for (int c = 0; c < 2; ++c)
        {
            for (int64_t m = 0; m < level.frames; ++m)
            {
                INFO("level " << n << " channel " << c << " frame " << m);
                const float want = halfbandByHand(above.data[c], above.frames, above.stride, m);
                REQUIRE(sameBits(level.data[c][level.stride * m], want));
            }
        }
    }

    // decimateHalfband itself gives the same sums.
    const gg::PreparedSource::Level &level0 = source->level(0);
    std::vector<float> out(1001);
    gg::decimateHalfband(level0.data[1], level0.frames, 1, out.data(), 1001, 1);
    for (int64_t m = 0; m < 1001; ++m)
    {
        INFO("frame " << m);
        REQUIRE(
            sameBits(out[static_cast<std::size_t>(m)], halfbandByHand(level0.data[1], 2002, 1, m)));
    }

    // A 3 kHz sine is far inside the passband: the level holds it, at its gain.
    const gg::PreparedSource::Level &level1 = source->level(1);
    float peak = 0.0f;
    for (int64_t m = 100; m < 900; ++m)
    {
        peak = std::max(peak, std::abs(level1.data[0][2 * m]));
    }
    CHECK(std::abs(peak - 0.8f) < 0.01f);
}

TEST_CASE("a mono source's levels are planar and a stereo source's interleaved", "[source][interp]")
{
    const auto mono = gg::PreparedSource::make(makeSine(1, 2000));
    const auto stereo = gg::PreparedSource::make(makeSine(2, 2000));
    REQUIRE(mono->levelCount() == 5);
    REQUIRE(stereo->levelCount() == 5);

    // Level 0 is the source as the engine read it before the pyramid.
    const gg::PreparedSource::Level &m0 = mono->level(0);
    CHECK(m0.data[0] == mono->buffer().channel(0));
    CHECK(m0.data[1] == mono->buffer().channel(0));
    CHECK(m0.frames == 2000);
    CHECK(m0.stride == 1);
    CHECK(m0.interleaved == nullptr);
    CHECK(m0.mono);
    CHECK(m0.decimation == 1);
    const gg::PreparedSource::Level &s0 = stereo->level(0);
    CHECK(s0.data[0] == stereo->buffer().channel(0));
    CHECK(s0.data[1] == stereo->buffer().channel(1));
    CHECK(s0.frames == 2000);
    CHECK(s0.stride == 1);
    CHECK(s0.interleaved == stereo->pairs().data());
    CHECK(s0.interleaved != nullptr);
    CHECK_FALSE(s0.mono);
    CHECK(s0.decimation == 1);

    for (uint32_t n = 1; n < 5; ++n)
    {
        INFO("level " << n);
        const gg::PreparedSource::Level &m = mono->level(n);
        CHECK(m.data[0] != nullptr);
        CHECK(m.data[1] == m.data[0]);
        CHECK(m.stride == 1);
        CHECK(m.interleaved == nullptr);
        CHECK(m.mono);
        CHECK(m.decimation == (1u << n));
        const gg::PreparedSource::Level &s = stereo->level(n);
        CHECK(s.data[0] != nullptr);
        CHECK(s.data[1] == s.data[0] + 1);
        CHECK(s.stride == 2);
        CHECK(s.interleaved == s.data[0]);
        CHECK_FALSE(s.mono);
        CHECK(s.decimation == (1u << n));
    }
}

TEST_CASE("level(n) past the count is level 0", "[source][interp]")
{
    // 100 frames: level 1 would hold 50 < 64, so level 0 alone.
    const auto short_ = gg::PreparedSource::make(makeSine(2, 100));
    REQUIRE(short_->levelCount() == 1);
    for (const uint32_t n : {1u, 4u, 5u, 0xFFFFFFFFu})
    {
        INFO("n = " << n);
        REQUIRE(&short_->level(n) == &short_->level(0));
    }
    const auto full = gg::PreparedSource::make(makeSine(1, 2000));
    REQUIRE(full->levelCount() == 5);
    REQUIRE(&full->level(4) != &full->level(0));
    for (const uint32_t n : {5u, 7u, 0xFFFFFFFFu})
    {
        INFO("n = " << n);
        REQUIRE(&full->level(n) == &full->level(0));
    }
    // A buffer without channels has one level and nothing to read.
    const auto none =
        gg::PreparedSource::make(std::make_shared<gg::SampleBuffer>(0, 1000, 48000.0));
    REQUIRE(none->levelCount() == 1);
    CHECK(none->level(0).frames == 0);
    CHECK(none->level(0).data[0] == nullptr);
    CHECK(none->level(0).interleaved == nullptr);
    CHECK(none->level(3).frames == 0);
}

TEST_CASE("bytes counts every copy", "[source][interp]")
{
    constexpr std::size_t g = gg::InterleavedStereo::kGuardFrames;
    constexpr std::size_t f = sizeof(float);
    const std::size_t levels[] = {1000, 500, 250, 125};

    // Mono: the buffer, then one planar copy per level, each with its guards.
    std::size_t mono = 2000;
    for (const std::size_t n : levels)
    {
        mono += n + 2 * g;
    }
    CHECK(gg::PreparedSource::make(makeSine(1, 2000))->bytes() == mono * f);

    // Stereo: the buffer's two channels, the pairs, then one interleaved copy per level.
    std::size_t stereo = 2 * 2000 + 2 * (2000 + 2 * g);
    for (const std::size_t n : levels)
    {
        stereo += 2 * (n + 2 * g);
    }
    CHECK(gg::PreparedSource::make(makeSine(2, 2000))->bytes() == stereo * f);

    // Three channels: the buffer holds all three; the pairs and levels only the first two.
    CHECK(gg::PreparedSource::make(makeSine(3, 2000))->bytes() == (stereo + 2000) * f);

    // Level 0 alone, and nothing at all.
    CHECK(gg::PreparedSource::make(makeSine(1, 100))->bytes() == 100 * f);
    CHECK(gg::PreparedSource::make(std::make_shared<gg::SampleBuffer>(0, 1000, 48000.0))->bytes() ==
          0);
}

TEST_CASE("a level's guard frames are zero", "[source][interp]")
{
    // A constant source: every level's frames are near 1, so a guard frame that isn't zero shows.
    for (const uint32_t channels : {1u, 2u})
    {
        auto buffer = std::make_shared<gg::SampleBuffer>(channels, 2000, 48000.0);
        for (uint32_t c = 0; c < channels; ++c)
        {
            for (int64_t i = 0; i < 2000; ++i)
            {
                buffer->channel(c)[i] = 1.0f;
            }
        }
        const auto source = gg::PreparedSource::make(buffer);
        REQUIRE(source->levelCount() == 5);
        constexpr int64_t g = gg::InterleavedStereo::kGuardFrames;
        for (uint32_t n = 1; n < source->levelCount(); ++n)
        {
            const gg::PreparedSource::Level &level = source->level(n);
            for (uint32_t c = 0; c < channels; ++c)
            {
                INFO("channels " << channels << " level " << n << " channel " << c);
                for (int64_t k = 1; k <= g; ++k)
                {
                    REQUIRE(level.data[c][-level.stride * k] == 0.0f);
                    REQUIRE(level.data[c][level.stride * (level.frames - 1 + k)] == 0.0f);
                }
                REQUIRE(level.data[c][level.stride * (level.frames / 2)] > 0.99f);
            }
        }
    }
}

TEST_CASE("make stops and returns nullptr once the cancel flag is set", "[source][interp]")
{
    std::atomic<bool> cancel{false};
    const auto buffer = makeSine(2, 4000);

    // A flag that is never set changes nothing: the same levels as with no flag.
    const auto plain = gg::PreparedSource::make(buffer);
    const auto unset = gg::PreparedSource::make(buffer, &cancel);
    REQUIRE(unset != nullptr);
    REQUIRE(unset->levelCount() == plain->levelCount());
    REQUIRE(unset->levelCount() > 1);
    for (uint32_t n = 0; n < plain->levelCount(); ++n)
    {
        const gg::PreparedSource::Level &a = plain->level(n);
        const gg::PreparedSource::Level &b = unset->level(n);
        REQUIRE(a.frames == b.frames);
        REQUIRE(std::memcmp(a.data[0], b.data[0],
                            sizeof(float) * static_cast<std::size_t>(a.frames) *
                                static_cast<std::size_t>(a.stride)) == 0);
    }

    // Set, it builds nothing: what a null buffer gives.
    cancel.store(true);
    REQUIRE(gg::PreparedSource::make(buffer, &cancel) == nullptr);
    REQUIRE(gg::PreparedSource::make(nullptr, &cancel) == nullptr);
}
