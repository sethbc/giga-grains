// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_HEADER_H
#define GG_UI_HEADER_H

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>
#include <sst/jucegui/components/GlyphButton.h>
#include <sst/jucegui/components/TextPushButton.h>

#include "../Palette.h"
#include "../PresetStrip.h"
#include "gg/ui/Editor.h"

namespace gg::ui
{
class UndoStack;

/// The editor's header (M2 sub-project 6's spec §6.2; the close's row 331), 40 px over the views,
/// left to right: the product's name ("header.product"; a click opens the About box), the preset
/// strip ("header.preset"), the view buttons (a radio group, "header.perform", "header.edit",
/// "header.mod", "header.fx"), the frame time ("header.frameMs") in the middle, and at the right
/// undo and redo ("header.undo", "header.redo"; tooltips naming the entry) and the theme switch
/// ("header.theme").
///
/// It holds no state of its own: the editor sets what it shows (setView, setFrameMs, setHistory,
/// applyPalette) and takes its clicks through the callbacks. The notice, which the editor shows
/// in the header's middle (middle()) or, where that is too narrow, in a band under the header over
/// the view, is the editor's child, not the header's, and so are the preset browser and Mutate
/// overlays the preset strip opens. The keyboard shortcuts stay the editor's (Editor::keyPressed).
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class Header final : public juce::Component
{
  public:
    static constexpr int kHeight = 40;

    explicit Header(const EditorContext &context);

    std::function<void()> onAbout;                    ///< the product's name was clicked
    std::function<void(View)> onSelectView;           ///< a view button was clicked
    std::function<void(bool redo)> onUndoRedo;        ///< undo (false) or redo (true) was clicked
    std::function<void()> onTheme;                    ///< the theme switch was clicked
    std::function<void()> onPresetName;               ///< the preset strip's name was clicked
    std::function<void()> onPresetPrev, onPresetNext; ///< its `‹` and `›`

    /// The view buttons show `view` as selected.
    void setView(View view);
    /// The frame time, `ms`, in the form the label's width allows.
    void setFrameMs(double ms);
    /// Hides the frame time (the notice shows in its place) or shows it again.
    void showFrameMs(bool shown);
    /// Undo's and redo's enablement and tooltips from `stack` (null: nothing to do).
    void setHistory(const UndoStack *stack);
    /// The preset strip's name and edited mark as they are now, and whether the browser is open.
    void refreshPreset(bool browserOpen);
    /// Gives the keyboard focus to the preset strip's name (the browser closed).
    void focusPresetName();
    /// The header's own colors from `palette`, and the theme switch's label for `theme`.
    void applyPalette(const Palette &palette, ThemeChoice theme);

    /// The header between the view buttons and undo, in the header's coordinates: where the
    /// frame time is, and the notice when it fits.
    juce::Rectangle<int> middle() const noexcept { return middle_; }

    void paint(juce::Graphics &g) override;
    void resized() override;

  private:
    /// The product's name: a click opens the About box.
    struct ProductLabel final : juce::Label
    {
        std::function<void()> onClick;
        void mouseUp(const juce::MouseEvent &e) override;
    };

    /// Undo or redo: sst-jucegui's glyph button with a curved arrow (sst-jucegui has no undo
    /// glyph), and a tooltip naming what it would apply.
    struct HistoryButton final : sst::jucegui::components::GlyphButton, juce::SettableTooltipClient
    {
        explicit HistoryButton(bool redo);
        void paint(juce::Graphics &g) override;
        const bool redo_;
    };

    struct ThemeButton final : sst::jucegui::components::TextPushButton, juce::SettableTooltipClient
    {
    };

    void setUpLabel(juce::Label &label, const char *id, const juce::String &text,
                    const juce::FontOptions &font);

    ProductLabel product_;
    PresetStrip preset_;
    juce::Label frameMs_;
    std::array<juce::TextButton, 4> viewButtons_;
    HistoryButton undo_, redo_;
    ThemeButton theme_;

    juce::Rectangle<int> middle_; // the header between the view buttons and undo
};
} // namespace gg::ui

#endif // GG_UI_HEADER_H
