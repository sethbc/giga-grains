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

# The audio-metrics tooling (tools/audiometrics) and gg-render use the libraries below. Their
# includes are SYSTEM and none of them links gg-compiler-options, so our -Werror never applies
# to third-party code.

# dr_libs: header-only WAV/FLAC/MP3 decoding. The implementation is compiled in exactly one of
# our sources (tools/audiometrics/src/WavIO.cpp).
add_library(dr_libs INTERFACE)
target_include_directories(dr_libs SYSTEM INTERFACE dr_libs)
add_library(gg::dr_libs ALIAS dr_libs)

# pffft: only the single-precision FFT, built from its sources rather than its own CMake.
# pffft_common.c is needed alongside pffft.c: it defines pffft_aligned_malloc/free.
add_library(pffft STATIC pffft/src/pffft.c pffft/src/pffft_common.c)
target_include_directories(pffft SYSTEM PUBLIC pffft/include)
target_include_directories(pffft PRIVATE pffft/include/pffft pffft/src)
# PFFFT_STATIC_DEFINE stops pffft.h from declaring dllimport on Windows. SSE is picked up
# automatically on x86; NEON has to be asked for (the define is ignored when not on ARM).
target_compile_definitions(pffft PUBLIC PFFFT_STATIC_DEFINE PRIVATE _USE_MATH_DEFINES PFFFT_ENABLE_NEON=1)
# pffft announces which SIMD flavour it built with a #pragma message, which clang reports as a
# warning.
target_compile_options(pffft PRIVATE "$<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-#pragma-messages>")
if (UNIX AND NOT APPLE)
    target_link_libraries(pffft PRIVATE m)
endif ()
add_library(gg::pffft ALIAS pffft)

# stb: header-only; only stb_image_write.h is used. The implementation is compiled in exactly
# one of our sources (tools/audiometrics/src/Spectrogram.cpp).
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE stb)
add_library(gg::stb ALIAS stb)

# nlohmann/json: header-only.
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE nlohmann_json/include)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
