// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "LoaderStrip.h"

#include "SampleFiles.h"
#include "NoticeSink.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "Palette.h"
#include "gg/ui/MemoryText.h"

namespace gg::ui
{
namespace
{
/// The formats the loader decodes (spec §6.4), for "Load…" and "Relink…", and the same as
/// extensions for a drop (File::hasFileExtension ignores case).
constexpr const char *kSampleFilePatterns = kSampleFileChooserPatterns;

constexpr float kFontHeight = 14.0f;

juce::String fromUtf8(const std::string &text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

/// `text` with an ellipsis (U+2026) after it, as a button that opens a dialog has.
juce::String withEllipsis(const char *text)
{
    return juce::String(text) + juce::String::charToString(0x2026);
}

/// The strip's status text (spec §6.5): the state, and the loader's message where it has one.
juce::String statusText(const SampleStatusView &status)
{
    using State = SampleStatusView::State;
    const juce::String message = fromUtf8(status.message);
    const auto withNote = [&message](juce::String text) {
        if (message.isNotEmpty())
        {
            text << " (" << message << ")";
        }
        return text;
    };
    switch (status.state)
    {
    case State::None:
        return withNote("no sample");
    case State::Loading:
        return withNote(withEllipsis("loading"));
    case State::Loaded:
        return withNote("loaded: " + juce::String(status.seconds, 2) + " s, " +
                        juce::String(status.channels) + " ch, " +
                        juce::String(juce::roundToInt(status.fileRate)) + " Hz");
    case State::Missing:
    case State::Changed:
        return message.isEmpty() ? juce::String("missing") : "missing: " + message;
    case State::Error:
        return message.isEmpty() ? juce::String("can't load the sample") : message;
    }
    return {};
}

/// The folder of `path`, a UTF-8 absolute path; none if it isn't one.
juce::File folderOf(const std::string &utf8Folder)
{
    const juce::String path = fromUtf8(utf8Folder);
    return juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File();
}
} // namespace

LoaderStrip::LoaderStrip(const EditorContext &context, uint8_t layer, Compact compact)
    : context_(context), layer_(std::min<uint8_t>(layer, gg::core::kLayers - 1)),
      compact_(compact == Compact::Yes)
{
    setComponentID("sampleStrip");
    setTitle("Sample");
    const auto setUpButton = [this](juce::TextButton &button, const char *id,
                                    const juce::String &text, std::function<void()> onClick) {
        button.setButtonText(text);
        button.setComponentID(id);
        button.onClick = std::move(onClick);
        addAndMakeVisible(button);
    };
    setUpButton(loadButton_, "sampleLoad", withEllipsis("Load"),
                [this] { chooseFile("Load a sample", false); });
    setUpButton(relinkButton_, "sampleRelink", withEllipsis("Relink"),
                [this] { chooseFile("Find the missing sample", true); });
    relinkButton_.setVisible(false); // shown while the sample can be relinked
    if (!compact_)
    {
        setUpButton(folderButton_, "sampleFolder", withEllipsis("Sample folder"),
                    [this] { chooseSampleFolder(); });
        setUpButton(collectButton_, "loader.collect", withEllipsis("Collect"),
                    [this] { chooseCollectFolder(); });
        collectButton_.setTooltip("Copy the samples into a folder, to move the session");
        memory_.setComponentID("loader.memory");
        memory_.setTooltip("The memory the samples hold, and its cap");
        memory_.setFont(juce::Font(juce::FontOptions(kFontHeight)));
        memory_.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(memory_);
        capMenu_.setComponentID("loader.cap");
        capMenu_.setTitle("Memory cap");
        capMenu_.setTooltip("The cap on the memory the samples may hold");
        capMenu_.onChange = [this] {
            const int mib = capMenu_.getSelectedId();
            if (mib > 0 && context_.setMemoryCapMiB)
            {
                context_.setMemoryCapMiB(static_cast<uint32_t>(mib));
            }
            pollMemory();
        };
        addAndMakeVisible(capMenu_);
    }

    letter_.setComponentID("loader.letter");
    letter_.setFont(juce::Font(juce::FontOptions(kFontHeight, juce::Font::bold)));
    letter_.setJustificationType(juce::Justification::centred);
    letter_.setText(juce::String::charToString(static_cast<juce::juce_wchar>('A' + layer_)),
                    juce::dontSendNotification);
    addAndMakeVisible(letter_);
    for (juce::Label *label : {&name_, &status_})
    {
        label->setFont(juce::Font(juce::FontOptions(kFontHeight)));
        label->setMinimumHorizontalScale(1.0f); // cut with an ellipsis, never squashed
        addAndMakeVisible(*label);
    }
    name_.setComponentID("sampleName");
    status_.setComponentID("sampleStatus");
    applyPalette();

    browse = [this](const Browse &b, std::function<void(const juce::File &)> done) {
        chooser_ =
            std::make_unique<juce::FileChooser>(b.title, b.initial, b.folders ? "" : b.patterns);
        const int flags = juce::FileBrowserComponent::openMode |
                          (b.folders ? juce::FileBrowserComponent::canSelectDirectories
                                     : juce::FileBrowserComponent::canSelectFiles);
        chooser_->launchAsync(flags, [done = std::move(done)](const juce::FileChooser &chooser) {
            done(chooser.getResult());
        });
    };
    pollMemory();
    refresh();
}

LoaderStrip::~LoaderStrip() = default;

void LoaderStrip::applyPalette()
{
    const Palette &palette = paletteOf(*this);
    name_.setColour(juce::Label::textColourId, palette.text);
    status_.setColour(juce::Label::textColourId, palette.dimText);
    letter_.setColour(juce::Label::textColourId, palette.layers[layer_]);
    for (juce::TextButton *button : {&loadButton_, &relinkButton_, &folderButton_, &collectButton_})
    {
        button->setColour(juce::TextButton::buttonColourId, palette.header);
        button->setColour(juce::TextButton::buttonOnColourId, palette.accent.withAlpha(0.35f));
        button->setColour(juce::TextButton::textColourOffId, palette.text);
        button->setColour(juce::TextButton::textColourOnId, palette.text);
        button->setColour(juce::ComboBox::outlineColourId, palette.outline);
    }
    capMenu_.setColour(juce::ComboBox::backgroundColourId, palette.header);
    capMenu_.setColour(juce::ComboBox::textColourId, palette.text);
    capMenu_.setColour(juce::ComboBox::arrowColourId, palette.text);
    capMenu_.setColour(juce::ComboBox::outlineColourId, palette.outline);
    applyMemory();
}

void LoaderStrip::parentHierarchyChanged() { applyPalette(); }

void LoaderStrip::lookAndFeelChanged() { applyPalette(); }

void LoaderStrip::setLayer(uint8_t layer)
{
    layer = std::min<uint8_t>(layer, gg::core::kLayers - 1);
    if (layer == layer_)
    {
        return;
    }
    layer_ = layer;
    letter_.setText(juce::String::charToString(static_cast<juce::juce_wchar>('A' + layer_)),
                    juce::dontSendNotification);
    letter_.setColour(juce::Label::textColourId, paletteOf(*this).layers[layer_]);
    notice_.clear(); // it was about the other layer's sample
    refresh();
}

void LoaderStrip::pollMemory()
{
    if (compact_)
    {
        return;
    }
    memoryView_ = context_.memory ? context_.memory() : MemoryView{};
    applyMemory();
}

void LoaderStrip::fillCapMenu(uint32_t capMiB)
{
    // The choices ascending, and a cap that is none of them (a state's) in its place.
    std::vector<uint32_t> items(memoryCapChoicesMiB().begin(), memoryCapChoicesMiB().end());
    if (capMiB != 0 && std::find(items.begin(), items.end(), capMiB) == items.end())
    {
        items.insert(std::upper_bound(items.begin(), items.end(), capMiB), capMiB);
    }
    capMenu_.clear(juce::dontSendNotification);
    for (const uint32_t mib : items)
    {
        capMenu_.addItem(fromUtf8(formatMemorySize(mib)), static_cast<int>(mib));
    }
    capListed_ = capMiB;
}

void LoaderStrip::applyMemory()
{
    if (compact_)
    {
        return;
    }
    if (!context_.memory)
    {
        memory_.setText({}, juce::dontSendNotification);
        capMenu_.setEnabled(false);
        return;
    }
    capMenu_.setEnabled(true);
    if (capListed_ != memoryView_.capMiB || capMenu_.getNumItems() == 0)
    {
        fillCapMenu(memoryView_.capMiB);
    }
    capMenu_.setSelectedId(static_cast<int>(memoryView_.capMiB), juce::dontSendNotification);
    memory_.setText(fromUtf8(formatMemory(memoryView_.usedBytes, memoryView_.capMiB)),
                    juce::dontSendNotification);
    overCap_ =
        memoryView_.usedBytes > static_cast<uint64_t>(memoryView_.capMiB) * 1024ull * 1024ull;
    const Palette &palette = paletteOf(*this);
    memory_.setColour(juce::Label::textColourId, overCap_ ? palette.notice : palette.dimText);
}

juce::File LoaderStrip::startFolder() const
{
    if (lastFolder_ != juce::File())
    {
        return lastFolder_;
    }
    return context_.lastLoadFolder ? folderOf(context_.lastLoadFolder()) : juce::File();
}

void LoaderStrip::chooseFile(const juce::String &title, bool relink)
{
    browse({title, startFolder(), false, kSampleFilePatterns},
           [this, relink](const juce::File &file) {
               if (file != juce::File())
               {
                   request(relink, file.getFullPathName());
               }
           });
}

void LoaderStrip::chooseSampleFolder()
{
    browse({"Choose the sample folder", startFolder(), true, {}}, [this](const juce::File &folder) {
        if (folder == juce::File())
        {
            return;
        }
        SampleFolderResult result;
        if (context_.setSampleFolder)
        {
            result = context_.setSampleFolder(folder.getFullPathName().toStdString());
        }
        else
        {
            result.message = "can't save the sample folder";
        }
        // The folder saved or not: either way the user is told (carry-over row 198).
        setNotice(result.saved ? "sample folder: " + folder.getFullPathName()
                               : (result.message.empty() ? juce::String("can't save the sample "
                                                                        "folder")
                                                         : fromUtf8(result.message)));
        refresh();
    });
}

void LoaderStrip::chooseCollectFolder()
{
    browse({"Collect the samples into a folder", startFolder(), true, {}},
           [this](const juce::File &folder) {
               if (folder == juce::File() || !context_.collectSamples)
               {
                   return;
               }
               const std::string summary =
                   context_.collectSamples(folder.getFullPathName().toStdString());
               postNotice(*this, context_, summary);
               refresh();
           });
}

void LoaderStrip::request(bool relink, const juce::String &path)
{
    if (juce::File::isAbsolutePath(path))
    {
        lastFolder_ = juce::File(path).getParentDirectory();
    }
    notice_.clear();
    const std::string utf8 = path.toStdString();
    if (relink)
    {
        if (context_.relinkSampleFor)
        {
            context_.relinkSampleFor(layer_, utf8);
        }
    }
    else if (context_.loadSampleFor && !context_.loadSampleFor(layer_, utf8))
    {
        setNotice("can't load " + juce::File::createFileWithoutCheckingPath(path).getFileName() +
                  ": the layer takes no sample");
    }
    refresh(); // the new status at once
}

void LoaderStrip::filesDropped(const juce::StringArray &files)
{
    if (files.isEmpty())
    {
        return;
    }
    if (!isSampleFilePath(files[0]))
    {
        // The decoder would refuse it: say so here, and leave the sample alone.
        setNotice(notSampleFileNotice(files[0]));
        refresh();
        return;
    }
    request(false, files[0]);
}

void LoaderStrip::setNotice(const juce::String &text)
{
    notice_ = text;
    noticeStatus_ = statusText_;
}

void LoaderStrip::refresh()
{
    if (!compact_ && ++ticks_ % 10 == 0)
    {
        pollMemory();
    }
    const SampleStatusView status =
        context_.sampleStatusFor ? context_.sampleStatusFor(layer_) : SampleStatusView{};
    name_.setText(fromUtf8(status.fileName), juce::dontSendNotification);
    relinkButton_.setVisible(status.relinkable);
    statusText_ = statusText(status);
    if (notice_.isNotEmpty() && statusText_ != noticeStatus_)
    {
        notice_.clear(); // the sample's status moved on: show it
    }
    showStatus(notice_.isNotEmpty() ? notice_ : statusText_);
}

void LoaderStrip::showStatus(const juce::String &text)
{
    status_.setText(text, juce::dontSendNotification);
    // A status cut off by its label (an ellipsis at its end) shows whole in a tooltip.
    const int room = status_.getBorderSize().subtractedFrom(status_.getLocalBounds()).getWidth();
    float width = juce::GlyphArrangement::getStringWidth(status_.getFont(), text);
    if (!(width > 0.0f) && text.isNotEmpty())
    {
        // No font to measure with (a headless system without one): estimate.
        width = 0.6f * kFontHeight * static_cast<float>(text.length());
    }
    status_.setTooltip(width > static_cast<float>(room) ? text : juce::String());
}

void LoaderStrip::resized()
{
    auto strip = getLocalBounds();
    letter_.setBounds(strip.removeFromLeft(22));
    strip.removeFromLeft(4);
    loadButton_.setBounds(strip.removeFromLeft(72));
    strip.removeFromLeft(8);
    if (!compact_)
    {
        folderButton_.setBounds(strip.removeFromRight(124));
        strip.removeFromRight(6);
        collectButton_.setBounds(strip.removeFromRight(84));
        strip.removeFromRight(6);
        capMenu_.setBounds(strip.removeFromRight(84));
        strip.removeFromRight(6);
        memory_.setBounds(strip.removeFromRight(120));
        strip.removeFromRight(6);
    }
    relinkButton_.setBounds(strip.removeFromRight(72));
    strip.removeFromRight(6);
    name_.setBounds(strip.removeFromLeft(std::min(200, strip.getWidth() / 3)));
    status_.setBounds(strip);
    showStatus(status_.getText()); // the tooltip follows the room
}
} // namespace gg::ui
