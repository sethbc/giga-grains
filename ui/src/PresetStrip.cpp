// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "PresetStrip.h"

#include <algorithm>

#include "Theme.h"

namespace gg::ui
{
namespace
{
constexpr int kArrowWidth = 16;

float widthOf(const juce::String &text, const juce::Font &font)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox(0, -1, true).getWidth();
}

void styleButton(juce::TextButton &button, const char *id, const juce::String &text)
{
    button.setComponentID(id);
    button.setButtonText(text);
    button.setColour(juce::TextButton::buttonColourId, theme::kBackground);
    button.setColour(juce::TextButton::buttonOnColourId, theme::kAccent.withAlpha(0.35f));
    button.setColour(juce::TextButton::textColourOffId, theme::kDimText);
    button.setColour(juce::TextButton::textColourOnId, theme::kText);
    button.setColour(juce::ComboBox::outlineColourId, theme::kOutline);
}
} // namespace

juce::String elideMiddle(const juce::String &text, const juce::Font &font, float width)
{
    if (widthOf(text, font) <= width)
    {
        return text;
    }
    const juce::String ellipsis = juce::String::fromUTF8("\xe2\x80\xa6");
    // The most characters kept (half at each end) that fit; the ellipsis alone at the least.
    for (int kept = text.length() - 1; kept > 0; --kept)
    {
        const int head = (kept + 1) / 2;
        const int tail = kept / 2;
        const juce::String cut =
            text.substring(0, head) + ellipsis + text.substring(text.length() - tail);
        if (widthOf(cut, font) <= width)
        {
            return cut;
        }
    }
    return ellipsis;
}

PresetStrip::PresetStrip(const EditorContext &context) : context_(context)
{
    setComponentID("header.preset");
    setTooltip("The preset");
    styleButton(prev_, "preset.prev", juce::String::fromUTF8("\xe2\x80\xb9"));
    styleButton(name_, "preset.name", "Init");
    styleButton(next_, "preset.next", juce::String::fromUTF8("\xe2\x80\xba"));
    name_.setColour(juce::TextButton::textColourOffId, theme::kText);
    name_.setTooltip("The preset: click to browse");
    prev_.setTooltip("Previous preset");
    next_.setTooltip("Next preset");
    prev_.onClick = [this] {
        if (onPrev)
        {
            onPrev();
        }
    };
    name_.onClick = [this] {
        if (onName)
        {
            onName();
        }
    };
    next_.onClick = [this] {
        if (onNext)
        {
            onNext();
        }
    };
    addAndMakeVisible(prev_);
    addAndMakeVisible(name_);
    addAndMakeVisible(next_);
    refresh();
}

void PresetStrip::refresh()
{
    juce::String text = "Init";
    if (context_.presetName)
    {
        text = juce::String::fromUTF8(context_.presetName().c_str());
    }
    const bool edited = context_.presetEdited && context_.presetEdited();
    const juce::String mark = edited ? juce::String::fromUTF8(" \xe2\x80\xa2") : juce::String();
    const juce::String shown = text + mark;
    if (shown == fullName_ && name_.getWidth() == shownWidth_)
    {
        return;
    }
    fullName_ = shown;
    shownWidth_ = name_.getWidth();
    const juce::Font font = juce::Font(juce::FontOptions(13.0f));
    const float room = static_cast<float>(name_.getWidth()) - 8.0f - widthOf(mark, font);
    name_.setButtonText(elideMiddle(text, font, std::max(room, 0.0f)) + mark);
    name_.setTooltip(shown);
}

void PresetStrip::setBrowserOpen(bool open)
{
    name_.setToggleState(open, juce::dontSendNotification);
}

void PresetStrip::resized()
{
    auto area = getLocalBounds();
    prev_.setBounds(area.removeFromLeft(kArrowWidth));
    next_.setBounds(area.removeFromRight(kArrowWidth));
    name_.setBounds(area);
    refresh();
}
} // namespace gg::ui
