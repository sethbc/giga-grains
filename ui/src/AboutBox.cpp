// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "AboutBox.h"

#include <algorithm>

#include "Palette.h"
#include "gg/core/Version.h"
#include "gg/ui/Notices.h"

namespace gg::ui
{
namespace
{
juce::String fromUtf8(std::string_view text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

bool isMarkdown(std::string_view name)
{
    return name.size() > 3 && name.substr(name.size() - 3) == ".md";
}

constexpr int kRowHeight = 22;
constexpr int kMargin = 12;
} // namespace

/// The list's model: the notices' names, in the installer's order.
class AboutBox::NoticeList final : public juce::ListBoxModel
{
  public:
    explicit NoticeList(AboutBox &owner) : owner_(owner) {}

    int getNumRows() override { return static_cast<int>(noticeTexts().size()); }

    void paintListBoxItem(int row, juce::Graphics &g, int width, int height, bool selected) override
    {
        const Palette &palette = paletteOf(owner_);
        if (selected)
        {
            g.fillAll(palette.accent.withAlpha(0.35f));
        }
        g.setColour(selected ? palette.text : palette.dimText);
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(owner_.rowName(row), 6, 0, width - 6, height, juce::Justification::centredLeft,
                   true);
    }

    void selectedRowsChanged(int lastRowSelected) override
    {
        if (lastRowSelected >= 0)
        {
            owner_.selectNotice(lastRowSelected);
        }
    }

  private:
    AboutBox &owner_;
};

AboutBox::AboutBox(const std::string &productName, const std::string &formatName)
    : repository_("https://github.com/sethbc/giga-grains",
                  juce::URL("https://github.com/sethbc/giga-grains")),
      listModel_(std::make_unique<NoticeList>(*this)), list_("about.list", listModel_.get())
{
    processKeys = true;
    consumesAllKeys = true; // the host's key bindings stay out from under the modal
    setComponentID("about");
    setTitle("About");

    const auto setUpLabel = [this](juce::Label &label, const char *id, const juce::String &text,
                                   float size, bool bold) {
        label.setComponentID(id);
        label.setText(text, juce::dontSendNotification);
        label.setFont(
            juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)));
        label.setMinimumHorizontalScale(1.0f);
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
    };
    setUpLabel(product_, "about.product", fromUtf8(productName), 20.0f, true);
    setUpLabel(version_, "about.version", "version " + fromUtf8(gg::kVersion), 14.0f, false);
    setUpLabel(format_, "about.format",
               formatName.empty() ? juce::String() : "format: " + fromUtf8(formatName), 14.0f,
               false);
    setUpLabel(license_, "about.license",
               "Free software, licensed under the GNU Affero General Public License, version 3 "
               "or (at your option) any later version.",
               13.0f, false);
    setUpLabel(source_, "about.source",
               "The corresponding source code of this version is at the repository below.", 13.0f,
               false);

    repository_.setComponentID("about.repository");
    repository_.setButtonText("https://github.com/sethbc/giga-grains");
    repository_.setFont(juce::Font(juce::FontOptions(13.0f)), false);
    addAndMakeVisible(repository_);

    list_.setComponentID("about.list");
    list_.setRowHeight(kRowHeight);
    addAndMakeVisible(list_);

    markdownHolder_.setComponentID("about.text");
    markdownHolder_.setViewedComponent(&markdown_, false);
    markdownHolder_.setScrollBarsShown(true, false);
    addAndMakeVisible(markdownHolder_);

    plainView_.setComponentID("about.text.plain");
    plainView_.setMultiLine(true, false);
    plainView_.setReadOnly(true);
    plainView_.setCaretVisible(false);
    plainView_.setScrollbarsShown(true);
    plainView_.setFont(juce::Font(
        juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    addChildComponent(plainView_);

    plain_.setComponentID("about.plain");
    plain_.setButtonText("Plain text");
    plain_.onClick = [this] { setPlainText(plain_.getToggleState()); };
    addAndMakeVisible(plain_);

    close_.setComponentID("about.close");
    close_.setButtonText("Close");
    close_.onClick = [this] { close(); };
    addAndMakeVisible(close_);

    applyPalette();
    list_.selectRow(0, true, true);
    showSelected();
}

AboutBox::~AboutBox() = default;

juce::String AboutBox::rowName(int row) const
{
    const auto notices = noticeTexts();
    if (row < 0 || row >= static_cast<int>(notices.size()))
    {
        return {};
    }
    return fromUtf8(notices[static_cast<std::size_t>(row)].name);
}

void AboutBox::selectNotice(int row)
{
    const int count = static_cast<int>(noticeTexts().size());
    if (row < 0 || row >= count)
    {
        return;
    }
    selected_ = row;
    if (list_.getSelectedRow() != row)
    {
        list_.selectRow(row, true, true);
    }
    showSelected();
}

void AboutBox::setPlainText(bool plain)
{
    plainText_ = plain;
    plain_.setToggleState(plain, juce::dontSendNotification);
    showSelected();
}

juce::String AboutBox::shownText() const
{
    return plainView_.isVisible() ? plainView_.getText() : juce::String(markdown_.getRawMarkdown());
}

void AboutBox::showSelected()
{
    const auto notices = noticeTexts();
    if (selected_ < 0 || selected_ >= static_cast<int>(notices.size()))
    {
        return;
    }
    const NoticeText &notice = notices[static_cast<std::size_t>(selected_)];
    const bool asMarkdown = isMarkdown(notice.name) && !plainText_;
    if (asMarkdown)
    {
        markdown_.setMarkdown(std::string(notice.text));
        plainView_.setVisible(false);
        markdownHolder_.setVisible(true);
        resized();
    }
    else
    {
        plainView_.setText(fromUtf8(notice.text), false);
        plainView_.moveCaretToTop(false);
        markdownHolder_.setVisible(false);
        plainView_.setVisible(true);
    }
}

void AboutBox::applyPalette()
{
    const Palette &palette = paletteOf(*this);
    for (juce::Label *label : {&product_, &version_, &format_, &license_, &source_})
    {
        label->setColour(juce::Label::textColourId,
                         label == &product_ ? palette.text : palette.dimText);
    }
    repository_.setColour(juce::HyperlinkButton::textColourId, palette.accent);
    list_.setColour(juce::ListBox::backgroundColourId, palette.lane);
    list_.setColour(juce::ListBox::outlineColourId, palette.outline);
    plainView_.setColour(juce::TextEditor::backgroundColourId, palette.lane);
    plainView_.setColour(juce::TextEditor::textColourId, palette.text);
    plainView_.setColour(juce::TextEditor::outlineColourId, palette.outline);
    plain_.setColour(juce::ToggleButton::textColourId, palette.text);
    close_.setColour(juce::TextButton::buttonColourId, palette.background);
    close_.setColour(juce::TextButton::textColourOffId, palette.text);
    list_.repaint();
}

void AboutBox::parentHierarchyChanged() { applyPalette(); }

void AboutBox::lookAndFeelChanged() { applyPalette(); }

void AboutBox::visibilityChanged()
{
    setWantsKeyboardFocus(processKeys);
    if (processKeys && isShowing())
    {
        grabKeyboardFocus();
    }
}

void AboutBox::mouseDown(const juce::MouseEvent &e)
{
    if (!getContentArea().contains(e.getPosition()))
    {
        close();
    }
}

juce::Point<int> AboutBox::innerContentSize() { return {760, 520}; }

void AboutBox::close()
{
    setVisible(false);
    if (onClose)
    {
        onClose();
    }
}

void AboutBox::onEscape() { close(); }

void AboutBox::resized()
{
    auto area = getContentArea().reduced(kMargin);
    product_.setBounds(area.removeFromTop(28));
    version_.setBounds(area.removeFromTop(20));
    format_.setBounds(area.removeFromTop(20));
    area.removeFromTop(4);
    license_.setBounds(area.removeFromTop(20));
    source_.setBounds(area.removeFromTop(20));
    repository_.setBounds(area.removeFromTop(22).removeFromLeft(320));
    area.removeFromTop(kMargin);

    auto footer = area.removeFromBottom(28);
    close_.setBounds(footer.removeFromRight(80));
    plain_.setBounds(footer.removeFromLeft(120));
    area.removeFromBottom(6);

    list_.setBounds(area.removeFromLeft(std::min(220, area.getWidth() / 3)));
    area.removeFromLeft(kMargin);
    markdownHolder_.setBounds(area);
    plainView_.setBounds(area);
    constexpr int kScrollBarRoom = 12;
    const int width = std::max(50, area.getWidth() - kScrollBarRoom);
    markdown_.setSize(width, markdown_.getHeightForWidth(width));
}
} // namespace gg::ui
