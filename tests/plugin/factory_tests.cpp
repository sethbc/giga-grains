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
#if GG_BUILD_EDITOR
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
#include <sst/jucegui/data/Continuous.h>
#include <sst/jucegui/style/StyleSheet.h>

#include "GrainCanvas.h" // ui/src: the editor's own components
#include "LayerStrip.h"
#include "LoaderStrip.h"
#include "MacroRow.h"
#include "MorphPad.h"
#include "Palette.h"
#include "ParamControl.h"
#include "editor/EditorPlugin.h"
#include "editor/UiSettings.h"
#include "gg/core/GrainQueue.h"
#include "gg/core/ParamIds.h"
#include "gg/core/UiSnapshot.h"
#include "gg/media/SettingsFile.h"
#include "gg/ui/Editor.h"
#include "gg/ui/LaneModel.h"
#include "gg/ui/UndoStack.h"
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

// The editor's sample loader strip (spec §6.5). These create the editor itself, so they start
// JUCE's GUI as the shim's guiCreate does, but open no window: the strip's widgets are found by
// their component IDs, and a drop is simulated by calling the editor's filesDropped.
namespace
{
/// The component with this ID anywhere in the editor (the strip's widgets are the strip's
/// children), or null.
const juce::Component *stripChild(const juce::Component &editor, const char *id)
{
    for (const juce::Component *child : editor.getChildren())
    {
        if (child->getComponentID() == id)
        {
            return child;
        }
        if (const juce::Component *found = stripChild(*child, id))
        {
            return found;
        }
    }
    return nullptr;
}

juce::String labelText(const juce::Component &editor, const char *id)
{
    const auto *label = dynamic_cast<const juce::Label *>(stripChild(editor, id));
    return label != nullptr ? label->getText() : juce::String("<no label>");
}

/// Whether the strip shows "Relink…"; fails the test if the strip has no such button.
bool relinkShown(const juce::Component &editor)
{
    const auto *relink = stripChild(editor, "sampleRelink");
    REQUIRE(relink != nullptr);
    return relink->isVisible();
}

juce::String utf8String(const std::filesystem::path &path)
{
    return juce::String::fromUTF8(gg::test::utf8(path).c_str());
}
} // namespace

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

TEST_CASE("the fx's gui has no sample loader strip", "[plugin]")
{
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
        CHECK(stripChild(*editor, id) == nullptr);
    }
    auto *target = dynamic_cast<juce::FileDragAndDropTarget *>(editor.get());
    CHECK((target == nullptr || !target->isInterestedInFileDrag({"/some/file.wav"})));
}

TEST_CASE("the gui's loader strip shows the sample's status and offers relink", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    gg::ui::SampleStatusView view;
    std::string loaded;
    gg::ui::EditorContext context;
    context.productName = "Giga Grains";
    context.showSampleLoader = true;
    context.loadSample = [&loaded](const std::string &path) { loaded = path; };
    context.sampleStatus = [&view] { return view; };
    using State = gg::ui::SampleStatusView::State;

    SECTION("loaded, with the file's length, channels and rate")
    {
        view = {State::Loaded, "kick.wav", {}, 1.5, 2, 44100.0};
        const auto editor = gg::ui::makeEditor(context);
        CHECK(labelText(*editor, "sampleName") == "kick.wav");
        CHECK(labelText(*editor, "sampleStatus") == "loaded: 1.50 s, 2 ch, 44100 Hz");
        CHECK_FALSE(relinkShown(*editor));

        // A drop passes the first file's path, as UTF-8, to loadSample.
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
        CHECK(context.livePeaks == nullptr);
        CHECK(context.clock == nullptr);
        REQUIRE(context.baseValue);
        CHECK(context.baseValue(ParamSlot::Mix) == 0.5f);
        REQUIRE(context.sampleWaveform);
        CHECK(context.sampleWaveform().peaks == nullptr); // the FX has no sample

        REQUIRE(fx.activate(44100.0, 512));
        auto *processor = plugin->processor();
        REQUIRE(processor != nullptr);
        CHECK(context.grains == &processor->grainQueue());
        CHECK(context.snapshot == &processor->snapshot());
        CHECK(context.livePeaks == &processor->livePeaks());
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
    REQUIRE(context.sampleWaveform);
    const auto waveform = context.sampleWaveform();
    REQUIRE(waveform.peaks != nullptr);
    CHECK(waveform.peaks == plugin->sampleLoader()->waveform().peaks);
    CHECK(waveform.frames == 192000);
    CHECK(waveform.seconds == 4.0);
}

// The Perform view (sub-project 5's spec §6): the header, the canvas, the macro row and the
// instrument's loader strip, built headless as above. A context whose callbacks record what the
// editor asks of the plugin stands in for the plugin where the test needs to see the calls.
namespace
{
using gg::core::ParamSlot;

/// What the editor asked of the plugin: 'b'egin, 'v'alue and 'e'nd, in order.
struct GestureCall
{
    char kind;
    ParamSlot slot;
    double value;
};

/// An EditorContext whose base values start at the product's defaults and follow setValue at
/// once (as the plugin's do once the audio thread takes them), and which records every gesture.
struct RecordingContext
{
    gg::ui::EditorContext context;
    std::vector<GestureCall> calls;
    std::array<float, gg::core::kParamCount> values{};

    explicit RecordingContext(gg::core::Mode mode)
    {
        context.productName = mode == gg::core::Mode::Fx ? "Giga Grains FX" : "Giga Grains";
        context.mode = mode;
        for (std::size_t i = 0; i < gg::core::kParamCount; ++i)
        {
            values[i] = gg::core::paramMeta(static_cast<ParamSlot>(i), mode).defaultVal;
        }
        context.baseValue = [this](ParamSlot s) { return values[static_cast<std::size_t>(s)]; };
        context.beginGesture = [this](ParamSlot s) { calls.push_back({'b', s, 0.0}); };
        context.setValue = [this](ParamSlot s, double v) {
            calls.push_back({'v', s, v});
            values[static_cast<std::size_t>(s)] = static_cast<float>(v);
        };
        context.endGesture = [this](ParamSlot s) { calls.push_back({'e', s, 0.0}); };
    }

    /// The kinds of the calls so far, as a string ("bvve").
    std::string kinds() const
    {
        std::string out;
        for (const GestureCall &c : calls)
        {
            out += c.kind;
        }
        return out;
    }
};

/// The component with this ID anywhere below `root`, or null.
juce::Component *findDeep(const juce::Component &root, const char *id)
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

/// A mouse event at (x, y) in `c`'s coordinates, the left button (or `button`) down since `down`.
juce::MouseEvent mouseAt(juce::Component &c, float x, float y, juce::Point<float> down,
                         bool dragged, int button = juce::ModifierKeys::leftButtonModifier)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(), {x, y}, juce::ModifierKeys(button),
        juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
        juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
        juce::MouseInputSource::defaultTiltY, &c, &c, now, down, now, 1, dragged);
}

/// The canvas's lane geometry as the canvas lays it out before its first paint (one physical
/// pixel per logical one).
gg::ui::LaneGeometry laneOf(const juce::Component &canvas, bool fx, double sampleRate,
                            int64_t sourceFrames)
{
    return {.width = static_cast<float>(canvas.getWidth()),
            .height = static_cast<float>(canvas.getHeight()),
            .pixelScale = 1.0f,
            .fxLane = fx,
            .sampleRate = sampleRate,
            .ringSeconds = 16.0,
            .sourceFrames = sourceFrames};
}

/// One second of a 48 kHz instrument sample, for the instrument's lane.
gg::ui::SampleWaveform oneSecondSample()
{
    return {std::make_shared<const std::vector<float>>(94, 0.5f), 48000, 1.0};
}
/// One sample status, every field given.
gg::ui::SampleStatusView statusView(gg::ui::SampleStatusView::State state,
                                    std::string fileName = {}, std::string message = {},
                                    double seconds = 0.0, uint32_t channels = 0,
                                    double fileRate = 0.0, bool relinkable = false)
{
    return {state,    std::move(fileName), std::move(message), seconds, channels,
            fileRate, relinkable};
}
} // namespace

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
        CHECK(text("header.preset") == "Init");
        const auto *perform =
            dynamic_cast<const juce::Button *>(findDeep(*editor, "header.perform"));
        REQUIRE(perform != nullptr);
        CHECK(perform->getToggleState());
        CHECK(perform->isEnabled());
        const auto *edit = findDeep(*editor, "header.edit");
        REQUIRE(edit != nullptr);
        CHECK_FALSE(edit->isEnabled()); // until M2

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

        // The loader strip is the instrument's alone (the FX's absence is tested above).
        CHECK((findDeep(*editor, "sampleLoad") != nullptr) == product.instrument);
        CHECK((findDeep(*editor, "sampleStatus") != nullptr) == product.instrument);

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

TEST_CASE("a canvas drag emits begin, values, end once", "[plugin]")
{
    // Review Focus 5: a drag that ends outside the window, or with the editor closing, ends its
    // gesture exactly once, and the last value sent is the last drag position.
    const juce::ScopedJuceInitialiser_GUI juceGui;

    SECTION("the instrument's window body sets Position, released outside the canvas")
    {
        RecordingContext recording(gg::core::Mode::Instrument);
        recording.values[static_cast<std::size_t>(ParamSlot::Position)] = 0.5f;
        recording.values[static_cast<std::size_t>(ParamSlot::Spray)] = 0.4f;
        recording.context.sampleWaveform = [] { return oneSecondSample(); };
        recording.context.sampleRate = 48000.0;
        auto editor = gg::ui::makeEditor(recording.context);
        juce::Component *canvas = findDeep(*editor, "canvas");
        REQUIRE(canvas != nullptr);
        const auto lane = laneOf(*canvas, false, 48000.0, 48000);
        const auto window = gg::ui::scanWindow(0.5f, 0.4f, lane);
        const float centre = 0.5f * (window.x0 + window.x1);
        const float y = 0.5f * static_cast<float>(canvas->getHeight());
        REQUIRE(gg::ui::hitTest(centre, window, 6.0f, lane) == gg::ui::DragTarget::Body);

        const juce::Point<float> down{centre, y};
        canvas->mouseDown(mouseAt(*canvas, centre, y, down, false));
        for (float dx : {10.0f, 20.0f, 30.0f})
        {
            canvas->mouseDrag(mouseAt(*canvas, centre - dx, y, down, true));
        }
        // Released well outside the canvas, to the left of the editor.
        canvas->mouseUp(mouseAt(*canvas, -200.0f, y, down, true));
        REQUIRE(recording.kinds() == "bvvve");
        CHECK(recording.calls.front().slot == ParamSlot::Position);
        CHECK(recording.calls.back().slot == ParamSlot::Position);
        double previous = 0.5;
        for (std::size_t i = 1; i <= 3; ++i)
        {
            const GestureCall &c = recording.calls[i];
            CHECK(c.slot == ParamSlot::Position);
            CHECK(c.value < previous); // moving left is earlier in the file
            previous = c.value;
        }
        CHECK(recording.calls[3].value ==
              gg::ui::positionValueAtX(centre - 30.0f, lane)); // the last drag position

        // Nothing more: another release or the editor closing ends nothing again.
        canvas->mouseUp(mouseAt(*canvas, -200.0f, y, down, true));
        editor.reset();
        CHECK(recording.kinds() == "bvvve");
    }
    SECTION("the fx window's older edge sets Spray")
    {
        RecordingContext recording(gg::core::Mode::Fx);
        recording.values[static_cast<std::size_t>(ParamSlot::Position)] = 0.5f;
        recording.values[static_cast<std::size_t>(ParamSlot::Spray)] = 0.8f;
        recording.context.sampleRate = 48000.0;
        const auto editor = gg::ui::makeEditor(recording.context);
        juce::Component *canvas = findDeep(*editor, "canvas");
        REQUIRE(canvas != nullptr);
        const auto lane = laneOf(*canvas, true, 48000.0, 0);
        const auto window = gg::ui::scanWindow(0.5f, 0.8f, lane);
        const float y = 10.0f;
        REQUIRE(gg::ui::hitTest(window.x0, window, 6.0f, lane) == gg::ui::DragTarget::LeftEdge);

        const juce::Point<float> down{window.x0, y};
        canvas->mouseDown(mouseAt(*canvas, window.x0, y, down, false));
        canvas->mouseDrag(mouseAt(*canvas, window.x0 - 40.0f, y, down, true));
        canvas->mouseUp(mouseAt(*canvas, window.x0 - 40.0f, y, down, true));
        REQUIRE(recording.kinds() == "bve");
        for (const GestureCall &c : recording.calls)
        {
            CHECK(c.slot == ParamSlot::Spray);
        }
        CHECK(recording.calls[1].value > 0.8); // wider
        CHECK(recording.calls[1].value ==
              gg::ui::sprayValueForExtent(window.x1 - (window.x0 - 40.0f), lane));
    }
    SECTION("the fx window's body moves Position further back as it moves left")
    {
        RecordingContext recording(gg::core::Mode::Fx);
        recording.values[static_cast<std::size_t>(ParamSlot::Position)] = 0.5f;
        recording.values[static_cast<std::size_t>(ParamSlot::Spray)] = 0.8f;
        const auto editor = gg::ui::makeEditor(recording.context); // inactive: no sample rate
        juce::Component *canvas = findDeep(*editor, "canvas");
        REQUIRE(canvas != nullptr);
        const auto lane = laneOf(*canvas, true, 48000.0, 0);
        const auto window = gg::ui::scanWindow(0.5f, 0.8f, lane);
        const float x = window.x1 - 2.0f; // Position's own edge is the body (ruling PU-T8a)
        const juce::Point<float> down{x, 10.0f};
        canvas->mouseDown(mouseAt(*canvas, x, 10.0f, down, false));
        canvas->mouseDrag(mouseAt(*canvas, x - 50.0f, 10.0f, down, true));
        canvas->mouseUp(mouseAt(*canvas, x - 50.0f, 10.0f, down, true));
        REQUIRE(recording.kinds() == "bve");
        CHECK(recording.calls[1].slot == ParamSlot::Position);
        CHECK(recording.calls[1].value > 0.5);
        CHECK_THAT(
            recording.calls[1].value,
            Catch::Matchers::WithinAbs(gg::ui::positionValueAtX(window.x1 - 50.0f, lane), 1e-6));
    }
    SECTION("a drag still live when the editor closes ends there")
    {
        RecordingContext recording(gg::core::Mode::Instrument);
        recording.context.sampleWaveform = [] { return oneSecondSample(); };
        recording.values[static_cast<std::size_t>(ParamSlot::Position)] = 0.5f;
        auto editor = gg::ui::makeEditor(recording.context);
        juce::Component *canvas = findDeep(*editor, "canvas");
        REQUIRE(canvas != nullptr);
        const float x = 0.5f * static_cast<float>(canvas->getWidth());
        const juce::Point<float> down{x, 10.0f};
        canvas->mouseDown(mouseAt(*canvas, x, 10.0f, down, false));
        canvas->mouseDrag(mouseAt(*canvas, x + 5.0f, 10.0f, down, true));
        editor.reset();
        CHECK(recording.kinds() == "bve");
    }
    SECTION("a press outside the window, or with no sample, grabs nothing")
    {
        RecordingContext recording(gg::core::Mode::Instrument);
        recording.values[static_cast<std::size_t>(ParamSlot::Position)] = 0.5f;
        const auto editor = gg::ui::makeEditor(recording.context); // no sample: no window
        juce::Component *canvas = findDeep(*editor, "canvas");
        REQUIRE(canvas != nullptr);
        const float x = 0.5f * static_cast<float>(canvas->getWidth());
        canvas->mouseDown(mouseAt(*canvas, x, 10.0f, {x, 10.0f}, false));
        canvas->mouseDrag(mouseAt(*canvas, x + 5.0f, 10.0f, {x, 10.0f}, true));
        canvas->mouseUp(mouseAt(*canvas, x + 5.0f, 10.0f, {x, 10.0f}, true));

        recording.context.sampleWaveform = [] { return oneSecondSample(); };
        canvas->mouseDown(mouseAt(*canvas, 2.0f, 10.0f, {2.0f, 10.0f}, false));
        canvas->mouseUp(mouseAt(*canvas, 2.0f, 10.0f, {2.0f, 10.0f}, false));
        CHECK(recording.calls.empty());
    }
    SECTION("a right-button press on the window grabs nothing")
    {
        RecordingContext recording(gg::core::Mode::Instrument);
        recording.values[static_cast<std::size_t>(ParamSlot::Position)] = 0.5f;
        recording.values[static_cast<std::size_t>(ParamSlot::Spray)] = 0.4f;
        recording.context.sampleWaveform = [] { return oneSecondSample(); };
        recording.context.sampleRate = 48000.0;
        const auto editor = gg::ui::makeEditor(recording.context);
        juce::Component *canvas = findDeep(*editor, "canvas");
        REQUIRE(canvas != nullptr);
        const auto lane = laneOf(*canvas, false, 48000.0, 48000);
        const auto window = gg::ui::scanWindow(0.5f, 0.4f, lane);
        const float y = 10.0f;
        constexpr int kRight = juce::ModifierKeys::rightButtonModifier;
        // The body and an edge, each of which the left button grabs.
        for (const float x : {0.5f * (window.x0 + window.x1), window.x0})
        {
            REQUIRE(gg::ui::hitTest(x, window, 6.0f, lane) != gg::ui::DragTarget::None);
            const juce::Point<float> down{x, y};
            canvas->mouseDown(mouseAt(*canvas, x, y, down, false, kRight));
            canvas->mouseDrag(mouseAt(*canvas, x + 20.0f, y, down, true, kRight));
            canvas->mouseUp(mouseAt(*canvas, x + 20.0f, y, down, true, kRight));
        }
        CHECK(recording.calls.empty());
    }
}

TEST_CASE("the canvas draws the grains the queue holds", "[plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceGui;
    constexpr double kRate = 48000.0;
    constexpr int64_t kNow = 480000; // 10 s in
    gg::core::GrainQueue queue;
    gg::core::SnapshotBuffer snapshots;
    std::atomic<int64_t> clock{kNow};
    RecordingContext recording(gg::core::Mode::Fx);
    recording.context.grains = &queue;
    recording.context.snapshot = &snapshots;
    recording.context.clock = &clock;
    recording.context.sampleRate = kRate;

    const auto editor = gg::ui::makeEditor(recording.context);
    auto *canvas = dynamic_cast<gg::ui::GrainCanvas *>(findDeep(*editor, "canvas"));
    REQUIRE(canvas != nullptr);
    const float width = static_cast<float>(canvas->getWidth());
    const float height = static_cast<float>(canvas->getHeight());

    // The lane's vertical axis is pitch (M2 Views' D1): rate 4, two octaves up, lies half the
    // reach above the centre, and rate 1/2, an octave down, a quarter of the reach below it.
    const auto grain = [](int64_t onset, double behindSeconds, float rate) {
        return gg::core::GrainVis{onset, static_cast<double>(kNow) - behindSeconds * kRate,
                                  rate,  static_cast<float>(0.1 * kRate),
                                  1.0f,  0.0f};
    };
    const float radius = 3.0f;
    const float reach = 0.5f * height - radius;
    const auto accum = [canvas](float x, float y) {
        return canvas->accumAt(static_cast<uint32_t>(std::lround(x)),
                               static_cast<uint32_t>(std::lround(y)));
    };
    // Each grain's capsule spans its 100 ms read, at its rate: from 4 s back to 3.6 s back (rate
    // 4), and from 8 s back to 7.95 s back (rate 1/2, wide enough that the clock's extrapolation
    // between frames can't scroll it off x8); x4 and x8 are the reads' middles.
    const float x4 = width * (1.0f - 3.8f / 16.0f);
    const float x8 = width * (1.0f - 7.975f / 16.0f);

    // Two grains of 100 ms that started just now, 4 s and 8 s behind the head, one pitched up
    // and one down; one that has already ended. Queued after the canvas's first frame, which
    // clears the queue (the next test).
    gg::core::UiSnapshot snapshot;
    snapshot.clock = kNow;
    snapshot.writeHead = kNow;
    snapshots.push(snapshot);
    canvas->renderFrame();
    REQUIRE(queue.push(grain(kNow - 100, 4.0, 4.0f)));
    REQUIRE(queue.push(grain(kNow - 100, 8.0, 0.5f)));
    REQUIRE(queue.push(grain(kNow - 20000, 12.0, 1.0f))); // ended at kNow - 15200

    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 2);
    CHECK(accum(x4, 0.5f * height - 0.5f * reach) > 0.0f);
    CHECK(accum(x8, 0.5f * height + 0.25f * reach) > 0.0f);
    // Nothing where no grain plays: the ended grain's place, and the lane's middle.
    CHECK(accum(width * (1.0f - 11.95f / 16.0f), 0.5f * height) == 0.0f);
    CHECK(accum(0.5f * width, 0.5f * height) == 0.0f);

    // Once both have ended (the clock moved on 100 ms), the queue empty, nothing is drawn.
    clock.store(kNow + 4800);
    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 0);
    CHECK(accum(x4, 0.5f * height - 0.5f * reach) == 0.0f);

    // Reactivated between two frames, with a clock that starts again from 0: the grains of the
    // new activation draw at once (the extrapolation restarts rather than waiting for the old
    // clock to come round).
    gg::core::GrainQueue queue2;
    gg::core::SnapshotBuffer snapshots2;
    std::atomic<int64_t> clock2{64};
    recording.context.grains = &queue2;
    recording.context.snapshot = &snapshots2;
    recording.context.clock = &clock2;
    gg::core::UiSnapshot fresh;
    fresh.clock = 64;
    fresh.writeHead = 64;
    snapshots2.push(fresh);
    canvas->renderFrame(); // a new queue: cleared first
    REQUIRE(queue2.push(gg::core::GrainVis{0, 64.0 - 4.0 * kRate, 4.0f, 4800.0f, 1.0f, 0.0f}));
    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 1);
    CHECK(accum(x4, 0.5f * height - 0.5f * reach) > 0.0f);

    // Deactivated (the plugin nulls the pointers): the canvas draws its static parts only.
    recording.context.grains = nullptr;
    recording.context.snapshot = nullptr;
    recording.context.clock = nullptr;
    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 0);
    CHECK(accum(x4, 0.5f * height - 0.5f * reach) == 0.0f);
}

TEST_CASE("the canvas drops a stale backlog and waits for a snapshot", "[plugin]")
{
    // A queue's backlog when the canvas first reads it was queued while nothing drew it (an
    // editor reopening): the first frame clears it. And the FX lane places no grain before a
    // snapshot gives it the write head. The grains last 10 s, so the clock's extrapolation
    // between frames (at most 0.25 s) can't end them.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    constexpr double kRate = 48000.0;
    constexpr int64_t kNow = 480000;
    gg::core::GrainQueue queue;
    gg::core::SnapshotBuffer snapshots;
    std::atomic<int64_t> clock{kNow};
    RecordingContext recording(gg::core::Mode::Fx);
    recording.context.grains = &queue;
    recording.context.snapshot = &snapshots;
    recording.context.clock = &clock;
    recording.context.sampleRate = kRate;
    const auto editor = gg::ui::makeEditor(recording.context);
    auto *canvas = dynamic_cast<gg::ui::GrainCanvas *>(findDeep(*editor, "canvas"));
    REQUIRE(canvas != nullptr);
    const float width = static_cast<float>(canvas->getWidth());
    const float height = static_cast<float>(canvas->getHeight());
    // A grain reading 4.5 s .. 4 s behind a write head at 0 (the head the lane would take
    // without a snapshot), slowly, for 10 s: so low that it lies on the lane's bottom edge (the
    // vertical axis is pitch, M2 Views' D1).
    const gg::core::GrainVis grain{
        kNow - 100, -4.5 * kRate, 0.05f, static_cast<float>(10.0 * kRate), 1.0f, 0.0f};
    const float y = gg::ui::pitchY(0.05f, laneOf(*canvas, true, kRate, 0), 3.0f);
    REQUIRE(y == height - 3.0f);
    const auto lit = [&] {
        return canvas->accumAt(static_cast<uint32_t>(std::lround(width * (1.0f - 4.4f / 16.0f))),
                               static_cast<uint32_t>(std::lround(y))) > 0.0f;
    };

    REQUIRE(queue.push(grain));
    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 0);
    gg::core::GrainVis leftover;
    CHECK_FALSE(queue.pop(leftover));

    // Held but not drawn without a snapshot...
    REQUIRE(queue.push(grain));
    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 1);
    CHECK_FALSE(lit());
    // ... and drawn once one arrives, with the head at 0.
    gg::core::UiSnapshot snapshot;
    snapshot.clock = kNow;
    snapshot.writeHead = 0;
    snapshots.push(snapshot);
    canvas->renderFrame();
    CHECK(canvas->liveGrainCount() == 1);
    CHECK(lit());
}

TEST_CASE("the folder button reports a failed save", "[plugin]")
{
    // Carry-over row 198: "Sample folder..." says when the settings couldn't be saved.
    const juce::ScopedJuceInitialiser_GUI juceGui;
    RecordingContext recording(gg::core::Mode::Instrument);
    gg::ui::EditorContext &context = recording.context;
    context.showSampleLoader = true;
    gg::ui::SampleStatusView view = statusView(gg::ui::SampleStatusView::State::None);
    context.sampleStatus = [&view] { return view; };
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
    recording.context.sampleStatus = [&view] { return view; };
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
    context.loadSample = [&loads](const std::string &path) { loads.push_back(path); };
    gg::ui::SampleStatusView view;
    context.sampleStatus = [&view] { return view; };
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

// The parameter control (M2 sub-project 6's spec §6.5): M1's macro adaptor generalized, which
// every view builds its controls from. Built headless, as the Perform view's tests are.
namespace
{
/// The first child of `parent` that is a T, or null.
template <typename T> T *childOfType(const juce::Component &parent)
{
    for (juce::Component *child : parent.getChildren())
    {
        if (auto *t = dynamic_cast<T *>(child))
        {
            return t;
        }
    }
    return nullptr;
}
} // namespace

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
    gg::ui::UndoStack undo{{[this](ParamSlot s, float v) { undone.emplace_back(s, v); },
                            [](const gg::core::ModRoutes &) {}}};
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
