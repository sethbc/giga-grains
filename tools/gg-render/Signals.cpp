// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "Signals.h"

#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

#include "gg/core/Rng.h"
#include "gg/metrics/WavIO.h"

namespace gg::render
{
int64_t framesFor(double seconds, double sampleRate) noexcept
{
    // For positive x, truncation of x + 0.5 is round-half-up.
    return static_cast<int64_t>(seconds * sampleRate + 0.5);
}

gg::metrics::AudioData makeSignal(const SourceSpec &source, double sampleRate, uint64_t seed)
{
    if (source.kind == SourceKind::File)
    {
        auto audio = gg::metrics::readAudioFile(source.path);
        if (!audio)
        {
            throw std::runtime_error("can't read the source file " + source.path);
        }
        return std::move(*audio);
    }

    const double rate = source.sampleRate.value_or(sampleRate);
    const auto frames = static_cast<std::size_t>(framesFor(source.seconds, rate));
    std::vector<float> x(frames);
    const double amp = source.amplitude;
    constexpr double twoPi = 2.0 * std::numbers::pi;

    switch (source.kind)
    {
    case SourceKind::Sine:
        for (std::size_t n = 0; n < frames; ++n)
        {
            const double t = static_cast<double>(n) / rate;
            x[n] = static_cast<float>(amp * std::sin(twoPi * source.frequencyHz * t));
        }
        break;
    case SourceKind::Sweep:
    {
        // The phase is the integral of f(t) = f0 · e^(k·t/T), k = ln(f1/f0):
        // φ(t) = 2π · f0 · T / k · (e^(k·t/T) - 1), which tends to 2π · f0 · t as k -> 0.
        const double f0 = source.startHz;
        const double bigT = source.seconds;
        const double k = std::log(source.endHz / f0);
        for (std::size_t n = 0; n < frames; ++n)
        {
            const double t = static_cast<double>(n) / rate;
            const double phase =
                k == 0.0 ? twoPi * f0 * t : twoPi * f0 * bigT / k * std::expm1(k * t / bigT);
            x[n] = static_cast<float>(amp * std::sin(phase));
        }
        break;
    }
    case SourceKind::Noise:
        for (std::size_t n = 0; n < frames; ++n)
        {
            const uint64_t h = rng::hash({seed, 0, 0, rng::Field::SignalNoise, n});
            x[n] = static_cast<float>(amp * rng::bipolar(h));
        }
        break;
    case SourceKind::File:
        break; // handled above
    }
    return {rate, {std::move(x)}};
}
} // namespace gg::render
