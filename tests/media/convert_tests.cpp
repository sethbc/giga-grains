// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numbers>
#include <optional>

#if !defined(_WIN32)
#include <sys/resource.h>
#endif

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "gg/media/Convert.h"

namespace
{
using gg::SampleBuffer;
using gg::media::convertRate;

/// `frames` frames of amplitude × sin(2π · hz · n / rate) in every channel, channel c's phase
/// advanced by c radians so the channels differ.
std::shared_ptr<SampleBuffer> sine(uint32_t channels, double rate, int64_t frames, double hz,
                                   double amplitude)
{
    auto buffer = std::make_shared<SampleBuffer>(channels, frames, rate);
    for (uint32_t c = 0; c < channels; ++c)
    {
        float *x = buffer->channel(c);
        for (int64_t n = 0; n < frames; ++n)
        {
            x[n] = static_cast<float>(
                amplitude * std::sin(2.0 * std::numbers::pi * hz * double(n) / rate + double(c)));
        }
    }
    return buffer;
}

int64_t expectedFrames(int64_t frames, double from, double to)
{
    return std::llround(double(frames) * to / from);
}

/// The RMS difference between channel 0 of `out` and the ideal 0.5 · sin(2π · hz · n / rate),
/// over [from, to) seconds.
double sineRmsError(const SampleBuffer &out, double hz, double from, double to)
{
    const double rate = out.sampleRate();
    const auto begin = static_cast<int64_t>(from * rate);
    const auto end = static_cast<int64_t>(to * rate);
    const float *y = out.channel(0);
    double sum = 0.0;
    for (int64_t n = begin; n < end; ++n)
    {
        const double ideal = 0.5 * std::sin(2.0 * std::numbers::pi * hz * double(n) / rate);
        const double e = double(y[n]) - ideal;
        sum += e * e;
    }
    return std::sqrt(sum / double(end - begin));
}

/// The process's peak resident memory so far, in bytes; nullopt where it isn't measured
/// (Windows). Each Catch2 test case runs in a process of its own (CLAUDE.md), so this is the
/// test's.
std::optional<uint64_t> peakResidentBytes()
{
#if defined(_WIN32)
    return std::nullopt;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
    {
        return std::nullopt;
    }
#if defined(__APPLE__)
    return uint64_t(usage.ru_maxrss); // bytes
#else
    return uint64_t(usage.ru_maxrss) * 1024; // kilobytes
#endif
#endif
}

bool identical(const SampleBuffer &a, const SampleBuffer &b, uint32_t ca, uint32_t cb)
{
    return a.frames() == b.frames() &&
           std::memcmp(a.channel(ca), b.channel(cb), size_t(a.frames()) * sizeof(float)) == 0;
}
} // namespace

TEST_CASE("convertRate returns the same buffer when the rate already matches", "[media]")
{
    const auto in = sine(2, 48000.0, 1000, 1000.0, 0.5);
    REQUIRE(convertRate(in, 48000.0) == in);
}

TEST_CASE("convertRate 44.1 kHz to 48 kHz keeps a 1 kHz sine, aligned at frame 0", "[media]")
{
    const auto in = sine(1, 44100.0, 2 * 44100, 1000.0, 0.5);
    const auto out = convertRate(in, 48000.0);
    REQUIRE(out != in);
    REQUIRE(out->channels() == 1);
    REQUIRE(out->sampleRate() == 48000.0);
    REQUIRE(out->frames() == 96000);
    const double error = sineRmsError(*out, 1000.0, 0.1, 1.9);
    INFO("RMS error " << error);
    REQUIRE(error < 1e-4);
}

TEST_CASE("convertRate gives llround(frames x to / from) frames", "[media]")
{
    struct Case
    {
        double from, to;
        int64_t frames;
    };
    const auto c = GENERATE(Case{96000.0, 48000.0, 96000}, Case{96000.0, 48000.0, 96001},
                            Case{48000.0, 44100.0, 48000}, Case{48000.0, 44100.0, 48007},
                            Case{44100.0, 48000.0, 44101});
    const auto out = convertRate(sine(1, c.from, c.frames, 1000.0, 0.5), c.to);
    INFO(c.from << " -> " << c.to << ", " << c.frames << " frames");
    REQUIRE(out->sampleRate() == c.to);
    REQUIRE(out->frames() == expectedFrames(c.frames, c.from, c.to));
    // Frame 0 stays aligned whichever way the rate goes (every case is at least 1 s long).
    REQUIRE(sineRmsError(*out, 1000.0, 0.1, 0.9) < 1e-4);
}

TEST_CASE("convertRate 96 kHz to 48 kHz removes a 30 kHz tone, without aliasing", "[media]")
{
    // A full-scale 30 kHz tone would alias to 18 kHz. Away from the edges, where the tone starts
    // and stops abruptly, every sample must stay below -90 dBFS.
    const auto out = convertRate(sine(1, 96000.0, 96000, 30000.0, 1.0), 48000.0);
    REQUIRE(out->frames() == 48000);
    const float *y = out->channel(0);
    float peak = 0.0f;
    for (int64_t n = 4800; n < 48000 - 4800; ++n)
    {
        peak = std::max(peak, std::abs(y[n]));
    }
    const double dbfs = 20.0 * std::log10(std::max(double(peak), 1e-30));
    INFO("peak " << dbfs << " dBFS");
    REQUIRE(dbfs < -90.0);
}

TEST_CASE("convertRate is deterministic", "[media]")
{
    const auto in = sine(2, 44100.0, 30000, 440.0, 0.7);
    const auto a = convertRate(in, 48000.0);
    const auto b = convertRate(in, 48000.0);
    REQUIRE(a != b);
    REQUIRE(identical(*a, *b, 0, 0));
    REQUIRE(identical(*a, *b, 1, 1));
}

TEST_CASE("convertRate converts each stereo channel as it would alone", "[media]")
{
    const auto stereo = sine(2, 48000.0, 20000, 440.0, 0.7);
    const auto out = convertRate(stereo, 44100.0);
    REQUIRE(out->channels() == 2);
    for (uint32_t c = 0; c < 2; ++c)
    {
        auto mono = std::make_shared<SampleBuffer>(1, stereo->frames(), stereo->sampleRate());
        std::copy_n(stereo->channel(c), stereo->frames(), mono->channel(0));
        const auto alone = convertRate(mono, 44100.0);
        INFO("channel " << c);
        REQUIRE(identical(*out, *alone, c, 0));
    }
}

TEST_CASE("convertRate keeps at least one frame of a buffer that has any", "[media]")
{
    // 4 frames at 384 kHz are 0.46 frames at 44.1 kHz: rounded to 0, a file that decoded would
    // give an empty source. One frame is kept instead.
    const auto out = convertRate(sine(2, 384000.0, 4, 1000.0, 0.5), 44100.0);
    REQUIRE(out->channels() == 2);
    REQUIRE(out->frames() == 1);
    REQUIRE(out->sampleRate() == 44100.0);
    REQUIRE(std::isfinite(out->channel(0)[0]));
    REQUIRE(std::isfinite(out->channel(1)[0]));
}

TEST_CASE("convertRate of an empty buffer is empty at the new rate", "[media]")
{
    const auto out = convertRate(std::make_shared<SampleBuffer>(2, 0, 44100.0), 48000.0);
    REQUIRE(out->channels() == 2);
    REQUIRE(out->frames() == 0);
    REQUIRE(out->sampleRate() == 48000.0);
}

TEST_CASE("convertRate of a 1 Hz file to 192 kHz and 768 kHz keeps its memory bounded",
          "[media][serial]")
{
    // r8brain's buffers grow with its MaxInLen × the rate ratio. At a fixed MaxInLen of 4096 a
    // 1 Hz file (600 frames: 600 s, M1's longest file) needed 6.7 GB to reach 192 kHz, and at
    // 768 kHz the lengths overflowed int. The output alone is what it must cost, and a margin.
    const double to = GENERATE(192000.0, 768000.0);
    const int64_t frames = 600;
    auto in = std::make_shared<SampleBuffer>(1, frames, 1.0);
    std::fill_n(in->channel(0), frames, 0.25f);
    const auto peakBefore = peakResidentBytes();
    const auto out = convertRate(in, to);
    const auto peakAfter = peakResidentBytes();
    INFO("to " << to << " Hz");
    REQUIRE(out->frames() == expectedFrames(frames, 1.0, to));
    REQUIRE(out->sampleRate() == to);
    // The middle of a constant input converts to the constant.
    REQUIRE(std::abs(out->channel(0)[out->frames() / 2] - 0.25f) < 1e-3f);
    if (peakBefore && peakAfter)
    {
        const uint64_t outputBytes = uint64_t(out->frames()) * sizeof(float);
        const uint64_t grew = *peakAfter > *peakBefore ? *peakAfter - *peakBefore : 0;
        INFO("peak grew by " << grew / 1e6 << " MB for " << outputBytes / 1e6 << " MB of output");
        REQUIRE(grew < outputBytes + (512ull << 20));
    }
}

TEST_CASE("convertRate stops once its cancel flag is set", "[media]")
{
    // The sample loader's prompt stop (carry-over [194]). A flag never set changes nothing.
    const auto in = sine(2, 44100.0, 44100, 440.0, 0.5);
    std::atomic<bool> cancel{false};
    const auto out = convertRate(in, 48000.0, &cancel);
    const auto plain = convertRate(in, 48000.0);
    REQUIRE(out != nullptr);
    REQUIRE(plain != nullptr);
    CHECK(identical(*out, *plain, 0, 0));
    CHECK(identical(*out, *plain, 1, 1));
    cancel = true;
    CHECK(convertRate(in, 48000.0, &cancel) == nullptr);
    CHECK(convertRate(in, 44100.0, &cancel) == in); // nothing to convert
}
