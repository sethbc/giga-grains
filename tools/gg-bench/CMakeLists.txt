# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# The M0 kernel benchmark spike (INTENT §16). gg-bench-kernels is the header-only set of
# prototype grain kernels in Kernels.h, shared with gg-tests.

add_library(gg-bench-kernels INTERFACE)
target_include_directories(gg-bench-kernels INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gg-bench-kernels INTERFACE gg::engine sst-basic-blocks simde)
add_library(gg::bench-kernels ALIAS gg-bench-kernels)

# gg-bench [--quick] [--json out.json]. Time it from the release preset.
add_executable(gg-bench main.cpp)
target_link_libraries(gg-bench PRIVATE gg::bench-kernels gg::compiler-options)
target_compile_definitions(gg-bench PRIVATE
        GG_BENCH_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
