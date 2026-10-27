// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_METRICS_METRICS_H
#define GG_METRICS_METRICS_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gg/metrics/AudioData.h"

/// The summary statistics golden renders are compared by (INTENT §19). The definitions below are
/// a contract: golden files store these values, so changing how any of them is computed means
/// regenerating every golden.
///
/// Conventions that apply to every metric:
/// - Arithmetic is in double precision.
/// - "dB" values are floored at -200, which is also what silence reads.
/// - The mono mix is the mean of all channels, over the shortest channel's length.
/// - Non-finite samples (NaN, ±inf) are counted in `nonFinite` and read as 0 by every other
///   metric, so the other values, and the JSON, stay finite.
namespace gg::metrics
{
struct Metrics
{
    /// 20·log10(max |x|) over every sample of every channel.
    double peakDb{};
    /// The mean of each channel, one entry per channel.
    std::vector<double> dc;
    /// RMS of the mono mix in frames of round(0.01 · sampleRate) samples (480 at 48 kHz), as
    /// 20·log10(rms). The last frame may be shorter and covers what's left. Empty if the sample
    /// rate isn't positive and finite.
    std::vector<double> rmsEnvelopeDb;
    /// Mean power per band of the mono mix, 32 entries, as 10·log10(power):
    /// - Frames of 2048 samples, periodic Hann window, hop 1024, transformed with pffft. Frames
    ///   start at multiples of the hop; the last is the first that reaches the end of the audio,
    ///   zero padded. There are no frames if there's no audio.
    /// - Band b covers [20·r^b, 20·r^(b+1)) Hz, with r = (Nyquist / 20)^(1/32); the last band also
    ///   includes Nyquist. A band's power in one frame is the sum over the FFT bins whose centre
    ///   frequency falls in it of c·|X[k]|² / (2048 · Σw²), where c is 2 below Nyquist and 1 at
    ///   it. That makes a sine of amplitude A in one band read its mean square, A²/2.
    /// - The profile is the mean of that over frames. Bands with no bins (the lowest few at
    ///   48 kHz) read -200, as does every band if there's no audio or the sample rate isn't
    ///   positive and finite.
    std::vector<double> bandProfileDb;
    /// Pearson correlation of channels 0 and 1. 1.0 when there are fewer than two channels, or
    /// when either channel has zero variance (silent or constant).
    double stereoCorrelation{};
    /// Discontinuity detector on the mono mix m:
    /// - d[n] = |m[n] - 2m[n-1] + m[n-2]| for n >= 2, and R = rms(d) over all of them;
    /// - clickScore = max(d[n] / R) over the n with d[n] > 1e-3, or 0 if there are none.
    double clickScore{};
    /// Samples (all channels) that are NaN or ±inf.
    uint64_t nonFinite{};
    /// Samples (all channels) that are float subnormals. Classified from the bits, so the
    /// count doesn't depend on the CPU's flush-to-zero or denormals-are-zero modes.
    uint64_t denormals{};
};

Metrics computeMetrics(const AudioData &audio);

struct Tolerances
{
    double rmsEnvelopeDb{0.5};
    double bandProfileDb{1.0};
    double peakDb{0.5};
    double dc{1e-4};
    double correlation{0.02};
    /// An absolute limit on actual.clickScore, not relative to the golden.
    double maxClickScore{30.0};
};

struct CompareResult
{
    bool pass;
    /// One line per failed check, each starting with the metric's name.
    std::vector<std::string> failures;
};

/// Checks `actual` against `golden`:
/// - peakDb, each dc entry, each rmsEnvelopeDb frame, each bandProfileDb band and
///   stereoCorrelation must be within their tolerances of the golden values, and the series
///   must have the same lengths;
/// - envelope frames where both values are below -60 dB, and bands where both are below -90 dB,
///   are ignored;
/// - actual.clickScore must be at most maxClickScore;
/// - actual.nonFinite and actual.denormals must both be 0.
CompareResult compare(const Metrics &golden, const Metrics &actual, const Tolerances &tol = {});

/// The metrics as a JSON object, ending with a newline. Doubles are written so they read back
/// exactly. A "metricsVersion" key records which revision of the definitions produced them.
std::string toJson(const Metrics &m);

/// Parses what toJson writes. Returns nullopt for invalid JSON, a missing or mistyped field, or a
/// different metricsVersion.
std::optional<Metrics> metricsFromJson(const std::string &json);
} // namespace gg::metrics

#endif // GG_METRICS_METRICS_H
