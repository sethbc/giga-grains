// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "Header.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include <sst/jucegui/components/ButtonPainter.h>

#include "gg/core/Version.h"
#include "gg/ui/UndoStack.h"

namespace gg::ui
{
namespace
{
namespace jcmp = sst::jucegui::components;

constexpr int kMargin = 12;

constexpr std::array<const char *, 4> kViewNames{"Perform", "Edit", "Mod", "FX"};
constexpr std::array<const char *, 4> kViewIds{"header.perform", "header.edit", "header.mod",
                                               "header.fx"};

juce::String fromUtf8(const std::string &text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}
} // namespace

void Header::ProductLabel::mouseUp(const juce::MouseEvent &e)
{
    if (onClick && !e.mouseWasDraggedSinceMouseDown())
    {
        onClick();
    }
}

Header::HistoryButton::HistoryButton(bool redo)
    : jcmp::GlyphButton(redo ? jcmp::GlyphPainter::JOG_RIGHT : jcmp::GlyphPainter::JOG_LEFT),
      redo_(redo)
{
}

void Header::HistoryButton::paint(juce::Graphics &g)
{
    jcmp::paintButtonBG(this, g);
    const float alpha = isEnabled() ? (isHovered ? 1.0f : 0.85f) : 0.35f;
    const auto box = getLocalBounds().toFloat().reduced(6.0f);
    const float side = std::min(box.getWidth(), box.getHeight());
    const auto c = box.getCentre().translated(0.0f, side * 0.12f);
    // An arc over the top from the right to the left, its arrowhead at the left end pointing
    // down (undo); mirrored for redo.
    const float r = side * 0.36f;
    const float sweep = juce::MathConstants<float>::halfPi * 1.3f;
    juce::Path arc;
    arc.addCentredArc(c.x, c.y, r, r, 0.0f, sweep, -sweep, true);
    const auto tip = arc.getCurrentPosition();
    const float h = side * 0.18f;
    juce::Path head;
    head.addTriangle(tip.x - h, tip.y - h * 0.2f, tip.x + h, tip.y - h * 0.2f, tip.x,
                     tip.y + h * 0.9f);
    if (redo_)
    {
        const auto flip = juce::AffineTransform::scale(-1.0f, 1.0f, c.x, c.y);
        arc.applyTransform(flip);
        head.applyTransform(flip);
    }
    g.setColour(getColour(Styles::labelcolor).withAlpha(alpha));
    g.strokePath(arc, juce::PathStrokeType(1.6f));
    g.fillPath(head);
}

Header::Header(const EditorContext &context) : preset_(context), undo_(false), redo_(true)
{
    setComponentID("header");
    setOpaque(true);

    setUpLabel(product_, "header.product", fromUtf8(context.productName),
               juce::FontOptions(18.0f, juce::Font::bold));
    product_.setTooltip("version " + fromUtf8(std::string(gg::kVersion)) +
                        ": click for About (Command I)");
    product_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    product_.onClick = [this] {
        if (onAbout)
        {
            onAbout();
        }
    };
    // The preset strip (M2 sub-project 5's) in the header's preset slot, "header.preset".
    addAndMakeVisible(preset_);
    preset_.onName = [this] {
        if (onPresetName)
        {
            onPresetName();
        }
    };
    preset_.onPrev = [this] {
        if (onPresetPrev)
        {
            onPresetPrev();
        }
    };
    preset_.onNext = [this] {
        if (onPresetNext)
        {
            onPresetNext();
        }
    };
    setUpLabel(frameMs_, "header.frameMs", {}, juce::FontOptions(12.0f));
    frameMs_.setJustificationType(juce::Justification::centredRight);
    frameMs_.setTooltip("The canvas's frame time, the mean of the last 60 frames");

    for (std::size_t i = 0; i < viewButtons_.size(); ++i)
    {
        juce::TextButton &button = viewButtons_[i];
        button.setComponentID(kViewIds[i]);
        button.setButtonText(kViewNames[i]);
        button.setRadioGroupId(1);
        button.setClickingTogglesState(true);
        button.setTooltip(juce::String(kViewNames[i]) + " (Command " +
                          juce::String(static_cast<int>(i) + 1) + ")");
        button.onClick = [this, i] {
            if (onSelectView)
            {
                onSelectView(static_cast<View>(i));
            }
        };
        addAndMakeVisible(button);
    }

    const auto history = [this](bool redo) {
        if (onUndoRedo)
        {
            onUndoRedo(redo);
        }
    };
    undo_.setComponentID("header.undo");
    undo_.setTitle("Undo");
    undo_.setOnCallback([history] { history(false); });
    addAndMakeVisible(undo_);
    redo_.setComponentID("header.redo");
    redo_.setTitle("Redo");
    redo_.setOnCallback([history] { history(true); });
    addAndMakeVisible(redo_);
    theme_.setComponentID("header.theme");
    theme_.setOnCallback([this] {
        if (onTheme)
        {
            onTheme();
        }
    });
    addAndMakeVisible(theme_);
}

void Header::setView(View view)
{
    for (std::size_t i = 0; i < viewButtons_.size(); ++i)
    {
        viewButtons_[i].setToggleState(i == static_cast<std::size_t>(view),
                                       juce::dontSendNotification);
    }
}

void Header::setFrameMs(double ms)
{
    // Label::setText does nothing when the text is unchanged.
    frameMs_.setText(frameMs_.getWidth() >= 150 ? "frame ms (avg 60): " + juce::String(ms, 2)
                                                : juce::String(ms, 2) + " ms",
                     juce::dontSendNotification);
}

void Header::showFrameMs(bool shown) { frameMs_.setVisible(shown); }

void Header::setHistory(const UndoStack *stack)
{
    // Mid-gesture they wait (UndoStack::busy), so the buttons are off then.
    const bool canUndo = stack != nullptr && stack->canUndo() && !stack->busy();
    const bool canRedo = stack != nullptr && stack->canRedo() && !stack->busy();
    undo_.setEnabled(canUndo);
    redo_.setEnabled(canRedo);
    undo_.setTooltip(canUndo ? "Undo " + fromUtf8(stack->undoLabel())
                             : juce::String("Nothing to undo"));
    redo_.setTooltip(canRedo ? "Redo " + fromUtf8(stack->redoLabel())
                             : juce::String("Nothing to redo"));
}

void Header::refreshPreset(bool browserOpen)
{
    preset_.refresh();
    preset_.setBrowserOpen(browserOpen);
}

void Header::focusPresetName() { preset_.focusName(); }

void Header::applyPalette(const Palette &pal, ThemeChoice theme)
{
    product_.setColour(juce::Label::textColourId, pal.text);
    frameMs_.setColour(juce::Label::textColourId, pal.dimText);
    for (juce::TextButton &button : viewButtons_)
    {
        button.setColour(juce::TextButton::buttonColourId, pal.background);
        button.setColour(juce::TextButton::buttonOnColourId, pal.accent.withAlpha(0.35f));
        button.setColour(juce::TextButton::textColourOffId, pal.dimText);
        button.setColour(juce::TextButton::textColourOnId, pal.text);
        button.setColour(juce::ComboBox::outlineColourId, pal.outline);
    }
    const bool light = theme == ThemeChoice::Light;
    theme_.setLabel(light ? "Light" : "Dark");
    theme_.setTitle("Theme");
    theme_.setTooltip(light ? "The light theme: click for the dark"
                            : "The dark theme: click for the light");
    repaint();
}

void Header::paint(juce::Graphics &g) { g.fillAll(paletteOf(*this).header); }

void Header::resized()
{
    auto header = getLocalBounds().reduced(kMargin, 6);
    // The left group is M1's: the product's name and the preset's slot.
    product_.setBounds(header.removeFromLeft(150));
    preset_.setBounds(header.removeFromLeft(90));
    header.removeFromLeft(12);
    // Narrow (the smallest sizes), the buttons are too.
    const bool compact = header.getWidth() < 560;
    const std::array<int, 4> widths =
        compact ? std::array<int, 4>{64, 44, 44, 36} : std::array<int, 4>{76, 56, 56, 48};
    for (std::size_t i = 0; i < viewButtons_.size(); ++i)
    {
        viewButtons_[i].setBounds(header.removeFromLeft(widths[i]));
        header.removeFromLeft(compact ? 2 : 4);
    }
    theme_.setBounds(header.removeFromRight(compact ? 48 : 56));
    header.removeFromRight(compact ? 4 : 8);
    redo_.setBounds(header.removeFromRight(28));
    header.removeFromRight(2);
    undo_.setBounds(header.removeFromRight(28));
    header.removeFromRight(compact ? 4 : 8);
    middle_ = header;
    frameMs_.setBounds(
        middle_.withTrimmedLeft(middle_.getWidth() - std::min(170, middle_.getWidth())));
}

void Header::setUpLabel(juce::Label &label, const char *id, const juce::String &text,
                        const juce::FontOptions &font)
{
    label.setComponentID(id);
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(font));
    label.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(label);
}
} // namespace gg::ui
