// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "LoaderStrip.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Palette.h"

namespace gg::ui
{
namespace
{
/// The formats the loader decodes (spec §6.4), for "Load…" and "Relink…", and the same as
/// extensions for a drop (File::hasFileExtension ignores case).
constexpr const char *kSampleFilePatterns = "*.wav;*.aif;*.aiff;*.aifc;*.flac;*.mp3";
constexpr const char *kSampleFileExtensions = "wav;aif;aiff;aifc;flac;mp3";

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

LoaderStrip::LoaderStrip(const EditorContext &context) : context_(context)
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
    setUpButton(folderButton_, "sampleFolder", withEllipsis("Sample folder"),
                [this] { chooseSampleFolder(); });
    relinkButton_.setVisible(false); // shown while the sample can be relinked

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
    refresh();
}

LoaderStrip::~LoaderStrip() = default;

void LoaderStrip::applyPalette()
{
    const Palette &palette = paletteOf(*this);
    name_.setColour(juce::Label::textColourId, palette.text);
    status_.setColour(juce::Label::textColourId, palette.dimText);
}

void LoaderStrip::parentHierarchyChanged() { applyPalette(); }

void LoaderStrip::lookAndFeelChanged() { applyPalette(); }

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
    browse({title, startFolder(), false, kSampleFilePatterns}, [this,
                                                                relink](const juce::File &file) {
        if (file != juce::File())
        {
            request(relink ? context_.relinkSample : context_.loadSample, file.getFullPathName());
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

void LoaderStrip::request(const std::function<void(const std::string &)> &action,
                          const juce::String &path)
{
    if (juce::File::isAbsolutePath(path))
    {
        lastFolder_ = juce::File(path).getParentDirectory();
    }
    notice_.clear();
    if (action)
    {
        action(path.toStdString());
    }
    refresh(); // the new status at once
}

void LoaderStrip::filesDropped(const juce::StringArray &files)
{
    if (files.isEmpty())
    {
        return;
    }
    // Not parsed as a juce::File, which needs an absolute path: the loader takes it as given.
    const juce::File file = juce::File::createFileWithoutCheckingPath(files[0]);
    if (!file.hasFileExtension(kSampleFileExtensions))
    {
        // The decoder would refuse it: say so here, and leave the sample alone.
        setNotice("can't load " + file.getFileName() + ": not a WAV, AIFF, FLAC or MP3 file");
        refresh();
        return;
    }
    request(context_.loadSample, files[0]);
}

void LoaderStrip::setNotice(const juce::String &text)
{
    notice_ = text;
    noticeStatus_ = statusText_;
}

void LoaderStrip::refresh()
{
    const SampleStatusView status =
        context_.sampleStatus ? context_.sampleStatus() : SampleStatusView{};
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
    loadButton_.setBounds(strip.removeFromLeft(72));
    strip.removeFromLeft(8);
    folderButton_.setBounds(strip.removeFromRight(124));
    strip.removeFromRight(6);
    relinkButton_.setBounds(strip.removeFromRight(72));
    strip.removeFromRight(6);
    name_.setBounds(strip.removeFromLeft(std::min(200, strip.getWidth() / 3)));
    status_.setBounds(strip);
    showStatus(status_.getText()); // the tooltip follows the room
}
} // namespace gg::ui
