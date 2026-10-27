// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_SOURCE_LIVESOURCE_H
#define GG_SOURCE_LIVESOURCE_H

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include <sst/cpputils/rtsan_support.h>

namespace gg
{
/// The live ring source (spec §6.3), internal to the Engine: a stereo ring that the FX writes its
/// input into before any grain of the same block reads it.
///
/// **Ring time.** Frames are numbered from 0 at construction or reset(). R(t), the write head, is
/// the number of frames written through output sample t. At t the ring's window is
/// [R(t) − N, R(t) − 1]: the newest frame is R(t) − 1 and the oldest readable one is R(t) − N.
/// Not frozen, the head advances one frame per output sample. Indices below 0 (before the first
/// frame) read as silence, so the ring never needs clearing.
///
/// **Storage** (ruling M1-o). Frame i is kept in slot i mod C, with C = N + B physical slots,
/// where B is the largest block write() takes. A block's input is written whole before its
/// samples render, so when sample t of a block renders, the frames R(t) .. R(t) + B − 2 may
/// already be in the ring. With C = N + B they overwrite frames R(t) − N − B .. R(t) − N − 2,
/// all below the oldest frame t may read, R(t) − N. So every frame in t's window is the one
/// written for it, and what a sample reads depends neither on the host's block size nor on B.
///
/// **Freeze.**
/// - On: the write gain ramps from 1 to 0 over L = freezeRampSamples() samples while the head
///   keeps advancing (the L-th frame is written with gain 0), then the head stops.
/// - Off: the head resumes at once and the gain ramps back to 1 over L samples.
/// - A toggle during a ramp continues from the current gain.
///
/// The gain is ramp / L for an integer ramp in [0, L], so it is exact, and it depends only on
/// which samples were frozen, never on where blocks fall.
///
/// **Input.** Non-finite samples are written as 0, and finite ones are clamped to
/// ±kMaxInputMagnitude. A mono input fills both channels; a missing input, or a missing channel
/// of a stereo one, is silence.
///
/// **Reads** never touch a frame outside [R(t) − N, R(t) − 1]; see fade() and read().
class LiveSource
{
  public:
    /// M: the head-proximity margin and fade length, in frames (spec §6.3; INTENT §14's
    /// 256-sample head-crossing crossfade).
    static constexpr int64_t kHeadMargin = 256;
    /// The freeze write-gain ramp, in seconds; freezeRampSamples() rounds it to samples.
    static constexpr double kFreezeRampSeconds = 0.02;
    /// Finite input beyond ± this (2^20, about +120 dBFS) is clamped. It is far above any real
    /// signal, and low enough that no sum of grains can overflow a float.
    static constexpr float kMaxInputMagnitude = 1048576.0f;

    /// L = round(kFreezeRampSeconds × sampleRate) samples, at least 1.
    static int64_t freezeRampSamples(double sampleRate) noexcept;

    /// Allocates a ring whose window is N = ceil(seconds × sampleRate) frames (no ring when
    /// seconds is 0), stored in N + maxBlockSize slots (see the class comment), and resets. Not
    /// real-time safe. sampleRate must be finite and > 0, and maxBlockSize > 0 (0 counts as 1).
    /// Throws std::invalid_argument when seconds is not finite and >= 0, or when the storage
    /// would exceed 2^40 frames.
    void prepare(double sampleRate, double seconds, uint32_t maxBlockSize);

    /// N, the ring length in frames; 0 when there is no ring.
    int64_t frames() const noexcept { return frames_; }
    bool enabled() const noexcept { return frames_ > 0; }

    /// Restarts ring time at 0: the head at 0, not frozen, write gain 1. The old frames stay in
    /// memory, but no read can reach them again.
    void reset() noexcept;

    /// The freeze setting for the next write() and after.
    void setFrozen(bool frozen) noexcept { frozen_ = frozen; }

    /// Writes the input for output samples [blockStart, blockStart + numFrames), where
    /// in[c] + offset is channel c's first sample, and records R(t) for those samples (headAt()
    /// and headRunAfter()). Blocks must be contiguous from reset(), and numFrames at most the
    /// maxBlockSize given to prepare(): a longer block would overwrite frames its own earlier
    /// samples read. Requires enabled().
    void write(const float *const *in, uint32_t numInChannels, uint32_t offset, int64_t blockStart,
               uint32_t numFrames) noexcept SST_CPPUTILS_NONBLOCKING;

    /// R after the last write(): the frames written since reset().
    int64_t head() const noexcept { return head_; }

    /// R(t), for an output sample t of the last write() block. The head advances on a prefix of
    /// the block (all of it unless a freeze stops it), so R(t) = R(blockStart − 1) +
    /// min(t − blockStart + 1, frames written in the block).
    int64_t headAt(int64_t t) const noexcept
    {
        return blockHead_ + std::min(t - blockStart_ + 1, blockAdvance_);
    }

    /// How many more frames the head will advance after output sample t of the last write()
    /// block, if the freeze setting doesn't change: +infinity when not frozen; when frozen, the
    /// frames left in the ramp, 0 once the head has stopped.
    double headRunAfter(int64_t t) const noexcept
    {
        if (!frozen_)
        {
            return std::numeric_limits<double>::infinity();
        }
        return static_cast<double>(blockRamp_ - std::min(t - blockStart_ + 1, blockRamp_));
    }

    /// The head-proximity fade for a read at pos (ring time) while the head is at R (spec §6.3):
    ///
    ///     g = clamp((R − 1 − (pos + 2)) / M, 0, 1) × clamp(((pos − 1) − (R − N)) / M, 0, 1)
    ///
    /// It is 1 while every tap is at least M frames inside both ends of the window, and falls
    /// linearly to 0 over M frames as a tap nears either end: the newest frame, or the oldest
    /// one, R − N, which the next frame written moves out of the window. (The slot it overwrites
    /// holds frame R − N − B, already outside the window; see Storage.) Frozen, those two ends are
    /// the seam where the ring's newest and oldest frames meet, so this is the head-crossing
    /// crossfade.
    double fade(double pos, int64_t head) const noexcept
    {
        const auto r = static_cast<double>(head);
        const double toHead = (r - 1.0 - (pos + 2.0)) / kMargin;
        const double fromTail = ((pos - 1.0) - (r - static_cast<double>(frames_))) / kMargin;
        return unit(toHead) * unit(fromTail);
    }

    /// Where the last read() was, so that reads moving forward find their slot without a
    /// division. A fresh Cursor works for any read.
    struct Cursor
    {
        int64_t index{0}; ///< the ring-time index of the last read's first tap
        int64_t slot{0};  ///< that index's slot
        bool valid{false};
    };

    /// Reads both channels at pos (ring time) with interp::catmullRom, the 4-point Hermite of
    /// interp::hermite4, over taps floor(pos) − 1 .. floor(pos) + 2. Returns false and reads
    /// nothing unless every tap is in the window [head − N, head − 1], where head is R(t) of a
    /// sample t of the last write() block; every frame there is the one written for it (see
    /// Storage in the class comment). Taps before frame 0 read as 0.
    bool read(double pos, int64_t head, Cursor &cursor, float &left,
              float &right) const noexcept SST_CPPUTILS_NONBLOCKING;

  private:
    static constexpr double kMargin = static_cast<double>(kHeadMargin);
    // Taps i − 1 .. i + 2 read slots s .. s + 3 of a channel. Slots C .. C + 2 mirror slots 0 .. 2,
    // so the four are contiguous wherever s falls.
    static constexpr int64_t kGuardFrames = 3;

    // clamp(x, 0, 1), with NaN read as 0.
    static double unit(double x) noexcept { return x > 0.0 ? (x < 1.0 ? x : 1.0) : 0.0; }

    std::vector<float> storage_; // channel c starts at c × (C + kGuardFrames)
    float *channels_[2]{nullptr, nullptr};
    int64_t frames_{0};     // N, the window
    int64_t capacity_{0};   // C = N + maxBlockSize slots (ruling M1-o)
    int64_t rampLength_{1}; // L

    bool frozen_{false};
    int64_t ramp_{1};      // the write gain is ramp_ / rampLength_, ramp_ in [0, rampLength_]
    int64_t head_{0};      // R
    int64_t writeSlot_{0}; // head_ mod C

    // The last write() block.
    int64_t blockStart_{0};
    int64_t blockHead_{0};    // R before the block
    int64_t blockAdvance_{0}; // frames written in the block, on its first blockAdvance_ samples
    int64_t blockRamp_{0};    // ramp_ before the block
};
} // namespace gg

#endif // GG_SOURCE_LIVESOURCE_H
