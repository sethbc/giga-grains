# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-ui-core: the framework-free editor code (the splat renderer, the lane geometry, the lane's
# UI clock and grain list, the canvas stress workload, the undo stack, the layers' colours and the
# views' placement table). It builds without plugins or JUCE, so its tests and `gg-bench --canvas`
# also run in the sanitizer builds.
#
# gg-ui: the JUCE editor (the Perform view: header, loader strip, grain canvas, macros; the
# palette and the parameter control the views build on), attached to the plugins through
# sst-clap-helpers' clap_juce_shim. Only built with the plugins' editor (GG_BUILD_PLUGINS and
# GG_BUILD_EDITOR).

add_library(gg-ui-core STATIC
        src/EditLayout.cpp
        src/LaneModel.cpp
        src/LaneState.cpp
        src/SplatRenderer.cpp
        src/StressGrains.cpp
        src/UndoStack.cpp
        )
add_library(gg::ui-core ALIAS gg-ui-core)

target_include_directories(gg-ui-core PUBLIC include)
# gg::core is PUBLIC: gg/ui/UndoStack.h includes its parameter slots and mod routes, and
# gg/ui/EditLayout.h its parameter slots. The engine only for gg::rng's header-only mixer.
target_link_libraries(gg-ui-core PUBLIC gg::core PRIVATE gg::engine gg::compiler-options)

if (GG_BUILD_PLUGINS AND GG_BUILD_EDITOR)
    add_library(gg-ui STATIC
            src/Editor.cpp
            src/GrainCanvas.cpp
            src/LoaderStrip.cpp
            src/MacroRow.cpp
            src/Palette.cpp
            src/ParamControl.cpp
            )
    add_library(gg::ui ALIAS gg-ui)

    target_include_directories(gg-ui PUBLIC include)
    # JUCE itself is compiled once, inside clap_juce_shim; gg::juce-headers adds its headers and
    # definitions without its sources. Every JUCE_* definition is set in libs/CMakeLists.txt.
    # sst-jucegui (the controls) compiles against those same headers (libs/CMakeLists.txt).
    # gg::core is PUBLIC: gg/ui/Editor.h includes its publish structures and parameter slots.
    target_link_libraries(gg-ui
            PUBLIC gg::ui-core gg::core
            PRIVATE clap_juce_shim clap_juce_shim_headers gg::juce-headers sst-jucegui gg::engine
            gg::compiler-options
            )
    # Its embedded notices (gg/ui/Notices.h's noticeTexts(), the About box's texts) are a
    # generated source that cmake/products.cmake attaches with gg_add_notices: the license set
    # names AudioUnitSDK's checkout, which is known only after clients/clap-first, processed
    # after this directory.
endif ()
