# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-render: the offline renderer the golden tests run (INTENT §19). gg-render-lib holds the
# render-spec parser, the test signals and the render loops (the engine's Cloud stream in sample
# mode, gg-core's Processor in FX mode), so gg-tests can link them; the gg-render executable adds
# the command line.

add_library(gg-render-lib STATIC
        Render.cpp
        RenderSpec.cpp
        Signals.cpp
        )
add_library(gg::render ALIAS gg-render-lib)

target_include_directories(gg-render-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# fmt is PUBLIC so gg-render (main.cpp uses it too) gets it from here: listing it again on the
# executable would make CMake repeat it on the link line, which Apple's linker warns about.
# gg::core is PUBLIC because RenderSpec.h names its parameter slots; it comes before gg::engine,
# which it links itself, for the same reason.
target_link_libraries(gg-render-lib
        PUBLIC gg::core gg::engine gg::audiometrics gg::compiler-options fmt::fmt
        PRIVATE nlohmann_json::nlohmann_json
        )

add_executable(gg-render main.cpp)
target_link_libraries(gg-render PRIVATE gg::render gg::compiler-options CLI11::CLI11)
