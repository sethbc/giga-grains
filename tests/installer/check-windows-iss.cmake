# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks that the Inno Setup script declares the Content component (spec §6.5): a line
# `Name: "content";` in its [Components] section, and a [Files] entry, a line starting `Source:`
# (so not a comment, T6), with `Components: content`.
# It only reads the script, so it runs on every OS, without Inno Setup or the products:
#
#   cmake -DISS=<giga-grains.iss> -P <this file>
#
# Its CTest is installer_windows_iss_content; installer_windows_inno compiles the script itself.

cmake_minimum_required(VERSION 3.22)

if (NOT DEFINED ISS)
    message(FATAL_ERROR "check-windows-iss: ISS is not set")
endif ()
if (NOT IS_ABSOLUTE "${ISS}")
    message(FATAL_ERROR "check-windows-iss: ISS '${ISS}' isn't an absolute path")
endif ()
if (NOT EXISTS "${ISS}")
    message(FATAL_ERROR "FAIL: ${ISS} doesn't exist")
endif ()

file(STRINGS "${ISS}" lines)
set(section "")
set(found_component FALSE)
set(found_file FALSE)
foreach (line IN LISTS lines)
    if (line MATCHES "^\\[([A-Za-z]+)\\]")
        set(section "${CMAKE_MATCH_1}")
    elseif (section STREQUAL "Components" AND line MATCHES "^Name: \"content\";")
        set(found_component TRUE)
    elseif (section STREQUAL "Files" AND line MATCHES "^Source: .*; Components: content(;|$)")
        set(found_file TRUE)
    endif ()
endforeach ()

set(failures "")
if (NOT found_component)
    string(APPEND failures "\n  [Components] has no Name: \"content\" line")
endif ()
if (NOT found_file)
    string(APPEND failures "\n  [Files] has no Source: line with Components: content")
endif ()
if (NOT failures STREQUAL "")
    message(FATAL_ERROR "FAIL: ${ISS} doesn't declare the Content component:${failures}")
endif ()
message(STATUS "PASS: ${ISS} declares the Content component and installs it")
