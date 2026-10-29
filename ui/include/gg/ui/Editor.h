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
#include <vector>

#include "gg/core/GrainQueue.h"
#include "gg/core/LivePeaks.h"
#include "gg/core/ParamIds.h"
#include "gg/core/UiSnapshot.h"

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

/// The outline of the instrument's sample (sub-project 5's spec §6.2): gg::plugin::
/// SampleWaveform, mirrored here as SampleStatusView mirrors the status. The peak, max |x| over
/// every channel, of each 512 frames; empty (peaks null) with no sample.
struct SampleWaveform
{
    std::shared_ptr<const std::vector<float>> peaks;
    int64_t frames{0};   ///< the sample's frames, at its own rate: GrainVis::startPos's unit
    double seconds{0.0}; ///< frames at the sample's rate
};

/// What "Sample folder…" did: whether the settings were saved, and if not, why, for the user.
struct SampleFolderResult
{
    bool saved{false};
    std::string message; ///< UTF-8; shown in the strip when the save failed
};

/// What the editor shows of the plugin that owns it.
///
/// **The editor keeps a reference to the context, never a copy**, and reads it on the main
/// thread at each use, so the context must outlive the editor. The one exception is
/// `publishing`, which the editor copies when it is made (each editor has its own).
///
/// **The publish structures** (grains, snapshot, livePeaks, clock, sampleRate) belong to the
/// plugin's active Processor, which the plugin replaces at every activation: they are null while
/// the plugin is inactive, and the plugin updates them in the context it gave makeEditor, in
/// place, on the main thread, when it activates or deactivates. The editor reads them through it
/// each frame, never keeping a pointer: one kept across a deactivate() dangles. A new
/// activation's clock starts again from 0.
struct EditorContext
{
    std::string productName; ///< "Giga Grains" or "Giga Grains FX"

    /// The sample loader strip (spec §6.5): the instrument only. The editor calls these on the
    /// main thread, with UTF-8 paths; any may be empty. What they capture must outlive the
    /// editor.
    bool showSampleLoader{false};
    std::function<void(const std::string &utf8Path)> loadSample, relinkSample;
    /// Sets and saves the per-machine sample folder; the strip shows a failure's message.
    std::function<SampleFolderResult(const std::string &utf8Path)> setSampleFolder;
    /// The settings' last load folder (UTF-8; empty if none), where the file choosers open
    /// until a file is chosen or dropped in this editor.
    std::function<std::string()> lastLoadFolder;
    /// Polled at 30 Hz, on the main thread; the plugin may do its main-thread work in it
    /// (applying a finished load).
    std::function<SampleStatusView()> sampleStatus;

    /// The product's parameters' metadata and its lane (gg::core::paramMeta).
    gg::core::Mode mode{gg::core::Mode::Fx};

    /// Base values (sub-project 5's spec §6.3), main thread. baseValue reads a parameter's base
    /// value as the host does (params.get_value). A change is a gesture: beginGesture, then
    /// setValue for each move (clamped to the parameter's range), then endGesture, exactly once
    /// each, which the plugin applies on the audio thread and sends on to the host so it records
    /// automation; baseValue shows a value once the audio thread has taken it. What they capture
    /// outlives the editor.
    std::function<float(gg::core::ParamSlot)> baseValue;
    std::function<void(gg::core::ParamSlot)> beginGesture, endGesture;
    std::function<void(gg::core::ParamSlot, double)> setValue;

    /// The spawned grains (spec §6.4); the editor is the queue's consumer. See the publish
    /// structures above; null while inactive.
    gg::core::GrainQueue *grains{nullptr};
    /// The latest per-block snapshot; the editor is its consumer. Null while inactive.
    gg::core::SnapshotBuffer *snapshot{nullptr};
    /// The FX's live-ring peak strip (the instrument's is unused). Null while inactive.
    const gg::core::LivePeaks *livePeaks{nullptr};
    /// The sample clock of the last call that published (Processor::publishedClock): acquire
    /// it, then take the snapshot. Null while inactive.
    const std::atomic<int64_t> *clock{nullptr};
    /// The active sample rate (the last activation's while inactive), for extrapolating the
    /// clock between snapshots.
    double sampleRate{0.0};
    /// The instrument's sample outline, main thread: cached by the plugin, so it is cheap to
    /// call every frame; empty in the FX and with no sample.
    std::function<SampleWaveform()> sampleWaveform;

    /// The editor's destructor calls publishing(false): the plugin stops publishing (the shim
    /// has no destroy hook). The plugin turned it on when it created the editor. May be empty.
    /// Copied when the editor is made, so each editor calls the one it was given.
    std::function<void(bool)> publishing;
};

/// The editor's initial size, in logical pixels. It is resizable, and lays out at any size the
/// host gives it; the plugin asks hosts for at least the minimum (gui.adjust_size).
inline constexpr int kEditorWidth = 900;
inline constexpr int kEditorHeight = 500;
inline constexpr int kEditorMinWidth = 640;
inline constexpr int kEditorMinHeight = 360;

/// Creates the editor, a gg::ui::Editor: the Perform view (sub-project 5's spec §6.1). A header
/// (the product name, the preset, Perform and a disabled Edit, and the frame time), the
/// instrument's sample loader strip (with showSampleLoader), the grain canvas, and the eight
/// macros. Call on the message thread with JUCE initialised, as clap_juce_shim's guiCreate does.
/// The editor keeps a reference to `context`, which must outlive it (see EditorContext).
std::unique_ptr<juce::Component> makeEditor(const EditorContext &context);
} // namespace gg::ui

#endif // GG_UI_EDITOR_H
