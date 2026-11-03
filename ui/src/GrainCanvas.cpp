// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "GrainCanvas.h"

#include "SampleFiles.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>

#include <sst/basic-blocks/params/ParamMetadata.h>

#include "NoticeSink.h"
#include "gg/core/GrainQueue.h"
#include "gg/core/LivePeaks.h"
#include "gg/core/ParamMap.h"
#include "gg/source/SourceMode.h"
#include "gg/ui/UndoStack.h"

namespace gg::ui
{
namespace
{
using gg::core::LayerParam;
using gg::core::ParamSlot;

/// A Live lane before the first activation has a sample rate: the rectangle doesn't depend on
/// it, and there are no grains to place.
constexpr double kFallbackRate = 48000.0;

/// An off lane's alpha.
constexpr float kOffAlpha = 0.4f;

/// The most grains the stress seam draws, whatever the variable says.
constexpr uint32_t kMaxStressGrains = 1u << 20;

/// A loop window's shade, and a selected splice's outline.
constexpr float kLoopAlpha = 0.10f;
constexpr float kSpliceAlpha = 0.8f;
/// A lane this high or more numbers its marks.
constexpr float kMarkNumberMinHeight = 40.0f;

constexpr std::array<LayerParam, 5> kHeldParams{LayerParam::Position, LayerParam::Spray,
                                                LayerParam::Pitch, LayerParam::PitchSpread,
                                                LayerParam::Freeze};

LaneGrain laneGrain(const gg::core::GrainVis &v) noexcept
{
    return {.onsetSample = v.onsetSample,
            .startPos = v.startPos,
            .rate = v.rate,
            .durationSamples = v.durationSamples,
            .gain = v.gain,
            .pan = v.pan};
}

/// GG_STRESS_GRAINS as a count: 0 when unset, empty, not a number or not positive.
uint32_t stressCountFromEnvironment()
{
    const char *text = std::getenv("GG_STRESS_GRAINS");
    if (text == nullptr || *text == '\0')
    {
        return 0;
    }
    char *end = nullptr;
    const long long n = std::strtoll(text, &end, 10);
    if (end == text || n <= 0)
    {
        return 0;
    }
    return static_cast<uint32_t>(std::min<long long>(n, kMaxStressGrains));
}
} // namespace

GrainCanvas::GrainCanvas(const EditorContext &context) : context_(context)
{
    setOpaque(true);
    setComponentID("canvas");
    setTitle("Grains");
    setDescription("The four layers' lanes, each with its grains and its scan rectangle. Drag a "
                   "rectangle to set Position or Pitch, its sides to set Spray, its top or "
                   "bottom to set Pitch Spread; double-click it to freeze the layer. With the "
                   "keyboard, the arrows move the selected lane's rectangle, with Shift its "
                   "spreads.");
    setWantsKeyboardFocus(true);
    stressCount_ = stressCountFromEnvironment();
    if (stressCount_ > 0)
    {
        stress_.resize(stressCount_);
    }
    for (std::vector<float> &marks : marks_)
    {
        marks.reserve(gg::core::kMaxSplices); // a paint never grows them
    }
    showMenu = [this](juce::PopupMenu menu) {
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(menuArea_));
    };
    browse = [this](const Browse &b, std::function<void(const juce::File &)> done) {
        chooser_ = std::make_unique<juce::FileChooser>(b.title, b.initial, b.patterns);
        const int flags = juce::FileBrowserComponent::canSelectFiles |
                          (b.save ? juce::FileBrowserComponent::saveMode |
                                        juce::FileBrowserComponent::warnAboutOverwriting
                                  : juce::FileBrowserComponent::openMode);
        chooser_->launchAsync(flags, [done = std::move(done)](const juce::FileChooser &chooser) {
            done(chooser.getResult());
        });
    };
}

GrainCanvas::~GrainCanvas()
{
    // A drag the release never ended (the editor closed under the mouse) ends here, once.
    if (drag_ != DragTarget::None)
    {
        endDrag();
    }
}

std::size_t GrainCanvas::liveGrainCount() const noexcept
{
    std::size_t n = 0;
    for (const LiveGrains &g : grains_)
    {
        n += g.size();
    }
    return n;
}

void GrainCanvas::setSelectedLayer(uint8_t layer)
{
    if (layer < kLaneCount && layer != selected_)
    {
        selected_ = layer;
        repaint();
    }
}

void GrainCanvas::laneRows(uint8_t layer, float scale, uint32_t &begin,
                           uint32_t &end) const noexcept
{
    const auto row = [&](int logicalRow) {
        return static_cast<uint32_t>(
            std::max(0, juce::roundToInt(static_cast<float>(logicalRow) * scale)));
    };
    const juce::Rectangle<int> b = laneBounds(layer);
    begin = row(b.getY());
    end = layer + 1u == kLaneCount ? row(getHeight()) : row(b.getBottom());
}

juce::Rectangle<int> GrainCanvas::laneBounds(uint8_t layer) const
{
    if (layer >= kLaneCount)
    {
        return {};
    }
    // The lanes tile the canvas in integer rows: lane L is [H L / 4, H (L + 1) / 4).
    const int height = getHeight();
    const int top = height * layer / static_cast<int>(kLaneCount);
    const int bottom = height * (layer + 1) / static_cast<int>(kLaneCount);
    return {kLaneHeaderWidth, top, std::max(0, getWidth() - kLaneHeaderWidth), bottom - top};
}

int GrainCanvas::laneAt(float x, float y) const noexcept
{
    if (!(x >= static_cast<float>(kLaneHeaderWidth)) || !(x < static_cast<float>(getWidth())) ||
        !(y >= 0.0f))
    {
        return -1;
    }
    // The lane whose [top, bottom) holds y: a row two lanes share is the lower lane's.
    for (uint8_t layer = 0; layer < kLaneCount; ++layer)
    {
        if (y < static_cast<float>(laneBounds(layer).getBottom()))
        {
            return layer;
        }
    }
    return -1;
}

bool GrainCanvas::liveLane(uint8_t layer) const
{
    if (haveSnapshot_)
    {
        return snapshot_.lanes[layer].builtKind == static_cast<uint8_t>(gg::SourceMode::Live);
    }
    // Inactive: the kind the Source base value builds (gg::core::builtKind; carry-over row 336).
    const ParamSlot slot = gg::core::layerSlot(LayerParam::Source, layer);
    const float source = context_.baseValue ? context_.baseValue(slot) : 0.0f;
    gg::core::ParamValues values{};
    values[static_cast<std::size_t>(slot)] = std::isfinite(source) ? source : 0.0f;
    return gg::core::builtKind(gg::core::sourceKindOf(values, layer), context_.mode) ==
           gg::SourceMode::Live;
}

bool GrainCanvas::laneOn(uint8_t layer) const
{
    if (haveSnapshot_)
    {
        return snapshot_.lanes[layer].on;
    }
    return !context_.baseValue || context_.baseValue(gg::core::layerOnSlot(layer)) > 0.0f;
}

bool GrainCanvas::laneFrozen(uint8_t layer, double now)
{
    return haveSnapshot_ ? snapshot_.lanes[layer].frozen : shown(layer, kHeldFreeze, now) >= 0.5f;
}

SampleWaveform GrainCanvas::waveformOf(uint8_t layer) const
{
    if (layer >= kLaneCount)
    {
        return {};
    }
    std::optional<SampleWaveform> &wave = waves_[layer];
    if (!wave)
    {
        wave = context_.sampleWaveformFor ? context_.sampleWaveformFor(layer) : SampleWaveform{};
    }
    return *wave;
}

RingView GrainCanvas::ringOf(uint8_t layer) const
{
    if (layer >= kLaneCount)
    {
        return {};
    }
    std::optional<RingView> &ring = rings_[layer];
    if (!ring)
    {
        ring = context_.ringView ? context_.ringView(layer) : RingView{};
    }
    return *ring;
}

void GrainCanvas::forgetFrame() noexcept
{
    for (std::size_t layer = 0; layer < kLaneCount; ++layer)
    {
        waves_[layer].reset();
        rings_[layer].reset();
    }
}

double GrainCanvas::ringSecondsOf(uint8_t layer) const
{
    const RingView ring = ringOf(layer);
    const double rate = context_.sampleRate;
    if (ring.valid && ring.ringFrames > 0 && std::isfinite(rate) && rate > 0.0)
    {
        return static_cast<double>(ring.ringFrames) / rate;
    }
    return kFallbackRingSeconds;
}

gg::core::RingParams GrainCanvas::ringParamsOf(uint8_t layer) const
{
    gg::core::ParamValues values{};
    for (const LayerParam p : {LayerParam::RingLength, LayerParam::Loop, LayerParam::LoopLength,
                               LayerParam::Overdub, LayerParam::Splice})
    {
        const ParamSlot slot = gg::core::layerSlot(p, layer);
        values[static_cast<std::size_t>(slot)] =
            context_.baseValue ? context_.baseValue(slot)
                               : gg::core::paramMeta(slot, context_.mode).defaultVal;
    }
    return gg::core::toRingParams(values, layer);
}

int64_t GrainCanvas::loopFramesOf(const gg::core::RingParams &ring,
                                  double sampleRate) const noexcept
{
    const double frames = std::min(ring.loopSeconds * sampleRate, 0x1.0p40);
    return std::isfinite(frames) ? std::max<int64_t>(1, std::llround(frames)) : 1;
}

int64_t GrainCanvas::laneHead(uint8_t layer, const RingView &ring) const noexcept
{
    return haveSnapshot_ && context_.clock != nullptr ? head_[layer] : ring.head;
}

std::optional<int64_t> GrainCanvas::menuFrame(uint8_t layer) const noexcept
{
    return menuLayer_ == layer ? menuFrame_ : std::nullopt;
}

LaneGeometry GrainCanvas::geometry(uint8_t layer, float scale) const
{
    uint32_t begin = 0, end = 0;
    laneRows(layer, scale, begin, end);
    LaneGeometry g{.width = static_cast<float>(laneBounds(layer).getWidth()) * scale,
                   .height = static_cast<float>(end - begin),
                   .pixelScale = scale,
                   .fxLane = liveLane(layer),
                   .sampleRate = kFallbackRate,
                   .ringSeconds = kFallbackRingSeconds,
                   .sourceFrames = 0,
                   .top = static_cast<float>(begin)};
    if (g.fxLane)
    {
        if (std::isfinite(context_.sampleRate) && context_.sampleRate > 0.0)
        {
            g.sampleRate = context_.sampleRate;
        }
        g.ringSeconds = ringSecondsOf(layer); // its ring's length (VW-P2d, amended)
    }
    else
    {
        // The sample's own frames and rate: GrainVis::startPos's unit, and Spray's seconds.
        const SampleWaveform wave = waveformOf(layer);
        g.sourceFrames = wave.frames;
        g.sampleRate = wave.seconds > 0.0 ? static_cast<double>(wave.frames) / wave.seconds : 0.0;
    }
    return g;
}

bool GrainCanvas::usable(const LaneGeometry &g) noexcept
{
    return g.width > 0.0f && g.height > 0.0f && std::isfinite(g.sampleRate) && g.sampleRate > 0.0 &&
           (g.fxLane || g.sourceFrames > 0);
}

ParamSlot GrainCanvas::slotOf(uint8_t layer, Held which) const noexcept
{
    return gg::core::layerSlot(kHeldParams[which], layer);
}

float GrainCanvas::baseValue(uint8_t layer, Held which) const
{
    // The editor always sets context_.baseValue; the fallback to the snapshot's base values is a
    // test seam, for a canvas made from a bare EditorContext.
    if (context_.baseValue)
    {
        return context_.baseValue(slotOf(layer, which));
    }
    if (!haveSnapshot_)
    {
        return 0.0f;
    }
    const gg::core::LaneSnapshot &lane = snapshot_.lanes[layer];
    switch (which)
    {
    case kHeldPosition:
        return lane.basePosition;
    case kHeldSpray:
        return lane.baseSpray;
    case kHeldPitch:
        return lane.basePitch;
    case kHeldPitchSpread:
        return lane.basePitchSpread;
    case kHeldFreeze:
    case kHeldCount:
        break;
    }
    return lane.frozen ? 1.0f : 0.0f;
}

float GrainCanvas::shown(uint8_t layer, Held which, double now)
{
    return held_[layer][which].shown(baseValue(layer, which), now);
}

ScanRectValues GrainCanvas::shownValues(uint8_t layer, double now)
{
    return {shown(layer, kHeldPosition, now), shown(layer, kHeldSpray, now),
            shown(layer, kHeldPitch, now), shown(layer, kHeldPitchSpread, now)};
}

void GrainCanvas::renderFrame()
{
    forgetFrame(); // a new frame fetches each lane's outline and ring view once
    // The engine's side: null while the plugin is inactive. The stress seam replaces it.
    const std::atomic<int64_t> *clock = stressMode() ? nullptr : context_.clock;
    if (clock != clockSource_)
    {
        // Another activation's structures (or none): what came before is its grains.
        clockSource_ = clock;
        clock_.reset();
        for (LiveGrains &g : grains_)
        {
            g.clear();
        }
        haveSnapshot_ = false;
    }
    if (clock != nullptr)
    {
        // The clock first, then the snapshot (EditorContext::clock).
        const int64_t published = clock->load(std::memory_order_acquire);
        bool took = false;
        if (context_.snapshot != nullptr)
        {
            took = context_.snapshot->take(snapshot_);
            haveSnapshot_ = haveSnapshot_ || took;
        }
        if (clock_.update(published, wallSeconds(), context_.sampleRate))
        {
            // The clock went backwards: a new activation behind the same pointers.
            for (LiveGrains &g : grains_)
            {
                g.clear();
            }
            haveSnapshot_ = took;
        }
        if (context_.grains != grainsSource_)
        {
            // A queue this canvas hasn't read (the editor opening, or a new activation): what it
            // holds was queued while nothing drew it, so it goes unread (Processor::setPublishing).
            grainsSource_ = context_.grains;
            if (context_.grains != nullptr)
            {
                context_.grains->clear();
            }
        }
        if (context_.grains != nullptr)
        {
            gg::core::GrainVis v;
            while (context_.grains->pop(v))
            {
                if (v.layer < kLaneCount)
                {
                    grains_[v.layer].push(laneGrain(v));
                }
            }
        }
        now_ = clock_.now();
        for (uint8_t layer = 0; layer < kLaneCount; ++layer)
        {
            grains_[layer].prune(now_);
            const gg::core::LaneSnapshot &lane = snapshot_.lanes[layer];
            head_[layer] =
                haveSnapshot_ ? headAtNow(lane.writeHead, snapshot_.clock, now_, lane.frozen) : 0;
            // The ring's frames as written by the block the clock published: not extrapolated.
            written_[layer] =
                haveSnapshot_ ? headAtNow(lane.writeHead, snapshot_.clock, published, lane.frozen)
                              : 0;
        }
    }
    else
    {
        grainsSource_ = nullptr;
    }

    // One accumulator pixel per physical pixel, over the lanes alone.
    const auto physical = [this](int logical) {
        return static_cast<uint32_t>(
            std::max(1, juce::roundToInt(static_cast<float>(logical) * scale_)));
    };
    const uint32_t width = physical(std::max(1, getWidth() - kLaneHeaderWidth));
    const uint32_t height = physical(getHeight());
    if (splat_ == nullptr || splat_->width() != width || splat_->height() != height)
    {
        splat_ = std::make_unique<SplatRenderer>(width, height);
        image_ = juce::Image(juce::Image::ARGB, static_cast<int>(width), static_cast<int>(height),
                             false);
    }
    splat_->clear();
    std::array<TintBand, kLaneCount> bands{};
    for (uint8_t layer = 0; layer < kLaneCount; ++layer)
    {
        uint32_t begin = 0, end = 0;
        laneRows(layer, scale_, begin, end);
        bands[layer] = {begin, end, kLayerTints[layer]};
        if (stressMode())
        {
            continue;
        }
        const LaneGeometry lane = geometry(layer, scale_);
        // A Live lane places its grains by its write head, which only a snapshot gives.
        if (clock != nullptr && usable(lane) && (!lane.fxLane || haveSnapshot_))
        {
            grains_[layer].forEach([&](const LaneGrain &grain) {
                if (const auto c = grainCapsule(grain, now_, head_[layer], lane))
                {
                    splat_->addCapsule(c->x0, c->x1, c->y, c->radius, c->intensity);
                }
            });
        }
    }
    if (stressMode())
    {
        stressGrains(stressFrame_++, stressCount_, width, height, scale_, stress_.data());
        for (const Splat &s : stress_)
        {
            splat_->add(s.x, s.y, s.radius, s.intensity);
        }
    }
    const juce::Image::BitmapData pixels(image_, juce::Image::BitmapData::writeOnly);
    jassert(pixels.pixelStride == 4);
    splat_->resolveToArgb(reinterpret_cast<uint32_t *>(pixels.data),
                          static_cast<uint32_t>(pixels.lineStride / 4), bands);
}

float GrainCanvas::accumAt(uint32_t x, uint32_t y) const noexcept
{
    return splat_ != nullptr ? splat_->accumAt(x, y) : 0.0f;
}

double GrainCanvas::averageFrameMs() const noexcept
{
    if (filledFrames_ == 0)
    {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = 0; i < filledFrames_; ++i)
    {
        sum += frameMs_[i];
    }
    return sum / static_cast<double>(filledFrames_);
}

void GrainCanvas::paint(juce::Graphics &g)
{
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const Palette &pal = paletteOf(*this);

    // The context knows the display's scale; the image follows it.
    scale_ = std::max(0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
    renderFrame();
    // The header column is the layer strip's, and chrome: its ground follows the theme (the
    // strip's text is the theme's). The lanes beside it stay dark in both (D9).
    g.fillAll(pal.background);
    g.setColour(pal.lane);
    g.fillRect(kLaneHeaderWidth, 0, std::max(0, getWidth() - kLaneHeaderWidth), getHeight());
    for (uint8_t layer = 0; layer < kLaneCount; ++layer)
    {
        drawLane(g, layer, pal);
    }
    // A 1 px rule between lanes, the selected lane outlined in its color, and the frame.
    g.setColour(pal.laneRule);
    for (uint8_t layer = 1; layer < kLaneCount; ++layer)
    {
        const juce::Rectangle<int> b = laneBounds(layer);
        g.fillRect(b.getX(), b.getY(), b.getWidth(), 1);
    }
    g.drawRect(juce::Rectangle<int>(kLaneHeaderWidth, 0, std::max(0, getWidth() - kLaneHeaderWidth),
                                    getHeight()),
               1);
    g.setColour(pal.layers[selected_]);
    g.drawRect(laneBounds(selected_), 1);

    frameMs_[nextFrame_] = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    nextFrame_ = (nextFrame_ + 1) % kAveragedFrames;
    filledFrames_ = std::min(filledFrames_ + 1, kAveragedFrames);
}

void GrainCanvas::drawLane(juce::Graphics &g, uint8_t layer, const Palette &pal)
{
    marks_[layer].clear(); // what this paint draws, even when it draws nothing
    outlined_[layer].reset();
    const juce::Rectangle<int> b = laneBounds(layer);
    if (b.isEmpty())
    {
        return;
    }
    const bool on = laneOn(layer);
    const bool live = context_.clock != nullptr && haveSnapshot_;
    const double now = wallSeconds();
    g.saveState();
    g.reduceClipRegion(b);
    if (!on)
    {
        g.beginTransparencyLayer(kOffAlpha);
    }
    g.fillAll(pal.lane);
    // The image already has one pixel per physical pixel, so nothing needs filtering.
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.drawImage(image_, juce::Rectangle<float>(static_cast<float>(kLaneHeaderWidth), 0.0f,
                                               static_cast<float>(b.getWidth()),
                                               static_cast<float>(getHeight())));

    // The overlays are in lane coordinates, logical pixels: x from the lane's left edge, y the
    // canvas's.
    g.saveState();
    g.setOrigin(kLaneHeaderWidth, 0);
    const LaneGeometry lane = geometry(layer, 1.0f);
    if (!usable(lane))
    {
        // A Sample lane with no sample: nothing to place, so say what to do.
        g.setColour(pal.laneText);
        g.setFont(juce::FontOptions(14.0f));
        g.drawText(juce::String("Load a sample, or drop one here"),
                   juce::Rectangle<int>(0, b.getY(), b.getWidth(), b.getHeight()),
                   juce::Justification::centred);
    }
    else
    {
        drawPeaks(g, layer, lane, pal);
        if (lane.fxLane)
        {
            drawRing(g, layer, lane, pal);
        }
        const juce::Colour color = pal.layers[layer];
        const WindowRect rect = scanRect(shownValues(layer, now), lane);
        drawRect(g, rect, false, lane.fxLane, color);
        if (layer == selected_ && hasKeyboardFocus(false))
        {
            // The keyboard's lane (spec §6.9): its window outlined in the focus color.
            const float cy = 0.5f * (rect.y0 + rect.y1);
            const float half = std::max(0.5f * (rect.y1 - rect.y0), 1.0f);
            g.setColour(pal.accent);
            g.drawRect(juce::Rectangle<float>(rect.x0, cy - half, std::max(rect.x1 - rect.x0, 1.0f),
                                              2.0f * half)
                           .expanded(3.0f),
                       1.5f);
        }
        // The ghost: where the grains read once macros and modulation are added (spec §6.2),
        // shown when that differs from the base values of the same block.
        if (live)
        {
            const gg::core::LaneSnapshot &s = snapshot_.lanes[layer];
            const auto ghost = ghostRect(
                {s.basePosition, s.baseSpray, s.basePitch, s.basePitchSpread},
                {s.effectivePosition, s.effectiveSpray, s.effectivePitch, s.effectivePitchSpread},
                lane);
            if (ghost)
            {
                drawRect(g, *ghost, true, lane.fxLane, color);
            }
        }
    }
    const juce::Rectangle<float> bounds(0.0f, static_cast<float>(b.getY()),
                                        static_cast<float>(b.getWidth()),
                                        static_cast<float>(b.getHeight()));
    if (laneFrozen(layer, now))
    {
        g.setColour(pal.freeze.withAlpha(0.12f));
        g.fillRect(bounds);
    }
    if (live && lane.fxLane && snapshot_.lanes[layer].feedback > 0.0f)
    {
        // Regeneration at the head: a bar at the right edge, stronger with more Feedback, and
        // an arrowhead pointing back into the ring.
        const float amount =
            std::clamp(snapshot_.lanes[layer].feedback / gg::core::kFeedbackMax, 0.0f, 1.0f);
        g.setColour(pal.feedback.withAlpha(0.35f + 0.65f * amount));
        g.fillRect(bounds.withLeft(bounds.getRight() - 3.0f));
        const float cy = bounds.getCentreY();
        juce::Path arrow;
        arrow.addTriangle(bounds.getRight() - 4.0f, cy - 6.0f, bounds.getRight() - 4.0f, cy + 6.0f,
                          bounds.getRight() - 14.0f, cy);
        g.fillPath(arrow);
    }
    if (dropLane_ == layer)
    {
        g.setColour(pal.layers[layer].withAlpha(0.18f));
        g.fillRect(bounds);
        g.setColour(pal.layers[layer]);
        g.drawRect(bounds, 2.0f);
    }
    g.restoreState(); // the origin
    if (!on)
    {
        g.endTransparencyLayer();
    }
    g.restoreState(); // the clip
}

void GrainCanvas::drawPeaks(juce::Graphics &g, uint8_t layer, const LaneGeometry &lane,
                            const Palette &pal)
{
    // One column per logical pixel, each the peak of what it spans, mirrored about the lane's
    // middle.
    const int columns = static_cast<int>(lane.width);
    if (columns <= 0)
    {
        return;
    }
    PeakStrip &strip = strips_[layer];
    const float mid = lane.top + 0.5f * lane.height;
    const float reach = std::max(0.0f, 0.5f * lane.height - 2.0f);
    const auto addBar = [&](int x, float peak) {
        const float h = std::min(peak, 1.0f) * reach;
        if (h > 0.25f)
        {
            strip.bars.addWithoutMerging({static_cast<float>(x), mid - h, 1.0f, 2.0f * h});
        }
    };
    const auto stale = [&](const void *source, int64_t key) {
        const bool same = strip.valid && strip.source == source && strip.key == key &&
                          strip.top == static_cast<int>(lane.top) && strip.width == columns &&
                          strip.height == static_cast<int>(lane.height) &&
                          strip.rate == lane.sampleRate && strip.span == lane.ringSeconds;
        strip.source = source;
        strip.key = key;
        strip.top = static_cast<int>(lane.top);
        strip.width = columns;
        strip.height = static_cast<int>(lane.height);
        strip.rate = lane.sampleRate;
        strip.span = lane.ringSeconds;
        strip.valid = true;
        return !same;
    };
    if (lane.fxLane)
    {
        const gg::core::LivePeaks *peaks =
            context_.livePeaksFor ? context_.livePeaksFor(layer) : nullptr;
        if (peaks == nullptr || context_.clock == nullptr || peaks->frames() <= 0)
        {
            strip.valid = false;
            strip.outline.reset();
            return;
        }
        // Frames head − span .. head across the lane (peakColumnFrames), but none past the last
        // published block (written) and none older than the strip's oldest whole bucket. That
        // is measured from head, at least written and nearer the writer, which may since have
        // overwritten the buckets just older than written − frames(). Those columns stay empty.
        const double framesPerColumn = lane.ringSeconds * lane.sampleRate / columns;
        const int64_t head = head_[layer];
        if (stale(peaks,
                  static_cast<int64_t>(std::floor(static_cast<double>(head) / framesPerColumn))))
        {
            strip.bars.clear(); // keeps its storage
            strip.outline.reset();
            // The strip spans the longest ring (Processor::livePeaks); the lane reads no older
            // than its own ring holds (row 385): its length, and after a grow the oldest frame
            // its storage got (RingView::oldestHeld), whose bucket is drawn only once whole.
            const RingView ring = ringOf(layer);
            const int64_t held = ring.valid && ring.ringFrames > 0
                                     ? std::min(peaks->frames(), ring.ringFrames)
                                     : peaks->frames();
            int64_t oldest = head - held + gg::core::LivePeaks::kBucket;
            if (ring.valid)
            {
                constexpr int64_t kBucket = gg::core::LivePeaks::kBucket;
                oldest = std::max(oldest, (ring.oldestHeld + kBucket - 1) / kBucket * kBucket);
            }
            for (int x = 0; x < columns; ++x)
            {
                const FrameSpan span =
                    peakColumnFrames(x, columns, framesPerColumn, head, oldest, written_[layer]);
                float peak = 0.0f;
                for (int64_t f = span.first; f < span.end; f += gg::core::LivePeaks::kBucket)
                {
                    peak = std::max(peak, peaks->at(f));
                }
                if (span.first < span.end)
                {
                    peak = std::max(peak, peaks->at(span.end - 1));
                }
                addBar(x, peak);
            }
        }
    }
    else
    {
        const SampleWaveform wave = waveformOf(layer);
        if (wave.peaks == nullptr || wave.peaks->empty())
        {
            strip.valid = false;
            strip.outline.reset();
            return;
        }
        const std::vector<float> &p = *wave.peaks;
        if (stale(wave.peaks.get(), static_cast<int64_t>(p.size())))
        {
            strip.outline = wave.peaks;
            strip.bars.clear();
            const double perColumn = static_cast<double>(p.size()) / columns;
            for (int x = 0; x < columns; ++x)
            {
                auto b0 = static_cast<std::size_t>(x * perColumn);
                auto b1 = static_cast<std::size_t>((x + 1) * perColumn);
                b0 = std::min(b0, p.size() - 1);
                b1 = std::clamp(b1, b0 + 1, p.size());
                float peak = 0.0f;
                for (std::size_t b = b0; b < b1; ++b)
                {
                    peak = std::max(peak, std::isfinite(p[b]) ? p[b] : 0.0f);
                }
                addBar(x, peak);
            }
        }
    }
    g.setColour(pal.peaks.withAlpha(0.55f));
    g.fillRectList(strip.bars);
}

void GrainCanvas::drawRing(juce::Graphics &g, uint8_t layer, const LaneGeometry &lane,
                           const Palette &pal)
{
    RingView ring = ringOf(layer);
    if (!ring.valid || !usable(lane))
    {
        return;
    }
    // Spans that end at the head end at the head the lane draws at (the grains'), not at the
    // last published one.
    const int64_t head = laneHead(layer, ring);
    ring.head = head;
    headDrawn_[layer] = head;
    const double rate = lane.sampleRate;
    const gg::core::RingParams params = ringParamsOf(layer);
    const int64_t loopFrames = loopFramesOf(params, rate);
    const int64_t span = std::llround(lane.ringSeconds * rate);
    const auto xOf = [&](int64_t frame) {
        return ringFrameX(lane, head, std::clamp(frame, head - span, head), rate).value_or(0.0f);
    };
    const float top = lane.top;
    const float height = lane.height;
    const RingSpan window = spliceSpan(ring, params.loop, loopFrames, 0);
    // 1. The loop window, while Loop is on.
    if (params.loop)
    {
        const float x0 = xOf(window.from);
        g.setColour(pal.window.withAlpha(kLoopAlpha));
        g.fillRect(juce::Rectangle<float>(x0, top, std::max(xOf(window.to) - x0, 1.0f), height));
    }
    // 2. The splice the engine plays, while a mark in storage makes one: outlined. The engine
    // numbers the splices from the first mark in storage and ignores Loop (ruling SP-T1a/b), so
    // the lane does too (carry-over row 386).
    const uint32_t count = std::min<uint32_t>(ring.spliceCount, gg::core::kMaxSplices);
    outlined_[layer] = playedSplice(ring, params.splice);
    if (const std::optional<RingSpan> &selected = outlined_[layer])
    {
        const float x0 = xOf(selected->from);
        g.setColour(pal.window.withAlpha(kSpliceAlpha));
        g.drawRect(juce::Rectangle<float>(x0, top + 1.0f, std::max(xOf(selected->to) - x0, 1.0f),
                                          std::max(height - 2.0f, 1.0f)),
                   1.5f);
    }
    // 3. Each mark on the lane: a 1 px rule, numbered from the oldest still in storage, as the
    // Splice parameter counts them.
    const bool numbered = height >= kMarkNumberMinHeight;
    g.setFont(juce::FontOptions(10.0f));
    uint32_t number = 0;
    for (uint32_t k = 0; k < count; ++k)
    {
        const int64_t mark = ring.splices[k];
        const bool inside = mark >= ring.head - ring.ringFrames && mark < ring.head;
        number += inside ? 1u : 0u;
        const std::optional<float> x = ringFrameX(lane, head, mark, rate);
        if (!x)
        {
            continue; // in the future (a stale head) or older than the lane
        }
        marks_[layer].push_back(*x);
        g.setColour(pal.laneRule);
        g.fillRect(juce::Rectangle<float>(*x, top, 1.0f, height));
        if (numbered && inside)
        {
            g.setColour(pal.laneText);
            g.drawText(juce::String(number),
                       juce::Rectangle<float>(*x + 2.0f, top + 1.0f, 20.0f, 12.0f),
                       juce::Justification::centredLeft, false);
        }
    }
}

std::vector<float> GrainCanvas::marksDrawn(uint8_t layer) const
{
    return layer < kLaneCount ? marks_[layer] : std::vector<float>{};
}

int64_t GrainCanvas::headDrawn(uint8_t layer) const noexcept
{
    return layer < kLaneCount ? headDrawn_[layer] : 0;
}

std::optional<RingSpan> GrainCanvas::spliceOutlined(uint8_t layer) const
{
    return layer < kLaneCount ? outlined_[layer] : std::nullopt;
}

juce::Rectangle<float> GrainCanvas::peaksDrawn(uint8_t layer) const
{
    return layer < kLaneCount && strips_[layer].valid ? strips_[layer].bars.getBounds()
                                                      : juce::Rectangle<float>();
}

void GrainCanvas::drawRect(juce::Graphics &g, const WindowRect &r, bool ghost, bool oneSided,
                           juce::Colour color) const
{
    // A band of no height (Pitch Spread 0) still shows: at least 2 px, about its center.
    const float cy = 0.5f * (r.y0 + r.y1);
    const float half = std::max(0.5f * (r.y1 - r.y0), 1.0f);
    const juce::Rectangle<float> box(r.x0, cy - half, std::max(r.x1 - r.x0, 1.0f), 2.0f * half);
    g.setColour(color.withAlpha(ghost ? 0.05f : 0.14f));
    g.fillRect(box);
    if (ghost)
    {
        const float dashes[] = {4.0f, 4.0f};
        g.setColour(color.withAlpha(0.55f));
        g.drawDashedLine({box.getX(), box.getY(), box.getRight(), box.getY()}, dashes, 2, 1.0f);
        g.drawDashedLine({box.getX(), box.getBottom(), box.getRight(), box.getBottom()}, dashes, 2,
                         1.0f);
        g.drawDashedLine({box.getX(), box.getY(), box.getX(), box.getBottom()}, dashes, 2, 1.0f);
        g.drawDashedLine({box.getRight(), box.getY(), box.getRight(), box.getBottom()}, dashes, 2,
                         1.0f);
        return;
    }
    g.setColour(color.withAlpha(0.9f));
    g.drawRect(box, 1.5f);
    // Grips at the middle of the draggable edges: the sides that set Spray (the older one only on
    // a Live lane), and the top and bottom that set Pitch Spread.
    const float mx = box.getCentreX();
    g.fillRoundedRectangle({box.getX() - 2.5f, cy - 6.0f, 5.0f, 12.0f}, 2.0f);
    if (!oneSided)
    {
        g.fillRoundedRectangle({box.getRight() - 2.5f, cy - 6.0f, 5.0f, 12.0f}, 2.0f);
    }
    g.fillRoundedRectangle({mx - 6.0f, box.getY() - 2.5f, 12.0f, 5.0f}, 2.0f);
    g.fillRoundedRectangle({mx - 6.0f, box.getBottom() - 2.5f, 12.0f, 5.0f}, 2.0f);
}

void GrainCanvas::mouseMove(const juce::MouseEvent &e)
{
    const int layer = laneAt(e.position.x, e.position.y);
    if (layer < 0)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    const LaneGeometry lane = geometry(static_cast<uint8_t>(layer), 1.0f);
    if (!usable(lane))
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    const WindowRect r = scanRect(shownValues(static_cast<uint8_t>(layer), wallSeconds()), lane);
    switch (hitTestRect(e.position.x - kLaneHeaderWidth, e.position.y, r, kEdgeTolerancePx, lane))
    {
    case DragTarget::Body:
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        break;
    case DragTarget::LeftEdge:
    case DragTarget::RightEdge:
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        break;
    case DragTarget::TopEdge:
    case DragTarget::BottomEdge:
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        break;
    case DragTarget::None:
        setMouseCursor(juce::MouseCursor::NormalCursor);
        break;
    }
}

void GrainCanvas::mouseDown(const juce::MouseEvent &e)
{
    forgetFrame(); // what the press lands on, as of now
    if (e.mods.isPopupMenu())
    {
        // The lane menu (VW-P2a): it selects the lane and begins no drag.
        const int found = laneAt(e.position.x, e.position.y);
        if (found < 0 || drag_ != DragTarget::None)
        {
            return;
        }
        const auto layer = static_cast<uint8_t>(found);
        setSelectedLayer(layer);
        if (onSelectLayer)
        {
            onSelectLayer(layer);
        }
        const LaneGeometry lane = geometry(layer, 1.0f);
        const RingView ring = ringOf(layer);
        std::optional<int64_t> frame;
        if (lane.fxLane && usable(lane) && ring.valid)
        {
            frame =
                ringFrameAt(lane, laneHead(layer, ring),
                            e.position.x - static_cast<float>(kLaneHeaderWidth), lane.sampleRate);
        }
        menuArea_ = juce::Rectangle<int>(e.getScreenX(), e.getScreenY(), 1, 1);
        openLaneMenu(layer, frame);
        return;
    }
    if (!e.mods.isLeftButtonDown())
    {
        return; // only the primary button drags
    }
    if (drag_ != DragTarget::None)
    {
        endDrag(); // a second press (another touch) while the first drag is live
    }
    const int found = laneAt(e.position.x, e.position.y);
    if (found < 0)
    {
        return;
    }
    const auto layer = static_cast<uint8_t>(found);
    setSelectedLayer(layer);
    if (onSelectLayer)
    {
        onSelectLayer(layer);
    }
    const LaneGeometry lane = geometry(layer, 1.0f);
    if (!usable(lane))
    {
        return;
    }
    const WindowRect r = scanRect(shownValues(layer, wallSeconds()), lane);
    const float x = e.position.x - static_cast<float>(kLaneHeaderWidth);
    const float y = e.position.y;
    const DragTarget target = hitTestRect(x, y, r, kEdgeTolerancePx, lane);
    if (target == DragTarget::None)
    {
        return;
    }
    // The part grabbed moves with the mouse from where it was, wherever in it the press was.
    const float centerX = 0.5f * (r.x0 + r.x1);
    const float centerY = 0.5f * (r.y0 + r.y1);
    drag_ = target;
    dragLayer_ = layer;
    axis_ = DragAxis::Undecided;
    began_ = false;
    sentAny_ = false;
    downX_ = x;
    downY_ = y;
    fixedX_ = lane.fxLane ? r.x1 : centerX;
    fixedY_ = centerY;
    switch (target)
    {
    case DragTarget::Body:
        anchorX_ = fixedX_;
        anchorY_ = centerY;
        return; // its parameter, and so its gesture, wait for the axis
    case DragTarget::LeftEdge:
        anchorX_ = r.x0;
        anchorY_ = centerY;
        beginDragGesture(kHeldSpray);
        return;
    case DragTarget::RightEdge:
        anchorX_ = r.x1;
        anchorY_ = centerY;
        beginDragGesture(kHeldSpray);
        return;
    case DragTarget::TopEdge:
        anchorX_ = centerX;
        anchorY_ = r.y0;
        beginDragGesture(kHeldPitchSpread);
        return;
    case DragTarget::BottomEdge:
        anchorX_ = centerX;
        anchorY_ = r.y1;
        beginDragGesture(kHeldPitchSpread);
        return;
    case DragTarget::None:
        break;
    }
}

void GrainCanvas::beginDragGesture(Held which)
{
    dragHeld_ = which;
    began_ = true;
    dragBefore_ = shown(dragLayer_, which, wallSeconds());
    recordBegin(slotOf(dragLayer_, which), dragBefore_);
    held_[dragLayer_][which].begin();
    if (context_.beginGesture)
    {
        context_.beginGesture(slotOf(dragLayer_, which));
    }
}

void GrainCanvas::recordBegin(ParamSlot slot, float before, bool nudge)
{
    if (context_.undo != nullptr)
    {
        context_.undo->beginGesture(slot, before, gg::core::paramMeta(slot, context_.mode).name,
                                    {.nudge = nudge, .timeSeconds = wallSeconds()});
    }
}

void GrainCanvas::recordEnd(ParamSlot slot, float after)
{
    if (context_.undo != nullptr)
    {
        context_.undo->endGesture(slot, after);
    }
}

void GrainCanvas::gestureTo(uint8_t layer, Held which, float before, float value, double now,
                            bool nudge)
{
    const ParamSlot slot = slotOf(layer, which);
    HeldValue &held = held_[layer][which];
    recordBegin(slot, before, nudge);
    held.begin();
    if (context_.beginGesture)
    {
        context_.beginGesture(slot);
    }
    held.set(value);
    if (context_.setValue)
    {
        context_.setValue(slot, static_cast<double>(value));
    }
    held.end(baseValue(layer, which), now);
    if (context_.endGesture)
    {
        context_.endGesture(slot);
    }
    recordEnd(slot, value);
}

void GrainCanvas::mouseDrag(const juce::MouseEvent &e)
{
    if (drag_ == DragTarget::None)
    {
        return;
    }
    const LaneGeometry lane = geometry(dragLayer_, 1.0f);
    const float lx = e.position.x - static_cast<float>(kLaneHeaderWidth);
    const float ly = e.position.y;
    if (drag_ == DragTarget::Body && axis_ == DragAxis::Undecided)
    {
        axis_ = decideAxis(lx - downX_, ly - downY_, kAxisLockPx);
        if (axis_ == DragAxis::Undecided)
        {
            return;
        }
        beginDragGesture(axis_ == DragAxis::Horizontal ? kHeldPosition : kHeldPitch);
    }
    const float x = lx + (anchorX_ - downX_);
    const float y = ly + (anchorY_ - downY_);
    float value = 0.0f;
    switch (drag_)
    {
    case DragTarget::Body:
        value = axis_ == DragAxis::Horizontal ? positionValueAtX(x, lane) : pitchValueAtY(y, lane);
        break;
    case DragTarget::LeftEdge:
        value = sprayValueForExtent(fixedX_ - x, lane);
        break;
    case DragTarget::RightEdge:
        value = sprayValueForExtent(x - fixedX_, lane);
        break;
    case DragTarget::TopEdge:
        value = pitchSpreadForExtent(fixedY_ - y, lane);
        break;
    case DragTarget::BottomEdge:
        value = pitchSpreadForExtent(y - fixedY_, lane);
        break;
    case DragTarget::None:
        return;
    }
    if (sentAny_ && value == lastSent_)
    {
        return;
    }
    sentAny_ = true;
    lastSent_ = value;
    held_[dragLayer_][dragHeld_].set(value);
    if (context_.setValue)
    {
        context_.setValue(slotOf(dragLayer_, dragHeld_), static_cast<double>(value));
    }
    repaint();
}

void GrainCanvas::mouseUp(const juce::MouseEvent &)
{
    // JUCE sends the release to the component that took the press, wherever the mouse is now.
    if (drag_ != DragTarget::None)
    {
        endDrag();
    }
}

void GrainCanvas::endDrag()
{
    const bool began = began_;
    const uint8_t layer = dragLayer_;
    const Held which = dragHeld_;
    drag_ = DragTarget::None;
    began_ = false;
    if (!began)
    {
        return; // a body press that never moved far enough to pick an axis: no gesture
    }
    held_[layer][which].end(baseValue(layer, which), wallSeconds());
    if (context_.endGesture)
    {
        context_.endGesture(slotOf(layer, which));
    }
    recordEnd(slotOf(layer, which), sentAny_ ? lastSent_ : dragBefore_);
}

bool GrainCanvas::endDragIfAny()
{
    if (drag_ == DragTarget::None)
    {
        return false;
    }
    endDrag();
    repaint();
    return true;
}

bool GrainCanvas::keyPressed(const juce::KeyPress &key)
{
    forgetFrame();
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::escapeKey)
    {
        return endDragIfAny();
    }
    if (code == juce::KeyPress::F10Key && key.getModifiers().isShiftDown() &&
        drag_ == DragTarget::None)
    {
        // The keyboard lane's menu (VW-P2a): its marks go at the anchor.
        menuArea_ = getScreenBounds().getIntersection(
            laneBounds(selected_).translated(getScreenX(), getScreenY()));
        openLaneMenu(selected_, std::nullopt);
        return true;
    }
    const bool horizontal = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey;
    const bool vertical = code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
    if ((!horizontal && !vertical) || key.getModifiers().isCommandDown() ||
        drag_ != DragTarget::None || !usable(geometry(selected_, 1.0f)))
    {
        return false;
    }
    // The selected lane's rectangle (spec §6.9): Left and Right move Position, Up and Down Pitch,
    // by 1/100 of the range; with Shift, Spray and Pitch Spread. Each press is one gesture.
    const bool shift = key.getModifiers().isShiftDown();
    const Held which =
        horizontal ? (shift ? kHeldSpray : kHeldPosition) : (shift ? kHeldPitchSpread : kHeldPitch);
    const float sign =
        code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey ? 1.0f : -1.0f;
    const auto &meta = gg::core::paramMeta(slotOf(selected_, which), context_.mode);
    const double now = wallSeconds();
    const float before = shown(selected_, which, now);
    const float value = std::clamp(before + sign * ((meta.maxVal - meta.minVal) / 100.0f),
                                   meta.minVal, meta.maxVal);
    gestureTo(selected_, which, before, value, now, true);
    repaint();
    return true;
}

void GrainCanvas::focusGained(FocusChangeType) { repaint(); }

void GrainCanvas::focusLost(FocusChangeType) { repaint(); }

void GrainCanvas::mouseDoubleClick(const juce::MouseEvent &e)
{
    forgetFrame();
    if (!e.mods.isLeftButtonDown())
    {
        return;
    }
    const int found = laneAt(e.position.x, e.position.y);
    if (found < 0)
    {
        return;
    }
    const auto layer = static_cast<uint8_t>(found);
    const LaneGeometry lane = geometry(layer, 1.0f);
    if (!usable(lane))
    {
        return;
    }
    const double now = wallSeconds();
    const WindowRect r = scanRect(shownValues(layer, now), lane);
    if (hitTestRect(e.position.x - static_cast<float>(kLaneHeaderWidth), e.position.y, r,
                    kEdgeTolerancePx, lane) != DragTarget::Body)
    {
        return;
    }
    // One gesture: the layer's Freeze flipped from what the lane shows.
    const float freeze = shown(layer, kHeldFreeze, now);
    gestureTo(layer, kHeldFreeze, freeze, freeze >= 0.5f ? 0.0f : 1.0f, now);
    repaint();
}

bool GrainCanvas::isInterestedInFileDrag(const juce::StringArray &)
{
    return true; // any file: the layer's loader says whether it can read it
}

void GrainCanvas::fileDragEnter(const juce::StringArray &, int x, int y) { fileDragMove({}, x, y); }

void GrainCanvas::fileDragMove(const juce::StringArray &, int x, int y)
{
    const int lane = laneAt(static_cast<float>(x), static_cast<float>(y));
    if (lane != dropLane_)
    {
        dropLane_ = lane;
        repaint();
    }
}

void GrainCanvas::fileDragExit(const juce::StringArray &)
{
    dropLane_ = -1;
    repaint();
}

void GrainCanvas::filesDropped(const juce::StringArray &files, int x, int y)
{
    dropLane_ = -1;
    repaint();
    const int found = laneAt(static_cast<float>(x), static_cast<float>(y));
    if (found < 0 || files.isEmpty())
    {
        return;
    }
    const auto layer = static_cast<uint8_t>(found);
    if (!isSampleFilePath(files[0]))
    {
        // As the loader strip refuses it: a notice, and the layer's sample stays.
        postNotice(*this, context_, notSampleFileNotice(files[0]).toStdString());
        return;
    }
    // Every lane takes a sample, in both products (VW-P2c).
    forgetFrame();
    const bool live = liveLane(layer); // the kind built before the load
    if (!context_.loadSampleFor || !context_.loadSampleFor(layer, files[0].toStdString()))
    {
        return;
    }
    if (live)
    {
        // A Live layer keeps reading its ring: the drop doesn't switch its Source.
        const std::string letter(1, kLayerLetters[layer]);
        postNotice(*this, context_,
                   letter + "'s sample is loading: set " + letter + " Source to Sample to play it");
    }
}
} // namespace gg::ui
