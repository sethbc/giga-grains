// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_METRICS_SRC_ANALYSIS_H
#define GG_METRICS_SRC_ANALYSIS_H

// Private to gg-audiometrics: the pieces the band profile (Metrics.cpp) and the spectrogram
// (Spectrogram.cpp) share, so both read the signal and frame it the same way.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <numbers>
#include <vector>

#include <pffft/pffft.h>

#include "gg/metrics/AudioData.h"

namespace gg::metrics::detail
{
enum class SampleKind
{
    Normal, // including zero
    Subnormal,
    NonFinite,
};

/// Classified from the IEEE-754 bits, so FTZ/DAZ modes can't change the answer.
inline SampleKind classify(float x) noexcept
{
    const auto bits = std::bit_cast<uint32_t>(x);
    const uint32_t exponent = (bits >> 23) & 0xFFu;
    const uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent == 0xFFu)
    {
        return SampleKind::NonFinite;
    }
    if (exponent == 0 && mantissa != 0)
    {
        return SampleKind::Subnormal;
    }
    return SampleKind::Normal;
}

/// The sample as a double, with NaN and ±inf read as 0.
inline double finiteOrZero(float x) noexcept
{
    return classify(x) == SampleKind::NonFinite ? 0.0 : static_cast<double>(x);
}

/// The mean of all channels over the shortest channel's length, non-finite samples read as 0.
inline std::vector<double> monoMix(const AudioData &audio)
{
    if (audio.channels.empty())
    {
        return {};
    }
    size_t frames = audio.channels.front().size();
    for (const auto &channel : audio.channels)
    {
        frames = std::min(frames, channel.size());
    }
    std::vector<double> mono(frames, 0.0);
    for (const auto &channel : audio.channels)
    {
        for (size_t n = 0; n < frames; ++n)
        {
            mono[n] += finiteOrZero(channel[n]);
        }
    }
    const auto count = static_cast<double>(audio.channels.size());
    for (auto &x : mono)
    {
        x /= count;
    }
    return mono;
}

/// A 2048-point real FFT of Hann-windowed frames, using pffft.
class Stft
{
  public:
    static constexpr size_t kSize = 2048;
    /// Bins 0 (DC) to kSize / 2 (Nyquist).
    static constexpr size_t kBins = kSize / 2 + 1;

    Stft()
        : setup_(pffft_new_setup(static_cast<int>(kSize), PFFFT_REAL)), in_(allocate()),
          out_(allocate()), work_(allocate()), window_(kSize)
    {
        if (!setup_ || !in_ || !out_ || !work_)
        {
            throw std::bad_alloc();
        }
        for (size_t n = 0; n < kSize; ++n)
        {
            // Periodic Hann: frames at a hop of kSize / 2 overlap-add to a constant.
            const double phase = 2.0 * std::numbers::pi * static_cast<double>(n) / kSize;
            window_[n] = 0.5 - 0.5 * std::cos(phase);
            windowSum_ += window_[n];
            windowSumSquares_ += window_[n] * window_[n];
        }
    }

    /// The number of frames at `hop` covering `numSamples`: frames start at multiples of the
    /// hop, and the last is the first that reaches the end. 0 if there are no samples.
    static size_t frameCount(size_t numSamples, size_t hop) noexcept
    {
        if (numSamples == 0)
        {
            return 0;
        }
        if (numSamples <= kSize)
        {
            return 1;
        }
        return 1 + (numSamples - kSize + hop - 1) / hop;
    }

    /// |X[k]|² for k in [0, kBins) of the windowed frame signal[start, start + kSize), zero
    /// padded past the end. Unnormalised: a sine of amplitude A on bin k gives
    /// (A · windowSum() / 2)².
    void powerSpectrum(const std::vector<double> &signal, size_t start, std::vector<double> &power)
    {
        for (size_t n = 0; n < kSize; ++n)
        {
            const size_t i = start + n;
            const double x = i < signal.size() ? signal[i] : 0.0;
            in_.get()[n] = static_cast<float>(x * window_[n]);
        }
        pffft_transform_ordered(setup_.get(), in_.get(), out_.get(), work_.get(), PFFFT_FORWARD);

        // Ordered real output: out[0] is DC, out[1] is Nyquist, then (re, im) pairs for bins
        // 1 .. kSize/2 - 1.
        const float *out = out_.get();
        power.resize(kBins);
        power[0] = static_cast<double>(out[0]) * out[0];
        power[kSize / 2] = static_cast<double>(out[1]) * out[1];
        for (size_t k = 1; k < kSize / 2; ++k)
        {
            const auto re = static_cast<double>(out[2 * k]);
            const auto im = static_cast<double>(out[2 * k + 1]);
            power[k] = re * re + im * im;
        }
    }

    double windowSum() const noexcept { return windowSum_; }
    double windowSumSquares() const noexcept { return windowSumSquares_; }

  private:
    struct SetupDeleter
    {
        void operator()(PFFFT_Setup *s) const noexcept { pffft_destroy_setup(s); }
    };
    struct AlignedDeleter
    {
        void operator()(float *p) const noexcept { pffft_aligned_free(p); }
    };
    using Buffer = std::unique_ptr<float, AlignedDeleter>;

    static Buffer allocate()
    {
        return Buffer(static_cast<float *>(pffft_aligned_malloc(kSize * sizeof(float))));
    }

    std::unique_ptr<PFFFT_Setup, SetupDeleter> setup_;
    Buffer in_;
    Buffer out_;
    Buffer work_;
    std::vector<double> window_;
    double windowSum_{0.0};
    double windowSumSquares_{0.0};
};
} // namespace gg::metrics::detail

#endif // GG_METRICS_SRC_ANALYSIS_H
