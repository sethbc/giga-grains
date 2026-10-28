// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "Signals.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
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

int64_t inputFadeFrames(double sampleRate) noexcept
{
    return std::max<int64_t>(1, framesFor(kInputFadeSeconds, sampleRate));
}

double raisedCosine(double x) noexcept
{
    // sin θ = θ(1 − θ²/(2·3)(1 − θ²/(4·5)(1 − ... (1 − θ²/(18·19))))), θ = πx/2.
    const double theta = std::numbers::pi / 2.0 * x;
    const double t = theta * theta;
    double s = 1.0;
    for (const double d : {18.0 * 19.0, 16.0 * 17.0, 14.0 * 15.0, 12.0 * 13.0, 10.0 * 11.0,
                           8.0 * 9.0, 6.0 * 7.0, 4.0 * 5.0, 2.0 * 3.0})
    {
        s = 1.0 - t / d * s;
    }
    s *= theta;
    return s * s;
}

gg::metrics::AudioData makeInput(const SourceSpec &input, uint32_t channels, double sampleRate,
                                 uint64_t seed)
{
    if (channels < 1 || channels > 2)
    {
        throw std::invalid_argument("an FX input has 1 or 2 channels");
    }
    if (input.kind == SourceKind::File || input.sampleRate.has_value())
    {
        throw std::invalid_argument(
            "an FX input is synthesized at the render's rate: no file, no sampleRate");
    }
    gg::metrics::AudioData audio = makeSignal(input, sampleRate, seed);
    if (channels == 2)
    {
        switch (input.kind)
        {
        case SourceKind::Noise:
            audio.channels.push_back(
                std::move(makeSignal(input, sampleRate, seed + 1).channels[0]));
            break;
        case SourceKind::Sine:
        {
            SourceSpec fifth = input;
            fifth.frequencyHz *= 1.5;
            audio.channels.push_back(std::move(makeSignal(fifth, sampleRate, seed).channels[0]));
            break;
        }
        case SourceKind::Sweep:
        case SourceKind::File: // rejected above
            audio.channels.push_back(audio.channels[0]);
            break;
        }
    }

    // The fades (ruling M1-v).
    const int64_t fade = inputFadeFrames(sampleRate);
    for (auto &x : audio.channels)
    {
        const auto frames = static_cast<int64_t>(x.size());
        for (int64_t n = 0; n < frames; ++n)
        {
            const int64_t fromEnd = frames - 1 - n;
            if (n >= fade && fromEnd >= fade)
            {
                continue; // the untouched middle
            }
            double gain = 1.0;
            if (n < fade)
            {
                gain *= raisedCosine(static_cast<double>(n) / static_cast<double>(fade));
            }
            if (fromEnd < fade)
            {
                gain *= raisedCosine(static_cast<double>(fromEnd) / static_cast<double>(fade));
            }
            const auto i = static_cast<std::size_t>(n);
            x[i] = static_cast<float>(static_cast<double>(x[i]) * gain);
        }
    }
    return audio;
}
} // namespace gg::render
