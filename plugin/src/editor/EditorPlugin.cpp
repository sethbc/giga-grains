// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "EditorPlugin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

// createEditor() returns a std::unique_ptr<juce::Component>, whose destructor the return
// statement may invoke, so juce::Component must be complete here. This is the only plugin
// translation unit that includes JUCE.
#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/core/Processor.h"
#include "gg/media/Utf8Path.h"
#include "gg/ui/Editor.h"

namespace gg::plugin
{
namespace
{
/// The path UTF-8 `text` names; empty if it can't be converted (text that isn't valid UTF-8, on
/// Windows), which the loader reports as a failed load.
std::filesystem::path pathOf(const std::string &text)
{
    return gg::media::fromUtf8(text).value_or(std::filesystem::path());
}

gg::ui::SampleStatusView viewOf(const SampleStatus &status)
{
    using State = gg::ui::SampleStatusView::State;
    State state = State::None;
    switch (status.state)
    {
    case SampleState::None:
        state = State::None;
        break;
    case SampleState::Loading:
        state = State::Loading;
        break;
    case SampleState::Loaded:
        state = State::Loaded;
        break;
    case SampleState::Missing:
        state = State::Missing;
        break;
    case SampleState::Changed:
        state = State::Changed;
        break;
    case SampleState::Error:
    case SampleState::Newer: // the strip shows the message, "sample from a newer version"
        state = State::Error;
        break;
    }
    return {state,           status.fileName, status.message,   status.seconds,
            status.channels, status.fileRate, status.relinkable};
}
} // namespace

EditorPlugin::EditorPlugin(const clap_host *host, Product product)
    : GigaGrainsPlugin(host, product), editorContext_(std::make_unique<gg::ui::EditorContext>())
{
    // As Shortcircuit XT does. Creating the shim needs no window and no message loop: the shim
    // starts JUCE's GUI in guiCreate, so a plugin that never shows its editor (or a headless
    // test) never does.
    clapJuceShim = std::make_unique<sst::clap_juce_shim::ClapJuceShim>(this);
    clapJuceShim->setResizable(true);
}

EditorPlugin::~EditorPlugin() = default;

std::unique_ptr<juce::Component> EditorPlugin::createEditor()
{
    // Filled afresh for each editor, in the one context this object keeps (see the class
    // comment).
    gg::ui::EditorContext &context = *editorContext_;
    context = gg::ui::EditorContext{};
    context.productName = descriptorFor(product())->name;
    // The instrument's loader strip (spec §6.5), on the main thread, as the loader requires. The
    // loader is GigaGrainsPlugin's, so it outlives the editor, which the shim holds.
    if (SampleLoader *loader = sampleLoader(); loader != nullptr)
    {
        // Nothing may throw into JUCE's callbacks. A request that can't be made (no memory)
        // leaves the status as the loader set it.
        context.showSampleLoader = true;
        context.loadSample = [loader](const std::string &path) {
            try
            {
                loader->requestLoad(pathOf(path));
            }
            catch (...)
            {
            }
        };
        context.relinkSample = [loader](const std::string &path) {
            try
            {
                loader->requestRelink(pathOf(path));
            }
            catch (...)
            {
            }
        };
        // The settings file couldn't be written, or an allocation failed: the folder stays as
        // it was, and the strip says so (carry-over row 198).
        context.setSampleFolder = [loader](const std::string &path) {
            gg::ui::SampleFolderResult result;
            try
            {
                result.saved = loader->setSampleFolder(pathOf(path));
                if (!result.saved)
                {
                    result.message =
                        "can't save the sample folder: the settings file can't be written";
                }
            }
            catch (...)
            {
                result.saved = false;
                try
                {
                    result.message = "can't save the sample folder: not enough memory";
                }
                catch (...)
                {
                }
            }
            return result;
        };
        context.lastLoadFolder = [loader] {
            try
            {
                return loader->lastLoadFolder();
            }
            catch (...)
            {
                return std::string();
            }
        };
        // Polled by the editor's 30 Hz timer, on the main thread: it first applies what the
        // loader's worker finished, as on_main_thread does, so a load shows (and plays) even
        // where the host doesn't deliver on_main_thread, as a VST3 host may not while the plugin
        // is inactive.
        context.sampleStatus = [this, loader] {
            onMainThread();
            return viewOf(loader->status());
        };
        // Cached by the loader: cheap once per sample.
        context.sampleWaveform = [loader] {
            const SampleWaveform w = loader->waveform();
            return gg::ui::SampleWaveform{w.peaks, w.frames, w.seconds};
        };
    }
    else
    {
        context.sampleWaveform = [] { return gg::ui::SampleWaveform{}; };
    }

    // The base values and the editor's gestures (sub-project 5's spec §6.3), through the
    // plugin's UI queue (GigaGrainsPlugin::uiBeginGesture).
    context.mode = mode();
    context.baseValue = [this](gg::core::ParamSlot slot) { return uiBaseValue(slot); };
    context.beginGesture = [this](gg::core::ParamSlot slot) { uiBeginGesture(slot); };
    context.setValue = [this](gg::core::ParamSlot slot, double value) { uiSetValue(slot, value); };
    context.endGesture = [this](gg::core::ParamSlot slot) { uiEndGesture(slot); };

    // Audio to UI (spec §6.4): the active Processor's structures, and publishing on from now
    // until the editor's destructor, or guiDestroy for the shim's, turns it off. The editor
    // copies its `publishing`, so each closes its own flag, once.
    updatePublished();
    auto counted = std::make_shared<bool>(true);
    context.publishing = [this, counted](bool open) {
        if (!open && *counted)
        {
            *counted = false;
            editorOpened(false);
        }
    };
    auto editor = gg::ui::makeEditor(context);
    editorOpened(true);
    lastEditor_ = std::move(counted);
    return editor;
}

bool EditorPlugin::guiCreate(const char *api, bool isFloating) noexcept
{
    // The shim calls createEditor() and holds what it returns.
    lastEditor_.reset();
    const bool created = clapJuceShim->guiCreate(api, isFloating);
    shimEditor_ = created ? lastEditor_ : nullptr;
    return created;
}

void EditorPlugin::guiDestroy() noexcept
{
    // With a parent attached the shim destroys its editor here, whose destructor closes it.
    // Without one (a host that created the GUI and never showed it) the shim keeps the editor
    // until the next guiCreate or its own end, so it is closed here: publishing follows
    // guiDestroy either way.
    clapJuceShim->guiDestroy();
    if (shimEditor_ != nullptr && *shimEditor_)
    {
        *shimEditor_ = false;
        editorOpened(false);
    }
    shimEditor_.reset();
}

bool EditorPlugin::guiAdjustSize(uint32_t *width, uint32_t *height) noexcept
{
    // Any size from the minimum up (sub-project 5's spec §6.1), in the host's pixels. The shim
    // applies a GUI scale on Windows only: getGuiScale() is 1 on macOS (logical pixels), and on
    // Linux its guiSetScale returns false without storing the scale, so it stays 1 there too.
    if (width == nullptr || height == nullptr)
    {
        return false;
    }
    const double scale = clapJuceShim->getGuiScale();
    const auto atLeast = [scale](uint32_t &v, int minimum) {
        const auto lowest = static_cast<uint32_t>(std::ceil(minimum * scale));
        v = std::max(v, lowest);
    };
    atLeast(*width, gg::ui::kEditorMinWidth);
    atLeast(*height, gg::ui::kEditorMinHeight);
    return true;
}

bool EditorPlugin::activate(double sampleRate, uint32_t minFrameCount,
                            uint32_t maxFrameCount) noexcept
{
    const bool activated = GigaGrainsPlugin::activate(sampleRate, minFrameCount, maxFrameCount);
    updatePublished();
    return activated;
}

void EditorPlugin::deactivate() noexcept
{
    // The pointers go with the Processor, before an editor can read them again: both here and
    // in the editor's timer are the main thread.
    GigaGrainsPlugin::deactivate();
    updatePublished();
}

void EditorPlugin::updatePublished() noexcept
{
    gg::ui::EditorContext &context = *editorContext_;
    gg::core::Processor *p = processor();
    context.grains = p != nullptr ? &p->grainQueue() : nullptr;
    context.snapshot = p != nullptr ? &p->snapshot() : nullptr;
    context.livePeaks = p != nullptr ? &p->livePeaks() : nullptr;
    context.clock = p != nullptr ? &p->publishedClock() : nullptr;
    context.sampleRate = activeSampleRate();
}

void EditorPlugin::editorOpened(bool open) noexcept
{
    if (open)
    {
        ++openEditors_;
    }
    else if (openEditors_ > 0)
    {
        --openEditors_;
    }
    setPublishing(openEditors_ > 0);
}

bool EditorPlugin::registerOrUnregisterTimer(clap_id &id, int ms, bool reg)
{
    if (!_host.canUseTimerSupport())
    {
        return false;
    }
    if (reg)
    {
        return _host.timerSupportRegister(static_cast<uint32_t>(ms), &id);
    }
    return _host.timerSupportUnregister(id);
}

bool EditorPlugin::registerOrUnregisterPosixFd(int fd, clap_posix_fd_flags_t flags, bool reg)
{
    if (!_host.canUsePosixFdSupport())
    {
        return false;
    }
    if (reg)
    {
        return _host.posixFdSupportRegister(fd, flags);
    }
    return _host.posixFdSupportUnregister(fd);
}
} // namespace gg::plugin
