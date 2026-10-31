// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "Signals.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <numbers>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "Render.h"
#include "gg/core/MachineSettings.h"
#include "gg/core/Rng.h"
#include "gg/media/Sample.h"
#include "gg/metrics/WavIO.h"
#include "gg/source/SampleBuffer.h"

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
            throw BadInput("can't read the source file " + source.path);
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

namespace
{
/// The file at UTF-8 `path` loaded with gg-media and prepared at `sampleRate`, as the plugin's
/// loader does it.
std::shared_ptr<const PreparedSource> loadFile(const std::string &path, double sampleRate)
{
    auto loaded = gg::media::loadSample(pathFromUtf8(path), gg::core::MachineSettings{},
                                        gg::media::kDefaultDecodeBound);
    if (!loaded.decoded)
    {
        throw BadInput(loaded.error);
    }
    return gg::media::prepare(std::move(loaded.decoded), sampleRate);
}

/// The audio of a source's planar buffer.
gg::metrics::AudioData toAudioData(const SampleBuffer &buffer)
{
    gg::metrics::AudioData audio{buffer.sampleRate(), {}};
    for (uint32_t c = 0; c < buffer.channels(); ++c)
    {
        const float *x = buffer.channel(c);
        audio.channels.emplace_back(x, x + buffer.frames());
    }
    return audio;
}
} // namespace

std::shared_ptr<const PreparedSource> makeSample(const SourceSpec &sample, double sampleRate,
                                                 uint64_t seed)
{
    if (sample.sampleRate.has_value())
    {
        throw std::invalid_argument(
            "an instrument's sample is made at the render's rate: no sampleRate");
    }
    if (sample.kind == SourceKind::File)
    {
        return loadFile(sample.path, sampleRate);
    }
    const gg::metrics::AudioData signal = makeSignal(sample, sampleRate, seed);
    const auto &x = signal.channels[0];
    auto buffer =
        std::make_shared<SampleBuffer>(1, static_cast<int64_t>(x.size()), signal.sampleRate);
    std::copy(x.begin(), x.end(), buffer->channel(0));
    return gg::media::prepare(std::move(buffer), sampleRate);
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
    if (input.sampleRate.has_value())
    {
        throw std::invalid_argument("an FX input is made at the render's rate: no sampleRate");
    }
    gg::metrics::AudioData audio = input.kind == SourceKind::File
                                       ? toAudioData(loadFile(input.path, sampleRate)->buffer())
                                       : makeSignal(input, sampleRate, seed);
    if (channels == 2 && input.kind != SourceKind::File)
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
        case SourceKind::File: // has its own channels
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

namespace
{
/// Renders a spec given as JSON, which parseRenderSpec must accept, with --save-state's machinery:
/// the session it ends with goes to `statePath`. Throws if the spec doesn't parse (a bug here).
void renderToState(const nlohmann::json &json, const std::string &statePath,
                   const std::string &mediaDir)
{
    std::string error;
    const auto spec = parseRenderSpec(json.dump(), error);
    if (!spec)
    {
        throw std::logic_error("a built-in fixture spec is invalid: " + error);
    }
    RenderOptions options;
    options.saveStatePath = statePath;
    options.mediaDir = mediaDir;
    (void)render(*spec, nullptr, options);
}

/// frozen-ring.ggst and four-samples.ggst (Signals.h).
void writeStateFixtures(const std::string &dir, const std::filesystem::path &folder)
{
    // An FX with a 20 s ring over a 20 s sweep, frozen at 18 s; splices 12 s and 6 s behind the
    // head at 19 s (the head has stopped at 18 s), so the marks are at 6 s and 12 s of the ring.
    nlohmann::json frozen = {
        {"mode", "fx"},
        {"sampleRate", 48000},
        {"durationSeconds", 20.0},
        {"blockSize", 128},
        {"seed", 1},
        {"input",
         {{"kind", "sweep"},
          {"startHz", 80.0},
          {"endHz", 2000.0},
          {"seconds", 20.0},
          {"amplitude", 0.5}}},
        {"channels", 1},
        {"params", {{"A Ring Length", std::log2(20.0)}}},
        {"automation", {{{"time", 18.0}, {"param", "A Freeze"}, {"value", 1}}}},
        {"operations",
         {{{"time", 19.0}, {"layer", "A"}, {"op", "splice"}, {"behind", 12.0}},
          {{"time", 19.0}, {"layer", "A"}, {"op", "splice"}, {"behind", 6.0}}}}};
    renderToState(frozen, utf8FromPath(folder / kFrozenRingState), dir);

    // An instrument whose four layers read the four files, by content path.
    const auto file = [&](const char *name) {
        return nlohmann::json{{"kind", "file"}, {"path", utf8FromPath(folder / name)}};
    };
    nlohmann::json four = {
        {"mode", "instrument"},
        {"sampleRate", 48000},
        {"durationSeconds", 0.1},
        {"blockSize", 128},
        {"seed", 1},
        {"sample", file(kToneMedium)},
        {"samples",
         {{"B", file(kLoopMedium)}, {"C", file(kSineMedium)}, {"D", file(kNoiseMedium)}}},
        {"params", {{"Layer B On", 1}, {"Layer C On", 1}, {"Layer D On", 1}}}};
    renderToState(four, utf8FromPath(folder / kFourSamplesState), dir);
}
} // namespace

bool writeTestMedia(const std::string &dir, std::string &error)
{
    error.clear();
    std::filesystem::path folder;
    try
    {
        folder = pathFromUtf8(dir);
    }
    catch (const BadInput &e)
    {
        error = e.what();
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec || !std::filesystem::is_directory(folder, ec))
    {
        error = "can't create the directory " + dir;
        return false;
    }

    // tone-44k1.wav: stereo, 44.1 kHz, 4 s, both channels at -6 dBFS.
    const double minus6 = std::pow(10.0, -6.0 / 20.0);
    constexpr double toneRate = 44100.0;
    gg::metrics::AudioData tone = makeSignal({.kind = SourceKind::Sweep,
                                              .startHz = 50.0,
                                              .endHz = 8000.0,
                                              .seconds = 4.0,
                                              .amplitude = minus6},
                                             toneRate, 0);
    tone.channels.push_back(std::move(
        makeSignal(
            {.kind = SourceKind::Sine, .frequencyHz = 220.0, .seconds = 4.0, .amplitude = minus6},
            toneRate, 0)
            .channels[0]));

    // loop-48k.wav: mono, 48 kHz, 4 s. Each burst's envelope is e^(-k / tau), k frames into it,
    // which reaches -60 dB (10^-3) at its 30 ms end.
    constexpr double loopRate = 48000.0;
    const auto period = static_cast<std::size_t>(framesFor(0.25, loopRate));
    const auto burst = static_cast<std::size_t>(framesFor(0.03, loopRate));
    const double tau = static_cast<double>(burst) / std::log(1000.0);
    std::vector<float> noise =
        makeSignal({.kind = SourceKind::Noise, .seconds = 4.0, .amplitude = 1.0}, loopRate, 7)
            .channels[0];
    std::vector<double> shaped(noise.size(), 0.0);
    double peak = 0.0;
    for (std::size_t n = 0; n < noise.size(); ++n)
    {
        const std::size_t k = n % period;
        if (k < burst)
        {
            shaped[n] = static_cast<double>(noise[n]) * std::exp(-static_cast<double>(k) / tau);
            peak = std::max(peak, std::abs(shaped[n]));
        }
    }
    const double scale = std::pow(10.0, -3.0 / 20.0) / peak;
    for (std::size_t n = 0; n < noise.size(); ++n)
    {
        noise[n] = static_cast<float>(shaped[n] * scale);
    }
    const gg::metrics::AudioData loop{loopRate, {std::move(noise)}};

    // sine-330.wav and noise-2s.wav: mono, 48 kHz, 2 s.
    const gg::metrics::AudioData sine330 = makeSignal(
        {.kind = SourceKind::Sine, .frequencyHz = 330.0, .seconds = 2.0, .amplitude = 0.5}, 48000.0,
        0);
    const gg::metrics::AudioData noise2s =
        makeSignal({.kind = SourceKind::Noise, .seconds = 2.0, .amplitude = 0.5}, 48000.0, 3);

    const std::pair<const char *, const gg::metrics::AudioData *> media[] = {
        {kToneMedium, &tone},
        {kLoopMedium, &loop},
        {kSineMedium, &sine330},
        {kNoiseMedium, &noise2s}};
    for (const auto &[name, audio] : media)
    {
        const std::string path = utf8FromPath(folder / name);
        if (!gg::metrics::writeWavFloat32(path, *audio))
        {
            error = "can't write " + path;
            return false;
        }
    }

    // The two state fixtures, rendered from the media just written.
    try
    {
        writeStateFixtures(dir, folder);
    }
    catch (const std::exception &e)
    {
        error = std::string("can't write the state fixtures: ") + e.what();
        return false;
    }
    return true;
}
} // namespace gg::render
