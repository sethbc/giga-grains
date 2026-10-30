// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "MetalGrainCanvas.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "gg/ui/LayerColours.h"
#include "gg/ui/StressGrains.h"

namespace gg::ui
{
namespace
{
constexpr const char *kShaders = R"(
#include <metal_stdlib>
using namespace metal;
struct Grain { float x, y, r, i; };
struct SplatOut { float4 pos [[position]]; float2 local; float intensity; };
vertex SplatOut splatVertex(uint vid [[vertex_id]], uint iid [[instance_id]],
                            const device Grain *grains [[buffer(0)]],
                            constant float2 &fbSize [[buffer(1)]])
{
    float2 corner = float2((vid & 1u) ? 1.0 : -1.0, (vid & 2u) ? 1.0 : -1.0);
    Grain g = grains[iid];
    float hr = g.r + 0.5;
    float2 p = float2(g.x, g.y) + corner * hr;
    float2 ndc = (p + 0.5) / fbSize * 2.0 - 1.0;
    SplatOut o;
    o.pos = float4(ndc.x, -ndc.y, 0.0, 1.0);
    o.local = corner * (hr / g.r);
    o.intensity = g.i;
    return o;
}
fragment float4 splatFragment(SplatOut in [[stage_in]])
{
    float w = max(0.0, 1.0 - dot(in.local, in.local));
    return float4(in.intensity * w, 0.0, 0.0, 0.0);
}
struct ResolveOut { float4 pos [[position]]; float2 uv; };
vertex ResolveOut resolveVertex(uint vid [[vertex_id]], constant float2 &xRange [[buffer(0)]])
{
    float2 corner = float2((vid & 1u) ? 1.0 : -1.0, (vid & 2u) ? 1.0 : -1.0);
    float x = mix(xRange.x, xRange.y, corner.x * 0.5 + 0.5);
    ResolveOut o;
    o.pos = float4(x, corner.y, 0.0, 1.0);
    o.uv = float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);
    return o;
}
struct ResolveUniforms { float4 tints[4]; float4 laneEnds; float fbHeight; };
fragment float4 resolveFragment(ResolveOut in [[stage_in]], texture2d<float> accum [[texture(0)]],
                                constant ResolveUniforms &u [[buffer(0)]])
{
    constexpr sampler s(coord::normalized, filter::nearest);
    float a = clamp(accum.sample(s, in.uv).r, 0.0, 30.0);
    float v = 1.0 - exp(-a);
    float row = in.uv.y * u.fbHeight;
    float3 tint = u.tints[3].rgb;
    if (row < u.laneEnds[0]) tint = u.tints[0].rgb;
    else if (row < u.laneEnds[1]) tint = u.tints[1].rgb;
    else if (row < u.laneEnds[2]) tint = u.tints[2].rgb;
    return float4(tint * v, 1.0);
}
)";

struct ResolveUniforms
{
    float tints[4][4];
    float laneEnds[4];
    float fbHeight;
    float pad[3];
};

constexpr int kInFlight = 3;
constexpr std::size_t kAveraged = 60;

struct Ring
{
    std::array<double, kAveraged> values{};
    std::size_t next{0}, filled{0};
    void push(double v) noexcept
    {
        values[next] = v;
        next = (next + 1) % kAveraged;
        filled = std::min(filled + 1, kAveraged);
    }
    double mean() const noexcept
    {
        if (filled == 0)
        {
            return 0.0;
        }
        double s = 0.0;
        for (std::size_t i = 0; i < filled; ++i)
        {
            s += values[i];
        }
        return s / static_cast<double>(filled);
    }
};
} // namespace

struct MetalGrainCanvas::Impl
{
    uint32_t stressCount{0};
    std::vector<Splat> stress;
    uint64_t frame{0};

    NSView *view = nil;
    CAMetalLayer *layer = nil;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLRenderPipelineState> splatPipeline = nil;
    id<MTLRenderPipelineState> resolvePipeline = nil;
    id<MTLTexture> accum = nil;
    uint32_t accumW{0}, accumH{0};
    std::array<id<MTLBuffer>, kInFlight> instances{};
    int inFlightIndex{0};
    dispatch_semaphore_t inFlight = nullptr;
    bool ready{false};

    Ring uiMs, waitMs;
    std::atomic<uint64_t> frames{0}, skipped{0};
    std::atomic<float> scale{1.0f};
    std::atomic<uint32_t> fbW{0}, fbH{0};
    std::mutex gpuLock;
    Ring gpuMs; // under gpuLock: the completion handlers write it
    std::atomic<double> gpuMean{0.0};

    bool setUp()
    {
        device = MTLCreateSystemDefaultDevice();
        if (device == nil)
        {
            std::fprintf(stderr, "gg-metal: no device\n");
            return false;
        }
        queue = [device newCommandQueue];
        NSError *error = nil;
        id<MTLLibrary> library =
            [device newLibraryWithSource:[NSString stringWithUTF8String:kShaders]
                                 options:nil
                                   error:&error];
        if (library == nil)
        {
            std::fprintf(stderr, "gg-metal shaders: %s\n", error.localizedDescription.UTF8String);
            return false;
        }
        MTLRenderPipelineDescriptor *splat = [[MTLRenderPipelineDescriptor alloc] init];
        splat.vertexFunction = [library newFunctionWithName:@"splatVertex"];
        splat.fragmentFunction = [library newFunctionWithName:@"splatFragment"];
        splat.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        splat.colorAttachments[0].blendingEnabled = YES;
        splat.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        splat.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        splat.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
        splat.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
        splat.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        splat.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
        splatPipeline = [device newRenderPipelineStateWithDescriptor:splat error:&error];
        if (splatPipeline == nil)
        {
            std::fprintf(stderr, "gg-metal splat pipeline: %s\n",
                         error.localizedDescription.UTF8String);
            return false;
        }
        MTLRenderPipelineDescriptor *resolve = [[MTLRenderPipelineDescriptor alloc] init];
        resolve.vertexFunction = [library newFunctionWithName:@"resolveVertex"];
        resolve.fragmentFunction = [library newFunctionWithName:@"resolveFragment"];
        resolve.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        resolvePipeline = [device newRenderPipelineStateWithDescriptor:resolve error:&error];
        if (resolvePipeline == nil)
        {
            std::fprintf(stderr, "gg-metal resolve pipeline: %s\n",
                         error.localizedDescription.UTF8String);
            return false;
        }
        for (auto &b : instances)
        {
            b = [device newBufferWithLength:std::max<std::size_t>(16, stress.size() * sizeof(Splat))
                                    options:MTLResourceStorageModeShared];
        }
        inFlight = dispatch_semaphore_create(kInFlight);

        layer = [CAMetalLayer layer];
        layer.device = device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.opaque = YES;
        view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
        view.layer = layer;
        view.wantsLayer = YES;
        ready = true;
        return true;
    }

    void ensureAccum(uint32_t w, uint32_t h)
    {
        if (accum != nil && accumW == w && accumH == h)
        {
            return;
        }
        MTLTextureDescriptor *d =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                               width:w
                                                              height:h
                                                           mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModeShared; // readable for the frame-120 proof
        accum = [device newTextureWithDescriptor:d];
        accumW = w;
        accumH = h;
        fbW.store(w);
        fbH.store(h);
    }
};

MetalGrainCanvas::MetalGrainCanvas(uint32_t stressCount) : impl_(std::make_unique<Impl>())
{
    setOpaque(true);
    setComponentID("canvas-metal");
    impl_->stressCount = stressCount;
    impl_->stress.resize(stressCount);
    if (impl_->setUp())
    {
        view_.setView((__bridge void *)impl_->view);
        addAndMakeVisible(view_);
    }
}

MetalGrainCanvas::~MetalGrainCanvas() { view_.setView(nullptr); }

void MetalGrainCanvas::resized() { view_.setBounds(getLocalBounds()); }

double MetalGrainCanvas::averageUiMs() const noexcept { return impl_->uiMs.mean(); }
double MetalGrainCanvas::averageWaitMs() const noexcept { return impl_->waitMs.mean(); }
double MetalGrainCanvas::averageGpuMs() const noexcept { return impl_->gpuMean.load(); }
uint64_t MetalGrainCanvas::frames() const noexcept { return impl_->frames.load(); }
uint64_t MetalGrainCanvas::skipped() const noexcept { return impl_->skipped.load(); }
float MetalGrainCanvas::pixelScale() const noexcept { return impl_->scale.load(); }
uint32_t MetalGrainCanvas::fbWidth() const noexcept { return impl_->fbW.load(); }
uint32_t MetalGrainCanvas::fbHeight() const noexcept { return impl_->fbH.load(); }
bool MetalGrainCanvas::ready() const noexcept { return impl_->ready; }

void MetalGrainCanvas::renderFrame()
{
    Impl &m = *impl_;
    if (!m.ready || getWidth() <= 0 || getHeight() <= 0 || !isShowing())
    {
        return;
    }
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();

    // The layer's scale is the window's backing scale.
    const float scale = static_cast<float>(m.view.window != nil ? m.view.window.backingScaleFactor
                                                                : m.layer.contentsScale);
    m.scale.store(scale);
    const auto physical = [scale](int logical) {
        return static_cast<uint32_t>(
            std::max(1, juce::roundToInt(static_cast<float>(logical) * scale)));
    };
    const int logicalW = getWidth(), logicalH = getHeight();
    const uint32_t width = physical(std::max(1, logicalW - kLaneHeaderWidth));
    const uint32_t height = physical(logicalH);
    const uint32_t windowW = physical(logicalW);
    if (m.layer.contentsScale != scale)
    {
        m.layer.contentsScale = scale;
    }
    const CGSize drawableSize = CGSizeMake(windowW, height);
    if (!CGSizeEqualToSize(m.layer.drawableSize, drawableSize))
    {
        m.layer.drawableSize = drawableSize;
    }
    m.ensureAccum(width, height);

    float laneEnds[4];
    for (int layer = 0; layer < 4; ++layer)
    {
        const int bottom = logicalH * (layer + 1) / 4;
        laneEnds[layer] = static_cast<float>(layer == 3 ? height : physical(bottom));
    }
    ResolveUniforms u{};
    for (int layer = 0; layer < 4; ++layer)
    {
        const uint32_t t = kLayerTints[static_cast<std::size_t>(layer)];
        u.tints[layer][0] = static_cast<float>((t >> 16) & 0xFFu) / 255.0f;
        u.tints[layer][1] = static_cast<float>((t >> 8) & 0xFFu) / 255.0f;
        u.tints[layer][2] = static_cast<float>(t & 0xFFu) / 255.0f;
        u.tints[layer][3] = 1.0f;
        u.laneEnds[layer] = laneEnds[layer];
    }
    u.fbHeight = static_cast<float>(height);

    // The workload, into the next instance buffer once the GPU is done with it.
    stressGrains(m.frame, m.stressCount, width, height, scale, m.stress.data());
    // At most kInFlight drawables presented and not yet returned: a frame that would have to
    // wait for one is skipped, as a production renderer would skip it, never blocking the
    // message thread on the display.
    if (dispatch_semaphore_wait(m.inFlight, DISPATCH_TIME_NOW) != 0)
    {
        m.skipped.fetch_add(1);
        return;
    }
    const auto waitStart = Clock::now();
    id<MTLBuffer> instances = m.instances[static_cast<std::size_t>(m.inFlightIndex)];
    m.inFlightIndex = (m.inFlightIndex + 1) % kInFlight;
    std::memcpy(instances.contents, m.stress.data(), m.stress.size() * sizeof(Splat));
    id<CAMetalDrawable> drawable = [m.layer nextDrawable];
    const double waitMs =
        std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
    if (drawable == nil)
    {
        dispatch_semaphore_signal(m.inFlight);
        return;
    }

    id<MTLCommandBuffer> cb = [m.queue commandBuffer];

    // Pass 1: the splats, additive, into the half-float accumulator.
    MTLRenderPassDescriptor *p1 = [MTLRenderPassDescriptor renderPassDescriptor];
    p1.colorAttachments[0].texture = m.accum;
    p1.colorAttachments[0].loadAction = MTLLoadActionClear;
    p1.colorAttachments[0].storeAction = MTLStoreActionStore;
    p1.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    id<MTLRenderCommandEncoder> e1 = [cb renderCommandEncoderWithDescriptor:p1];
    [e1 setRenderPipelineState:m.splatPipeline];
    [e1 setVertexBuffer:instances offset:0 atIndex:0];
    const float fbSize[2] = {static_cast<float>(width), static_cast<float>(height)};
    [e1 setVertexBytes:fbSize length:sizeof(fbSize) atIndex:1];
    [e1 drawPrimitives:MTLPrimitiveTypeTriangleStrip
           vertexStart:0
           vertexCount:4
         instanceCount:m.stress.size()];
    [e1 endEncoding];

    // Pass 2: the resolve into the drawable, over the lanes' rectangle.
    MTLRenderPassDescriptor *p2 = [MTLRenderPassDescriptor renderPassDescriptor];
    p2.colorAttachments[0].texture = drawable.texture;
    p2.colorAttachments[0].loadAction = MTLLoadActionClear;
    p2.colorAttachments[0].storeAction = MTLStoreActionStore;
    p2.colorAttachments[0].clearColor =
        MTLClearColorMake(11.0 / 255.0, 12.0 / 255.0, 15.0 / 255.0, 1);
    id<MTLRenderCommandEncoder> e2 = [cb renderCommandEncoderWithDescriptor:p2];
    [e2 setRenderPipelineState:m.resolvePipeline];
    const float x0 =
        static_cast<float>(physical(kLaneHeaderWidth)) / static_cast<float>(windowW) * 2.0f - 1.0f;
    const float xRange[2] = {x0, 1.0f};
    [e2 setVertexBytes:xRange length:sizeof(xRange) atIndex:0];
    [e2 setFragmentBytes:&u length:sizeof(u) atIndex:0];
    [e2 setFragmentTexture:m.accum atIndex:0];
    [e2 drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [e2 endEncoding];

    Impl *impl = impl_.get();
    [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
      const double ms = (done.GPUEndTime - done.GPUStartTime) * 1000.0;
      const std::lock_guard<std::mutex> lock(impl->gpuLock);
      impl->gpuMs.push(ms);
      impl->gpuMean.store(impl->gpuMs.mean());
    }];
    [drawable addPresentedHandler:^(id<MTLDrawable>) {
      dispatch_semaphore_signal(impl->inFlight);
    }];
    [cb presentDrawable:drawable];
    [cb commit];

    const bool proof = m.frame == 120;
    ++m.frame;
    m.frames.fetch_add(1);
    m.uiMs.push(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    m.waitMs.push(waitMs);

    // Once, at frame 120: the accumulator read back and summed per lane, the proof pass 1
    // draws (the window can't be seen from the measuring session). This frame stalls; the
    // readout at 20 s averages the last 60 frames, so it never sees it.
    if (proof)
    {
        [cb waitUntilCompleted];
        std::vector<_Float16> half(static_cast<std::size_t>(width) * height * 4);
        [m.accum getBytes:half.data()
              bytesPerRow:static_cast<NSUInteger>(width) * 8
               fromRegion:MTLRegionMake2D(0, 0, width, height)
              mipmapLevel:0];
        std::fprintf(stderr, "gg-metal readback frame=120 status=%ld\n",
                     static_cast<long>(cb.status));
        for (int lane = 0; lane < 4; ++lane)
        {
            const uint32_t r0 = lane == 0 ? 0 : static_cast<uint32_t>(laneEnds[lane - 1]);
            const uint32_t r1 = static_cast<uint32_t>(laneEnds[lane]);
            double sum = 0.0, lit = 0.0;
            for (uint32_t y = r0; y < r1 && y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    const float a =
                        static_cast<float>(half[(static_cast<std::size_t>(y) * width + x) * 4]);
                    sum += a;
                    lit += a > 0.0f ? 1.0 : 0.0;
                }
            }
            std::fprintf(stderr, "gg-metal lane=%d rows=[%u,%u) accum_sum=%.1f lit_px=%.0f\n", lane,
                         r0, r1, sum, lit);
        }
    }
}
} // namespace gg::ui
