// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_CLOUDSCHEDULER_H
#define GG_GRAIN_CLOUDSCHEDULER_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "gg/core/MathDet.h"
#include "gg/core/Rng.h"

namespace gg
{
/// The controls of one Cloud grain stream. CloudScheduler::setParams sanitizes them: a
/// non-finite value becomes the default, then every value is clamped to the range given here.
struct CloudParams
{
    /// Grains per second. 0 or below turns the stream off (ruling M1-a); any positive value is
    /// clamped to [0.01, min(4000, sampleRate)].
    double densityHz{16.0};
    double distribution{0.0};     ///< 0 = periodic onsets, 1 = Poisson; [0, 1].
    double grainDurationSec{0.1}; ///< At least 16 samples (the minimum window length).
    double position{0.5};         ///< Where grains read, as a fraction of the source; [0, 1].
    double spray{0.0}; ///< Bipolar random spread of position (M0's positionJitter); [0, 1].
    double pitchSemitones{0.0}; ///< [-48, 48].
    double stereo{0.0};         ///< Random pan spread: 0 = every grain centred; [0, 1].
    float gain{0.5f};           ///< [0, 4].
};

/// One grain as the scheduler draws it, before the source turns it into a GrainSpec. Every
/// field is latched at the grain's onset (INTENT §14).
struct GrainDraw
{
    uint64_t index;      ///< k, counted from the scheduler's reset().
    int64_t onsetSample; ///< floor(t(k)), in output samples.
    double onsetFrac;    ///< t(k) - onsetSample, in [0, 1).
    double positionDraw; ///< clamp(position + spray × bipolar(u), 0, 1): a fraction of the source.
    double rate;         ///< The pitch ratio exp2Det(pitchSemitones / 12).
    double durationSamples; ///< Window length in output samples, at least 16.
    float gain;
    float pan; ///< stereo × bipolar(u), in [-1, 1]; +0 at stereo 0 (see schedule()).

    friend bool operator==(const GrainDraw &, const GrainDraw &) = default;
};

/// The Cloud scheduler (INTENT §14; spec §6.2): one grain stream whose onsets run continuously
/// from periodic (distribution 0) to Poisson (distribution 1).
///
/// Onsets follow the recurrence t(0) = 0, t(k+1) = t(k) + Δ(k), in output samples since
/// reset(), with
///
///     Δ(k) = max(P × ((1 − d) + d × E(k)), kMinIntervalSamples),
///     P = sampleRate / densityHz,  E(k) = −logDet(1 − u(k)),
///
/// where d is the distribution and u(k) = uniform01(hash(seed, layer, stream, Onset, k)). E is a
/// unit exponential, so the mean interval is P at every d. At d = 0 every interval is exactly P.
/// When the sums k × P are exact too, as for a whole number of samples or a period like 5512.5,
/// the onsets are exactly M0's lattice; otherwise they differ from it in the last bits.
///
/// t(k+1) is computed when grain k is emitted, from the parameters in effect then. So a change
/// to the density or the distribution applies from the next onset computed after it: the
/// interval already pending keeps the old values.
///
/// Every random field of a grain has its own rng::Field, keyed by (seed, layer, stream, field,
/// k), so the draws don't depend on block size, call order or each other.
///
/// All members are real-time safe. Nothing in schedule() calls libm except the exact floor:
/// E(k) comes from logDet and the pitch ratio from exp2Det, so no libm value reaches a grain.
class CloudScheduler
{
  public:
    /// The floor on Δ(k). At d = 1 an interval P × E(k) can be arbitrarily small (it is 0 when
    /// u(k) = 0), so without a floor nothing would bound the number of onsets in one sample.
    /// With it, one schedule() call emits at most about 16 grains per frame. At 48 kHz and the
    /// maximum density (P = 12) it raises about 0.5% of Poisson intervals, by less than 1/16
    /// sample, which moves the mean by about 2e-4 samples; at d = 0 it never applies.
    static constexpr double kMinIntervalSamples = 1.0 / 16.0;

    CloudScheduler() noexcept { prepare(48000.0); }

    /// Sets the output sample rate (a non-finite or non-positive rate means 48000) and resets.
    void prepare(double sampleRate) noexcept;

    /// Takes new controls, sanitized as CloudParams documents. They apply from the next
    /// interval computed after this call; the onset already pending doesn't move.
    void setParams(const CloudParams &params) noexcept;

    /// Restarts the schedule: the next onset is t = 0 with k = 0. Keeps the sample rate and the
    /// controls.
    void reset() noexcept;

    /// Calls emit(const GrainDraw &) for every grain with onsetSample in
    /// [blockStart, blockStart + numFrames), in onset order. Blocks must be contiguous from the
    /// last reset(), starting at 0. seed, layer and stream key the random draws.
    ///
    /// While the stream is off nothing is emitted, and the pending onset keeps its time. If
    /// that time has passed when the stream is turned back on, the onset moves to blockStart,
    /// the first sample after the change.
    template <class Emit>
    void schedule(int64_t blockStart, uint32_t numFrames, uint64_t seed, uint32_t layer,
                  uint32_t stream, Emit &&emit) noexcept
    {
        if (!(densityHz_ > 0.0))
        {
            return; // off
        }
        const auto start = static_cast<double>(blockStart);
        const auto end = static_cast<double>(blockStart + static_cast<int64_t>(numFrames));
        nextOnset_ = std::max(nextOnset_, start);
        while (nextOnset_ < end)
        {
            const double t = nextOnset_;
            const uint64_t k = nextK_;
            const double whole = std::floor(t);
            const auto key = [&](rng::Field field) {
                return rng::hash({seed, layer, stream, field, k});
            };
            const double spray = params_.spray * rng::bipolar(key(rng::Field::PositionJitter));
            const double p = params_.stereo * rng::bipolar(key(rng::Field::Pan));
            // A pan below the smallest normal float is 0, so the value doesn't depend on the
            // flush-to-zero mode. It is +0, never the -0 of stereo 0 times a negative draw.
            const float pan =
                std::abs(p) < std::numeric_limits<float>::min() ? 0.0f : static_cast<float>(p);
            const GrainDraw g{
                .index = k,
                .onsetSample = static_cast<int64_t>(whole),
                .onsetFrac = t - whole,
                .positionDraw = std::clamp(params_.position + spray, 0.0, 1.0),
                .rate = pitchRatio_,
                .durationSamples = durationSamples_,
                .gain = params_.gain,
                .pan = pan,
            };
            const double e = -logDet(1.0 - rng::uniform01(key(rng::Field::Onset)));
            const double d = params_.distribution;
            nextOnset_ = t + std::max(period_ * ((1.0 - d) + d * e), kMinIntervalSamples);
            ++nextK_;
            emit(g);
        }
    }

  private:
    void updateDerived() noexcept;

    double sampleRate_{48000.0};
    CloudParams params_{}; // sanitized, independent of the sample rate

    // Derived from params_ and sampleRate_ by updateDerived().
    double densityHz_{0.0}; // <= 0 when off
    double period_{0.0};    // P = sampleRate_ / densityHz_, when on
    double durationSamples_{0.0};
    double pitchRatio_{1.0}; // exp2Det(pitchSemitones / 12)

    double nextOnset_{0.0}; // t(nextK_), in output samples since reset()
    uint64_t nextK_{0};
};
} // namespace gg

#endif // GG_GRAIN_CLOUDSCHEDULER_H
