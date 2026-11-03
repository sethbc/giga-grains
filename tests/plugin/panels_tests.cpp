// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The editor's parameter control, Edit view and FX view.

#include <catch2/catch_test_macros.hpp>

#include "PluginHarness.h"
#include "EditorTestUtil.h"

#if GG_BUILD_EDITOR

using namespace gg::test::editor;

TEST_CASE("a ParamControl edits its parameter as a gesture", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    using Catch::Matchers::WithinAbs;
    using gg::ui::ParamControl;
    RecordingContext recording(gg::core::Mode::Fx);

    CHECK(ParamControl::kindFor(gg::core::paramMeta(ParamSlot::Size, gg::core::Mode::Fx)) ==
          ParamControl::Kind::Knob);
    CHECK(ParamControl::kindFor(gg::core::paramMeta(ParamSlot::Source, gg::core::Mode::Fx)) ==
          ParamControl::Kind::Menu);
    CHECK(ParamControl::kindFor(gg::core::paramMeta(ParamSlot::B_Freeze, gg::core::Mode::Fx)) ==
          ParamControl::Kind::Toggle);
    CHECK(ParamControl::kindFor(gg::core::paramMeta(ParamSlot::StepLength, gg::core::Mode::Fx)) ==
          ParamControl::Kind::Menu);
    CHECK(ParamControl::kindFor(gg::core::paramMeta(ParamSlot::D_Quality, gg::core::Mode::Fx)) ==
          ParamControl::Kind::Menu);
    // Budget's 16,129 steps are too many for a menu: a knob, which takes whole steps.
    CHECK(ParamControl::kindFor(gg::core::paramMeta(ParamSlot::Budget, gg::core::Mode::Fx)) ==
          ParamControl::Kind::Knob);

    // A knob for layer B's Size: its ID is the CLAP ID's, its label the name less the letter.
    ParamControl size(recording.context, ParamSlot::B_Size, ParamControl::Kind::Knob);
    CHECK(size.getComponentID() ==
          juce::String("param.") + juce::String(gg::core::paramId(ParamSlot::B_Size)));
    CHECK(size.slot() == ParamSlot::B_Size);
    auto *knob = childOfType<sst::jucegui::components::Knob>(size);
    REQUIRE(knob != nullptr);
    REQUIRE(knob->continuous() != nullptr);
    CHECK(knob->continuous()->getLabel() == "Size");
    const float sizeDefault = gg::core::paramMeta(ParamSlot::B_Size, gg::core::Mode::Fx).defaultVal;
    CHECK(size.shownValue() == sizeDefault);

    knob->onBeginEdit();
    knob->continuous()->setValueFromGUI(0.3f);
    knob->onEndEdit();
    REQUIRE(recording.kinds() == "bve");
    for (const GestureCall &c : recording.calls)
    {
        CHECK(c.slot == ParamSlot::B_Size);
    }
    CHECK_THAT(recording.calls[1].value, WithinAbs(0.3, 1e-6));
    CHECK_THAT(size.shownValue(), WithinAbs(0.3, 1e-6));

    // Up: 1/100 of the range, one gesture; a double-click returns to the default, one gesture.
    recording.calls.clear();
    const auto &meta = gg::core::paramMeta(ParamSlot::B_Size, gg::core::Mode::Fx);
    CHECK(knob->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    REQUIRE(recording.kinds() == "bve");
    CHECK_THAT(recording.calls[1].value, WithinAbs(0.3 + 0.01 * (meta.maxVal - meta.minVal), 1e-5));
    recording.calls.clear();
    const juce::Point<float> centre = knob->getLocalBounds().getCentre().toFloat();
    knob->mouseDoubleClick(mouseAt(*knob, centre.x, centre.y, centre, false));
    REQUIRE(recording.kinds() == "bve");
    CHECK(recording.calls[1].value == static_cast<double>(sizeDefault));

    // The control follows a base value the host changed, at refresh.
    recording.values[static_cast<std::size_t>(ParamSlot::B_Size)] = -1.0f;
    size.refresh();
    CHECK(size.shownValue() == -1.0f);

    // A menu for Source shows the five kinds' names, and a choice is one gesture.
    recording.calls.clear();
    ParamControl source(recording.context, ParamSlot::Source, ParamControl::Kind::Menu);
    auto *menu = childOfType<sst::jucegui::components::MenuButtonDiscreteEditor>(source);
    REQUIRE(menu != nullptr);
    REQUIRE(menu->data != nullptr);
    CHECK(menu->data->getLabel() == "Source");
    CHECK(menu->data->getMin() == 0);
    CHECK(menu->data->getMax() == 4);
    const std::array<const char *, 5> kinds{"Sample", "Live", "Resample", "Spectral", "Oscillator"};
    for (int i = 0; i < 5; ++i)
    {
        CHECK(menu->data->getValueAsStringFor(i) == kinds[static_cast<std::size_t>(i)]);
    }
    CHECK(menu->data->getValue() == 1); // Live, the FX's default
    menu->onBeginEdit();
    menu->data->setValueFromGUI(0);
    menu->onEndEdit();
    REQUIRE(recording.kinds() == "bve");
    CHECK(recording.calls[1].slot == ParamSlot::Source);
    CHECK(recording.calls[1].value == 0.0);

    // A global keeps its name.
    ParamControl decay(recording.context, ParamSlot::ReverbDecay, ParamControl::Kind::Knob);
    auto *decayKnob = childOfType<sst::jucegui::components::Knob>(decay);
    REQUIRE(decayKnob != nullptr);
    CHECK(decayKnob->continuous()->getLabel() == "Reverb Decay");
}

TEST_CASE("commit on release sends one value", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    using gg::ui::ParamControl;
    RecordingContext recording(gg::core::Mode::Fx);
    ParamControl size(recording.context, ParamSlot::ReverbSize, ParamControl::Kind::Knob);
    size.setCommitOnRelease(true);
    auto *knob = childOfType<sst::jucegui::components::Knob>(size);
    REQUIRE(knob != nullptr);

    knob->onBeginEdit();
    knob->continuous()->setValueFromGUI(0.2f);
    knob->continuous()->setValueFromGUI(0.4f);
    knob->continuous()->setValueFromGUI(0.6f);
    CHECK(size.shownValue() == 0.6f); // the target, while dragged
    CHECK(recording.kinds() == "b");
    knob->onEndEdit();
    REQUIRE(recording.kinds() == "bve");
    CHECK(recording.calls[1].slot == ParamSlot::ReverbSize);
    CHECK(recording.calls[1].value == static_cast<double>(0.6f));

    // A gesture that moved nothing sends no value.
    recording.calls.clear();
    knob->onBeginEdit();
    knob->onEndEdit();
    CHECK(recording.kinds() == "be");

    // Row 320: commit on release turned off mid-gesture sends the value held back at once, and
    // the gesture goes on sending as it moves; its end sends nothing more.
    recording.calls.clear();
    knob->onBeginEdit();
    knob->continuous()->setValueFromGUI(0.3f);
    CHECK(recording.kinds() == "b");
    size.setCommitOnRelease(false);
    REQUIRE(recording.kinds() == "bv");
    CHECK(recording.calls[1].value == static_cast<double>(0.3f));
    size.setCommitOnRelease(false); // nothing held now: nothing sent
    CHECK(recording.kinds() == "bv");
    knob->continuous()->setValueFromGUI(0.5f);
    CHECK(recording.kinds() == "bvv");
    knob->onEndEdit();
    CHECK(recording.kinds() == "bvve");
}

TEST_CASE("each layer's Ring Length commits on release and its neighbors do not", "[plugin]")
{
    // SP-M9: a Ring Length change is a ring resize, so a drag sends one value, as it ends.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::EditView view(recording.context);
    view.setSize(640, 360);
    gg::ui::ParamPanel *source = nullptr; // the Ring panel since PR 2
    for (gg::ui::ParamPanel *panel : view.panels())
    {
        if (panel->panel() == gg::ui::Panel::Ring)
        {
            source = panel;
        }
    }
    REQUIRE(source != nullptr);
    for (uint8_t layer = 0; layer < 4; ++layer)
    {
        INFO("layer " << int(layer));
        source->setLayer(layer);
        gg::ui::ParamControl *ringLength = nullptr;
        gg::ui::ParamControl *loopLength = nullptr;
        for (gg::ui::ParamControl *control : source->controls())
        {
            const auto ref = gg::core::layerOf(control->slot());
            if (ref && ref->param == gg::core::LayerParam::RingLength)
            {
                ringLength = control;
            }
            if (ref && ref->param == gg::core::LayerParam::LoopLength)
            {
                loopLength = control;
            }
        }
        REQUIRE(ringLength != nullptr);
        REQUIRE(loopLength != nullptr);
        CHECK(ringLength->tooltip().contains("on release"));
        CHECK_FALSE(loopLength->tooltip().contains("on release"));

        // Ring Length: three values dragged, one sent at the release.
        recording.calls.clear();
        auto *knob = childOfType<sst::jucegui::components::Knob>(*ringLength);
        REQUIRE(knob != nullptr);
        knob->onBeginEdit();
        knob->continuous()->setValueFromGUI(0.2f);
        knob->continuous()->setValueFromGUI(0.4f);
        knob->continuous()->setValueFromGUI(0.6f);
        CHECK(recording.kinds() == "b");
        knob->onEndEdit();
        REQUIRE(recording.kinds() == "bve");
        CHECK(recording.calls[1].slot == ringLength->slot());
        CHECK(recording.calls[1].value == static_cast<double>(0.6f));

        // Loop Length stays continuous: a value at every step.
        recording.calls.clear();
        auto *loopKnob = childOfType<sst::jucegui::components::Knob>(*loopLength);
        REQUIRE(loopKnob != nullptr);
        loopKnob->onBeginEdit();
        loopKnob->continuous()->setValueFromGUI(0.2f);
        loopKnob->continuous()->setValueFromGUI(0.4f);
        loopKnob->onEndEdit();
        CHECK(recording.kinds() == "bvve");
    }
}

TEST_CASE("a ParamControl in mod mode drags its rings and not its value", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    using gg::ui::ParamControl;
    RecordingContext recording(gg::core::Mode::Fx);
    ParamControl size(recording.context, ParamSlot::Size, ParamControl::Kind::Knob);
    size.setBounds(0, 0, 72, 92);
    size.setModMode(true);
    size.setRings({{13, 0.25f, true, juce::Colours::yellow}});
    std::vector<std::pair<uint8_t, float>> drags;
    size.onRingDrag = [&drags](uint8_t slot, float depth) { drags.emplace_back(slot, depth); };

    auto *knob = childOfType<sst::jucegui::components::Knob>(size);
    REQUIRE(knob != nullptr);
    bool knobClicks = true, knobChildClicks = true;
    knob->getInterceptsMouseClicks(knobClicks, knobChildClicks);
    CHECK_FALSE(knobClicks);
    CHECK_FALSE(knob->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));

    // A press on the knob's centre grabs no ring; one on the ring drags its depth.
    const juce::Point<float> centre =
        knob->getBounds().toFloat().withHeight(static_cast<float>(knob->getWidth())).getCentre();
    size.mouseDown(mouseAt(size, centre.x, centre.y, centre, false));
    size.mouseDrag(mouseAt(size, centre.x, centre.y - 30.0f, centre, true));
    size.mouseUp(mouseAt(size, centre.x, centre.y - 30.0f, centre, true));
    CHECK(drags.empty());
    const juce::Point<float> onRing{centre.x, knob->getBounds().toFloat().getY() - 2.0f};
    size.mouseDown(mouseAt(size, onRing.x, onRing.y, onRing, false));
    size.mouseDrag(mouseAt(size, onRing.x, onRing.y - 15.0f, onRing, true));
    size.mouseUp(mouseAt(size, onRing.x, onRing.y - 15.0f, onRing, true));
    REQUIRE_FALSE(drags.empty());
    CHECK(drags.back().first == 13);
    CHECK(drags.back().second > 0.25f);
    CHECK(recording.calls.empty()); // the parameter itself never moved

    // The ring paints around the knob, in its colour.
    sst::jucegui::style::StyleSheet::initializeStyleSheets([] {});
    knob->setStyle(sst::jucegui::style::StyleSheet::getBuiltInStyleSheet(
        sst::jucegui::style::StyleSheet::DARK));
    juce::Image image(juce::Image::ARGB, size.getWidth(), size.getHeight(), true);
    {
        juce::Graphics g(image);
        size.paintEntireComponent(g, false);
    }
    const juce::Colour atRing =
        image.getPixelAt(juce::roundToInt(onRing.x), juce::roundToInt(onRing.y - 1.0f));
    CHECK(atRing.getAlpha() > 0);
    CHECK(atRing.getRed() > atRing.getBlue());
}

TEST_CASE("the light palette's macro colour shows on its ground", "[plugin]")
{
    // Okabe-Ito's yellow is about 1.2:1 on the light background, so light() darkens it.
    const gg::ui::Palette &light = gg::ui::Palette::light();
    const gg::ui::Palette &dark = gg::ui::Palette::dark();
    CHECK(light.sourceFamilies[0] != dark.sourceFamilies[0]);
    CHECK(light.sourceFamilies[0].getPerceivedBrightness() <
          light.background.getPerceivedBrightness());
}

TEST_CASE("the light palette's surfaces", "[plugin]")
{
    // Row 320: the editor's own surfaces and text change with the theme (the lanes do not: D9).
    const gg::ui::Palette &light = gg::ui::Palette::light();
    const gg::ui::Palette &dark = gg::ui::Palette::dark();
    CHECK(light.background != dark.background);
    CHECK(light.header != dark.header);
    CHECK(light.text != dark.text);
    CHECK(light.outline != dark.outline);
    CHECK(light.accent != dark.accent);
    // Light reads dark on light, dark the reverse.
    CHECK(light.background.getPerceivedBrightness() > light.text.getPerceivedBrightness());
    CHECK(dark.background.getPerceivedBrightness() < dark.text.getPerceivedBrightness());
    CHECK(light.lane == dark.lane);
}

TEST_CASE("paletteOf walks through an intermediate parent", "[plugin]")
{
    // Row 320: a component reads the palette of the nearest PaletteOwner above it, however deep.
    struct Owner final : juce::Component, gg::ui::PaletteOwner
    {
        const gg::ui::Palette &palette() const noexcept override
        {
            return gg::ui::Palette::light();
        }
        gg::ui::ThemeChoice theme() const noexcept override { return gg::ui::ThemeChoice::Light; }
    };
    const juce::ScopedJuceInitialiser_GUI juceGui;
    Owner owner;
    juce::Component middle, leaf;
    CHECK(&gg::ui::paletteOf(leaf) == &gg::ui::Palette::dark()); // no owner: dark
    middle.addChildComponent(leaf);
    CHECK(&gg::ui::paletteOf(leaf) == &gg::ui::Palette::dark());
    owner.addChildComponent(middle);
    CHECK(&gg::ui::paletteOf(leaf) == &gg::ui::Palette::light());
    CHECK(&gg::ui::paletteOf(middle) == &gg::ui::Palette::light());
    CHECK(&gg::ui::paletteOf(owner) == &gg::ui::Palette::light());
    owner.removeChildComponent(&middle);
    CHECK(&gg::ui::paletteOf(leaf) == &gg::ui::Palette::dark());
}

TEST_CASE("the macro row is unchanged", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::MacroRow row(recording.context);
    const std::array<const char *, 8> names{"Size",    "Density", "Pitch",    "Spray",
                                            "Texture", "Freeze",  "Feedback", "Mix"};
    for (std::size_t i = 0; i < 8; ++i)
    {
        const std::string id = "macro." + std::to_string(i + 1);
        INFO(id);
        juce::Component *macro = findDeep(row, id.c_str());
        REQUIRE(macro != nullptr);
        CHECK(macro->getTitle() == names[i]);
    }
}

TEST_CASE("the Edit view shows every placed parameter of the layer", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    for (const gg::core::Mode mode : {gg::core::Mode::Fx, gg::core::Mode::Instrument})
    {
        RecordingContext recording(mode);
        gg::ui::EditView view(recording.context);
        view.setSize(900, 500);
        CHECK(view.getComponentID() == "view.edit");
        REQUIRE(view.panels().size() == 14);
        CHECK(view.layer() == 0);

        const auto b = editSlots(1, mode);
        const auto d = editSlots(3, mode);
        view.setLayer(1);
        CHECK(view.layer() == 1);
        REQUIRE_FALSE(b.empty());
        for (const ParamSlot slot : b)
        {
            INFO(gg::core::paramMeta(slot, mode).name);
            CHECK(findDeep(view, controlId(slot).toRawUTF8()) != nullptr);
        }
        CHECK(findDeep(view, controlId(ParamSlot::Size).toRawUTF8()) == nullptr); // A's, now gone

        view.setLayer(3);
        for (const ParamSlot slot : d)
        {
            INFO(gg::core::paramMeta(slot, mode).name);
            CHECK(findDeep(view, controlId(slot).toRawUTF8()) != nullptr);
        }
        CHECK(findDeep(view, controlId(ParamSlot::B_Size).toRawUTF8()) == nullptr);
        CHECK(findDeep(view, controlId(ParamSlot::D_Size).toRawUTF8()) != nullptr);
    }
}

TEST_CASE("the Edit view's layer can follow the strip's selection", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::EditView view(recording.context);
    uint8_t selected = 2;
    view.setSelectedLayer([&selected] { return selected; });
    view.refresh();
    CHECK(view.layer() == 2);
    CHECK(findDeep(view, controlId(ParamSlot::C_Size).toRawUTF8()) != nullptr);
    selected = 0;
    view.refresh();
    CHECK(findDeep(view, controlId(ParamSlot::Size).toRawUTF8()) != nullptr);
}

TEST_CASE("spread pairs sit together", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::EditView view(recording.context);
    view.setSize(900, 500);
    for (const uint8_t layer : {0, 2})
    {
        view.setLayer(layer);
        const auto pair = [&](gg::core::LayerParam base, gg::core::LayerParam spread) {
            const auto *a = findDeep(view, controlId(gg::core::layerSlot(base, layer)).toRawUTF8());
            const auto *b =
                findDeep(view, controlId(gg::core::layerSlot(spread, layer)).toRawUTF8());
            REQUIRE(a != nullptr);
            REQUIRE(b != nullptr);
            CHECK(a->getParentComponent() == b->getParentComponent());
            CHECK(a->getRight() <= b->getX()); // the spread at the base's right
        };
        pair(gg::core::LayerParam::Size, gg::core::LayerParam::SizeSpread);
        pair(gg::core::LayerParam::Position, gg::core::LayerParam::Spray);
        pair(gg::core::LayerParam::Pitch, gg::core::LayerParam::PitchSpread);
    }
    // Density has no spread: its own cell.
    const auto *density = findDeep(view, controlId(ParamSlot::C_Density).toRawUTF8());
    REQUIRE(density != nullptr);
    CHECK(density->getParentComponent()->getNumChildComponents() == 1);
}

TEST_CASE("every Edit panel shows, Filter with the buses, and Step has sixteen sliders", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::EditView view(recording.context);
    view.setSize(640, 360);
    int visible = 0;
    for (gg::ui::ParamPanel *panel : view.panels())
    {
        INFO(gg::ui::panelName(panel->panel()));
        // Filter was hidden while empty, until M2 sub-project 4's bus split and the buses'
        // filters filled it (ruling VW-T19b).
        CHECK_FALSE(panel->controls().empty());
        CHECK(panel->isVisible());
        ++visible;
        if (panel->panel() == gg::ui::Panel::Filter)
        {
            CHECK(panel->controls().size() == 7);
        }
        if (panel->panel() == gg::ui::Panel::Step)
        {
            std::vector<sst::jucegui::components::VSlider *> sliders;
            collect(*panel, sliders);
            CHECK(sliders.size() == 16);
            for (const auto *slider : sliders)
            {
                CHECK(slider->getParentComponent()->getWidth() == 20);
            }
            CHECK(panel->controls().size() == 18); // and Rate and Length
        }
    }
    CHECK(visible == 14);
    // Wrapped in the view's width: every visible panel lies inside the scrolled content.
    for (gg::ui::ParamPanel *panel : view.panels())
    {
        if (panel->isVisible())
        {
            CHECK(panel->getRight() <= 640);
            CHECK(panel->getHeight() > 20);
            for (gg::ui::ParamPanel *other : view.panels())
            {
                if (other != panel && other->isVisible())
                {
                    CHECK_FALSE(panel->getBounds().intersects(other->getBounds()));
                }
            }
        }
    }
    // The instrument hides Mix, and nothing else of the Edit view.
    RecordingContext instrument(gg::core::Mode::Instrument);
    gg::ui::EditView iview(instrument.context);
    CHECK(findDeep(iview, controlId(ParamSlot::Mix).toRawUTF8()) == nullptr);
}

TEST_CASE("the Edit view's cells stay inside their panel's content area", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::EditView view(recording.context);
    view.setSize(900, 500);
    const auto check = [&view] {
        for (gg::ui::ParamPanel *panel : view.panels())
        {
            if (!panel->isVisible())
            {
                continue;
            }
            const auto area = panel->getContentArea();
            for (juce::Component *cell : panel->getChildren())
            {
                INFO(gg::ui::panelName(panel->panel()));
                CHECK(area.contains(cell->getBounds()));
            }
        }
    };
    check();
    view.setLayer(1);
    check();
    view.setLayer(3); // the same shapes as B's: no panel resizes
    check();
    view.setSize(900, 400); // a height-only change
    check();
}

TEST_CASE("the Edit view draws rings from the routes and follows the base values", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::core::ModRoutes routes = gg::core::factoryRoutes(gg::core::Mode::Fx);
    int routeReads = 0;
    recording.context.routes = [&routes, &routeReads] {
        ++routeReads;
        return routes;
    };
    recording.context.setRoutes = [&routes](const gg::core::ModRoutes &r) { routes = r; };
    gg::ui::EditView view(recording.context);
    view.setSize(900, 500);
    view.setLayer(1);
    view.refresh();

    const auto ringsOf =
        [&view](ParamSlot slot) -> const std::vector<gg::ui::ParamControl::Ring> & {
        auto *control =
            dynamic_cast<gg::ui::ParamControl *>(findDeep(view, controlId(slot).toRawUTF8()));
        REQUIRE(control != nullptr);
        return control->rings();
    };
    const auto &size = ringsOf(ParamSlot::B_Size);
    REQUIRE(size.size() == 1);
    CHECK(size[0].slot == 1);
    CHECK(size[0].bipolar);
    CHECK(size[0].colour == gg::ui::Palette::dark().sourceFamilies[0]);
    CHECK(ringsOf(ParamSlot::B_Density).size() == 2);
    CHECK(ringsOf(ParamSlot::B_PitchSpread).empty());
    // The spread knobs have rings too: the factory route Macro 4 to Spray is slot 6.
    const auto &spray = ringsOf(ParamSlot::B_Spray);
    REQUIRE(spray.size() == 1);
    CHECK(spray[0].slot == 6);
    CHECK(spray[0].colour == gg::ui::Palette::dark().sourceFamilies[0]);

    // A route's change reaches the ring at the next refresh; a route to layer C alone leaves B's.
    routes[0].targetLayer = 2;
    view.refresh();
    CHECK(ringsOf(ParamSlot::B_Size).empty());

    // A base value the host changed shows at refresh, in the control (the band: its own test).
    recording.values[static_cast<std::size_t>(ParamSlot::B_Pitch)] = 5.0f;
    view.refresh();
    auto *pitch = dynamic_cast<gg::ui::ParamControl *>(
        findDeep(view, controlId(ParamSlot::B_Pitch).toRawUTF8()));
    REQUIRE(pitch != nullptr);
    CHECK(pitch->shownValue() == 5.0f);
    CHECK(routeReads > 1);
}

TEST_CASE("a spread pair's band is its spread either side of the base value", "[plugin]")
{
    // Spec §6.5: Size +- its octaves, Pitch +- its semitones, Position +- Spray on a Sample layer
    // and Position to Position + Spray on a Live one (all in 0..1 of the base's range).
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::EditView view(recording.context);
    view.setSize(900, 500);
    view.refresh();
    const auto set = [&](ParamSlot slot, float v) {
        recording.values[static_cast<std::size_t>(slot)] = v;
    };
    const auto bandOn = [&](ParamSlot slot) {
        view.refresh();
        auto *control =
            dynamic_cast<gg::ui::ParamControl *>(findDeep(view, controlId(slot).toRawUTF8()));
        REQUIRE(control != nullptr);
        return control->spreadBand();
    };
    const auto expected = [&](ParamSlot slot, float low, float high) {
        const auto &meta = gg::core::paramMeta(slot, gg::core::Mode::Fx);
        const float range = meta.maxVal - meta.minVal;
        const auto to01 = [&](float v) {
            return std::clamp((v - meta.minVal) / range, 0.0f, 1.0f);
        };
        return std::pair{to01(low), to01(high)};
    };

    set(ParamSlot::Size, -2.0f);
    set(ParamSlot::SizeSpread, 0.5f);
    const auto size = bandOn(ParamSlot::Size);
    REQUIRE(size.has_value());
    CHECK(*size == expected(ParamSlot::Size, -2.5f, -1.5f));

    set(ParamSlot::Pitch, 3.0f);
    set(ParamSlot::PitchSpread, 7.0f);
    const auto pitch = bandOn(ParamSlot::Pitch);
    REQUIRE(pitch.has_value());
    CHECK(*pitch == expected(ParamSlot::Pitch, -4.0f, 10.0f));

    set(ParamSlot::Pitch, 0.0f);
    set(ParamSlot::PitchSpread, 0.0f);
    CHECK_FALSE(bandOn(ParamSlot::Pitch).has_value()); // no spread, no band

    set(ParamSlot::Position, 0.5f);
    set(ParamSlot::Spray, 0.25f);
    set(ParamSlot::Source, static_cast<float>(gg::core::SourceKind::Sample));
    const auto sample = bandOn(ParamSlot::Position);
    REQUIRE(sample.has_value());
    CHECK(*sample == expected(ParamSlot::Position, 0.25f, 0.75f));
    set(ParamSlot::Source, static_cast<float>(gg::core::SourceKind::Live));
    const auto live = bandOn(ParamSlot::Position);
    REQUIRE(live.has_value());
    CHECK(*live == expected(ParamSlot::Position, 0.5f, 0.75f));
    // Row 336: the kind is gg::core::builtKind's. Resample builds a Live engine, Spectral sprays
    // one way as Live does, and the unbuilt Oscillator falls back to the FX's Live: one-sided.
    for (const auto kind : {gg::core::SourceKind::Resample, gg::core::SourceKind::Spectral,
                            gg::core::SourceKind::Oscillator})
    {
        INFO("kind " << int(kind));
        set(ParamSlot::Source, static_cast<float>(kind));
        const auto band = bandOn(ParamSlot::Position);
        REQUIRE(band.has_value());
        CHECK(*band == expected(ParamSlot::Position, 0.5f, 0.75f));
    }
}

TEST_CASE("the instrument's Oscillator layer takes the Sample band", "[plugin]")
{
    // Row 388 (g): the unbuilt Oscillator falls back to the product's default kind, Sample in
    // the instrument, so its Position band is two-sided there (the FX's, Live, is one-sided: the
    // test above), and follows a switch to Live and back.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Instrument);
    gg::ui::EditView view(recording.context);
    view.setSize(900, 500);
    const auto set = [&](ParamSlot slot, float v) {
        recording.values[static_cast<std::size_t>(slot)] = v;
    };
    const auto band = [&] {
        view.refresh();
        auto *control = dynamic_cast<gg::ui::ParamControl *>(
            findDeep(view, controlId(ParamSlot::Position).toRawUTF8()));
        REQUIRE(control != nullptr);
        return control->spreadBand();
    };
    const auto &meta = gg::core::paramMeta(ParamSlot::Position, gg::core::Mode::Instrument);
    const auto to01 = [&](float v) { return (v - meta.minVal) / (meta.maxVal - meta.minVal); };
    set(ParamSlot::Position, 0.5f);
    set(ParamSlot::Spray, 0.25f);
    const auto twoSided = std::pair{to01(0.25f), to01(0.75f)};
    const auto oneSided = std::pair{to01(0.5f), to01(0.75f)};
    for (const auto &[kind, expected] : {std::pair{gg::core::SourceKind::Oscillator, twoSided},
                                         std::pair{gg::core::SourceKind::Live, oneSided},
                                         std::pair{gg::core::SourceKind::Oscillator, twoSided},
                                         std::pair{gg::core::SourceKind::Sample, twoSided}})
    {
        INFO("kind " << int(kind));
        set(ParamSlot::Source, static_cast<float>(kind));
        const auto shown = band();
        REQUIRE(shown.has_value());
        CHECK(*shown == expected);
    }
}

TEST_CASE("mod mode drags the Edit view's rings and nothing else", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::core::ModRoutes routes = gg::core::factoryRoutes(gg::core::Mode::Fx);
    recording.context.routes = [&routes] { return routes; };
    recording.context.setRoutes = [&routes](const gg::core::ModRoutes &r) { routes = r; };
    gg::ui::EditView view(recording.context);
    view.setSize(900, 500);
    view.refresh();
    view.setModMode(true);

    auto *size = dynamic_cast<gg::ui::ParamControl *>(
        findDeep(view, controlId(ParamSlot::Size).toRawUTF8()));
    REQUIRE(size != nullptr);
    REQUIRE(size->rings().size() == 1);

    // The accessibility interface can't set the value in mod mode, as nothing else can.
    auto *knob = childOfType<sst::jucegui::components::Knob>(*size);
    REQUIRE(knob != nullptr);
    const std::unique_ptr<juce::AccessibilityHandler> handler(knob->createAccessibilityHandler());
    REQUIRE(handler != nullptr);
    REQUIRE(handler->getValueInterface() != nullptr);
    const float before = size->shownValue();
    handler->getValueInterface()->setValue(0.5);
    handler->getValueInterface()->setValueAsString("0.1");
    CHECK(recording.calls.empty());
    CHECK(size->shownValue() == before);

    // A ring drag writes its route's depth, in the parameter's units, through the context.
    size->onRingDrag(1, 0.25f);
    const auto &meta = gg::core::paramMeta(ParamSlot::Size, gg::core::Mode::Fx);
    CHECK(routes[0].depth == 0.25f * (meta.maxVal - meta.minVal));
    CHECK(routes[1].depth == gg::core::factoryRoutes(gg::core::Mode::Fx)[1].depth);
    CHECK(recording.calls.empty());

    // Out of mod mode, the same interface edits as a gesture.
    view.setModMode(false);
    handler->getValueInterface()->setValue(0.5);
    CHECK(recording.kinds() == "bve");
}
TEST_CASE("the Filter panel puts each bus on its own line", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::EditView view(recording.context);
    view.setSize(900, 500); // wide enough for the panel's seven cells on one line
    for (const uint8_t layer : {0, 3})
    {
        INFO("layer " << int(layer));
        view.setLayer(layer);
        const auto cellY = [&](gg::core::LayerParam p) {
            const auto *control =
                findDeep(view, controlId(gg::core::layerSlot(p, layer)).toRawUTF8());
            REQUIRE(control != nullptr);
            return control->getParentComponent()->getY(); // its cell's, in the panel
        };
        using gg::core::LayerParam;
        CHECK(cellY(LayerParam::Bus1Filter) > cellY(LayerParam::BusSplit));
        CHECK(cellY(LayerParam::Bus1Cutoff) == cellY(LayerParam::Bus1Filter));
        CHECK(cellY(LayerParam::Bus1Resonance) == cellY(LayerParam::Bus1Filter));
        CHECK(cellY(LayerParam::Bus2Filter) > cellY(LayerParam::Bus1Resonance));
        CHECK(cellY(LayerParam::Bus2Resonance) == cellY(LayerParam::Bus2Filter));
    }
}

TEST_CASE("a hidden Edit view rebuilds when shown", "[plugin]")
{
    // Row 335: a layer change rebuilds only the shown view; a hidden one keeps its controls until
    // it is shown.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    juce::Component holder; // as the editor's view stack holds its views
    gg::ui::EditView view(recording.context);
    holder.addChildComponent(view);
    view.setSize(900, 500);
    CHECK_FALSE(view.isStale());

    view.setLayer(2);
    CHECK(view.layer() == 2);
    CHECK(view.isStale());
    CHECK(findDeep(view, controlId(ParamSlot::Size).toRawUTF8()) != nullptr); // A's still
    CHECK(findDeep(view, controlId(ParamSlot::C_Size).toRawUTF8()) == nullptr);
    view.refresh(); // a tick while hidden builds nothing either
    CHECK(view.isStale());

    view.setVisible(true);
    CHECK_FALSE(view.isStale());
    CHECK(findDeep(view, controlId(ParamSlot::Size).toRawUTF8()) == nullptr);
    CHECK(findDeep(view, controlId(ParamSlot::C_Size).toRawUTF8()) != nullptr);
    CHECK(findDeep(view, controlId(ParamSlot::C_RingLength).toRawUTF8()) != nullptr);

    // A shown view rebuilds at once.
    view.setLayer(1);
    CHECK_FALSE(view.isStale());
    CHECK(findDeep(view, controlId(ParamSlot::B_Size).toRawUTF8()) != nullptr);

    // The Mod view's Edit panels wait while the Mod view is hidden, and follow when it shows.
    gg::ui::ModView mod(recording.context);
    holder.addChildComponent(mod);
    mod.setSize(900, 500);
    mod.setLayer(3);
    std::vector<gg::ui::EditView *> inner;
    collect(mod, inner);
    REQUIRE(inner.size() == 1);
    CHECK(inner[0]->isStale());
    CHECK(findDeep(mod, controlId(ParamSlot::D_Size).toRawUTF8()) == nullptr);
    mod.setVisible(true);
    CHECK_FALSE(inner[0]->isStale());
    CHECK(findDeep(mod, controlId(ParamSlot::D_Size).toRawUTF8()) != nullptr);
}

TEST_CASE("the Edit view reads the routes once a tick", "[plugin]")
{
    // Row 325: one copy of the routes a refresh, for every panel, not one a panel.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    const gg::core::ModRoutes routes = gg::core::factoryRoutes(gg::core::Mode::Fx);
    int reads = 0;
    recording.context.routes = [&routes, &reads] {
        ++reads;
        return routes;
    };
    gg::ui::EditView view(recording.context);
    view.setSize(900, 500);
    gg::ui::FxView fx(recording.context);
    fx.setSize(900, 500);
    for (int tick = 0; tick < 3; ++tick)
    {
        reads = 0;
        view.refresh();
        CHECK(reads == 1);
        reads = 0;
        fx.refresh();
        CHECK(reads == 1);
    }
}

TEST_CASE("held arrow keys make one undo entry", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    std::vector<std::pair<ParamSlot, float>> undone;
    gg::ui::UndoStack undo{
        {.setValue = [&undone](ParamSlot s, float v) { undone.emplace_back(s, v); },
         .setRoutes = [](const gg::core::ModRoutes &) {},
         .setValues = {}}};
    recording.context.undo = &undo;
    gg::ui::ParamControl size(recording.context, ParamSlot::Size, gg::ui::ParamControl::Kind::Knob);
    auto *knob = childOfType<sst::jucegui::components::Knob>(size);
    REQUIRE(knob != nullptr);
    const float before = size.shownValue();
    SteppedWallClock clock; // repeats 50 ms apart, whatever the machine's speed
    for (int i = 0; i < 20; ++i)
    {
        clock.advance(0.05);
        REQUIRE(knob->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    }
    CHECK(recording.kinds().size() == 20 * 3); // each press is still a host gesture
    CHECK(size.shownValue() > before);
    CHECK(undo.size() == 1);
    undo.undo();
    REQUIRE(undone.size() == 1);
    CHECK(undone[0] == std::pair{ParamSlot::Size, before});

    // A press with a pause is its own entry, and a drag between presses splits them.
    clock.advance(0.05);
    REQUIRE(knob->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK(undo.size() == 1); // redone history: a new entry, not merged into the undone one
    clock.advance(gg::ui::UndoStack::kNudgeMergeSeconds + 0.5);
    REQUIRE(knob->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK(undo.size() == 2); // the pause
    clock.advance(0.05);
    REQUIRE(knob->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK(undo.size() == 2); // within the merge time again
    knob->onBeginEdit();
    knob->continuous()->setValueFromGUI(0.5f);
    knob->onEndEdit();
    CHECK(undo.size() == 3);
    clock.advance(0.05);
    CHECK(knob->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    CHECK(undo.size() == 4);
}

TEST_CASE("a control destroyed mid commit-on-release gesture sends its value before the end",
          "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    const ParamSlot ringSlot = gg::core::layerSlot(gg::core::LayerParam::RingLength, 0);
    {
        gg::ui::ParamControl ring(recording.context, ringSlot, gg::ui::ParamControl::Kind::Knob);
        ring.setCommitOnRelease(true);
        auto *knob = childOfType<sst::jucegui::components::Knob>(ring);
        REQUIRE(knob != nullptr);
        knob->onBeginEdit();
        knob->continuous()->setValueFromGUI(0.2f);
        knob->continuous()->setValueFromGUI(0.6f);
        CHECK(recording.kinds() == "b"); // shown, not sent
    } // the editor closes under the mouse
    REQUIRE(recording.kinds() == "bve");
    CHECK(recording.calls[1].slot == ringSlot);
    CHECK(recording.calls[1].value == static_cast<double>(0.6f));
}

TEST_CASE("a panel destroyed mid ring drag closes its undo entry", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    ModHarness h(gg::core::Mode::Fx);
    {
        auto view = std::make_unique<gg::ui::EditView>(h.recording.context);
        view->setSize(900, 500);
        view->setModMode(true);
        view->refresh();
        auto *size = dynamic_cast<gg::ui::ParamControl *>(
            findDeep(*view, controlId(ParamSlot::Size).toRawUTF8()));
        REQUIRE(size != nullptr);
        size->onRingDrag(1, 0.4f);
        CHECK(h.undo.busy());
        CHECK(h.undo.size() == 0);
    } // the view goes with the mouse still down
    CHECK_FALSE(h.undo.busy());
    REQUIRE(h.undo.size() == 1);
    CHECK(h.undo.undoLabel() == "route 1");
    const float depth = h.routes[0].depth;
    h.undo.undo();
    CHECK(h.routes[0].depth != depth);
}

TEST_CASE("the FX view shows the reverb and the output", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    const ParamSlot reverb[] = {ParamSlot::ReverbAmount,   ParamSlot::ReverbDecay,
                                ParamSlot::ReverbSize,     ParamSlot::ReverbDamping,
                                ParamSlot::ReverbPredelay, ParamSlot::ReverbModulation,
                                ParamSlot::DryToReverb};
    {
        RecordingContext recording(gg::core::Mode::Fx);
        gg::ui::FxView view(recording.context);
        view.setSize(900, 400);
        CHECK(view.getComponentID() == "view.fx");
        for (const ParamSlot slot : reverb)
        {
            INFO(gg::core::paramMeta(slot, gg::core::Mode::Fx).name);
            CHECK(findDeep(view, controlId(slot).toRawUTF8()) != nullptr);
        }
        CHECK(findDeep(view, controlId(ParamSlot::Mix).toRawUTF8()) != nullptr);
    }
    {
        RecordingContext recording(gg::core::Mode::Instrument);
        gg::ui::FxView view(recording.context);
        view.setSize(900, 400);
        CHECK(findDeep(view, controlId(ParamSlot::Mix).toRawUTF8()) == nullptr);
        CHECK(findDeep(view, controlId(ParamSlot::DryToReverb).toRawUTF8()) == nullptr);
        CHECK(findDeep(view, controlId(ParamSlot::ReverbSize).toRawUTF8()) != nullptr);
    }
}

TEST_CASE("Budget in the Output panel in both products", "[plugin]")
{
    // Ruling VW-T19a: the engine's global Budget follows Mix in the Output panel; the instrument,
    // which hides Mix, shows the panel with Budget alone.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    for (const gg::core::Mode mode : {gg::core::Mode::Fx, gg::core::Mode::Instrument})
    {
        INFO((mode == gg::core::Mode::Fx ? "fx" : "instrument"));
        RecordingContext recording(mode);
        gg::ui::FxView view(recording.context);
        view.setSize(900, 400);
        auto *budget = dynamic_cast<gg::ui::ParamControl *>(
            findDeep(view, controlId(ParamSlot::Budget).toRawUTF8()));
        REQUIRE(budget != nullptr);
        // The Output panel, found below the view by its kind, not by walking up from the control
        // to whatever sits under the view (the grid's viewport; row 388 (h)).
        std::vector<gg::ui::ParamPanel *> panels;
        collect(view, panels);
        const auto output = std::find_if(panels.begin(), panels.end(), [](auto *p) {
            return p->panel() == gg::ui::Panel::Output;
        });
        REQUIRE(output != panels.end());
        gg::ui::ParamPanel &panel = **output;
        CHECK(panel.isParentOf(budget));
        CHECK(panel.isVisible());
        CHECK(view.getLocalBounds().contains(
            view.getLocalArea(panel.getParentComponent(), panel.getBounds())));
        CHECK(childOfType<sst::jucegui::components::Knob>(*budget) != nullptr);
        CHECK((findDeep(view, controlId(ParamSlot::Mix).toRawUTF8()) != nullptr) ==
              (mode == gg::core::Mode::Fx));
    }
}

TEST_CASE("the FX view shows its panels in chain order", "[plugin]")
{
    // The signal chain runs the delay, then the reverb, then the limiter (core/src/Processor.cpp),
    // then the output's Mix; the view reads the same way, left to right and top to bottom (ruling
    // VW-P2e).
    const juce::ScopedJuceInitialiser_GUI juceGui;
    for (const gg::core::Mode mode : {gg::core::Mode::Fx, gg::core::Mode::Instrument})
    {
        INFO((mode == gg::core::Mode::Fx ? "fx" : "instrument"));
        RecordingContext recording(mode);
        gg::ui::FxView view(recording.context);
        view.setSize(960, 600);
        std::vector<gg::ui::ParamPanel *> panels;
        collect(view, panels);
        std::erase_if(panels, [](const gg::ui::ParamPanel *p) { return !p->isVisible(); });
        std::sort(panels.begin(), panels.end(),
                  [](const gg::ui::ParamPanel *a, const gg::ui::ParamPanel *b) {
                      return std::pair(a->getY(), a->getX()) < std::pair(b->getY(), b->getX());
                  });
        std::vector<std::string> titles;
        for (const gg::ui::ParamPanel *panel : panels)
        {
            titles.emplace_back(gg::ui::panelName(panel->panel()));
        }
        CHECK(titles == std::vector<std::string>{"Delay", "Reverb", "Limiter", "Output"});
        const auto output = std::find_if(panels.begin(), panels.end(), [](auto *p) {
            return p->panel() == gg::ui::Panel::Output;
        });
        REQUIRE(output != panels.end());
        std::vector<ParamSlot> shown;
        for (const gg::ui::ParamControl *control : (*output)->controls())
        {
            shown.push_back(control->slot());
        }
        CHECK(shown == (mode == gg::core::Mode::Fx
                            ? std::vector<ParamSlot>{ParamSlot::Mix, ParamSlot::Budget}
                            : std::vector<ParamSlot>{ParamSlot::Budget}));
    }
}

TEST_CASE("the FX view lays out any panel list", "[plugin]")
{
    // The FX view's panels go through the grid the Edit view uses (row 329): wrapped in the
    // view's width, apart, and in a vertical viewport that scrolls when they outgrow it, so
    // every control is reachable.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::FxView view(recording.context);
    const auto placed = [&view]() -> std::pair<juce::Viewport *, juce::Component *> {
        auto *viewport = childOfType<juce::Viewport>(view);
        REQUIRE(viewport != nullptr);
        CHECK(viewport->getBounds() == view.getLocalBounds());
        juce::Component *content = viewport->getViewedComponent();
        REQUIRE(content != nullptr);
        CHECK(content->getWidth() <= viewport->getWidth()); // no sideways scroll
        std::vector<gg::ui::ParamPanel *> panels;
        collect(*content, panels);
        int visible = 0;
        for (const gg::ui::ParamPanel *panel : panels)
        {
            if (!panel->isVisible())
            {
                continue;
            }
            ++visible;
            INFO(gg::ui::panelName(panel->panel()));
            CHECK(panel->getParentComponent() == content);
            CHECK(content->getLocalBounds().contains(panel->getBounds()));
            for (const gg::ui::ParamPanel *other : panels)
            {
                if (other != panel && other->isVisible())
                {
                    CHECK_FALSE(panel->getBounds().intersects(other->getBounds()));
                }
            }
        }
        CHECK(visible == 4);
        return {viewport, content};
    };

    view.setSize(960, 600);
    placed();

    view.setSize(640, 360);
    const auto [viewport, content] = placed();
    CHECK(content->getHeight() > viewport->getHeight());
    CHECK(viewport->canScrollVertically());
    // Every control the view holds lies inside the content, so scrolling reaches it.
    std::vector<gg::ui::ParamControl *> controls;
    collect(*content, controls);
    CHECK(controls.size() == 17); // the FX's 5 + 7 + 3 + 2
    for (const gg::ui::ParamControl *control : controls)
    {
        INFO(control->getComponentID());
        const juce::Rectangle<int> inContent =
            content->getLocalArea(control->getParentComponent(), control->getBounds());
        CHECK_FALSE(inContent.isEmpty());
        CHECK(content->getLocalBounds().contains(inContent));
    }
    // Scrolled to the bottom, the lowest control is in view.
    const auto lowest = *std::max_element(
        controls.begin(), controls.end(), [content](const auto *a, const auto *b) {
            return content->getLocalArea(a->getParentComponent(), a->getBounds()).getBottom() <
                   content->getLocalArea(b->getParentComponent(), b->getBounds()).getBottom();
        });
    viewport->setViewPosition(0, content->getHeight());
    CHECK(viewport->getLocalBounds().intersects(
        viewport->getLocalArea(lowest->getParentComponent(), lowest->getBounds())));
}

TEST_CASE("Reverb Size sets on release", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::FxView view(recording.context);
    view.setSize(900, 400);
    auto *size = dynamic_cast<gg::ui::ParamControl *>(
        findDeep(view, controlId(ParamSlot::ReverbSize).toRawUTF8()));
    REQUIRE(size != nullptr);
    CHECK(size->tooltip() == "Sets on release: the room's size changes in one step");
    auto *knob = childOfType<sst::jucegui::components::Knob>(*size);
    REQUIRE(knob != nullptr);
    knob->onBeginEdit();
    knob->continuous()->setValueFromGUI(0.2f);
    knob->continuous()->setValueFromGUI(0.4f);
    knob->continuous()->setValueFromGUI(0.6f);
    CHECK(recording.kinds() == "b");
    knob->onEndEdit();
    CHECK(recording.kinds() == "bve");

    // Another reverb control sets as it moves.
    recording.calls.clear();
    auto *damping = dynamic_cast<gg::ui::ParamControl *>(
        findDeep(view, controlId(ParamSlot::ReverbDamping).toRawUTF8()));
    REQUIRE(damping != nullptr);
    auto *dknob = childOfType<sst::jucegui::components::Knob>(*damping);
    REQUIRE(dknob != nullptr);
    dknob->onBeginEdit();
    dknob->continuous()->setValueFromGUI(0.2f);
    dknob->continuous()->setValueFromGUI(0.4f);
    dknob->onEndEdit();
    CHECK(recording.kinds() == "bvve");
}

TEST_CASE("Decay reads infinity at the top", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    recording.values[static_cast<std::size_t>(ParamSlot::ReverbDecay)] = gg::core::kDecayMax;
    gg::ui::FxView view(recording.context);
    view.setSize(900, 400);
    view.refresh();
    auto *decay = dynamic_cast<gg::ui::ParamControl *>(
        findDeep(view, controlId(ParamSlot::ReverbDecay).toRawUTF8()));
    REQUIRE(decay != nullptr);
    CHECK(decay->shownText() == "\xE2\x88\x9E");
}

#endif
