// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// The plugin identities are frozen at the first public release (INTENT §18): every value here
// is spelled out literally, so a change to gg/plugin/Identity.h fails these tests.

#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <clap/clap.h>
#include <clapwrapper/auv2.h>

#include "gg/plugin/Identity.h"
#include "PluginHarness.h"
#include "EditorTestUtil.h"
#if GG_BUILD_EDITOR
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <sst/jucegui/components/Knob.h>
#include <sst/jucegui/components/MenuButton.h>
#include <sst/jucegui/components/ToggleButton.h>
#include <sst/jucegui/components/VSlider.h>
#include <sst/jucegui/data/Continuous.h>
#include <sst/jucegui/style/StyleSheet.h>

#include "AboutBox.h"
#include "GrainCanvas.h" // ui/src: the editor's own components
#include "LayerStrip.h"
#include "LoaderStrip.h"
#include "MacroRow.h"
#include "MorphPad.h"
#include "Palette.h"
#include "ParamControl.h"
#include "views/EditView.h"
#include "views/FxView.h"
#include "views/ModView.h"
#include "views/RouteList.h"
#include "views/SourceStrip.h"
#include "editor/EditorPlugin.h"
#include "editor/UiSettings.h"
#include "gg/core/GrainQueue.h"
#include "gg/core/ParamIds.h"
#include "gg/core/UiSnapshot.h"
#include "gg/media/SettingsFile.h"
#include "gg/ui/Editor.h"
#include "gg/ui/LaneModel.h"
#include "gg/ui/UndoStack.h"
#include "gg/ui/Notices.h"
#include "gg/core/Version.h"
#include "support/Utf8.h"
#endif

namespace
{
std::vector<std::string_view> features(const clap_plugin_descriptor *d)
{
    std::vector<std::string_view> out;
    for (auto f = d->features; f != nullptr && *f != nullptr; ++f)
    {
        out.emplace_back(*f);
    }
    return out;
}
} // namespace

TEST_CASE("factory exposes two plugins with frozen ids", "[plugin]")
{
    REQUIRE(gg::plugin::clapInit(""));

    const auto *factory = gg::test::pluginFactory();
    REQUIRE(factory != nullptr);
    REQUIRE(factory->get_plugin_count(factory) == 2);

    SECTION("instrument descriptor")
    {
        const auto *d = factory->get_plugin_descriptor(factory, 0);
        REQUIRE(d != nullptr);
        CHECK(clap_version_is_compatible(d->clap_version));
        CHECK(std::string_view(d->id) == "org.sethbc.giga-grains");
        CHECK(std::string_view(d->name) == "Giga Grains");
        CHECK(std::string_view(d->vendor) == "sethbc");
        CHECK(std::string_view(d->url) == "https://github.com/sethbc/giga-grains");
        CHECK(std::string_view(d->version) == GG_VERSION_STRING);
        CHECK(features(d) ==
              std::vector<std::string_view>{"instrument", "synthesizer", "granular", "stereo"});
    }

    SECTION("fx descriptor")
    {
        const auto *d = factory->get_plugin_descriptor(factory, 1);
        REQUIRE(d != nullptr);
        CHECK(clap_version_is_compatible(d->clap_version));
        CHECK(std::string_view(d->id) == "org.sethbc.giga-grains-fx");
        CHECK(std::string_view(d->name) == "Giga Grains FX");
        CHECK(std::string_view(d->vendor) == "sethbc");
        CHECK(std::string_view(d->url) == "https://github.com/sethbc/giga-grains");
        CHECK(std::string_view(d->version) == GG_VERSION_STRING);
        CHECK(features(d) == std::vector<std::string_view>{"audio-effect", "granular", "stereo"});
    }

    SECTION("there is no third plugin and no unknown factory")
    {
        CHECK(factory->get_plugin_descriptor(factory, 2) == nullptr);
        CHECK(gg::plugin::getFactory("org.sethbc.no-such-factory") == nullptr);
    }

    SECTION("create_plugin builds each product and rejects unknown ids")
    {
        gg::test::PluginUnderTest instrument("org.sethbc.giga-grains");
        REQUIRE(instrument.valid());
        CHECK(std::string_view(instrument.clap()->desc->id) == "org.sethbc.giga-grains");
        CHECK(instrument.impl().product() == gg::plugin::Product::Instrument);

        gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
        REQUIRE(fx.valid());
        CHECK(std::string_view(fx.clap()->desc->id) == "org.sethbc.giga-grains-fx");
        CHECK(fx.impl().product() == gg::plugin::Product::Fx);

        CHECK(factory->create_plugin(factory, gg::test::stubHost(), "org.sethbc.nope") == nullptr);
    }

    SECTION("the identity constants hold the frozen values")
    {
        namespace id = gg::plugin::identity;
        CHECK(std::string_view(id::kVendor) == "sethbc");
        CHECK(std::string_view(id::kUrl) == "https://github.com/sethbc/giga-grains");
        CHECK(std::string_view(id::kInstrumentId) == "org.sethbc.giga-grains");
        CHECK(std::string_view(id::kFxId) == "org.sethbc.giga-grains-fx");
        CHECK(std::string_view(id::kInstrumentName) == "Giga Grains");
        CHECK(std::string_view(id::kFxName) == "Giga Grains FX");
        CHECK(std::string_view(id::kAuManufacturer) == "Sbcx");
        CHECK(std::string_view(id::kAuManufacturerName) == "sethbc");
        CHECK(std::string_view(id::kAuInstrumentSubtype) == "GGin");
        CHECK(std::string_view(id::kAuFxSubtype) == "GGfx");
    }

    gg::plugin::clapDeinit();
}

TEST_CASE("auv2 identities", "[plugin]")
{
    const auto *factory = static_cast<const clap_plugin_factory_as_auv2 *>(
        gg::plugin::getFactory(CLAP_PLUGIN_FACTORY_INFO_AUV2));
    REQUIRE(factory != nullptr);
    CHECK(std::string_view(factory->manufacturer_code) == "Sbcx");
    CHECK(std::string_view(factory->manufacturer_name) == "sethbc");

    clap_plugin_info_as_auv2_t info{};
    REQUIRE(factory->get_auv2_info(factory, 0, &info));
    CHECK(std::string_view(info.au_type) == "aumu");
    CHECK(std::string_view(info.au_subt) == "GGin");

    info = {};
    REQUIRE(factory->get_auv2_info(factory, 1, &info));
    CHECK(std::string_view(info.au_type) == "aufx");
    CHECK(std::string_view(info.au_subt) == "GGfx");

    CHECK_FALSE(factory->get_auv2_info(factory, 2, &info));
}

TEST_CASE("fx also answers to aumf", "[plugin]")
{
    const auto *factory = static_cast<const clap_plugin_factory_auv2_legacy *>(
        gg::plugin::getFactory(CLAP_PLUGIN_FACTORY_INFO_AUV2_LEGACY));
    REQUIRE(factory != nullptr);

    CHECK(factory->count(factory, 0) == 0);
    REQUIRE(factory->count(factory, 1) == 1);
    CHECK(factory->count(factory, 2) == 0);

    clap_plugin_auv2_legacy_identity_t identity{};
    REQUIRE(factory->get(factory, 1, 0, &identity));
    CHECK(std::string_view(identity.au_type) == "aumf");
    CHECK(std::string_view(identity.au_subt) == "GGfx");
    CHECK(std::string_view(identity.au_manu) == "Sbcx");

    CHECK_FALSE(factory->get(factory, 0, 0, &identity));
    CHECK_FALSE(factory->get(factory, 1, 1, &identity));
}

#if GG_BUILD_EDITOR
using namespace gg::test::editor;
TEST_CASE("both plugins expose a resizable embedded GUI", "[plugin]")
{
    // Only what needs no window or JUCE runtime: JUCE starts when a host creates the GUI, which
    // pluginval exercises on the built plugins. (CLAP allows can_resize only after create.)
    // clap_juce_shim says yes to cocoa, win32 and x11 on every platform, so only the window API
    // native to the platform under test is asserted.
#if defined(__APPLE__)
    const char *const nativeApi = CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32)
    const char *const nativeApi = CLAP_WINDOW_API_WIN32;
#else
    const char *const nativeApi = CLAP_WINDOW_API_X11;
#endif
    for (const char *id : {"org.sethbc.giga-grains", "org.sethbc.giga-grains-fx"})
    {
        INFO(id);
        gg::test::PluginUnderTest plugin(id);
        REQUIRE(plugin.valid());
        const auto *gui = static_cast<const clap_plugin_gui *>(
            plugin.clap()->get_extension(plugin.clap(), CLAP_EXT_GUI));
        REQUIRE(gui != nullptr);

        CHECK(gui->is_api_supported(plugin.clap(), nativeApi, false));
        CHECK_FALSE(gui->is_api_supported(plugin.clap(), CLAP_WINDOW_API_WAYLAND, false));
        CHECK_FALSE(gui->is_api_supported(plugin.clap(), nativeApi, true)); // floating

        // With the editor built, every instance is an EditorPlugin, which holds the shim.
        auto *editor = dynamic_cast<gg::plugin::EditorPlugin *>(&plugin.impl());
        REQUIRE(editor != nullptr);
        REQUIRE(editor->clapJuceShim != nullptr);
        CHECK(editor->clapJuceShim->guiCanResize());
    }
}

TEST_CASE("the instrument's gui has a sample loader strip that loads a dropped file", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    const auto dir = gg::test::sampleDir("gui-strip");
    gg::test::useSettingsFile(dir / "settings.xml");
    // A non-ASCII name: the path crosses the editor boundary as UTF-8.
    const auto file = gg::test::writeSineWav(dir / std::filesystem::path(u8"gräin.wav"));
    const auto other = gg::test::writeSineWav(dir / "other.wav");

    gg::test::PluginUnderTest instrument("org.sethbc.giga-grains");
    REQUIRE(instrument.valid());
    auto *plugin = dynamic_cast<gg::plugin::EditorPlugin *>(&instrument.impl());
    REQUIRE(plugin != nullptr);
    auto *loader = plugin->sampleLoader();
    REQUIRE(loader != nullptr);
    {
        const std::unique_ptr<juce::Component> editor = plugin->createEditor();
        REQUIRE(editor != nullptr);
        CHECK(stripChild(*editor, "sampleLoad") != nullptr);
        CHECK(stripChild(*editor, "sampleFolder") != nullptr);
        CHECK(stripChild(*editor, "sampleRelink") != nullptr);
        CHECK_FALSE(relinkShown(*editor)); // not missing
        CHECK(labelText(*editor, "sampleStatus") == "no sample");

        // Only the first file dropped loads.
        auto *target = dynamic_cast<juce::FileDragAndDropTarget *>(editor.get());
        REQUIRE(target != nullptr);
        const juce::StringArray files{utf8String(file), utf8String(other)};
        CHECK(target->isInterestedInFileDrag(files));
        target->filesDropped(files, 10, 10);
        // The strip refreshes at once after the request, and that refresh applies the worker's
        // result if it has already finished (EditorPlugin's sampleStatus), so the status here
        // is Loading or, when the worker wins the race (seen once on CI's mac leg), Loaded.
        {
            const auto state = loader->status().state;
            CHECK((state == gg::plugin::SampleState::Loading ||
                   state == gg::plugin::SampleState::Loaded));
            const auto text = labelText(*editor, "sampleStatus");
            CHECK((text.startsWith("loading") || text.startsWith("loaded")));
        }
        CHECK(loader->status().fileName == "gr\xc3\xa4in.wav");
        CHECK(labelText(*editor, "sampleName") == utf8String(file.filename()));

        // As a host does after request_callback, until the load lands.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (loader->status().state == gg::plugin::SampleState::Loading &&
               std::chrono::steady_clock::now() < deadline)
        {
            instrument.onMainThread();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(loader->status().state == gg::plugin::SampleState::Loaded);
        REQUIRE(loader->entry().has_value());
        CHECK(gg::test::pathOf(loader->entry()->absolutePath) ==
              std::filesystem::absolute(file).lexically_normal());
    }
}

TEST_CASE("the editor's timer applies a finished load without on_main_thread", "[plugin]")
{
    // A VST3 host may not deliver on_main_thread while the plugin is inactive: the editor's
    // 30 Hz timer applies the loader's finished result itself, on the main thread.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    const auto dir = gg::test::sampleDir("gui-timer-apply");
    gg::test::useSettingsFile(dir / "settings.xml");
    const auto file = gg::test::writeSineWav(dir / "timer.wav");
    gg::test::PluginUnderTest instrument("org.sethbc.giga-grains");
    REQUIRE(instrument.valid());
    auto *plugin = dynamic_cast<gg::plugin::EditorPlugin *>(&instrument.impl());
    REQUIRE(plugin != nullptr);
    auto *loader = plugin->sampleLoader();
    REQUIRE(loader != nullptr);
    const std::unique_ptr<juce::Component> editor = plugin->createEditor();
    REQUIRE(editor != nullptr);
    auto *timer = dynamic_cast<juce::Timer *>(editor.get());
    REQUIRE(timer != nullptr);
    auto *target = dynamic_cast<juce::FileDragAndDropTarget *>(editor.get());
    REQUIRE(target != nullptr);
    target->filesDropped({utf8String(file)}, 10, 10);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (loader->status().state == gg::plugin::SampleState::Loading &&
           std::chrono::steady_clock::now() < deadline)
    {
        timer->timerCallback(); // never instrument.onMainThread()
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(loader->status().state == gg::plugin::SampleState::Loaded);
    CHECK(labelText(*editor, "sampleStatus").startsWith("loaded"));
}

TEST_CASE("the instrument's gui says when a session's sample is from a newer version", "[plugin]")
{
    // INTENT §18: a newer build's manifest loads with no sample, and the strip says why.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::useSettingsFile(gg::test::sampleDir("gui-newer") / "settings.xml");
    gg::test::PluginUnderTest instrument("org.sethbc.giga-grains");
    REQUIRE(instrument.valid());
    auto *plugin = dynamic_cast<gg::plugin::EditorPlugin *>(&instrument.impl());
    REQUIRE(plugin != nullptr);
    REQUIRE(instrument.load(gg::test::withNewerMedia(instrument.save())));
    const std::unique_ptr<juce::Component> editor = plugin->createEditor();
    REQUIRE(editor != nullptr);
    auto *timer = dynamic_cast<juce::Timer *>(editor.get());
    REQUIRE(timer != nullptr);
    timer->timerCallback();
    CHECK(labelText(*editor, "sampleName").isEmpty());
    CHECK(labelText(*editor, "sampleStatus") == "sample from a newer version");
    CHECK_FALSE(relinkShown(*editor)); // nothing to relink: this build can't read the entry
}

TEST_CASE("the fx's gui has the sample loader strip for its selected layer", "[plugin]")
{
    // M2 sub-project 6's PR 2: both products show the strip (PR 1's FX editor had none).
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
    REQUIRE(fx.valid());
    auto *plugin = dynamic_cast<gg::plugin::EditorPlugin *>(&fx.impl());
    REQUIRE(plugin != nullptr);
    const std::unique_ptr<juce::Component> editor = plugin->createEditor();
    REQUIRE(editor != nullptr);
    for (const char *id :
         {"sampleLoad", "sampleFolder", "sampleRelink", "sampleName", "sampleStatus"})
    {
        INFO(id);
        CHECK(stripChild(*editor, id) != nullptr);
    }
    CHECK(labelText(*editor, "sampleStatus") == "no sample");
    auto *target = dynamic_cast<juce::FileDragAndDropTarget *>(editor.get());
    REQUIRE(target != nullptr);
    CHECK(target->isInterestedInFileDrag({"/some/file.wav"}));
}

TEST_CASE("the gui's loader strip shows the sample's status and offers relink", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::ui::SampleStatusView view;
    std::string loaded;
    gg::ui::EditorContext context;
    context.productName = "Giga Grains";
    context.showSampleLoader = true;
    context.loadSampleFor = [&loaded](uint8_t layer, const std::string &path) {
        CHECK(layer == 0);
        loaded = path;
        return true;
    };
    context.sampleStatusFor = [&view](uint8_t) { return view; };
    using State = gg::ui::SampleStatusView::State;

    SECTION("loaded, with the file's length, channels and rate")
    {
        view = {State::Loaded, "kick.wav", {}, 1.5, 2, 44100.0};
        const auto editor = gg::ui::makeEditor(context);
        CHECK(labelText(*editor, "sampleName") == "kick.wav");
        CHECK(labelText(*editor, "sampleStatus") == "loaded: 1.50 s, 2 ch, 44100 Hz");
        CHECK_FALSE(relinkShown(*editor));

        // A drop passes the first file's path, as UTF-8, to loadSampleFor (layer A).
        auto *target = dynamic_cast<juce::FileDragAndDropTarget *>(editor.get());
        REQUIRE(target != nullptr);
        const juce::String path = juce::String::fromUTF8("/samples/gr\xc3\xa4in.aiff");
        target->filesDropped({path, "/samples/b.wav"}, 0, 0);
        CHECK(loaded == "/samples/gr\xc3\xa4in.aiff");
    }
    SECTION("missing, with relink")
    {
        view = {State::Missing, "kick.wav", "not found", 0.0, 0, 0.0, true};
        const auto editor = gg::ui::makeEditor(context);
        CHECK(labelText(*editor, "sampleStatus") == "missing: not found");
        CHECK(relinkShown(*editor));
    }
    SECTION("changed, with relink")
    {
        view = {State::Changed,
                "kick.wav",
                "the file at the saved path has changed",
                0.0,
                0,
                0.0,
                true};
        const auto editor = gg::ui::makeEditor(context);
        CHECK(labelText(*editor, "sampleStatus") ==
              "missing: the file at the saved path has changed");
        CHECK(relinkShown(*editor));
    }
    SECTION("an error shows its message")
    {
        view = {State::Error, "bad.wav", "can't load bad.wav: not audio", 0.0, 0, 0.0};
        const auto editor = gg::ui::makeEditor(context);
        CHECK(labelText(*editor, "sampleStatus") == "can't load bad.wav: not audio");
        CHECK_FALSE(relinkShown(*editor));
    }
    SECTION("an error for the saved file, with relink")
    {
        // Carry-over [194]: a file at the saved path that can't be read is an Error, and may be
        // relinked as a missing one may.
        view = {State::Error, "locked.wav", "the file at the saved path can't be read", 0.0, 0,
                0.0,          true};
        const auto editor = gg::ui::makeEditor(context);
        CHECK(labelText(*editor, "sampleStatus") == "the file at the saved path can't be read");
        CHECK(relinkShown(*editor));
    }
}

// sst-jucegui (libs/sst/sst-jucegui) builds against our JUCE and works without a window: a Knob
// constructs, reads its value through the data source it's given, and paints with the built-in
// dark style sheet.
namespace
{
struct FixedContinuous final : sst::jucegui::data::Continuous
{
    float value{0.25f};
    std::string getLabel() const override { return "Fixed"; }
    float getValue() const override { return value; }
    void setValueFromGUI(const float &f) override { value = f; }
    void setValueFromModel(const float &f) override { value = f; }
    float getDefaultValue() const override { return 0.5f; }
};
} // namespace

TEST_CASE("an sst-jucegui knob constructs and reads its source", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    sst::jucegui::style::StyleSheet::initializeStyleSheets([] {});

    // The source outlives the knob, which unregisters from it when destroyed.
    FixedContinuous source;
    sst::jucegui::components::Knob knob;
    CHECK(knob.continuous() == nullptr);
    knob.setSource(&source);
    REQUIRE(knob.continuous() == &source);
    CHECK(knob.continuous()->getValue() == 0.25f);
    CHECK(knob.getTitle() == "Fixed");

    knob.setStyle(sst::jucegui::style::StyleSheet::getBuiltInStyleSheet(
        sst::jucegui::style::StyleSheet::DARK));
    knob.setBounds(0, 0, 48, 48);
    juce::Image image(juce::Image::ARGB, 48, 48, true);
    {
        juce::Graphics g(image);
        knob.paintEntireComponent(g, false);
    }
    bool painted = false;
    for (int y = 0; y < image.getHeight() && !painted; ++y)
    {
        for (int x = 0; x < image.getWidth() && !painted; ++x)
        {
            painted = image.getPixelAt(x, y).getAlpha() != 0;
        }
    }
    CHECK(painted);
}

TEST_CASE("the editor context carries the publish structures and the gestures", "[plugin]")
{
    // Task 10's bridge, as the editor sees it: the Processor's publish structures, updated in
    // the plugin's context at every activation and deactivation; publishing on from the
    // editor's creation to its destruction; the base values and gestures through the plugin.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
    REQUIRE(fx.valid());
    auto *plugin = dynamic_cast<gg::plugin::EditorPlugin *>(&fx.impl());
    REQUIRE(plugin != nullptr);
    using gg::core::ParamSlot;
    gg::test::Stereo in(64);
    gg::test::Stereo out(64);
    const auto clockAfterBlock = [&] {
        REQUIRE(fx.process(64, in.data(), out.data()) == CLAP_PROCESS_CONTINUE);
        return plugin->processor()->publishedClock().load();
    };

    {
        std::unique_ptr<juce::Component> editor = plugin->createEditor();
        REQUIRE(editor != nullptr);
        CHECK(plugin->publishing());
        const gg::ui::EditorContext &context = plugin->editorContext();
        CHECK(context.mode == gg::core::Mode::Fx);
        // Inactive: no Processor, so nothing to read.
        CHECK(context.grains == nullptr);
        CHECK(context.snapshot == nullptr);
        REQUIRE(context.livePeaksFor);
        CHECK(context.livePeaksFor(0) == nullptr);
        CHECK(context.clock == nullptr);
        REQUIRE(context.baseValue);
        CHECK(context.baseValue(ParamSlot::Mix) == 0.5f);
        REQUIRE(context.sampleWaveformFor);
        CHECK(context.sampleWaveformFor(0).peaks == nullptr); // no sample yet

        REQUIRE(fx.activate(44100.0, 512));
        auto *processor = plugin->processor();
        REQUIRE(processor != nullptr);
        CHECK(context.grains == &processor->grainQueue());
        CHECK(context.snapshot == &processor->snapshot());
        for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
        {
            CHECK(context.livePeaksFor(layer) == &processor->livePeaks(layer));
        }
        CHECK(context.clock == &processor->publishedClock());
        CHECK(context.sampleRate == 44100.0);
        // The editor opened before activate(): the new Processor publishes all the same.
        CHECK(clockAfterBlock() == 64);

        // A gesture through the context reaches the host.
        gg::test::OutputEvents events;
        context.beginGesture(ParamSlot::Mix);
        context.setValue(ParamSlot::Mix, 0.75);
        context.endGesture(ParamSlot::Mix);
        REQUIRE(fx.process(64, in.data(), out.data(), nullptr, 2, events.list()) ==
                CLAP_PROCESS_CONTINUE);
        CHECK(events.events().size() == 3);
        CHECK(context.baseValue(ParamSlot::Mix) == 0.75f);

        // Deactivated, the pointers go before the Processor does; a new activation brings new
        // ones, and its clock starts again.
        fx.deactivate();
        CHECK(context.grains == nullptr);
        CHECK(context.clock == nullptr);
        REQUIRE(fx.activate(48000.0, 512));
        CHECK(context.clock == &plugin->processor()->publishedClock());
        CHECK(context.sampleRate == 48000.0);
        CHECK(clockAfterBlock() == 64);

        editor.reset();
        CHECK_FALSE(plugin->publishing());
        // Closed: the Processor no longer publishes.
        CHECK(clockAfterBlock() == 64);
    }

    // Two editors at once (a host that creates a second before destroying the first): publishing
    // stays on until the last one goes.
    auto first = plugin->createEditor();
    auto second = plugin->createEditor();
    first.reset();
    CHECK(plugin->publishing());
    second.reset();
    CHECK_FALSE(plugin->publishing());
}

TEST_CASE("the instrument's editor context outlines its sample", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::test::PluginUnderTest instrument("org.sethbc.giga-grains");
    REQUIRE(instrument.valid());
    auto *plugin = dynamic_cast<gg::plugin::EditorPlugin *>(&instrument.impl());
    REQUIRE(plugin != nullptr);
    REQUIRE(instrument.loadSampleState(gg::test::sineSampleFile("gui-waveform")));
    const auto editor = plugin->createEditor();
    REQUIRE(editor != nullptr);
    const gg::ui::EditorContext &context = plugin->editorContext();
    CHECK(context.mode == gg::core::Mode::Instrument);
    REQUIRE(context.sampleWaveformFor);
    const auto waveform = context.sampleWaveformFor(0);
    REQUIRE(waveform.peaks != nullptr);
    CHECK(waveform.peaks == plugin->sampleLoader()->waveform().peaks);
    CHECK(waveform.frames == 192000);
    CHECK(waveform.seconds == 4.0);
}

TEST_CASE("the editor's captures folder is the session's", "[plugin]")
{
    // Carry-over row 384: the editor asks the media session for the captures folder, which keeps
    // no copy of its rule: the settings file's folder, else the default.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    const std::filesystem::path dir = gg::test::useMediaScratch("gui-captures-folder");
    gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
    REQUIRE(fx.valid());
    auto *plugin = dynamic_cast<gg::plugin::EditorPlugin *>(&fx.impl());
    REQUIRE(plugin != nullptr);
    const auto editor = plugin->createEditor();
    REQUIRE(editor != nullptr);
    const gg::ui::EditorContext &context = plugin->editorContext();
    REQUIRE(context.capturesFolder);
    CHECK(plugin->media().capturesFolder() == dir / "captures");
    CHECK(context.capturesFolder() == gg::test::utf8(dir / "captures"));

    // A settings file that names no captures folder: the default.
    const std::filesystem::path empty = dir / "no-folder" / "settings.xml";
    std::filesystem::remove(empty);
    gg::test::useSettingsFile(empty);
    gg::test::PluginUnderTest other("org.sethbc.giga-grains-fx");
    REQUIRE(other.valid());
    auto *second = dynamic_cast<gg::plugin::EditorPlugin *>(&other.impl());
    REQUIRE(second != nullptr);
    const auto secondEditor = second->createEditor();
    REQUIRE(secondEditor != nullptr);
    const gg::ui::EditorContext &defaults = second->editorContext();
    REQUIRE(defaults.capturesFolder);
    CHECK(second->media().capturesFolder() == gg::media::defaultCapturesFolder());
    CHECK(defaults.capturesFolder() == gg::test::utf8(gg::media::defaultCapturesFolder()));
}

TEST_CASE("the perform view has a header, a canvas and eight macros", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    CHECK(gg::ui::kEditorMinWidth == 640);
    CHECK(gg::ui::kEditorMinHeight == 360);
    struct Product
    {
        const char *id;
        const char *name;
        bool instrument;
        std::array<const char *, 8> macros;
    };
    const std::array<Product, 2> products{{
        {"org.sethbc.giga-grains",
         "Giga Grains",
         true,
         {"Size", "Density", "Pitch", "Spray", "Texture", "Freeze", "Feedback", "Level"}},
        {"org.sethbc.giga-grains-fx",
         "Giga Grains FX",
         false,
         {"Size", "Density", "Pitch", "Spray", "Texture", "Freeze", "Feedback", "Mix"}},
    }};
    for (const Product &product : products)
    {
        INFO(product.id);
        gg::test::PluginUnderTest instance(product.id);
        REQUIRE(instance.valid());
        auto *plugin = dynamic_cast<gg::plugin::EditorPlugin *>(&instance.impl());
        REQUIRE(plugin != nullptr);
        const std::unique_ptr<juce::Component> editor = plugin->createEditor();
        REQUIRE(editor != nullptr);
        CHECK(editor->getWidth() == gg::ui::kEditorWidth);
        CHECK(editor->getHeight() == gg::ui::kEditorHeight);

        for (const char *id : {"header.product", "header.preset", "header.perform", "header.edit",
                               "header.frameMs", "canvas"})
        {
            INFO(id);
            CHECK(findDeep(*editor, id) != nullptr);
        }
        const auto text = [&editor](const char *id) {
            const auto *label = dynamic_cast<const juce::Label *>(findDeep(*editor, id));
            return label != nullptr ? label->getText() : juce::String("<no label>");
        };
        CHECK(text("header.product") == product.name);
        // The preset strip (M2 sub-project 5's spec §6.8) replaced the label: its name button.
        const auto *presetName =
            dynamic_cast<const juce::Button *>(findDeep(*editor, "preset.name"));
        REQUIRE(presetName != nullptr);
        CHECK(presetName->getButtonText() == "Init");
        const auto *perform =
            dynamic_cast<const juce::Button *>(findDeep(*editor, "header.perform"));
        REQUIRE(perform != nullptr);
        CHECK(perform->getToggleState());
        CHECK(perform->isEnabled());
        for (const char *view : {"header.edit", "header.mod", "header.fx"})
        {
            INFO(view);
            const auto *button = dynamic_cast<const juce::Button *>(findDeep(*editor, view));
            REQUIRE(button != nullptr);
            CHECK(button->isEnabled());
            CHECK_FALSE(button->getToggleState());
        }

        for (std::size_t i = 0; i < 8; ++i)
        {
            const std::string id = "macro." + std::to_string(i + 1);
            INFO(id);
            juce::Component *macro = findDeep(*editor, id.c_str());
            REQUIRE(macro != nullptr);
            CHECK(macro->getTitle() == product.macros[i]);
            CHECK(macro->getWantsKeyboardFocus());
            if (i == 5)
            {
                CHECK(dynamic_cast<sst::jucegui::components::ToggleButton *>(macro) != nullptr);
            }
            else
            {
                CHECK(dynamic_cast<sst::jucegui::components::Knob *>(macro) != nullptr);
            }
        }

        // The loader strip is in both products (M2 sub-project 6, PR 2).
        CHECK(findDeep(*editor, "sampleLoad") != nullptr);
        CHECK(findDeep(*editor, "sampleStatus") != nullptr);

        // At the smallest size every part still has room, inside the editor.
        editor->setSize(gg::ui::kEditorMinWidth, gg::ui::kEditorMinHeight);
        const auto *canvas = findDeep(*editor, "canvas");
        CHECK(canvas->getHeight() >= 100);
        CHECK(canvas->getWidth() >= 600);
        for (const char *id : {"header.frameMs", "macro.1", "macro.8", "canvas"})
        {
            INFO(id);
            const auto *part = findDeep(*editor, id);
            const auto bounds = editor->getLocalArea(part->getParentComponent(), part->getBounds());
            CHECK(editor->getLocalBounds().contains(bounds));
            CHECK_FALSE(bounds.isEmpty());
        }
    }
}

TEST_CASE("a macro knob writes the base value with a gesture", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    const auto editor = gg::ui::makeEditor(recording.context);
    auto *knob = dynamic_cast<sst::jucegui::components::Knob *>(findDeep(*editor, "macro.3"));
    REQUIRE(knob != nullptr);
    REQUIRE(knob->continuous() != nullptr);
    CHECK(knob->continuous()->getValue() == 0.5f);

    // As the knob does for a drag: begin, values, end.
    knob->onBeginEdit();
    knob->continuous()->setValueFromGUI(0.75f);
    knob->onEndEdit();
    REQUIRE(recording.kinds() == "bve");
    for (const GestureCall &c : recording.calls)
    {
        CHECK(c.slot == ParamSlot::Macro3);
    }
    CHECK(recording.calls[1].value == 0.75);
    CHECK(knob->continuous()->getValue() == 0.75f);

    // The row follows a base value the host changed, at its 30 Hz refresh.
    recording.values[static_cast<std::size_t>(ParamSlot::Macro3)] = 0.25f;
    dynamic_cast<juce::Timer *>(editor.get())->timerCallback();
    CHECK(knob->continuous()->getValue() == 0.25f);

    // The Freeze toggle sends 1 and then 0 as it is clicked twice, each a gesture.
    recording.calls.clear();
    auto *freeze =
        dynamic_cast<sst::jucegui::components::ToggleButton *>(findDeep(*editor, "macro.6"));
    REQUIRE(freeze != nullptr);
    const juce::Point<float> centre = freeze->getLocalBounds().getCentre().toFloat();
    for (int click = 0; click < 2; ++click)
    {
        freeze->mouseDown(mouseAt(*freeze, centre.x, centre.y, centre, false));
        freeze->mouseUp(mouseAt(*freeze, centre.x, centre.y, centre, false));
    }
    REQUIRE(recording.kinds() == "bvebve");
    CHECK(recording.calls[1].slot == ParamSlot::Macro6);
    CHECK(recording.calls[1].value == 1.0);
    CHECK(recording.calls[4].value == 0.0);
}

TEST_CASE("arrow keys adjust a focused macro", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Instrument);
    const auto editor = gg::ui::makeEditor(recording.context);
    juce::Component *knob = findDeep(*editor, "macro.1");
    REQUIRE(knob != nullptr);
    const auto lastValue = [&recording] {
        REQUIRE_FALSE(recording.calls.empty());
        return recording.calls.back().kind == 'e'
                   ? recording.calls[recording.calls.size() - 2].value
                   : -1.0;
    };
    using Catch::Matchers::WithinAbs;

    // Up: +1/100 of the range (0..1), one gesture per press.
    CHECK(knob->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    CHECK(recording.kinds() == "bve");
    CHECK_THAT(lastValue(), WithinAbs(0.51, 1e-6));
    // Shift: +1/1000.
    CHECK(knob->keyPressed(
        juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)));
    CHECK_THAT(lastValue(), WithinAbs(0.511, 1e-6));
    // Down, and Left and Right as Down and Up.
    CHECK(knob->keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    CHECK_THAT(lastValue(), WithinAbs(0.501, 1e-6));
    CHECK(knob->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK_THAT(lastValue(), WithinAbs(0.511, 1e-6));
    CHECK(knob->keyPressed(
        juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0)));
    CHECK_THAT(lastValue(), WithinAbs(0.510, 1e-6));
    CHECK(recording.kinds() == "bvebvebvebvebve");
    for (const GestureCall &c : recording.calls)
    {
        CHECK(c.slot == ParamSlot::Macro1);
    }

    // At the top of the range a press stays there.
    recording.values[static_cast<std::size_t>(ParamSlot::Macro1)] = 1.0f;
    dynamic_cast<juce::Timer *>(editor.get())->timerCallback();
    CHECK(knob->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    CHECK(lastValue() == 1.0);
}

TEST_CASE("the folder button reports a failed save", "[plugin]")
{
    // Carry-over row 198: "Sample folder..." says when the settings couldn't be saved.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Instrument);
    gg::ui::EditorContext &context = recording.context;
    context.showSampleLoader = true;
    gg::ui::SampleStatusView view = statusView(gg::ui::SampleStatusView::State::None);
    context.sampleStatusFor = [&view](uint8_t) { return view; };
    std::string folderAsked;
    gg::ui::SampleFolderResult result{false, "can't save the sample folder: settings.xml is "
                                             "read-only"};
    context.setSampleFolder = [&](const std::string &path) {
        folderAsked = path;
        return result;
    };
    const auto editor = gg::ui::makeEditor(context);
    auto *strip = dynamic_cast<gg::ui::LoaderStrip *>(findDeep(*editor, "sampleStrip"));
    REQUIRE(strip != nullptr);
    // The chooser, answered at once with a folder.
    const juce::File folder = juce::File::getCurrentWorkingDirectory();
    strip->browse = [&folder](const gg::ui::LoaderStrip::Browse &, auto done) { done(folder); };
    auto *button = dynamic_cast<juce::Button *>(findDeep(*editor, "sampleFolder"));
    REQUIRE(button != nullptr);
    REQUIRE(button->onClick);

    button->onClick();
    CHECK(folderAsked == folder.getFullPathName().toStdString());
    CHECK(labelText(*editor, "sampleStatus") == "can't save the sample folder: settings.xml is "
                                                "read-only");
    // It stays through the strip's refresh, until the sample's status changes.
    auto *timer = dynamic_cast<juce::Timer *>(editor.get());
    timer->timerCallback();
    CHECK(labelText(*editor, "sampleStatus").startsWith("can't save the sample folder"));
    view = statusView(gg::ui::SampleStatusView::State::Loading, "kick.wav");
    timer->timerCallback();
    CHECK(labelText(*editor, "sampleStatus").startsWith("loading"));

    // A save that works says where the folder is.
    result = {true, {}};
    button->onClick();
    CHECK(labelText(*editor, "sampleStatus") == "sample folder: " + folder.getFullPathName());
}

TEST_CASE("the loader strip shows every sample state", "[plugin]")
{
    // Carry-over row 199: every state the loader can give, with a message and without.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    using State = gg::ui::SampleStatusView::State;
    struct Case
    {
        gg::ui::SampleStatusView view;
        const char *expected;
        bool relink;
    };
    const std::vector<Case> cases{
        {statusView(State::None), "no sample", false},
        {statusView(State::None, {}, "the session has no sample"),
         "no sample (the session has no sample)", false},
        {statusView(State::Loading, "kick.wav"), "loading\xe2\x80\xa6", false},
        {statusView(State::Loading, "kick.wav", "relinking"), "loading\xe2\x80\xa6 (relinking)",
         false},
        {statusView(State::Loaded, "kick.wav", {}, 1.5, 2, 44100.0),
         "loaded: 1.50 s, 2 ch, 44100 Hz", false},
        {statusView(State::Loaded, "kick.wav", "different audio than the saved sample", 0.25, 1,
                    48000.0),
         "loaded: 0.25 s, 1 ch, 48000 Hz (different audio than the saved sample)", false},
        {statusView(State::Missing, "kick.wav", {}, 0.0, 0, 0.0, true), "missing", true},
        {statusView(State::Missing, "kick.wav", "not found", 0.0, 0, 0.0, true),
         "missing: not found", true},
        {statusView(State::Changed, "kick.wav", {}, 0.0, 0, 0.0, true), "missing", true},
        {statusView(State::Changed, "kick.wav", "the file at the saved path has changed", 0.0, 0,
                    0.0, true),
         "missing: the file at the saved path has changed", true},
        {statusView(State::Error, "bad.wav"), "can't load the sample", false},
        {statusView(State::Error, "bad.wav", "can't load bad.wav: not audio"),
         "can't load bad.wav: not audio", false},
        // A failed relink stays relinkable (SampleLoader keeps `relinkable`): Relink stays.
        {statusView(State::Error, "kick.wav", "can't load kick.wav: not audio", 0.0, 0, 0.0, true),
         "can't load kick.wav: not audio", true},
        {statusView(State::Error, "locked.wav", {}, 0.0, 0, 0.0, true), "can't load the sample",
         true},
    };
    gg::ui::SampleStatusView view;
    RecordingContext recording(gg::core::Mode::Instrument);
    recording.context.showSampleLoader = true;
    recording.context.sampleStatusFor = [&view](uint8_t) { return view; };
    const auto editor = gg::ui::makeEditor(recording.context);
    editor->setSize(1600, gg::ui::kEditorHeight); // wide enough for every status below to fit
    auto *timer = dynamic_cast<juce::Timer *>(editor.get());
    auto *status = dynamic_cast<juce::Label *>(findDeep(*editor, "sampleStatus"));
    REQUIRE(status != nullptr);
    for (const Case &c : cases)
    {
        INFO(c.expected);
        view = c.view;
        timer->timerCallback();
        CHECK(status->getText() == juce::String::fromUTF8(c.expected));
        CHECK(labelText(*editor, "sampleName") == juce::String(c.view.fileName));
        CHECK(relinkShown(*editor) == c.relink);
        CHECK(status->getTooltip().isEmpty()); // each fits
    }

    // A status too long for the strip shows whole in a tooltip.
    const std::string longMessage(300, 'x');
    view = statusView(State::Error, "bad.wav", "can't load bad.wav: " + longMessage);
    timer->timerCallback();
    CHECK(status->getText() == juce::String("can't load bad.wav: ") + longMessage.c_str());
    CHECK(status->getTooltip() == status->getText());
    // and loses it when the status fits again.
    view = statusView(State::None);
    timer->timerCallback();
    CHECK(status->getTooltip().isEmpty());
}

TEST_CASE("the loader strip refuses other files and opens at the last folder", "[plugin]")
{
    // Carry-over row 198: a drop the decoder can't read is refused with a message, and the
    // choosers start in the folder of the last file chosen or dropped (at first, the settings'
    // last load folder).
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Instrument);
    gg::ui::EditorContext &context = recording.context;
    context.showSampleLoader = true;
    std::vector<std::string> loads;
    context.loadSampleFor = [&loads](uint8_t layer, const std::string &path) {
        CHECK(layer == 0);
        loads.push_back(path);
        return true;
    };
    gg::ui::SampleStatusView view;
    context.sampleStatusFor = [&view](uint8_t) { return view; };
    const juce::File start = juce::File::getCurrentWorkingDirectory();
    context.lastLoadFolder = [&start] { return start.getFullPathName().toStdString(); };
    const auto editor = gg::ui::makeEditor(context);
    auto *strip = dynamic_cast<gg::ui::LoaderStrip *>(findDeep(*editor, "sampleStrip"));
    REQUIRE(strip != nullptr);
    std::vector<juce::File> opened;
    juce::File answer;
    strip->browse = [&](const gg::ui::LoaderStrip::Browse &b, auto done) {
        opened.push_back(b.initial);
        done(answer);
    };
    auto *load = dynamic_cast<juce::Button *>(findDeep(*editor, "sampleLoad"));
    REQUIRE(load != nullptr);

    // At first, the settings' last load folder; cancelled (no file), nothing loads.
    load->onClick();
    REQUIRE(opened.size() == 1);
    CHECK(opened[0] == start);
    CHECK(loads.empty());

    // A file outside the decoder's formats is refused, whatever its case, and says why.
    auto *target = dynamic_cast<juce::FileDragAndDropTarget *>(editor.get());
    REQUIRE(target != nullptr);
    const juce::File dir = start.getChildFile("drops");
    CHECK(target->isInterestedInFileDrag({dir.getChildFile("notes.txt").getFullPathName()}));
    target->filesDropped({dir.getChildFile("notes.txt").getFullPathName()}, 0, 0);
    CHECK(loads.empty());
    CHECK(labelText(*editor, "sampleStatus") ==
          "can't load notes.txt: not a WAV, AIFF, FLAC or MP3 file");
    target->filesDropped({dir.getChildFile("noextension").getFullPathName()}, 0, 0);
    CHECK(loads.empty());

    // An upper-case extension is one of them: it loads, and the next chooser opens there.
    const juce::File kick = dir.getChildFile("KICK.WAV");
    target->filesDropped({kick.getFullPathName()}, 0, 0);
    REQUIRE(loads.size() == 1);
    CHECK(loads[0] == kick.getFullPathName().toStdString());
    load->onClick();
    CHECK(opened.back() == dir);

    // A file chosen in another folder moves it there, for Relink too.
    answer = start.getChildFile("elsewhere").getChildFile("snare.flac");
    load->onClick();
    CHECK(loads.back() == answer.getFullPathName().toStdString());
    auto *relink = dynamic_cast<juce::Button *>(findDeep(*editor, "sampleRelink"));
    REQUIRE(relink != nullptr);
    answer = juce::File();
    relink->onClick();
    CHECK(opened.back() == start.getChildFile("elsewhere"));
}

namespace
{
/// A key with Command (Ctrl off macOS), and Shift if asked.
juce::KeyPress commandShiftKey(int code)
{
    return juce::KeyPress(
        code, juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
}

void tickEditor(juce::Component &editor, int n = 1)
{
    auto *timer = dynamic_cast<juce::Timer *>(&editor);
    REQUIRE(timer != nullptr);
    for (int i = 0; i < n; ++i)
    {
        timer->timerCallback();
    }
}

/// The view with this component ID below the editor.
juce::Component *viewById(const juce::Component &editor, const char *viewId)
{
    juce::Component *view = findDeep(editor, viewId);
    REQUIRE(view != nullptr);
    return view;
}

constexpr uint64_t kMiBytes = 1024ull * 1024ull;
} // namespace

TEST_CASE("the strip follows the selected layer in both products", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    const auto dir = gg::test::sampleDir("gui-strip-layers");
    gg::test::useSettingsFile(dir / "settings.xml");
    const auto file = gg::test::writeSineWav(dir / "layer-c.wav");
    for (const char *id : {"org.sethbc.giga-grains", "org.sethbc.giga-grains-fx"})
    {
        INFO(id);
        gg::test::PluginUnderTest product(id);
        REQUIRE(product.valid());
        auto *plugin = dynamic_cast<gg::plugin::EditorPlugin *>(&product.impl());
        REQUIRE(plugin != nullptr);
        const std::unique_ptr<juce::Component> editor = plugin->createEditor();
        REQUIRE(editor != nullptr);
        const gg::ui::EditorContext &context = plugin->editorContext();
        CHECK(labelText(*editor, "loader.letter") == "A");

        REQUIRE(editor->keyPressed(commandShiftKey('C')));
        tickEditor(*editor);
        CHECK(labelText(*editor, "loader.letter") == "C");
        CHECK(labelText(*editor, "sampleStatus") == "no sample");

        // Load... through its path seam: the file goes to layer C.
        auto *strip = dynamic_cast<gg::ui::LoaderStrip *>(findDeep(*editor, "sampleStrip"));
        REQUIRE(strip != nullptr);
        strip->browse = [&file](const gg::ui::LoaderStrip::Browse &, auto done) {
            done(juce::File(utf8String(file)));
        };
        auto *load = dynamic_cast<juce::Button *>(findDeep(*editor, "sampleLoad"));
        REQUIRE(load != nullptr);
        load->onClick();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (context.sampleStatusFor(2).state == gg::ui::SampleStatusView::State::Loading &&
               std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(context.sampleStatusFor(2).state == gg::ui::SampleStatusView::State::Loaded);
        CHECK(context.sampleStatusFor(0).state == gg::ui::SampleStatusView::State::None);
        tickEditor(*editor);
        CHECK(labelText(*editor, "sampleStatus").startsWith("loaded"));
        CHECK(labelText(*editor, "sampleName") == "layer-c.wav");

        // Layer A has none; back on C the strip shows C's again.
        REQUIRE(editor->keyPressed(commandShiftKey('A')));
        tickEditor(*editor);
        CHECK(labelText(*editor, "loader.letter") == "A");
        CHECK(labelText(*editor, "sampleStatus") == "no sample");
        REQUIRE(editor->keyPressed(commandShiftKey('C')));
        tickEditor(*editor);
        CHECK(labelText(*editor, "sampleStatus").startsWith("loaded"));
    }
}

TEST_CASE("the memory readout and the cap", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    for (const auto mode : {gg::core::Mode::Instrument, gg::core::Mode::Fx})
    {
        RecordingContext recording(mode);
        recording.context.showSampleLoader = true;
        gg::ui::EditorContext &context = recording.context;
        gg::ui::MemoryView memory{312 * kMiBytes, 1024};
        std::vector<uint32_t> caps;
        context.memory = [&memory] { return memory; };
        context.setMemoryCapMiB = [&caps, &memory](uint32_t mib) {
            caps.push_back(mib);
            memory.capMiB = mib;
        };
        const auto editor = gg::ui::makeEditor(context);
        tickEditor(*editor, 10);
        CHECK(labelText(*editor, "loader.memory") == "312 MB of 1 GB");

        auto *cap = dynamic_cast<juce::ComboBox *>(findDeep(*editor, "loader.cap"));
        REQUIRE(cap != nullptr);
        CHECK(cap->getNumItems() == 9);
        CHECK(cap->getSelectedId() == 1024);
        CHECK(cap->getText() == "1 GB");
        cap->setSelectedId(512, juce::sendNotificationSync);
        REQUIRE(caps.size() == 1);
        CHECK(caps[0] == 512);
        tickEditor(*editor, 10);
        CHECK(labelText(*editor, "loader.memory") == "312 MB of 512 MB");
        CHECK(cap->getSelectedId() == 512);

        // A cap from a state that is not a choice is listed in order and ticked.
        memory.capMiB = 300;
        tickEditor(*editor, 10);
        CHECK(cap->getNumItems() == 10);
        CHECK(cap->getSelectedId() == 300);
        CHECK(cap->getText() == "300 MB");
        CHECK(cap->getItemText(2) == "256 MB");
        CHECK(cap->getItemText(3) == "300 MB");
        CHECK(cap->getItemText(4) == "512 MB");
        CHECK(caps.size() == 1); // listing it asks for nothing
    }
}

TEST_CASE("the readout shows over the cap", "[plugin]")
{
    // Review Focus 3: lowering the cap below what is held unloads nothing; the readout says so.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Instrument);
    recording.context.showSampleLoader = true;
    gg::ui::EditorContext &context = recording.context;
    gg::ui::MemoryView memory{700 * kMiBytes, 512};
    int loads = 0;
    context.memory = [&memory] { return memory; };
    context.setMemoryCapMiB = [&memory](uint32_t mib) { memory.capMiB = mib; };
    context.loadSampleFor = [&loads](uint8_t, const std::string &) {
        ++loads;
        return true;
    };
    context.relinkSampleFor = [&loads](uint8_t, const std::string &) { ++loads; };
    const auto editor = gg::ui::makeEditor(context);
    tickEditor(*editor, 10);
    const auto *readout = dynamic_cast<juce::Label *>(findDeep(*editor, "loader.memory"));
    REQUIRE(readout != nullptr);
    CHECK(readout->getText() == "700 MB of 512 MB");
    CHECK(readout->findColour(juce::Label::textColourId) == gg::ui::Palette::dark().notice);
    CHECK(loads == 0);

    memory.capMiB = 1024;
    tickEditor(*editor, 10);
    CHECK(readout->getText() == "700 MB of 1 GB");
    CHECK(readout->findColour(juce::Label::textColourId) == gg::ui::Palette::dark().dimText);

    // and in the light theme, the light palette's notice color.
    memory.capMiB = 64;
    REQUIRE(gg::ui::setTheme(*editor, gg::ui::ThemeChoice::Light));
    tickEditor(*editor, 10);
    CHECK(readout->findColour(juce::Label::textColourId) == gg::ui::Palette::light().notice);
}

TEST_CASE("Collect asks for a folder and posts the summary", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    recording.context.showSampleLoader = true;
    gg::ui::EditorContext &context = recording.context;
    std::string collected;
    context.collectSamples = [&collected](const std::string &folder) {
        collected = folder;
        return std::string("Collected 2 files; 1 missing");
    };
    const auto editor = gg::ui::makeEditor(context);
    auto *strip = dynamic_cast<gg::ui::LoaderStrip *>(findDeep(*editor, "sampleStrip"));
    REQUIRE(strip != nullptr);
    const juce::File folder = juce::File::getCurrentWorkingDirectory();
    std::string title;
    bool wantedFolder = false;
    strip->browse = [&](const gg::ui::LoaderStrip::Browse &b, auto done) {
        title = b.title.toStdString();
        wantedFolder = b.folders;
        done(folder);
    };
    auto *button = dynamic_cast<juce::Button *>(findDeep(*editor, "loader.collect"));
    REQUIRE(button != nullptr);
    button->onClick();
    CHECK(wantedFolder);
    CHECK(collected == folder.getFullPathName().toStdString());
    CHECK(labelText(*editor, "header.notice") == "Collected 2 files; 1 missing");

    // Cancelled: nothing collected.
    collected.clear();
    strip->browse = [](const gg::ui::LoaderStrip::Browse &, auto done) { done(juce::File()); };
    button->onClick();
    CHECK(collected.empty());
}

TEST_CASE("Edit shows the compact strip for the selected layer", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    using State = gg::ui::SampleStatusView::State;
    for (const auto mode : {gg::core::Mode::Instrument, gg::core::Mode::Fx})
    {
        RecordingContext recording(mode);
        recording.context.showSampleLoader = true;
        gg::ui::EditorContext &context = recording.context;
        std::vector<uint8_t> asked;
        context.sampleStatusFor = [&asked](uint8_t layer) {
            asked.push_back(layer);
            return layer == 2 ? statusView(State::Loaded, "pad.wav", {}, 2.0, 2, 48000.0)
                              : statusView(State::None);
        };
        context.layer = [] { return uint8_t{2}; };
        const auto editor = gg::ui::makeEditor(context);
        asked.clear(); // the strips poll layer A once as they are built, before the layer is set
        REQUIRE(gg::ui::selectView(*editor, gg::ui::View::Edit));
        tickEditor(*editor);
        const juce::Component *edit = viewById(*editor, "view.edit");
        const auto *status = dynamic_cast<const juce::Label *>(findDeep(*edit, "sampleStatus"));
        REQUIRE(status != nullptr);
        CHECK(status->getText() == "loaded: 2.00 s, 2 ch, 48000 Hz");
        CHECK(status->isShowing() == edit->isShowing());
        const auto *letter = dynamic_cast<const juce::Label *>(findDeep(*edit, "loader.letter"));
        REQUIRE(letter != nullptr);
        CHECK(letter->getText() == "C");
        CHECK(findDeep(*edit, "sampleLoad") != nullptr);
        // Compact: no readout, cap, Collect or folder button.
        CHECK(findDeep(*edit, "loader.memory") == nullptr);
        CHECK(findDeep(*edit, "loader.cap") == nullptr);
        CHECK(findDeep(*edit, "loader.collect") == nullptr);
        CHECK(findDeep(*edit, "sampleFolder") == nullptr);
        // The panels sit below it, inside the view.
        const auto *strip = findDeep(*edit, "sampleStrip");
        REQUIRE(strip != nullptr);
        CHECK(edit->getLocalBounds().contains(strip->getBounds()));
        for (const uint8_t layer : asked)
        {
            CHECK(layer == 2);
        }
    }
}

TEST_CASE("the strip's buttons follow the light theme", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Instrument);
    recording.context.showSampleLoader = true;
    gg::ui::SampleStatusView view =
        statusView(gg::ui::SampleStatusView::State::Missing, "k.wav", {}, 0.0, 0, 0.0, true);
    recording.context.sampleStatusFor = [&view](uint8_t) { return view; };
    const auto editor = gg::ui::makeEditor(recording.context);
    REQUIRE(gg::ui::selectView(*editor, gg::ui::View::Edit));
    tickEditor(*editor);

    const auto check = [&](const gg::ui::Palette &palette) {
        for (const char *viewId : {"view.perform", "view.edit"})
        {
            const juce::Component *view = viewById(*editor, viewId);
            for (const char *id : {"sampleLoad", "sampleRelink", "sampleFolder", "loader.collect"})
            {
                const auto *button = dynamic_cast<const juce::TextButton *>(findDeep(*view, id));
                if (button == nullptr)
                {
                    CHECK(std::string(viewId) == "view.edit"); // the compact strip has no more
                    CHECK(
                        (std::string(id) == "sampleFolder" || std::string(id) == "loader.collect"));
                    continue;
                }
                INFO(viewId << " " << id);
                CHECK(button->findColour(juce::TextButton::buttonColourId) == palette.header);
                CHECK(button->findColour(juce::TextButton::textColourOffId) == palette.text);
                CHECK(button->findColour(juce::ComboBox::outlineColourId) == palette.outline);
            }
        }
        const auto *cap = dynamic_cast<const juce::ComboBox *>(findDeep(*editor, "loader.cap"));
        REQUIRE(cap != nullptr);
        CHECK(cap->findColour(juce::ComboBox::backgroundColourId) == palette.header);
        CHECK(cap->findColour(juce::ComboBox::textColourId) == palette.text);
    };
    check(gg::ui::Palette::dark());
    REQUIRE(gg::ui::setTheme(*editor, gg::ui::ThemeChoice::Light));
    check(gg::ui::Palette::light());
    REQUIRE(gg::ui::setTheme(*editor, gg::ui::ThemeChoice::Dark));
    check(gg::ui::Palette::dark());
}

TEST_CASE("the strip keeps room for its name and status at 640 by 360", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    using State = gg::ui::SampleStatusView::State;
    for (const auto mode : {gg::core::Mode::Instrument, gg::core::Mode::Fx})
    {
        RecordingContext recording(mode);
        recording.context.showSampleLoader = true;
        gg::ui::EditorContext &context = recording.context;
        context.memory = [] { return gg::ui::MemoryView{1023 * kMiBytes, 1536}; };
        context.sampleStatusFor = [](uint8_t) {
            return statusView(State::Loaded, "a-long-sample-name.wav", {}, 12.5, 2, 44100.0);
        };
        const auto editor = gg::ui::makeEditor(context);
        for (const auto &size : {std::pair{640, 360}, std::pair{960, 600}})
        {
            INFO(size.first << " x " << size.second);
            editor->setSize(size.first, size.second);
            tickEditor(*editor, 10);
            const juce::Component *perform = viewById(*editor, "view.perform");
            const auto *name = findDeep(*perform, "sampleName");
            const auto *status =
                dynamic_cast<const juce::Label *>(findDeep(*perform, "sampleStatus"));
            const auto *readout =
                dynamic_cast<const juce::Label *>(findDeep(*perform, "loader.memory"));
            REQUIRE((name != nullptr && status != nullptr && readout != nullptr));
            CHECK(name->getWidth() >= 120);
            CHECK(status->getWidth() >= 120);
            // The readout's text fits its label, measured in its font.
            const float text =
                juce::GlyphArrangement::getStringWidth(readout->getFont(), readout->getText());
            const int room =
                readout->getBorderSize().subtractedFrom(readout->getLocalBounds()).getWidth();
            CHECK(text <= static_cast<float>(room));
            CHECK(readout->getText() == "1023 MB of 1.5 GB");
            // Narrow, the folder and Collect sit in the "..." menu; wide, they are buttons.
            const bool narrow = size.first == 640;
            CHECK(findDeep(*perform, "loader.more")->isVisible() == narrow);
            CHECK(findDeep(*perform, "sampleFolder")->isVisible() == !narrow);
            CHECK(findDeep(*perform, "loader.collect")->isVisible() == !narrow);
            // Everything stays inside the strip, with no overlaps among the visible parts.
            const auto *strip = findDeep(*perform, "sampleStrip");
            std::vector<juce::Rectangle<int>> boxes;
            for (const juce::Component *child : strip->getChildren())
            {
                if (child->isVisible())
                {
                    CHECK(strip->getLocalBounds().contains(child->getBounds()));
                    for (const auto &other : boxes)
                    {
                        CHECK_FALSE(other.intersects(child->getBounds()));
                    }
                    boxes.push_back(child->getBounds());
                }
            }
        }
    }
}

TEST_CASE("a refused drop is told in the header when the strip is hidden", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    recording.context.showSampleLoader = true;
    const auto editor = gg::ui::makeEditor(recording.context);
    auto *target = dynamic_cast<juce::FileDragAndDropTarget *>(editor.get());
    REQUIRE(target != nullptr);
    target->filesDropped({"/drops/notes.txt"}, 0, 0);
    CHECK(labelText(*editor, "sampleStatus") ==
          "can't load notes.txt: not a WAV, AIFF, FLAC or MP3 file");
    CHECK_FALSE(findDeep(*editor, "header.notice")->isVisible()); // Perform shows it in the strip
    REQUIRE(gg::ui::selectView(*editor, gg::ui::View::Edit));
    target->filesDropped({"/drops/notes.txt"}, 0, 0);
    CHECK(findDeep(*editor, "header.notice")->isVisible());
    CHECK(labelText(*editor, "header.notice") ==
          "can't load notes.txt: not a WAV, AIFF, FLAC or MP3 file");
    // Either separator gives the name, on every OS.
    target->filesDropped({"C:\\drops\\more.txt"}, 0, 0);
    CHECK(labelText(*editor, "header.notice") ==
          "can't load more.txt: not a WAV, AIFF, FLAC or MP3 file");
}

TEST_CASE("publishing follows gui destroy without a parent", "[plugin]")
{
    // Task 10's minor (a): the shim destroys its editor in guiDestroy only once a parent was
    // attached, so a host that creates and destroys the GUI without set_parent left publishing
    // on. The plugin's own guiDestroy turns it off.
#if defined(__APPLE__)
    const char *const api = CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32)
    const char *const api = CLAP_WINDOW_API_WIN32;
#else
    const char *const api = CLAP_WINDOW_API_X11;
#endif
    {
        gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
        REQUIRE(fx.valid());
        auto *plugin = dynamic_cast<gg::plugin::EditorPlugin *>(&fx.impl());
        REQUIRE(plugin != nullptr);
        const auto *gui =
            static_cast<const clap_plugin_gui *>(fx.clap()->get_extension(fx.clap(), CLAP_EXT_GUI));
        REQUIRE(gui != nullptr);
        REQUIRE(gui->create(fx.clap(), api, false));
        CHECK(plugin->publishing());
        gui->destroy(fx.clap());
        CHECK_FALSE(plugin->publishing());
        // The editor the shim kept goes with the plugin, and counts nothing closed again. (A
        // second create would replace it, which the shim asserts against in a debug build.)
    }

    // The size a host offers is held at the minimum, and any larger size is taken as it is.
    gg::test::PluginUnderTest fx("org.sethbc.giga-grains-fx");
    REQUIRE(fx.valid());
    const auto *gui =
        static_cast<const clap_plugin_gui *>(fx.clap()->get_extension(fx.clap(), CLAP_EXT_GUI));
    REQUIRE(gui != nullptr);
    REQUIRE(gui->create(fx.clap(), api, false));
    uint32_t width = 100;
    uint32_t height = 100;
    CHECK(gui->adjust_size(fx.clap(), &width, &height));
    CHECK(width == static_cast<uint32_t>(gg::ui::kEditorMinWidth));
    CHECK(height == static_cast<uint32_t>(gg::ui::kEditorMinHeight));
    width = 1200;
    height = 700;
    CHECK(gui->adjust_size(fx.clap(), &width, &height));
    CHECK(width == 1200);
    CHECK(height == 700);
    gui->destroy(fx.clap());
}

TEST_CASE("the theme setting is read and written", "[plugin]")
{
    // The editor's theme is per machine (M2 sub-project 6's spec §6.9), in the settings file
    // the loader keeps its folders in. No GUI: the setting is file I/O on the main thread, so
    // this runs on every leg with the editor.
    const auto dir = gg::test::sampleDir("theme-setting");
    const auto file = dir / "settings.xml";
    std::filesystem::remove(file);
    gg::test::useSettingsFile(file);
    CHECK(gg::plugin::readTheme() == gg::plugin::ThemeName::Dark); // no file yet

    // The loader's sample folder, written first, survives the theme's writes.
    {
        gg::test::PluginUnderTest instrument("org.sethbc.giga-grains");
        REQUIRE(instrument.valid());
        auto *plugin = dynamic_cast<gg::plugin::EditorPlugin *>(&instrument.impl());
        REQUIRE(plugin != nullptr);
        REQUIRE(plugin->sampleLoader() != nullptr);
        REQUIRE(plugin->sampleLoader()->setSampleFolder(dir / "library"));
    }
    const std::string library = gg::media::loadSettings(file).sampleFolder;
    REQUIRE_FALSE(library.empty());

    CHECK(gg::plugin::writeTheme(gg::plugin::ThemeName::Light));
    CHECK(gg::plugin::readTheme() == gg::plugin::ThemeName::Light);
    CHECK(gg::media::loadSettings(file).theme == "light");
    CHECK(gg::media::loadSettings(file).sampleFolder == library);

    CHECK(gg::plugin::writeTheme(gg::plugin::ThemeName::Dark));
    CHECK(gg::plugin::readTheme() == gg::plugin::ThemeName::Dark);
    CHECK(gg::media::loadSettings(file).theme == "dark");
    CHECK(gg::media::loadSettings(file).sampleFolder == library);

    // Anything but "light" is the dark theme.
    gg::core::MachineSettings odd = gg::media::loadSettings(file);
    odd.theme = "Light";
    REQUIRE(gg::media::saveSettings(file, odd));
    CHECK(gg::plugin::readTheme() == gg::plugin::ThemeName::Dark);
}

TEST_CASE("a theme write into an unwritable folder fails cleanly", "[plugin]")
{
    // The settings file's folder can't be made: a regular file stands where it would go.
    const auto dir = gg::test::sampleDir("theme-unwritable");
    const auto blocker = dir / "not-a-folder";
    std::filesystem::remove_all(blocker);
    gg::test::writeFile(blocker, std::vector<uint8_t>{'x'});
    REQUIRE(std::filesystem::is_regular_file(blocker));
    gg::test::useSettingsFile(blocker / "settings.xml");

    bool written = true;
    REQUIRE_NOTHROW(written = gg::plugin::writeTheme(gg::plugin::ThemeName::Light));
    CHECK_FALSE(written);
    CHECK(gg::plugin::readTheme() == gg::plugin::ThemeName::Dark);
    CHECK(std::filesystem::is_regular_file(blocker));
}

TEST_CASE("the strip's controls are the layer's parameters", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::LayerStrip strip(recording.context, gg::ui::LayerStrip::Layout::Row);
    strip.setSize(800, 80);
    CHECK(strip.getComponentID() == "strip");
    const auto id = [](ParamSlot s) { return "param." + std::to_string(gg::core::paramId(s)); };
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        const auto letter = "strip." + std::to_string(layer) + ".letter";
        CHECK(findDeep(strip, letter.c_str()) != nullptr);
        for (const ParamSlot slot : {gg::core::layerOnSlot(layer), gg::core::levelSlot(layer),
                                     gg::core::layerSlot(gg::core::LayerParam::Source, layer),
                                     gg::core::layerSlot(gg::core::LayerParam::Freeze, layer)})
        {
            auto *control = findDeep(strip, id(slot).c_str());
            REQUIRE(control != nullptr);
            CHECK_FALSE(control->getBounds().isEmpty());
        }
    }
    CHECK(findDeep(strip, id(ParamSlot::LayerBOn).c_str()) != nullptr);
    CHECK(findDeep(strip, id(ParamSlot::LevelB).c_str()) != nullptr);
    CHECK(findDeep(strip, id(ParamSlot::B_Source).c_str()) != nullptr);
    CHECK(findDeep(strip, id(ParamSlot::B_Freeze).c_str()) != nullptr);

    // The column places each layer's cell where its lane's rows are.
    gg::ui::LayerStrip column(recording.context, gg::ui::LayerStrip::Layout::Column);
    column.setSize(120, 400);
    column.setLaneBounds({juce::Rectangle<int>(0, 0, 120, 100),
                          {0, 100, 120, 100},
                          {0, 200, 120, 100},
                          {0, 300, 120, 100}});
    CHECK(column.getLocalBounds().contains(findDeep(column, "strip.3.letter")->getBounds()));
    CHECK(findDeep(column, "strip.3.letter")->getY() >= 300);
}

TEST_CASE("a narrow strip draws the Source kinds' short forms and keeps their names", "[plugin]")
{
    // The editor at its smallest (640 x 360) leaves the strip's Source button too narrow for the
    // longer kinds: the button draws a short form, while the list and the accessibility value keep
    // the name. Freeze takes from On what its label needs.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    sst::jucegui::style::StyleSheet::initializeStyleSheets([] {});
    RecordingContext recording(gg::core::Mode::Fx);
    for (const int width : {800, 400})
    {
        INFO("width " << width);
        gg::ui::LayerStrip strip(recording.context, gg::ui::LayerStrip::Layout::Row);
        strip.setSize(width, 64);
        auto *source = dynamic_cast<gg::ui::ParamControl *>(
            findDeep(strip, controlId(ParamSlot::B_Source).toRawUTF8()));
        auto *freeze = dynamic_cast<gg::ui::ParamControl *>(
            findDeep(strip, controlId(ParamSlot::B_Freeze).toRawUTF8()));
        REQUIRE(source != nullptr);
        REQUIRE(freeze != nullptr);
        CHECK(freeze->labelFits());
        CHECK(freeze->shownLabel() == "Freeze");
        auto *menu = childOfType<sst::jucegui::components::MenuButtonDiscreteEditor>(*source);
        REQUIRE(menu != nullptr);
        REQUIRE(menu->data != nullptr);
        menu->setStyle(sst::jucegui::style::StyleSheet::getBuiltInStyleSheet(
            sst::jucegui::style::StyleSheet::DARK)); // the editor's, for the button's font
        strip
            .sendLookAndFeelChange(); // as the editor's theme switch does: the strip lays out again
        CHECK(source->labelFits());
        const int oscillator = static_cast<int>(gg::core::SourceKind::Oscillator);
        CHECK(menu->data->getValueAsStringFor(oscillator) == "Oscillator"); // the list's text
        CHECK(source->menuText(oscillator) == (width == 800 ? "Oscillator" : "Osc"));
        CHECK(source->menuText(static_cast<int>(gg::core::SourceKind::Live)) == "Live");
        // The accessibility value is the kind's name, whatever the button draws.
        recording.values[static_cast<std::size_t>(ParamSlot::B_Source)] =
            static_cast<float>(oscillator);
        strip.refresh();
        const std::unique_ptr<juce::AccessibilityHandler> handler(
            menu->createAccessibilityHandler());
        REQUIRE(handler != nullptr);
        REQUIRE(handler->getValueInterface() != nullptr);
        CHECK(handler->getValueInterface()->getCurrentValueAsString() == "Oscillator");
        recording.values[static_cast<std::size_t>(ParamSlot::B_Source)] =
            gg::core::paramMeta(ParamSlot::B_Source, gg::core::Mode::Fx).defaultVal;
    }
}

TEST_CASE("selecting a layer in the strip", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::LayerStrip strip(recording.context, gg::ui::LayerStrip::Layout::Row);
    strip.setSize(800, 80);
    std::vector<int> selected;
    strip.onSelectLayer = [&selected](uint8_t layer) { selected.push_back(layer); };
    CHECK(strip.selectedLayer() == 0);

    juce::Component *letter = findDeep(strip, "strip.2.letter");
    REQUIRE(letter != nullptr);
    letter->mouseDown(mouseAt(*letter, 4.0f, 4.0f, {4.0f, 4.0f}, false));
    letter->mouseUp(mouseAt(*letter, 4.0f, 4.0f, {4.0f, 4.0f}, false));
    REQUIRE(selected.size() == 1);
    CHECK(selected[0] == 2);
    CHECK(strip.selectedLayer() == 2);
    CHECK(recording.calls.empty()); // selecting is no parameter's edit
}

namespace
{
/// A pad of 200 x 200 in a context with an undo stack that records what it applies.
struct PadFixture
{
    RecordingContext recording{gg::core::Mode::Fx};
    std::vector<std::pair<ParamSlot, float>> undone;
    gg::ui::UndoStack undo{{.setValue = [this](ParamSlot s, float v) { undone.emplace_back(s, v); },
                            .setRoutes = [](const gg::core::ModRoutes &) {},
                            .setValues = {}}};
    std::unique_ptr<gg::ui::MorphPad> pad;

    PadFixture()
    {
        recording.context.undo = &undo;
        pad = std::make_unique<gg::ui::MorphPad>(recording.context);
        pad->setSize(200, 200);
    }

    float value(ParamSlot s) const { return recording.values[static_cast<std::size_t>(s)]; }
};
} // namespace

TEST_CASE("the pad's drag is one gesture on both morphs", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    PadFixture f;
    gg::ui::MorphPad &pad = *f.pad;
    CHECK(pad.getComponentID() == "pad");
    REQUIRE(f.value(ParamSlot::MorphX) == 0.5f);
    REQUIRE(f.value(ParamSlot::MorphY) == 0.5f);

    const juce::Point<float> centre{100.0f, 100.0f};
    pad.mouseDown(mouseAt(pad, 100.0f, 100.0f, centre, false));
    pad.mouseDrag(mouseAt(pad, 150.0f, 50.0f, centre, true));
    pad.mouseDrag(mouseAt(pad, 260.0f, -40.0f, centre, true)); // past the corner: pinned
    pad.mouseUp(mouseAt(pad, 260.0f, -40.0f, centre, true));

    const auto &calls = f.recording.calls;
    REQUIRE(calls.size() >= 6);
    CHECK(calls[0].kind == 'b');
    CHECK(calls[0].slot == ParamSlot::MorphX);
    CHECK(calls[1].kind == 'b');
    CHECK(calls[1].slot == ParamSlot::MorphY);
    CHECK(calls[calls.size() - 2].kind == 'e');
    CHECK(calls[calls.size() - 2].slot == ParamSlot::MorphX);
    CHECK(calls.back().kind == 'e');
    CHECK(calls.back().slot == ParamSlot::MorphY);
    int begins = 0;
    for (std::size_t i = 2; i + 2 < calls.size(); ++i)
    {
        CHECK(calls[i].kind == 'v');
        begins += calls[i].kind == 'b';
    }
    CHECK(begins == 0);
    CHECK_THAT(f.value(ParamSlot::MorphX), Catch::Matchers::WithinAbs(1.0, 1e-3));
    CHECK_THAT(f.value(ParamSlot::MorphY), Catch::Matchers::WithinAbs(1.0, 1e-3));

    // One undo group: a single entry, labelled "Morph", whose undo restores both.
    CHECK(f.undo.size() == 1);
    CHECK(f.undo.undoLabel() == "Morph");
    f.undo.undo();
    REQUIRE(f.undone.size() == 2);
    for (const auto &[slot, v] : f.undone)
    {
        CHECK((slot == ParamSlot::MorphX || slot == ParamSlot::MorphY));
        CHECK(v == 0.5f);
    }
}

TEST_CASE("the pad works without an undo stack", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::MorphPad pad(recording.context);
    pad.setSize(200, 100); // the square is the 100 x 100 in the middle
    const juce::Point<float> centre{100.0f, 50.0f};
    pad.mouseDown(mouseAt(pad, 100.0f, 50.0f, centre, false));
    pad.mouseDrag(mouseAt(pad, 50.0f, 100.0f, centre, true)); // the square's bottom-left corner
    pad.mouseUp(mouseAt(pad, 50.0f, 100.0f, centre, true));
    CHECK(recording.kinds() == "bbvvvvee");
    CHECK_THAT(recording.values[static_cast<std::size_t>(ParamSlot::MorphX)],
               Catch::Matchers::WithinAbs(0.0, 1e-3));
    CHECK_THAT(recording.values[static_cast<std::size_t>(ParamSlot::MorphY)],
               Catch::Matchers::WithinAbs(0.0, 1e-3));
}

TEST_CASE("double-click centres the pad", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    PadFixture f;
    f.recording.values[static_cast<std::size_t>(ParamSlot::MorphX)] = 0.9f;
    f.recording.values[static_cast<std::size_t>(ParamSlot::MorphY)] = 0.1f;
    f.pad->refresh();
    f.pad->mouseDoubleClick(mouseAt(*f.pad, 20.0f, 20.0f, {20.0f, 20.0f}, false));
    CHECK(f.recording.kinds() == "bbvvee");
    CHECK(f.value(ParamSlot::MorphX) == 0.5f);
    CHECK(f.value(ParamSlot::MorphY) == 0.5f);
    CHECK(f.undo.size() == 1);
}

TEST_CASE("a real double-click sequence centres the pad in one undo entry", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    PadFixture f;
    f.recording.values[static_cast<std::size_t>(ParamSlot::MorphX)] = 0.9f;
    f.recording.values[static_cast<std::size_t>(ParamSlot::MorphY)] = 0.1f;
    f.pad->refresh();
    gg::ui::MorphPad &pad = *f.pad;
    const juce::Point<float> at{20.0f, 20.0f};
    const auto click = [&](int clicks) {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(
            juce::Desktop::getInstance().getMainMouseSource(), at,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
            juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
            juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
            juce::MouseInputSource::defaultTiltY, &pad, &pad, now, at, now, clicks, false);
    };
    // JUCE's order: press, release, press (2 clicks), release, then mouseDoubleClick.
    pad.mouseDown(click(1));
    pad.mouseUp(click(1));
    pad.mouseDown(click(2));
    pad.mouseUp(click(2));
    pad.mouseDoubleClick(click(2));
    CHECK(f.value(ParamSlot::MorphX) == 0.5f);
    CHECK(f.value(ParamSlot::MorphY) == 0.5f);
    // The first press is an ordinary click (one entry); the double-click itself adds one.
    CHECK(f.undo.size() == 2);
    f.undo.undo();
    f.undo.undo();
    REQUIRE(f.undone.size() == 4);
    for (std::size_t i = 2; i < 4; ++i)
    {
        CHECK(f.undone[i].second == (f.undone[i].first == ParamSlot::MorphX ? 0.9f : 0.1f));
    }
}

TEST_CASE("arrows move the pad", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    PadFixture f;
    gg::ui::MorphPad &pad = *f.pad;
    CHECK(pad.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK_THAT(f.value(ParamSlot::MorphX), Catch::Matchers::WithinAbs(0.51, 1e-6));
    CHECK(f.value(ParamSlot::MorphY) == 0.5f);
    CHECK(f.recording.kinds() == "bbvvee");

    CHECK(pad.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    CHECK_THAT(f.value(ParamSlot::MorphY), Catch::Matchers::WithinAbs(0.51, 1e-6));
    CHECK(pad.keyPressed(
        juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0)));
    CHECK_THAT(f.value(ParamSlot::MorphX), Catch::Matchers::WithinAbs(0.509, 1e-6));
    CHECK(pad.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    CHECK_THAT(f.value(ParamSlot::MorphY), Catch::Matchers::WithinAbs(0.50, 1e-6));
    CHECK_FALSE(pad.keyPressed(juce::KeyPress('x')));
    CHECK(f.undo.size() == 4);
}

TEST_CASE("held arrow keys make one undo entry on the pad", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    PadFixture f;
    for (int i = 0; i < 20; ++i)
    {
        REQUIRE(f.pad->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    }
    CHECK_THAT(f.value(ParamSlot::MorphX), Catch::Matchers::WithinAbs(0.7, 1e-5));
    CHECK(f.undo.size() == 1);
    f.undo.undo();
    REQUIRE(f.undone.size() == 1); // only Morph X moved
    CHECK(f.undone[0] == std::pair{ParamSlot::MorphX, 0.5f});
}

TEST_CASE("the pad's ghost handle follows setGhost", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    PadFixture f;
    gg::ui::MorphPad &pad = *f.pad;
    const auto shot = [&pad] { return pad.createComponentSnapshot(pad.getLocalBounds(), false); };
    const auto same = [](const juce::Image &a, const juce::Image &b) {
        if (a.getBounds() != b.getBounds())
        {
            return false;
        }
        for (int y = 0; y < a.getHeight(); ++y)
        {
            for (int x = 0; x < a.getWidth(); ++x)
            {
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                {
                    return false;
                }
            }
        }
        return true;
    };
    const juce::Image none = shot();
    pad.setGhost(gg::ui::PadPoint{0.2f, 0.8f});
    const juce::Image first = shot();
    CHECK_FALSE(same(none, first)); // the ghost is drawn
    pad.setGhost(gg::ui::PadPoint{0.8f, 0.2f});
    const juce::Image moved = shot();
    CHECK_FALSE(same(first, moved)); // and it follows
    pad.setGhost(std::nullopt);
    CHECK(same(shot(), none)); // nothing for none
    pad.setGhost(gg::ui::PadPoint{std::nanf(""), 0.5f});
    CHECK(same(shot(), none)); // a non-finite point draws nothing
}

TEST_CASE("the strip letter's other cells select their layer", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    for (const auto layout : {gg::ui::LayerStrip::Layout::Row, gg::ui::LayerStrip::Layout::Column})
    {
        RecordingContext recording(gg::core::Mode::Fx);
        gg::ui::LayerStrip strip(recording.context, layout);
        strip.setSize(layout == gg::ui::LayerStrip::Layout::Row ? 800 : 160,
                      layout == gg::ui::LayerStrip::Layout::Row ? 80 : 400);
        std::vector<int> selected;
        strip.onSelectLayer = [&selected](uint8_t layer) { selected.push_back(layer); };
        for (const int layer : {3, 1, 0, 2})
        {
            const auto id = "strip." + std::to_string(layer) + ".letter";
            juce::Component *letter = findDeep(strip, id.c_str());
            REQUIRE(letter != nullptr);
            letter->mouseDown(mouseAt(*letter, 4.0f, 4.0f, {4.0f, 4.0f}, false));
            letter->mouseUp(mouseAt(*letter, 4.0f, 4.0f, {4.0f, 4.0f}, false));
            CHECK(strip.selectedLayer() == layer);
        }
        CHECK(selected == std::vector<int>{3, 1, 0, 2});
        CHECK(recording.calls.empty());
    }
}

TEST_CASE("LayerStrip refresh follows the base values", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Fx);
    gg::ui::LayerStrip strip(recording.context, gg::ui::LayerStrip::Layout::Row);
    strip.setSize(800, 80);
    const auto control = [&strip](ParamSlot slot) {
        const auto id = "param." + std::to_string(gg::core::paramId(slot));
        return dynamic_cast<gg::ui::ParamControl *>(findDeep(strip, id.c_str()));
    };
    for (uint8_t layer = 0; layer < gg::core::kLayers; ++layer)
    {
        const ParamSlot on = gg::core::layerOnSlot(layer);
        const ParamSlot level = gg::core::levelSlot(layer);
        const ParamSlot freeze = gg::core::layerSlot(gg::core::LayerParam::Freeze, layer);
        REQUIRE(control(on) != nullptr);
        REQUIRE(control(level) != nullptr);
        REQUIRE(control(freeze) != nullptr);
        const float level0 = control(level)->shownValue();
        const float on0 = control(on)->shownValue();
        // The host (or the audio thread) moves the base values; the strip follows at refresh.
        const auto &levelMeta = gg::core::paramMeta(level, gg::core::Mode::Fx);
        const float level1 =
            levelMeta.defaultVal == levelMeta.minVal ? levelMeta.maxVal : levelMeta.minVal;
        recording.values[static_cast<std::size_t>(level)] = level1;
        recording.values[static_cast<std::size_t>(on)] = on0 > 0.5f ? 0.0f : 1.0f;
        recording.values[static_cast<std::size_t>(freeze)] = 1.0f;
        CHECK(control(level)->shownValue() == level0); // not before
        strip.refresh();
        CHECK(control(level)->shownValue() == level1);
        CHECK(control(on)->shownValue() == (on0 > 0.5f ? 0.0f : 1.0f));
        CHECK(control(freeze)->shownValue() == 1.0f);
    }
    CHECK(recording.calls.empty()); // following the base values is no gesture
}

TEST_CASE("the About box lists the license set", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    const auto notices = gg::ui::noticeTexts();
    REQUIRE(!notices.empty());

    gg::ui::AboutBox box("Giga Grains FX", "VST3");
    box.setBounds(0, 0, 800, 520);

    const auto *list = dynamic_cast<const juce::ListBox *>(findDeep(box, "about.list"));
    REQUIRE(list != nullptr);
    REQUIRE(list->getListBoxModel() != nullptr);
    REQUIRE(list->getListBoxModel()->getNumRows() == static_cast<int>(notices.size()));
    for (std::size_t i = 0; i < notices.size(); ++i)
    {
        CHECK(box.rowName(static_cast<int>(i)) ==
              juce::String::fromUTF8(notices[i].name.data(),
                                     static_cast<int>(notices[i].name.size())));
    }

    // The product, the version and the format are shown; the build's paths never are.
    const auto *version = dynamic_cast<const juce::Label *>(findDeep(box, "about.version"));
    REQUIRE(version != nullptr);
    CHECK(version->getText().contains(
        juce::String::fromUTF8(gg::kVersion.data(), static_cast<int>(gg::kVersion.size()))));
    const auto *format = dynamic_cast<const juce::Label *>(findDeep(box, "about.format"));
    REQUIRE(format != nullptr);
    CHECK(format->getText().contains("VST3"));
    // The corresponding source's link: the repository's URL, as the text and as the target.
    const auto *repository =
        dynamic_cast<const juce::HyperlinkButton *>(findDeep(box, "about.repository"));
    REQUIRE(repository != nullptr);
    CHECK(repository->getButtonText() == "https://github.com/sethbc/giga-grains");
    CHECK(repository->getURL().toString(true) == "https://github.com/sethbc/giga-grains");

    // The first text shown is the notices' first.
    const std::string_view first = notices[0].text.substr(0, 40);
    CHECK(box.shownText().startsWith(
        juce::String::fromUTF8(first.data(), static_cast<int>(first.size()))));
    CHECK_FALSE(box.shownText().contains(juce::String::fromUTF8(
        notices[0].sourcePath.data(), static_cast<int>(notices[0].sourcePath.size()))));

    // Another row shows its own text; "Plain text" shows the Markdown raw.
    if (notices.size() > 1)
    {
        box.selectNotice(1);
        const std::string_view second = notices[1].text.substr(0, 40);
        CHECK(box.shownText().startsWith(
            juce::String::fromUTF8(second.data(), static_cast<int>(second.size()))));
    }
    box.selectNotice(0);
    box.setPlainText(true);
    CHECK(box.shownText().startsWith(
        juce::String::fromUTF8(first.data(), static_cast<int>(first.size()))));
}

TEST_CASE("Escape closes the About box", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::ui::AboutBox box("Giga Grains", "CLAP");
    int closed = 0;
    box.onClose = [&closed] { ++closed; };
    box.setBounds(0, 0, 800, 520);
    box.setVisible(true);
    REQUIRE(box.isVisible());
    CHECK(box.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(box.isVisible());
    CHECK(closed == 1);

    // The Close button does the same.
    box.setVisible(true);
    auto *close = dynamic_cast<juce::Button *>(findDeep(box, "about.close"));
    REQUIRE(close != nullptr);
    REQUIRE(close->onClick);
    close->onClick(); // triggerClick() does nothing off screen
    CHECK_FALSE(box.isVisible());
    CHECK(closed == 2);
}
#else
TEST_CASE("headless build exposes no gui extension", "[plugin]")
{
    // Without the editor (GG_BUILD_EDITOR=OFF, as in the asan and rtsan presets) the plugins
    // are the JUCE-free core: a host must find no GUI, rather than one that can't open.
    for (const char *id : {"org.sethbc.giga-grains", "org.sethbc.giga-grains-fx"})
    {
        INFO(id);
        gg::test::PluginUnderTest plugin(id);
        REQUIRE(plugin.valid());
        CHECK(plugin.clap()->get_extension(plugin.clap(), CLAP_EXT_GUI) == nullptr);
    }
}
#endif
