// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The editor's frame (M2 sub-project 6's spec §6.2, §6.8–6.10): the four views in one editor, the
// header, undo and redo, the themes, the keys and the About box, built headless as
// factory_tests.cpp builds the views: JUCE's GUI is started, no window is opened, components
// are found by their IDs and driven by their own callbacks.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <sst/jucegui/accessibility/KeyboardTraverser.h>
#include <sst/jucegui/components/Knob.h>
#include <sst/jucegui/data/Continuous.h>

#include "GrainCanvas.h"
#include "MorphPad.h"
#include "Palette.h"
#include "ParamControl.h"
#include "PluginHarness.h"
#include "editor/EditorPlugin.h"
#include "editor/UiSettings.h"
#include "gg/core/ParamIds.h"
#include "gg/ui/EditLayout.h"
#include "gg/ui/Editor.h"
#include "gg/ui/LaneModel.h"
#include "gg/ui/LayerColours.h"
#include "gg/ui/UndoStack.h"

namespace
{
using gg::core::ParamSlot;
using gg::ui::ThemeChoice;
using gg::ui::View;

constexpr std::array<const char *, 2> kProducts{"org.sethbc.giga-grains",
                                                "org.sethbc.giga-grains-fx"};
constexpr std::array<View, 4> kViews{View::Perform, View::Edit, View::Mod, View::Fx};

const char *viewId(View v)
{
    switch (v)
    {
    case View::Perform:
        return "view.perform";
    case View::Edit:
        return "view.edit";
    case View::Mod:
        return "view.mod";
    case View::Fx:
        return "view.fx";
    }
    return "";
}

/// The component with this ID anywhere below `root` (depth first), or null.
juce::Component *findDeep(const juce::Component &root, const juce::String &id)
{
    for (juce::Component *child : root.getChildren())
    {
        if (child->getComponentID() == id)
        {
            return child;
        }
        if (juce::Component *found = findDeep(*child, id))
        {
            return found;
        }
    }
    return nullptr;
}

/// The view's own component: the editor's view stack's child with the view's ID.
juce::Component *viewOf(const juce::Component &editor, View v)
{
    juce::Component *stack = findDeep(editor, "views");
    REQUIRE(stack != nullptr);
    for (juce::Component *child : stack->getChildren())
    {
        if (child->getComponentID() == viewId(v))
        {
            return child;
        }
    }
    return nullptr;
}

juce::String controlId(ParamSlot slot) { return "param." + juce::String(gg::core::paramId(slot)); }

/// A mouse event at (x, y) in `c`'s coordinates, the left button down.
juce::MouseEvent mouseAt(juce::Component &c, float x, float y, bool dragged = false,
                         juce::Point<float> down = {})
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(), {x, y},
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
        juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
        juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
        juce::MouseInputSource::defaultTiltY, &c, &c, now,
        dragged ? down : juce::Point<float>{x, y}, now, 1, dragged);
}

/// A press and release at the middle of `c`.
void click(juce::Component &c)
{
    const auto centre = c.getLocalBounds().getCentre().toFloat();
    c.mouseDown(mouseAt(c, centre.x, centre.y));
    c.mouseUp(mouseAt(c, centre.x, centre.y));
}

/// Runs the editor's 30 Hz timer `n` times.
void tick(juce::Component &editor, int n = 1)
{
    auto *timer = dynamic_cast<juce::Timer *>(&editor);
    REQUIRE(timer != nullptr);
    for (int i = 0; i < n; ++i)
    {
        timer->timerCallback();
    }
}

juce::String tooltipOf(const juce::Component *c)
{
    const auto *client = dynamic_cast<const juce::TooltipClient *>(c);
    return client != nullptr ? const_cast<juce::TooltipClient *>(client)->getTooltip()
                             : juce::String("<no tooltip>");
}

juce::String labelText(const juce::Component &root, const char *id)
{
    const auto *label = dynamic_cast<const juce::Label *>(findDeep(root, id));
    return label != nullptr ? label->getText() : juce::String("<no label>");
}

/// Whether `c` and every component above it, below `root`, is visible: shown, were `root` on
/// screen (the tests open no window, so nothing is isShowing()).
bool visibleIn(const juce::Component &c, const juce::Component &root)
{
    for (const juce::Component *at = &c; at != nullptr && at != &root;
         at = at->getParentComponent())
    {
        if (!at->isVisible())
        {
            return false;
        }
    }
    return true;
}

/// Whether a juce::Viewport lies between `c` and `root`: `c` may be scrolled into view.
bool scrolledIn(const juce::Component &c, const juce::Component &root)
{
    for (const juce::Component *at = c.getParentComponent(); at != nullptr && at != &root;
         at = at->getParentComponent())
    {
        if (dynamic_cast<const juce::Viewport *>(at) != nullptr)
        {
            return true;
        }
    }
    return false;
}

template <typename T> void collect(const juce::Component &root, std::vector<T *> &out)
{
    for (juce::Component *child : root.getChildren())
    {
        if (auto *t = dynamic_cast<T *>(child))
        {
            out.push_back(t);
        }
        collect(*child, out);
    }
}

/// Every parameter's control shown in `view` (param.<id>, macro.<n> and the pad) lies inside it,
/// with room, unless it scrolls (D11).
void checkControlsFit(const juce::Component &view)
{
    std::vector<juce::Component *> all;
    collect(view, all);
    std::vector<const juce::Component *> controls;
    for (const juce::Component *c : all)
    {
        const juce::String id = c->getComponentID();
        if (id.startsWith("param.") || id.startsWith("macro.") || id == "pad")
        {
            controls.push_back(c);
        }
    }
    REQUIRE_FALSE(controls.empty());
    for (const juce::Component *c : controls)
    {
        if (!visibleIn(*c, view) || scrolledIn(*c, view))
        {
            continue;
        }
        INFO(c->getComponentID() << " (" << c->getTitle() << ")");
        const auto bounds = view.getLocalArea(c->getParentComponent(), c->getBounds());
        CHECK(view.getLocalBounds().contains(bounds));
        CHECK_FALSE(bounds.isEmpty());
    }
}

gg::plugin::EditorPlugin *editorPluginOf(gg::test::PluginUnderTest &instance)
{
    auto *plugin = dynamic_cast<gg::plugin::EditorPlugin *>(&instance.impl());
    REQUIRE(plugin != nullptr);
    return plugin;
}

/// A settings file of the test's own (GG_SETTINGS_FILE), which starts empty: the dark theme.
std::filesystem::path ownSettings(const std::string &name)
{
    const auto file = gg::test::sampleDir(name) / "settings.xml";
    std::filesystem::remove(file);
    gg::test::useSettingsFile(file);
    return file;
}

/// One block through the instance: stereo in for the FX, none for the instrument.
void processBlock(gg::test::PluginUnderTest &instance, bool fx,
                  const clap_output_events *events = nullptr)
{
    gg::test::Stereo in(64);
    gg::test::Stereo out(64);
    REQUIRE(instance.process(64, fx ? in.data() : nullptr, out.data(), nullptr, fx ? 2u : 0u,
                             events) == CLAP_PROCESS_CONTINUE);
}

/// The knob inside a parameter control.
sst::jucegui::components::Knob *knobOf(juce::Component *control)
{
    REQUIRE(control != nullptr);
    std::vector<sst::jucegui::components::Knob *> knobs;
    collect(*control, knobs);
    REQUIRE(knobs.size() == 1);
    return knobs[0];
}

/// A key with Command (Ctrl off macOS), and Shift if asked.
juce::KeyPress commandKey(int code, bool shift = false)
{
    return juce::KeyPress(
        code, juce::ModifierKeys::commandModifier | (shift ? juce::ModifierKeys::shiftModifier : 0),
        0);
}

/// The palette the editor shows now.
const gg::ui::Palette &paletteOf(const juce::Component &editor)
{
    const auto *owner = dynamic_cast<const gg::ui::PaletteOwner *>(&editor);
    REQUIRE(owner != nullptr);
    return owner->palette();
}
} // namespace

TEST_CASE("every view opens and closes in both products and themes", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ownSettings("frame-every-view");
    for (const char *id : kProducts)
    {
        const bool fx = std::string(id).ends_with("-fx");
        for (const ThemeChoice theme : {ThemeChoice::Dark, ThemeChoice::Light})
        {
            for (const View view : kViews)
            {
                INFO(id << ", theme " << int(theme) << ", " << viewId(view));
                gg::test::PluginUnderTest instance(id);
                REQUIRE(instance.valid());
                auto *plugin = editorPluginOf(instance);
                REQUIRE(instance.activate(48000.0, 512));
                processBlock(instance, fx);
                {
                    std::unique_ptr<juce::Component> editor = plugin->createEditor();
                    REQUIRE(editor != nullptr);
                    CHECK(plugin->publishing());
                    REQUIRE(gg::ui::setTheme(*editor, theme));
                    CHECK(&paletteOf(*editor) == (theme == ThemeChoice::Light
                                                      ? &gg::ui::Palette::light()
                                                      : &gg::ui::Palette::dark()));
                    REQUIRE(gg::ui::selectView(*editor, view));
                    CHECK(gg::ui::currentView(*editor) == view);
                    processBlock(instance, fx); // a snapshot and grains for the canvas
                    for (const auto size : {std::pair{960, 600}, std::pair{640, 360}})
                    {
                        INFO(size.first << " x " << size.second);
                        editor->setSize(size.first, size.second);
                        tick(*editor);
                        const juce::Image image =
                            editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                        CHECK(image.isValid());
                        CHECK(image.getWidth() == size.first);
                        CHECK(image.getHeight() == size.second);
                        for (const View other : kViews)
                        {
                            const juce::Component *v = viewOf(*editor, other);
                            REQUIRE(v != nullptr);
                            CHECK(v->isVisible() == (other == view));
                        }
                        if (size.first == 640)
                        {
                            checkControlsFit(*viewOf(*editor, view));
                        }
                    }
                }
                // Destroyed, the editor turned publishing off.
                CHECK_FALSE(plugin->publishing());
            }
        }
    }
}

TEST_CASE("every view opens on an inactive plugin", "[plugin]")
{
    // Review Focus 1: the plugin never activated, so the publish structures are null: every view
    // draws from the base values alone, and the pad and the strip still take edits.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    for (const char *id : kProducts)
    {
        INFO(id);
        gg::test::PluginUnderTest instance(id);
        REQUIRE(instance.valid());
        auto *plugin = editorPluginOf(instance);
        const auto editor = plugin->createEditor();
        REQUIRE(editor != nullptr);
        CHECK(plugin->editorContext().snapshot == nullptr);
        for (const View view : kViews)
        {
            INFO(viewId(view));
            REQUIRE(gg::ui::selectView(*editor, view));
            for (const auto size : {std::pair{960, 600}, std::pair{640, 360}})
            {
                editor->setSize(size.first, size.second);
                tick(*editor);
                const juce::Image image =
                    editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                CHECK(image.isValid());
                CHECK(image.getWidth() == size.first);
            }
        }
        REQUIRE(gg::ui::selectView(*editor, View::Perform));
        juce::Component *perform = viewOf(*editor, View::Perform);
        REQUIRE(perform != nullptr);
        // The pad: an arrow key is a gesture on Morph X.
        juce::Component *pad = findDeep(*perform, "pad");
        REQUIRE(pad != nullptr);
        CHECK(pad->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
        // The strip: a letter selects its layer in every view.
        juce::Component *letter = findDeep(*perform, "strip.2.letter");
        REQUIRE(letter != nullptr);
        click(*letter);
        tick(*editor);
        REQUIRE(gg::ui::selectView(*editor, View::Edit));
        const auto *bSize = findDeep(*viewOf(*editor, View::Edit),
                                     controlId(gg::core::layerSlot(gg::core::LayerParam::Size, 2)));
        CHECK(bSize != nullptr);
    }
}

TEST_CASE("every parameter is reachable from the UI", "[plugin]")
{
    // Each parameter has a control in some view: a param.<id> one, the macros' macro.<n> and the
    // morph's pad, with the Edit view's strip selecting each layer in turn for the layer
    // parameters.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    for (const char *id : kProducts)
    {
        INFO(id);
        gg::test::PluginUnderTest instance(id);
        REQUIRE(instance.valid());
        auto *plugin = editorPluginOf(instance);
        const auto editor = plugin->createEditor();
        const gg::core::Mode mode = plugin->editorContext().mode;
        const auto expectedId = [](ParamSlot slot) -> juce::String {
            for (std::size_t i = 0; i < gg::core::kMacroSlots.size(); ++i)
            {
                if (gg::core::kMacroSlots[i] == slot)
                {
                    return "macro." + juce::String(static_cast<int>(i) + 1);
                }
            }
            if (slot == ParamSlot::MorphX || slot == ParamSlot::MorphY)
            {
                return "pad";
            }
            return controlId(slot);
        };
        std::vector<bool> found(gg::core::kParamCount, false);
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            REQUIRE(gg::ui::selectView(*editor, View::Edit));
            juce::Component *letter = findDeep(*viewOf(*editor, View::Edit),
                                               "strip." + juce::String(int(layer)) + ".letter");
            REQUIRE(letter != nullptr);
            click(*letter);
            tick(*editor);
            for (std::size_t i = 0; i < gg::core::kParamCount; ++i)
            {
                const auto slot = static_cast<ParamSlot>(i);
                for (const View view : kViews)
                {
                    found[i] = found[i] || findDeep(*viewOf(*editor, view), expectedId(slot));
                }
            }
        }
        for (std::size_t i = 0; i < gg::core::kParamCount; ++i)
        {
            const auto slot = static_cast<ParamSlot>(i);
            if (gg::ui::hiddenIn(slot, mode))
            {
                CHECK_FALSE(found[i]);
                continue;
            }
            INFO("slot " << i << ": " << gg::core::paramMeta(slot, mode).name);
            CHECK(found[i]);
        }
    }
}

TEST_CASE("undo and redo replay through the gesture path and survive the editor", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
    REQUIRE(fx.valid());
    auto *plugin = editorPluginOf(fx);
    REQUIRE(fx.activate(48000.0, 512));
    gg::test::OutputEvents events;
    const auto run = [&] {
        events.clear();
        processBlock(fx, true, events.list());
    };
    const ParamSlot slot = ParamSlot::Size; // layer A's
    const clap_id id = gg::core::paramId(slot);
    const float before = plugin->uiBaseValue(slot);
    const float after = 0.25f;
    REQUIRE(before != after);

    auto editor = plugin->createEditor();
    REQUIRE(gg::ui::selectView(*editor, View::Edit));
    juce::Component *undo = findDeep(*editor, "header.undo");
    juce::Component *redo = findDeep(*editor, "header.redo");
    REQUIRE(undo != nullptr);
    REQUIRE(redo != nullptr);
    tick(*editor);
    CHECK_FALSE(undo->isEnabled());
    CHECK_FALSE(redo->isEnabled());

    // A knob's gesture in the Edit view.
    auto *knob = knobOf(findDeep(*viewOf(*editor, View::Edit), controlId(slot)));
    knob->onBeginEdit();
    knob->continuous()->setValueFromGUI(after);
    knob->onEndEdit();
    run();
    CHECK(plugin->uiBaseValue(slot) == after);
    tick(*editor);
    CHECK(undo->isEnabled());
    CHECK(tooltipOf(undo) == "Undo A Size");
    CHECK_FALSE(redo->isEnabled());

    // Undo: one gesture, the value before, which the host records.
    const auto checkGesture = [&](float value) {
        REQUIRE(events.events().size() == 3);
        CHECK(events.events()[0].header.type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
        CHECK(events.events()[1].header.type == CLAP_EVENT_PARAM_VALUE);
        CHECK(events.events()[1].value == value);
        CHECK(events.events()[2].header.type == CLAP_EVENT_PARAM_GESTURE_END);
        for (const auto &e : events.events())
        {
            CHECK(e.paramId == id);
        }
    };
    click(*undo);
    run();
    checkGesture(before);
    CHECK(plugin->uiBaseValue(slot) == before);
    tick(*editor);
    CHECK_FALSE(undo->isEnabled());
    CHECK(redo->isEnabled());
    CHECK(tooltipOf(redo) == "Redo A Size");

    click(*redo);
    run();
    checkGesture(after);
    tick(*editor);
    CHECK(undo->isEnabled());

    // The history is the plugin's: a new editor has it.
    editor.reset();
    editor = plugin->createEditor();
    tick(*editor);
    CHECK(findDeep(*editor, "header.undo")->isEnabled());
    CHECK(tooltipOf(findDeep(*editor, "header.undo")) == "Undo A Size");
}

TEST_CASE("destroying the editor mid pad drag leaves the undo stack usable", "[plugin]")
{
    // The pad's group closes when the editor goes under the mouse, and the stack, which the
    // plugin owns, outlives the editor: the next gesture is an entry of its own.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
    REQUIRE(fx.valid());
    auto *plugin = editorPluginOf(fx);
    REQUIRE(fx.activate(48000.0, 512));
    {
        auto editor = plugin->createEditor();
        juce::Component *pad = findDeep(*viewOf(*editor, View::Perform), "pad");
        REQUIRE(pad != nullptr);
        REQUIRE(pad->getWidth() > 20);
        const auto c = pad->getLocalBounds().getCentre().toFloat();
        pad->mouseDown(mouseAt(*pad, c.x, c.y));
        pad->mouseDrag(mouseAt(*pad, c.x + 15.0f, c.y - 10.0f, true, c));
        // Destroyed here, the pad's drag still open.
    }
    gg::ui::UndoStack *stack = plugin->editorContext().undo;
    REQUIRE(stack != nullptr);
    CHECK(stack->undoLabel() == "Morph");
    processBlock(fx, true);
    stack->undo();
    processBlock(fx, true);
    CHECK(plugin->uiBaseValue(ParamSlot::MorphX) == 0.5f);

    auto editor = plugin->createEditor();
    auto *knob = knobOf(findDeep(*viewOf(*editor, View::Edit), controlId(ParamSlot::Texture)));
    knob->onBeginEdit();
    knob->continuous()->setValueFromGUI(0.75f);
    knob->onEndEdit();
    CHECK(stack->undoLabel() == "A Texture");
    CHECK(stack->redoLabel().empty());
}

TEST_CASE("the theme switch restyles, persists and is followed by a second editor", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ownSettings("frame-theme");
    gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
    REQUIRE(fx.valid());
    auto *plugin = editorPluginOf(fx);
    auto first = plugin->createEditor();
    CHECK(&paletteOf(*first) == &gg::ui::Palette::dark()); // dark by default (D8)
    juce::Component *theme = findDeep(*first, "header.theme");
    REQUIRE(theme != nullptr);
    click(*theme);
    CHECK(&paletteOf(*first) == &gg::ui::Palette::light());
    CHECK(gg::plugin::readTheme() == gg::plugin::ThemeName::Light);

    // A second editor, in another instance, opens light; switched back there, the first follows
    // at its next poll (once a second).
    gg::test::PluginUnderTest other("org.sethbc.giga-grains");
    REQUIRE(other.valid());
    auto second = editorPluginOf(other)->createEditor();
    CHECK(&paletteOf(*second) == &gg::ui::Palette::light());
    REQUIRE(gg::ui::setTheme(*second, ThemeChoice::Dark));
    CHECK(gg::plugin::readTheme() == gg::plugin::ThemeName::Dark);
    tick(*first, 30);
    CHECK(&paletteOf(*first) == &gg::ui::Palette::dark());

    // Review Focus 4: the settings can't be written (a file stands where its folder would be).
    // The switch still applies here, the header says why it wasn't kept, and the poll doesn't
    // undo it.
    const auto dir = gg::test::sampleDir("frame-theme-unwritable");
    const auto blocker = dir / "not-a-folder";
    std::filesystem::remove_all(blocker);
    gg::test::writeFile(blocker, std::vector<uint8_t>{'x'});
    gg::test::useSettingsFile(blocker / "settings.xml");
    click(*theme);
    CHECK(&paletteOf(*first) == &gg::ui::Palette::light());
    CHECK(labelText(*first, "header.notice").contains("theme"));
    CHECK(findDeep(*first, "header.notice")->isVisible());
    tick(*first, 30);
    CHECK(&paletteOf(*first) == &gg::ui::Palette::light());
}

TEST_CASE("the header shows a notice for four seconds", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::PluginUnderTest instrument("org.sethbc.giga-grains");
    REQUIRE(instrument.valid());
    const auto editor = editorPluginOf(instrument)->createEditor();
    juce::Component *notice = findDeep(*editor, "header.notice");
    REQUIRE(notice != nullptr);
    CHECK_FALSE(notice->isVisible());

    // A file dropped on lane B is refused (Task 9): the canvas's notice shows in the header.
    auto *canvas = dynamic_cast<juce::FileDragAndDropTarget *>(findDeep(*editor, "canvas"));
    REQUIRE(canvas != nullptr);
    const auto *lanes = findDeep(*editor, "canvas");
    const int laneB = lanes->getHeight() * 3 / 8;
    canvas->filesDropped(juce::StringArray("/nowhere/x.wav"), lanes->getWidth() - 20, laneB);
    CHECK(notice->isVisible());
    CHECK(labelText(*editor, "header.notice") ==
          "layer B takes a sample after the session format's next version");
    tick(*editor, 119);
    CHECK(notice->isVisible());
    tick(*editor);
    CHECK_FALSE(notice->isVisible());
}

TEST_CASE("macro knobs refuse a dropped chip and the strip's controls route one", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
    REQUIRE(fx.valid());
    auto *plugin = editorPluginOf(fx);
    const auto editor = plugin->createEditor();
    const gg::core::ModRoutes routes0 = plugin->uiRoutes();
    const auto drop = [](juce::Component *target, gg::core::ModSource source) {
        auto *t = dynamic_cast<juce::DragAndDropTarget *>(target);
        REQUIRE(t != nullptr);
        const juce::DragAndDropTarget::SourceDetails details(
            juce::var("modsource:" + juce::String(static_cast<int>(source))), nullptr, {5, 5});
        REQUIRE(t->isInterestedInDragSource(details));
        t->itemDropped(details);
    };

    // A macro knob in the Perform view, and its Freeze toggle: refused, with the notice.
    juce::Component *perform = viewOf(*editor, View::Perform);
    for (const char *macro : {"macro.3", "macro.6"})
    {
        INFO(macro);
        drop(findDeep(*perform, macro), gg::core::ModSource::Lfo1);
        CHECK(plugin->uiRoutes() == routes0);
        CHECK(labelText(*editor, "header.notice") == "a macro can't be a target");
    }

    // Layer C's Level in the Perform view's strip: a route to it, one undo entry.
    const ParamSlot levelC = gg::core::levelSlot(2);
    drop(findDeep(*perform, controlId(levelC)), gg::core::ModSource::Lfo2);
    const gg::core::ModRoutes routed = plugin->uiRoutes();
    CHECK(routed != routes0);
    REQUIRE(plugin->editorContext().undo != nullptr);
    CHECK(plugin->editorContext().undo->undoLabel().starts_with("route "));

    // Layer B's On in the Mod view's strip.
    juce::Component *mod = viewOf(*editor, View::Mod);
    drop(findDeep(*findDeep(*mod, "strip"), controlId(gg::core::layerOnSlot(1))),
         gg::core::ModSource::Random1);
    CHECK(plugin->uiRoutes() != routed);
}

TEST_CASE("the keyboard reaches every control", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    for (const char *id : kProducts)
    {
        INFO(id);
        gg::test::PluginUnderTest instance(id);
        REQUIRE(instance.valid());
        auto *plugin = editorPluginOf(instance);
        const auto editor = plugin->createEditor();
        editor->setVisible(true); // as a host shows it (the traverser skips what isn't visible)
        CHECK(editor->isKeyboardFocusContainer());
        const auto traverser = editor->createKeyboardFocusTraverser();
        REQUIRE(dynamic_cast<sst::jucegui::accessibility::KeyboardTraverser *>(traverser.get()) !=
                nullptr);

        // The canvas, the pad and the strip's letters take focus.
        for (const char *focusable : {"canvas", "pad", "strip.0.letter", "strip.3.letter"})
        {
            INFO(focusable);
            CHECK(findDeep(*viewOf(*editor, View::Perform), focusable)->getWantsKeyboardFocus());
        }

        for (const View view : kViews)
        {
            INFO(viewId(view));
            REQUIRE(gg::ui::selectView(*editor, view));
            std::vector<juce::Component *> all;
            collect(*editor, all);
            std::size_t focusable = 0;
            for (const juce::Component *c : all)
            {
                if (c->getWantsKeyboardFocus() && c->isEnabled() && visibleIn(*c, *editor))
                {
                    ++focusable;
                }
            }
            REQUIRE(focusable > 10);
            // Tab from the first focusable component until the order comes round again.
            juce::Component *first = traverser->getDefaultComponent(editor.get());
            REQUIRE(first != nullptr);
            std::set<juce::Component *> seen;
            juce::Component *at = first;
            while (at != nullptr && seen.insert(at).second)
            {
                at = traverser->getNextComponent(at);
            }
            CHECK(at == first);
            CHECK(seen.size() == focusable);
        }

        // Command 2 shows Edit, Command 1 Perform; Command Shift B selects layer B; Command Z
        // undoes, Command Shift Z redoes; Command I opens the About box and Escape closes it.
        REQUIRE(gg::ui::selectView(*editor, View::Perform));
        CHECK(editor->keyPressed(commandKey('2')));
        CHECK(gg::ui::currentView(*editor) == View::Edit);
        CHECK(editor->keyPressed(commandKey('4')));
        CHECK(gg::ui::currentView(*editor) == View::Fx);
        CHECK(editor->keyPressed(commandKey('1')));
        CHECK(gg::ui::currentView(*editor) == View::Perform);
        CHECK(editor->keyPressed(commandKey('B', true)));
        const auto *bSize = findDeep(*viewOf(*editor, View::Edit),
                                     controlId(gg::core::layerSlot(gg::core::LayerParam::Size, 1)));
        CHECK(bSize != nullptr);

        gg::ui::UndoStack *stack = plugin->editorContext().undo;
        REQUIRE(stack != nullptr);
        juce::Component *pad = findDeep(*viewOf(*editor, View::Perform), "pad");
        REQUIRE(pad->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
        REQUIRE(stack->canUndo());
        CHECK(editor->keyPressed(commandKey('Z')));
        CHECK_FALSE(stack->canUndo());
        CHECK(stack->canRedo());
        CHECK(editor->keyPressed(commandKey('Z', true)));
        CHECK(stack->canUndo());
        CHECK(editor->keyPressed(juce::KeyPress('Y', juce::ModifierKeys::ctrlModifier, 0)) ==
              (juce::ModifierKeys::ctrlModifier == juce::ModifierKeys::commandModifier));

        juce::Component *about = findDeep(*editor, "about");
        REQUIRE(about != nullptr);
        CHECK_FALSE(about->isVisible());
        CHECK(editor->keyPressed(commandKey('I')));
        CHECK(about->isVisible());
        CHECK(about->getBounds() == editor->getLocalBounds());
        CHECK(editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
        CHECK_FALSE(about->isVisible());

        // The product's name opens it too, and a click outside its panel closes it.
        click(*findDeep(*editor, "header.product"));
        CHECK(about->isVisible());
        about->mouseDown(mouseAt(*about, 2.0f, 2.0f));
        CHECK_FALSE(about->isVisible());
    }
}

TEST_CASE("a focused lane's arrows nudge its window and Escape ends a canvas drag", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
    REQUIRE(fx.valid());
    auto *plugin = editorPluginOf(fx);
    REQUIRE(fx.activate(48000.0, 512));
    const auto editor = plugin->createEditor();
    juce::Component *canvas = findDeep(*editor, "canvas");
    REQUIRE(canvas != nullptr);
    CHECK(editor->keyPressed(commandKey('C', true))); // layer C is the canvas's focused lane
    using gg::core::LayerParam;
    const auto base = [&](LayerParam p) {
        processBlock(fx, true);
        return plugin->uiBaseValue(gg::core::layerSlot(p, 2));
    };
    const auto range = [&](LayerParam p) {
        const auto &meta = gg::core::paramMeta(gg::core::layerSlot(p, 2), gg::core::Mode::Fx);
        return meta.maxVal - meta.minVal;
    };
    const float position = base(LayerParam::Position);
    CHECK(canvas->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK(base(LayerParam::Position) == position + range(LayerParam::Position) / 100.0f);
    const float pitch = base(LayerParam::Pitch);
    CHECK(canvas->keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    CHECK(base(LayerParam::Pitch) == pitch - range(LayerParam::Pitch) / 100.0f);
    const float spread = base(LayerParam::PitchSpread);
    CHECK(canvas->keyPressed(
        juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)));
    CHECK(base(LayerParam::PitchSpread) == spread + range(LayerParam::PitchSpread) / 100.0f);
    gg::ui::UndoStack *stack = plugin->editorContext().undo;
    REQUIRE(stack != nullptr);
    CHECK(stack->undoLabel() == "C Pitch Spread");
    CHECK(stack->size() == 3);

    // A drag on lane A's window body, ended by Escape: one gesture, one entry; the drag's later
    // moves and its release do nothing.
    auto *grains = dynamic_cast<gg::ui::GrainCanvas *>(canvas);
    REQUIRE(grains != nullptr);
    const juce::Rectangle<int> b = grains->laneBounds(0);
    const gg::ui::LaneGeometry lane{.width = static_cast<float>(b.getWidth()),
                                    .height = static_cast<float>(b.getHeight()),
                                    .pixelScale = 1.0f,
                                    .fxLane = true,
                                    .sampleRate = 48000.0,
                                    .ringSeconds = 16.0,
                                    .sourceFrames = 0,
                                    .top = static_cast<float>(b.getY())};
    const auto a = [&](LayerParam p) { return plugin->uiBaseValue(gg::core::layerSlot(p, 0)); };
    const auto rect = gg::ui::scanRect({a(LayerParam::Position), a(LayerParam::Spray),
                                        a(LayerParam::Pitch), a(LayerParam::PitchSpread)},
                                       lane);
    const float x = 0.5f * (rect.x0 + rect.x1);
    const float y = rect.y0;
    REQUIRE(gg::ui::hitTestRect(x, y, rect, gg::ui::GrainCanvas::kEdgeTolerancePx, lane) ==
            gg::ui::DragTarget::Body);
    const float off = static_cast<float>(b.getX());
    const juce::Point<float> down{off + x, y};
    grains->mouseDown(mouseAt(*grains, off + x, y));
    grains->mouseDrag(mouseAt(*grains, off + x - 30.0f, y, true, down));
    CHECK(editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK(stack->size() == 4);
    CHECK(stack->undoLabel() == "A Position");
    CHECK_FALSE(editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))); // nothing to end
    grains->mouseDrag(mouseAt(*grains, off + x - 60.0f, y, true, down));
    grains->mouseUp(mouseAt(*grains, off + x - 60.0f, y, true, down));
    CHECK(stack->size() == 4);
}

TEST_CASE("the perform view lays the strip beside the lanes and the pad beside the macros",
          "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    for (const char *id : kProducts)
    {
        INFO(id);
        gg::test::PluginUnderTest instance(id);
        REQUIRE(instance.valid());
        const auto editor = editorPluginOf(instance)->createEditor();
        juce::Component *perform = viewOf(*editor, View::Perform);
        REQUIRE(perform != nullptr);
        CHECK(perform->isVisible()); // the first view
        const auto in = [perform](const char *part) {
            juce::Component *c = findDeep(*perform, part);
            REQUIRE(c != nullptr);
            return perform->getLocalArea(c->getParentComponent(), c->getBounds());
        };
        const auto canvas = in("canvas");
        const auto strip = in("strip");
        const auto pad = in("pad");
        const auto macros = in("macros");
        // The strip is the canvas's header column; each layer's letter sits in its lane's rows.
        CHECK(strip.getX() == canvas.getX());
        CHECK(strip.getY() == canvas.getY());
        CHECK(strip.getHeight() == canvas.getHeight());
        for (int layer = 0; layer < 4; ++layer)
        {
            const auto letter = in(("strip." + std::to_string(layer) + ".letter").c_str());
            const int laneTop = canvas.getY() + canvas.getHeight() * layer / 4;
            const int laneBottom = canvas.getY() + canvas.getHeight() * (layer + 1) / 4;
            CHECK(letter.getY() >= laneTop);
            CHECK(letter.getBottom() <= laneBottom);
        }
        // The pad: a square at the bottom left, the macros the rest of its row.
        CHECK(pad.getWidth() == pad.getHeight());
        CHECK(pad.getY() == macros.getY());
        CHECK(pad.getRight() <= macros.getX());
        CHECK(pad.getY() >= canvas.getBottom());
    }
}

TEST_CASE("the window opens at 960 by 600 and the minimum is kept", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    CHECK(gg::ui::kEditorWidth == 960);
    CHECK(gg::ui::kEditorHeight == 600);
    gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
    REQUIRE(fx.valid());
    const auto editor = editorPluginOf(fx)->createEditor();
    CHECK(editor->getWidth() == 960);
    CHECK(editor->getHeight() == 600);
    uint32_t width = 100;
    uint32_t height = 100;
    CHECK(editorPluginOf(fx)->guiAdjustSize(&width, &height));
    CHECK(width == 640);
    CHECK(height == 360);
}

TEST_CASE("the view chosen is remembered per plugin instance", "[plugin]")
{
    // Spec §6.2: while the instance lives, its editor reopens on the view last chosen; it is not
    // saved, and another instance opens on Perform.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
    REQUIRE(fx.valid());
    auto *plugin = editorPluginOf(fx);
    {
        const auto editor = plugin->createEditor();
        CHECK(gg::ui::currentView(*editor) == View::Perform);
        REQUIRE(gg::ui::selectView(*editor, View::Mod));
    }
    {
        const auto editor = plugin->createEditor();
        CHECK(gg::ui::currentView(*editor) == View::Mod);
        CHECK(viewOf(*editor, View::Mod)->isVisible());
        CHECK_FALSE(viewOf(*editor, View::Perform)->isVisible());
        const auto *mod = dynamic_cast<const juce::Button *>(findDeep(*editor, "header.mod"));
        REQUIRE(mod != nullptr);
        CHECK(mod->getToggleState());
        CHECK(editor->keyPressed(commandKey('4')));
    }
    CHECK(gg::ui::currentView(*plugin->createEditor()) == View::Fx);

    gg::test::PluginUnderTest other("org.sethbc.giga-grains-fx");
    REQUIRE(other.valid());
    CHECK(gg::ui::currentView(*editorPluginOf(other)->createEditor()) == View::Perform);
}

TEST_CASE("the About box keeps the keys and the focus to itself", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
    REQUIRE(fx.valid());
    auto *plugin = editorPluginOf(fx);
    const auto editor = plugin->createEditor();
    editor->setVisible(true);
    gg::ui::UndoStack *stack = plugin->editorContext().undo;
    REQUIRE(stack != nullptr);
    juce::Component *pad = findDeep(*viewOf(*editor, View::Perform), "pad");
    REQUIRE(pad->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    REQUIRE(stack->canUndo());

    juce::Component *about = findDeep(*editor, "about");
    REQUIRE(about != nullptr);
    REQUIRE(editor->keyPressed(commandKey('I')));
    REQUIRE(about->isVisible());
    const juce::Component *bSize = nullptr;
    const auto layerBShown = [&] {
        bSize = findDeep(*viewOf(*editor, View::Edit),
                         controlId(gg::core::layerSlot(gg::core::LayerParam::Size, 1)));
        return bSize != nullptr;
    };
    REQUIRE_FALSE(layerBShown());

    // Behind the box nothing changes: every key is the box's, Escape and Command I aside.
    for (const juce::KeyPress &key :
         {commandKey('Z'), commandKey('Z', true), commandKey('Y'), commandKey('2'),
          commandKey('B', true), juce::KeyPress(juce::KeyPress::upKey), juce::KeyPress('x')})
    {
        INFO(key.getTextDescription());
        CHECK(editor->keyPressed(key));
        CHECK(stack->canUndo());
        CHECK_FALSE(stack->canRedo());
        CHECK(gg::ui::currentView(*editor) == View::Perform);
        CHECK_FALSE(layerBShown());
        CHECK(about->isVisible());
    }
    CHECK(editor->keyPressed(commandKey('I'))); // already open: it stays
    CHECK(about->isVisible());

    // Tab stays in the box: it is a focus container of its own, and the editor's traverser
    // sends whatever is behind it to the box.
    CHECK(about->isKeyboardFocusContainer());
    const auto traverser = editor->createKeyboardFocusTraverser();
    CHECK(traverser->getDefaultComponent(editor.get()) == about);
    CHECK(traverser->getNextComponent(pad) == about);
    CHECK(traverser->getPreviousComponent(pad) == about);
    const auto inner = about->createKeyboardFocusTraverser();
    const auto inside = inner->getAllComponents(about);
    REQUIRE_FALSE(inside.empty());
    for (juce::Component *c : inside)
    {
        INFO(c->getComponentID());
        CHECK(about->isParentOf(c));
        juce::Component *next = inner->getNextComponent(c);
        CHECK((next == nullptr || about->isParentOf(next)));
    }

    CHECK(editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(about->isVisible());
    CHECK(traverser->getNextComponent(pad) != about);
    CHECK(editor->keyPressed(commandKey('Z')));
    CHECK(stack->canRedo());
}

TEST_CASE("an undo while a gesture is open does nothing", "[plugin]")
{
    // Command Z with a knob held: the undo would send a gesture of its own on the slot whose
    // gesture is open, so the host would see a nested begin and an early end. It waits instead.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
    REQUIRE(fx.valid());
    auto *plugin = editorPluginOf(fx);
    REQUIRE(fx.activate(48000.0, 512));
    const auto editor = plugin->createEditor();
    REQUIRE(gg::ui::selectView(*editor, View::Edit));
    gg::ui::UndoStack *stack = plugin->editorContext().undo;
    REQUIRE(stack != nullptr);
    auto *knob = knobOf(findDeep(*viewOf(*editor, View::Edit), controlId(ParamSlot::Size)));

    knob->onBeginEdit();
    knob->continuous()->setValueFromGUI(0.25f);
    knob->onEndEdit();
    REQUIRE(stack->size() == 1);
    processBlock(fx, true); // that gesture reaches the host

    gg::test::OutputEvents events;
    knob->onBeginEdit();
    knob->continuous()->setValueFromGUI(0.75f);
    tick(*editor);
    CHECK_FALSE(findDeep(*editor, "header.undo")->isEnabled()); // nothing to do mid-gesture
    CHECK(editor->keyPressed(commandKey('Z')));
    processBlock(fx, true, events.list());
    CHECK(stack->size() == 1);
    CHECK_FALSE(stack->canRedo());
    // The host saw the open gesture's begin and value alone.
    REQUIRE(events.events().size() == 2);
    CHECK(events.events()[0].header.type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
    CHECK(events.events()[1].value == 0.75);
    knob->onEndEdit();
    CHECK(stack->size() == 2);
    tick(*editor);
    CHECK(findDeep(*editor, "header.undo")->isEnabled());
}
