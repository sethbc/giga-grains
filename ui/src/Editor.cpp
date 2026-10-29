// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "gg/ui/Editor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/core/Version.h"
#include "gg/ui/SplatRenderer.h"
#include "gg/ui/StressGrains.h"

namespace gg::ui
{
namespace
{
/// The grain counts the stress panel offers. 8,192 is the INTENT §16 budget point: at most
/// 3 ms of UI-thread time per frame with 8,000 visible grains.
constexpr std::array<uint32_t, 4> kGrainCounts{0, 4096, 8192, 16384};
constexpr int kDefaultGrainCountIndex = 2;
constexpr std::size_t kAveragedFrames = 60;

constexpr uint32_t kGrainTint = 0xFFC46B; // amber, 0xRRGGBB
const juce::Colour kBackground{0xff15171c};
const juce::Colour kText{0xffe8e8ec};
const juce::Colour kDimText{0xff9aa0ab};

/// The formats the loader decodes (spec §6.4), for "Load…" and "Relink…".
constexpr const char *kSampleFilePatterns = "*.wav;*.aif;*.aiff;*.aifc;*.flac;*.mp3";

/// `text` with an ellipsis (U+2026) after it, for a button that opens a dialog.
juce::String withEllipsis(const char *text)
{
    return juce::String(text) + juce::String::charToString(0x2026);
}

/// The loader strip's status text (spec §6.5).
juce::String statusText(const SampleStatusView &status)
{
    using State = SampleStatusView::State;
    const juce::String message =
        juce::String::fromUTF8(status.message.data(), static_cast<int>(status.message.size()));
    switch (status.state)
    {
    case State::None:
        return "no sample";
    case State::Loading:
        return withEllipsis("loading");
    case State::Loaded:
    {
        juce::String text = "loaded: " + juce::String(status.seconds, 2) + " s, " +
                            juce::String(status.channels) + " ch, " +
                            juce::String(juce::roundToInt(status.fileRate)) + " Hz";
        if (message.isNotEmpty())
        {
            text << " (" << message << ")";
        }
        return text;
    }
    case State::Missing:
    case State::Changed:
        return message.isEmpty() ? juce::String("missing") : "missing: " + message;
    case State::Error:
        return message;
    }
    return {};
}
} // namespace

/// The canvas stress panel's drawing surface: every display frame it splats the stress
/// workload's grains at physical-pixel resolution, resolves them and draws the image, and times
/// those three. (Generating the synthetic grains is left out of the time: the real canvas will
/// get its grains from the engine.)
class StressCanvas final : public juce::Component
{
  public:
    StressCanvas()
    {
        setOpaque(true);
        setTitle("Canvas stress");
    }

    void setGrainCount(uint32_t count)
    {
        splats_.resize(count);
        filled_ = 0; // a new average for the new count
        next_ = 0;
    }

    /// The mean frame time (splat, resolve and paint) over the last 60 frames, in ms; 0 before
    /// the first frame.
    double averageFrameMs() const noexcept
    {
        if (filled_ == 0)
        {
            return 0.0;
        }
        double sum = 0.0;
        for (std::size_t i = 0; i < filled_; ++i)
        {
            sum += frameMs_[i];
        }
        return sum / static_cast<double>(filled_);
    }

    /// The canvas size in physical pixels at the last frame.
    uint32_t physicalWidth() const noexcept { return splat_ ? splat_->width() : 0; }
    uint32_t physicalHeight() const noexcept { return splat_ ? splat_->height() : 0; }

    void paint(juce::Graphics &g) override
    {
        using Clock = std::chrono::steady_clock;

        // One accumulator pixel per physical pixel: the context knows the display's scale.
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto physical = [scale](int logical) {
            return static_cast<uint32_t>(
                std::max(1, juce::roundToInt(static_cast<float>(logical) * scale)));
        };
        const uint32_t width = physical(getWidth());
        const uint32_t height = physical(getHeight());
        if (splat_ == nullptr || splat_->width() != width || splat_->height() != height)
        {
            splat_ = std::make_unique<SplatRenderer>(width, height);
            image_ = juce::Image(juce::Image::ARGB, static_cast<int>(width),
                                 static_cast<int>(height), false);
        }

        stressGrains(frame_++, static_cast<uint32_t>(splats_.size()), width, height, scale,
                     splats_.data());

        const auto start = Clock::now();
        splat_->clear();
        for (const Splat &s : splats_)
        {
            splat_->add(s.x, s.y, s.radius, s.intensity);
        }
        {
            const juce::Image::BitmapData pixels(image_, juce::Image::BitmapData::writeOnly);
            jassert(pixels.pixelStride == 4);
            splat_->resolveToArgb(reinterpret_cast<uint32_t *>(pixels.data),
                                  static_cast<uint32_t>(pixels.lineStride / 4), kGrainTint);
        }
        // The image already has one pixel per physical pixel, so nothing needs filtering.
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.drawImage(image_, getLocalBounds().toFloat());

        frameMs_[next_] = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        next_ = (next_ + 1) % kAveragedFrames;
        filled_ = std::min(filled_ + 1, kAveragedFrames);
    }

  private:
    std::vector<Splat> splats_;
    uint64_t frame_{0};
    std::unique_ptr<SplatRenderer> splat_;
    juce::Image image_;

    std::array<double, kAveragedFrames> frameMs_{};
    std::size_t next_{0};
    std::size_t filled_{0};

    // Repaints on every display refresh while the canvas is on screen.
    juce::VBlankAttachment vblank_{this, [this] { repaint(); }};
};

/// The M0 editor: the product name and version, a MIDI note-on counter, the instrument's sample
/// loader strip, and the canvas stress panel. It is 900 x 500 to start with and lays out at any
/// size.
///
/// The strip (spec §6.5; sub-project 5 replaces it): "Load…", the file name and status,
/// "Relink…" while the sample is missing, changed or can't be read, and "Sample folder…". Files
/// dropped anywhere on the editor load too (the first one only). The editor is a drop target in the
/// FX as well, but takes no files there. Its timer is a public base so tests can drive it, as they
/// drive drops.
class Editor final : public juce::Component, public juce::FileDragAndDropTarget, public juce::Timer
{
  public:
    explicit Editor(const EditorContext &context)
        : noteOnCount_(context.noteOnCount), showSampleLoader_(context.showSampleLoader),
          loadSample_(context.loadSample), relinkSample_(context.relinkSample),
          setSampleFolder_(context.setSampleFolder), sampleStatus_(context.sampleStatus),
          publishing_(context.publishing)
    {
        setOpaque(true);
        setTitle(context.productName);

        const auto version =
            juce::String::fromUTF8(gg::kVersion.data(), static_cast<int>(gg::kVersion.size()));
        setUpLabel(title_, context.productName, juce::FontOptions(26.0f, juce::Font::bold), kText);
        setUpLabel(version_, "version " + version, juce::FontOptions(15.0f), kDimText);
        setUpLabel(noteOns_, {}, juce::FontOptions(16.0f), kText);
        setUpLabel(stressLabel_, "Canvas stress, grains:", juce::FontOptions(15.0f), kText);
        setUpLabel(frameMs_, {}, juce::FontOptions(15.0f), kText);
        if (showSampleLoader_)
        {
            setUpSampleStrip();
        }

        for (std::size_t i = 0; i < kGrainCounts.size(); ++i)
        {
            grainCount_.addItem(juce::String(kGrainCounts[i]), static_cast<int>(i) + 1);
        }
        grainCount_.setTitle("Canvas stress grain count");
        grainCount_.onChange = [this] {
            const int index = grainCount_.getSelectedItemIndex();
            if (index >= 0 && index < static_cast<int>(kGrainCounts.size()))
            {
                canvas_.setGrainCount(kGrainCounts[static_cast<std::size_t>(index)]);
            }
        };
        grainCount_.setSelectedItemIndex(kDefaultGrainCountIndex, juce::sendNotificationSync);
        addAndMakeVisible(grainCount_);
        addAndMakeVisible(canvas_);

        setSize(kEditorWidth, kEditorHeight);
        timerCallback();
        startTimerHz(30);
    }

    ~Editor() override
    {
        stopTimer();
        // The plugin publishes its grains and snapshots only while an editor is open (spec
        // §6.4); the shim destroys the editor in guiDestroy, with no hook of its own.
        if (publishing_)
        {
            publishing_(false);
        }
    }

    void paint(juce::Graphics &g) override { g.fillAll(kBackground); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);

        auto header = area.removeFromTop(36);
        title_.setBounds(header.removeFromLeft(320));
        version_.setBounds(header);

        noteOns_.setBounds(area.removeFromTop(26));
        area.removeFromTop(10);

        if (showSampleLoader_)
        {
            auto strip = area.removeFromTop(28);
            loadButton_.setBounds(strip.removeFromLeft(80));
            strip.removeFromLeft(8);
            folderButton_.setBounds(strip.removeFromRight(130));
            strip.removeFromRight(8);
            relinkButton_.setBounds(strip.removeFromRight(80));
            strip.removeFromRight(8);
            sampleName_.setBounds(strip.removeFromLeft(240));
            sampleStatusLabel_.setBounds(strip);
            area.removeFromTop(10);
        }

        auto controls = area.removeFromTop(28);
        stressLabel_.setBounds(controls.removeFromLeft(170));
        grainCount_.setBounds(controls.removeFromLeft(110));
        controls.removeFromLeft(16);
        frameMs_.setBounds(controls);

        area.removeFromTop(8);
        canvas_.setBounds(area);
    }

    // ---- juce::FileDragAndDropTarget: the strip's drop target, the whole editor ----

    bool isInterestedInFileDrag(const juce::StringArray &files) override
    {
        return showSampleLoader_ && !files.isEmpty();
    }

    void filesDropped(const juce::StringArray &files, int /*x*/, int /*y*/) override
    {
        if (isInterestedInFileDrag(files))
        {
            request(loadSample_, files[0]);
        }
    }

  private:
    void setUpSampleStrip()
    {
        setUpButton(loadButton_, "sampleLoad", withEllipsis("Load"),
                    [this] { chooseFile("Load a sample", loadSample_); });
        setUpButton(relinkButton_, "sampleRelink", withEllipsis("Relink"),
                    [this] { chooseFile("Find the missing sample", relinkSample_); });
        setUpButton(folderButton_, "sampleFolder", withEllipsis("Sample folder"),
                    [this] { chooseSampleFolder(); });
        relinkButton_.setVisible(
            false); // shown while the sample is missing, changed or can't be read
        setUpLabel(sampleName_, {}, juce::FontOptions(15.0f), kText);
        sampleName_.setComponentID("sampleName");
        setUpLabel(sampleStatusLabel_, {}, juce::FontOptions(15.0f), kDimText);
        sampleStatusLabel_.setComponentID("sampleStatus");
    }

    void setUpButton(juce::TextButton &button, const char *id, const juce::String &text,
                     std::function<void()> onClick)
    {
        button.setButtonText(text);
        button.setComponentID(id);
        button.onClick = std::move(onClick);
        addAndMakeVisible(button);
    }

    /// Calls `action` with `file`'s path as UTF-8, then shows the new status at once.
    void request(const std::function<void(const std::string &)> &action, const juce::String &file)
    {
        if (action)
        {
            action(file.toStdString());
        }
        timerCallback();
    }

    /// Opens an audio file chooser, asynchronously; a file chosen goes to `action`. The chooser
    /// is a member, so a dialog still open when the editor closes is dismissed with it, and its
    /// callback never runs.
    void chooseFile(const juce::String &title,
                    const std::function<void(const std::string &)> &action)
    {
        chooser_ = std::make_unique<juce::FileChooser>(title, juce::File(), kSampleFilePatterns);
        chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [this, &action](const juce::FileChooser &chooser) {
                                  const juce::File file = chooser.getResult();
                                  if (file != juce::File())
                                  {
                                      request(action, file.getFullPathName());
                                  }
                              });
    }

    void chooseSampleFolder()
    {
        chooser_ = std::make_unique<juce::FileChooser>("Choose the sample folder");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectDirectories,
                              [this](const juce::FileChooser &chooser) {
                                  const juce::File folder = chooser.getResult();
                                  if (folder != juce::File())
                                  {
                                      request(setSampleFolder_, folder.getFullPathName());
                                  }
                              });
    }

    void setUpLabel(juce::Label &label, const juce::String &text, const juce::FontOptions &font,
                    juce::Colour colour)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(font));
        label.setColour(juce::Label::textColourId, colour);
        addAndMakeVisible(label);
    }

    // 30 Hz: the note-on counter, the sample's status and the frame-time readout.
    // Label::setText does nothing when the text is unchanged.
    void timerCallback() override
    {
        const uint32_t noteOns =
            noteOnCount_ != nullptr ? noteOnCount_->load(std::memory_order_relaxed) : 0;
        noteOns_.setText("MIDI note-ons: " + juce::String(noteOns), juce::dontSendNotification);

        if (showSampleLoader_)
        {
            const SampleStatusView status = sampleStatus_ ? sampleStatus_() : SampleStatusView{};
            sampleName_.setText(juce::String::fromUTF8(status.fileName.data(),
                                                       static_cast<int>(status.fileName.size())),
                                juce::dontSendNotification);
            sampleStatusLabel_.setText(statusText(status), juce::dontSendNotification);
            relinkButton_.setVisible(status.relinkable);
        }

        juce::String readout = "frame ms (avg 60): " + juce::String(canvas_.averageFrameMs(), 2);
        if (canvas_.physicalWidth() > 0)
        {
            readout << "   at " << juce::String(canvas_.physicalWidth()) << " x "
                    << juce::String(canvas_.physicalHeight()) << " px";
        }
        frameMs_.setText(readout, juce::dontSendNotification);
    }

    const std::atomic<uint32_t> *noteOnCount_;
    const bool showSampleLoader_;
    const std::function<void(const std::string &)> loadSample_, relinkSample_, setSampleFolder_;
    const std::function<SampleStatusView()> sampleStatus_;
    const std::function<void(bool)> publishing_;

    juce::Label title_, version_, noteOns_, stressLabel_, frameMs_;
    juce::ComboBox grainCount_;
    StressCanvas canvas_;
    // The sample loader strip: built and shown only with showSampleLoader.
    juce::TextButton loadButton_, relinkButton_, folderButton_;
    juce::Label sampleName_, sampleStatusLabel_;
    std::unique_ptr<juce::FileChooser> chooser_; // the open dialog, if any
};

std::unique_ptr<juce::Component> makeEditor(const EditorContext &context)
{
    return std::make_unique<Editor>(context);
}
} // namespace gg::ui
