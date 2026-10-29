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

project(GigaGrains VERSION 0.0.1 LANGUAGES C CXX)

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
    # codesign_* tests verify the signatures. "-" signs ad hoc; a Developer ID signs with a real
    # identity. That alone doesn't make a distributable bundle: distribution also needs the
    # hardened runtime, a secure timestamp and notarization, which this build doesn't do yet.
    set(GG_MACOS_CODESIGN_IDENTITY "-" CACHE STRING
            "Identity that signs the macOS plugin bundles and apps: - (ad hoc) or a Developer ID")
    if ("${GG_MACOS_CODESIGN_IDENTITY}" STREQUAL "")
        message(FATAL_ERROR "GG_MACOS_CODESIGN_IDENTITY is empty; use - to sign ad hoc")
    endif ()
    find_program(GG_CODESIGN codesign REQUIRED)
endif ()

message(STATUS "Giga Grains ${PROJECT_VERSION}")
message(STATUS "Compiler is ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

include(cmake/compiler-options.cmake)

add_subdirectory(libs)

enable_testing()

add_subdirectory(engine)
add_subdirectory(core)
add_subdirectory(media)
add_subdirectory(ui)
add_subdirectory(tools/audiometrics)
add_subdirectory(tools/gg-bench)
add_subdirectory(tools/gg-render)
if (GG_BUILD_PLUGINS)
    add_subdirectory(plugin)
endif ()
if (GG_BUILD_PLUGINS AND GG_BUILD_EDITOR)
    add_subdirectory(clients/clap-first)
    include(cmake/products.cmake)
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
    add_dependencies(gg-all gg-plugin-editor gg_clapfirst_all)
endif ()
