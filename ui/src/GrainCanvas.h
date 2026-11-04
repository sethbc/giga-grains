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
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HeldValue.h"
#include "Palette.h"
#include "gg/core/ParamIds.h"
#include "gg/core/ParamMap.h"
#include "gg/core/UiSnapshot.h"
#include "gg/ui/Editor.h"
#include "gg/ui/LaneModel.h"
#include "gg/ui/LaneState.h"
#include "gg/ui/LayerColors.h"
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
/// and before that (the plugin inactive) gg::core::builtKind of the layer's Source base value. A
/// Sample lane with no sample shows what to do and can't be grabbed. A lane whose layer is off is
/// drawn at 40% alpha.
///
/// **A Live lane spans its layer's ring** (ruling VW-P2d, amended): ringView's ringFrames /
/// sampleRate seconds, 16 s while there is none (inactive). Its peaks draw over the frames the
/// peak strip holds (the longest ring's) back to the oldest frame the ring's storage holds
/// (RingView::oldestHeld: after a Ring Length grow, no older than the old ring reached). Over the
/// peaks it draws, in order, the loop window while the layer's Loop is on (spliceSpan's window,
/// shaded), the splice the engine plays (playedSplice for Splice's base value, rounded, when it is
/// 1 or more and a mark is in storage: its span outlined), and each mark: a 1 px rule, numbered as
/// the engine numbers the splices, from the oldest still in storage, Loop ignored (ruling SP-T1a/b;
/// carry-over row 386), at its top when the lane is at least 40 px high.
///
/// **The lane menu** (ruling VW-P2a) opens on a right-click on any lane, which selects its layer
/// and begins no drag, or on Shift+F10 on the keyboard's lane (laneMenu). A Live lane's: "Add
/// splice mark here" (where clicked; from the keyboard, at the anchor), "Clear splice marks",
/// "Loop" (one gesture), "Capture to layer" A to D and "Save capture as…"; the ring's items are
/// disabled while the ring view isn't valid (inactive). A Sample lane's: "Load sample…".
///
/// **Each frame** (renderFrame, which paint calls) it drains the context's grain queue into one
/// list per layer (LiveGrains: a grain goes once it has ended), first clearing a queue it hasn't
/// read before (its backlog is stale), takes the newest snapshot, runs the published clock on by
/// the wall time since it changed (LaneClock), and splats every playing grain's capsule
/// (grainCapsule) into its lane's rows of one accumulator image, at physical-pixel resolution; a
/// Live lane's only once a snapshot gives it its write head. The image covers the lanes alone:
/// its x runs from 0 at the lanes' left edge, its y is the canvas's. The resolve tints each
/// lane's rows with its layer's color. paint then draws the image and, over it, per lane, the
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
/// it (onSelectLayer). A file dropped on any lane goes to the context's loadSampleFor for its
/// layer (ruling VW-P2c); a file that isn't a sample is refused with a notice (postNotice), and
/// one dropped on a Live layer is loaded with a notice that its Source must be Sample to play it.
/// With the context's undo stack, each gesture is an entry, labeled with its parameter's name.
///
/// **The keyboard** (spec §6.9): the canvas takes focus, and its focused lane is the selected
/// one, its rectangle outlined: Left and Right move its Position, Up and Down its Pitch, by 1/100
/// of the range, Shift its Spray and Pitch Spread instead, each press a gesture; Shift+F10 opens
/// its lane menu. Escape ends a drag under way, as its release would.
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
    /// A Live lane's span while its layer has no ring (the plugin inactive), in seconds.
    static constexpr double kFallbackRingSeconds = 16.0;
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

    /// The layer whose lane is outlined in its color; 0 at first. Out of range is ignored.
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

    /// Layer `layer`'s lane geometry at one physical pixel per logical one, as a press is placed
    /// (its x from the lane's left edge). For the tests.
    LaneGeometry laneGeometry(uint8_t layer) const { return geometry(layer, 1.0f); }
    /// The x of each splice mark the last paint drew on layer `layer`'s lane, logical pixels from
    /// the lane's left edge, oldest first; empty for a lane out of range. For the tests.
    std::vector<float> marksDrawn(uint8_t layer) const;
    /// The head (ring frames) the last paint drew layer `layer`'s marks against; 0 before. For
    /// the tests.
    int64_t headDrawn(uint8_t layer) const noexcept;
    /// The splice the last paint outlined on layer `layer`'s lane, in ring frames; none when it
    /// outlined none, or for a lane out of range. For the tests.
    std::optional<RingSpan> spliceOutlined(uint8_t layer) const;
    /// The ring frame the last right-click on layer `layer`'s lane landed on, which its menu's
    /// "Add splice mark here" marks (AddSpliceAt); none from the keyboard or another lane. For
    /// the tests.
    std::optional<int64_t> menuFrame(uint8_t layer) const noexcept;
    /// The bounds of the peak bars the last paint built for layer `layer`'s lane, x in columns
    /// from the lane's left edge; empty when it drew none, or for a lane out of range. For the
    /// tests.
    juce::Rectangle<float> peaksDrawn(uint8_t layer) const;

    /// Layer `layer`'s lane menu (the class comment), for the place last right-clicked on it, or
    /// for the keyboard (no place) when the menu was opened by Shift+F10 or not at all. Its
    /// items' actions do what the menu says. The tests take it here and invoke an item by its
    /// text; nothing shows it but showMenu.
    juce::PopupMenu laneMenu(uint8_t layer);
    /// Shows a lane menu. By default juce::PopupMenu::showMenuAsync at the mouse; tests replace
    /// it (a menu shown is modal to the test).
    std::function<void(juce::PopupMenu)> showMenu;

    /// What a lane menu's chooser asks for: a sample to load, or a WAV to save a capture as.
    struct Browse
    {
        juce::String title;
        juce::File initial;    ///< the folder it opens in; none to let the system choose
        bool save{false};      ///< a file to write, or one to read
        juce::String patterns; ///< the file patterns
    };
    /// Opens a chooser and calls the callback with what was chosen (juce::File() if nothing),
    /// perhaps later. By default an asynchronous juce::FileChooser, which the canvas owns, so a
    /// dialog still open when the canvas goes is dismissed and its callback never runs. Tests
    /// replace it.
    std::function<void(const Browse &, std::function<void(const juce::File &)>)> browse;

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
    /// The layer's sample outline and ring view, each fetched once per frame (renderFrame, or a
    /// press, starts a new one: forgetFrame) and kept until the next (carry-over row 327).
    SampleWaveform waveformOf(uint8_t layer) const;
    RingView ringOf(uint8_t layer) const;
    void forgetFrame() noexcept;
    /// The seconds a Live lane spans: the ring's frames over the sample rate, or
    /// kFallbackRingSeconds.
    double ringSecondsOf(uint8_t layer) const;
    /// The layer's ring parameters' base values (gg::core::toRingParams over them).
    gg::core::RingParams ringParamsOf(uint8_t layer) const;
    /// The Loop Length's frames at the lane's rate, as MediaSession::windowOf rounds them.
    int64_t loopFramesOf(const gg::core::RingParams &ring, double sampleRate) const noexcept;
    /// The head the lane draws at: the snapshot's, run on to now, or the ring view's without one.
    int64_t laneHead(uint8_t layer, const RingView &ring) const noexcept;
    void drawRing(juce::Graphics &g, uint8_t layer, const LaneGeometry &lane, const Palette &pal);
    /// Builds layer `layer`'s menu for `frame` (nullopt: the keyboard's) and shows it.
    void openLaneMenu(uint8_t layer, std::optional<int64_t> frame);
    void chooseSampleFor(uint8_t layer);
    void chooseCaptureFileFor(uint8_t layer, uint32_t splice);
    float baseValue(uint8_t layer, Held which) const;
    /// A value as the rectangle shows it: the base value, or a drag's.
    float shown(uint8_t layer, Held which, double now);
    ScanRectValues shownValues(uint8_t layer, double now);
    gg::core::ParamSlot slotOf(uint8_t layer, Held which) const noexcept;

    void drawLane(juce::Graphics &g, uint8_t layer, const Palette &pal);
    void drawPeaks(juce::Graphics &g, uint8_t layer, const LaneGeometry &lane, const Palette &pal);
    void drawRect(juce::Graphics &g, const WindowRect &r, bool ghost, bool oneSided,
                  juce::Colour color) const;
    void beginDragGesture(Held which);
    void endDrag();
    /// One whole gesture on layer `layer`'s `which`, from `before` (what the lane showed) to
    /// `value`.
    void gestureTo(uint8_t layer, Held which, float before, float value, double now,
                   bool nudge = false);
    /// The undo stack's side of a gesture, when the context has one.
    void recordBegin(gg::core::ParamSlot slot, float before, bool nudge = false);
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
        double span{0.0}; ///< a Live strip's seconds
        bool valid{false};
    };
    std::array<PeakStrip, kLaneCount> strips_;

    uint8_t selected_{0};
    int dropLane_{-1}; // the lane a file is being dragged over, or -1

    // This frame's sample outlines and ring views (waveformOf, ringOf), fetched on first use.
    mutable std::array<std::optional<SampleWaveform>, kLaneCount> waves_;
    mutable std::array<std::optional<RingView>, kLaneCount> rings_;
    // The marks the last paint drew, per lane (marksDrawn).
    std::array<std::vector<float>, kLaneCount> marks_;
    std::array<int64_t, kLaneCount> headDrawn_{};
    std::array<std::optional<RingSpan>, kLaneCount> outlined_{};
    // The lane menu's place: the ring frame right-clicked on its layer's lane, or none (the
    // keyboard's, or a Sample lane's).
    uint8_t menuLayer_{0};
    std::optional<int64_t> menuFrame_;
    juce::Rectangle<int> menuArea_; // where the menu shows, in screen coordinates
    std::unique_ptr<juce::FileChooser> chooser_;

    // The drag under way, if any (lane coordinates: x from the lanes' left edge, y the canvas's).
    DragTarget drag_{DragTarget::None};
    uint8_t dragLayer_{0};
    DragAxis axis_{DragAxis::Undecided}; // a Body drag's, once decided
    Held dragHeld_{kHeldPosition};
    bool began_{false}; // the gesture has begun (a Body drag's waits for its axis)
    float downX_{0.0f}, downY_{0.0f};
    float anchorX_{0.0f}; // the grabbed part's x at the press: Position's x, or the edge's
    float anchorY_{0.0f}; // and its y: the rectangle's center, or the edge's
    float fixedX_{0.0f};  // what a Spray drag measures from: Position's x, or the center
    float fixedY_{0.0f};  // what a Pitch Spread drag measures from: the rectangle's center
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
