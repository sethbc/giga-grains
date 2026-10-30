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
#include "gg/ui/EditLayout.h" // gg::ui::View, JUCE-free

namespace gg::ui
{
struct EditorContext; // gg/ui/Editor.h, kept out of this header's includers
class UndoStack;      // gg/ui/UndoStack.h
} // namespace gg::ui

namespace gg::plugin
{
/// A GigaGrainsPlugin with its editor. The editor (gg::ui) is a JUCE component, attached
/// through sst-clap-helpers' clap_juce_shim the way Shortcircuit XT attaches its own. Built only
/// with GG_BUILD_EDITOR. The audio side is GigaGrainsPlugin's, unchanged.
///
/// **The editor's context** (gg::ui::EditorContext) is this object's, for its whole life: the
/// editor keeps a reference to it and reads the active Processor's publish structures through
/// it, and activate() and deactivate() update them there, on the main thread (see
/// EditorContext).
///
/// **The undo history** (gg::ui::UndoStack, M2 sub-project 6's spec §6.8) is this object's too,
/// one per instance from construction to destruction, so closing and reopening the editor keeps
/// it. An undo or redo replays through the UI's gesture path (uiBeginGesture, uiSetValue,
/// uiEndGesture: hosts record it as automation) or uiSetRoutes.
///
/// **The view** an editor shows is remembered here while the instance lives (spec §6.2), and
/// the next editor opens on it; it is not saved and is no parameter.
///
/// **The theme** (spec §6.9) is the per-machine setting (UiSettings.h), mapped between the UI's
/// gg::ui::ThemeChoice and the plugin's ThemeName, the two sides of the JUCE-free line.
///
/// **Publishing** is on while any editor createEditor() made is open: each turns it on when
/// created and off, through its own `publishing`, when destroyed (the shim has no destroy hook
/// of its own). gui.destroy closes the shim's editor too, even when the shim keeps it (it
/// destroys its editor only once a parent was attached), so publishing follows guiDestroy.
class EditorPlugin : public GigaGrainsPlugin, public sst::clap_juce_shim::EditorProvider
{
  public:
    EditorPlugin(const clap_host *host, Product product);
    ~EditorPlugin() override;

    // ---- The GUI: clap_juce_shim implements CLAP's gui extension around createEditor() ----
    // Members of this subclass are destroyed before the GigaGrainsPlugin it derives from, so
    // the shim, and any editor it still holds, goes before the loader and the parameters, which
    // the editor's context reaches.

    // clapJuceShim itself is declared last, after the private members (below).
    bool implementsGui() const noexcept override { return clapJuceShim != nullptr; }
    // ADD_SHIM_IMPLEMENTATION(clapJuceShim), by hand: guiCreate, guiDestroy and guiAdjustSize
    // are this class's (below); the rest forward to the shim as the macro's do.
    bool guiCanResize() const noexcept override { return clapJuceShim->guiCanResize(); }
    bool guiIsApiSupported(const char *api, bool isFloating) noexcept override
    {
        return clapJuceShim->guiIsApiSupported(api, isFloating);
    }
    bool guiCreate(const char *api, bool isFloating) noexcept override;
    void guiDestroy() noexcept override;
    bool guiSetParent(const clap_window *window) noexcept override
    {
        return clapJuceShim->guiSetParent(window);
    }
    bool guiSetScale(double scale) noexcept override { return clapJuceShim->guiSetScale(scale); }
    bool guiAdjustSize(uint32_t *width, uint32_t *height) noexcept override;
    bool guiSetSize(uint32_t width, uint32_t height) noexcept override
    {
        return clapJuceShim->guiSetSize(width, height);
    }
    bool guiGetSize(uint32_t *width, uint32_t *height) noexcept override
    {
        return clapJuceShim->guiGetSize(width, height);
    }
    bool guiShow() noexcept override { return clapJuceShim->guiShow(); }
    ADD_SHIM_LINUX_TIMER(clapJuceShim)

    /// The editor, a gg::ui::Editor. The shim calls this on the main thread, with JUCE set up.
    std::unique_ptr<juce::Component> createEditor() override;
    /// The context the editors are given (see the class comment). Main thread.
    const gg::ui::EditorContext &editorContext() const noexcept { return *editorContext_; }
    /// For the shim on Linux, which runs JUCE's event loop from host timers and file
    /// descriptors.
    bool registerOrUnregisterTimer(clap_id &id, int ms, bool reg) override;
    bool registerOrUnregisterPosixFd(int fd, clap_posix_fd_flags_t flags, bool reg) override;

  protected:
    /// GigaGrainsPlugin's, then the context's publish structures are the new Processor's.
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override;
    /// GigaGrainsPlugin's, then the context's publish structures are null.
    void deactivate() noexcept override;

  private:
    /// Points the context's publish structures at the active Processor's, or null. Main thread.
    void updatePublished() noexcept;
    /// An editor was created (true) or destroyed (false): publishing is on while any is alive.
    void editorOpened(bool open) noexcept;

    // Before clapJuceShim, so destroyed after it: an editor the shim still holds reads the
    // context, and its destructor calls editorOpened(false), which reads the rest. The undo
    // stack must outlive every editor too: their components close their open gestures and
    // groups in it as they are destroyed (EditorContext::undo).
    std::unique_ptr<gg::ui::UndoStack> undo_;
    std::unique_ptr<gg::ui::EditorContext> editorContext_;
    uint32_t openEditors_{0}; // main thread
    // The view the editor last showed, which the next one opens on (spec §6.2): the instance's,
    // not its state's. Main thread.
    gg::ui::View lastView_{gg::ui::View::Perform};
    // Each editor's "still counted open" flag, which its publishing callback and guiDestroy
    // share, so an editor is counted closed once: the last createEditor()'s, and the one the
    // shim holds (from guiCreate to guiDestroy). Main thread.
    std::shared_ptr<bool> lastEditor_, shimEditor_;

  public:
    // Last, so destroyed first (above). Public as ADD_SHIM_IMPLEMENTATION's is; the tests reach it.
    std::unique_ptr<sst::clap_juce_shim::ClapJuceShim> clapJuceShim;
};
} // namespace gg::plugin

#endif // GG_PLUGIN_EDITOR_EDITORPLUGIN_H
