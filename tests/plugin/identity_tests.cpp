// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

// Identities frozen at the first public release (INTENT §18) that the code, not the build, decides.
// The bundle identities are checked on the built bundles (tests/CMakeLists.txt, macOS).

#include <cstdint>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <clap/clap.h>

#include "PluginHarness.h"

TEST_CASE("parameter ids stay below clap-wrapper's VST3 range", "[plugin]")
{
    // Ruling T8-d: clap-wrapper's VST3 wrapper adds its MIDI-mapping parameters from 0xB00000
    // up, and hosts save their IDs, so ours must stay below.
    constexpr clap_id kFirstWrapperId = 0xB00000;
    const char *id = GENERATE("org.sethbc.giga-grains", "org.sethbc.giga-grains-fx");
    CAPTURE(id);
    gg::test::PluginUnderTest plugin(id);
    REQUIRE(plugin.valid());
    const auto *params = static_cast<const clap_plugin_params *>(
        plugin.clap()->get_extension(plugin.clap(), CLAP_EXT_PARAMS));
    REQUIRE(params != nullptr);

    const uint32_t count = params->count(plugin.clap());
    REQUIRE(count == 10);
    for (uint32_t i = 0; i < count; ++i)
    {
        clap_param_info info{};
        REQUIRE(params->get_info(plugin.clap(), i, &info));
        CAPTURE(i, info.id);
        CHECK(info.id < kFirstWrapperId);
    }
}
