// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_PLUGIN_EDITOR_EDITORPLUGIN_H
#define GG_PLUGIN_EDITOR_EDITORPLUGIN_H

#include <memory>
// Before clap_juce_shim.h: sst-clap-helpers' clap_juce_shim.h uses std::string without <string>.
#include <string>

#include <clap/clap.h>
#include <sst/clap_juce_shim/clap_juce_shim.h>

#include "GigaGrainsPlugin.h"

namespace gg::plugin
{
/// A GigaGrainsPlugin with its editor. The editor (gg::ui) is a JUCE component, attached
/// through sst-clap-helpers' clap_juce_shim the way Shortcircuit XT attaches its own. Built only
/// with GG_BUILD_EDITOR. The audio side is GigaGrainsPlugin's, unchanged.
class EditorPlugin : public GigaGrainsPlugin, public sst::clap_juce_shim::EditorProvider
{
  public:
    EditorPlugin(const clap_host *host, Product product);
    ~EditorPlugin() override;

    // ---- The GUI: clap_juce_shim implements CLAP's gui extension around createEditor() ----
    // Members of this subclass are destroyed before the GigaGrainsPlugin it derives from, so
    // the shim, and any editor it still holds, goes before noteOnCount, which the editor reads.

    bool implementsGui() const noexcept override { return clapJuceShim != nullptr; }
    std::unique_ptr<sst::clap_juce_shim::ClapJuceShim> clapJuceShim;
    ADD_SHIM_IMPLEMENTATION(clapJuceShim)
    ADD_SHIM_LINUX_TIMER(clapJuceShim)

    /// The editor, a gg::ui::Editor. The shim calls this on the main thread, with JUCE set up.
    std::unique_ptr<juce::Component> createEditor() override;
    /// For the shim on Linux, which runs JUCE's event loop from host timers and file
    /// descriptors.
    bool registerOrUnregisterTimer(clap_id &id, int ms, bool reg) override;
    bool registerOrUnregisterPosixFd(int fd, clap_posix_fd_flags_t flags, bool reg) override;
};
} // namespace gg::plugin

#endif // GG_PLUGIN_EDITOR_EDITORPLUGIN_H
