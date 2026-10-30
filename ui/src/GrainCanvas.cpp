// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "GrainCanvas.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <vector>

#include "Theme.h"
#include "gg/core/GrainQueue.h"
#include "gg/core/LivePeaks.h"

namespace gg::ui
{
namespace
{
using gg::core::ParamSlot;

/// The FX's lane before the first activation has a sample rate: the window doesn't depend on
/// it, and there are no grains to place.
constexpr double kFallbackRate = 48000.0;

LaneGrain laneGrain(const gg::core::GrainVis &v) noexcept
{
    return {.onsetSample = v.onsetSample,
            .startPos = v.startPos,
            .rate = v.rate,
            .durationSamples = v.durationSamples,
            .gain = v.gain,
            .pan = v.pan};
}
} // namespace

GrainCanvas::GrainCanvas(const EditorContext &context) : context_(context)
{
    setOpaque(true);
    setComponentID("canvas");
    setTitle("Grains");
    setDescription(fxLane() ? "The live input and its grains. Drag the window to set Position, "
                              "its left edge to set Spray."
                            : "The sample and its grains. Drag the window to set Position, an "
                              "edge to set Spray.");
}

GrainCanvas::~GrainCanvas()
{
    // A drag the release never ended (the editor closed under the mouse) ends here, once.
    if (drag_ != DragTarget::None)
    {
        endDrag();
    }
}

LaneGeometry GrainCanvas::geometry(float scale) const
{
    LaneGeometry g{.width = static_cast<float>(getWidth()) * scale,
                   .height = static_cast<float>(getHeight()) * scale,
                   .pixelScale = scale,
                   .fxLane = fxLane(),
                   .sampleRate = kFallbackRate,
                   .ringSeconds = kRingSeconds,
                   .sourceFrames = 0};
    if (g.fxLane)
    {
        if (std::isfinite(context_.sampleRate) && context_.sampleRate > 0.0)
        {
            g.sampleRate = context_.sampleRate;
        }
    }
    else
    {
        // The sample's own frames and rate: GrainVis::startPos's unit, and Spray's seconds.
        const SampleWaveform wave =
            context_.sampleWaveform ? context_.sampleWaveform() : SampleWaveform{};
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

float GrainCanvas::baseValue(ParamSlot slot) const
{
    // The editor always sets context_.baseValue; the fallback to the snapshot's base values is a
    // test seam, for a canvas made from a bare EditorContext.
    if (context_.baseValue)
    {
        return context_.baseValue(slot);
    }
    return haveSnapshot_
               ? (slot == ParamSlot::Position ? snapshot_.basePosition : snapshot_.baseSpray)
               : 0.0f;
}

float GrainCanvas::shownPosition(double now)
{
    return heldPosition_.shown(baseValue(ParamSlot::Position), now);
}

float GrainCanvas::shownSpray(double now)
{
    return heldSpray_.shown(baseValue(ParamSlot::Spray), now);
}

void GrainCanvas::renderFrame()
{
    // The engine's side: null while the plugin is inactive.
    const std::atomic<int64_t> *clock = context_.clock;
    if (clock != clockSource_)
    {
        // Another activation's structures (or none): what came before is its grains.
        clockSource_ = clock;
        clock_.reset();
        grains_.clear();
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
            grains_.clear();
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
                // INTENT §17: until the layers' views, the view still shows layer A alone.
                if (v.layer == 0)
                {
                    grains_.push(laneGrain(v));
                }
            }
        }
        now_ = clock_.now();
        grains_.prune(now_);
        head_ = haveSnapshot_
                    ? headAtNow(snapshot_.writeHead, snapshot_.clock, now_, snapshot_.frozen)
                    : 0;
        // The ring's frames as written by the block the clock published: not extrapolated.
        written_ = haveSnapshot_ ? headAtNow(snapshot_.writeHead, snapshot_.clock, published,
                                             snapshot_.frozen)
                                 : 0;
    }
    else
    {
        grainsSource_ = nullptr;
    }

    // One accumulator pixel per physical pixel.
    const auto physical = [this](int logical) {
        return static_cast<uint32_t>(
            std::max(1, juce::roundToInt(static_cast<float>(logical) * scale_)));
    };
    const uint32_t width = physical(getWidth());
    const uint32_t height = physical(getHeight());
    if (splat_ == nullptr || splat_->width() != width || splat_->height() != height)
    {
        splat_ = std::make_unique<SplatRenderer>(width, height);
        image_ = juce::Image(juce::Image::ARGB, static_cast<int>(width), static_cast<int>(height),
                             false);
    }
    splat_->clear();
    const LaneGeometry lane = geometry(scale_);
    // The FX lane places its grains by the write head, which only a snapshot gives.
    if (clock != nullptr && usable(lane) && (!fxLane() || haveSnapshot_))
    {
        grains_.forEach([&](const LaneGrain &grain) {
            if (const auto c = grainCapsule(grain, now_, head_, lane))
            {
                splat_->addCapsule(c->x0, c->x1, c->y, c->radius, c->intensity);
            }
        });
    }
    const juce::Image::BitmapData pixels(image_, juce::Image::BitmapData::writeOnly);
    jassert(pixels.pixelStride == 4);
    splat_->resolveToArgb(reinterpret_cast<uint32_t *>(pixels.data),
                          static_cast<uint32_t>(pixels.lineStride / 4), theme::kGrainTint);
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

    // The context knows the display's scale; the image follows it.
    scale_ = std::max(0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
    renderFrame();
    // The image already has one pixel per physical pixel, so nothing needs filtering.
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.drawImage(image_, getLocalBounds().toFloat());

    const LaneGeometry lane = geometry(1.0f); // the overlays are drawn in logical pixels
    const bool live = context_.clock != nullptr && haveSnapshot_;
    if (!usable(lane))
    {
        // The instrument with no sample: nothing to place, so say what to do.
        g.setColour(theme::kDimText);
        g.setFont(juce::FontOptions(15.0f));
        g.drawText(juce::String("Load a sample, or drop one here"), getLocalBounds(),
                   juce::Justification::centred);
    }
    else
    {
        drawPeaks(g, lane);
        const double now = wallSeconds();
        drawWindow(g, scanWindow(shownPosition(now), shownSpray(now), lane), false);
        // The ghost: where the grains read once macros and modulation are added (spec §6.2),
        // shown when that differs from the base values of the same block.
        if (live)
        {
            const auto ghost =
                ghostWindow({snapshot_.basePosition, snapshot_.baseSpray},
                            {snapshot_.effectivePosition, snapshot_.effectiveSpray}, lane);
            if (ghost)
            {
                drawWindow(g, *ghost, true);
            }
        }
    }
    const auto bounds = getLocalBounds().toFloat();
    if (live && snapshot_.frozen)
    {
        g.setColour(theme::kFreeze.withAlpha(0.12f));
        g.fillRect(bounds);
    }
    if (live && fxLane() && snapshot_.feedback > 0.0f)
    {
        // Regeneration at the head: a bar at the right edge, stronger with more Feedback, and
        // an arrowhead pointing back into the ring.
        const float amount = std::clamp(snapshot_.feedback / gg::core::kFeedbackMax, 0.0f, 1.0f);
        g.setColour(theme::kFeedback.withAlpha(0.35f + 0.65f * amount));
        g.fillRect(bounds.withLeft(bounds.getRight() - 3.0f));
        const float cy = bounds.getCentreY();
        juce::Path arrow;
        arrow.addTriangle(bounds.getRight() - 4.0f, cy - 6.0f, bounds.getRight() - 4.0f, cy + 6.0f,
                          bounds.getRight() - 14.0f, cy);
        g.fillPath(arrow);
    }
    g.setColour(theme::kOutline);
    g.drawRect(getLocalBounds(), 1);

    frameMs_[nextFrame_] = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    nextFrame_ = (nextFrame_ + 1) % kAveragedFrames;
    filledFrames_ = std::min(filledFrames_ + 1, kAveragedFrames);
}

void GrainCanvas::drawPeaks(juce::Graphics &g, const LaneGeometry &lane)
{
    // One column per logical pixel, each the peak of what it spans, mirrored about the middle.
    const int columns = getWidth();
    if (columns <= 0)
    {
        return;
    }
    const float mid = 0.5f * static_cast<float>(getHeight());
    const float reach = std::max(0.0f, mid - 2.0f);
    bars_.clear(); // keeps its storage
    const auto addBar = [&](int x, float peak) {
        const float h = std::min(peak, 1.0f) * reach;
        if (h > 0.25f)
        {
            bars_.addWithoutMerging({static_cast<float>(x), mid - h, 1.0f, 2.0f * h});
        }
    };
    if (lane.fxLane)
    {
        const gg::core::LivePeaks *peaks = context_.livePeaks;
        if (peaks == nullptr || context_.clock == nullptr || peaks->frames() <= 0)
        {
            return;
        }
        // Frames head − span .. head across the lane (peakColumnFrames), but none past the last
        // published block (written_) and none older than the strip's oldest whole bucket. That
        // is measured from head_, at least written_ and nearer the writer, which may since have
        // overwritten the buckets just older than written_ − frames(). Those columns stay empty.
        const double framesPerColumn = kRingSeconds * lane.sampleRate / columns;
        const int64_t oldest = head_ - peaks->frames() + gg::core::LivePeaks::kBucket;
        for (int x = 0; x < columns; ++x)
        {
            const FrameSpan span =
                peakColumnFrames(x, columns, framesPerColumn, head_, oldest, written_);
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
    else
    {
        const SampleWaveform wave =
            context_.sampleWaveform ? context_.sampleWaveform() : SampleWaveform{};
        if (wave.peaks == nullptr || wave.peaks->empty())
        {
            return;
        }
        const std::vector<float> &p = *wave.peaks;
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
    g.setColour(theme::kPeaks.withAlpha(0.55f));
    g.fillRectList(bars_);
}

void GrainCanvas::drawWindow(juce::Graphics &g, const Window &w, bool ghost) const
{
    const float height = static_cast<float>(getHeight());
    const float x0 = std::min(w.x0, w.x1);
    const float x1 = std::max(w.x0, w.x1);
    const juce::Colour colour = ghost ? theme::kGhost : theme::kWindow;
    g.setColour(colour.withAlpha(ghost ? 0.05f : 0.14f));
    g.fillRect(juce::Rectangle<float>(x0, 0.0f, std::max(x1 - x0, 1.0f), height));
    if (ghost)
    {
        const float dashes[] = {4.0f, 4.0f};
        g.setColour(colour.withAlpha(0.55f));
        g.drawDashedLine({x0, 0.0f, x0, height}, dashes, 2, 1.0f);
        g.drawDashedLine({x1, 0.0f, x1, height}, dashes, 2, 1.0f);
        return;
    }
    g.setColour(colour.withAlpha(0.9f));
    g.fillRect(juce::Rectangle<float>(x0 - 0.75f, 0.0f, 1.5f, height));
    g.fillRect(juce::Rectangle<float>(x1 - 0.75f, 0.0f, 1.5f, height));
    // Grips at the middle of the draggable edges.
    const float cy = 0.5f * height;
    g.fillRoundedRectangle({x0 - 2.5f, cy - 10.0f, 5.0f, 20.0f}, 2.0f);
    if (!fxLane())
    {
        g.fillRoundedRectangle({x1 - 2.5f, cy - 10.0f, 5.0f, 20.0f}, 2.0f);
    }
}

void GrainCanvas::mouseMove(const juce::MouseEvent &e)
{
    const LaneGeometry lane = geometry(1.0f);
    if (!usable(lane))
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    const double now = wallSeconds();
    const Window w = scanWindow(shownPosition(now), shownSpray(now), lane);
    switch (gg::ui::hitTest(e.position.x, w, kEdgeTolerancePx, lane))
    {
    case DragTarget::Body:
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        break;
    case DragTarget::LeftEdge:
    case DragTarget::RightEdge:
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        break;
    case DragTarget::None:
        setMouseCursor(juce::MouseCursor::NormalCursor);
        break;
    }
}

void GrainCanvas::mouseDown(const juce::MouseEvent &e)
{
    if (!e.mods.isLeftButtonDown())
    {
        return; // only the primary button drags; a right-click is no gesture
    }
    if (drag_ != DragTarget::None)
    {
        endDrag(); // a second press (another touch) while the first drag is live
    }
    const LaneGeometry lane = geometry(1.0f);
    if (!usable(lane))
    {
        return;
    }
    const double now = wallSeconds();
    const Window w = scanWindow(shownPosition(now), shownSpray(now), lane);
    const DragTarget target = gg::ui::hitTest(e.position.x, w, kEdgeTolerancePx, lane);
    if (target == DragTarget::None)
    {
        return;
    }
    // The part grabbed moves with the mouse from where it was, wherever in it the press was.
    const float positionX = lane.fxLane ? w.x1 : 0.5f * (w.x0 + w.x1);
    drag_ = target;
    downX_ = e.position.x;
    dragSlot_ = target == DragTarget::Body ? ParamSlot::Position : ParamSlot::Spray;
    anchorX_ = target == DragTarget::Body       ? positionX
               : target == DragTarget::LeftEdge ? w.x0
                                                : w.x1;
    fixedX_ = positionX;
    sentAny_ = false;
    (dragSlot_ == ParamSlot::Position ? heldPosition_ : heldSpray_).begin();
    if (context_.beginGesture)
    {
        context_.beginGesture(dragSlot_);
    }
}

void GrainCanvas::mouseDrag(const juce::MouseEvent &e)
{
    if (drag_ == DragTarget::None)
    {
        return;
    }
    const LaneGeometry lane = geometry(1.0f);
    const float x = e.position.x + (anchorX_ - downX_);
    float value = 0.0f;
    switch (drag_)
    {
    case DragTarget::Body:
        value = positionValueAtX(x, lane);
        break;
    case DragTarget::LeftEdge:
        value = sprayValueForExtent(fixedX_ - x, lane);
        break;
    case DragTarget::RightEdge:
        value = sprayValueForExtent(x - fixedX_, lane);
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
    (dragSlot_ == ParamSlot::Position ? heldPosition_ : heldSpray_).set(value);
    if (context_.setValue)
    {
        context_.setValue(dragSlot_, static_cast<double>(value));
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
    const ParamSlot slot = dragSlot_;
    drag_ = DragTarget::None;
    (slot == ParamSlot::Position ? heldPosition_ : heldSpray_).end(baseValue(slot), wallSeconds());
    if (context_.endGesture)
    {
        context_.endGesture(slot);
    }
}
} // namespace gg::ui
