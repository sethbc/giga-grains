// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_GRAINCANVAS_H
#define GG_UI_GRAINCANVAS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HeldValue.h"
#include "gg/core/ParamIds.h"
#include "gg/core/UiSnapshot.h"
#include "gg/ui/Editor.h"
#include "gg/ui/LaneModel.h"
#include "gg/ui/LaneState.h"
#include "gg/ui/SplatRenderer.h"

namespace gg::ui
{
/// The Perform view's lane (sub-project 5's spec §6.2–6.3): the grains the engine is playing,
/// over the live ring's peaks (FX) or the sample's outline (instrument), with the scan window
/// at Position and Spray's base values, a ghost window where the effective values put it, a tint
/// while frozen and a regeneration marker at the head while Feedback is up.
///
/// **Each frame** (renderFrame, which paint calls) it drains the context's grain queue into its
/// own list (LiveGrains: a grain goes once it has ended), takes the newest snapshot, runs the
/// published clock on by the wall time since it changed (LaneClock), and splats every playing
/// grain's capsule (grainCapsule) at physical-pixel resolution into an image. paint then draws
/// the image and, over it, the peaks, the windows, the tint and the marker. With the plugin
/// inactive (the context's publish pointers null) it draws only those static parts.
///
/// **Drags** set base values as host-visible gestures: the window's body sets Position and an
/// edge sets Spray (hitTest; on the FX lane the newer edge is Position itself, ruling R-T8a).
/// Each drag calls beginGesture once on the press, setValue for each move that changes the
/// value, and endGesture once: on the release (wherever it is) or, if the canvas goes first,
/// from its destructor. The window keeps the dragged value until the base value catches up
/// (HeldValue).
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class GrainCanvas final : public juce::Component
{
  public:
    /// How near an edge, in logical pixels, a press grabs it rather than the body.
    static constexpr float kEdgeTolerancePx = 6.0f;
    /// The FX lane's span: the live ring's 16 s, Position's range.
    static constexpr double kRingSeconds = 16.0;

    explicit GrainCanvas(const EditorContext &context);
    ~GrainCanvas() override;

    /// One frame's grains: drains the queue, extrapolates the clock and splats the grains that
    /// play now into the image, at the physical-pixel scale of the last paint (1 before it).
    void renderFrame();

    /// The splat accumulator at physical pixel (x, y) as of the last renderFrame; 0 outside it
    /// or before the first.
    float accumAt(uint32_t x, uint32_t y) const noexcept;
    /// The grains held at the last renderFrame: drained and not yet ended.
    std::size_t liveGrainCount() const noexcept { return grains_.size(); }
    /// The mean time of the last 60 frames (renderFrame, the image and the overlays), in ms; 0
    /// before the first painted frame.
    double averageFrameMs() const noexcept;

    void paint(juce::Graphics &g) override;
    void mouseMove(const juce::MouseEvent &e) override;
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;

  private:
    bool fxLane() const noexcept { return context_.mode == gg::core::Mode::Fx; }
    /// The lane at `scale` physical pixels per logical one. The instrument's lane needs a
    /// sample; usable says whether this one can place anything.
    LaneGeometry geometry(float scale) const;
    static bool usable(const LaneGeometry &g) noexcept;
    float baseValue(gg::core::ParamSlot slot) const;
    /// Position and Spray as the solid window shows them: the base values, or a drag's.
    float shownPosition(double now);
    float shownSpray(double now);

    void drawPeaks(juce::Graphics &g, const LaneGeometry &lane) const;
    void drawWindow(juce::Graphics &g, const Window &w, bool ghost) const;
    void endDrag();

    const EditorContext &context_;

    // The image and its renderer, both in physical pixels.
    std::unique_ptr<SplatRenderer> splat_;
    juce::Image image_;
    float scale_{1.0f};

    // The engine's view, as of the last frame.
    LaneClock clock_;
    LiveGrains grains_;
    const void *clockSource_{nullptr}; // the clock last read: a new one is a new activation
    gg::core::UiSnapshot snapshot_{};
    bool haveSnapshot_{false};
    int64_t now_{0};
    int64_t head_{0};

    // The drag under way, if any (logical pixels).
    DragTarget drag_{DragTarget::None};
    gg::core::ParamSlot dragSlot_{gg::core::ParamSlot::Position};
    float downX_{0.0f};
    float anchorX_{0.0f}; // the grabbed part's x at the press: Position's x, or the edge's
    float fixedX_{0.0f};  // what a Spray drag measures from: Position's x, or the centre
    bool sentAny_{false};
    float lastSent_{0.0f};
    HeldValue heldPosition_, heldSpray_;

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
