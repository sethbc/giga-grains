// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The preset strip and the preset browser in the editor (M2 sub-project 5's spec §6.8), driven
// through the editor an EditorPlugin creates: its components are found by ID, and a click is a
// button's triggerClick or a list row's selection. Each case works on a scratch machine of its
// own under GG_TEST_OUTPUT_DIR (the content folder, the settings file and so the user presets
// folder and the favorites, with the install roots' environment pointed away), restored when it
// ends; each case runs in a process of its own. A preset load calls state.mark_dirty, and a
// browser load tells the host with preset_load.loaded: both pinned here with a stub host.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <clap/clap.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <sst/jucegui/components/Knob.h>
#include <sst/jucegui/data/Continuous.h>

#include "PluginHarness.h"
#include "PresetBrowser.h"
#include "editor/EditorPlugin.h"
#include "gg/core/ParamIds.h"
#include "gg/core/Preset.h"
#include "gg/core/PresetInfo.h"
#include "gg/media/ContentPaths.h"
#include "gg/media/SettingsFile.h"
#include "gg/ui/Editor.h"
#include "support/Utf8.h"

namespace
{
namespace fs = std::filesystem;
using gg::core::Mode;
using gg::core::ParamSlot;
using gg::core::PresetInfo;

/// Sets (or, given nullptr, unsets) an environment variable for its lifetime.
class ScopedEnv
{
  public:
    ScopedEnv(const char *name, const char *value) : name_(name)
    {
        if (const char *old = std::getenv(name); old != nullptr)
        {
            old_ = old;
        }
        set(value);
    }
    ~ScopedEnv() { set(old_ ? old_->c_str() : nullptr); }
    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

  private:
    void set(const char *value) const
    {
#if defined(_WIN32)
        (void)::_putenv_s(name_, value != nullptr ? value : "");
#else
        if (value != nullptr)
        {
            ::setenv(name_, value, 1);
        }
        else
        {
            ::unsetenv(name_);
        }
#endif
    }
    const char *name_;
    std::optional<std::string> old_;
};

/// A scratch machine: `<root>/content` is GG_CONTENT_DIR, `<root>/settings/settings.xml` the
/// settings file (GG_SETTINGS_FILE; the user presets folder is `<root>/settings/Presets`), and
/// the install roots are `<root>/home/system` (GG_SYSTEM_CONTENT_DIR), which holds nothing.
class Machine
{
  public:
    explicit Machine(const char *name)
        : root_(fresh(name)), contentEnv_("GG_CONTENT_DIR", gg::test::utf8(content()).c_str()),
          settingsEnv_("GG_SETTINGS_FILE", gg::test::utf8(settingsFile()).c_str()),
          systemEnv_("GG_SYSTEM_CONTENT_DIR", gg::test::utf8(root_ / "home" / "system").c_str())
    {
    }

    fs::path root() const { return root_; }
    fs::path content() const { return root_ / "content"; }
    fs::path settingsFile() const { return root_ / "settings" / "settings.xml"; }
    fs::path user() const { return gg::media::userPresetsFolder(settingsFile()); }
    fs::path factory() const { return gg::media::factoryPresetsFolder(content()); }

  private:
    static fs::path fresh(const char *name)
    {
        const auto dir = fs::path(GG_TEST_OUTPUT_DIR) / "browser" / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        return dir;
    }

    fs::path root_;
    ScopedEnv contentEnv_;
    ScopedEnv settingsEnv_;
    ScopedEnv systemEnv_; // the install roots (the tests' seam, ContentPaths.h)
};

/// Writes the preset `name` for FX, with `tags` and the Size macro's value `size`.
void putPreset(const fs::path &folder, const std::string &name,
               const std::vector<std::string> &tags, float size)
{
    PresetInfo info;
    info.name = name;
    info.product = Mode::Fx;
    info.tags = tags;
    auto state = gg::core::initState(Mode::Fx);
    state.values[static_cast<std::size_t>(ParamSlot::Macro1)] = size;
    fs::create_directories(folder);
    gg::test::writeBytes(folder / (name + ".ggpreset"), gg::core::encodePreset(info, state));
}

/// Factory "FX Test" (rhythmic) and user "My Pad" (texture), as in a machine with content.
void putPresets(const Machine &machine)
{
    putPreset(machine.factory(), "FX Test", {"rhythmic"}, 0.25f);
    putPreset(machine.user(), "My Pad", {"texture", "playable"}, 0.75f);
}

/// A host that offers clap.preset-load and clap.state, and records the plugin's loaded and
/// on_error calls and its state.mark_dirty calls.
class BrowserHost
{
  public:
    struct Call
    {
        bool loaded{false};
        uint32_t kind{0};
        std::optional<std::string> location, loadKey;
    };

    BrowserHost() { host_.host_data = this; }
    BrowserHost(const BrowserHost &) = delete;
    BrowserHost &operator=(const BrowserHost &) = delete;

    const clap_host *clap() const noexcept { return &host_; }
    const std::vector<Call> &calls() const noexcept { return calls_; }
    int dirtyMarks() const noexcept { return dirty_; }
    void clear() noexcept { calls_.clear(); }

  private:
    static BrowserHost &self(const clap_host *host) noexcept
    {
        return *static_cast<BrowserHost *>(host->host_data);
    }
    static std::optional<std::string> copied(const char *text)
    {
        return text != nullptr ? std::optional<std::string>(text) : std::nullopt;
    }
    static const void *getExtension(const clap_host *, const char *id) noexcept
    {
        static const clap_host_preset_load kPresetLoad{
            [](const clap_host *host, uint32_t kind, const char *location, const char *loadKey,
               int32_t, const char *) {
                self(host).calls_.push_back({false, kind, copied(location), copied(loadKey)});
            },
            [](const clap_host *host, uint32_t kind, const char *location, const char *loadKey) {
                self(host).calls_.push_back({true, kind, copied(location), copied(loadKey)});
            }};
        static const clap_host_state kState{[](const clap_host *host) { ++self(host).dirty_; }};
        if (std::strcmp(id, CLAP_EXT_PRESET_LOAD) == 0)
        {
            return &kPresetLoad;
        }
        return std::strcmp(id, CLAP_EXT_STATE) == 0 ? &kState : nullptr;
    }

    std::vector<Call> calls_;
    int dirty_{0};
    clap_host host_{CLAP_VERSION_INIT,
                    nullptr,
                    "gg-tests browser host",
                    "Giga Grains",
                    "",
                    "0.0.0",
                    &BrowserHost::getExtension,
                    [](const clap_host *) {},
                    [](const clap_host *) {},
                    [](const clap_host *) {}};
};

/// The component with this ID anywhere under `parent`, or null.
juce::Component *find(juce::Component &parent, const char *id)
{
    for (juce::Component *child : parent.getChildren())
    {
        if (child->getComponentID() == id)
        {
            return child;
        }
        if (juce::Component *found = find(*child, id))
        {
            return found;
        }
    }
    return nullptr;
}

/// Types `text` into `editor` now (TextEditor::setText notifies later): its handler runs at once.
void type(juce::TextEditor &editor, const juce::String &text)
{
    editor.setText(text, false);
    if (editor.onTextChange)
    {
        editor.onTextChange();
    }
}

/// A click now (Button::triggerClick posts a message): a toggle flips and calls its handler.
void click(juce::Button &button)
{
    if (button.getClickingTogglesState())
    {
        button.setToggleState(!button.getToggleState(), juce::sendNotification);
    }
    else if (button.onClick)
    {
        button.onClick();
    }
}

template <class T> T &need(juce::Component &parent, const char *id)
{
    auto *component = dynamic_cast<T *>(find(parent, id));
    REQUIRE(component != nullptr);
    return *component;
}

/// An editor over a plugin made for the scratch machine, with the pieces the cases drive.
struct Fixture
{
    explicit Fixture(const char *id = "org.sethbc.giga-grains-fx",
                     const clap_host *host = gg::test::stubHost())
        : plugin(id, host)
    {
        REQUIRE(plugin.valid());
        impl = dynamic_cast<gg::plugin::EditorPlugin *>(&plugin.impl());
        REQUIRE(impl != nullptr);
        editor = impl->createEditor();
        REQUIRE(editor != nullptr);
        editor->setSize(900, 500);
    }

    juce::TextButton &button(const char *id) { return need<juce::TextButton>(*editor, id); }
    juce::ListBox &list() { return need<juce::ListBox>(*editor, "preset.list"); }
    gg::ui::PresetBrowser &browser()
    {
        return need<gg::ui::PresetBrowser>(*editor, "preset.browser");
    }
    void openBrowser()
    {
        click(button("preset.name"));
        REQUIRE(browser().isVisible());
    }
    int rows() { return list().getListBoxModel()->getNumRows(); }
    juce::String rowName(int row)
    {
        auto *component = list().getComponentForRowNumber(row);
        REQUIRE(component != nullptr);
        return component->getTitle();
    }
    /// The 30 Hz tick, by hand.
    void tick() { dynamic_cast<juce::Timer &>(*editor).timerCallback(); }
    juce::String nameText() { return button("preset.name").getButtonText(); }

    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::PluginUnderTest plugin;
    gg::plugin::EditorPlugin *impl{nullptr};
    std::unique_ptr<juce::Component> editor;
};

fs::path file(const fs::path &folder, const std::string &name)
{
    return folder / (name + ".ggpreset");
}
} // namespace

TEST_CASE("the header's preset strip keeps the label's place", "[plugin][ui]")
{
    const Machine machine("strip");
    Fixture f;
    auto &strip = *find(*f.editor, "header.preset");
    CHECK(strip.getX() == 162);
    CHECK(strip.getWidth() == 90);
    CHECK(strip.getParentComponent() == f.editor.get());
    CHECK(find(strip, "preset.mutate") == nullptr); // Mutate is the browser's (spec §6.9)
    CHECK(find(strip, "preset.prev") != nullptr);
    CHECK(find(strip, "preset.next") != nullptr);
    CHECK(f.nameText() == "Init");
    CHECK_FALSE(f.browser().isVisible());
}

TEST_CASE("the browser lists Init, the factory preset and a user preset", "[plugin][ui]")
{
    const Machine machine("list");
    putPresets(machine);
    Fixture f;
    f.openBrowser();
    REQUIRE(f.rows() == 3);
    CHECK(f.rowName(0) == "Init");
    CHECK(f.rowName(1) == "FX Test");
    CHECK(f.rowName(2) == "My Pad");
    // The loaded row is Init's.
    CHECK(f.list().getSelectedRow() == 0);
    // The name button toggles the browser.
    click(f.button("preset.name"));
    CHECK_FALSE(f.browser().isVisible());
}

TEST_CASE("a row loads its preset, tells the host and marks the state dirty", "[plugin][ui]")
{
    const Machine machine("load");
    putPresets(machine);
    BrowserHost host;
    Fixture f("org.sethbc.giga-grains-fx", host.clap());
    f.openBrowser();
    REQUIRE(f.rows() == 3);

    f.list().selectRow(1);
    CHECK(f.impl->presetName() == "FX Test");
    CHECK(f.impl->uiBaseValue(ParamSlot::Macro1) == 0.25f);
    REQUIRE(host.calls().size() == 1);
    const auto location = gg::test::utf8(file(machine.factory(), "FX Test"));
    CHECK(host.calls()[0].loaded);
    CHECK(host.calls()[0].kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE);
    CHECK(host.calls()[0].location == location);
    CHECK_FALSE(host.calls()[0].loadKey.has_value());
    CHECK(host.dirtyMarks() >= 1);

    // The strip and the browser follow at the next tick.
    f.tick();
    CHECK(f.nameText() == "FX Test");
    CHECK(f.list().getSelectedRow() == 1);

    // INIT is announced by its load key.
    host.clear();
    f.list().selectRow(0);
    CHECK(f.impl->presetName() == "Init");
    REQUIRE(host.calls().size() == 1);
    CHECK(host.calls()[0].loaded);
    CHECK(host.calls()[0].kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN);
    CHECK_FALSE(host.calls()[0].location.has_value());
    CHECK(host.calls()[0].loadKey == "init");
}

TEST_CASE("a host's preset-load and INIT mark the state dirty", "[plugin][ui]")
{
    const Machine machine("dirty");
    putPresets(machine);
    BrowserHost host;
    gg::test::PluginUnderTest plugin("org.sethbc.giga-grains-fx", host.clap());
    REQUIRE(plugin.valid());
    const auto location = gg::test::utf8(file(machine.factory(), "FX Test"));
    const int before = host.dirtyMarks();
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, location.c_str(), nullptr));
    CHECK(host.dirtyMarks() == before + 1);
    REQUIRE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "init"));
    CHECK(host.dirtyMarks() == before + 2);
    // A preset that fails to load changes nothing, so marks nothing.
    const auto missing = gg::test::utf8(file(machine.factory(), "Nope"));
    CHECK_FALSE(plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, missing.c_str(), nullptr));
    CHECK(host.dirtyMarks() == before + 2);
}

TEST_CASE("search and tag toggles narrow the list", "[plugin][ui]")
{
    const Machine machine("filter");
    putPresets(machine);
    Fixture f;
    f.openBrowser();
    REQUIRE(f.rows() == 3);

    auto &search = need<juce::TextEditor>(*f.editor, "preset.search");
    type(search, "pad");
    REQUIRE(f.rows() == 1);
    CHECK(f.rowName(0) == "My Pad");
    type(search, "RHYTHM"); // a tag, ignoring case
    REQUIRE(f.rows() == 1);
    CHECK(f.rowName(0) == "FX Test");
    type(search, "");
    CHECK(f.rows() == 3);

    // The focus tags are always there; "playable" is in use by a preset; "drone" is not.
    CHECK(find(*f.editor, "preset.tag.texture") != nullptr);
    CHECK(find(*f.editor, "preset.tag.playable") != nullptr);
    CHECK(find(*f.editor, "preset.tag.drone") == nullptr);
    click(f.button("preset.tag.texture"));
    REQUIRE(f.rows() == 1);
    CHECK(f.rowName(0) == "My Pad");
    click(f.button("preset.tag.rhythmic")); // both: no preset has them
    CHECK(f.rows() == 0);
    click(f.button("preset.tag.texture"));
    REQUIRE(f.rows() == 1);
    CHECK(f.rowName(0) == "FX Test");
}

TEST_CASE("the star keeps a favorite and the Favorites filter shows it", "[plugin][ui]")
{
    const Machine machine("favorites");
    putPresets(machine);
    Fixture f;
    f.openBrowser();
    REQUIRE(f.rows() == 3);

    auto *row = f.list().getComponentForRowNumber(2);
    REQUIRE(row != nullptr);
    click(need<juce::TextButton>(*row, "preset.star"));
    click(f.button("preset.favorites"));
    REQUIRE(f.rows() == 1);
    CHECK(f.rowName(0) == "My Pad");
    // Saved: a new catalog, made from the settings file as a new editor's would be, sees it.
    click(f.button("preset.favorites"));
    REQUIRE(f.rows() == 3);
    const gg::plugin::PresetCatalog fresh(Mode::Fx, machine.settingsFile());
    REQUIRE(fresh.entries().size() == 3);
    CHECK(fresh.entries()[2].favorite);
    CHECK_FALSE(fresh.entries()[1].favorite);
}

TEST_CASE("Save as writes a user preset and lists it, asking before it replaces", "[plugin][ui]")
{
    const Machine machine("save");
    putPresets(machine);
    Fixture f;
    f.openBrowser();
    auto &panel = *find(*f.editor, "preset.savepanel");

    click(f.button("preset.save"));
    REQUIRE(panel.isVisible());
    type(need<juce::TextEditor>(panel, "preset.save.name"), "Fresh Sound");
    type(need<juce::TextEditor>(panel, "preset.save.description"), "a test");
    click(f.button("preset.save.tag.extreme"));
    click(f.button("preset.save.ok"));
    CHECK_FALSE(panel.isVisible());
    const auto saved = file(machine.user(), "Fresh Sound");
    REQUIRE(fs::exists(saved));
    REQUIRE(f.rows() == 4);
    CHECK(f.rowName(2) == "Fresh Sound"); // the user folder is by file name
    CHECK(f.impl->presetName() == "Fresh Sound");
    CHECK_FALSE(f.impl->presetEdited());
    PresetInfo info;
    const auto size = fs::file_size(saved);
    std::string bytes(size, '\0');
    {
        std::ifstream in(saved, std::ios::binary);
        in.read(bytes.data(), static_cast<std::streamsize>(size));
    }
    REQUIRE(gg::core::presetInfoOf(bytes, info));
    CHECK(info.name == "Fresh Sound");
    CHECK(info.description == "a test");
    CHECK(info.tags == std::vector<std::string>{"extreme"});

    // The same name again asks first; Cancel leaves the file alone.
    click(f.button("preset.save"));
    type(need<juce::TextEditor>(panel, "preset.save.name"), "Fresh Sound");
    click(f.button("preset.save.ok"));
    CHECK(panel.isVisible());
    CHECK(f.button("preset.save.ok").getButtonText() == "Replace");
    CHECK(need<juce::Label>(panel, "preset.save.message").getText().contains("Replace"));
    click(f.button("preset.save.cancel"));
    CHECK_FALSE(panel.isVisible());
    CHECK(fs::file_size(saved) == size);

    // Confirmed, it is replaced (now with no tag).
    click(f.button("preset.save"));
    type(need<juce::TextEditor>(panel, "preset.save.name"), "Fresh Sound");
    click(f.button("preset.save.ok"));
    click(f.button("preset.save.ok"));
    CHECK_FALSE(panel.isVisible());
    CHECK(f.rows() == 4);

    // A name the file system refuses is made safe, never written outside the folder.
    click(f.button("preset.save"));
    type(need<juce::TextEditor>(panel, "preset.save.name"), "../Escape");
    click(f.button("preset.save.ok"));
    CHECK_FALSE(fs::exists(machine.root() / "settings" / "Escape.ggpreset"));
}

TEST_CASE("Delete removes a user preset after a second click and never a factory one",
          "[plugin][ui]")
{
    const Machine machine("delete");
    putPresets(machine);
    Fixture f;
    f.openBrowser();
    auto &del = f.button("preset.delete");

    f.list().selectRow(1); // the factory preset
    CHECK_FALSE(del.isEnabled());
    f.list().selectRow(2);
    REQUIRE(del.isEnabled());
    click(del);
    CHECK(fs::exists(file(machine.user(), "My Pad"))); // asked, not done
    CHECK(del.getButtonText() == "Confirm");
    click(del);
    CHECK_FALSE(fs::exists(file(machine.user(), "My Pad")));
    CHECK(f.rows() == 2);
    CHECK(fs::exists(file(machine.factory(), "FX Test")));
}

TEST_CASE("the edited dot follows a macro drag and clears on reload", "[plugin][ui]")
{
    const Machine machine("edited");
    putPresets(machine);
    Fixture f;
    REQUIRE(f.plugin.activate(48000.0, 64));
    gg::test::Stereo in(64);
    gg::test::Stereo out(64);
    CHECK_FALSE(f.nameText().endsWith(juce::String::fromUTF8(" \xe2\x80\xa2")));

    auto &knob = need<sst::jucegui::components::Knob>(*f.editor, "macro.1");
    REQUIRE(knob.continuous() != nullptr);
    knob.continuous()->setValueFromGUI(0.9f);
    REQUIRE(f.plugin.process(64, in.data(), out.data()) == CLAP_PROCESS_CONTINUE);
    f.tick();
    CHECK(f.nameText().endsWith(juce::String::fromUTF8(" \xe2\x80\xa2")));

    f.openBrowser();
    f.list().selectRow(1);
    REQUIRE(f.plugin.process(64, in.data(), out.data()) == CLAP_PROCESS_CONTINUE);
    f.tick();
    CHECK(f.nameText() == "FX Test");
    CHECK_FALSE(f.impl->presetEdited());
}

TEST_CASE("the strip's arrows load the neighbours in the filtered list", "[plugin][ui]")
{
    const Machine machine("arrows");
    putPresets(machine);
    Fixture f;
    CHECK(f.nameText() == "Init");
    click(f.button("preset.next"));
    CHECK(f.impl->presetName() == "FX Test");
    click(f.button("preset.next"));
    CHECK(f.impl->presetName() == "My Pad");
    click(f.button("preset.prev"));
    CHECK(f.impl->presetName() == "FX Test");
    click(f.button("preset.prev"));
    CHECK(f.impl->presetName() == "Init");
    f.tick();
    CHECK(f.nameText() == "Init");

    // Through the filter: only the texture presets, so next from Init is My Pad.
    f.openBrowser();
    click(f.button("preset.tag.texture"));
    click(f.button("preset.next"));
    CHECK(f.impl->presetName() == "My Pad");
}

TEST_CASE("Escape closes the browser", "[plugin][ui]")
{
    const Machine machine("escape");
    Fixture f;
    f.openBrowser();
    CHECK(f.browser().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(f.browser().isVisible());
}

TEST_CASE("the status line without content shows the install hint", "[plugin][ui]")
{
    const Machine machine("nocontent");
    Fixture f;
    f.openBrowser();
    REQUIRE(f.rows() == 1);
    CHECK(f.rowName(0) == "Init");
    CHECK(need<juce::Label>(*f.editor, "preset.status").getText().contains("No factory content"));
    CHECK(need<juce::Label>(*f.editor, "preset.status").getText().contains("Content folder"));
}

TEST_CASE("Content folder saves the settings' folder and refuses a NUL byte", "[plugin][ui]")
{
    const Machine machine("folder");
    Fixture f;
    f.openBrowser();
    const auto &context = f.impl->editorContext();

    // A NUL byte is refused before anything is saved.
    CHECK_FALSE(context.setContentFolder(std::string("a\0b", 3)).empty());
    CHECK_FALSE(fs::exists(machine.settingsFile()));

    // The chooser's answer is saved as the chooser gives it: juce::File's full path, which on
    // Windows has native separators even where the scratch path (GG_TEST_OUTPUT_DIR) has '/'.
    const fs::path chosen = machine.root() / "My Pack";
    fs::create_directories(chosen);
    const juce::File answer(juce::String::fromUTF8(gg::test::utf8(chosen).c_str()));
    const std::string saved = answer.getFullPathName().toStdString();
#if defined(_WIN32)
    CHECK(saved.find('/') == std::string::npos);
#endif
    CHECK(gg::media::fromUtf8(saved) == chosen); // the same folder, compared as paths
    f.browser().browse = [&](const gg::ui::PresetBrowser::Browse &,
                             std::function<void(const juce::File &)> done) { done(answer); };
    click(f.button("preset.folder"));
    CHECK(gg::media::loadSettings(machine.settingsFile()).contentFolder == saved);
    CHECK(need<juce::Label>(*f.editor, "preset.status").getText().contains("My Pack"));

    // One the chooser didn't give leaves it as it was.
    f.browser().browse = [](const gg::ui::PresetBrowser::Browse &,
                            std::function<void(const juce::File &)> done) { done(juce::File()); };
    click(f.button("preset.folder"));
    CHECK(gg::media::loadSettings(machine.settingsFile()).contentFolder == saved);
}

// ---- The Mutate panel (spec §6.9) ----

namespace
{
std::string lockId(ParamSlot slot)
{
    return "mutate.lock." + std::to_string(static_cast<int>(slot));
}

juce::Component &panel(Fixture &f)
{
    auto *component = find(*f.editor, "preset.mutatePanel");
    REQUIRE(component != nullptr);
    return *component;
}

/// Opens the panel as the user does: the browser, then its Mutate button.
void openPanel(Fixture &f)
{
    f.openBrowser();
    click(need<juce::TextButton>(f.browser(), "preset.mutate"));
    REQUIRE(panel(f).isVisible());
}

juce::TextButton &lockRow(Fixture &f, ParamSlot slot)
{
    return need<juce::TextButton>(panel(f), lockId(slot).c_str());
}
} // namespace

TEST_CASE("the browser's Mutate button opens the panel with its knob, buttons and lock rows",
          "[plugin][ui][mutate]")
{
    const Machine machine("mutate-open");
    Fixture f;
    CHECK_FALSE(panel(f).isVisible());
    openPanel(f);
    CHECK_FALSE(f.browser().isVisible());

    auto &knob = need<sst::jucegui::components::Knob>(panel(f), "mutate.amount");
    REQUIRE(knob.continuous() != nullptr);
    CHECK(knob.continuous()->getValue() == 0.15f);
    CHECK(knob.continuous()->getMin() == 0.0f);
    CHECK(knob.continuous()->getMax() == 1.0f);
    CHECK(find(panel(f), "mutate.go") != nullptr);
    CHECK(find(panel(f), "mutate.reset") != nullptr);

    // The structural locks start on, the sound's parameters off.
    CHECK(lockRow(f, ParamSlot::Mix).getToggleState());
    CHECK_FALSE(lockRow(f, ParamSlot::Size).getToggleState());

    // The browser and the panel take turns: the name opens the browser over it, and Close
    // closes the panel.
    click(f.button("preset.name"));
    CHECK(f.browser().isVisible());
    CHECK_FALSE(panel(f).isVisible());
    click(need<juce::TextButton>(f.browser(), "preset.mutate"));
    CHECK(panel(f).isVisible());
    CHECK_FALSE(f.browser().isVisible());
    click(need<juce::TextButton>(panel(f), "mutate.close"));
    CHECK_FALSE(panel(f).isVisible());
    CHECK_FALSE(f.browser().isVisible());
}

TEST_CASE("Escape closes the Mutate panel", "[plugin][ui][mutate]")
{
    const Machine machine("mutate-escape");
    Fixture f;
    openPanel(f);
    CHECK(panel(f).getWantsKeyboardFocus());
    CHECK(panel(f).keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(panel(f).isVisible());
}

TEST_CASE("a lock toggle flips the plugin's lock and Reset locks restores the structural locks",
          "[plugin][ui][mutate]")
{
    const Machine machine("mutate-lock");
    Fixture f;
    openPanel(f);
    auto &size = lockRow(f, ParamSlot::Size);
    CHECK_FALSE(f.impl->mutateLocked(ParamSlot::Size));
    click(size);
    CHECK(f.impl->mutateLocked(ParamSlot::Size));
    click(size);
    CHECK_FALSE(f.impl->mutateLocked(ParamSlot::Size));

    click(size);
    auto &mix = lockRow(f, ParamSlot::Mix);
    click(mix);
    CHECK_FALSE(f.impl->mutateLocked(ParamSlot::Mix));
    click(f.button("mutate.reset"));
    CHECK(f.impl->mutateLocked(ParamSlot::Mix));
    CHECK_FALSE(f.impl->mutateLocked(ParamSlot::Size));
    // Layer B is off, but its locks are the user's alone: Reset leaves them off.
    CHECK_FALSE(f.impl->mutateLocked(ParamSlot::B_Size));
    f.tick();
    CHECK(mix.getToggleState());
    CHECK_FALSE(size.getToggleState());
}

TEST_CASE("an off layer's rows are dimmed apart from their locks", "[plugin][ui][mutate]")
{
    const Machine machine("mutate-offlayer");
    Fixture f;
    openPanel(f);
    f.tick();
    // Layer B is off by default: Mutate leaves it alone, which its rows show, dimmed, while its
    // own locks stay off.
    auto &bSize = lockRow(f, ParamSlot::B_Size);
    CHECK(bSize.getAlpha() < 1.0f);
    CHECK_FALSE(bSize.getToggleState());
    CHECK(lockRow(f, ParamSlot::Size).getAlpha() == 1.0f);

    // Switched on, it is mutated, and its rows are no longer dimmed.
    f.impl->uiBeginGesture(ParamSlot::LayerBOn);
    f.impl->uiSetValue(ParamSlot::LayerBOn, 1.0);
    f.impl->uiEndGesture(ParamSlot::LayerBOn);
    gg::test::InputEvents none;
    f.plugin.flush(none.list());
    REQUIRE(f.impl->uiBaseValue(ParamSlot::LayerBOn) == 1.0f);
    f.tick();
    CHECK(bSize.getAlpha() == 1.0f);
}

TEST_CASE("a layer's 16 steps lock together and a layer locks and unlocks whole",
          "[plugin][ui][mutate]")
{
    const Machine machine("mutate-steps");
    Fixture f;
    openPanel(f);
    // One row for layer A's step sequence (named by its Step 1), and none for Step 2 to 16.
    CHECK(find(panel(f), lockId(ParamSlot::Step2).c_str()) == nullptr);
    CHECK(find(panel(f), lockId(ParamSlot::Step16).c_str()) == nullptr);
    click(lockRow(f, ParamSlot::Step1));
    for (uint8_t i = 0; i < 16; ++i)
    {
        const auto slot = gg::core::layerSlot(
            static_cast<gg::core::LayerParam>(static_cast<int>(gg::core::LayerParam::Step1) + i),
            0);
        CHECK(f.impl->mutateLocked(slot));
    }
    // Layer B's steps are its own row.
    CHECK_FALSE(f.impl->mutateLocked(ParamSlot::B_Step1));
    CHECK_FALSE(f.impl->mutateLocked(ParamSlot::B_Step16));

    click(f.button("mutate.lockall.B"));
    for (uint8_t p = 0; p < gg::core::kLayerParamCount; ++p)
    {
        CHECK(f.impl->mutateLocked(gg::core::layerSlot(static_cast<gg::core::LayerParam>(p), 1)));
    }
    CHECK_FALSE(f.impl->mutateLocked(ParamSlot::C_Size));
    click(f.button("mutate.unlockall.B"));
    for (uint8_t p = 0; p < gg::core::kLayerParamCount; ++p)
    {
        CHECK_FALSE(
            f.impl->mutateLocked(gg::core::layerSlot(static_cast<gg::core::LayerParam>(p), 1)));
    }
}

TEST_CASE("the Mutate button moves the sound and marks the preset edited", "[plugin][ui][mutate]")
{
    const Machine machine("mutate-go");
    Fixture f;
    REQUIRE(f.plugin.activate(48000.0, 64));
    gg::test::Stereo in(64);
    gg::test::Stereo out(64);
    CHECK_FALSE(f.nameText().endsWith(juce::String::fromUTF8(" \xe2\x80\xa2")));
    openPanel(f);
    click(f.button("mutate.go"));
    REQUIRE(f.plugin.process(64, in.data(), out.data()) == CLAP_PROCESS_CONTINUE);
    f.tick();
    CHECK(f.impl->presetEdited());
    CHECK(f.nameText().endsWith(juce::String::fromUTF8(" \xe2\x80\xa2")));
}

TEST_CASE("EditorContext::sendValues is filled and its values arrive after a drain",
          "[plugin][ui][queue]")
{
    const Machine machine("sendvalues");
    Fixture f;
    const auto &context = f.impl->editorContext();
    REQUIRE(context.sendValues);
    const auto &sizeMeta = gg::core::paramMeta(ParamSlot::Size, f.impl->mode());
    const auto &pitchMeta = gg::core::paramMeta(ParamSlot::Pitch, f.impl->mode());
    const double size = static_cast<double>(
        static_cast<float>(0.25 * double(sizeMeta.minVal) + 0.75 * double(sizeMeta.maxVal)));
    const double pitch = static_cast<double>(
        static_cast<float>(0.75 * double(pitchMeta.minVal) + 0.25 * double(pitchMeta.maxVal)));
    context.sendValues({{ParamSlot::Size, size}, {ParamSlot::Pitch, pitch}});
    gg::test::InputEvents none;
    f.plugin.flush(none.list());
    CHECK(f.plugin.value(gg::core::paramId(ParamSlot::Size)) == size);
    CHECK(f.plugin.value(gg::core::paramId(ParamSlot::Pitch)) == pitch);
}

// ---- The fix wave's browser cases (I4, M2, M3, M4, T5) ----

TEST_CASE("a user preset named like a factory one is the loaded row after its save", "[plugin][ui]")
{
    // I4: the loaded row is the loaded entry, not the first of its name.
    const Machine machine("same-name");
    putPresets(machine);
    Fixture f;
    f.openBrowser();
    auto &panel = *find(*f.editor, "preset.savepanel");
    click(f.button("preset.save"));
    type(need<juce::TextEditor>(panel, "preset.save.name"), "FX Test");
    click(f.button("preset.save.ok"));
    REQUIRE_FALSE(panel.isVisible());
    REQUIRE(f.rows() == 4); // Init, FX Test (factory), FX Test (user), My Pad
    CHECK(f.list().getSelectedRow() == 2);
    CHECK(f.button("preset.delete").isEnabled());
    const auto index = f.impl->loadedCatalogIndex();
    REQUIRE(index.has_value());
    CHECK(f.impl->presetCatalog().entries()[*index].kind == gg::plugin::PresetKind::User);
    // The arrows step from it: next is My Pad, not the user's FX Test again.
    click(f.button("preset.next"));
    CHECK(f.impl->presetName() == "My Pad");
    click(f.button("preset.prev"));
    f.tick();
    CHECK(f.list().getSelectedRow() == 2);
    // A host's load of the factory file moves the mark to it, though the name is the same.
    const auto location = gg::test::utf8(file(machine.factory(), "FX Test"));
    REQUIRE(f.plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, location.c_str(), nullptr));
    f.tick();
    CHECK(f.list().getSelectedRow() == 1);
    CHECK_FALSE(f.button("preset.delete").isEnabled());
}

TEST_CASE("a session's preset is found by its name", "[plugin][ui]")
{
    const Machine machine("session-name");
    putPresets(machine);
    Fixture f;
    const auto location = gg::test::utf8(file(machine.user(), "My Pad"));
    REQUIRE(f.plugin.loadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, location.c_str(), nullptr));
    const std::string state = f.plugin.save();
    Fixture g;
    REQUIRE(g.plugin.load(state));
    g.openBrowser();
    CHECK(g.list().getSelectedRow() == 2);
}

TEST_CASE("the Replace prompt names the preset the file holds", "[plugin][ui]")
{
    // M2: "a/b" and "a:b" make one file, a_b.ggpreset.
    const Machine machine("replace-name");
    Fixture f;
    f.openBrowser();
    auto &panel = *find(*f.editor, "preset.savepanel");
    click(f.button("preset.save"));
    type(need<juce::TextEditor>(panel, "preset.save.name"), "a/b");
    click(f.button("preset.save.ok"));
    REQUIRE_FALSE(panel.isVisible());
    click(f.button("preset.save"));
    type(need<juce::TextEditor>(panel, "preset.save.name"), "a:b");
    click(f.button("preset.save.ok"));
    CHECK(panel.isVisible());
    CHECK(need<juce::Label>(panel, "preset.save.message").getText() ==
          "Replace the preset named a/b?");
}

TEST_CASE("the arrows step through the list without rescanning, the poll rescans", "[plugin][ui]")
{
    // M3 and M4.
    const Machine machine("poll");
    putPresets(machine);
    Fixture f;
    f.openBrowser();
    REQUIRE(f.rows() == 3);
    click(f.button("preset.name")); // closed
    putPreset(machine.user(), "Zed", {}, 0.5f);
    for (int i = 0; i < 4; ++i)
    {
        click(f.button("preset.next"));
        CHECK(f.impl->presetName() != "Zed"); // the list as it stood
    }
    // Open, it rescans; then the 30 Hz poll sees a file added while it is open.
    f.openBrowser();
    REQUIRE(f.rows() == 4);
    putPreset(machine.user(), "Zz Late", {}, 0.5f);
    f.tick();
    CHECK(f.rows() == 5);
    // The rescan took the change in: the poll sees nothing more until the folders change again.
    CHECK_FALSE(f.impl->editorContext().presetFoldersChanged());
}

TEST_CASE("the strip's Left and Right keys load the neighbours", "[plugin][ui]")
{
    const Machine machine("strip-keys");
    putPresets(machine);
    Fixture f;
    auto &strip = *find(*f.editor, "header.preset");
    CHECK(strip.getWantsKeyboardFocus());
    CHECK(strip.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK(f.impl->presetName() == "FX Test");
    CHECK(strip.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    CHECK(f.impl->presetName() == "Init");
    CHECK_FALSE(strip.keyPressed(juce::KeyPress('a')));
}

TEST_CASE("the browser's INIT marks the state dirty and tells the host", "[plugin][ui]")
{
    const Machine machine("init-dirty");
    putPresets(machine);
    BrowserHost host;
    Fixture f("org.sethbc.giga-grains-fx", host.clap());
    f.openBrowser();
    f.list().selectRow(1);
    const int before = host.dirtyMarks();
    host.clear();
    f.list().selectRow(0);
    CHECK(f.impl->presetName() == "Init");
    CHECK(host.dirtyMarks() == before + 1);
    REQUIRE(host.calls().size() == 1);
    CHECK(host.calls()[0].loadKey == "init");
}

TEST_CASE("a failed browser load tells the host nothing and shows why", "[plugin][ui]")
{
    const Machine machine("load-fails");
    putPresets(machine);
    BrowserHost host;
    Fixture f("org.sethbc.giga-grains-fx", host.clap());
    f.openBrowser();
    REQUIRE(f.rows() == 3);
    fs::remove(file(machine.user(), "My Pad")); // gone since the list was made
    host.clear();
    f.list().selectRow(2);
    CHECK(host.calls().empty());
    CHECK(f.impl->presetName() == "Init");
    CHECK(need<juce::Label>(*f.editor, "preset.status").getText().startsWith("can't load My Pad"));
    CHECK(f.list().getSelectedRow() == 0); // back on the loaded row
}

TEST_CASE("Content folder lists the chosen pack's presets as content", "[plugin][ui]")
{
    const Machine machine("folder-list");
    putPresets(machine);
    Fixture f;
    f.openBrowser();
    REQUIRE(f.rows() == 3);
    const fs::path pack = machine.root() / "Pack";
    putPreset(gg::media::factoryPresetsFolder(pack), "Theirs", {}, 0.5f);
    const juce::File answer(juce::String::fromUTF8(gg::test::utf8(pack).c_str()));
    f.browser().browse = [&](const gg::ui::PresetBrowser::Browse &,
                             std::function<void(const juce::File &)> done) { done(answer); };
    click(f.button("preset.folder"));
    REQUIRE(f.rows() == 4);
    CHECK(f.rowName(2) == "Theirs"); // after the factory's, before the user's
    f.list().selectRow(2);
    CHECK(f.impl->presetName() == "Theirs");
    CHECK_FALSE(f.button("preset.delete").isEnabled()); // the user's pack, not deletable here
}

// ---- The director's screenshots (UI-1, UI-2) ----

namespace
{
/// The editor drawn offscreen, at 1x.
juce::Image snapshot(Fixture &f)
{
    return f.editor->createComponentSnapshot(f.editor->getLocalBounds(), true, 1.0f);
}

/// Where the last macro knob draws its label (the bottom 20 px of its bounds, MacroRow), in the
/// editor's coordinates, 2 px in from each side.
juce::Rectangle<int> lastMacroLabel(Fixture &f)
{
    auto &knob = *find(*f.editor, "macro.8");
    const auto bounds = f.editor->getLocalArea(&knob, knob.getLocalBounds());
    return bounds.withTop(bounds.getBottom() - 20).reduced(2);
}

/// The pixels of `area` in `image` that aren't `colour`.
int pixelsNot(const juce::Image &image, juce::Rectangle<int> area, juce::Colour colour)
{
    int n = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
    {
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            n += image.getPixelAt(x, y).getARGB() != colour.getARGB() ? 1 : 0;
        }
    }
    return n;
}

bool sameArea(const juce::Image &a, const juce::Image &b, juce::Rectangle<int> area)
{
    for (int y = area.getY(); y < area.getBottom(); ++y)
    {
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            if (a.getPixelAt(x, y).getARGB() != b.getPixelAt(x, y).getARGB())
            {
                return false;
            }
        }
    }
    return true;
}

void checkOverlayHides(Fixture &f, const std::function<void()> &open)
{
    const juce::Colour background{0xff15171c}; // theme::kBackground
    const juce::Image perform = snapshot(f);
    const auto label = lastMacroLabel(f);
    REQUIRE(pixelsNot(perform, label, background) > 0); // the label's ink, with nothing over it
    open();
    const juce::Image covered = snapshot(f);
    CHECK(pixelsNot(covered, label, background) == 0); // none of it shows through
    // The header is untouched, so its strip stays usable.
    CHECK(sameArea(perform, covered, f.editor->getLocalBounds().removeFromTop(40)));
}
} // namespace

TEST_CASE("the browser hides the Perform view beneath it", "[plugin][ui]")
{
    // UI-1: the overlay is opaque over the canvas and the macro row.
    const Machine machine("overlay-browser");
    Fixture f;
    checkOverlayHides(f, [&] { f.openBrowser(); });
}

TEST_CASE("the Mutate panel hides the Perform view beneath it", "[plugin][ui][mutate]")
{
    const Machine machine("overlay-mutate");
    Fixture f;
    checkOverlayHides(f, [&] { openPanel(f); });
}

TEST_CASE("each layer's Lock all and Unlock all have a row of their own, in view",
          "[plugin][ui][mutate]")
{
    // UI-2: under the heading, inside the viewport's width (clear of its scroll bar), touching
    // neither the heading nor any lock row.
    const Machine machine("lockall-row");
    Fixture f;
    openPanel(f);
    auto &viewport = need<juce::Viewport>(panel(f), "mutate.viewport");
    auto &list = *find(panel(f), "mutate.lockList");
    const int usable = viewport.getWidth() - viewport.getScrollBarThickness();
    std::vector<juce::Rectangle<int>> rows;
    for (std::size_t s = 0; s < gg::core::kParamCount; ++s)
    {
        if (auto *row = find(list, lockId(static_cast<ParamSlot>(s)).c_str()))
        {
            rows.push_back(row->getBounds());
        }
    }
    REQUIRE(rows.size() > 50);
    for (const char *letter : {"A", "B", "C", "D"})
    {
        CAPTURE(letter);
        const auto heading =
            need<juce::Label>(list, (std::string("mutate.heading.") + letter).c_str()).getBounds();
        for (const char *kind : {"mutate.lockall.", "mutate.unlockall."})
        {
            const auto button =
                need<juce::TextButton>(list, (std::string(kind) + letter).c_str()).getBounds();
            CAPTURE(kind);
            CHECK(button.getX() >= 0);
            CHECK(button.getRight() <= usable);
            CHECK(button.getY() >= heading.getBottom()); // under its heading
            CHECK_FALSE(button.intersects(heading));
            for (const auto &row : rows)
            {
                CHECK_FALSE(button.intersects(row));
            }
        }
    }
    // No two lock rows overlap either.
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        for (std::size_t j = i + 1; j < rows.size(); ++j)
        {
            CHECK_FALSE(rows[i].intersects(rows[j]));
        }
    }
}
