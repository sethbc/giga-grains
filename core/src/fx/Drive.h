// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <sst/cpputils/rtsan_support.h>

#include "fx/HalfBand2x.h"
#include "gg/core/ParamIds.h"

namespace gg::core::fx
{
/// A layer's drive (color spec C2 to C4, §5.2): one of eight sst-waveshapers shapes, run at 2x
/// through HalfBand2x, level-matched at -12 dBFS by C3's comp table, then a 5 Hz DC blocker and
/// the entry blend `x + e·(D − x)`, `e = min(1, d / 0.05)`, so d = 0 is the input bit for bit.
///
/// The owner calls setCell at each control-grid boundary with the shape in force, then process
/// once per sample with Drive's ramped value d (0 to 1). A shape change follows BusFilter's type
/// rule: the incoming shape starts from rest on the spare state and crossfades over
/// `rampSamples`; a change back during the fade turns it; a third shape waits for the fade to end.
/// sst-waveshapers' state stays behind a pimpl (Drive.cpp is the one file that includes the
/// library). Deterministic; process and setCell never allocate.
class Drive
{
  public:
    Drive();
    ~Drive();
    Drive(const Drive &) = delete;
    Drive &operator=(const Drive &) = delete;

    /// Not real-time: allocates the shaper states, builds the comp table (which calls every
    /// shape once: the prewarm of ADAA's function-local constants), sets the DC blocker's
    /// coefficient and resets. `oversample` false runs the shaper at the base rate (the alias
    /// test's comparison alone).
    void prepare(double sampleRate, uint32_t rampSamples, bool oversample = true);

    /// Every state to rest (the half-bands, both shaper states as a fresh lane, the DC blocker)
    /// and no fade; the next setCell snaps to its shape.
    void reset() noexcept;

    /// Snaps to `shape` with no fade, the active shaper state fresh.
    void jump(DriveShape shape) noexcept;

    /// The shape in force for the next cell.
    void setCell(DriveShape shape) noexcept SST_CPPUTILS_NONBLOCKING;

    /// Drives one stereo sample in place; d is Drive's ramped value, 0 to 1. The state advances
    /// at d = 0 too, where the sample is left untouched.
    void process(float &l, float &r, float d) noexcept SST_CPPUTILS_NONBLOCKING;

    /// C3's level compensation for `shape` at d, read from the table by linear interpolation.
    float comp(DriveShape shape, float d) const noexcept;

    static constexpr int kShapes = 8;
    static constexpr int kCompSteps = 128; ///< The table holds d = 0, 1/128, … 1.

  private:
    struct State;

    void shapePair(float inL, float inR, float g, float cOld, float cNew, float f, float &outL,
                   float &outR) noexcept;

    std::unique_ptr<State> state_;
    HalfBand2x up_;
    HalfBand2x down_;
    std::array<float, kShapes *(kCompSteps + 1)> comp_{};
    bool oversample_{true};
    // state_->ws[active_] runs shape_; during a fade state_->ws[1 - active_] runs nextShape_.
    uint8_t active_{0};
    DriveShape shape_{DriveShape::Soft};
    DriveShape nextShape_{DriveShape::Soft};
    bool fading_{false};
    bool snapNext_{true};
    uint32_t fadePos_{0};
    uint32_t rampSamples_{1};
    float dcR_{0.0f};
    float dcXL_{0.0f}, dcXR_{0.0f}, dcYL_{0.0f}, dcYR_{0.0f};
};
} // namespace gg::core::fx
