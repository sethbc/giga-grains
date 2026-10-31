# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Both CLAP plugins (Giga Grains and Giga Grains FX) and their factory, in two layers:
# - gg-plugin-core: the plugins' audio side (GigaGrainsPlugin) and the CLAP factory. No JUCE, so
#   the sanitizer presets build and test it (GG_BUILD_EDITOR=OFF).
# - gg-plugin-editor (GG_BUILD_EDITOR): EditorPlugin, a GigaGrainsPlugin with the JUCE editor
#   attached through clap_juce_shim.
# The factory creates each instance through makePlugin(), defined by headless/MakePlugin.cpp in
# the core when there's no editor, and by editor/MakePlugin.cpp when there is.
#
# gg::plugin-impl names the top layer built: clients/clap-first links it into every format
# (CLAP, VST3, AUv2, standalone), and the tests link it directly.

add_library(gg-plugin-core STATIC
        src/Factory.cpp
        src/Favorites.cpp
        src/GigaGrainsPlugin.cpp
        src/HelpersConfig.cpp
        src/PresetCatalog.cpp
        src/PresetDiscovery.cpp
        src/SampleLoader.cpp
        src/SearchCache.cpp
        )
target_include_directories(gg-plugin-core PUBLIC include PRIVATE src)

# Only header-only interfaces are PUBLIC. gg-core (and through it the engine), sst-cpputils and
# our compiler options stay PRIVATE so they do not reach clap-wrapper's own sources, which
# make_clapfirst_plugins compiles into the same format targets that link these libraries
# (sst-cpputils carries MSVC flags: /utf-8 /bigobj /permissive-). GigaGrainsPlugin.h includes
# gg-core's headers (the Processor, the parameter IDs) and needs sst-cpputils for
# SST_CPPUTILS_NONBLOCKING, so whatever includes it links gg::core and sst-cpputils itself: the
# editor layer below, and the tests. gg::media (the sample loader's decoding, conversion, search
# and settings file, and the content search path) is PRIVATE too: its headers appear only in
# SampleLoader.cpp, SearchCache.cpp, GigaGrainsPlugin.cpp, PresetCatalog.cpp,
# PresetDiscovery.cpp and, for the UTF-8 path helper (gg/media/Utf8Path.h), EditorPlugin.cpp;
# SearchCache.h and SettingsPath.h include them too.
target_link_libraries(gg-plugin-core
        PUBLIC clap clap-helpers gg::clap-wrapper-extensions
        PRIVATE gg::media gg::core sst-cpputils gg::compiler-options
        )

if (NOT GG_BUILD_EDITOR)
    target_sources(gg-plugin-core PRIVATE src/headless/MakePlugin.cpp)
    add_library(gg::plugin-impl ALIAS gg-plugin-core)
else ()
    # An OBJECT library, not a static one. The core's factory calls this layer's makePlugin(),
    # but this layer links the core, so its archive would come before the core's on the link
    # line, and GNU ld takes from an archive only the symbols already undefined when it reaches
    # it: makePlugin() would go unresolved. As objects, the editor layer is linked whole into
    # each format and into the tests.
    add_library(gg-plugin-editor OBJECT
            src/editor/EditorPlugin.cpp
            src/editor/MakePlugin.cpp
            )
    add_library(gg::plugin-impl ALIAS gg-plugin-editor)
    target_include_directories(gg-plugin-editor PRIVATE src)

    # clap_juce_shim_headers is PUBLIC because editor/EditorPlugin.h includes the shim header,
    # whose layout depends on the SHIM_<platform> definition it carries; the tests include
    # EditorPlugin.h too. EditorPlugin.cpp needs JUCE's headers (never its sources) for one
    # complete type; see createEditor().
    target_link_libraries(gg-plugin-editor
            PUBLIC gg-plugin-core clap_juce_shim_headers
            PRIVATE gg::ui gg::media gg::core clap_juce_shim gg::juce-headers sst-cpputils
            gg::compiler-options
            )
endif ()
