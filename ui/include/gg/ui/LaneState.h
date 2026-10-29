// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_LANESTATE_H
#define GG_UI_LANESTATE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gg/ui/LaneModel.h"

namespace gg::ui
{
/// The canvas's UI time (spec §6.4): the engine's sample clock, extrapolated between the blocks
/// that publish it. The audio thread publishes its clock once a block; the UI draws at the
/// display's rate, so between two blocks it runs the last clock on at the sample rate by the wall
/// time since it changed. UI thread only; framework-free, for the tests.
///
/// - **Never backwards** within one activation: a block that lands behind the extrapolation holds
///   now() until the clock passes it, so a grain never flickers back to an earlier frame.
/// - **A clock that goes backwards is a new activation** (a reactivated Processor starts again
///   from 0): update() says so, and now() follows the new clock at once.
/// - **At most kMaxExtrapolationSeconds past the last change**, so a host that stops processing
///   (a stopped transport in a host that suspends the plugin) stops the lane instead of scrolling
///   it on forever. That is longer than a block at any rate a host uses (8,192 frames at
///   44.1 kHz are 186 ms).
class LaneClock
{
  public:
    static constexpr double kMaxExtrapolationSeconds = 0.25;

    /// Takes the clock as published at this frame, the wall time in seconds (any steady origin)
    /// and the sample rate. Returns true if the clock went backwards since the last update (a new
    /// activation): the caller forgets the grains it holds. A sample rate that isn't positive and
    /// finite extrapolates nothing.
    bool update(int64_t publishedClock, double wallSeconds, double sampleRate) noexcept;

    /// The extrapolated engine sample at the last update; 0 before the first.
    int64_t now() const noexcept { return now_; }
    /// Whether update() has been called since construction or reset().
    bool valid() const noexcept { return valid_; }
    /// Forgets the clock (the plugin is inactive): the next update starts afresh.
    void reset() noexcept;

  private:
    bool valid_{false};
    int64_t published_{0};  // the last clock update() was given
    double changedAt_{0.0}; // the wall time it last changed
    int64_t now_{0};
};

/// The write head at UI time `now` (FX lane): the snapshot's head run on by the samples since the
/// snapshot's clock, or the snapshot's head itself while frozen (the scroll stops; spec §6.2).
/// Never behind the snapshot's head.
int64_t headAtNow(int64_t writeHead, int64_t snapshotClock, int64_t now, bool frozen) noexcept;

/// A range of ring frames, [first, end); empty when end <= first.
struct FrameSpan
{
    int64_t first;
    int64_t end;
};

/// The ring frames column x (0 .. columns − 1) of the FX lane's peak strip reads, the lane
/// spanning columns × framesPerColumn frames up to `head` (headAtNow at UI time): from
/// head − (columns − x) × framesPerColumn to head − (columns − x − 1) × framesPerColumn, rounded
/// outwards, then clamped below to `oldest` (the strip's oldest whole bucket, which the canvas
/// measures from `head`, since the writer may be past `written`) and above to
/// `written`, the frames the ring has actually written: headAtNow at the published clock rather
/// than the extrapolated one. head runs on past the last published block by extrapolation, and
/// the strip wraps, so a frame at or past `written` would read a bucket a ring's length old.
/// Empty for a column wholly outside [oldest, written).
FrameSpan peakColumnFrames(int x, int columns, double framesPerColumn, int64_t head, int64_t oldest,
                           int64_t written) noexcept;

/// The grains the canvas draws: those drained from the engine's grain queue that haven't ended,
/// in the order they were spawned. Fixed in size (kCapacity, allocated at construction): a push
/// to a full list drops the oldest grain, so a burst of grains never grows it. UI thread only.
class LiveGrains
{
  public:
    static constexpr std::size_t kCapacity = 8192;

    LiveGrains();

    /// Appends `grain`, dropping the oldest one when the list holds kCapacity.
    void push(const LaneGrain &grain) noexcept;
    /// Removes every grain that has ended by `now` (now ≥ onset + duration), and every grain
    /// whose duration isn't positive and finite. Grains that haven't started are kept.
    void prune(int64_t now) noexcept;
    void clear() noexcept;
    std::size_t size() const noexcept { return size_; }

    /// Calls f(const LaneGrain &) for each grain, oldest first.
    template <class F> void forEach(F &&f) const
    {
        for (std::size_t i = 0; i < size_; ++i)
        {
            f(slots_[(first_ + i) % kCapacity]);
        }
    }

  private:
    std::vector<LaneGrain> slots_;
    std::size_t first_{0}; // the oldest grain's slot
    std::size_t size_{0};
};
} // namespace gg::ui

#endif // GG_UI_LANESTATE_H
