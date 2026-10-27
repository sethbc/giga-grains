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

TEST_CASE("both plugins expose a resizable embedded GUI", "[plugin]")
{
    // Only what needs no window or JUCE runtime: JUCE starts when a host creates the GUI, which
    // pluginval exercises on the built plugins. (CLAP allows can_resize only after create.)
    for (const char *id : {"org.sethbc.giga-grains", "org.sethbc.giga-grains-fx"})
    {
        INFO(id);
        gg::test::PluginUnderTest plugin(id);
        REQUIRE(plugin.valid());
        const auto *gui = static_cast<const clap_plugin_gui *>(
            plugin.clap()->get_extension(plugin.clap(), CLAP_EXT_GUI));
        REQUIRE(gui != nullptr);

        CHECK(gui->is_api_supported(plugin.clap(), CLAP_WINDOW_API_COCOA, false));
        CHECK(gui->is_api_supported(plugin.clap(), CLAP_WINDOW_API_WIN32, false));
        CHECK(gui->is_api_supported(plugin.clap(), CLAP_WINDOW_API_X11, false));
        CHECK_FALSE(gui->is_api_supported(plugin.clap(), CLAP_WINDOW_API_WAYLAND, false));
        CHECK_FALSE(gui->is_api_supported(plugin.clap(), CLAP_WINDOW_API_COCOA, true)); // floating
        REQUIRE(plugin.impl().clapJuceShim != nullptr);
        CHECK(plugin.impl().clapJuceShim->guiCanResize());
    }
}

TEST_CASE("both plugins expose an empty params extension", "[plugin]")
{
    // M0 has no parameters, but clap-wrapper's VST3 IMidiMapping needs CLAP's params extension
    // before it creates its MIDI-controller parameters: without it, Steinberg's validator fails
    // "MIDI Mapping" with an unknown ParamID.
    for (const char *id : {"org.sethbc.giga-grains", "org.sethbc.giga-grains-fx"})
    {
        INFO(id);
        gg::test::PluginUnderTest plugin(id);
        REQUIRE(plugin.valid());
        const auto *params = static_cast<const clap_plugin_params *>(
            plugin.clap()->get_extension(plugin.clap(), CLAP_EXT_PARAMS));
        REQUIRE(params != nullptr);
        REQUIRE(params->count != nullptr);
        CHECK(params->count(plugin.clap()) == 0);
        CHECK(params->get_info != nullptr);
        CHECK(params->get_value != nullptr);
        CHECK(params->value_to_text != nullptr);
        CHECK(params->text_to_value != nullptr);
        CHECK(params->flush != nullptr);
    }
}
