// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "MorphPad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "Palette.h"
#include "ParamValue.h"
#include "gg/core/ParamIds.h"
#include "gg/ui/LayerColours.h"
#include "gg/ui/UndoStack.h"

namespace gg::ui
{
namespace
{
using gg::core::ParamSlot;

constexpr const char *kUndoLabel = "Morph";
constexpr float kHandleRadius = 7.0f;
} // namespace

struct MorphPad::Impl
{
    Impl(const EditorContext &c)
        : context(c), x(c, ParamSlot::MorphX, "Morph X"), y(c, ParamSlot::MorphY, "Morph Y")
    {
    }

    const EditorContext &context;
    ParamValue x, y;
    std::optional<PadPoint> ghost;
    bool dragging{false};
    bool grouped{false}; ///< begin opened an undo group that end hasn't closed
    std::array<bool, gg::core::kLayers> lastOn{};

    /// The largest centred square in the component, in its coordinates.
    static juce::Rectangle<float> squareOf(const juce::Component &c)
    {
        const auto bounds = c.getLocalBounds().toFloat();
        const float side = std::min(bounds.getWidth(), bounds.getHeight());
        return bounds.withSizeKeepingCentre(side, side);
    }

    std::array<bool, gg::core::kLayers> layersOn() const
    {
        std::array<bool, gg::core::kLayers> on{};
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            const ParamSlot slot = gg::core::layerOnSlot(layer);
            const float v = context.baseValue ? context.baseValue(slot)
                                              : gg::core::paramMeta(slot, context.mode).defaultVal;
            on[layer] = v > 0.5f;
        }
        return on;
    }

    /// Opens the gesture on both parameters, and the undo group. The pad opens the two undo
    /// gestures itself, before the values do, so the entry is labelled "Morph"; the values close
    /// them (ParamValue).
    void begin()
    {
        if (UndoStack *undo = context.undo)
        {
            undo->beginGroup();
            undo->beginGesture(ParamSlot::MorphX, x.value(), kUndoLabel);
            undo->beginGesture(ParamSlot::MorphY, y.value(), kUndoLabel);
        }
        grouped = context.undo != nullptr;
        x.begin();
        y.begin();
    }

    void set(PadPoint p)
    {
        x.set(p.x);
        y.set(p.y);
    }

    /// Closes both gestures and, if begin opened one, the undo group: one entry.
    void end()
    {
        x.end();
        y.end();
        if (grouped && context.undo != nullptr)
        {
            context.undo->endGroup();
        }
        grouped = false;
    }

    /// One whole gesture to `p`.
    void gestureTo(PadPoint p)
    {
        begin();
        set(p);
        end();
    }
};

MorphPad::MorphPad(const EditorContext &context) : impl_(std::make_unique<Impl>(context))
{
    setComponentID("pad");
    setTitle("Morph");
    setWantsKeyboardFocus(true);
    impl_->lastOn = impl_->layersOn();
}

MorphPad::~MorphPad()
{
    // A drag the release never ended (the editor closed under the mouse): its gestures and its
    // undo group close here, as one entry. The undo stack outlives the editor (EditorContext).
    impl_->end();
}

void MorphPad::refresh()
{
    const bool movedX = impl_->x.refresh();
    const bool movedY = impl_->y.refresh();
    const auto on = impl_->layersOn();
    if (movedX || movedY || on != impl_->lastOn)
    {
        impl_->lastOn = on;
        repaint();
    }
}

void MorphPad::setGhost(std::optional<PadPoint> ghost)
{
    impl_->ghost = ghost;
    repaint();
}

PadPoint MorphPad::value() const noexcept { return {impl_->x.value(), impl_->y.value()}; }

void MorphPad::paint(juce::Graphics &g)
{
    const Palette &palette = paletteOf(*this);
    const auto square = Impl::squareOf(*this);
    const float side = square.getWidth();
    const PadPoint p = value();

    g.setColour(palette.lane);
    g.fillRect(square);

    // Quadrants, as laid out in the corners: C top-left, D top-right, A bottom-left, B
    // bottom-right.
    const auto weights = gg::ui::morphWeights(p.x, p.y, impl_->layersOn());
    const float half = side * 0.5f;
    const std::array<juce::Rectangle<float>, gg::core::kLayers> quadrants{
        juce::Rectangle<float>(square.getX(), square.getY() + half, half, half),
        juce::Rectangle<float>(square.getX() + half, square.getY() + half, half, half),
        juce::Rectangle<float>(square.getX(), square.getY(), half, half),
        juce::Rectangle<float>(square.getX() + half, square.getY(), half, half)};
    for (std::size_t layer = 0; layer < quadrants.size(); ++layer)
    {
        g.setColour(palette.layers[layer].withAlpha(0.55f * weights[layer]));
        g.fillRect(quadrants[layer]);
    }
    g.setColour(palette.outline);
    g.drawRect(square, 1.0f);
    g.drawLine(square.getCentreX(), square.getY(), square.getCentreX(), square.getBottom(), 0.5f);
    g.drawLine(square.getX(), square.getCentreY(), square.getRight(), square.getCentreY(), 0.5f);

    // The corners' letters.
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    for (std::size_t layer = 0; layer < quadrants.size(); ++layer)
    {
        g.setColour(palette.layers[layer]);
        g.drawText(juce::String::charToString(kLayerLetters[layer]),
                   quadrants[layer].reduced(4.0f).toNearestInt(),
                   (layer % 2 == 0 ? juce::Justification::left : juce::Justification::right) |
                       (layer < 2 ? juce::Justification::bottom : juce::Justification::top),
                   false);
    }

    const auto at = [&](PadPoint v) {
        const auto px = padPixelOf(v, side);
        return juce::Point<float>(square.getX() + px[0], square.getY() + px[1]);
    };
    if (impl_->ghost && std::isfinite(impl_->ghost->x) && std::isfinite(impl_->ghost->y))
    {
        g.setColour(palette.ghost);
        g.drawEllipse(juce::Rectangle<float>(kHandleRadius * 2, kHandleRadius * 2)
                          .withCentre(at(*impl_->ghost)),
                      1.5f);
    }
    g.setColour(palette.accent);
    g.fillEllipse(juce::Rectangle<float>(kHandleRadius * 2, kHandleRadius * 2).withCentre(at(p)));
}

void MorphPad::mouseDown(const juce::MouseEvent &e)
{
    // The second press of a double-click moves nothing: mouseDoubleClick, which JUCE sends after
    // that press's mouseUp, makes the one gesture, so a double-click is a single undo entry.
    if (e.getNumberOfClicks() >= 2)
    {
        return;
    }
    impl_->dragging = true;
    impl_->begin();
    mouseDrag(e);
}

void MorphPad::mouseDrag(const juce::MouseEvent &e)
{
    if (!impl_->dragging)
    {
        return;
    }
    const auto square = Impl::squareOf(*this);
    const auto local = e.position - square.getPosition();
    impl_->set(padValueAt(local.x, local.y, square.getWidth()));
    repaint();
}

void MorphPad::mouseUp(const juce::MouseEvent &)
{
    if (impl_->dragging)
    {
        impl_->dragging = false;
        impl_->end();
        repaint();
    }
}

void MorphPad::mouseDoubleClick(const juce::MouseEvent &)
{
    // The second press began no gesture (mouseDown), so this is the gesture of its own.
    impl_->gestureTo({0.5f, 0.5f});
    repaint();
}

bool MorphPad::keyPressed(const juce::KeyPress &key)
{
    const int code = key.getKeyCode();
    const float step = key.getModifiers().isShiftDown() ? 0.001f : 0.01f;
    float dx = 0.0f;
    float dy = 0.0f;
    if (code == juce::KeyPress::rightKey)
    {
        dx = step;
    }
    else if (code == juce::KeyPress::leftKey)
    {
        dx = -step;
    }
    else if (code == juce::KeyPress::upKey)
    {
        dy = step;
    }
    else if (code == juce::KeyPress::downKey)
    {
        dy = -step;
    }
    else
    {
        return false;
    }
    if (key.getModifiers().isCommandDown() || impl_->dragging)
    {
        return false;
    }
    const PadPoint p = value();
    impl_->gestureTo({std::clamp(p.x + dx, 0.0f, 1.0f), std::clamp(p.y + dy, 0.0f, 1.0f)});
    repaint();
    return true;
}
} // namespace gg::ui
