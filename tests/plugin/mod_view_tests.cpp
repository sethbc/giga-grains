// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The editor's Mod view: the route list, ring drags and the source strip.

#include <catch2/catch_test_macros.hpp>

#include "PluginHarness.h"
#include "EditorTestUtil.h"

#if GG_BUILD_EDITOR

using namespace gg::test::editor;

TEST_CASE("the Mod view routes a dropped chip", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::ui::ModView view(h.recording.context);
    view.setSize(900, 500);
    view.setLayer(1);
    view.refresh();

    // "modsource:9" is LFO 1; the first empty slot after the twelve factory routes is 13.
    dropChip(findDeep(view, controlId(ParamSlot::B_Size).toRawUTF8()), 9);
    CHECK(h.routes[12].source == gg::core::ModSource::Lfo1);
    CHECK(h.routes[12].target == ParamSlot::Size);
    CHECK(h.routes[12].targetLayer == 1);
    CHECK(h.undo.size() == 1);
    CHECK(h.notices.empty());
    // The chip's route is in the list, and the control has a ring for it.
    view.refresh();
    auto *source = dynamic_cast<juce::ComboBox *>(findDeep(view, "route.13.source"));
    REQUIRE(source != nullptr);
    CHECK(source->getSelectedId() == 9 + 1);
    auto *size = dynamic_cast<gg::ui::ParamControl *>(
        findDeep(view, controlId(ParamSlot::B_Size).toRawUTF8()));
    REQUIRE(size != nullptr);
    CHECK(size->rings().size() == 2);

    // A second drop of the same route selects it: no new route, no new undo entry.
    const auto routesAfter = h.routes;
    dropChip(findDeep(view, controlId(ParamSlot::B_Size).toRawUTF8()), 9);
    CHECK(h.routes == routesAfter);
    CHECK(h.undo.size() == 1);
    CHECK(h.notices.empty());

    // Undo takes it away again.
    h.undo.undo();
    CHECK(h.routes[12].source == gg::core::ModSource::None);
}

TEST_CASE("the Mod view refuses a drop with a notice", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::ui::ModView view(h.recording.context);
    view.setSize(900, 500);
    view.setLayer(0);
    view.refresh();
    const auto factory = h.routes;

    // A Step slider is no target.
    dropChip(
        findDeep(view, controlId(gg::core::layerSlot(gg::core::LayerParam::Step1, 0)).toRawUTF8()),
        9);
    REQUIRE(h.notices.size() == 1);
    CHECK_FALSE(h.notices[0].empty());
    CHECK(h.routes == factory);
    CHECK(h.undo.size() == 0);

    // A full matrix refuses.
    for (std::size_t i = 12; i < h.routes.size(); ++i)
    {
        h.routes[i].source = gg::core::ModSource::Lfo2;
        h.routes[i].target = ParamSlot::Pitch;
        h.routes[i].targetLayer = 0;
    }
    const auto full = h.routes;
    dropChip(findDeep(view, controlId(ParamSlot::Size).toRawUTF8()), 9);
    REQUIRE(h.notices.size() == 2);
    CHECK(h.notices[1] == "the matrix is full: 48 routes");
    CHECK(h.routes == full);
}

TEST_CASE("the Mod view marks a toggle's and a menu's routes with dots", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::ui::ModView view(h.recording.context);
    view.setSize(900, 500);
    view.setLayer(0);
    view.refresh();
    // The Edit panels' controls: the layer strip above them has a Source and a Freeze too.
    const juce::Component *edit = findDeep(view, "view.edit");
    REQUIRE(edit != nullptr);
    const auto controlOf = [edit](ParamSlot slot) {
        auto *c =
            dynamic_cast<gg::ui::ParamControl *>(findDeep(*edit, controlId(slot).toRawUTF8()));
        REQUIRE(c != nullptr);
        return c;
    };
    // The factory route Macro Freeze to Freeze reaches the toggle.
    gg::ui::ParamControl *freeze = controlOf(ParamSlot::Freeze);
    REQUIRE(freeze->rings().size() == 1);
    CHECK(freeze->rings()[0].colour == gg::ui::Palette::dark().sourceFamilies[0]);
    // A chip dropped on the Source menu adds a dot in the modulators' colour.
    gg::ui::ParamControl *menu = controlOf(ParamSlot::Source);
    CHECK(menu->rings().empty());
    dropChip(menu, 9);
    view.refresh();
    REQUIRE(menu->rings().size() == 1);
    CHECK(menu->rings()[0].colour == gg::ui::Palette::dark().sourceFamilies[1]);
    CHECK(h.routes[12].curve == gg::core::ModCurve::Unipolar);
}

TEST_CASE("a ring drag in the Mod view sets the depth in one undo entry", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::ui::ModView view(h.recording.context);
    view.setSize(900, 500);
    view.setLayer(0);
    view.refresh();
    auto *size = dynamic_cast<gg::ui::ParamControl *>(
        findDeep(view, controlId(ParamSlot::Size).toRawUTF8()));
    REQUIRE(size != nullptr);
    REQUIRE(size->rings().size() == 1);
    const auto &meta = gg::core::paramMeta(ParamSlot::Size, gg::core::Mode::Fx);
    const float range = meta.maxVal - meta.minVal;
    const float depth0 = h.routes[0].depth;

    size->onRingDrag(1, 0.1f);
    size->onRingDrag(1, 0.2f);
    size->onRingDrag(1, 0.3f);
    CHECK(h.routes[0].depth == 0.3f * range);
    CHECK(h.undo.size() == 0); // still dragging
    size->mouseUp(mouseAt(*size, 5, 5, {5, 5}, true));
    CHECK(h.undo.size() == 1);
    CHECK(h.undo.undoLabel() == "route 1");
    h.undo.undo();
    CHECK(h.routes[0].depth == depth0);
    h.undo.redo();
    CHECK(h.routes[0].depth == 0.3f * range);
    // A press that moved no ring is no entry.
    size->mouseUp(mouseAt(*size, 5, 5, {5, 5}, false));
    CHECK(h.undo.size() == 1);
    CHECK(h.recording.calls.empty());
}

TEST_CASE("a ring drag that the mode or the layer ends mid-drag is still one undo entry",
          "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::ui::EditView view(h.recording.context);
    view.setSize(900, 500);
    view.setModMode(true);
    view.refresh();
    auto *size = dynamic_cast<gg::ui::ParamControl *>(
        findDeep(view, controlId(ParamSlot::Size).toRawUTF8()));
    REQUIRE(size != nullptr);
    size->onRingDrag(1, 0.4f);
    view.setModMode(false);
    CHECK(h.undo.size() == 1);

    view.setModMode(true);
    view.refresh();
    size = dynamic_cast<gg::ui::ParamControl *>(
        findDeep(view, controlId(ParamSlot::Size).toRawUTF8()));
    REQUIRE(size != nullptr);
    size->onRingDrag(1, 0.5f);
    view.setLayer(1);
    CHECK(h.undo.size() == 2);
}

TEST_CASE("a fresh Mod view shows the layer letter and a ring click selects its row", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::ui::ModView view(h.recording.context);
    view.setSize(900, 500);
    view.refresh();
    auto *lfo = dynamic_cast<gg::ui::SourceChip *>(findDeep(view, "source.9"));
    REQUIRE(lfo != nullptr);
    CHECK(lfo->getButtonText().endsWith(" A"));

    auto *routes = dynamic_cast<gg::ui::RouteList *>(findDeep(view, "routes"));
    REQUIRE(routes != nullptr);
    CHECK(routes->selected() == 0);
    auto *size = dynamic_cast<gg::ui::ParamControl *>(
        findDeep(view, controlId(ParamSlot::Size).toRawUTF8()));
    REQUIRE(size != nullptr);
    REQUIRE(size->rings().size() == 1);
    auto *knob = childOfType<sst::jucegui::components::Knob>(*size);
    REQUIRE(knob != nullptr);
    // Ring 0's radius: the knob's, 2 px of gap and half the 3 px spacing.
    const auto circle = knob->getBounds().toFloat();
    const float r = circle.getWidth() * 0.5f;
    const auto centre = circle.withHeight(circle.getWidth()).getCentre();
    const float y = centre.y - (r + 3.5f);
    size->mouseDown(mouseAt(*size, centre.x, y, {centre.x, y}, false));
    size->mouseUp(mouseAt(*size, centre.x, y, {centre.x, y}, false));
    CHECK(routes->selected() == size->rings()[0].slot);
    CHECK(h.undo.size() == 0);
}

TEST_CASE("an undo that adds one route selects it; a load selects none", "[plugin]")
{
    // Row 333 (the director's option 1, 2026-11-02): a refresh selects a route that appeared
    // only when it is the one route that did (an undo, a redo, a drop), never on a load.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::ui::ModView view(h.recording.context);
    view.setSize(900, 500);
    view.refresh();
    auto *routes = dynamic_cast<gg::ui::RouteList *>(findDeep(view, "routes"));
    REQUIRE(routes != nullptr);
    REQUIRE(routes->selected() == 0);

    // Clear slot 2 (a factory route), then undo it: the one route that came back is selected.
    REQUIRE(h.routes[1].source != gg::core::ModSource::None);
    auto *clear = dynamic_cast<juce::Button *>(findDeep(view, "route.2.clear"));
    REQUIRE(clear != nullptr);
    REQUIRE(clear->onClick);
    clear->onClick();
    view.refresh();
    REQUIRE(h.routes[1].source == gg::core::ModSource::None);
    routes->select(0);
    h.undo.undo();
    view.refresh();
    CHECK(h.routes[1].source != gg::core::ModSource::None);
    CHECK(routes->selected() == 2);
    // Redo takes it away, a second undo brings it back: selected again.
    h.undo.redo();
    view.refresh();
    routes->select(0);
    h.undo.undo();
    view.refresh();
    CHECK(routes->selected() == 2);

    // A load that brings five routes leaves the selection alone.
    routes->select(0);
    gg::core::ModRoutes loaded = h.routes;
    for (std::size_t i = 12; i < 17; ++i)
    {
        loaded[i] = {.source = gg::core::ModSource::Lfo1,
                     .target = ParamSlot::Position,
                     .depth = 0.25f,
                     .curve = gg::core::ModCurve::Bipolar};
    }
    h.routes = loaded;
    view.refresh();
    CHECK(routes->selected() == 0);
    // And so does one that brings them while a row is selected.
    routes->select(3);
    for (std::size_t i = 20; i < 23; ++i)
    {
        loaded[i] = loaded[12];
    }
    h.routes = loaded;
    view.refresh();
    CHECK(routes->selected() == 3);
}

TEST_CASE("choosing no source in the route list clears the row", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::ui::ModView view(h.recording.context);
    view.setSize(900, 500);
    view.refresh();
    auto *source = dynamic_cast<juce::ComboBox *>(findDeep(view, "route.3.source"));
    REQUIRE(source != nullptr);
    source->setSelectedId(1, juce::sendNotificationSync);
    CHECK(h.routes[2] == gg::core::ModRoute{});
    CHECK(h.undo.size() == 1);
}

TEST_CASE("the route list edits and clears a slot", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::ui::ModView view(h.recording.context);
    view.setSize(900, 500);
    view.refresh();
    const auto factory = h.routes;

    // 48 rows, the factory routes' slots shown, the rest empty ("-").
    for (int slot = 1; slot <= 48; ++slot)
    {
        INFO(slot);
        CHECK(findDeep(view, ("route." + std::to_string(slot) + ".source").c_str()) != nullptr);
    }
    auto *source13 = dynamic_cast<juce::ComboBox *>(findDeep(view, "route.13.source"));
    REQUIRE(source13 != nullptr);
    CHECK(source13->getSelectedId() == 1);
    CHECK(source13->getText() == juce::String::fromUTF8("\xe2\x80\x94"));

    // A curve choice is one undo entry.
    auto *curve = dynamic_cast<juce::ComboBox *>(findDeep(view, "route.1.curve"));
    REQUIRE(curve != nullptr);
    curve->setSelectedId(static_cast<int>(gg::core::ModCurve::UnipolarSquared) + 1,
                         juce::sendNotificationSync);
    CHECK(h.routes[0].curve == gg::core::ModCurve::UnipolarSquared);
    CHECK(h.undo.size() == 1);

    // Clear.
    auto *clear = dynamic_cast<juce::Button *>(findDeep(view, "route.2.clear"));
    REQUIRE(clear != nullptr);
    REQUIRE(clear->onClick);
    clear->onClick();
    CHECK(h.routes[1] == gg::core::ModRoute{});
    CHECK(h.routes[2] == factory[2]);
    CHECK(h.undo.size() == 2);
    view.refresh();
    auto *source2 = dynamic_cast<juce::ComboBox *>(findDeep(view, "route.2.source"));
    REQUIRE(source2 != nullptr);
    CHECK(source2->getSelectedId() == 1);
    h.undo.undo();
    view.refresh();
    CHECK(source2->getSelectedId() == static_cast<int>(factory[1].source) + 1);

    // The depth slider writes the depth in the target's units.
    auto *depth = dynamic_cast<juce::Slider *>(findDeep(view, "route.1.depth"));
    REQUIRE(depth != nullptr);
    depth->setValue(0.5, juce::sendNotificationSync);
    CHECK(h.routes[0].depth == 0.5f);
}

TEST_CASE("a held ring drag or depth drag keeps undo from changing the routes under it", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    {
        ModHarness h(gg::core::Mode::Fx);
        gg::ui::EditView view(h.recording.context);
        view.setSize(900, 500);
        view.setModMode(true);
        view.refresh();
        auto *size = dynamic_cast<gg::ui::ParamControl *>(
            findDeep(view, controlId(ParamSlot::Size).toRawUTF8()));
        REQUIRE(size != nullptr);
        const auto start = h.routes;
        h.undo.pushRoutes(gg::core::ModRoutes{}, start, "earlier");
        size->onRingDrag(1, 0.3f);
        const auto mid = h.routes;
        REQUIRE(mid != start);
        h.undo.undo(); // mid-drag: nothing
        CHECK(h.routes == mid);
        CHECK(h.undo.size() == 1);
        size->onRingRelease();
        CHECK(h.undo.size() == 2);
        h.undo.undo();
        CHECK(h.routes == start); // the routes before the drag, not a stale before
    }
    {
        ModHarness h(gg::core::Mode::Fx);
        gg::ui::ModView view(h.recording.context);
        view.setSize(900, 500);
        view.refresh();
        auto *depth = dynamic_cast<juce::Slider *>(findDeep(view, "route.1.depth"));
        REQUIRE(depth != nullptr);
        REQUIRE(depth->onDragStart);
        REQUIRE(depth->onDragEnd);
        const auto start = h.routes;
        h.undo.pushRoutes(gg::core::ModRoutes{}, start, "earlier");
        depth->onDragStart();
        depth->setValue(0.5, juce::sendNotificationSync);
        const auto mid = h.routes;
        REQUIRE(mid != start);
        h.undo.undo(); // mid-drag: nothing
        CHECK(h.routes == mid);
        CHECK(h.undo.size() == 1);
        depth->onDragEnd();
        CHECK(h.undo.size() == 2);
        h.undo.undo();
        CHECK(h.routes == start);
    }
}

TEST_CASE("a Mod view destroyed mid depth-drag leaves undo usable", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    const auto start = h.routes;
    {
        gg::ui::ModView view(h.recording.context);
        view.setSize(900, 500);
        view.refresh();
        auto *depth = dynamic_cast<juce::Slider *>(findDeep(view, "route.1.depth"));
        REQUIRE(depth != nullptr);
        REQUIRE(depth->onDragStart);
        depth->onDragStart();
        depth->setValue(0.5, juce::sendNotificationSync);
        REQUIRE(h.routes != start);
        REQUIRE(h.undo.busy());
    } // the view is destroyed with the drag still open
    REQUIRE_FALSE(h.undo.busy());
    h.undo.undo();
    CHECK(h.routes == start); // the depth from before the drag
}

TEST_CASE("a half-built route row is kept until it has a target", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::ui::ModView view(h.recording.context);
    view.setSize(900, 500);
    view.refresh();
    const auto before = h.routes;
    REQUIRE(before[12] == gg::core::ModRoute{}); // slot 13 is empty

    auto *source = dynamic_cast<juce::ComboBox *>(findDeep(view, "route.13.source"));
    auto *target = dynamic_cast<juce::ComboBox *>(findDeep(view, "route.13.target"));
    REQUIRE(source != nullptr);
    REQUIRE(target != nullptr);
    source->setSelectedId(static_cast<int>(gg::core::ModSource::Lfo1) + 1,
                          juce::sendNotificationSync);
    CHECK(h.routes == before); // nothing is sent
    CHECK(h.undo.size() == 0);
    CHECK(source->getSelectedId() == static_cast<int>(gg::core::ModSource::Lfo1) + 1);
    CHECK(target->getText() == "choose a target");
    view.refresh(); // a refresh keeps the row
    CHECK(source->getSelectedId() == static_cast<int>(gg::core::ModSource::Lfo1) + 1);
    CHECK(target->getText() == "choose a target");

    target->setSelectedId(static_cast<int>(ParamSlot::Position) + 1, juce::sendNotificationSync);
    CHECK(h.routes[12].source == gg::core::ModSource::Lfo1);
    CHECK(h.routes[12].target == ParamSlot::Position);
    CHECK(h.undo.size() == 1);
    for (std::size_t i = 0; i < h.routes.size(); ++i)
    {
        if (i != 12)
        {
            CHECK(h.routes[i] == before[i]);
        }
    }
}

TEST_CASE("a route to a hidden target shows its name", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Instrument);
    h.routes[12] = {.source = gg::core::ModSource::Lfo1,
                    .target = ParamSlot::Mix,
                    .depth = 0.25f,
                    .curve = gg::core::ModCurve::Bipolar};
    gg::ui::ModView view(h.recording.context);
    view.setSize(900, 500);
    view.refresh();
    auto *target = dynamic_cast<juce::ComboBox *>(findDeep(view, "route.13.target"));
    REQUIRE(target != nullptr);
    CHECK(target->getText() == "Mix");
    // Dimmed: the hidden name reads in the palette's dim text color, not the target boxes'.
    auto *other = dynamic_cast<juce::ComboBox *>(findDeep(view, "route.14.target"));
    REQUIRE(other != nullptr);
    CHECK(target->findColour(juce::ComboBox::textColourId) !=
          other->findColour(juce::ComboBox::textColourId));
}

TEST_CASE("retargeting a route scales its depth to the new target's units", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::core::ModRoute route;
    route.source = gg::core::ModSource::Lfo1;
    route.target = ParamSlot::Pitch;
    route.depth = 6.0f;
    h.routes[12] = route;
    gg::ui::ModView view(h.recording.context);
    view.setSize(900, 500);
    view.refresh();
    auto *target = dynamic_cast<juce::ComboBox *>(findDeep(view, "route.13.target"));
    REQUIRE(target != nullptr);
    target->setSelectedId(static_cast<int>(ParamSlot::Spray) + 1, juce::sendNotificationSync);
    REQUIRE(h.routes[12].target == ParamSlot::Spray);
    const auto &pitch = gg::core::paramMeta(ParamSlot::Pitch, gg::core::Mode::Fx);
    const auto &spray = gg::core::paramMeta(ParamSlot::Spray, gg::core::Mode::Fx);
    const float expected = 6.0f * ((spray.maxVal - spray.minVal) / (pitch.maxVal - pitch.minVal));
    CHECK(h.routes[12].depth == expected);
    CHECK(h.routes[12].depth != 6.0f);
}

TEST_CASE("a per-layer global drops its letter in its layer's panel and keeps it in the tooltip",
          "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::ui::EditView view(h.recording.context);
    view.setSize(900, 500);
    view.setLayer(1);
    view.refresh();
    auto *level = dynamic_cast<gg::ui::ParamControl *>(
        findDeep(view, controlId(ParamSlot::LevelB).toRawUTF8()));
    REQUIRE(level != nullptr);
    CHECK(level->getTitle() == "Level B");
    CHECK(level->shownLabel() == "Level");
    CHECK(level->tooltip() == "Level B");
}

TEST_CASE("a menu's caption leaves off its panel's title", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::ui::ModView view(h.recording.context);
    view.setSize(900, 500);
    view.refresh();
    auto *shape = findDeep(view, controlId(ParamSlot::Lfo1Shape).toRawUTF8());
    REQUIRE(shape != nullptr);
    REQUIRE(shape->getParentComponent() != nullptr);
    CHECK(shape->getParentComponent()->getName() == "Shape"); // not "LFO 1 Shape" under "LFO 1"
}

TEST_CASE("the source strip has 24 chips and the layer's letter on the per-layer ones", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::ui::SourceStrip strip;
    strip.setSize(660, 80);
    for (int i = 1; i <= 24; ++i)
    {
        INFO(i);
        auto *chip = dynamic_cast<gg::ui::SourceChip *>(
            findDeep(strip, ("source." + std::to_string(i)).c_str()));
        REQUIRE(chip != nullptr);
        CHECK(chip->description() == juce::String("modsource:") + juce::String(i));
    }
    CHECK(strip.getComponentID() == "sources");
    auto *lfo = dynamic_cast<gg::ui::SourceChip *>(findDeep(strip, "source.9"));
    auto *wheel = dynamic_cast<gg::ui::SourceChip *>(findDeep(strip, "source.16"));
    REQUIRE(lfo != nullptr);
    REQUIRE(wheel != nullptr);
    strip.setLayer(2);
    CHECK(lfo->getButtonText().endsWith(" C"));
    CHECK_FALSE(wheel->getButtonText().endsWith(" C"));
    strip.setLayer(3);
    CHECK(lfo->getButtonText().endsWith(" D"));
}

TEST_CASE("the Mod view's knobs are inert", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    gg::ui::ModView view(h.recording.context);
    view.setSize(900, 500);
    view.refresh();
    auto *size = dynamic_cast<gg::ui::ParamControl *>(
        findDeep(view, controlId(ParamSlot::Size).toRawUTF8()));
    REQUIRE(size != nullptr);
    auto *knob = childOfType<sst::jucegui::components::Knob>(*size);
    REQUIRE(knob != nullptr);
    const float before = size->shownValue();
    knob->mouseDown(mouseAt(*knob, 20, 20, {20, 20}, false));
    knob->mouseDrag(mouseAt(*knob, 20, 60, {20, 20}, true));
    knob->mouseUp(mouseAt(*knob, 20, 60, {20, 20}, true));
    CHECK(h.recording.calls.empty());
    CHECK(size->shownValue() == before);
    CHECK(h.undo.size() == 0);
}

#endif
