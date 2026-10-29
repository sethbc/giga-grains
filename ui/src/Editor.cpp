// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/Editor.h"

#include <algorithm>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>
#include <sst/jucegui/components/BaseStyles.h>
#include <sst/jucegui/style/JUCELookAndFeelAdapter.h>
#include <sst/jucegui/style/StyleAndSettingsConsumer.h>

#include "GrainCanvas.h"
#include "LoaderStrip.h"
#include "MacroRow.h"
#include "Theme.h"
#include "gg/core/Version.h"

namespace gg::ui
{
/// The Perform view (sub-project 5's spec §6.1): a header (the product name, the preset, Perform
/// selected and Edit greyed out until M2, and the frame time at the right), the instrument's
/// sample loader strip, the grain canvas across the full width, and the macro row. It starts at
/// kEditorWidth x kEditorHeight and lays out at any size, kEditorMinWidth x kEditorMinHeight
/// being the smallest the plugin asks a host for.
///
/// Files dropped anywhere on the editor go to the loader strip. The editor is a drop target in
/// the FX as well, but takes no files there. Its timer is a public base so tests can drive it,
/// as they drive drops: at 30 Hz it polls the sample's status, follows the macros' base values
/// and shows the frame time. The canvas repaints on every display refresh by itself.
///
/// It is the sst-jucegui style root: the dark style sheet set on it reaches every control, and
/// its look and feel (sst-jucegui's adapter, for menus) every JUCE widget.
class Editor final : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     public juce::Timer,
                     public sst::jucegui::style::StyleConsumer
{
  public:
    explicit Editor(const EditorContext &context)
        : sst::jucegui::style::StyleConsumer(
              sst::jucegui::components::base_styles::Base::styleClass),
          publishing_(context.publishing), lookAndFeel_(this), canvas_(context), macros_(context)
    {
        setOpaque(true);
        const juce::String product = juce::String::fromUTF8(
            context.productName.data(), static_cast<int>(context.productName.size()));
        setTitle(product);

        setUpLabel(product_, "header.product", product, juce::FontOptions(18.0f, juce::Font::bold),
                   theme::kText);
        setUpLabel(preset_, "header.preset", "Init", juce::FontOptions(15.0f), theme::kDimText);
        product_.setTooltip(
            "version " +
            juce::String::fromUTF8(gg::kVersion.data(), static_cast<int>(gg::kVersion.size())));
        preset_.setTooltip("The preset");
        setUpLabel(frameMs_, "header.frameMs", {}, juce::FontOptions(12.0f), theme::kDimText);
        frameMs_.setJustificationType(juce::Justification::centredRight);
        frameMs_.setTooltip("The canvas's frame time, the mean of the last 60 frames");

        setUpViewButton(perform_, "header.perform", "Perform");
        perform_.setToggleState(true, juce::dontSendNotification);
        setUpViewButton(edit_, "header.edit", "Edit");
        edit_.setEnabled(false); // until M2
        edit_.setTooltip("Edit arrives with M2");

        if (context.showSampleLoader)
        {
            strip_ = std::make_unique<LoaderStrip>(context);
            addAndMakeVisible(*strip_);
        }
        addAndMakeVisible(canvas_);
        addAndMakeVisible(macros_);

        const auto style = theme::darkStyle();
        lookAndFeel_.setStyle(style);
        setStyle(style);

        setSize(kEditorWidth, kEditorHeight);
        timerCallback();
        startTimerHz(30);
    }

    ~Editor() override
    {
        stopTimer();
        // The look and feel is a member, gone before juce::Component's destructor runs.
        setLookAndFeel(nullptr);
        // The plugin publishes its grains and snapshots only while an editor is open (spec
        // §6.4); the shim destroys the editor in guiDestroy, with no hook of its own.
        if (publishing_)
        {
            publishing_(false);
        }
    }

    void paint(juce::Graphics &g) override
    {
        g.fillAll(theme::kBackground);
        g.setColour(theme::kHeader);
        g.fillRect(getLocalBounds().removeFromTop(kHeaderHeight));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop(kHeaderHeight).reduced(kMargin, 6);
        product_.setBounds(header.removeFromLeft(150));
        preset_.setBounds(header.removeFromLeft(90));
        header.removeFromLeft(12);
        perform_.setBounds(header.removeFromLeft(76));
        header.removeFromLeft(4);
        edit_.setBounds(header.removeFromLeft(56));
        frameMs_.setBounds(header.removeFromRight(std::min(170, std::max(0, header.getWidth()))));

        area = area.reduced(kMargin, kGap);
        if (strip_)
        {
            strip_->setBounds(area.removeFromTop(28));
            area.removeFromTop(kGap);
        }
        macros_.setBounds(area.removeFromBottom(kMacroRowHeight));
        area.removeFromBottom(kGap);
        canvas_.setBounds(area);
    }

    // ---- juce::FileDragAndDropTarget: the strip's drop target, the whole editor ----

    bool isInterestedInFileDrag(const juce::StringArray &files) override
    {
        return strip_ != nullptr && !files.isEmpty();
    }

    void filesDropped(const juce::StringArray &files, int /*x*/, int /*y*/) override
    {
        if (isInterestedInFileDrag(files))
        {
            strip_->filesDropped(files);
        }
    }

    // 30 Hz: the sample's status, the macros' base values and the frame-time readout.
    // Label::setText does nothing when the text is unchanged.
    void timerCallback() override
    {
        if (strip_)
        {
            strip_->refresh();
        }
        macros_.refresh();
        frameMs_.setText("frame ms (avg 60): " + juce::String(canvas_.averageFrameMs(), 2),
                         juce::dontSendNotification);
    }

  private:
    static constexpr int kHeaderHeight = 40;
    static constexpr int kMacroRowHeight = 104;
    static constexpr int kMargin = 12;
    static constexpr int kGap = 6;

    void setUpLabel(juce::Label &label, const char *id, const juce::String &text,
                    const juce::FontOptions &font, juce::Colour colour)
    {
        label.setComponentID(id);
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(font));
        label.setColour(juce::Label::textColourId, colour);
        label.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(label);
    }

    void setUpViewButton(juce::TextButton &button, const char *id, const juce::String &text)
    {
        button.setComponentID(id);
        button.setButtonText(text);
        button.setClickingTogglesState(false); // one view in M1: Perform stays selected
        button.setColour(juce::TextButton::buttonColourId, theme::kBackground);
        button.setColour(juce::TextButton::buttonOnColourId, theme::kAccent.withAlpha(0.35f));
        button.setColour(juce::TextButton::textColourOffId, theme::kDimText);
        button.setColour(juce::TextButton::textColourOnId, theme::kText);
        button.setColour(juce::ComboBox::outlineColourId, theme::kOutline);
        addAndMakeVisible(button);
    }

    const std::function<void(bool)> publishing_; // this editor's own (EditorContext)
    sst::jucegui::style::LookAndFeelManager lookAndFeel_;

    juce::Label product_, preset_, frameMs_;
    juce::TextButton perform_, edit_;
    std::unique_ptr<LoaderStrip> strip_; // the instrument's only
    GrainCanvas canvas_;
    MacroRow macros_;
    juce::TooltipWindow tooltips_{this, 600};
};

std::unique_ptr<juce::Component> makeEditor(const EditorContext &context)
{
    return std::make_unique<Editor>(context);
}
} // namespace gg::ui
