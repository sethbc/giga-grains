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

# On macOS each bundle has to be signed before it is copied to ~/Library/Audio/Plug-Ins (see
# below), so clap-wrapper's own copy is turned off there and the copy is added after the signing
# step. Elsewhere clap-wrapper copies as usual.
if (APPLE)
    set(gg_wrapper_copy_after_build FALSE)
else ()
    set(gg_wrapper_copy_after_build ${GG_COPY_PLUGIN_AFTER_BUILD})
endif ()

make_clapfirst_plugins(
        TARGET_NAME gg_clapfirst
        IMPL_TARGET gg-plugin-impl

        OUTPUT_NAME "Giga Grains"

        ENTRY_SOURCE gg-clap-entry.cpp

        BUNDLE_IDENTIFIER "org.sethbc.giga-grains"
        BUNDLE_VERSION ${PROJECT_VERSION}

        COPY_AFTER_BUILD ${gg_wrapper_copy_after_build}

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

# macOS code signing. The linker signs only each bundle's executable (ad hoc), and clap-wrapper
# signs no product, so no bundle has sealed resources or a bound Info.plist. Strict hosts reject
# that: Ableton Live's scanner fails the VST3 with "code has no resources but signature indicates
# they must be present" and doesn't list it. So every bundle is signed as a whole, with
# GG_MACOS_CODESIGN_IDENTITY, once everything else has been put in it.
#
# Ordering: a target's POST_BUILD commands run in the order they are added, after the link, and
# clap-wrapper has already added its own (PkgInfo, for instance). The signing step comes next and
# the copy to ~/Library/Audio/Plug-Ins (clap-wrapper's target_copy_after_build) last, so what gets
# installed is the signed bundle. All of them run only when the target relinks, so a no-op build
# doesn't re-sign.
#
# The signature seals the bundle's Info.plist, and CMake writes that file when it generates the
# build, without relinking anything. So each bundle also relinks, and re-signs, when its
# Info.plist changes. The AU needs a different trigger. CMake writes a generic Info.plist into it
# on every run, without the AudioComponents list. clap-wrapper copies the real one back only as
# part of the AU's own link, and Ninja would then see the input changed during the link and
# relink on every build. So the AU depends instead on a stamp that every CMake run touches.
if (APPLE)
    foreach (format IN ITEMS clap vst3 auv2 standalone standalone_fx)
        add_custom_command(TARGET gg_clapfirst_${format} POST_BUILD
                COMMAND ${GG_CODESIGN} --force --sign "${GG_MACOS_CODESIGN_IDENTITY}"
                "$<TARGET_BUNDLE_DIR:gg_clapfirst_${format}>"
                VERBATIM
                )
        if (format STREQUAL "auv2")
            set(regenerated "${CMAKE_CURRENT_BINARY_DIR}/gg_clapfirst_auv2-regenerated.stamp")
            file(TOUCH "${regenerated}")
            set_property(TARGET gg_clapfirst_auv2 APPEND PROPERTY LINK_DEPENDS "${regenerated}")
        else ()
            set_property(TARGET gg_clapfirst_${format} APPEND PROPERTY
                    LINK_DEPENDS "$<TARGET_BUNDLE_CONTENT_DIR:gg_clapfirst_${format}>/Info.plist")
        endif ()
    endforeach ()

    if (GG_COPY_PLUGIN_AFTER_BUILD)
        target_copy_after_build(TARGET gg_clapfirst_clap FLAVOR clap)
        target_copy_after_build(TARGET gg_clapfirst_vst3 FLAVOR vst3)
        target_copy_after_build(TARGET gg_clapfirst_auv2 FLAVOR auv2)
    endif ()
endif ()

# cmake/products.cmake reads this to find the built artifacts.
set_target_properties(gg_clapfirst_all PROPERTIES ARTEFACT_DIRECTORY ${CMAKE_BINARY_DIR}/gg_assets)
