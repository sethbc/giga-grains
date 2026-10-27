// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/metrics/Metrics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "Analysis.h"

namespace gg::metrics
{
namespace
{
// The constants of the metric definitions in Metrics.h. Changing any of them invalidates every
// golden; bump kMetricsVersion when that happens.
constexpr int kMetricsVersion = 1;
constexpr double kFloorDb = -200.0;
constexpr double kEnvelopeFrameSeconds = 0.01;
constexpr double kEnvelopeIgnoreDb = -60.0;
constexpr size_t kBandHop = 1024;
constexpr int kBandCount = 32;
constexpr double kBandLowHz = 20.0;
constexpr double kBandIgnoreDb = -90.0;
constexpr double kClickMinDelta = 1e-3;

double amplitudeDb(double amplitude)
{
    return amplitude > 0.0 ? std::max(20.0 * std::log10(amplitude), kFloorDb) : kFloorDb;
}

double powerDb(double power)
{
    return power > 0.0 ? std::max(10.0 * std::log10(power), kFloorDb) : kFloorDb;
}

std::vector<double> rmsEnvelopeDb(const std::vector<double> &mono, double sampleRate)
{
    std::vector<double> envelope;
    if (!(std::isfinite(sampleRate) && sampleRate > 0.0))
    {
        return envelope;
    }
    const auto frame =
        static_cast<size_t>(std::max(1.0, std::round(kEnvelopeFrameSeconds * sampleRate)));
    envelope.reserve(mono.size() / frame + 1);
    for (size_t start = 0; start < mono.size(); start += frame)
    {
        const size_t end = std::min(mono.size(), start + frame);
        double sumSquares = 0.0;
        for (size_t n = start; n < end; ++n)
        {
            sumSquares += mono[n] * mono[n];
        }
        envelope.push_back(amplitudeDb(std::sqrt(sumSquares / static_cast<double>(end - start))));
    }
    return envelope;
}

std::vector<double> bandProfileDb(const std::vector<double> &mono, double sampleRate)
{
    using detail::Stft;

    std::vector<double> profile(kBandCount, kFloorDb);
    const size_t frames = Stft::frameCount(mono.size(), kBandHop);
    if (frames == 0 || !(std::isfinite(sampleRate) && sampleRate > 0.0))
    {
        return profile;
    }

    // Which band each bin's centre frequency falls in, or -1 if below 20 Hz.
    const double nyquist = sampleRate / 2.0;
    std::array<double, kBandCount + 1> edges{};
    for (int b = 0; b <= kBandCount; ++b)
    {
        edges[b] = kBandLowHz * std::pow(nyquist / kBandLowHz, static_cast<double>(b) / kBandCount);
    }
    std::vector<int> bandOfBin(Stft::kBins, -1);
    for (size_t k = 1; k < Stft::kBins; ++k)
    {
        const double hz = static_cast<double>(k) * sampleRate / Stft::kSize;
        if (k == Stft::kBins - 1)
        {
            bandOfBin[k] = nyquist >= kBandLowHz ? kBandCount - 1 : -1; // Nyquist: last band
            continue;
        }
        for (int b = 0; b < kBandCount; ++b)
        {
            if (edges[b] <= hz && hz < edges[b + 1])
            {
                bandOfBin[k] = b;
                break;
            }
        }
    }

    Stft stft;
    // Scales the one-sided |X|² so the bins sum to the frame's mean square.
    const double norm = 1.0 / (static_cast<double>(Stft::kSize) * stft.windowSumSquares());
    std::vector<double> bandPower(kBandCount, 0.0);
    std::vector<double> power;
    for (size_t f = 0; f < frames; ++f)
    {
        stft.powerSpectrum(mono, f * kBandHop, power);
        for (size_t k = 1; k < Stft::kBins; ++k)
        {
            if (bandOfBin[k] >= 0)
            {
                const double sides = k == Stft::kBins - 1 ? 1.0 : 2.0;
                bandPower[bandOfBin[k]] += sides * power[k] * norm;
            }
        }
    }
    for (int b = 0; b < kBandCount; ++b)
    {
        profile[b] = powerDb(bandPower[b] / static_cast<double>(frames));
    }
    return profile;
}

double stereoCorrelation(const AudioData &audio)
{
    if (audio.channels.size() < 2)
    {
        return 1.0;
    }
    const auto &left = audio.channels[0];
    const auto &right = audio.channels[1];
    const size_t frames = std::min(left.size(), right.size());
    if (frames == 0)
    {
        return 1.0;
    }

    double sumL = 0.0;
    double sumR = 0.0;
    for (size_t n = 0; n < frames; ++n)
    {
        sumL += detail::finiteOrZero(left[n]);
        sumR += detail::finiteOrZero(right[n]);
    }
    const double meanL = sumL / static_cast<double>(frames);
    const double meanR = sumR / static_cast<double>(frames);

    double covariance = 0.0;
    double varianceL = 0.0;
    double varianceR = 0.0;
    for (size_t n = 0; n < frames; ++n)
    {
        const double l = detail::finiteOrZero(left[n]) - meanL;
        const double r = detail::finiteOrZero(right[n]) - meanR;
        covariance += l * r;
        varianceL += l * l;
        varianceR += r * r;
    }
    const double denominator = std::sqrt(varianceL * varianceR);
    if (!(varianceL > 0.0 && varianceR > 0.0 && denominator > 0.0))
    {
        return 1.0; // a silent or constant channel
    }
    return std::clamp(covariance / denominator, -1.0, 1.0);
}

double clickScore(const std::vector<double> &mono)
{
    if (mono.size() < 3)
    {
        return 0.0;
    }
    const auto delta = [&](size_t n) {
        return std::abs(mono[n] - 2.0 * mono[n - 1] + mono[n - 2]);
    };

    double sumSquares = 0.0;
    double maxDelta = 0.0;
    for (size_t n = 2; n < mono.size(); ++n)
    {
        const double d = delta(n);
        sumSquares += d * d;
        if (d > kClickMinDelta)
        {
            maxDelta = std::max(maxDelta, d);
        }
    }
    const double rms = std::sqrt(sumSquares / static_cast<double>(mono.size() - 2));
    // maxDelta > 0 implies rms > 0.
    return maxDelta > 0.0 ? maxDelta / rms : 0.0;
}

/// Checks a dB series element by element, skipping entries where both values are below `ignoreDb`.
void compareSeries(const char *name, const char *unit, const std::vector<double> &golden,
                   const std::vector<double> &actual, double tolerance, double ignoreDb,
                   std::vector<std::string> &failures)
{
    if (golden.size() != actual.size())
    {
        failures.push_back(fmt::format("{}: golden has {} {}s, actual has {}", name, golden.size(),
                                       unit, actual.size()));
        return;
    }
    size_t outside = 0;
    size_t compared = 0;
    size_t worst = 0;
    double worstError = -1.0;
    for (size_t i = 0; i < golden.size(); ++i)
    {
        if (golden[i] < ignoreDb && actual[i] < ignoreDb)
        {
            continue;
        }
        ++compared;
        const double error = std::abs(actual[i] - golden[i]);
        if (!(error <= tolerance))
        {
            ++outside;
            if (!(error <= worstError))
            {
                worst = i;
                worstError = error;
            }
        }
    }
    if (outside > 0)
    {
        failures.push_back(fmt::format("{}: {} of {} compared {}s differ by more than {} dB; worst "
                                       "is {} {} (golden {:.3f} dB, actual {:.3f} dB)",
                                       name, outside, compared, unit, tolerance, unit, worst,
                                       golden[worst], actual[worst]));
    }
}

bool readNumber(const nlohmann::json &j, const char *key, double &out)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number())
    {
        return false;
    }
    out = it->get<double>();
    return true;
}

bool readNumbers(const nlohmann::json &j, const char *key, std::vector<double> &out)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array())
    {
        return false;
    }
    out.clear();
    out.reserve(it->size());
    for (const auto &v : *it)
    {
        if (!v.is_number())
        {
            return false;
        }
        out.push_back(v.get<double>());
    }
    return true;
}

bool readCount(const nlohmann::json &j, const char *key, uint64_t &out)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_unsigned())
    {
        return false;
    }
    out = it->get<uint64_t>();
    return true;
}
} // namespace

Metrics computeMetrics(const AudioData &audio)
{
    Metrics m{};

    double peak = 0.0;
    m.dc.reserve(audio.channels.size());
    for (const auto &channel : audio.channels)
    {
        double sum = 0.0;
        for (const float x : channel)
        {
            switch (detail::classify(x))
            {
            case detail::SampleKind::NonFinite:
                ++m.nonFinite;
                continue; // reads as 0
            case detail::SampleKind::Subnormal:
                ++m.denormals;
                break;
            case detail::SampleKind::Normal:
                break;
            }
            const auto v = static_cast<double>(x);
            peak = std::max(peak, std::abs(v));
            sum += v;
        }
        m.dc.push_back(channel.empty() ? 0.0 : sum / static_cast<double>(channel.size()));
    }
    m.peakDb = amplitudeDb(peak);

    const std::vector<double> mono = detail::monoMix(audio);
    m.rmsEnvelopeDb = rmsEnvelopeDb(mono, audio.sampleRate);
    m.bandProfileDb = bandProfileDb(mono, audio.sampleRate);
    m.stereoCorrelation = stereoCorrelation(audio);
    m.clickScore = clickScore(mono);
    return m;
}

CompareResult compare(const Metrics &golden, const Metrics &actual, const Tolerances &tol)
{
    std::vector<std::string> failures;

    if (!(std::abs(actual.peakDb - golden.peakDb) <= tol.peakDb))
    {
        failures.push_back(
            fmt::format("peakDb: golden {:.3f} dB, actual {:.3f} dB (tolerance {} dB)",
                        golden.peakDb, actual.peakDb, tol.peakDb));
    }

    if (golden.dc.size() != actual.dc.size())
    {
        failures.push_back(fmt::format("dc: golden has {} channels, actual has {}",
                                       golden.dc.size(), actual.dc.size()));
    }
    else
    {
        for (size_t c = 0; c < golden.dc.size(); ++c)
        {
            if (!(std::abs(actual.dc[c] - golden.dc[c]) <= tol.dc))
            {
                failures.push_back(fmt::format("dc: channel {} golden {:.3g}, actual {:.3g} "
                                               "(tolerance {})",
                                               c, golden.dc[c], actual.dc[c], tol.dc));
            }
        }
    }

    compareSeries("rmsEnvelope", "frame", golden.rmsEnvelopeDb, actual.rmsEnvelopeDb,
                  tol.rmsEnvelopeDb, kEnvelopeIgnoreDb, failures);
    compareSeries("bandProfile", "band", golden.bandProfileDb, actual.bandProfileDb,
                  tol.bandProfileDb, kBandIgnoreDb, failures);

    if (!(std::abs(actual.stereoCorrelation - golden.stereoCorrelation) <= tol.correlation))
    {
        failures.push_back(fmt::format("stereoCorrelation: golden {:.4f}, actual {:.4f} "
                                       "(tolerance {})",
                                       golden.stereoCorrelation, actual.stereoCorrelation,
                                       tol.correlation));
    }

    if (!(actual.clickScore <= tol.maxClickScore))
    {
        failures.push_back(fmt::format("clickScore: actual {:.2f} exceeds the limit {}",
                                       actual.clickScore, tol.maxClickScore));
    }
    if (actual.nonFinite != 0)
    {
        failures.push_back(
            fmt::format("nonFinite: actual has {} NaN or infinite samples", actual.nonFinite));
    }
    if (actual.denormals != 0)
    {
        failures.push_back(
            fmt::format("denormals: actual has {} subnormal samples", actual.denormals));
    }

    const bool pass = failures.empty();
    return {pass, std::move(failures)};
}

std::string toJson(const Metrics &m)
{
    // Insertion order: the scalars a reader looks for first, then the series.
    nlohmann::ordered_json j;
    j["metricsVersion"] = kMetricsVersion;
    j["peakDb"] = m.peakDb;
    j["stereoCorrelation"] = m.stereoCorrelation;
    j["clickScore"] = m.clickScore;
    j["nonFinite"] = m.nonFinite;
    j["denormals"] = m.denormals;
    j["dc"] = m.dc;
    j["rmsEnvelopeDb"] = m.rmsEnvelopeDb;
    j["bandProfileDb"] = m.bandProfileDb;
    // nlohmann/json prints the shortest decimal that parses back to the same double.
    return j.dump(2) + "\n";
}

std::optional<Metrics> metricsFromJson(const std::string &json)
{
    const auto j = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object())
    {
        return std::nullopt;
    }
    uint64_t version = 0;
    if (!readCount(j, "metricsVersion", version) || version != kMetricsVersion)
    {
        return std::nullopt;
    }
    Metrics m{};
    const bool ok = readNumber(j, "peakDb", m.peakDb) &&
                    readNumber(j, "stereoCorrelation", m.stereoCorrelation) &&
                    readNumber(j, "clickScore", m.clickScore) &&
                    readCount(j, "nonFinite", m.nonFinite) &&
                    readCount(j, "denormals", m.denormals) && readNumbers(j, "dc", m.dc) &&
                    readNumbers(j, "rmsEnvelopeDb", m.rmsEnvelopeDb) &&
                    readNumbers(j, "bandProfileDb", m.bandProfileDb);
    if (!ok)
    {
        return std::nullopt;
    }
    return m;
}
} // namespace gg::metrics
