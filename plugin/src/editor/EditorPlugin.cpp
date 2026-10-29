// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "EditorPlugin.h"

#include <cstdint>
#include <filesystem>
#include <string>

// createEditor() returns a std::unique_ptr<juce::Component>, whose destructor the return
// statement may invoke, so juce::Component must be complete here. This is the only plugin
// translation unit that includes JUCE.
#include <juce_gui_basics/juce_gui_basics.h>

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
    return {state,          status.fileName, status.message,
            status.seconds, status.channels, status.fileRate};
}
} // namespace

EditorPlugin::EditorPlugin(const clap_host *host, Product product) : GigaGrainsPlugin(host, product)
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
    gg::ui::EditorContext context{descriptorFor(product())->name, &noteOnCount};
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
        // The settings file couldn't be written (false), or an allocation failed: the folder
        // stays as it was.
        context.setSampleFolder = [loader](const std::string &path) {
            try
            {
                loader->setSampleFolder(pathOf(path));
            }
            catch (...)
            {
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
    }
    return gg::ui::makeEditor(context);
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
