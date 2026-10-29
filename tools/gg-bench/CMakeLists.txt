# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-bench, the CPU benchmark (INTENT §16). gg-bench-kernels is the header-only set of prototype
# grain kernels in Kernels.h. gg-bench-lib (gg::bench) times them, the reference kernel and the
# real-engine cases in interleaved rounds, computes the ratios, writes the JSON and checks a
# baseline (Bench.h); gg-bench and gg-tests both link it. It also times Space (--space), so it
# links gg-core for the diffuser and the reverb.

add_library(gg-bench-kernels INTERFACE)
target_include_directories(gg-bench-kernels INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gg-bench-kernels INTERFACE gg::engine sst-basic-blocks simde)
add_library(gg::bench-kernels ALIAS gg-bench-kernels)

add_library(gg-bench-lib STATIC Bench.cpp)
add_library(gg::bench ALIAS gg-bench-lib)
target_include_directories(gg-bench-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# nlohmann is PUBLIC: compare() and makeBaseline() take nlohmann::json.
target_link_libraries(gg-bench-lib
        PUBLIC gg::bench-kernels gg::compiler-options nlohmann_json::nlohmann_json
        PRIVATE gg::core
        )
target_compile_definitions(gg-bench-lib PRIVATE
        GG_BENCH_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# gg-bench [options] [--json out.json], gg-bench --canvas [--quick] [--json out.json] or
# gg-bench --space [--quick] [--json out.json]. Time it from the release preset. --canvas times
# the canvas renderer, so gg-bench links gg-ui-core (never JUCE).
add_executable(gg-bench main.cpp)
target_link_libraries(gg-bench PRIVATE gg::bench gg::ui-core gg::compiler-options)
