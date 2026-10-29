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

/// The scan window for Position and Spray's CLAP values (spec §6.2; ruling PU-T8a): where the
/// grains read. Each value is clamped to 0..1 first (NaN to 0). Spray is 2/255 × (2^(8v) − 1)
/// seconds (ParamMap's taper), converted to pixels at the lane's scale (width / ringSeconds, or
/// width × sampleRate / sourceFrames).
///
/// - FX: one-sided, as the engine draws a live grain's position, Position + Spray × u with u in
///   [0, 1) (CloudScheduler): from Position to Position + Spray seconds behind the head. Position
///   is 16/255 × (2^(8v) − 1) seconds behind the head, so x1, the window's newer (right) edge, is
///   Position's x, width × (1 − seconds / ringSeconds), and x0 lies Spray's pixels further left.
///   The lane is relative to the head, so the window doesn't move as the head does.
/// - Instrument: ±Spray around Position, as the engine spreads a sample grain's spray either
///   side. Position is the fraction of the file, so the centre is at width × v.
Window scanWindow(float positionValue, float sprayValue, const LaneGeometry &geometry) noexcept;

/// Position and Spray's CLAP values, as one UiSnapshot field pair (gg-core's; copied in by the
/// canvas, as LaneGrain is) gives them: the base values or the effective ones.
struct ScanValues
{
    float position;
    float spray;
};

/// How far an effective value must be from its base value for the ghost window to show.
inline constexpr float kGhostThreshold = 1.0e-4f;

/// The ghost window (spec §6.2): where the grains read once macros and modulation are added, the
/// scan window at the effective values, when Position's or Spray's effective value is more than
/// kGhostThreshold from its base value (both of the same block); nullopt when both are within it
/// (or either difference is NaN).
std::optional<Window> ghostWindow(const ScanValues &base, const ScanValues &effective,
                                  const LaneGeometry &geometry) noexcept;

/// The Position value whose window is anchored at x, scanWindow's inverse, for a body drag: the
/// window's right edge (x1) on the FX lane, its centre on the instrument's. Clamped to 0..1, so
/// a drag past either end of the lane pins Position to that end (left of the FX lane is 1, the
/// oldest audio; left of the instrument lane is 0), infinities included. A NaN x or a
/// degenerate lane gives 0.
float positionValueAtX(float x, const LaneGeometry &geometry) noexcept;

/// The Spray value whose window has the extent extentPx, scanWindow's inverse, for an edge drag:
/// on the FX lane the window's full width (x1 − x0, from Position to the dragged older edge), on
/// the instrument's its half-width (the dragged edge's distance from the centre). Clamped to
/// 0..1; a NaN or negative extent, or a degenerate lane, gives 0.
float sprayValueForExtent(float extentPx, const LaneGeometry &geometry) noexcept;

/// What a press at x grabs.
enum class DragTarget
{
    None,
    Body,      ///< Sets Position (on the FX lane, the right edge too: it is Position).
    LeftEdge,  ///< Sets Spray (the FX window's older edge, or either instrument edge).
    RightEdge, ///< Sets Spray. Instrument lane only: the FX window's right edge is the Body.
};

/// Hit-tests x against the window on geometry's lane. The left edge grabs within
/// edgeTolerancePx outside the window and within min(edgeTolerancePx, width / 3) inside it, so a
/// narrow window keeps at least its middle third for the body.
///
/// - Instrument: the right edge grabs the same way, mirrored; everything else inside is the
///   body. A window of no width is the body only at its own x, with an edge either side.
/// - FX: the right edge is Position, so it and its tolerance outside the window are the body
///   (ruling PU-T8a). A window of no width (Spray 0) is the left edge just left of its x and the
///   body from its x rightwards.
///
/// A non-finite x grabs nothing.
DragTarget hitTest(float x, const Window &window, float edgeTolerancePx,
                   const LaneGeometry &geometry) noexcept;
} // namespace gg::ui

#endif // GG_UI_LANEMODEL_H
