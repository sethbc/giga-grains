# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-ui-core: the framework-free canvas code (the splat renderer and the canvas stress
# workload). It builds without plugins or JUCE, so its tests and `gg-bench --canvas` also run in
# the sanitizer builds.
#
# gg-ui: the JUCE editor, attached to the plugins through sst-clap-helpers' clap_juce_shim. Only
# built with the plugins.

add_library(gg-ui-core STATIC
        src/SplatRenderer.cpp
        src/StressGrains.cpp
        )
add_library(gg::ui-core ALIAS gg-ui-core)

target_include_directories(gg-ui-core PUBLIC include)
# The engine only for gg::rng's header-only mixer.
target_link_libraries(gg-ui-core PRIVATE gg::engine gg::compiler-options)

if (GG_BUILD_PLUGINS)
    add_library(gg-ui STATIC
            src/Editor.cpp
            )
    add_library(gg::ui ALIAS gg-ui)

    target_include_directories(gg-ui PUBLIC include)
    # JUCE itself is compiled once, inside clap_juce_shim; gg::juce-headers adds its headers and
    # definitions without its sources (see libs/CMakeLists.txt).
    target_link_libraries(gg-ui
            PUBLIC gg::ui-core
            PRIVATE clap_juce_shim clap_juce_shim_headers gg::juce-headers gg::engine
            gg::compiler-options
            )
    # As Shortcircuit XT's editor library does.
    target_compile_definitions(gg-ui PRIVATE
            JUCE_STANDALONE_APPLICATION=0
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0
            )
endif ()
