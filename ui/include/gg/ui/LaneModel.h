// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_LANEMODEL_H
#define GG_UI_LANEMODEL_H

#include <cstdint>
#include <optional>

namespace gg::ui
{
/// The canvas's lane geometry (spec §6.2–6.3): where each playing grain's capsule lies, where
/// the scan window lies, and the inverse mappings a drag needs. Pure functions, framework-free,
/// for the UI thread only: they may use libm (std::log2, std::exp2), since nothing here reaches
/// the audio or the grain-event log.
///
/// Coordinates are in physical pixels, as SplatRenderer's: x runs 0 .. width left to right and
/// y 0 .. height top to bottom.
///
/// - **FX lane:** the live ring, with the write head fixed at the right edge as "now". A ring
///   frame `pos` with the head at `head` lies at x = width × (1 − behind / ringSeconds), with
///   behind = (head − pos) / sampleRate seconds: history scrolls left as the head moves on.
/// - **Instrument lane:** static. Source frame `pos` lies at x = width × pos / sourceFrames.
struct LaneGeometry
{
    float width;          ///< The lane's width in pixels; > 0 to place anything.
    float height;         ///< The lane's height in pixels.
    float pixelScale;     ///< Physical pixels per logical pixel.
    bool fxLane;          ///< The FX's live-ring lane, or the instrument's sample lane.
    double sampleRate;    ///< The engine's sample rate, Hz.
    double ringSeconds;   ///< FX: the seconds the lane spans (the live ring's 16).
    int64_t sourceFrames; ///< Instrument: the loaded sample's length in frames.
};

/// One grain as the lane needs it: the six fields of the engine's spawn event (gg-core's
/// GrainVis), copied in by the canvas so this header depends on nothing but the standard
/// library. Every field is latched at the grain's onset.
struct LaneGrain
{
    int64_t onsetSample;   ///< The engine sample the grain starts at.
    double startPos;       ///< Where it starts reading: ring time (FX) or source frames.
    float rate;            ///< Frames read per output sample (negative reads backwards).
    float durationSamples; ///< Its length in output samples.
    float gain;            ///< Its gain; the capsule's brightness, clamped to 0..1.
    float pan;             ///< Its pan, −1 (left) .. 1 (right); the capsule's height.
};

/// The arguments of SplatRenderer::addCapsule, with x0 <= x1.
struct Capsule
{
    float x0, x1, y, radius, intensity;
};

/// The capsule for `grain` at UI time `nowSample` (engine samples, extrapolated from the engine
/// clock), with the write head at `headAtNow` (ring time; the FX lane only). It is drawn while
/// onsetSample <= now < onsetSample + durationSamples; nullopt otherwise, or when the grain or
/// the geometry can't be placed (a non-finite field, a duration or a lane that isn't positive).
///
/// - x0 .. x1 span the grain's read extent, startPos .. startPos + rate × durationSamples.
/// - y = height/2 + pan × (height/2 − radius), so pan +1 is the bottom edge and the capsule
///   stays on the lane; pan is clamped to −1 .. 1 (NaN draws centred), and a lane shorter than
///   the capsule draws it centred.
/// - radius = max(1.5, 3 × pixelScale).
/// - intensity = clamp(gain, 0, 1) (NaN draws dark).
std::optional<Capsule> grainCapsule(const LaneGrain &grain, int64_t nowSample, int64_t headAtNow,
                                    const LaneGeometry &geometry) noexcept;

/// The scan window, in pixels, x0 <= x1.
struct Window
{
    float x0, x1;
};

/// The scan window for Position and Spray's CLAP values (spec §6.2): centred at Position, with
/// edges at ±Spray. Each value is clamped to 0..1 first (NaN to 0).
///
/// - FX: Position is 16/255 × (2^(8v) − 1) seconds behind the head (ParamMap's taper), so the
///   centre is at width × (1 − seconds / ringSeconds).
/// - Instrument: Position is the fraction of the file, so the centre is at width × v.
/// - Both: Spray is 2/255 × (2^(8v) − 1) seconds either side, converted to pixels at the lane's
///   scale (width / ringSeconds, or width × sampleRate / sourceFrames).
///
/// The FX lane is relative to the head, so the window doesn't move as the head does:
/// `headAtNow` is part of the interface for the canvas's convenience and is unused.
Window scanWindow(float positionValue, float sprayValue, int64_t headAtNow,
                  const LaneGeometry &geometry) noexcept;

/// The Position value whose window is centred at x: scanWindow's inverse, for a body drag.
/// Clamped to 0..1, so a drag past either end of the lane pins Position to that end (left of
/// the FX lane is 1, the oldest audio; left of the instrument lane is 0), infinities included.
/// A NaN x or a degenerate lane gives 0.
float positionValueAtX(float x, const LaneGeometry &geometry) noexcept;

/// The Spray value whose window is ±halfWidthPx wide: scanWindow's inverse, for an edge drag.
/// Clamped to 0..1; a NaN or negative half-width, or a degenerate lane, gives 0.
float sprayValueForHalfWidth(float halfWidthPx, const LaneGeometry &geometry) noexcept;

/// What a press at x grabs.
enum class DragTarget
{
    None,
    Body,      ///< Sets Position.
    LeftEdge,  ///< Sets Spray.
    RightEdge, ///< Sets Spray.
};

/// Hit-tests x against the window. Each edge grabs within edgeTolerancePx outside the window
/// and within min(edgeTolerancePx, width / 3) inside it, so the middle third of a narrow window
/// stays the body; everything else inside is the body. A window of no width is the body only
/// at its own x, with an edge either side. A non-finite x grabs nothing.
DragTarget hitTest(float x, const Window &window, float edgeTolerancePx) noexcept;
} // namespace gg::ui

#endif // GG_UI_LANEMODEL_H
