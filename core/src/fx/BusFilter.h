// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#pragma once

#include <cstdint>

#include <sst/basic-blocks/simd/setup.h>
#include <sst/cpputils/rtsan_support.h>
#include <sst/filters/CytomicSVF.h>

#include "gg/core/ParamIds.h"

namespace gg::core::fx
{
/// One grain bus's filter (M2 sub-project 4's spec §6.3): a stereo Cytomic state-variable filter
/// (low-pass, high-pass, band-pass or notch), or none.
///
/// The owner calls setCell at each control-grid boundary (32 samples) with the bus's parameters in
/// force, then process once per sample. Cutoff and resonance glide linearly across the cell
/// (CytomicSVF::setCoeffForBlock<32>), so they never zipper. A type change starts the incoming
/// type on the spare filter, from silence, and crossfades from the outgoing one over `rampSamples`
/// (Off being no filter: the dry signal); a type change asked during a fade waits for it to end
/// (the owner asks again each cell). Off with no fade in progress leaves the samples untouched
/// (the sign of zero included). Everything is deterministic and allocation-free.
class BusFilter
{
  public:
    /// Sets the sample rate and the crossfade length, and resets. Not real-time.
    void prepare(double sampleRate, uint32_t rampSamples);

    /// Back to Off, silent, with no fade in progress.
    void reset() noexcept;

    /// Snaps to `type` with no fade: any fade ends, and the active filter, silent, runs `type`
    /// from the next setCell, its coefficients snapped. For the owner's first setCell after a
    /// clear (construction, reset, a layer switched back on), where a fade from Off would let the
    /// first 10 ms through unfiltered.
    void jump(BusFilterType type) noexcept;

    /// The bus's parameters for the next cell (32 samples): the cutoff is clamped to [20,
    /// min(20000, 0.45 × sample rate)] Hz and the filter's resonance is 0.95 × `resonance` (0 to
    /// 1).
    void setCell(BusFilterType type, float cutoffHz,
                 float resonance) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Filters one stereo sample in place.
    void process(float &l, float &r) noexcept SST_CPPUTILS_NONBLOCKING;

  private:
    using Svf = sst::filters::CytomicSVF;

    static Svf::Mode modeOf(BusFilterType type) noexcept;
    void setCoeffs(Svf &f, BusFilterType type) noexcept;

    Svf filter_[2];
    // filter_[active_] runs type_; during a fade filter_[1 - active_] runs nextType_.
    uint8_t active_{0};
    BusFilterType type_{BusFilterType::Off};
    BusFilterType nextType_{BusFilterType::Off};
    bool fading_{false};
    uint32_t fadePos_{0};
    uint32_t rampSamples_{1};
    float cutoffMax_{20000.0f};
    float srInv_{1.0f / 48000.0f};
    // The cell's clamped cutoff and filter resonance.
    float cutoff_{20000.0f};
    float res_{0.0f};
};
} // namespace gg::core::fx
