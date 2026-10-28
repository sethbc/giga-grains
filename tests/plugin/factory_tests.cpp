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
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include <juce_gui_basics/juce_gui_basics.h>

#include "editor/EditorPlugin.h"
#include "gg/ui/Editor.h"
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
const juce::Component *stripChild(const juce::Component &editor, const char *id)
{
    return editor.findChildWithID(id);
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
    const auto text = path.u8string();
    return juce::String::fromUTF8(reinterpret_cast<const char *>(text.data()),
                                  static_cast<int>(text.size()));
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
        CHECK(loader->status().state == gg::plugin::SampleState::Loading);
        CHECK(loader->status().fileName == "gr\xc3\xa4in.wav");
        CHECK(labelText(*editor, "sampleName") == utf8String(file.filename()));
        CHECK(labelText(*editor, "sampleStatus").startsWith("loading"));

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
        CHECK(std::filesystem::path(std::u8string(loader->entry()->absolutePath.begin(),
                                                  loader->entry()->absolutePath.end())) ==
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
        view = {State::Missing, "kick.wav", "not found", 0.0, 0, 0.0};
        const auto editor = gg::ui::makeEditor(context);
        CHECK(labelText(*editor, "sampleStatus") == "missing: not found");
        CHECK(relinkShown(*editor));
    }
    SECTION("changed, with relink")
    {
        view = {State::Changed, "kick.wav", "the file at the saved path has changed", 0.0, 0, 0.0};
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
