// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_EDITOR_H
#define GG_UI_EDITOR_H

#include <atomic>
#include <cstdint>
#include <functional>
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
/// What the instrument's sample is doing (spec §6.5): gg::plugin::SampleStatus, mirrored here
/// because the UI doesn't include plugin headers.
struct SampleStatusView
{
    enum class State
    {
        None,    ///< No sample.
        Loading, ///< A load or relink is under way.
        Loaded,  ///< Loaded.
        Missing, ///< A restored session's sample wasn't found.
        Changed, ///< Not found, and the file at the saved path has other content.
        Error,   ///< The last load failed; `message` says why.
    };

    State state{State::None};
    std::string fileName; ///< UTF-8
    std::string message;  ///< For the user: why it failed, or a note on a loaded sample.
    double seconds{0.0};  ///< Loaded: the file's length.
    uint32_t channels{0}; ///< Loaded: the file's channels, as stored.
    double fileRate{0.0}; ///< Loaded: the file's sample rate.
    /// The saved sample's file wasn't at hand (Missing, Changed, or an Error for a file that
    /// can't be read): the strip offers Relink.
    bool relinkable{false};
};

/// What the editor shows of the plugin that owns it.
struct EditorContext
{
    std::string productName; ///< "Giga Grains" or "Giga Grains FX"
    /// The plugin's note-on counter, polled at 30 Hz; may be null. It must outlive the editor.
    const std::atomic<uint32_t> *noteOnCount{nullptr};

    /// The sample loader strip (spec §6.5): the instrument only. The editor calls these on the
    /// main thread, with UTF-8 paths; any may be empty. What they capture must outlive the
    /// editor.
    bool showSampleLoader{false};
    std::function<void(const std::string &utf8Path)> loadSample, relinkSample, setSampleFolder;
    /// Polled at 30 Hz with the note counter, on the main thread; the plugin may do its
    /// main-thread work in it (applying a finished load).
    std::function<SampleStatusView()> sampleStatus;
};

/// The editor's initial size, in logical pixels. It lays out at any size the host gives it.
inline constexpr int kEditorWidth = 900;
inline constexpr int kEditorHeight = 500;

/// Creates the M0 editor, a gg::ui::Editor: the product name and version, a MIDI note-on
/// counter, the sample loader strip (with showSampleLoader), and the canvas stress panel. Call on
/// the message thread with JUCE initialised, as clap_juce_shim's guiCreate does.
std::unique_ptr<juce::Component> makeEditor(const EditorContext &context);
} // namespace gg::ui

#endif // GG_UI_EDITOR_H
