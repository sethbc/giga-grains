# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# One shared, framework-free grain engine (engine/), the CLAP-first plugin shells for both
# products (plugin/, clients/clap-first/) and the tests; the UI and tools are added by later
# milestones. See INTENT.md.

cmake_minimum_required(VERSION 3.22)

# These must be set before project() so they reach toolchain detection.
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(GigaGrains VERSION 0.0.1 LANGUAGES C CXX)

option(GG_BUILD_PLUGINS "Build the CLAP/VST3/AUv2/standalone plugins" ON)
option(GG_SANITIZE "Build with clang/gcc address and undefined-behaviour sanitizers" OFF)
option(GG_USE_RTSAN "Build with the LLVM RealtimeSanitizer" OFF)
option(GG_COPY_PLUGIN_AFTER_BUILD "Copy plugins to the user plugin folders after build" OFF)
set(GG_JUCE_PATH "${CMAKE_SOURCE_DIR}/libs/JUCE" CACHE PATH "JUCE checkout used for the UI")

message(STATUS "Giga Grains ${PROJECT_VERSION}")
message(STATUS "Compiler is ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

include(cmake/compiler-options.cmake)

add_subdirectory(libs)

enable_testing()

add_subdirectory(engine)
add_subdirectory(tools/audiometrics)
add_subdirectory(tools/gg-bench)
if (GG_BUILD_PLUGINS)
    add_subdirectory(plugin)
    add_subdirectory(clients/clap-first)
    include(cmake/products.cmake)
endif ()
add_subdirectory(tests)

# Everything this tree builds; CI builds this target.
add_custom_target(gg-all)
add_dependencies(gg-all gg-engine gg-audiometrics gg-bench gg-tests)
if (GG_BUILD_PLUGINS)
    add_dependencies(gg-all gg_clapfirst_all)
endif ()
