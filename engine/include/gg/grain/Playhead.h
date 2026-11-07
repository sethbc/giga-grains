// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_GRAIN_PLAYHEAD_H
#define GG_GRAIN_PLAYHEAD_H

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

#include "gg/core/FpContract.h"

namespace gg
{
/// What the scan point does at the source window's edges (M3 playhead spec H3). Wrap (the
/// default) jumps to the other edge and goes on; Bounce reflects, a triangle function of the
/// unbounded offset; Stop holds the edge it reached, and leaves it at once when the speed turns
/// back into the window (canon, below).
enum class ScanEdge : uint8_t
{
    Wrap,
    Bounce,
    Stop
};

namespace detail
{
/// mod(a, b) = a - b * floor(a / b) for b > 0 (spec HB3), in [0, b). A result of b (a rounding
/// of an a just below a multiple of b) is taken as 0, and so is any other result outside [0, b),
/// which only an a far larger than b can round to.
inline double scanMod(double a, double b) noexcept
{
    GG_FP_CONTRACT_OFF
    const double m = a - b * std::floor(a / b);
    return (m >= 0.0 && m < b) ? m : 0.0;
}
} // namespace detail

/// The scan point y folded into the window [lo, hi] (spec HB3), with L = hi - lo:
/// - Wrap: lo + mod(y - lo, L), so hi itself wraps to lo;
/// - Bounce: lo + tri(y - lo), tri(u) = m <= L ? m : 2L - m with m = mod(u, 2L);
/// - Stop: clamp(y, lo, hi).
///
/// An empty or reversed window (L <= 0), or a non-finite y or L, gives lo: no NaN comes out.
inline double scanFold(double y, double lo, double hi, ScanEdge edge) noexcept
{
    GG_FP_CONTRACT_OFF
    const double length = hi - lo;
    if (!(length > 0.0) || !std::isfinite(length) || !std::isfinite(y))
    {
        return lo;
    }
    switch (edge)
    {
    case ScanEdge::Bounce:
    {
        const double twice = 2.0 * length;
        const double m = detail::scanMod(y - lo, twice);
        return lo + (m <= length ? m : twice - m);
    }
    case ScanEdge::Stop:
        return std::clamp(y, lo, hi);
    case ScanEdge::Wrap:
    default:
        return lo + detail::scanMod(y - lo, length);
    }
}

/// The source's geometry at a piece start (spec HB2), in Position's units: how far the point
/// moves per second at 1x, and the window [lo, hi] the point folds into. A Sample source of F
/// frames at rate R is R / (F - 1) over [0, 1]; a Live or Resample ring is -1 (seconds of lag a
/// second; forward is toward the head) over [0, W / sampleRate]; no source is 0, which holds.
struct ScanGeometry
{
    double unitsPerSecond{0.0};
    double lo{0.0};
    double hi{1.0};
};

/// One stream's playhead (M3 playhead spec H5, §3a HB1, HB4): the scan offset
/// x(t) = x0 + r * (t - tau0), exactly piecewise linear in the engine's sample clock. t is a
/// grain's exact onset in engine samples since reset (onsetSample + onsetFrac, HB4); r is the
/// rate in Position's units per output sample; (x0, tau0) is the last rebase. The point a grain
/// reads is Position while x(t) is exactly 0, and scanFold(Position + x(t), lo, hi, edge)
/// otherwise; a default playhead (and one restarted, at the restart's own sample) is exactly 0.
///
/// The owner calls sync() at every block start, before any grain is drawn, and it rebases only
/// on a restart or a rate or edge that differs bit for bit, which change only at grid boundaries
/// or an event's sample: the same at any host block size, so the offset is too. Never the
/// window: a ring's window can grow sample by sample (spec HB1).
///
/// A value type: no allocation, nothing but a few double operations, all noexcept. Its running
/// state is never saved.
class Playhead
{
  public:
    /// The next sync() restarts: x0 = 0 at its sample (H4: wherever the Cloud onset is forced,
    /// a voice's start, and restartPlayhead(), HB1, HB5).
    void requestRestart() noexcept { restartPending_ = true; }

    /// Brings the playhead to the block starting at t (spec HB1), with the stream's speed (x real
    /// time, after the law and any tracking), its edge, the source's geometry, the engine's
    /// sample rate and the stream's Position at t:
    /// - a restart pending: x0 = 0, tau0 = t, the new rate and edge taken, the flag cleared;
    /// - else if the new rate r' = speed * g.unitsPerSecond / sampleRate, or the edge, differs
    ///   bit for bit from the last rebase's: x0 = canon(x(t)), tau0 = t, r' and the edge taken;
    /// - else nothing.
    ///
    /// canon (read only here, so the window at the rebase's own sample): Wrap reduces x mod L,
    /// Bounce mod 2L (only to keep the offset small: the fold is periodic), and Stop gives
    /// clamp(position + x, lo, hi) - position, so a reversed speed leaves the edge at once (H3).
    /// An empty or non-finite window, or a non-finite position under Stop, leaves x as it is.
    /// A non-finite r' (a sample rate of 0, say) is taken as 0, which holds, and r' = -0.0 as
    /// +0.0: speed 0 never rebases on a change of source alone, where Stop's canon would move x0
    /// off 0 at rest with Position outside the window.
    ///
    /// A Wrap or Bounce canon can land exactly on x0 = 0 (x a multiple of L or 2L); there the
    /// at-rest guard reads plain Position, the spec's H5 as written.
    void sync(int64_t t, double speed, ScanEdge edge, const ScanGeometry &g, double sampleRate,
              double position) noexcept
    {
        GG_FP_CONTRACT_OFF
        // + 0.0 maps -0.0 (speed 0 over a ring's -1) to +0.0, so the bit comparison below sees
        // speed 0 over a Sample and over a ring as the same rate.
        double rate = speed * g.unitsPerSecond / sampleRate + 0.0;
        if (!std::isfinite(rate))
        {
            rate = 0.0;
        }
        if (restartPending_)
        {
            restartPending_ = false;
            x0_ = 0.0;
        }
        else if (std::bit_cast<uint64_t>(rate) != std::bit_cast<uint64_t>(r_) || edge != edge_)
        {
            x0_ = canon(offsetAt(static_cast<double>(t)), edge, g, position);
        }
        else
        {
            return;
        }
        tau0_ = t;
        r_ = rate;
        edge_ = edge;
    }

    /// x(t) = x0 + r * (t - tau0), t in engine samples (with its fraction).
    double offsetAt(double t) const noexcept
    {
        GG_FP_CONTRACT_OFF
        return x0_ + r_ * (t - static_cast<double>(tau0_));
    }

    /// The edge mode the last rebase took.
    ScanEdge edge() const noexcept { return edge_; }

    /// Whether the offset can be anything but 0: a rate, or a point held away from Position.
    bool moving() const noexcept { return r_ != 0.0 || x0_ != 0.0; }

  private:
    static double canon(double x, ScanEdge edge, const ScanGeometry &g, double position) noexcept
    {
        GG_FP_CONTRACT_OFF
        const double length = g.hi - g.lo;
        if (!(length > 0.0) || !std::isfinite(length))
        {
            return x;
        }
        switch (edge)
        {
        case ScanEdge::Bounce:
            return detail::scanMod(x, 2.0 * length);
        case ScanEdge::Stop:
            return std::isfinite(position) ? std::clamp(position + x, g.lo, g.hi) - position : x;
        case ScanEdge::Wrap:
        default:
            return detail::scanMod(x, length);
        }
    }

    double x0_{0.0};
    double r_{0.0};
    int64_t tau0_{0};
    ScanEdge edge_{ScanEdge::Wrap};
    bool restartPending_{true};
};
} // namespace gg

#endif // GG_GRAIN_PLAYHEAD_H
