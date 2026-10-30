// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_PRESETSTRIP_H
#define GG_UI_PRESETSTRIP_H

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/ui/Editor.h"

namespace gg::ui
{
/// The header's preset strip (M2 sub-project 5's spec §6.8; component ID "header.preset"): `‹`
/// ("preset.prev"), the preset's name ("preset.name"), `›` ("preset.next") and "Mutate"
/// ("preset.mutate", spec §6.9: it calls onMutate, which shows or hides the Mutate panel). The
/// first four take kNameWidth together; the strip is kWidth wide. The name is cut in
/// the middle to fit, with " •" after it while the preset is edited; clicking it calls onName
/// (the editor shows or hides the browser); `‹` and `›` call onPrev and onNext (the browser moves
/// through its filtered list and loads).
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class PresetStrip final : public juce::Component, public juce::SettableTooltipClient
{
  public:
    explicit PresetStrip(const EditorContext &context);

    /// The strip's width: the arrows and the name, then Mutate.
    static constexpr int kNameWidth = 90;
    static constexpr int kMutateWidth = 56;
    static constexpr int kWidth = kNameWidth + 4 + kMutateWidth;

    std::function<void()> onName, onPrev, onNext, onMutate;

    /// Shows the preset's name and edited mark as they are now (the editor's 30 Hz timer).
    void refresh();
    /// Whether the name button shows the browser as open.
    void setBrowserOpen(bool open);
    /// Whether the Mutate button shows the panel as open.
    void setMutateOpen(bool open);

    void resized() override;

  private:
    const EditorContext &context_;
    juce::TextButton prev_, name_, next_, mutate_;
    juce::String fullName_; // the name last shown, uncut, with its mark
    int shownWidth_{-1};    // the name button's width when it was cut
};

/// `text` cut in the middle with an ellipsis until it is at most `width` pixels wide in `font`
/// (`text` itself if it fits; at least the ellipsis alone).
juce::String elideMiddle(const juce::String &text, const juce::Font &font, float width);
} // namespace gg::ui

#endif // GG_UI_PRESETSTRIP_H
