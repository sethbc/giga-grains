// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_METALGRAINCANVAS_H
#define GG_UI_METALGRAINCANVAS_H

#include <cstdint>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace gg::ui
{
/// The GPU canvas spike's candidate 2 (M2 sub-project 6, Task 17; spike/gpu-canvas only; macOS
/// only): the stress workload drawn with Metal into a CAMetalLayer under an NSView the canvas
/// hosts. The same two passes as the OpenGL candidate: instanced quads with the splat falloff,
/// additive into an RGBA16Float texture, then a resolve 1 - exp(-a) tinted per lane band into
/// the layer's drawable. It renders on the message thread, from a VBlankAttachment, so its
/// encoding cost is UI-thread time by construction; the GPU time comes from each command
/// buffer's GPUStartTime and GPUEndTime.
class MetalGrainCanvas final : public juce::Component
{
  public:
    static constexpr int kLaneHeaderWidth = 120;

    explicit MetalGrainCanvas(uint32_t stressCount);
    ~MetalGrainCanvas() override;

    /// The message thread's time per frame (generate, encode, commit), mean of the last 60.
    double averageUiMs() const noexcept;
    /// The time of those frames spent waiting for a drawable, mean of the last 60 (inside
    /// averageUiMs).
    double averageWaitMs() const noexcept;
    /// The GPU time per command buffer, mean of the last 60 completed.
    double averageGpuMs() const noexcept;
    uint64_t frames() const noexcept;
    /// The VBlank callbacks that rendered nothing because kInFlight drawables were out.
    uint64_t skipped() const noexcept;
    float pixelScale() const noexcept;
    uint32_t fbWidth() const noexcept;
    uint32_t fbHeight() const noexcept;
    bool ready() const noexcept;

    void paint(juce::Graphics &) override {}
    void resized() override;

  private:
    void renderFrame();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    juce::NSViewComponent view_;
    juce::VBlankAttachment vblank_{this, [this] { renderFrame(); }};
};
} // namespace gg::ui

#endif // GG_UI_METALGRAINCANVAS_H
