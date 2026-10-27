// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_PERIODICSTREAM_H
#define GG_GRAIN_PERIODICSTREAM_H

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <sst/cpputils/rtsan_support.h>

#include "gg/core/Rng.h"
#include "gg/grain/GrainPool.h"

namespace gg
{
/// The controls of one periodic grain stream. PeriodicStream::setParams sanitizes them.
struct StreamParams
{
    double grainRateHz{20.0};     ///< Grains per second; <= 0 turns the stream off; max 4000.
    double grainDurationSec{0.1}; ///< At least 16 samples (the minimum window length).
    double position{0.5};         ///< Where grains read, as a fraction of the source; 0..1.
    double positionJitter{0.0};   ///< Random spread of position, 0..1.
    double pitchSemitones{0.0};   ///< -48..48.
    float gain{0.5f};             ///< 0..4.
};

/// A synchronous scheduler: grains on a regular lattice of onsets (INTENT §14, Scheduler).
///
/// Grain k's onset is t_k = k · sampleRate / grainRateHz samples after reset(), split into
/// onsetSample = floor(t_k) and onsetFrac = t_k - onsetSample. schedule() emits exactly the
/// grains whose onsetSample lies in the block, so the grains, and their parameters, don't
/// depend on how the timeline is cut into blocks.
///
/// When the grain rate changes, the lattice restarts from the last emitted onset: the next
/// grain falls one new period after it, or at the first block after the change if that is
/// later. So a rate change takes effect at once instead of jumping to k · newPeriod.
///
/// All members are real-time safe. Nothing in schedule() calls libm except the exact floor.
class PeriodicStream
{
  public:
    PeriodicStream() noexcept { prepare(48000.0); }

    /// Sets the output sample rate (a non-finite or non-positive rate means 48000) and resets.
    void prepare(double sampleRate) noexcept;

    /// Takes new controls. Non-finite values become the defaults, then every value is
    /// clamped to the range documented on StreamParams. The rate is also capped at one grain
    /// per output sample, which keeps each schedule() call bounded.
    void setParams(const StreamParams &params) noexcept;

    /// Restarts the lattice at k = 0, onset 0. Keeps the sample rate and the controls.
    void reset() noexcept;

    /// Calls emit(const GrainSpec &) for every grain with onsetSample in
    /// [blockStart, blockStart + numFrames), in onset order. Blocks must be contiguous from
    /// the last reset(), starting at 0. sourceFrames and sourceRate describe the source the
    /// grains will read; seed keys the position jitter.
    template <class Emit>
    void schedule(int64_t blockStart, uint32_t numFrames, int64_t sourceFrames, double sourceRate,
                  uint64_t seed, Emit &&emit) noexcept
    {
        if (!(rateHz_ > 0.0))
        {
            return; // off
        }
        const auto start = static_cast<double>(blockStart);
        const auto end = static_cast<double>(blockStart + static_cast<int64_t>(numFrames));
        if (restartLattice_)
        {
            restartLattice(start);
        }
        const double span = sourceFrames > 1 ? static_cast<double>(sourceFrames - 1) : 0.0;
        const double rate = pitchRatio_ * sourceRate / sampleRate_;
        for (;;)
        {
            const double t = onsetTime(nextK_);
            if (!(t < end))
            {
                break;
            }
            const double whole = std::floor(t);
            const double jitter =
                params_.positionJitter *
                rng::bipolar(rng::hash({seed, 0, 0, rng::Field::PositionJitter, nextK_}));
            const GrainSpec g{
                .index = nextK_,
                .onsetSample = static_cast<int64_t>(whole),
                .onsetFrac = t - whole,
                .startPos = std::clamp(params_.position + jitter, 0.0, 1.0) * span,
                .rate = rate,
                .durationSamples = durationSamples_,
                .gain = params_.gain,
            };
            lastOnset_ = t;
            hasLastOnset_ = true;
            ++nextK_;
            emit(g);
        }
    }

  private:
    void restartLattice(double blockStart) noexcept SST_CPPUTILS_NONBLOCKING;
    void updateDerived() noexcept;

    /// t_k for the current lattice. With the lattice anchored at (0, 0) this is exactly
    /// k · sampleRate / grainRateHz.
    double onsetTime(uint64_t k) const noexcept
    {
        return anchorTime_ + static_cast<double>(k - anchorK_) * sampleRate_ / rateHz_;
    }

    double sampleRate_{48000.0};
    StreamParams params_{}; // sanitized, independent of the sample rate

    // Derived from params_ and sampleRate_ by updateDerived().
    double rateHz_{0.0}; // <= 0 when off
    double durationSamples_{0.0};
    double pitchRatio_{1.0}; // exp2Det(pitchSemitones / 12)

    // The lattice: grain k's onset is anchorTime_ + (k - anchorK_) · period.
    uint64_t nextK_{0};
    uint64_t anchorK_{0};
    double anchorTime_{0.0};
    double lastOnset_{0.0};
    bool hasLastOnset_{false};
    bool restartLattice_{false};
};
} // namespace gg

#endif // GG_GRAIN_PERIODICSTREAM_H
