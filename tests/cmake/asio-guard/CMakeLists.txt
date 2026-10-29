# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# A scratch project for the ASIO SDK guard (cmake/AsioGuard.cmake, carry-over [116]). The CTests
# asio_guard_* configure it with -DGG_GUARD_CASE=<case> and -DGG_REPO=<repository root>. It
# plays the part of libs/CMakeLists.txt (a fake asiosdk folder, one allowed target with one
# allowed source and one allowed include directory) and then of a misuse, and calls the guard.
# Case "allowed" must configure; every other case must fail naming the misuse.
cmake_minimum_required(VERSION 3.22)
project(asio-guard-scratch CXX)

include(${GG_REPO}/cmake/AsioGuard.cmake)

set(sdk ${CMAKE_CURRENT_BINARY_DIR}/asiosdk)
file(MAKE_DIRECTORY ${sdk}/common ${sdk}/host)
foreach (name IN ITEMS common/asio.cpp host/other.cpp)
    file(WRITE ${sdk}/${name} "int f() { return 0; }\n")
endforeach ()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/plain.cpp "int plain() { return 0; }\n")

# What libs/CMakeLists.txt does on Windows.
add_library(rtaudio STATIC ${CMAKE_CURRENT_BINARY_DIR}/plain.cpp)
target_sources(rtaudio PRIVATE ${sdk}/common/asio.cpp)
target_include_directories(rtaudio PRIVATE ${sdk}/common)
set_property(GLOBAL PROPERTY GG_ASIO_TARGET rtaudio)
set_property(GLOBAL PROPERTY GG_ASIO_SOURCES ${sdk}/common/asio.cpp)
set_property(GLOBAL PROPERTY GG_ASIO_INCLUDE_DIRS ${sdk}/common)

add_library(other STATIC ${CMAKE_CURRENT_BINARY_DIR}/plain.cpp)
add_subdirectory(sub)

if (GG_GUARD_CASE STREQUAL "allowed")
    # nothing more
elseif (GG_GUARD_CASE STREQUAL "other-target-source")
    target_sources(other PRIVATE ${sdk}/common/asio.cpp)
elseif (GG_GUARD_CASE STREQUAL "other-target-include")
    target_include_directories(other PRIVATE ${sdk}/common)
elseif (GG_GUARD_CASE STREQUAL "other-target-link")
    target_link_libraries(other PRIVATE ${sdk}/common/asio.lib)
elseif (GG_GUARD_CASE STREQUAL "other-file-in-allowed-target")
    target_sources(rtaudio PRIVATE ${sdk}/host/other.cpp)
elseif (GG_GUARD_CASE STREQUAL "other-dir-in-allowed-target")
    target_include_directories(rtaudio PRIVATE ${sdk}/host)
elseif (GG_GUARD_CASE STREQUAL "allowed-source-in-other-target-of-a-subdirectory")
    target_sources(sub_lib PRIVATE ${sdk}/common/asio.cpp)
elseif (GG_GUARD_CASE STREQUAL "directory-include")
    include_directories(${sdk}/host)
else ()
    message(FATAL_ERROR "unknown GG_GUARD_CASE ${GG_GUARD_CASE}")
endif ()

gg_check_asiosdk(ROOT ${CMAKE_CURRENT_SOURCE_DIR})
