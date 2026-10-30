// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_UI_PRESETBROWSER_H
#define GG_UI_PRESETBROWSER_H

#include <cstddef>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "gg/ui/Editor.h"

namespace gg::ui
{
/// The preset browser (M2 sub-project 5's spec §6.8; component ID "preset.browser"), an overlay
/// the editor lays over the canvas and the macros while it is visible: a search field
/// ("preset.search"), a Favorites toggle ("preset.favorites"), the tags as toggles
/// ("preset.tag.<word>": the four focus tags always, then the other tags the listed presets
/// carry), the list ("preset.list", one row per preset, each with a star "preset.star"),
/// "Save as…" ("preset.save"), "Delete" ("preset.delete"), "Content folder…" ("preset.folder")
/// and the status line ("preset.status").
///
/// - Choosing a row loads it (by a click or the arrow keys) and leaves the browser open; Escape
///   closes it. The arrow keys work from the search field too.
/// - The list is the context's presetRows filtered here: the search, a substring of the name,
///   description or a tag, ignoring case; every selected tag; and, with Favorites, the starred.
/// - Delete works on a user preset only (the selected row), and asks for a second click.
/// - Save as… opens a panel ("preset.savepanel": name, description and tags, OK and Cancel); an
///   existing name asks "Replace?" first.
/// - The status line shows the context's contentStatus until an action leaves a notice (a load
///   that failed, a save, the content folder).
///
/// Message thread only. The context is the editor's (see EditorContext): read at each use.
class PresetBrowser final : public juce::Component,
                            private juce::ListBoxModel,
                            private juce::KeyListener
{
  public:
    /// What the Content folder chooser asks for.
    struct Browse
    {
        juce::String title;
        juce::File initial; ///< the folder it opens in; none to let the system choose
    };
    /// Opens a folder chooser and calls the callback with what was chosen (juce::File() if
    /// nothing), perhaps later. By default an asynchronous juce::FileChooser, which the browser
    /// owns. Tests replace it.
    std::function<void(const Browse &, std::function<void(const juce::File &)>)> browse;

    explicit PresetBrowser(const EditorContext &context);
    ~PresetBrowser() override;

    /// The editor's 30 Hz timer: when the loaded preset's name changed (the host loaded one),
    /// lists again.
    void refresh();
    /// Loads the neighbour of the loaded preset in the filtered list (`direction` +1 or -1,
    /// wrapping; the first or last when the loaded one isn't listed), listing again first.
    /// False if the list is empty or the load failed.
    bool step(int direction);

    void paint(juce::Graphics &g) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress &key) override;

  private:
    struct Row;
    class SavePanel;

    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics &, int, int, bool) override {}
    juce::Component *refreshComponentForRow(int row, bool selected,
                                            juce::Component *existing) override;
    void selectedRowsChanged(int lastRowSelected) override;
    // juce::KeyListener: the arrow keys in the search field
    bool keyPressed(const juce::KeyPress &key, juce::Component *origin) override;

    /// Lists again from the context (the catalog rescans), rebuilds the tag toggles and filters.
    void reload();
    void rebuildTags();
    void applyFilter();
    bool matches(const PresetRow &row) const;
    /// Loads filtered row `row`; false, with the error as the notice, if it fails. Marks it
    /// loaded in the list, leaves the selection to the caller.
    bool loadRow(int row);
    void selectRow(int row);
    int loadedRow() const;
    void toggleFavorite(int row);
    void deleteSelected();
    void chooseContentFolder();
    /// Save as…: "" or the error, for the panel.
    std::string save(const std::string &name, const std::vector<std::string> &tags,
                     const std::string &description, bool overwrite);
    void close();
    void setNotice(const juce::String &text);
    void updateStatus();
    void updateButtons();

    const EditorContext &context_;
    std::vector<PresetRow> rows_;
    std::vector<std::size_t> visible_; ///< indices into rows_, in order
    std::set<std::string> selectedTags_;
    std::vector<std::string> shownTags_;
    std::vector<std::unique_ptr<juce::TextButton>> tagButtons_;
    juce::TextEditor search_;
    juce::TextButton favorites_, saveButton_, deleteButton_, folderButton_;
    juce::ListBox list_;
    juce::Label status_;
    std::unique_ptr<SavePanel> savePanel_;
    std::unique_ptr<juce::FileChooser> chooser_; // the folder dialog, if any
    juce::String notice_;
    std::string lastName_; ///< presetName() when the list was last made
    bool settingSelection_{false};
    bool deleteArmed_{false};
};
} // namespace gg::ui

#endif // GG_UI_PRESETBROWSER_H
