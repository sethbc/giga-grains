# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# One shared, framework-free grain engine (engine/), the JUCE-free layer both plugins share above
# it (core/: the patch), sample-file I/O (media/), the CLAP-first plugin shells for both products
# (plugin/, clients/clap-first/), their editor (ui/: a framework-free canvas core plus a JUCE
# editor attached through clap_juce_shim), the tools and the tests. See INTENT.md.

cmake_minimum_required(VERSION 3.22)

# These must be set before project() so they reach toolchain detection.
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The rtsan preset takes its compilers from $GG_LLVM. Unset, they resolve to /bin/clang, which is
# missing or has no RTSan; name the cause here rather than let project() fail on the compiler.
if (GG_USE_RTSAN AND IS_ABSOLUTE "${CMAKE_CXX_COMPILER}" AND NOT EXISTS "${CMAKE_CXX_COMPILER}")
    message(FATAL_ERROR "GG_USE_RTSAN: the C++ compiler ${CMAKE_CXX_COMPILER} doesn't exist. "
            "Set GG_LLVM to an upstream LLVM 20 or newer when configuring, for example "
            "GG_LLVM=/opt/homebrew/opt/llvm cmake --preset rtsan")
endif ()

project(GigaGrains VERSION 0.2.0 LANGUAGES C CXX)
# The version (spec §6.1): project()'s VERSION is the numeric part, and GG_VERSION_PRERELEASE the
# pre-release suffix. A build from main reports 0.2.0-dev; a release commit sets beta.N, or empty
# for the milestone release, and the nightly configures with dev.<yyyymmdd>.g<sha7>. Bundle
# metadata takes GG_VERSION_NUMERIC alone; everything else reports GG_VERSION_STRING.
set(GG_VERSION_PRERELEASE "dev" CACHE STRING "Pre-release suffix: dev on main, beta.N for a beta, empty for a release")
include(cmake/version.cmake)
gg_compose_version("${PROJECT_VERSION}" "${GG_VERSION_PRERELEASE}" GG_VERSION_STRING)
set(GG_VERSION_NUMERIC "${PROJECT_VERSION}")

option(GG_BUILD_PLUGINS "Build the plugins: their core, and the formats with GG_BUILD_EDITOR" ON)
# GG_BUILD_EDITOR=OFF with GG_BUILD_PLUGINS=ON builds the plugins headless: the JUCE-free plugin
# core and its [plugin] tests, without the editor, JUCE or clap-wrapper's CLAP/VST3/AUv2/standalone
# targets. Nothing is downloaded then. The sanitizer presets build the plugin audio path this way.
option(GG_BUILD_EDITOR "Build the JUCE editor and the clap-wrapper formats" ON)
option(GG_SANITIZE "Build with clang/gcc address and undefined-behaviour sanitizers" OFF)
option(GG_USE_RTSAN "Build with the LLVM RealtimeSanitizer" OFF)
# RealtimeSanitizer is in upstream Clang 20 and newer; Apple clang and gcc have none.
if (GG_USE_RTSAN AND NOT (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 20))
    message(FATAL_ERROR "GG_USE_RTSAN needs upstream Clang 20 or newer, but the C++ compiler is "
            "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} (${CMAKE_CXX_COMPILER}). "
            "Set GG_LLVM to an upstream LLVM 20 or newer when configuring, for example "
            "GG_LLVM=/opt/homebrew/opt/llvm cmake --preset rtsan")
endif ()
option(GG_COPY_PLUGIN_AFTER_BUILD "Copy plugins to the user plugin folders after build" OFF)
set(GG_JUCE_PATH "${CMAKE_SOURCE_DIR}/libs/JUCE" CACHE PATH "JUCE checkout used for the UI")
if (APPLE AND GG_BUILD_PLUGINS AND GG_BUILD_EDITOR)
    # clients/clap-first signs every macOS bundle with this identity after building it, and the
    # codesign_* tests verify the signatures. "-" signs ad hoc, locally and on every CI run but a
    # tag's or the nightly's. A Developer ID signs with a real identity, and then also with the
    # hardened runtime and a secure timestamp, and the two standalone apps with their
    # entitlements (installer/macos/standalone.entitlements: the microphone). The installer is
    # notarized after that. See docs/superpowers/specs/2026-10-29-release-pipeline-design.md §6.2.
    set(GG_MACOS_CODESIGN_IDENTITY "-" CACHE STRING
            "Identity that signs the macOS plugin bundles and apps: - (ad hoc) or a Developer ID")
    if ("${GG_MACOS_CODESIGN_IDENTITY}" STREQUAL "")
        message(FATAL_ERROR "GG_MACOS_CODESIGN_IDENTITY is empty; use - to sign ad hoc")
    endif ()
    find_program(GG_CODESIGN codesign REQUIRED)
endif ()

message(STATUS "Giga Grains ${GG_VERSION_STRING}")
message(STATUS "Compiler is ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
# The toolchain floor (C++23): GCC 13 or newer. Apple clang and ClangCL compile C++23 as they are.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
    message(FATAL_ERROR "Giga Grains needs GCC 13 or newer for C++23; ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} found")
endif ()

include(cmake/compiler-options.cmake)
# The license set's one list, which cmake/products.cmake reads for gg-products and for the
# editor's embedded notices (gg_license_files, gg_add_notices).
include(cmake/licenses.cmake)

add_subdirectory(libs)

enable_testing()

add_subdirectory(engine)
add_subdirectory(core)
add_subdirectory(media)
add_subdirectory(ui)
add_subdirectory(tools/audiometrics)
add_subdirectory(tools/gg-bench)
add_subdirectory(tools/gg-render)
# The CC0 content's pin, GG_CONTENT_ROOT and the gg-content target (spec §6.5).
include(cmake/content.cmake)
if (GG_BUILD_PLUGINS)
    add_subdirectory(plugin)
endif ()
if (GG_BUILD_PLUGINS AND GG_BUILD_EDITOR)
    add_subdirectory(clients/clap-first)
    include(cmake/products.cmake)
    add_subdirectory(tools/gg-snap)
endif ()
add_subdirectory(tests)

# Nothing but RtAudio's ASIO backend may use libs/asiosdk (INTENT §15, §23 #56).
include(cmake/AsioGuard.cmake)
gg_check_asiosdk()

# Everything this tree builds; CI builds this target.
add_custom_target(gg-all)
add_dependencies(gg-all gg-engine gg-core gg-media gg-ui-core gg-audiometrics gg-bench gg-render
        gg-tests)
if (TARGET gg-rtsan-negative-control)
    add_dependencies(gg-all gg-rtsan-negative-control)
endif ()
if (GG_BUILD_PLUGINS)
    add_dependencies(gg-all gg-plugin-core)
endif ()
if (GG_BUILD_PLUGINS AND GG_BUILD_EDITOR)
    add_dependencies(gg-all gg-plugin-editor gg_clapfirst_all gg-snap)
endif ()
