# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Assembles every format from gg-plugin-impl with clap-wrapper's clap-first support: one
# bundle per format ("Giga Grains.clap", ".vst3", ".component"), each holding both plugins,
# plus one standalone app per plugin. The AUv2 identities come from the plugin's own AUv2
# factories (plugin/src/Factory.cpp), which clap-wrapper reads at build time.
#
# The identities here are frozen at the first public release (INTENT §18).

make_clapfirst_plugins(
        TARGET_NAME gg_clapfirst
        IMPL_TARGET gg-plugin-impl

        OUTPUT_NAME "Giga Grains"

        ENTRY_SOURCE gg-clap-entry.cpp

        BUNDLE_IDENTIFIER "org.sethbc.giga-grains"
        BUNDLE_VERSION ${PROJECT_VERSION}

        COPY_AFTER_BUILD ${GG_COPY_PLUGIN_AFTER_BUILD}

        # Always explicit: the default list includes AAX.
        PLUGIN_FORMATS CLAP VST3 AUV2 STANDALONE

        WINDOWS_FOLDER_VST3 TRUE

        ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/gg_assets

        # target-postfix, output name, CLAP id: one standalone per plugin.
        STANDALONE_CONFIGURATIONS
        standalone "Giga Grains" "org.sethbc.giga-grains"
        standalone_fx "Giga Grains FX" "org.sethbc.giga-grains-fx"
)

# make_clapfirst_plugins gives every standalone the same bundle identifier
# ("<BUNDLE_IDENTIFIER>.standalone"). Two apps sharing one would share their preferences and
# confuse LaunchServices, so the FX app gets its own.
if (APPLE AND TARGET gg_clapfirst_standalone_fx)
    set_target_properties(gg_clapfirst_standalone_fx PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.sethbc.giga-grains-fx.standalone"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.sethbc.giga-grains-fx.standalone"
            )
endif ()

# cmake/products.cmake reads this to find the built artifacts.
set_target_properties(gg_clapfirst_all PROPERTIES ARTEFACT_DIRECTORY ${CMAKE_BINARY_DIR}/gg_assets)
