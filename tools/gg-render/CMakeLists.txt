# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-render: the offline renderer the golden tests run (INTENT §19). gg-render-lib holds the
# render-spec parser, the test signals and media, and the render loops (the engine's Cloud stream
# in sample mode, gg-core's Processor in instrument and FX mode), so gg-tests can link them; the
# gg-render executable adds the command line.

add_library(gg-render-lib STATIC
        Render.cpp
        RenderSpec.cpp
        Signals.cpp
        )
add_library(gg::render ALIAS gg-render-lib)

target_include_directories(gg-render-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Apple's linker warns about a library repeated on the link line ("ignoring duplicate
# libraries"), which CMake writes when a library is listed after another that links it too:
# - fmt is PUBLIC so gg-render (main.cpp uses it too) gets it from here, not from a second
#   listing on the executable;
# - gg::media, the file loader for instrument samples and FX file inputs (PRIVATE), comes before
#   gg::core and gg::engine, which it links;
# - gg::core is PUBLIC because RenderSpec.h names its parameter slots, and comes before
#   gg::engine, which it links.
target_link_libraries(gg-render-lib
        PRIVATE gg::media
        PUBLIC gg::core gg::engine gg::audiometrics gg::compiler-options fmt::fmt
        PRIVATE nlohmann_json::nlohmann_json
        )

add_executable(gg-render main.cpp)
target_link_libraries(gg-render PRIVATE gg::render gg::compiler-options CLI11::CLI11)
