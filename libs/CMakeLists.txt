# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Third-party dependencies. Each is a git submodule pinned to an exact SHA.

# Add a SIMDE target
add_library(simde INTERFACE)
target_include_directories(simde INTERFACE simde)
add_library(gg::simde ALIAS simde)

# Third-party headers reach our translation units through their targets' plain include
# directories, so their warnings (-Wall -Wextra, carry-over [94]) would fail our own -Werror.
# gg_mark_system_includes marks each named target's include directories SYSTEM, without touching
# the submodules (carry-over [219]); call it once the targets exist. Targets that don't exist
# are skipped.
function (gg_mark_system_includes)
    foreach (gg_target IN LISTS ARGN)
        if (TARGET ${gg_target})
            get_target_property(gg_aliased ${gg_target} ALIASED_TARGET)
            if (gg_aliased)
                set(gg_real ${gg_aliased})
            else ()
                set(gg_real ${gg_target})
            endif ()
            get_target_property(gg_includes ${gg_real} INTERFACE_INCLUDE_DIRECTORIES)
            if (gg_includes)
                set_property(TARGET ${gg_real} APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
                        ${gg_includes})
            endif ()
        endif ()
    endforeach ()
endfunction ()

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

# stb: header-only; stb_image_write.h (the spectrogram writer) and stb_image.h (the tests read it
# back). gg::stb gives the headers; the implementation is compiled exactly once, in
# gg::stb_impl, built as gg::dr_libs_impl is.
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
# - Its libs/asiosdk is the ASIO SDK 2.3.4, whose LICENSE.txt offers Steinberg's ASIO License
#   or GPLv3. No sst-plugininfra target uses it (asiosdk has no CMake of its own). Exactly three
#   of its files are compiled, and only on Windows, into RtAudio for the standalone's ASIO,
#   under the GPLv3 option: see the editor block below (INTENT §15, §23 #56).
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
gg_mark_system_includes(simde sst-cpputils sst-basic-blocks sst-filters sst-effects
        sst-plugininfra::patchbase sst-plugininfra::tinyxml sst-plugininfra::filesystem clap)
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
    gg_mark_system_includes(clap-helpers)

    add_library(gg-clap-wrapper-extensions INTERFACE)
    target_include_directories(gg-clap-wrapper-extensions INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/clap/clap-wrapper/include)
    gg_mark_system_includes(gg-clap-wrapper-extensions)
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
    # RtAudio's own APIs on Windows: none. RtAudio 6.0.1 bundles Steinberg's ASIO SDK files
    # (include/asio*, iasiodrv.h), which carry Steinberg's copyright and no license grant, and we
    # ship no unlicensed code (ruling F4, INTENT §23 #30). RTAUDIO_API_ASIO OFF keeps RtAudio's
    # CMake from compiling them, and with WASAPI and DirectSound off too it never puts its
    # include/ directory on a path; JACK and PulseAudio, which RtAudio would turn on if it found
    # them, are off as well. The standalone's WASAPI audio and WinMM MIDI are clap-wrapper's own
    # backends, which don't use RtAudio, so CLAP_WRAPPER_STANDALONE_RTAUDIO (RtAudio in their
    # place) is refused: it would leave the standalone with ASIO only. FORCE, so an older build
    # tree's cache can't turn any of these back.
    if (WIN32)
        if (CLAP_WRAPPER_STANDALONE_RTAUDIO)
            message(FATAL_ERROR "CLAP_WRAPPER_STANDALONE_RTAUDIO must stay off on Windows: RtAudio is built for ASIO only, without WASAPI")
        endif ()
        set(RTAUDIO_API_ASIO OFF CACHE BOOL "RtAudio's CMake never compiles its bundled ASIO files: they have no license grant" FORCE)
        set(RTAUDIO_API_WASAPI OFF CACHE BOOL "The standalone's WASAPI is clap-wrapper's own" FORCE)
        set(RTAUDIO_API_DS OFF CACHE BOOL "No DirectSound" FORCE)
        set(RTAUDIO_API_JACK OFF CACHE BOOL "No JACK on Windows" FORCE)
        set(RTAUDIO_API_PULSE OFF CACHE BOOL "No PulseAudio on Windows" FORCE)
    endif ()
    add_subdirectory(clap/clap-wrapper EXCLUDE_FROM_ALL)

    # ASIO in the Windows standalone (the director, 2026-10-28; INTENT §15, §23 #56), from the
    # Steinberg ASIO SDK 2.3.4 in sst-plugininfra's libs/asiosdk, used under its GPLv3 option
    # (its LICENSE.txt: "the Steinberg ASIO License, or alternatively ... the General Public
    # License (GPL) Version 3"). Only these SDK files are compiled, unmodified:
    # - common/asio.cpp, with common/asio.h, asiosys.h and iasiodrv.h: under that LICENSE.txt,
    #   so GPLv3 here;
    # - host/asiodrivers.cpp and host/pc/asiolist.cpp, with host/asiodrivers.h, ginclude.h and
    #   host/pc/asiolist.h: under the BSD-3-Clause-style license each carries in its header.
    # RtAudio is created here, before the standalone asks for it, with no API of its own (above);
    # then those files, RtAudio's ASIO backend (__WINDOWS_ASIO__) and our stand-in for RtAudio's
    # iasiothiscallresolver.h (thirdparty-impl/rtaudio-asio, which says why) are added to it.
    # clap-wrapper's standalone takes ASIO from RtAudio when RTAUDIO_API_ASIO is true where it
    # is called (cmake/wrap_standalone.cmake), and RtAudio already exists by then, so
    # guarantee_rtaudio() returns at once. So RTAUDIO_API_ASIO is set ON as a normal variable in
    # the parent scope, where clients/clap-first calls it; the cache entry stays OFF for RtAudio's
    # own CMake.
    if (WIN32)
        if (NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT MATCHES "MSVC" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
                OR CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "ARM")
            message(FATAL_ERROR "The standalone's ASIO builds only for Windows x64 with MSVC or ClangCL (not ARM64)")
        endif ()
        set(gg_asiosdk ${CMAKE_CURRENT_SOURCE_DIR}/sst/sst-plugininfra/libs/asiosdk)
        set(gg_asio_sources
                ${gg_asiosdk}/common/asio.cpp
                ${gg_asiosdk}/host/asiodrivers.cpp
                ${gg_asiosdk}/host/pc/asiolist.cpp)
        if (NOT EXISTS ${gg_asiosdk}/LICENSE.txt)
            message(FATAL_ERROR "No ASIO SDK at ${gg_asiosdk}: run git submodule update --init")
        endif ()
        file(READ ${gg_asiosdk}/LICENSE.txt gg_asio_license)
        if (NOT gg_asio_license MATCHES "General Public License \\(GPL\\) Version 3")
            message(FATAL_ERROR "${gg_asiosdk}/LICENSE.txt no longer offers GPLv3")
        endif ()
        # The licensing review (INTENT §23 #56) covered these files' exact content: the license,
        # every file compiled and every SDK header they include. A pin bump that changes any of
        # them fails here, and gets a fresh review before its hash is updated. Line endings are
        # normalized first: the SDK mixes CRLF and LF, and a checkout may convert them.
        foreach (pinned IN ITEMS
                "LICENSE.txt=61e56814f24b32216884b2db0b4dcc317111a5ab354d54d19dd9fc13d71b1c7a"
                "common/asio.cpp=08438d3df3fa85839f7d6606c0c9cc843f2d79f4d3f00f9e2ed343db76ea8e5f"
                "common/asio.h=6d83d9b6815d21c002b055c628860e8b0a1ca94c5ae1d9d18f137e308ba98096"
                "common/asiosys.h=387fc21d036c3f39d7f74704bebb1e9cfe0bd8564ccd48088827162b06afe583"
                "common/iasiodrv.h=0b7d9786fc77ad47dc16324629eb2ae9bff716a0edb023727df3c7ab1b59bf6d"
                "host/asiodrivers.cpp=734ddc4db97bf977d099b4702c8a78930395a5646db315076284c3ae50096afc"
                "host/asiodrivers.h=893c287ccf848ca1dde66051a87d6f4c9f62442e1eb79723fbf746d4c1c421a9"
                "host/ginclude.h=303b54d99c05fb401632cdea28d558e9f3d6fbcaad0bde4080d1b7a59751ad52"
                "host/pc/asiolist.cpp=9c908c1f97740156103b7f9fbdc0f93f2cc02ba9266e7b769afdd20e3d41b3d7"
                "host/pc/asiolist.h=5b62bc60463e72fdce708b131f1c5b681377f8fc4ea24ab2918465b1f6e07de4")
            string(REPLACE "=" ";" pinned "${pinned}")
            list(GET pinned 0 file)
            list(GET pinned 1 expected)
            file(READ ${gg_asiosdk}/${file} content)
            string(REPLACE "\r\n" "\n" content "${content}")
            string(SHA256 actual "${content}")
            if (NOT actual STREQUAL expected)
                message(FATAL_ERROR "ASIO SDK ${file} changed (SHA-256 ${actual}): review its license again (INTENT §23 #56) before updating the hash in libs/CMakeLists.txt")
            endif ()
        endforeach ()
        guarantee_rtaudio()
        target_sources(rtaudio PRIVATE ${gg_asio_sources})
        target_include_directories(rtaudio PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty-impl/rtaudio-asio
                ${gg_asiosdk}/common
                ${gg_asiosdk}/host
                ${gg_asiosdk}/host/pc)
        target_compile_definitions(rtaudio PRIVATE __WINDOWS_ASIO__)
        # What cmake/AsioGuard.cmake allows (carry-over [116]): this target, these sources and
        # these include directories use libs/asiosdk, and nothing else does.
        set_property(GLOBAL PROPERTY GG_ASIO_TARGET rtaudio)
        set_property(GLOBAL PROPERTY GG_ASIO_SOURCES ${gg_asio_sources})
        set_property(GLOBAL PROPERTY GG_ASIO_INCLUDE_DIRS
                ${gg_asiosdk}/common ${gg_asiosdk}/host ${gg_asiosdk}/host/pc)
        target_link_libraries(base-sdk-rtaudio INTERFACE ole32 winmm advapi32 user32)

        # No ASIO file of RtAudio's own may reach the build: none of its sources, and not its
        # include/ directory. Every source whose name mentions asio must be one of the SDK's.
        get_target_property(gg_rtaudio_dir rtaudio SOURCE_DIR)
        get_target_property(gg_rtaudio_sources rtaudio SOURCES)
        foreach (source IN LISTS gg_rtaudio_sources)
            string(TOLOWER "${source}" lower)
            if (lower MATCHES "asio" AND NOT source IN_LIST gg_asio_sources)
                message(FATAL_ERROR "RtAudio compiles ${source}: only the ASIO SDK's files may be compiled")
            endif ()
        endforeach ()
        get_target_property(gg_rtaudio_includes rtaudio INCLUDE_DIRECTORIES)
        string(FIND "${gg_rtaudio_includes}" "${gg_rtaudio_dir}/include" gg_rtaudio_include_at)
        if (NOT gg_rtaudio_include_at EQUAL -1)
            message(FATAL_ERROR "RtAudio's include/ (its unlicensed ASIO headers) is on its include path")
        endif ()

        set(RTAUDIO_API_ASIO ON PARENT_SCOPE)
    endif ()

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

    # sst-jucegui (MIT; the md4c and CMakeRC it vendors are MIT too): the editor's controls and
    # theme. Added after the shim, so its CMake makes sst-jucegui-juce-requirements an INTERFACE
    # library over clap_juce_shim_requirements, and after gg-juce-headers, for the line below.
    # Outside a unity build clap_juce_shim_requirements links JUCE's modules, whose sources
    # would then compile into sst-jucegui and, through its PUBLIC link, into every target that
    # links it (gg-ui, under our -Werror): JUCE three times over. So its requirements are
    # re-pointed at gg-juce-headers, the same headers and definitions without the sources, and
    # JUCE stays compiled once, in clap_juce_shim, which gg-ui and the plugin link. Its examples
    # stay off; FORCE, so a cache from another configuration can't turn them back on.
    set(SST_JUCEGUI_BUILD_EXAMPLES OFF CACHE BOOL "Don't build sst-jucegui's examples" FORCE)
    add_subdirectory(sst/sst-jucegui EXCLUDE_FROM_ALL)
    get_target_property(gg_jucegui_requirements_type sst-jucegui-juce-requirements TYPE)
    if (NOT gg_jucegui_requirements_type STREQUAL "INTERFACE_LIBRARY")
        message(FATAL_ERROR "sst-jucegui-juce-requirements is a ${gg_jucegui_requirements_type}, not an INTERFACE library: it would compile JUCE again")
    endif ()
    set_property(TARGET sst-jucegui-juce-requirements PROPERTY INTERFACE_LINK_LIBRARIES
            gg-juce-headers)
    gg_mark_system_includes(sst-jucegui sst-jucegui-resources sst-jucegui-juce-requirements
            cmrc-base)
endif ()
