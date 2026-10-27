// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_EDITOR_H
#define GG_UI_EDITOR_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

// This header stays free of JUCE; gg::ui::Editor itself is defined in Editor.cpp. A caller that
// may destroy the returned std::unique_ptr<juce::Component> still needs juce::Component to be
// complete there, as GigaGrainsPlugin.cpp does.
namespace juce
{
class Component;
}

namespace gg::ui
{
/// What the editor shows of the plugin that owns it.
struct EditorContext
{
    std::string productName; ///< "Giga Grains" or "Giga Grains FX"
    /// The plugin's note-on counter, polled at 30 Hz; may be null. It must outlive the editor.
    const std::atomic<uint32_t> *noteOnCount{nullptr};
};

/// The editor's initial size, in logical pixels. It lays out at any size the host gives it.
inline constexpr int kEditorWidth = 900;
inline constexpr int kEditorHeight = 500;

/// Creates the M0 editor, a gg::ui::Editor: the product name and version, a MIDI note-on
/// counter, and the canvas stress panel. Call on the message thread with JUCE initialised, as
/// clap_juce_shim's guiCreate does.
std::unique_ptr<juce::Component> makeEditor(const EditorContext &context);
} // namespace gg::ui

#endif // GG_UI_EDITOR_H
