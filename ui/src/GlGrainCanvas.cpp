// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "GlGrainCanvas.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "gg/ui/LayerColours.h"

namespace gg::ui
{
using namespace ::juce::gl;

namespace
{
// Pass 1: an instanced quad per grain. corner is the quad's unit corner, grain is (x, y, r, i)
// in the framebuffer's pixels, with pixel (x, y)'s centre at (x, y) as the CPU renderer has it
// (so GL's pixel centre is at + 0.5). The quad covers the box of pixels whose centres can lie
// within r; local is (dx / r, dy / r), linear across the quad, so each fragment's falloff is
// exact.
constexpr const char *kSplatVertex = R"(#version 150
in vec2 corner;
in vec4 grain;
uniform vec2 fbSize;
out vec2 local;
out float intensity;
void main()
{
    float r = grain.z;
    float hr = r + 0.5;
    vec2 p = grain.xy + corner * hr;
    local = corner * (hr / r);
    intensity = grain.w;
    vec2 ndc = (p + 0.5) / fbSize * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

constexpr const char *kSplatFragment = R"(#version 150
in vec2 local;
in float intensity;
out vec4 frag;
void main()
{
    float w = max(0.0, 1.0 - dot(local, local));
    frag = vec4(intensity * w, 0.0, 0.0, 0.0);
}
)";

// Pass 2: the lanes' rectangle of the window, sampling the accumulator; v = 1 - exp(-a),
// clamped as the CPU resolve clamps, tinted by the lane band the row falls in.
constexpr const char *kResolveVertex = R"(#version 150
in vec2 corner;
uniform vec2 xRange;
out vec2 uv;
void main()
{
    float x = mix(xRange.x, xRange.y, corner.x * 0.5 + 0.5);
    gl_Position = vec4(x, corner.y, 0.0, 1.0);
    uv = vec2(corner.x * 0.5 + 0.5, corner.y * 0.5 + 0.5);
}
)";

constexpr const char *kResolveFragment = R"(#version 150
uniform sampler2D accum;
uniform vec3 tints[4];
uniform float laneEnds[4];
uniform float fbHeight;
in vec2 uv;
out vec4 frag;
void main()
{
    float a = clamp(texture(accum, uv).r, 0.0, 30.0);
    float v = 1.0 - exp(-a);
    float row = uv.y * fbHeight;
    vec3 tint = tints[3];
    if (row < laneEnds[0]) tint = tints[0];
    else if (row < laneEnds[1]) tint = tints[1];
    else if (row < laneEnds[2]) tint = tints[2];
    frag = vec4(tint * v, 1.0);
}
)";

constexpr float kQuad[8] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
} // namespace

GlGrainCanvas::GlGrainCanvas(uint32_t stressCount) : stressCount_(stressCount)
{
    setOpaque(true);
    setComponentID("canvas-gl");
    stress_.resize(stressCount_);
    context_.setRenderer(this);
    context_.setPreferredVersion(juce::OpenGLVersion{3, 2});
    context_.setComponentPaintingEnabled(false);
    context_.setContinuousRepainting(true);
    context_.attachTo(*this);
}

GlGrainCanvas::~GlGrainCanvas() { context_.detach(); }

void GlGrainCanvas::newOpenGLContextCreated()
{
    glGetIntegerv(GL_MAJOR_VERSION, &glMajorTmp_);
    glGetIntegerv(GL_MINOR_VERSION, &glMinorTmp_);
    glMajor_.store(glMajorTmp_);
    glMinor_.store(glMinorTmp_);

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glGenBuffers(1, &instanceVbo_);

    splat_ = std::make_unique<juce::OpenGLShaderProgram>(context_);
    if (!splat_->addVertexShader(kSplatVertex) || !splat_->addFragmentShader(kSplatFragment) ||
        !splat_->link())
    {
        std::fprintf(stderr, "gg-gl splat shader: %s\n", splat_->getLastError().toRawUTF8());
    }
    resolve_ = std::make_unique<juce::OpenGLShaderProgram>(context_);
    if (!resolve_->addVertexShader(kResolveVertex) ||
        !resolve_->addFragmentShader(kResolveFragment) || !resolve_->link())
    {
        std::fprintf(stderr, "gg-gl resolve shader: %s\n", resolve_->getLastError().toRawUTF8());
    }
    uFbSize_ = glGetUniformLocation(splat_->getProgramID(), "fbSize");
    uAccum_ = glGetUniformLocation(resolve_->getProgramID(), "accum");
    uTints_ = glGetUniformLocation(resolve_->getProgramID(), "tints");
    uLaneEnds_ = glGetUniformLocation(resolve_->getProgramID(), "laneEnds");
    uFbHeight_ = glGetUniformLocation(resolve_->getProgramID(), "fbHeight");
    uXRange_ = glGetUniformLocation(resolve_->getProgramID(), "xRange");

    glGenQueries(kQueries, queries_.data());
    queryIssued_.fill(false);
    queryHead_ = 0;
    // Whether this context has the timer query: GL 3.3's, so a 3.2 core request may lack it
    // in name while Apple's "3.2+" context (4.1) has it. Issue one empty query and check.
    while (glGetError() != GL_NO_ERROR)
    {
    }
    glBeginQuery(GL_TIME_ELAPSED, queries_[0]);
    glEndQuery(GL_TIME_ELAPSED);
    timerOk_.store(glGetError() == GL_NO_ERROR && glGetQueryObjectui64v != nullptr);
    ringNext_ = ringFilled_ = gpuFilled_ = 0;
}

void GlGrainCanvas::ensureFramebuffer(uint32_t w, uint32_t h)
{
    if (fbo_ != 0 && fboW_ == w && fboH_ == h)
    {
        return;
    }
    if (accumTex_ != 0)
    {
        glDeleteTextures(1, &accumTex_);
    }
    if (fbo_ != 0)
    {
        glDeleteFramebuffers(1, &fbo_);
    }
    glGenTextures(1, &accumTex_);
    glBindTexture(GL_TEXTURE_2D, accumTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0,
                 GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumTex_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        std::fprintf(stderr, "gg-gl framebuffer incomplete at %ux%u\n", w, h);
    }
    fboW_ = w;
    fboH_ = h;
    fbW_.store(w);
    fbH_.store(h);
}

void GlGrainCanvas::record(double glMs, double gpuMs)
{
    glMsRing_[ringNext_] = glMs;
    if (gpuMs >= 0.0)
    {
        gpuMsRing_[ringNext_] = gpuMs;
        gpuFilled_ = std::min(gpuFilled_ + 1, kAveraged);
    }
    ringNext_ = (ringNext_ + 1) % kAveraged;
    ringFilled_ = std::min(ringFilled_ + 1, kAveraged);
    double s = 0.0;
    for (std::size_t i = 0; i < ringFilled_; ++i)
    {
        s += glMsRing_[i];
    }
    glThreadMs_.store(s / static_cast<double>(ringFilled_));
    if (gpuFilled_ > 0)
    {
        double g = 0.0;
        for (std::size_t i = 0; i < gpuFilled_; ++i)
        {
            g += gpuMsRing_[i];
        }
        gpuMs_.store(g / static_cast<double>(gpuFilled_));
    }
    frames_.fetch_add(1);
}

void GlGrainCanvas::renderOpenGL()
{
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();

    const auto scale = static_cast<float>(context_.getRenderingScale());
    scale_.store(scale);
    const auto physical = [scale](int logical) {
        return static_cast<uint32_t>(
            std::max(1, juce::roundToInt(static_cast<float>(logical) * scale)));
    };
    const int logicalW = getWidth(), logicalH = getHeight();
    const uint32_t width = physical(std::max(1, logicalW - kLaneHeaderWidth));
    const uint32_t height = physical(logicalH);
    const uint32_t windowW = physical(logicalW);
    ensureFramebuffer(width, height);

    // The lanes' row ends, as GrainCanvas::laneRows has them.
    float laneEnds[4];
    for (int layer = 0; layer < 4; ++layer)
    {
        const int bottom = logicalH * (layer + 1) / 4;
        laneEnds[layer] =
            static_cast<float>(layer == 3 ? height : physical(bottom) * (bottom > 0 ? 1u : 0u));
    }
    float tints[12];
    for (int layer = 0; layer < 4; ++layer)
    {
        const uint32_t t = kLayerTints[static_cast<std::size_t>(layer)];
        tints[layer * 3 + 0] = static_cast<float>((t >> 16) & 0xFFu) / 255.0f;
        tints[layer * 3 + 1] = static_cast<float>((t >> 8) & 0xFFu) / 255.0f;
        tints[layer * 3 + 2] = static_cast<float>(t & 0xFFu) / 255.0f;
    }

    // The stress workload for this frame, as the CPU canvas generates it in its timed span.
    stressGrains(frame_++, stressCount_, width, height, scale, stress_.data());

    // Read the oldest query that is done, then issue this frame's.
    double gpuMs = -1.0;
    if (timerOk_.load())
    {
        const int oldest = (queryHead_ + 1) % kQueries;
        if (queryIssued_[static_cast<std::size_t>(oldest)])
        {
            GLuint available = 0;
            glGetQueryObjectuiv(queries_[static_cast<std::size_t>(oldest)],
                                GL_QUERY_RESULT_AVAILABLE, &available);
            if (available != 0)
            {
                GLuint64 ns = 0;
                glGetQueryObjectui64v(queries_[static_cast<std::size_t>(oldest)], GL_QUERY_RESULT,
                                      &ns);
                gpuMs = static_cast<double>(ns) * 1e-6;
                queryIssued_[static_cast<std::size_t>(oldest)] = false;
            }
        }
        glBeginQuery(GL_TIME_ELAPSED, queries_[static_cast<std::size_t>(queryHead_)]);
    }

    glBindVertexArray(vao_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);

    // Pass 1: the splats, additive, into the half-float accumulator.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    splat_->use();
    glUniform2f(uFbSize_, static_cast<float>(width), static_cast<float>(height));
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glVertexAttribDivisor(0, 0);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(stress_.size() * sizeof(Splat)),
                 stress_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Splat), nullptr);
    glVertexAttribDivisor(1, 1);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(stress_.size()));
    glDisableVertexAttribArray(1);
    glVertexAttribDivisor(1, 0);

    // Pass 2: the resolve into the window, over the lanes' rectangle.
    glBindFramebuffer(GL_FRAMEBUFFER, context_.getFrameBufferID());
    glViewport(0, 0, static_cast<GLsizei>(windowW), static_cast<GLsizei>(height));
    glDisable(GL_BLEND);
    glClearColor(11.0f / 255.0f, 12.0f / 255.0f, 15.0f / 255.0f, 1.0f); // the lane ground
    glClear(GL_COLOR_BUFFER_BIT);
    resolve_->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, accumTex_);
    glUniform1i(uAccum_, 0);
    glUniform3fv(uTints_, 4, tints);
    glUniform1fv(uLaneEnds_, 4, laneEnds);
    glUniform1f(uFbHeight_, static_cast<float>(height));
    const float x0 =
        static_cast<float>(physical(kLaneHeaderWidth)) / static_cast<float>(windowW) * 2.0f - 1.0f;
    glUniform2f(uXRange_, x0, 1.0f);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (timerOk_.load())
    {
        glEndQuery(GL_TIME_ELAPSED);
        queryIssued_[static_cast<std::size_t>(queryHead_)] = true;
        queryHead_ = (queryHead_ + 1) % kQueries;
    }
    glBindVertexArray(0);

    // Once, at frame 120: read both targets back and log per-lane sums, the proof the passes
    // draw (the window can't be seen from the measuring session). This frame stalls; the
    // readout at 20 s averages the last 60 frames, so it never sees it.
    if (frame_ == 120)
    {
        std::vector<float> accum(static_cast<std::size_t>(width) * height * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGBA,
                     GL_FLOAT, accum.data());
        std::vector<unsigned char> window(static_cast<std::size_t>(windowW) * height * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, context_.getFrameBufferID());
        glReadPixels(0, 0, static_cast<GLsizei>(windowW), static_cast<GLsizei>(height), GL_RGBA,
                     GL_UNSIGNED_BYTE, window.data());
        const GLenum err = glGetError();
        std::fprintf(stderr, "gg-gl readback frame=%llu glerror=0x%x\n",
                     static_cast<unsigned long long>(frame_), static_cast<unsigned>(err));
        for (int lane = 0; lane < 4; ++lane)
        {
            const uint32_t r0 = lane == 0 ? 0 : static_cast<uint32_t>(laneEnds[lane - 1]);
            const uint32_t r1 = static_cast<uint32_t>(laneEnds[lane]);
            double sum = 0.0, lit = 0.0, rgb = 0.0;
            for (uint32_t y = r0; y < r1 && y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    const float a = accum[(static_cast<std::size_t>(y) * width + x) * 4];
                    sum += a;
                    lit += a > 0.0f ? 1.0 : 0.0;
                    const std::size_t wi =
                        (static_cast<std::size_t>(y) * windowW + x + physical(kLaneHeaderWidth)) *
                        4;
                    rgb += window[wi] + window[wi + 1] + window[wi + 2];
                }
            }
            std::fprintf(stderr,
                         "gg-gl lane=%d rows=[%u,%u) accum_sum=%.1f lit_px=%.0f "
                         "window_rgb_sum=%.0f\n",
                         lane, r0, r1, sum, lit, rgb);
        }
    }

    record(std::chrono::duration<double, std::milli>(Clock::now() - start).count(), gpuMs);
}

void GlGrainCanvas::openGLContextClosing()
{
    splat_.reset();
    resolve_.reset();
    if (accumTex_ != 0)
    {
        glDeleteTextures(1, &accumTex_);
        accumTex_ = 0;
    }
    if (fbo_ != 0)
    {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    glDeleteQueries(kQueries, queries_.data());
    glDeleteBuffers(1, &quadVbo_);
    glDeleteBuffers(1, &instanceVbo_);
    glDeleteVertexArrays(1, &vao_);
    quadVbo_ = instanceVbo_ = vao_ = 0;
    fboW_ = fboH_ = 0;
}
} // namespace gg::ui
