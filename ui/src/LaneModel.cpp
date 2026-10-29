// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/LaneModel.h"

#include <algorithm>
#include <cmath>

// UI-thread display code: it works in double and uses libm freely. Nothing here reaches the
// audio or the grain-event log, so the audio path's determinism rules (INTENT §14) don't bind it.

namespace gg::ui
{
namespace
{
/// ParamMap's tapers (core/src/ParamMap.cpp): FX Position spans 0..16 s behind the head and
/// Spray 0..2 s, each as k / 255 × (2^(8v) − 1) seconds.
constexpr double kPositionSpanSeconds = 16.0;
constexpr double kSpraySpanSeconds = 2.0;

double taperSeconds(double span, double v) { return span / 255.0 * (std::exp2(8.0 * v) - 1.0); }

/// The taper's inverse, clamped to 0..1. seconds must not be NaN.
double taperValue(double span, double seconds)
{
    const double v = std::log2(1.0 + 255.0 * std::max(seconds, 0.0) / span) / 8.0;
    return std::min(v, 1.0);
}

/// v clamped to 0..1, with NaN mapped to 0.
double clamp01(double v) { return v >= 0.0 ? (v <= 1.0 ? v : 1.0) : 0.0; }

/// Whether the lane can place anything.
bool usable(const LaneGeometry &g)
{
    if (!(std::isfinite(g.width) && g.width > 0.0f) ||
        !(std::isfinite(g.height) && g.height > 0.0f) ||
        !(std::isfinite(g.sampleRate) && g.sampleRate > 0.0))
    {
        return false;
    }
    return g.fxLane ? std::isfinite(g.ringSeconds) && g.ringSeconds > 0.0 : g.sourceFrames > 0;
}

/// Pixels per second of audio along the lane.
double pixelsPerSecond(const LaneGeometry &g)
{
    return g.fxLane ? g.width / g.ringSeconds
                    : g.width * g.sampleRate / static_cast<double>(g.sourceFrames);
}

/// The x of ring time (FX) or source frame (instrument) `pos`, with the head at `head`.
double laneX(double pos, int64_t head, const LaneGeometry &g)
{
    if (g.fxLane)
    {
        const double behindSeconds = (static_cast<double>(head) - pos) / g.sampleRate;
        return g.width * (1.0 - behindSeconds / g.ringSeconds);
    }
    return g.width * pos / static_cast<double>(g.sourceFrames);
}

/// Far past any canvas, yet a float: x is clamped here before it becomes one, so a grain far
/// off the lane is still a valid (clipped) capsule and the conversion stays defined.
constexpr double kMaxPixels = 1.0e9;

float toPixel(double x) { return static_cast<float>(std::clamp(x, -kMaxPixels, kMaxPixels)); }
} // namespace

std::optional<Capsule> grainCapsule(const LaneGrain &grain, int64_t nowSample, int64_t headAtNow,
                                    const LaneGeometry &geometry) noexcept
{
    const double duration = grain.durationSamples;
    if (!usable(geometry) || !std::isfinite(grain.startPos) || !std::isfinite(grain.rate) ||
        !(std::isfinite(duration) && duration > 0.0) || nowSample < grain.onsetSample)
    {
        return std::nullopt;
    }
    // now >= onset, so the unsigned difference is exact and can't overflow.
    const auto elapsed = static_cast<double>(static_cast<uint64_t>(nowSample) -
                                             static_cast<uint64_t>(grain.onsetSample));
    if (!(elapsed < duration))
    {
        return std::nullopt;
    }

    const double end = grain.startPos + static_cast<double>(grain.rate) * duration;
    const double xa = laneX(grain.startPos, headAtNow, geometry);
    const double xb = laneX(end, headAtNow, geometry);
    if (!std::isfinite(xa) || !std::isfinite(xb))
    {
        return std::nullopt;
    }

    const float scaled = 3.0f * geometry.pixelScale;
    const float radius = scaled > 1.5f ? scaled : 1.5f; // NaN gives 1.5
    const double pan = std::isnan(grain.pan) ? 0.0 : std::clamp<double>(grain.pan, -1.0, 1.0);
    const double halfHeight = 0.5 * geometry.height;
    const double reach = halfHeight - radius; // how far the centre may move from the middle
    const double y = reach > 0.0 ? halfHeight + pan * reach : halfHeight;

    return Capsule{.x0 = toPixel(std::min(xa, xb)),
                   .x1 = toPixel(std::max(xa, xb)),
                   .y = static_cast<float>(y),
                   .radius = radius,
                   .intensity = static_cast<float>(clamp01(grain.gain))};
}

Window scanWindow(float positionValue, float sprayValue, const LaneGeometry &geometry) noexcept
{
    if (!usable(geometry))
    {
        return {0.0f, 0.0f};
    }
    const double position = clamp01(positionValue);
    const double spray =
        taperSeconds(kSpraySpanSeconds, clamp01(sprayValue)) * pixelsPerSecond(geometry);
    if (geometry.fxLane)
    {
        // One-sided (ruling PU-T8a): from Position back to Position + Spray seconds.
        const double newest = geometry.width * (1.0 - taperSeconds(kPositionSpanSeconds, position) /
                                                          geometry.ringSeconds);
        return {toPixel(newest - spray), toPixel(newest)};
    }
    const double centre = geometry.width * position;
    return {toPixel(centre - spray), toPixel(centre + spray)};
}

std::optional<Window> ghostWindow(const ScanValues &base, const ScanValues &effective,
                                  const LaneGeometry &geometry) noexcept
{
    if (std::abs(effective.position - base.position) > kGhostThreshold ||
        std::abs(effective.spray - base.spray) > kGhostThreshold)
    {
        return scanWindow(effective.position, effective.spray, geometry);
    }
    return std::nullopt;
}

float positionValueAtX(float x, const LaneGeometry &geometry) noexcept
{
    if (std::isnan(x) || !usable(geometry))
    {
        return 0.0f;
    }
    const double fraction = static_cast<double>(x) / geometry.width;
    if (!geometry.fxLane)
    {
        return static_cast<float>(clamp01(fraction));
    }
    const double behindSeconds = (1.0 - fraction) * geometry.ringSeconds; // ±inf for ∓inf x
    return static_cast<float>(taperValue(kPositionSpanSeconds, behindSeconds));
}

float sprayValueForExtent(float extentPx, const LaneGeometry &geometry) noexcept
{
    if (!(extentPx >= 0.0f) || !usable(geometry))
    {
        return 0.0f;
    }
    // The FX's full width and the instrument's half-width are both Spray's seconds in pixels.
    const double seconds = static_cast<double>(extentPx) / pixelsPerSecond(geometry);
    return static_cast<float>(taperValue(kSpraySpanSeconds, seconds));
}

DragTarget hitTest(float x, const Window &window, float edgeTolerancePx,
                   const LaneGeometry &geometry) noexcept
{
    if (!std::isfinite(x))
    {
        return DragTarget::None;
    }
    const float lo = std::min(window.x0, window.x1);
    const float hi = std::max(window.x0, window.x1);
    const float tolerance = edgeTolerancePx > 0.0f ? edgeTolerancePx : 0.0f; // NaN gives 0
    const float inside = std::min(tolerance, (hi - lo) / 3.0f);
    if (x >= lo - tolerance && x < lo + inside)
    {
        return DragTarget::LeftEdge;
    }
    if (geometry.fxLane)
    {
        // The FX window's right edge is Position itself (ruling PU-T8a): the body reaches it and
        // its tolerance.
        return x >= lo + inside && x <= hi + tolerance ? DragTarget::Body : DragTarget::None;
    }
    if (x >= lo + inside && x <= hi - inside)
    {
        return DragTarget::Body;
    }
    if (x > hi - inside && x <= hi + tolerance)
    {
        return DragTarget::RightEdge;
    }
    return DragTarget::None;
}
} // namespace gg::ui
