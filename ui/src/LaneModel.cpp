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
        !(std::isfinite(g.height) && g.height > 0.0f) || !std::isfinite(g.top) ||
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

/// A capsule's radius on the lane: 3 logical pixels, at least 1.5 physical ones.
float capsuleRadius(const LaneGeometry &g)
{
    const float scaled = 3.0f * g.pixelScale;
    return scaled > 1.5f ? scaled : 1.5f; // NaN gives 1.5
}

/// The y of the lane's centre, where a pitch of 0 semitones lies.
double laneCentre(const LaneGeometry &g) { return g.top + 0.5 * g.height; }

/// How far a pitch may move from the lane's centre, kLaneSemitones' worth (the half-height less
/// the capsules' radius); not positive when the lane can't place a pitch.
double pitchReach(const LaneGeometry &g) { return 0.5 * g.height - capsuleRadius(g); }

/// v clamped to lo..hi, with NaN mapped to 0 (inside both ranges used here).
double clampOrZero(double v, double lo, double hi)
{
    return std::isnan(v) ? 0.0 : std::clamp(v, lo, hi);
}

/// Pitch's range in semitones and Pitch Spread's (ParamIds' metadata).
constexpr double kPitchMax = 24.0;
constexpr double kPitchSpreadMax = 24.0;
} // namespace

float pitchY(float rate, const LaneGeometry &g, float radius) noexcept
{
    if (!std::isfinite(g.top) || !std::isfinite(g.height))
    {
        return 0.0f;
    }
    const double centre = laneCentre(g);
    const double reach = 0.5 * g.height - radius; // NaN for a NaN radius: centred below
    if (!(rate > 0.0f) || !std::isfinite(rate) || !(reach > 0.0))
    {
        return static_cast<float>(centre);
    }
    const double semitones = 12.0 * std::log2(static_cast<double>(rate));
    const double y = centre - semitones * reach / kLaneSemitones;
    // reach > 0, so the bounds are in order.
    return static_cast<float>(std::clamp(y, g.top + static_cast<double>(radius),
                                         g.top + static_cast<double>(g.height) - radius));
}

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

    // The vertical axis is pitch (D1); pan is no longer drawn.
    const float radius = capsuleRadius(geometry);
    return Capsule{.x0 = toPixel(std::min(xa, xb)),
                   .x1 = toPixel(std::max(xa, xb)),
                   .y = pitchY(grain.rate, geometry, radius),
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

WindowRect scanRect(const ScanRectValues &v, const LaneGeometry &g) noexcept
{
    if (!usable(g))
    {
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }
    const Window w = scanWindow(v.position, v.spray, g);
    const double pitch = clampOrZero(v.pitch, -kPitchMax, kPitchMax);
    const double spread = clampOrZero(v.pitchSpread, 0.0, kPitchSpreadMax);
    const double centre = laneCentre(g);
    // pitchY's mapping, without its clamp: |pitch| + spread <= kLaneSemitones keeps it on the
    // lane. A lane too short to place a pitch gives a band at its centre.
    const double reach = std::max(pitchReach(g), 0.0);
    return {w.x0, w.x1, static_cast<float>(centre - (pitch + spread) * reach / kLaneSemitones),
            static_cast<float>(centre - (pitch - spread) * reach / kLaneSemitones)};
}

std::optional<WindowRect> ghostRect(const ScanRectValues &base, const ScanRectValues &effective,
                                    const LaneGeometry &g) noexcept
{
    const auto finite = [](const ScanRectValues &v) {
        return std::isfinite(v.position) && std::isfinite(v.spray) && std::isfinite(v.pitch) &&
               std::isfinite(v.pitchSpread);
    };
    if (!finite(base) || !finite(effective))
    {
        return std::nullopt;
    }
    // Each difference in its own 0..1 domain: Pitch spans 48 semitones, Pitch Spread 24.
    const auto differs = [](float a, float b, double span) {
        return std::abs(static_cast<double>(a) - static_cast<double>(b)) / span > kGhostThreshold;
    };
    if (differs(effective.position, base.position, 1.0) ||
        differs(effective.spray, base.spray, 1.0) ||
        differs(effective.pitch, base.pitch, 2.0 * kPitchMax) ||
        differs(effective.pitchSpread, base.pitchSpread, kPitchSpreadMax))
    {
        return scanRect(effective, g);
    }
    return std::nullopt;
}

DragTarget hitTestRect(float x, float y, const WindowRect &w, float edgeTolerancePx,
                       const LaneGeometry &g) noexcept
{
    if (!std::isfinite(x) || !std::isfinite(y) || !usable(g) || x < 0.0f || x > g.width ||
        y < g.top || y > g.top + g.height)
    {
        return DragTarget::None;
    }
    // Across: M1's rules, the FX's right side as the body included.
    const DragTarget across = hitTest(x, Window{w.x0, w.x1}, edgeTolerancePx, g);
    if (across == DragTarget::None)
    {
        return DragTarget::None;
    }
    // Down (ruling VW-T2b): the body is the rectangle less the tolerance at each edge, but at
    // least 2 × the tolerance high, centred, so a thin band (the default Pitch Spread 0) keeps a
    // body to grab. Each edge grabs from the tolerance beyond the rectangle's edge or the body's,
    // whichever is further out, to the body: M1's rule for a rectangle 4 tolerances high or more.
    const float lo = std::min(w.y0, w.y1);
    const float hi = std::max(w.y0, w.y1);
    const float tolerance = edgeTolerancePx > 0.0f ? edgeTolerancePx : 0.0f; // NaN gives 0
    const float centre = 0.5f * (lo + hi);
    const float half = std::max(tolerance, 0.5f * (hi - lo) - tolerance);
    const float bodyLo = centre - half;
    const float bodyHi = centre + half;
    DragTarget down = DragTarget::None;
    if (y >= bodyLo && y <= bodyHi)
    {
        down = DragTarget::Body;
    }
    else if (y < bodyLo && y >= std::min(lo, bodyLo) - tolerance)
    {
        down = DragTarget::TopEdge;
    }
    else if (y > bodyHi && y <= std::max(hi, bodyHi) + tolerance)
    {
        down = DragTarget::BottomEdge;
    }
    else
    {
        return DragTarget::None;
    }
    // A corner takes the horizontal edge (spec §6.3): a left or right edge wins.
    return across == DragTarget::Body ? down : across;
}

float pitchValueAtY(float y, const LaneGeometry &g) noexcept
{
    const double reach = pitchReach(g);
    if (std::isnan(y) || !usable(g) || !(reach > 0.0))
    {
        return 0.0f;
    }
    const double semitones = (laneCentre(g) - y) * kLaneSemitones / reach; // ±inf for ∓inf y
    return static_cast<float>(std::clamp(semitones, -kPitchMax, kPitchMax));
}

float pitchSpreadForExtent(float extentPx, const LaneGeometry &g) noexcept
{
    const double reach = pitchReach(g);
    if (!(extentPx >= 0.0f) || !usable(g) || !(reach > 0.0))
    {
        return 0.0f;
    }
    const double semitones = static_cast<double>(extentPx) * kLaneSemitones / reach;
    return static_cast<float>(std::min(semitones, kPitchSpreadMax));
}

DragAxis decideAxis(float dx, float dy, float thresholdPx) noexcept
{
    const float ax = std::isnan(dx) ? 0.0f : std::abs(dx);
    const float ay = std::isnan(dy) ? 0.0f : std::abs(dy);
    if (!(std::max(ax, ay) >= thresholdPx)) // a NaN threshold never decides
    {
        return DragAxis::Undecided;
    }
    return ax >= ay ? DragAxis::Horizontal : DragAxis::Vertical;
}
} // namespace gg::ui
