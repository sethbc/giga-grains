// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// M2 sub-project 4's comb exit criterion (its spec §6.1, §10): grains that re-read one material at
// one offset comb. At an infinite stretch (a static Position) and at 1000x, Smear 1 removes the
// comb that Smear 0 leaves. The measure is a cepstral peak: a comb, grains re-read every 1/D,
// shows as a peak in the real cepstrum of the log power spectrum near quefrency 1/D and its
// multiples, and the score is that peak's height over the median in the 10-500 ms band.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Render.h"
#include "RenderSpec.h"
#include "gg/metrics/AudioData.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr std::size_t kWindow = 48000;  ///< One second.
constexpr std::size_t kFftSize = 65536; ///< The window, zero-padded to a power of two.
constexpr double kBandLowSec = 0.010;
constexpr double kBandHighSec = 0.500;
constexpr double kSkipSeconds = 2.0; ///< The first 2 s of the 8 s render settle.

// Pinned scores, in dB (20 log10 of the cepstral peak over the band's median), the mean over the
// six 1 s windows of the last 6 s. Measured on the first run (dev, macOS arm64):
//   static Position: Smear 0 scores 33.89 dB, Smear 1 scores 17.92 dB;
//   1000x stretch:   Smear 0 scores 34.71 dB, Smear 1 scores 17.52 dB.
// The floor is the statistic's own on noise: the largest of about 23,500 cepstral values over the
// median magnitude is about 15 to 18 dB for a spectrum with no comb. The limits sit between: a
// Smear 0 below kCombPresent has lost its comb (the test would prove nothing), a Smear 1 above
// kCombAbsent has kept it.
constexpr double kCombPresent = 28.0;
constexpr double kCombAbsent = 22.0;
constexpr double kGapDb = 6.0;

/// An in-place radix-2 complex FFT (`inverse` leaves the 1/N scaling to the caller).
void fft(std::vector<std::complex<double>> &a, bool inverse)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
        {
            j ^= bit;
        }
        j ^= bit;
        if (i < j)
        {
            std::swap(a[i], a[j]);
        }
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = 2.0 * std::numbers::pi / static_cast<double>(len) * (inverse ? 1 : -1);
        const std::complex<double> step{std::cos(angle), std::sin(angle)};
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w{1.0, 0.0};
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

/// The comb score of one 1 s window starting at `start`, in dB: the largest real-cepstrum value
/// in the 10-500 ms quefrency band over the band's median magnitude.
double combScore(const std::vector<float> &x, std::size_t start)
{
    std::vector<std::complex<double>> a(kFftSize);
    for (std::size_t n = 0; n < kWindow; ++n)
    {
        const double hann =
            0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / kWindow);
        a[n] = static_cast<double>(x[start + n]) * hann;
    }
    fft(a, false);
    for (auto &bin : a)
    {
        bin = std::log(std::norm(bin) + 1e-30);
    }
    fft(a, true); // the log spectrum is real and even: its inverse is the real cepstrum
    const auto lo = static_cast<std::size_t>(kBandLowSec * kSampleRate);
    const auto hi = static_cast<std::size_t>(kBandHighSec * kSampleRate);
    std::vector<double> magnitudes;
    double peak = 0.0;
    for (std::size_t q = lo; q <= hi; ++q)
    {
        const double c = a[q].real() / static_cast<double>(kFftSize);
        peak = std::max(peak, c);
        magnitudes.push_back(std::abs(c));
    }
    std::nth_element(magnitudes.begin(), magnitudes.begin() + magnitudes.size() / 2,
                     magnitudes.end());
    return 20.0 * std::log10(peak / magnitudes[magnitudes.size() / 2]);
}

/// Renders the comb scenario (a noise sample, Size 200 ms, Density 20 Hz: an overlap of 4) with
/// `smear`, and the position moving 0.002 of the sample over the 8 s when `stretched`: 1000x. The
/// score is the mean over the six windows after the first 2 s.
double renderScore(double smear, bool stretched)
{
    const std::string json = std::string{R"({
      "mode": "instrument", "sampleRate": 48000, "durationSeconds": 8.0, "blockSize": 128,
      "seed": 1,
      "sample": {"kind": "noise", "seconds": 4.0, "amplitude": 0.5},
      "params": {"Position": 0.4, "Density": 4.321928094887363, "Size": -2.321928094887362,
                 "Level": -6.0, "A Smear": )"} +
                             std::to_string(smear) + R"(, "A LFO 1 Rate": -3.0,
                 "A LFO 1 Shape": 2},)" +
                             (stretched ? R"("mod": [{"slot": 13, "source": "LFO 1",
                 "target": "Position", "targetLayer": "A", "depth": 0.002,
                 "curve": "Unipolar"}],)"
                                        : "") +
                             R"("notes": [{"time": 0.0, "key": 60, "duration": 8.0}]
    })";
    std::string error;
    auto spec = gg::render::parseRenderSpec(json, error);
    INFO(error);
    REQUIRE(spec.has_value());
    const auto audio = gg::render::render(*spec, nullptr);
    REQUIRE_FALSE(audio.channels.empty());
    const auto &left = audio.channels[0];
    REQUIRE(left.size() >= static_cast<std::size_t>(8.0 * kSampleRate));
    double sum = 0.0;
    int windows = 0;
    for (auto start = static_cast<std::size_t>(kSkipSeconds * kSampleRate);
         start + kWindow <= static_cast<std::size_t>(8.0 * kSampleRate); start += kWindow)
    {
        sum += combScore(left, start);
        ++windows;
    }
    REQUIRE(windows == 6);
    return sum / windows;
}
} // namespace

TEST_CASE("smear removes the comb at infinite and 1000x stretch", "[golden][metrics]")
{
    for (const bool stretched : {false, true})
    {
        const double off = renderScore(0.0, stretched);
        const double on = renderScore(1.0, stretched);
        CAPTURE(stretched, off, on);
        CHECK(off > kCombPresent);
        CHECK(on < kCombAbsent);
        CHECK(off - on >= kGapDb);
    }
}
