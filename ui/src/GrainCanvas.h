// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_GRAINCANVAS_H
#define GG_UI_GRAINCANVAS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HeldValue.h"
#include "Palette.h"
#include "gg/core/ParamIds.h"
#include "gg/core/UiSnapshot.h"
#include "gg/ui/Editor.h"
#include "gg/ui/LaneModel.h"
#include "gg/ui/LaneState.h"
#include "gg/ui/LayerColours.h"
#include "gg/ui/SplatRenderer.h"
#include "gg/ui/StressGrains.h"

namespace gg::ui
{
/// The Perform view's canvas (M2 sub-project 6's spec §6.3, after sub-project 5's §6.2–6.3): four
/// lanes stacked, one per layer A to D, to the right of a header column of kLaneHeaderWidth
/// logical pixels that the canvas leaves empty (the layer strip's, Task 10). Each lane shows
/// the grains its layer is playing, over the layer's live ring's peaks (a Live layer) or its
/// sample's outline (a Sample layer), with the scan rectangle at the layer's Position, Spray,
/// Pitch and Pitch Spread base values, a dashed ghost where the effective values put it, a tint
/// while frozen and a regeneration marker at the head while the layer's Feedback is up.
///
/// **A lane's kind** is the one its layer renders as: the snapshot's builtKind once there is one,
/// and before that (the plugin inactive) the layer's Source base value, a layer whose Source
/// isn't built drawing as its product's default. A Sample lane with no sample shows what to do and
/// can't be grabbed. A lane whose layer is off is drawn at 40% alpha.
///
/// **Each frame** (renderFrame, which paint calls) it drains the context's grain queue into one
/// list per layer (LiveGrains: a grain goes once it has ended), first clearing a queue it hasn't
/// read before (its backlog is stale), takes the newest snapshot, runs the published clock on by
/// the wall time since it changed (LaneClock), and splats every playing grain's capsule
/// (grainCapsule) into its lane's rows of one accumulator image, at physical-pixel resolution; a
/// Live lane's only once a snapshot gives it its write head. The image covers the lanes alone:
/// its x runs from 0 at the lanes' left edge, its y is the canvas's. The resolve tints each
/// lane's rows with its layer's colour. paint then draws the image and, over it, per lane, the
/// peaks, the rectangles, the tint and the marker. With the plugin inactive (the context's
/// publish pointers null) it draws only those static parts.
///
/// **Drags** set base values as host-visible gestures, on the lane the press is in (a press on
/// the row two lanes share goes to the lower lane, whose range [top, bottom) holds it):
/// the rectangle's body sets Position (horizontally) or Pitch (vertically: the first
/// kAxisLockPx of movement pick the axis and it stays until the release), a left or right edge
/// sets Spray (on a Live lane the newer edge is Position itself, ruling PU-T8a), and a top or
/// bottom edge sets Pitch Spread (hitTestRect, with rulings VW-T2a and VW-T2b). Each drag calls
/// beginGesture once, setValue for each move that changes the value, and endGesture once: on
/// the release (wherever it is) or, if the canvas goes first, from its destructor. A body drag
/// begins its gesture when its axis is decided, so a press that never moves is no gesture. The
/// rectangle keeps the dragged value until the base value catches up (HeldValue). A double-click
/// on a rectangle's body toggles that layer's Freeze as one gesture. A press in a lane selects
/// it (onSelectLayer). A file dropped on a lane goes to the context's loadSampleFor; a refusal
/// is posted as a notice (postNotice). With the context's undo stack, each gesture is an entry,
/// labelled with its parameter's name.
///
/// **The keyboard** (spec §6.9): the canvas takes focus, and its focused lane is the selected
/// one, its rectangle outlined: Left and Right move its Position, Up and Down its Pitch, by 1/100
/// of the range, Shift its Spray and Pitch Spread instead, each press a gesture. Escape ends a
/// drag under way, as its release would.
///
/// With GG_STRESS_GRAINS=N (N > 0) in the environment when the canvas is made, it draws
/// stressGrains(frame, N, ...) instead of the queue, four lanes (stressMode).
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class GrainCanvas final : public juce::Component, public juce::FileDragAndDropTarget
{
  public:
    /// How near an edge, in logical pixels, a press grabs it rather than the body.
    static constexpr float kEdgeTolerancePx = 6.0f;
    /// How far, in logical pixels, a body drag moves before its axis is decided.
    static constexpr float kAxisLockPx = 4.0f;
    /// The FX lane's span: the live ring's 16 s, Position's range.
    static constexpr double kRingSeconds = 16.0;
    /// The canvas's left column, in logical pixels, reserved for the layer strip: the canvas
    /// draws nothing in it (but its ground).
    static constexpr int kLaneHeaderWidth = 120;

    explicit GrainCanvas(const EditorContext &context);
    ~GrainCanvas() override;

    /// One frame's grains: drains the queue, extrapolates the clock and splats the grains that
    /// play now into the image, at the physical-pixel scale of the last paint (1 before it).
    void renderFrame();

    /// The splat accumulator at physical pixel (x, y) as of the last renderFrame (x from the
    /// lanes' left edge, not the canvas's); 0 outside it or before the first.
    float accumAt(uint32_t x, uint32_t y) const noexcept;
    /// The grains held at the last renderFrame, over all four layers: drained and not yet ended.
    std::size_t liveGrainCount() const noexcept;
    /// The mean time of the last 60 frames (renderFrame, the image and the overlays), in ms; 0
    /// before the first painted frame.
    double averageFrameMs() const noexcept;

    /// The layer whose lane is outlined in its colour; 0 at first. Out of range is ignored.
    void setSelectedLayer(uint8_t layer);
    uint8_t selectedLayer() const noexcept { return selected_; }
    /// Called with the layer when a press lands in its lane (the canvas has selected it).
    std::function<void(uint8_t)> onSelectLayer;
    /// Layer `layer`'s lane, in the canvas's logical pixels: the four tile the canvas to the
    /// right of the header column, top to bottom. Layer 4 or more gives an empty rectangle.
    juce::Rectangle<int> laneBounds(uint8_t layer) const;
    /// Whether GG_STRESS_GRAINS asked for the synthetic workload.
    bool stressMode() const noexcept { return stressCount_ > 0; }
    /// The snapshot the last frame took, or null before one (the plugin inactive).
    const gg::core::UiSnapshot *latestSnapshot() const noexcept
    {
        return haveSnapshot_ ? &snapshot_ : nullptr;
    }
    /// Ends a drag under way, as its release would; false if there is none.
    bool endDragIfAny();

    void paint(juce::Graphics &g) override;
    void mouseMove(const juce::MouseEvent &e) override;
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;
    void mouseDoubleClick(const juce::MouseEvent &e) override;
    bool keyPressed(const juce::KeyPress &key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

    // juce::FileDragAndDropTarget: a drop on a lane loads into its layer.
    bool isInterestedInFileDrag(const juce::StringArray &files) override;
    void fileDragEnter(const juce::StringArray &files, int x, int y) override;
    void fileDragMove(const juce::StringArray &files, int x, int y) override;
    void fileDragExit(const juce::StringArray &files) override;
    void filesDropped(const juce::StringArray &files, int x, int y) override;

  private:
    static constexpr std::size_t kLaneCount = gg::core::kLayers;
    /// The parameters a lane's gestures move, in HeldValue order.
    enum Held : std::size_t
    {
        kHeldPosition,
        kHeldSpray,
        kHeldPitch,
        kHeldPitchSpread,
        kHeldFreeze,
        kHeldCount
    };

    /// The lane under the canvas point (x, y), or -1: the half-open range [top, bottom) of the
    /// lane holding y, so a row two lanes share belongs to the lower one.
    int laneAt(float x, float y) const noexcept;
    /// Lane `layer`'s rows in the image at `scale` physical pixels per logical one.
    void laneRows(uint8_t layer, float scale, uint32_t &begin, uint32_t &end) const noexcept;
    /// Whether the layer's lane shows a Live ring (else a Sample).
    bool liveLane(uint8_t layer) const;
    bool laneOn(uint8_t layer) const;
    bool laneFrozen(uint8_t layer, double now);
    /// Layer `layer`'s lane at `scale` physical pixels per logical one. A Sample lane needs a
    /// sample; usable says whether this one can place anything.
    LaneGeometry geometry(uint8_t layer, float scale) const;
    static bool usable(const LaneGeometry &g) noexcept;
    SampleWaveform waveformOf(uint8_t layer) const;
    float baseValue(uint8_t layer, Held which) const;
    /// A value as the rectangle shows it: the base value, or a drag's.
    float shown(uint8_t layer, Held which, double now);
    ScanRectValues shownValues(uint8_t layer, double now);
    gg::core::ParamSlot slotOf(uint8_t layer, Held which) const noexcept;

    void drawLane(juce::Graphics &g, uint8_t layer, const Palette &pal);
    void drawPeaks(juce::Graphics &g, uint8_t layer, const LaneGeometry &lane, const Palette &pal);
    void drawRect(juce::Graphics &g, const WindowRect &r, bool ghost, bool oneSided,
                  juce::Colour colour) const;
    void beginDragGesture(Held which);
    void endDrag();
    /// One whole gesture on layer `layer`'s `which`, from `before` (what the lane showed) to
    /// `value`.
    void gestureTo(uint8_t layer, Held which, float before, float value, double now);
    /// The undo stack's side of a gesture, when the context has one.
    void recordBegin(gg::core::ParamSlot slot, float before);
    void recordEnd(gg::core::ParamSlot slot, float after);

    const EditorContext &context_;

    // The image and its renderer, both in physical pixels.
    std::unique_ptr<SplatRenderer> splat_;
    juce::Image image_;
    float scale_{1.0f};

    // The engine's view, as of the last frame.
    LaneClock clock_;
    std::array<LiveGrains, kLaneCount> grains_;
    const void *clockSource_{nullptr};  // the clock last read: a new one is a new activation
    const void *grainsSource_{nullptr}; // the queue last drained: a new one is cleared first
    gg::core::UiSnapshot snapshot_{};
    bool haveSnapshot_{false};
    int64_t now_{0};
    std::array<int64_t, kLaneCount> head_{}; // each Live lane's write head, run on to now_
    std::array<int64_t, kLaneCount>
        written_{}; // the frames each ring has written: the head at the published clock

    // The stress seam (GG_STRESS_GRAINS): the workload's size, its scratch and its frame.
    uint32_t stressCount_{0};
    std::vector<Splat> stress_;
    uint64_t stressFrame_{0};

    // The peak strip of each lane, kept so a frame reuses its storage and a lane rebuilds it
    // only when its head moved by a column, the ring or the sample changed, or its size did.
    struct PeakStrip
    {
        juce::RectangleList<float> bars;
        const void *source{nullptr};
        /// A Sample strip's outline, held so `source` can't be a freed address reused.
        std::shared_ptr<const std::vector<float>> outline;
        int64_t key{0};
        int top{0}, width{0}, height{0};
        double rate{0.0};
        bool valid{false};
    };
    std::array<PeakStrip, kLaneCount> strips_;

    uint8_t selected_{0};
    int dropLane_{-1}; // the lane a file is being dragged over, or -1

    // The drag under way, if any (lane coordinates: x from the lanes' left edge, y the canvas's).
    DragTarget drag_{DragTarget::None};
    uint8_t dragLayer_{0};
    DragAxis axis_{DragAxis::Undecided}; // a Body drag's, once decided
    Held dragHeld_{kHeldPosition};
    bool began_{false}; // the gesture has begun (a Body drag's waits for its axis)
    float downX_{0.0f}, downY_{0.0f};
    float anchorX_{0.0f}; // the grabbed part's x at the press: Position's x, or the edge's
    float anchorY_{0.0f}; // and its y: the rectangle's centre, or the edge's
    float fixedX_{0.0f};  // what a Spray drag measures from: Position's x, or the centre
    float fixedY_{0.0f};  // what a Pitch Spread drag measures from: the rectangle's centre
    bool sentAny_{false};
    float lastSent_{0.0f};
    float dragBefore_{0.0f}; // the dragged value as the lane showed it when the gesture began
    std::array<std::array<HeldValue, kHeldCount>, kLaneCount> held_;

    // The frame time, as the M0 stress panel averaged it.
    static constexpr std::size_t kAveragedFrames = 60;
    std::array<double, kAveragedFrames> frameMs_{};
    std::size_t nextFrame_{0};
    std::size_t filledFrames_{0};

    // Repaints on every display refresh while the canvas is on screen.
    juce::VBlankAttachment vblank_{this, [this] { repaint(); }};
};
} // namespace gg::ui

#endif // GG_UI_GRAINCANVAS_H
