// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_LOADERSTRIP_H
#define GG_UI_LOADERSTRIP_H

#include <cstdint>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/ui/Editor.h"

namespace gg::ui
{
/// Whether the strip is the Edit view's row (letter, name, status, Load and Relink) or Perform's
/// (those, and the memory readout, its cap menu, Collect and Sample folder).
enum class Compact : uint8_t
{
    No,
    Yes
};

/// The sample loader strip (spec §6.3, §6.5; component ID "sampleStrip") for one layer at a time,
/// the selected one (setLayer), in both products: the layer's letter in its color
/// ("loader.letter"), "Load..." ("sampleLoad"), the file name ("sampleName") and status
/// ("sampleStatus"), "Relink..." ("sampleRelink") while the sample is relinkable and, unless
/// compact, the memory readout ("loader.memory", "312 MB of 1 GB"), the cap menu ("loader.cap"),
/// "Collect..." ("loader.collect") and "Sample folder..." ("sampleFolder").
///
/// - The choosers open in the folder of the last file chosen or dropped here, or at first in
///   the settings' last load folder (EditorContext::lastLoadFolder).
/// - A dropped file (the first, when several are) loads if its extension is one the decoder
///   reads; any other is refused, and the status says why.
/// - A notice (a refused drop, the folder saved or not) shows in the status until the sample's
///   status changes or the next request.
/// - A status too long for its label shows whole in a tooltip.
/// - The readout is polled every 10th refresh (and when the cap is chosen), and drawn in the
///   palette's notice color while the instance holds more than its cap (nothing is unloaded).
///   The menu ticks the current cap; a cap that is not one of the choices (a state's) is listed
///   in order and ticked.
/// - Collect opens a folder chooser, then EditorContext::collectSamples; the summary is posted
///   as a notice.
/// - Narrower than kNarrowWidth (the editor's minimum is 640), "Sample folder..." and "Collect..."
///   move into a "..." menu ("loader.more") so the name and status keep room; the readout is as
///   wide as its text, and Relink takes no width while it is hidden.
/// - Its buttons and menu take the editor's palette, and follow a theme switch.
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class LoaderStrip final : public juce::Component
{
  public:
    /// Below this width the strip folds Sample folder and Collect into its "..." menu.
    static constexpr int kNarrowWidth = 900;

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

    LoaderStrip(const EditorContext &context, uint8_t layer, Compact compact);
    ~LoaderStrip() override;

    /// Shows another layer's sample (0 to 3; clamped), dropping a notice about the last.
    void setLayer(uint8_t layer);
    uint8_t layer() const noexcept { return layer_; }
    /// Polls the layer's sample status (the editor's 30 Hz timer), and the memory every 10th time.
    void refresh();
    /// Loads the first of `files` (full paths), or refuses it.
    void filesDropped(const juce::StringArray &files);

    void resized() override;
    /// The editor's palette, when the strip joins it and when its theme changes.
    void parentHierarchyChanged() override;
    void lookAndFeelChanged() override;

  private:
    void applyPalette();
    void chooseFile(const juce::String &title, bool relink);
    void chooseSampleFolder();
    void chooseCollectFolder();
    /// Loads or relinks the layer's sample from `path` (UTF-8 to the context), then shows the new
    /// status at once.
    void request(bool relink, const juce::String &path);
    void pollMemory();
    void applyMemory();
    void fillCapMenu(uint32_t capMiB);
    void setNotice(const juce::String &text);
    /// Whether the strip shows (it and its ancestors below the root visible).
    bool shownInEditor() const;
    int memoryTextWidth() const;
    void showMore();
    void showStatus(const juce::String &text);
    juce::File startFolder() const;

    const EditorContext &context_;
    uint8_t layer_;
    const bool compact_;
    juce::TextButton loadButton_, relinkButton_, folderButton_, collectButton_, moreButton_;
    juce::ComboBox capMenu_;
    juce::Label letter_, name_, status_, memory_;
    MemoryView memoryView_; // as last polled
    uint32_t capListed_{0}; // the cap the menu's items were made for
    uint32_t ticks_{0};
    bool overCap_{false};
    int memoryWidth_{0};    // the readout's width: its text's, so it never takes more than it needs
    juce::File lastFolder_; // of the last file chosen or dropped here
    juce::String notice_;   // shown in place of the status while it lasts
    juce::String noticeStatus_;                  // the status text when the notice was set
    juce::String statusText_;                    // the status as the loader last gave it
    std::unique_ptr<juce::FileChooser> chooser_; // the open dialog, if any
};
} // namespace gg::ui

#endif // GG_UI_LOADERSTRIP_H
