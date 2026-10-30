// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#include "PresetBrowser.h"

#include <algorithm>
#include <initializer_list>
#include <utility>

#include "Theme.h"

namespace gg::ui
{
namespace
{
constexpr int kRowHeight = 24;
constexpr int kControlHeight = 26;
constexpr int kTagHeight = 22;
constexpr int kGap = 4;
constexpr int kStarWidth = 30;
constexpr std::size_t kFocusTags = 4; ///< the vocabulary's first four

juce::String fromUtf8(const std::string &text) { return juce::String::fromUTF8(text.c_str()); }

void styleButton(juce::TextButton &button, const char *id, const juce::String &text)
{
    button.setComponentID(id);
    button.setButtonText(text);
    button.setColour(juce::TextButton::buttonColourId, theme::kBackground);
    button.setColour(juce::TextButton::buttonOnColourId, theme::kAccent.withAlpha(0.35f));
    button.setColour(juce::TextButton::textColourOffId, theme::kDimText);
    button.setColour(juce::TextButton::textColourOnId, theme::kText);
    button.setColour(juce::ComboBox::outlineColourId, theme::kOutline);
}

void styleEditor(juce::TextEditor &editor, const char *id, const juce::String &placeholder)
{
    editor.setComponentID(id);
    editor.setTextToShowWhenEmpty(placeholder, theme::kDimText);
    editor.setColour(juce::TextEditor::backgroundColourId, theme::kBackground);
    editor.setColour(juce::TextEditor::textColourId, theme::kText);
    editor.setColour(juce::TextEditor::outlineColourId, theme::kOutline);
    editor.setColour(juce::TextEditor::focusedOutlineColourId, theme::kAccent);
}

/// A toggle for the tag `word`.
std::unique_ptr<juce::TextButton> makeTagButton(const std::string &word, const char *prefix)
{
    auto button = std::make_unique<juce::TextButton>();
    styleButton(*button, (std::string(prefix) + word).c_str(), fromUtf8(word));
    button->setClickingTogglesState(true);
    return button;
}

/// Places `buttons` left to right in rows of at most `width`, from (x, y); returns the height
/// used. With `place` false, only measures.
int flowTags(const std::vector<std::unique_ptr<juce::TextButton>> &buttons, int x, int y, int width,
             bool place)
{
    int cx = 0;
    int cy = 0;
    for (const auto &button : buttons)
    {
        const int w = std::min(width, button->getBestWidthForHeight(kTagHeight) + 8);
        if (cx > 0 && cx + w > width)
        {
            cx = 0;
            cy += kTagHeight + kGap;
        }
        if (place)
        {
            button->setBounds(x + cx, y + cy, w, kTagHeight);
        }
        cx += w + kGap;
    }
    return buttons.empty() ? 0 : cy + kTagHeight;
}
} // namespace

/// One list row: the name, the tags and the favorite star.
struct PresetBrowser::Row final : juce::Component
{
    explicit Row(PresetBrowser &browser) : owner(browser)
    {
        setComponentID("preset.row");
        // A click anywhere but the star reaches the list, which selects the row.
        setInterceptsMouseClicks(false, true);
        styleButton(star, "preset.star", {});
        star.onClick = [this] { owner.toggleFavorite(row); };
        addAndMakeVisible(star);
    }

    void update(int newRow, bool isSelected)
    {
        row = newRow;
        selected = isSelected;
        const PresetRow &r = owner.rows_[owner.visible_[static_cast<std::size_t>(row)]];
        star.setButtonText(r.favorite ? juce::String::fromUTF8("\xe2\x98\x85")
                                      : juce::String::fromUTF8("\xe2\x98\x86"));
        star.setColour(juce::TextButton::textColourOffId,
                       r.favorite ? theme::kAccent : theme::kDimText);
        star.setTooltip(r.favorite ? "Remove from favorites" : "Add to favorites");
        setTitle(fromUtf8(r.name));
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        const PresetRow &r = owner.rows_[owner.visible_[static_cast<std::size_t>(row)]];
        if (selected)
        {
            g.fillAll(theme::kAccent.withAlpha(0.25f));
        }
        auto area = getLocalBounds().withTrimmedRight(kStarWidth + kGap).reduced(6, 0);
        g.setColour(r.loaded ? theme::kText : theme::kText.withAlpha(0.85f));
        const juce::Font nameFont(
            juce::FontOptions(14.0f, r.loaded ? juce::Font::bold : juce::Font::plain));
        g.setFont(nameFont);
        const juce::String name = fromUtf8(r.name);
        const int nameWidth = std::min(area.getWidth() * 2 / 3,
                                       juce::GlyphArrangement::getStringWidthInt(nameFont, name));
        g.drawText(name, area.removeFromLeft(nameWidth), juce::Justification::centredLeft, true);
        area.removeFromLeft(10);
        juce::String tags;
        for (const auto &tag : r.tags)
        {
            tags << (tags.isEmpty() ? "" : "  ") << fromUtf8(tag);
        }
        if (r.factory)
        {
            tags = juce::String("factory") + (tags.isEmpty() ? "" : "  ") + tags;
        }
        g.setColour(theme::kDimText);
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(tags, area, juce::Justification::centredLeft, true);
    }

    void resized() override { star.setBounds(getLocalBounds().removeFromRight(kStarWidth)); }

    PresetBrowser &owner;
    juce::TextButton star;
    int row{0};
    bool selected{false};
};

/// The Save as… panel: a name, a description and tags, OK and Cancel.
class PresetBrowser::SavePanel final : public juce::Component
{
  public:
    explicit SavePanel(PresetBrowser &browser) : owner_(browser)
    {
        setComponentID("preset.savepanel");
        setWantsKeyboardFocus(true);
        title_.setText(juce::String::fromUTF8("Save as\xe2\x80\xa6"), juce::dontSendNotification);
        title_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        title_.setColour(juce::Label::textColourId, theme::kText);
        message_.setComponentID("preset.save.message");
        message_.setColour(juce::Label::textColourId, theme::kAccent);
        styleEditor(name_, "preset.save.name", "Name");
        styleEditor(description_, "preset.save.description", "Description (optional)");
        styleButton(ok_, "preset.save.ok", "OK");
        styleButton(cancel_, "preset.save.cancel", "Cancel");
        for (const auto &word : browser.context_.tagVocabulary)
        {
            tags_.push_back(makeTagButton(word, "preset.save.tag."));
            addAndMakeVisible(*tags_.back());
        }
        name_.onTextChange = [this] {
            confirming_ = false;
            showReplace();
        };
        name_.onReturnKey = [this] { ok(); };
        name_.onEscapeKey = [this] { close(); };
        description_.onReturnKey = [this] { ok(); };
        description_.onEscapeKey = [this] { close(); };
        ok_.onClick = [this] { ok(); };
        cancel_.onClick = [this] { close(); };
        addAndMakeVisible(title_);
        addAndMakeVisible(message_);
        addAndMakeVisible(name_);
        addAndMakeVisible(description_);
        addAndMakeVisible(ok_);
        addAndMakeVisible(cancel_);
        setVisible(false);
    }

    void open(const juce::String &name)
    {
        name_.setText(name, juce::dontSendNotification);
        description_.clear();
        for (auto &tag : tags_)
        {
            tag->setToggleState(false, juce::dontSendNotification);
        }
        confirming_ = false;
        message_.setText({}, juce::dontSendNotification);
        ok_.setButtonText("OK");
        setVisible(true);
        toFront(false);
        if (isShowing())
        {
            name_.grabKeyboardFocus();
        }
    }

    void close()
    {
        setVisible(false);
        if (owner_.isShowing())
        {
            owner_.search_.grabKeyboardFocus();
        }
    }

    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.55f));
        g.setColour(theme::kHeader);
        g.fillRoundedRectangle(box_.toFloat(), 4.0f);
        g.setColour(theme::kOutline);
        g.drawRoundedRectangle(box_.toFloat(), 4.0f, 1.0f);
    }

    void resized() override
    {
        const int width = std::min(getWidth() - 20, 520);
        const int inner = width - 24;
        const int tagsHeight = flowTags(tags_, 0, 0, inner, false);
        const int height =
            std::min(getHeight() - 8, 12 + 22 + kGap + kControlHeight + kGap + kControlHeight + 8 +
                                          tagsHeight + 8 + 18 + 8 + kControlHeight + 12);
        box_ = juce::Rectangle<int>(width, height).withCentre(getLocalBounds().getCentre());
        auto area = box_.reduced(12, 12);
        title_.setBounds(area.removeFromTop(22));
        area.removeFromTop(kGap);
        name_.setBounds(area.removeFromTop(kControlHeight));
        area.removeFromTop(kGap);
        description_.setBounds(area.removeFromTop(kControlHeight));
        area.removeFromTop(8);
        flowTags(tags_, area.getX(), area.getY(), inner, true);
        area.removeFromTop(tagsHeight + 8);
        auto bottom = area.removeFromBottom(kControlHeight);
        cancel_.setBounds(bottom.removeFromRight(80));
        bottom.removeFromRight(kGap);
        ok_.setBounds(bottom.removeFromRight(80));
        area.removeFromBottom(8);
        message_.setBounds(area.removeFromBottom(18));
    }

    bool keyPressed(const juce::KeyPress &key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }
        return true; // modal: nothing falls through to the browser behind
    }

  private:
    void showReplace()
    {
        ok_.setButtonText(confirming_ ? "Replace" : "OK");
        if (!confirming_)
        {
            message_.setText({}, juce::dontSendNotification);
        }
    }

    void ok()
    {
        std::vector<std::string> tags;
        for (const auto &tag : tags_)
        {
            if (tag->getToggleState())
            {
                tags.push_back(tag->getButtonText().toStdString());
            }
        }
        const std::string name = name_.getText().trim().toStdString();
        const std::string error =
            owner_.save(name, tags, description_.getText().toStdString(), confirming_);
        if (error.empty())
        {
            close();
        }
        else if (!confirming_ && error.rfind("a preset named", 0) == 0)
        {
            confirming_ = true;
            message_.setText("Replace the preset named " + juce::String::fromUTF8(name.c_str()) +
                                 "?",
                             juce::dontSendNotification);
            ok_.setButtonText("Replace");
        }
        else
        {
            confirming_ = false;
            ok_.setButtonText("OK");
            message_.setText(fromUtf8(error), juce::dontSendNotification);
        }
    }

    PresetBrowser &owner_;
    juce::Rectangle<int> box_;
    juce::Label title_, message_;
    juce::TextEditor name_, description_;
    std::vector<std::unique_ptr<juce::TextButton>> tags_;
    juce::TextButton ok_, cancel_;
    bool confirming_{false};
};

PresetBrowser::PresetBrowser(const EditorContext &context)
    : context_(context), list_("preset.list", this), savePanel_(std::make_unique<SavePanel>(*this))
{
    setComponentID("preset.browser");
    setWantsKeyboardFocus(true);
    setVisible(false);

    styleEditor(search_, "preset.search", "Search presets");
    search_.setSelectAllWhenFocused(true);
    search_.onTextChange = [this] { applyFilter(); };
    search_.onEscapeKey = [this] { close(); };
    search_.addKeyListener(this);
    styleButton(favorites_, "preset.favorites", juce::String::fromUTF8("\xe2\x98\x85 Favorites"));
    favorites_.setClickingTogglesState(true);
    favorites_.onClick = [this] { applyFilter(); };
    styleButton(saveButton_, "preset.save", juce::String::fromUTF8("Save as\xe2\x80\xa6"));
    styleButton(mutateButton_, "preset.mutate", "Mutate");
    mutateButton_.setTooltip("Mutate: move the sound at random, within the locks");
    mutateButton_.onClick = [this] {
        if (onMutate)
        {
            onMutate();
        }
    };
    styleButton(deleteButton_, "preset.delete", "Delete");
    styleButton(folderButton_, "preset.folder",
                juce::String::fromUTF8("Content folder\xe2\x80\xa6"));
    saveButton_.onClick = [this] {
        const std::string name = context_.presetName ? context_.presetName() : std::string();
        savePanel_->open(name == "Init" ? juce::String() : fromUtf8(name));
    };
    deleteButton_.onClick = [this] { deleteSelected(); };
    folderButton_.onClick = [this] { chooseContentFolder(); };
    status_.setComponentID("preset.status");
    status_.setColour(juce::Label::textColourId, theme::kDimText);
    status_.setFont(juce::FontOptions(12.0f));
    list_.setComponentID("preset.list");
    list_.setRowHeight(kRowHeight);
    list_.setColour(juce::ListBox::backgroundColourId, theme::kLane);
    list_.setColour(juce::ListBox::outlineColourId, theme::kOutline);
    list_.setOutlineThickness(1);
    list_.setWantsKeyboardFocus(true);

    for (juce::Component *c : std::initializer_list<juce::Component *>{
             &search_, &favorites_, &list_, &saveButton_, &mutateButton_, &deleteButton_,
             &folderButton_, &status_})
    {
        addAndMakeVisible(*c);
    }
    addChildComponent(*savePanel_);

    browse = [this](const Browse &b, std::function<void(const juce::File &)> done) {
        chooser_ = std::make_unique<juce::FileChooser>(b.title, b.initial, "");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectDirectories,
                              [done = std::move(done)](const juce::FileChooser &chooser) {
                                  done(chooser.getResult());
                              });
    };
}

PresetBrowser::~PresetBrowser()
{
    search_.removeKeyListener(this);
    list_.setModel(nullptr);
}

void PresetBrowser::paint(juce::Graphics &g)
{
    g.fillAll(theme::kBackground.withAlpha(0.96f));
    g.setColour(theme::kOutline);
    g.drawRect(getLocalBounds());
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced(10, 8);
    auto top = area.removeFromTop(kControlHeight);
    favorites_.setBounds(top.removeFromRight(110));
    top.removeFromRight(kGap);
    search_.setBounds(top);
    area.removeFromTop(kGap);
    const int tagsHeight = flowTags(tagButtons_, area.getX(), area.getY(), area.getWidth(), true);
    area.removeFromTop(tagsHeight + (tagsHeight > 0 ? kGap : 0));
    status_.setBounds(area.removeFromBottom(18));
    area.removeFromBottom(kGap);
    auto buttons = area.removeFromBottom(kControlHeight);
    saveButton_.setBounds(buttons.removeFromLeft(90));
    buttons.removeFromLeft(kGap);
    mutateButton_.setBounds(buttons.removeFromLeft(70));
    buttons.removeFromLeft(kGap);
    deleteButton_.setBounds(buttons.removeFromLeft(70));
    buttons.removeFromLeft(kGap);
    folderButton_.setBounds(buttons.removeFromLeft(140));
    area.removeFromBottom(kGap);
    list_.setBounds(area);
    savePanel_->setBounds(getLocalBounds());
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        notice_.clear();
        deleteArmed_ = false;
        reload();
        if (isShowing())
        {
            search_.grabKeyboardFocus();
        }
    }
}

bool PresetBrowser::keyPressed(const juce::KeyPress &key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

bool PresetBrowser::keyPressed(const juce::KeyPress &key, juce::Component *)
{
    // From the search field, which would move its caret: Up and Down move the list.
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        const int row = list_.getSelectedRow();
        const int next = key == juce::KeyPress::downKey ? row + 1 : (row < 0 ? 0 : row - 1);
        if (next >= 0 && next < getNumRows())
        {
            selectRow(next);
            loadRow(next);
        }
        return true;
    }
    return false;
}

void PresetBrowser::close() { setVisible(false); }

// ---- the list ----

int PresetBrowser::getNumRows() { return static_cast<int>(visible_.size()); }

juce::Component *PresetBrowser::refreshComponentForRow(int row, bool selected,
                                                       juce::Component *existing)
{
    if (row < 0 || row >= getNumRows())
    {
        delete existing;
        return nullptr;
    }
    auto *component = dynamic_cast<Row *>(existing);
    if (component == nullptr)
    {
        delete existing;
        component = new Row(*this);
    }
    component->update(row, selected);
    return component;
}

void PresetBrowser::selectedRowsChanged(int lastRowSelected)
{
    deleteArmed_ = false;
    deleteButton_.setButtonText("Delete");
    updateButtons();
    if (!settingSelection_ && lastRowSelected >= 0 && lastRowSelected < getNumRows())
    {
        loadRow(lastRowSelected);
    }
}

void PresetBrowser::selectRow(int row)
{
    const bool was = settingSelection_;
    settingSelection_ = true;
    if (row >= 0)
    {
        list_.selectRow(row);
    }
    else
    {
        list_.deselectAllRows();
    }
    settingSelection_ = was;
}

int PresetBrowser::loadedRow() const
{
    for (std::size_t i = 0; i < visible_.size(); ++i)
    {
        if (rows_[visible_[i]].loaded)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool PresetBrowser::loadRow(int row)
{
    if (row < 0 || row >= getNumRows() || !context_.loadPreset)
    {
        return false;
    }
    const PresetRow &chosen = rows_[visible_[static_cast<std::size_t>(row)]];
    const std::string error = context_.loadPreset(chosen.index);
    if (!error.empty())
    {
        setNotice("can't load " + fromUtf8(chosen.name) + ": " + fromUtf8(error));
        return false;
    }
    // Only this entry is loaded now: no new listing (a click would then rescan the folders).
    const std::size_t index = chosen.index;
    for (auto &r : rows_)
    {
        r.loaded = r.index == index;
    }
    lastName_ = context_.presetName ? context_.presetName() : std::string();
    setNotice({});
    list_.repaint();
    updateButtons();
    return true;
}

bool PresetBrowser::step(int direction)
{
    reload();
    const int count = getNumRows();
    if (count == 0)
    {
        return false;
    }
    const int at = loadedRow();
    const int next =
        at < 0 ? (direction > 0 ? 0 : count - 1) : ((at + (direction > 0 ? 1 : count - 1)) % count);
    selectRow(next);
    return loadRow(next);
}

void PresetBrowser::toggleFavorite(int row)
{
    if (row < 0 || row >= getNumRows() || !context_.setFavorite)
    {
        return;
    }
    const PresetRow &r = rows_[visible_[static_cast<std::size_t>(row)]];
    context_.setFavorite(r.index, !r.favorite);
    // Listed again, so the star shows what was saved. Under the Favorites filter, unstarring
    // drops the row, which would destroy the button whose click this is: after it returns.
    if (favorites_.getToggleState() && r.favorite)
    {
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<PresetBrowser>(this)] {
            if (safe != nullptr)
            {
                safe->reload();
            }
        });
    }
    else
    {
        reload();
    }
}

void PresetBrowser::deleteSelected()
{
    const int row = list_.getSelectedRow();
    if (row < 0 || row >= getNumRows() || !context_.deletePreset)
    {
        return;
    }
    const PresetRow &r = rows_[visible_[static_cast<std::size_t>(row)]];
    if (r.isInit || r.factory)
    {
        return;
    }
    if (!deleteArmed_)
    {
        deleteArmed_ = true;
        deleteButton_.setButtonText("Confirm");
        setNotice("Delete " + fromUtf8(r.name) + "? Click Confirm");
        return;
    }
    const juce::String name = fromUtf8(r.name);
    const bool removed = context_.deletePreset(r.index);
    deleteArmed_ = false;
    deleteButton_.setButtonText("Delete");
    setNotice(removed ? "deleted " + name : "can't delete " + name);
    reload();
}

std::string PresetBrowser::save(const std::string &name, const std::vector<std::string> &tags,
                                const std::string &description, bool overwrite)
{
    if (!context_.savePreset)
    {
        return "can't save";
    }
    const std::string error = context_.savePreset(name, tags, description, overwrite);
    if (error.empty())
    {
        setNotice("saved " + fromUtf8(name));
        reload();
    }
    return error;
}

void PresetBrowser::chooseContentFolder()
{
    browse({"Choose the content folder", juce::File()}, [this](const juce::File &folder) {
        if (folder == juce::File() || !context_.setContentFolder)
        {
            return;
        }
        const std::string error = context_.setContentFolder(folder.getFullPathName().toStdString());
        setNotice(error.empty() ? "content folder: " + folder.getFullPathName() : fromUtf8(error));
        reload();
    });
}

// ---- the filtered list ----

void PresetBrowser::reload()
{
    rows_ = context_.presetRows ? context_.presetRows() : std::vector<PresetRow>();
    lastName_ = context_.presetName ? context_.presetName() : std::string();
    rebuildTags();
    applyFilter();
    updateStatus();
}

void PresetBrowser::rebuildTags()
{
    // The four focus tags always, then the vocabulary's others that a listed preset carries.
    std::vector<std::string> shown;
    for (std::size_t i = 0; i < context_.tagVocabulary.size(); ++i)
    {
        const std::string &word = context_.tagVocabulary[i];
        const bool used = std::any_of(rows_.begin(), rows_.end(), [&](const PresetRow &r) {
            return std::find(r.tags.begin(), r.tags.end(), word) != r.tags.end();
        });
        if (i < kFocusTags || used)
        {
            shown.push_back(word);
        }
    }
    if (shown == shownTags_)
    {
        return;
    }
    shownTags_ = shown;
    tagButtons_.clear();
    std::erase_if(selectedTags_, [&](const std::string &word) {
        return std::find(shown.begin(), shown.end(), word) == shown.end();
    });
    for (const auto &word : shown)
    {
        auto button = makeTagButton(word, "preset.tag.");
        button->setToggleState(selectedTags_.count(word) != 0, juce::dontSendNotification);
        button->onClick = [this, word, raw = button.get()] {
            if (raw->getToggleState())
            {
                selectedTags_.insert(word);
            }
            else
            {
                selectedTags_.erase(word);
            }
            applyFilter();
        };
        addAndMakeVisible(*button);
        tagButtons_.push_back(std::move(button));
    }
    resized();
}

bool PresetBrowser::matches(const PresetRow &row) const
{
    if (favorites_.getToggleState() && !row.favorite)
    {
        return false;
    }
    for (const auto &tag : selectedTags_)
    {
        if (std::find(row.tags.begin(), row.tags.end(), tag) == row.tags.end())
        {
            return false;
        }
    }
    const juce::String needle = search_.getText().trim();
    if (needle.isEmpty())
    {
        return true;
    }
    if (fromUtf8(row.name).containsIgnoreCase(needle) ||
        fromUtf8(row.description).containsIgnoreCase(needle))
    {
        return true;
    }
    return std::any_of(row.tags.begin(), row.tags.end(), [&](const std::string &tag) {
        return fromUtf8(tag).containsIgnoreCase(needle);
    });
}

void PresetBrowser::applyFilter()
{
    visible_.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        if (matches(rows_[i]))
        {
            visible_.push_back(i);
        }
    }
    list_.updateContent();
    selectRow(loadedRow());
    list_.repaint();
    deleteArmed_ = false;
    deleteButton_.setButtonText("Delete");
    updateButtons();
}

void PresetBrowser::refresh()
{
    if (!isVisible() || !context_.presetName)
    {
        return;
    }
    if (context_.presetName() != lastName_)
    {
        reload(); // the host loaded a preset, or a save changed the name
    }
}

void PresetBrowser::setNotice(const juce::String &text)
{
    notice_ = text;
    updateStatus();
}

void PresetBrowser::updateStatus()
{
    if (notice_.isNotEmpty())
    {
        status_.setText(notice_, juce::dontSendNotification);
    }
    else
    {
        status_.setText(context_.contentStatus ? fromUtf8(context_.contentStatus())
                                               : juce::String(),
                        juce::dontSendNotification);
    }
    status_.setTooltip(status_.getText());
}

void PresetBrowser::updateButtons()
{
    const int row = list_.getSelectedRow();
    bool deletable = false;
    if (row >= 0 && row < getNumRows())
    {
        const PresetRow &r = rows_[visible_[static_cast<std::size_t>(row)]];
        deletable = !r.isInit && !r.factory;
    }
    deleteButton_.setEnabled(deletable);
}
} // namespace gg::ui
