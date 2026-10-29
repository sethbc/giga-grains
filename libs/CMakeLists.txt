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

# sst-filters and sst-effects (GPL-3.0-or-later): Space's reverb, sst-effects' Reverb 2, and the
# biquad its effect base names. Both are header-only INTERFACE targets; gg-core links them
# PRIVATE (core/src/space/GgFxConfig.h), so their headers compile only in gg-core's sources (and
# the tests that reach core/src), with gg::compiler-options' -ffp-contract=off. Pinned with
# shortcircuit-xt, whose pins pair with our sst-basic-blocks. Neither has submodules of its own.
# - sst-filters links sst-basic-blocks, so comes after it. Its tests and examples stay off.
# - sst-effects comes before sst-plugininfra on purpose: when sst-plugininfra::tinyxml exists,
#   its CMake compiles a preset library (CMakeRC, tinyxml) we don't use. Its tests and examples
#   stay off, and so does Nimbus's eurorack library (no eurorack target).
# FORCE, so a cache from another configuration can't turn any of these back.
set(SST_FILTERS_BUILD_TESTS OFF CACHE BOOL "Don't build sst-filters' tests" FORCE)
set(SST_FILTERS_BUILD_EXAMPLES OFF CACHE BOOL "Don't build sst-filters' examples" FORCE)
add_subdirectory(sst/sst-filters EXCLUDE_FROM_ALL)
set(SST_EFFECTS_BUILD_TESTS OFF CACHE BOOL "Don't build sst-effects' tests" FORCE)
set(SST_EFFECTS_BUILD_EXAMPLES OFF CACHE BOOL "Don't build sst-effects' examples" FORCE)
add_subdirectory(sst/sst-effects EXCLUDE_FROM_ALL)
if (TARGET sst-effects-preset-api)
    message(FATAL_ERROR "sst-effects built its preset library: add it before sst-plugininfra")
endif ()

# The audio-metrics tooling (tools/audiometrics) and gg-render use the libraries below. Their
# includes are SYSTEM and none of them links gg-compiler-options, so our -Werror never applies
# to third-party code.

# dr_libs: header-only WAV/FLAC/MP3 decoding. gg::dr_libs gives the headers; the implementation
# is compiled exactly once, in gg::dr_libs_impl below, which every user of the functions links.
add_library(dr_libs INTERFACE)
target_include_directories(dr_libs SYSTEM INTERFACE dr_libs)
add_library(gg::dr_libs ALIAS dr_libs)

# The dr_libs and stb implementation units (carry-over [119]). Each is a static library of one
# translation unit in thirdparty-impl/, compiled with the project's -ffp-contract=off
# (gg::fp-contract-off) but without gg::compiler-options, so our -Werror can't fail on a warning
# in third-party code, gcc 12's middle-end warnings among them.
add_library(dr_libs_impl STATIC thirdparty-impl/DrLibsImpl.cpp)
target_link_libraries(dr_libs_impl PRIVATE dr_libs gg::fp-contract-off)
add_library(gg::dr_libs_impl ALIAS dr_libs_impl)

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

# stb: header-only; only stb_image_write.h is used. gg::stb gives the header; the implementation
# is compiled exactly once, in gg::stb_impl, built as gg::dr_libs_impl is.
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE stb)
add_library(gg::stb ALIAS stb)

add_library(stb_impl STATIC thirdparty-impl/StbImpl.cpp)
target_link_libraries(stb_impl PRIVATE stb gg::fp-contract-off)
add_library(gg::stb_impl ALIAS stb_impl)

# r8brain-free-src (MIT), pinned at tag 7.6 (cb2abb9): sample-rate conversion on load
# (gg::media's convertRate). Since 7.0 it
# is header-only (its r8bbase.cpp is gone), so there are no sources to compile apart: the
# includes are SYSTEM, which keeps its warnings out of our -Werror, and gg::fp-contract-off comes
# with them, so whatever includes it computes with the project's rounding. Its default FFT, the
# bundled Ooura fft4g.h, is used: none of R8B_IPP, R8B_PFFFT or R8B_PFFFT_DOUBLE is defined.
# Its filter caches are guarded by a std::mutex, hence Threads.
find_package(Threads REQUIRED)
add_library(r8brain INTERFACE)
target_include_directories(r8brain SYSTEM INTERFACE r8brain)
target_link_libraries(r8brain INTERFACE gg::fp-contract-off Threads::Threads)
add_library(gg::r8brain ALIAS r8brain)

# utf8proc (MIT; its Unicode data under the Unicode license), pinned at tag v2.12.0 (79cdd5a,
# Unicode 18): Unicode normalization for gg-media's search for a missing sample, which matches a
# file name saved in one normal form against the same name stored in the other (NFC and NFD;
# carry-over [190]). Built from its one source file (utf8proc.c includes utf8proc_data.c), not
# its own CMake, as a static C library without gg::compiler-options, so our -Werror never applies
# to it. UTF8PROC_STATIC keeps utf8proc.h from declaring dllimport on Windows.
add_library(utf8proc STATIC utf8proc/utf8proc.c)
target_include_directories(utf8proc SYSTEM PUBLIC utf8proc)
target_compile_definitions(utf8proc PUBLIC UTF8PROC_STATIC)
add_library(gg::utf8proc ALIAS utf8proc)

# nlohmann/json: header-only.
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE nlohmann_json/include)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)

# CLI11: gg-render's command line, used header-only (CLI11_COMPILE stays undefined).
add_library(CLI11 INTERFACE)
target_include_directories(CLI11 SYSTEM INTERFACE CLI11/include)
add_library(CLI11::CLI11 ALIAS CLI11)

# CLAP itself: header-only. Added in every preset, plugins or not, because gg-core's patch
# (sst-plugininfra's patchbase, below) needs the clap target (ruling M1-c).
add_subdirectory(clap/clap EXCLUDE_FROM_ALL)

# sst-plugininfra (MIT): gg-core's patch, its PatchBase and the tinyxml (zlib) it bundles. Only
# the patchbase interface target and tinyxml (with the filesystem shim tinyxml links) are used.
# - Never compile anything from its libs/asiosdk: ASIO stays off (ruling F4, INTENT §22 and
#   §23 #30). This copy is ASIO SDK 2.3.4, whose LICENSE.txt offers Steinberg's ASIO License or
#   GPLv3; bringing ASIO back is a separate decision. No sst-plugininfra target uses it: asiosdk
#   has no CMake of its own and nothing adds it.
# - miniz is off: we don't use it.
# - strnatcmp (zlib) is on but never built. We don't use it, but at this pin the library's own
#   sst-plugininfra target links sst-plugininfra::strnatcmp unconditionally, so turning it off
#   fails the configure.
# - The library's other targets (sst-plugininfra itself, version_information) are
#   EXCLUDE_FROM_ALL, and so is the whole directory, so only what gg-core links is built: tinyxml.
# FORCE, so a cache from another configuration can't turn any of these back.
set(SST_PLUGININFRA_PROVIDE_PATCHBASE ON CACHE BOOL "gg-core's patch is a PatchBase" FORCE)
set(SST_PLUGININFRA_PROVIDE_TINYXML ON CACHE BOOL "PatchBase's state is tinyxml" FORCE)
set(SST_PLUGININFRA_PROVIDE_MINIZ OFF CACHE BOOL "Giga Grains doesn't use miniz" FORCE)
set(SST_PLUGININFRA_PROVIDE_STRNATCMP ON CACHE BOOL "Declared, never built: sst-plugininfra's own target links it" FORCE)
set(SST_PLUGININFRA_BUILD_TESTS OFF CACHE BOOL "Don't build sst-plugininfra's tests" FORCE)
add_subdirectory(sst/sst-plugininfra EXCLUDE_FROM_ALL)
# Every translation unit that includes tinyxml.h compiles with char8_t off: sst-plugininfra's
# filesystem shim, which tinyxml links PUBLIC, puts -fno-char8_t (MSVC: /Zc:char8_t-) on its
# consumers, and the import.h that tinyxml.h includes needs it on Windows (path.u8string() into a
# std::string). The flag stays where upstream puts it. gg-core links patchbase PRIVATE and keeps
# PatchBase out of its public headers, so only gg-core's own sources get it (ruling M1-l): the
# rest of our code, nlohmann_json's users included, keeps C++20's char8_t.

# The plugins come in two layers (plugin/CMakeLists.txt):
# - the plugin core, which needs only header-only CLAP libraries from these submodules, so
#   building it downloads nothing (GG_BUILD_PLUGINS);
# - the editor and the formats: JUCE through sst-clap-helpers' clap_juce_shim, and clap-wrapper
#   for VST3, AUv2 and standalone (GG_BUILD_PLUGINS and GG_BUILD_EDITOR). clap-wrapper downloads
#   the VST3 SDK, Apple's AudioUnitSDK and RtAudio/RtMidi at configure time (CPM).

# What the plugin core builds on: clap-helpers (header-only) and clap-wrapper's extension
# headers (clapwrapper/auv2.h: the AUv2 identity factories). The headers are taken directly, not
# from clap-wrapper's own clap-wrapper-extensions target, because adding clap-wrapper's CMake
# project starts its downloads.
if (GG_BUILD_PLUGINS)
    add_subdirectory(clap/clap-helpers EXCLUDE_FROM_ALL)

    add_library(gg-clap-wrapper-extensions INTERFACE)
    target_include_directories(gg-clap-wrapper-extensions INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/clap/clap-wrapper/include)
    target_link_libraries(gg-clap-wrapper-extensions INTERFACE clap)
    add_library(gg::clap-wrapper-extensions ALIAS gg-clap-wrapper-extensions)
endif ()

# The editor and the formats.
if (GG_BUILD_PLUGINS AND GG_BUILD_EDITOR)
    set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE CACHE BOOL "Download the VST3, AU and RtAudio SDKs")
    set(CLAP_WRAPPER_DONT_ADD_TARGETS TRUE CACHE BOOL "We make our own targets")
    set(CLAP_WRAPPER_BUILD_AUV2 TRUE CACHE BOOL "Build the AUv2 wrapper")
    if (APPLE)
        set(RTAUDIO_API_JACK FALSE CACHE BOOL "No JACK on Apple")
    endif ()
    # No ASIO in the Windows standalone (ruling F4, INTENT §22). RtAudio 6.0.1 bundles Steinberg's
    # ASIO SDK files (include/asio*, iasiodrv.h), which carry Steinberg's copyright and no license
    # grant, and we ship no unlicensed code. With ASIO off, clap-wrapper doesn't build RtAudio on
    # Windows at all: the standalone uses its own WASAPI audio and WinMM MIDI backends. FORCE, so
    # a build tree configured before this change drops ASIO too.
    if (WIN32)
        set(RTAUDIO_API_ASIO OFF CACHE BOOL "No ASIO: RtAudio's bundled ASIO SDK files have no license grant" FORCE)
    endif ()
    add_subdirectory(clap/clap-wrapper EXCLUDE_FROM_ALL)

    # The editor: JUCE (GUI modules only) attached through sst-clap-helpers' clap_juce_shim, as
    # Shortcircuit XT does. add_clap_juce_shim adds JUCE's modules (JUCE_MODULES_ONLY) and the
    # clap_juce_shim library. Neither links gg-compiler-options, so our -Werror never applies
    # to JUCE.
    add_subdirectory(sst/sst-clap-helpers)
    add_clap_juce_shim(JUCE_PATH ${GG_JUCE_PATH})

    # Every JUCE_* configuration definition is set here.
    #
    # Those that change how JUCE itself compiles go on the interface of the JUCE module they
    # configure. That way they reach every TU that compiles or includes the module, with one
    # value: the module sources compiled into clap_juce_shim (or its requirements library in
    # unity builds), and gg-ui and the plugin editor through gg-juce-headers below.
    # - JUCE_USE_WEBP=0: we never load WebP images. JUCE 9.0.4's libwebp SSE4.1/AVX2 translation
    #   units (juce_graphics_libwebp_dsp_*_{sse41,avx2}.c) don't compile under ClangCL: JUCE
    #   wraps them in `#pragma clang attribute push(target("sse4.1"))`, and libwebp's MSVC-mode
    #   <intrin.h> include then clashes with clang's always_inline intrinsics. With
    #   JUCE_USE_WEBP=0 those TUs compile to nothing. Ruling T11-g.
    # - JUCE_DISABLE_JUCE_VERSION_PRINTING=1: in Debug builds juce_core otherwise prints
    #   "JUCE v9.0.4" from a static initializer when any program that links it starts, which
    #   puts it at the top of every test's output.
    target_compile_definitions(juce_graphics INTERFACE JUCE_USE_WEBP=0)
    target_compile_definitions(juce_core INTERFACE JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

    # JUCE's modules are INTERFACE libraries that compile their sources into every target that
    # links them. Only clap_juce_shim links them, so JUCE is compiled once, there. Our editor
    # code gets the same include directories and definitions through gg-juce-headers, which
    # carries no sources. SYSTEM keeps JUCE's headers out of our -Werror. On top of those, our
    # editor code is compiled as Shortcircuit XT compiles its own: as a plugin, not an app,
    # without curl or the web browser component. (clap_juce_shim sets the last two for itself
    # too.)
    add_library(gg-juce-headers INTERFACE)
    target_include_directories(gg-juce-headers SYSTEM INTERFACE
            $<TARGET_PROPERTY:clap_juce_shim,INCLUDE_DIRECTORIES>)
    target_compile_definitions(gg-juce-headers INTERFACE
            $<TARGET_PROPERTY:clap_juce_shim,COMPILE_DEFINITIONS>
            JUCE_STANDALONE_APPLICATION=0
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0
            )
    add_library(gg::juce-headers ALIAS gg-juce-headers)
endif ()
