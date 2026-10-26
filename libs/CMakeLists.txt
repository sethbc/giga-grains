# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Third-party dependencies. Each is a git submodule pinned to an exact SHA.

# Add a SIMDE target
add_library(simde INTERFACE)
target_include_directories(simde INTERFACE simde)
add_library(gg::simde ALIAS simde)

add_subdirectory(catch2 EXCLUDE_FROM_ALL)
add_subdirectory(fmt EXCLUDE_FROM_ALL)

# sst-cpputils and simde must exist before sst-basic-blocks, which links them if present.
add_subdirectory(sst/sst-cpputils)

set(SST_BASIC_BLOCKS_SIMD_OMIT_NATIVE_ALIASES ON CACHE BOOL "No native SIMD aliases for Giga Grains")
add_subdirectory(sst/sst-basic-blocks)
