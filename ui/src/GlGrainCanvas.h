// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_GLGRAINCANVAS_H
#define GG_UI_GLGRAINCANVAS_H

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include "gg/ui/StressGrains.h"

namespace gg::ui
{
/// The GPU canvas spike's candidate 1 (M2 sub-project 6, Task 17; spike/gpu-canvas only): the
/// stress workload drawn through JUCE's OpenGLContext. Each frame, on JUCE's GL thread:
/// - pass 1: every grain as an instanced quad, the splat's falloff 1 - d^2/r^2 in the fragment
///   shader, blended additively into an RGBA16F framebuffer the size of the lanes;
/// - pass 2: a resolve over the lanes, v = 1 - exp(-a), tinted per lane band, into the window.
/// It draws the stress workload only (no queue, no overlays): the question is the render cost.
///
/// It keeps three readouts, each the mean of the last 60 frames: the GL thread's wall time in
/// renderOpenGL (building and uploading the instances and issuing both passes; not the swap),
/// the GPU time of both passes from GL_TIME_ELAPSED queries, and the frames rendered.
class GlGrainCanvas final : public juce::Component, private juce::OpenGLRenderer
{
  public:
    static constexpr int kLaneHeaderWidth = 120;

    explicit GlGrainCanvas(uint32_t stressCount);
    ~GlGrainCanvas() override;

    double averageGlThreadMs() const noexcept { return glThreadMs_.load(); }
    double averageGpuMs() const noexcept { return gpuMs_.load(); }
    uint64_t frames() const noexcept { return frames_.load(); }
    float pixelScale() const noexcept { return scale_.load(); }
    bool gpuTimerWorks() const noexcept { return timerOk_.load(); }
    int glMajor() const noexcept { return glMajor_.load(); }
    int glMinor() const noexcept { return glMinor_.load(); }
    uint32_t fbWidth() const noexcept { return fbW_.load(); }
    uint32_t fbHeight() const noexcept { return fbH_.load(); }

    void paint(juce::Graphics &) override {}

  private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void ensureFramebuffer(uint32_t w, uint32_t h);
    void record(double glMs, double gpuMs);

    juce::OpenGLContext context_;
    const uint32_t stressCount_;
    std::vector<Splat> stress_;
    uint64_t frame_{0};

    // GL objects (GL thread only).
    unsigned vao_{0}, quadVbo_{0}, instanceVbo_{0}, fbo_{0}, accumTex_{0};
    uint32_t fboW_{0}, fboH_{0};
    std::unique_ptr<juce::OpenGLShaderProgram> splat_, resolve_;
    int uFbSize_{-1}, uAccum_{-1}, uTints_{-1}, uLaneEnds_{-1}, uFbHeight_{-1}, uXRange_{-1};
    static constexpr int kQueries = 4;
    std::array<unsigned, kQueries> queries_{};
    std::array<bool, kQueries> queryIssued_{};
    int queryHead_{0};

    // Readouts (written on the GL thread, read on the message thread).
    static constexpr std::size_t kAveraged = 60;
    std::array<double, kAveraged> glMsRing_{}, gpuMsRing_{};
    std::size_t ringNext_{0}, ringFilled_{0}, gpuFilled_{0};
    std::atomic<double> glThreadMs_{0.0}, gpuMs_{0.0};
    std::atomic<uint64_t> frames_{0};
    std::atomic<float> scale_{1.0f};
    std::atomic<bool> timerOk_{false};
    std::atomic<int> glMajor_{0}, glMinor_{0};
    int glMajorTmp_{0}, glMinorTmp_{0};
    std::atomic<uint32_t> fbW_{0}, fbH_{0};
};
} // namespace gg::ui

#endif // GG_UI_GLGRAINCANVAS_H
