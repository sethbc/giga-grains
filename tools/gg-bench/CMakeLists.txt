# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-bench, the CPU benchmark (INTENT §16). gg-bench-kernels is the header-only set of prototype
# grain kernels in Kernels.h. It is self-contained, with no engine dependency, so no engine
# change can move the frozen reference kernel. gg-bench-lib (gg::bench) times them, the reference
# kernel and the real-engine cases in interleaved rounds, computes the ratios, writes the JSON
# and checks a baseline (Bench.h); gg-bench and gg-tests both link it. It also times Space
# (--space) and the new FX units (--chain), so it links gg-core for the diffuser, the reverb, the
# bus filters, the delay and the limiter, and the per-grain filter spike
# (GrainFilterKernel.h, INTENT §22), so it links sst-filters for its CytomicSVF.

add_library(gg-bench-kernels INTERFACE)
target_include_directories(gg-bench-kernels INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gg-bench-kernels INTERFACE sst-basic-blocks simde)
add_library(gg::bench-kernels ALIAS gg-bench-kernels)

add_library(gg-bench-lib STATIC Bench.cpp)
add_library(gg::bench ALIAS gg-bench-lib)
target_include_directories(gg-bench-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# nlohmann is PUBLIC: compare() and makeBaseline() take nlohmann::json.
target_link_libraries(gg-bench-lib
        PUBLIC gg::bench-kernels gg::compiler-options nlohmann_json::nlohmann_json
        PRIVATE gg::core sst-filters
        )
# The chain (--chain) times gg-core's BusFilter, which lives in core/src/fx: a private include
# directory of gg-bench-lib alone, as gg-tests reaches Space's, so gg-core's public one stays as is.
target_include_directories(gg-bench-lib PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
target_compile_definitions(gg-bench-lib PRIVATE
        GG_BENCH_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# gg-bench [options] [--json out.json], gg-bench --canvas [--quick] [--json out.json] or
# gg-bench --space [--quick] [--json out.json]. Time it from the release preset. --canvas times
# the canvas renderer, so gg-bench links gg-ui-core (never JUCE).
add_executable(gg-bench main.cpp)
target_link_libraries(gg-bench PRIVATE gg::bench gg::ui-core gg::compiler-options)
