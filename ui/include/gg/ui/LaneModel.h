// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_LANEMODEL_H
#define GG_UI_LANEMODEL_H

#include <cstdint>
#include <optional>

namespace gg::ui
{
/// The canvas's lane geometry (spec §6.2–6.3; M2 Views' spec §6.3): where each playing grain's
/// capsule lies, where the scan rectangle lies, and the inverse mappings a drag needs. Pure
/// functions, framework-free, for the UI thread only: they may use libm (std::log2, std::exp2),
/// since nothing here reaches the audio or the grain-event log.
///
/// Coordinates are in physical pixels, as SplatRenderer's: x runs 0 .. width left to right and
/// y top .. top + height top to bottom. The canvas stacks its four lanes (one per layer) in one
/// accumulator, each at its own top; M1's one lane is the lane at top 0.
///
/// - **The vertical axis is pitch** (D1): a grain at rate r lies 12·log2(r) semitones from the
///   lane's centre, ±kLaneSemitones across its height less the capsule's radius, higher pitches
///   higher up (pitchY).
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
    float top{0.0f};      ///< The lane's top in pixels: where it sits in the stacked canvas.
};

/// The semitones either side of a lane's centre that its height spans (D1): Pitch's ±24 plus
/// Pitch Spread's ±24.
inline constexpr float kLaneSemitones = 48.0f;

/// The y of a grain at rate `rate` (frames read per output sample) on `g`'s lane, for a capsule
/// of radius `radius`: top + height/2 − 12·log2(rate) × (height/2 − radius) / kLaneSemitones,
/// clamped to [top + radius, top + height − radius], so the capsule stays on its lane. A rate
/// that isn't positive and finite (a reverse grain, 0, NaN or infinity) draws centred, as does
/// any rate on a lane shorter than two radii. A top or height that isn't finite gives 0.
float pitchY(float rate, const LaneGeometry &g, float radius) noexcept;

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
    float pan;             ///< Its pan, −1 (left) .. 1 (right); no longer drawn (D1).
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
/// - y = pitchY(rate, geometry, radius): its pitch, clamped to [top + radius, top + height −
///   radius], so the capsule stays on its own lane; a reverse grain draws centred. Pan is no
///   longer drawn (D1).
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
/// kGhostThreshold from its base value (both of the same block); nullopt when neither difference
/// exceeds the threshold (a NaN difference never does).
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

/// What a press grabs: at x for hitTest, at (x, y) for hitTestRect.
enum class DragTarget
{
    None,
    Body,       ///< Sets Position (on the FX lane, the right edge too: it is Position), or Pitch
                ///< on the rectangle when the drag is vertical (decideAxis).
    LeftEdge,   ///< Sets Spray (the FX window's older edge, or either instrument edge).
    RightEdge,  ///< Sets Spray. Instrument lane only: the FX window's right edge is the Body.
    TopEdge,    ///< The rectangle's higher-pitch edge: sets Pitch Spread.
    BottomEdge, ///< The rectangle's lower-pitch edge: sets Pitch Spread.
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

/// One layer's four scan values as CLAP values, as a UiSnapshot gives them (base or effective):
/// Position and Spray 0..1 (scanWindow's), Pitch in semitones (−24..24) and Pitch Spread in
/// semitones (0..24).
struct ScanRectValues
{
    float position, spray, pitch, pitchSpread;
};

/// The scan rectangle, in pixels: x0 <= x1, y0 <= y1, so y0 is the higher pitch.
struct WindowRect
{
    float x0, x1, y0, y1;
};

/// The scan rectangle (D2): where a layer's grains read and at what pitch. x0 .. x1 is
/// scanWindow(position, spray): one-sided on a Live (FX) lane, ±Spray on a Sample lane. y0 .. y1
/// is Pitch ± Pitch Spread semitones on the lane's pitch axis, placed as pitchY places a grain
/// at that pitch (with the capsules' radius, max(1.5, 3 × pixelScale)), so a grain at Pitch's
/// rate lies on the rectangle's middle. Pitch is clamped to −24..24 and the spread to 0..24 first
/// (NaN to 0), so Pitch 0 with no spread is a band of no height at the lane's centre and the
/// rectangle never leaves the lane. A lane that can't place anything gives all zeros, as
/// scanWindow does.
WindowRect scanRect(const ScanRectValues &v, const LaneGeometry &g) noexcept;

/// The ghost rectangle (spec §6.3): the scan rectangle at the effective values, when any of the
/// four differs from its base value by more than kGhostThreshold in its own 0..1 domain (Position
/// and Spray as they are, Pitch over its 48 semitones, Pitch Spread over its 24); nullopt when
/// none does, and whenever any of the eight values isn't finite (Review Focus 3: a bad host
/// event's NaN shows no ghost).
std::optional<WindowRect> ghostRect(const ScanRectValues &base, const ScanRectValues &effective,
                                    const LaneGeometry &g) noexcept;

/// What a press at (x, y) grabs on `g`'s lane, for the rectangle w (D2): the left and right edges
/// set Spray, the top and bottom edges Pitch Spread, the body Position or Pitch (decideAxis).
///
/// - x by M1's rules (hitTest, both lanes' kinds included): the FX's right side is the body.
/// - y by the same tolerance rule: the top edge grabs within edgeTolerancePx above w.y0 and
///   within min(edgeTolerancePx, height / 3) below it, the bottom edge mirrored, the rest between
///   them is the body. A band of no height is the body only at its own y, an edge either side.
/// - A corner, within the tolerance of a left or right edge and of the top or bottom edge, takes
///   the horizontal edge: the one whose drag is horizontal (LeftEdge or RightEdge), as M1's
///   window did. On the FX lane the right side is the body, so there the top and bottom edges
///   run to it.
///
/// A press outside the lane (x outside 0 .. width, y outside top .. top + height, both
/// inclusive), a non-finite coordinate or a lane that can't place anything grabs nothing.
DragTarget hitTestRect(float x, float y, const WindowRect &w, float edgeTolerancePx,
                       const LaneGeometry &g) noexcept;

/// The Pitch value, in semitones, whose rectangle is centred at y: scanRect's inverse for a
/// vertical body drag. Clamped to −24..24, so a drag past the lane's edges pins Pitch to its
/// range. A NaN y, or a lane that can't place a pitch (one shorter than two radii included),
/// gives 0.
float pitchValueAtY(float y, const LaneGeometry &g) noexcept;

/// The Pitch Spread, in semitones, whose rectangle reaches extentPx from its centre: scanRect's
/// inverse for a top or bottom edge drag (the dragged edge's distance from the centre; the
/// spread is symmetric). Clamped to 0..24; a NaN or negative extent, or a lane that can't place
/// a pitch, gives 0.
float pitchSpreadForExtent(float extentPx, const LaneGeometry &g) noexcept;

/// Which parameter a body drag moves: Position (Horizontal) or Pitch (Vertical).
enum class DragAxis
{
    Undecided, ///< Not yet moved far enough to tell.
    Horizontal,
    Vertical,
};

/// The axis lock (D2): a body drag that has moved (dx, dy) pixels from its press is Undecided
/// until max(|dx|, |dy|) reaches thresholdPx; then the larger movement wins, a tie going
/// Horizontal. The caller decides once and keeps the answer until release. A NaN movement counts
/// as no movement; a NaN threshold never decides.
DragAxis decideAxis(float dx, float dy, float thresholdPx) noexcept;
} // namespace gg::ui

#endif // GG_UI_LANEMODEL_H
