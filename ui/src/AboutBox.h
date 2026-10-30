// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_ABOUTBOX_H
#define GG_UI_ABOUTBOX_H

#include <functional>
#include <memory>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>
#include <sst/jucegui/markdown/MarkdownComponent.h>
#include <sst/jucegui/screens/ModalBase.h>

namespace gg::ui
{
/// The About box (M2 sub-project 6's spec §6.10, carry-over row 221): the product, its version
/// and format, the AGPL line, the repository's link and the line on the corresponding source,
/// then the shipped notices and licenses (noticeTexts()) as a list of their names and a viewer.
/// The Markdown ones render as Markdown; "Plain text" shows them raw, and the others are always
/// plain, in a monospaced font. Only a notice's name is ever shown, never its source path.
///
/// A modal over its parent: size it to cover the editor and make it visible. Escape, Close and a
/// press outside the panel hide it and call onClose. Component IDs: about.product, about.version,
/// about.format, about.license, about.repository, about.source, about.list, about.text (the
/// viewer's holder), about.plain, about.close.
class AboutBox final : public sst::jucegui::screens::ModalBase
{
  public:
    /// `formatName` is "CLAP", "VST3", "AU" or "Standalone", or empty when the plugin can't say.
    AboutBox(const std::string &productName, const std::string &formatName);
    ~AboutBox() override;

    /// Called after Escape or Close has hidden the box.
    std::function<void()> onClose;

    /// The notices' list, for tests and the list's model: the name of row `row` ("" if none).
    juce::String rowName(int row) const;
    /// Shows notice `row` (as selecting its row does).
    void selectNotice(int row);
    /// The "Plain text" toggle.
    void setPlainText(bool plain);
    /// The text the viewer shows now: the Markdown source or the plain text.
    juce::String shownText() const;

    juce::Point<int> innerContentSize() override;
    void onEscape() override;
    void resized() override;
    void parentHierarchyChanged() override;
    /// The editor's theme changed: the palette again.
    void lookAndFeelChanged() override;
    /// A press outside the panel closes the box, as Escape does.
    void mouseDown(const juce::MouseEvent &e) override;
    /// As ModalBase's, but the focus is taken only once the box is on screen (JUCE asserts
    /// otherwise; an editor built headless, as the tests build it, never is).
    void visibilityChanged() override;

  private:
    class NoticeList;

    void close();
    void showSelected();
    void applyPalette();

    juce::Label product_, version_, format_, license_, source_;
    juce::HyperlinkButton repository_;
    std::unique_ptr<NoticeList> listModel_;
    juce::ListBox list_;
    juce::ToggleButton plain_;
    juce::TextButton close_;
    // Both viewers sit in the same area; one is visible. The Markdown one in a Viewport, whose
    // width it tracks.
    juce::Viewport markdownHolder_;
    sst::jucegui::markdown::MarkdownComponent markdown_;
    juce::TextEditor plainView_;
    int selected_{0};
    bool plainText_{false};
};
} // namespace gg::ui

#endif // GG_UI_ABOUTBOX_H
