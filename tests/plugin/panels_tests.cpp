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
}

TEST_CASE("each layer's Ring Length commits on release and its neighbors do not", "[plugin]")
{
    // SP-M9: a Ring Length change is a ring resize, so a drag sends one value, as it ends.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::EditView view(recording.context);
    view.setSize(640, 360);
    gg::ui::ParamPanel *source = nullptr;
    for (gg::ui::ParamPanel *panel : view.panels())
    {
        if (panel->panel() == gg::ui::Panel::Source)
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
        REQUIRE(view.panels().size() == 13);
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
    CHECK(visible == 13);
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
        juce::Component *panel = budget->getParentComponent();
        while (panel != nullptr && panel->getParentComponent() != &view)
        {
            panel = panel->getParentComponent();
        }
        REQUIRE(panel != nullptr);
        CHECK(panel->isVisible());
        CHECK(view.getLocalBounds().contains(panel->getBounds()));
        CHECK(childOfType<sst::jucegui::components::Knob>(*budget) != nullptr);
        CHECK((findDeep(view, controlId(ParamSlot::Mix).toRawUTF8()) != nullptr) ==
              (mode == gg::core::Mode::Fx));
    }
}

TEST_CASE("the FX view scrolls when its panels outgrow it", "[plugin][ui]")
{
    // The integration review's Minor 6: since the delay and the limiter joined the Output panel
    // (ruling VW-T19b, ten controls in the FX), the FX view holds its panels in a vertical
    // viewport. At a small height the content is taller than the viewport, which scrolls, and
    // every Output control lies inside the content, so scrolling reaches each one.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::FxView view(recording.context);
    view.setSize(420, 120);
    auto *viewport = childOfType<juce::Viewport>(view);
    REQUIRE(viewport != nullptr);
    CHECK(viewport->getBounds() == view.getLocalBounds());
    juce::Component *content = viewport->getViewedComponent();
    REQUIRE(content != nullptr);
    CHECK(content->getHeight() > viewport->getHeight());
    CHECK(viewport->canScrollVertically());
    CHECK(content->getWidth() <= viewport->getWidth()); // no sideways scroll
    const std::vector<ParamSlot> output = gg::ui::slotsIn(gg::ui::Panel::Output, 0);
    REQUIRE(output.size() == 10);
    for (const ParamSlot slot : output)
    {
        INFO(gg::core::paramMeta(slot, gg::core::Mode::Fx).name);
        juce::Component *control = findDeep(*content, controlId(slot).toRawUTF8());
        REQUIRE(control != nullptr);
        for (const juce::Component *c = control; c != content; c = c->getParentComponent())
        {
            CHECK(c->isVisible()); // the control and its panel (headless: none is on screen)
        }
        const juce::Rectangle<int> inContent =
            content->getLocalArea(control->getParentComponent(), control->getBounds());
        CHECK(!inContent.isEmpty());
        CHECK(content->getLocalBounds().contains(inContent));
    }
    // Scrolled to the bottom, the last control is in view.
    viewport->setViewPosition(0, content->getHeight());
    const juce::Component *last = findDeep(*content, controlId(output.back()).toRawUTF8());
    const juce::Rectangle<int> lastInView =
        viewport->getLocalArea(last->getParentComponent(), last->getBounds());
    CHECK(viewport->getLocalBounds().intersects(lastInView));
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
