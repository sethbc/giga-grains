// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "EditorPlugin.h"

#include <cstdint>

// createEditor() returns a std::unique_ptr<juce::Component>, whose destructor the return
// statement may invoke, so juce::Component must be complete here. This is the only plugin
// translation unit that includes JUCE.
#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/ui/Editor.h"

namespace gg::plugin
{
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
    return gg::ui::makeEditor({descriptorFor(product())->name, &noteOnCount});
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
