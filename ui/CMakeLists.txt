# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-ui-core: the framework-free canvas code (the splat renderer, the lane geometry and the
# canvas stress workload). It builds without plugins or JUCE, so its tests and
# `gg-bench --canvas` also run in the sanitizer builds.
#
# gg-ui: the JUCE editor, attached to the plugins through sst-clap-helpers' clap_juce_shim. Only
# built with the plugins' editor (GG_BUILD_PLUGINS and GG_BUILD_EDITOR).

add_library(gg-ui-core STATIC
        src/LaneModel.cpp
        src/SplatRenderer.cpp
        src/StressGrains.cpp
        )
add_library(gg::ui-core ALIAS gg-ui-core)

target_include_directories(gg-ui-core PUBLIC include)
# The engine only for gg::rng's header-only mixer.
target_link_libraries(gg-ui-core PRIVATE gg::engine gg::compiler-options)

if (GG_BUILD_PLUGINS AND GG_BUILD_EDITOR)
    add_library(gg-ui STATIC
            src/Editor.cpp
            )
    add_library(gg::ui ALIAS gg-ui)

    target_include_directories(gg-ui PUBLIC include)
    # JUCE itself is compiled once, inside clap_juce_shim; gg::juce-headers adds its headers and
    # definitions without its sources. Every JUCE_* definition is set in libs/CMakeLists.txt.
    # sst-jucegui (the controls) compiles against those same headers (libs/CMakeLists.txt).
    # gg::core for the publish structures and parameter slots gg/ui/Editor.h names; whatever
    # includes that header links gg::core itself (the plugin's editor layer and the tests do).
    target_link_libraries(gg-ui
            PUBLIC gg::ui-core
            PRIVATE clap_juce_shim clap_juce_shim_headers gg::juce-headers sst-jucegui gg::engine
            gg::core gg::compiler-options
            )
endif ()
