// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_LOADERSTRIP_H
#define GG_UI_LOADERSTRIP_H

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/ui/Editor.h"

namespace gg::ui
{
/// The instrument's sample loader strip (spec §6.5; component ID "sampleStrip"): "Load…"
/// ("sampleLoad"), the file name ("sampleName") and status ("sampleStatus"), "Relink…"
/// ("sampleRelink") while the sample is relinkable, and "Sample folder…" ("sampleFolder").
///
/// - The choosers open in the folder of the last file chosen or dropped here, or at first in
///   the settings' last load folder (EditorContext::lastLoadFolder).
/// - A dropped file (the first, when several are) loads if its extension is one the decoder
///   reads; any other is refused, and the status says why.
/// - A notice (a refused drop, the folder saved or not) shows in the status until the sample's
///   status changes or the next request.
/// - A status too long for its label shows whole in a tooltip.
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class LoaderStrip final : public juce::Component
{
  public:
    /// What a chooser asks for.
    struct Browse
    {
        juce::String title;
        juce::File initial;    ///< the folder it opens in; none to let the system choose
        bool folders{false};   ///< a folder, or an audio file
        juce::String patterns; ///< the file patterns, for a file
    };
    /// Opens a chooser and calls the callback with what was chosen (juce::File() if nothing),
    /// perhaps later. By default an asynchronous juce::FileChooser, which the strip owns, so a
    /// dialog still open when the strip goes is dismissed and its callback never runs. Tests
    /// replace it.
    std::function<void(const Browse &, std::function<void(const juce::File &)>)> browse;

    explicit LoaderStrip(const EditorContext &context);
    ~LoaderStrip() override;

    /// Polls the sample's status (the editor's 30 Hz timer).
    void refresh();
    /// Loads the first of `files` (full paths), or refuses it.
    void filesDropped(const juce::StringArray &files);

    void resized() override;

  private:
    void chooseFile(const juce::String &title, bool relink);
    void chooseSampleFolder();
    /// Calls `action` with `path` as UTF-8, then shows the new status at once.
    void request(const std::function<void(const std::string &)> &action, const juce::String &path);
    void setNotice(const juce::String &text);
    void showStatus(const juce::String &text);
    juce::File startFolder() const;

    const EditorContext &context_;
    juce::TextButton loadButton_, relinkButton_, folderButton_;
    juce::Label name_, status_;
    juce::File lastFolder_;                      // of the last file chosen or dropped here
    juce::String notice_;                        // shown in place of the status while it lasts
    juce::String noticeStatus_;                  // the status text when the notice was set
    juce::String statusText_;                    // the status as the loader last gave it
    std::unique_ptr<juce::FileChooser> chooser_; // the open dialog, if any
};
} // namespace gg::ui

#endif // GG_UI_LOADERSTRIP_H
