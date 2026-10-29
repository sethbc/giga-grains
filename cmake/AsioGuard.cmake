# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# The ASIO SDK guard (carry-over [116]). sst-plugininfra's libs/asiosdk is the Steinberg ASIO SDK,
# and we use it under its GPLv3 option for exactly one purpose (INTENT §15, §23 #56): three of
# its files are compiled, Windows only, into RtAudio for the standalone's ASIO (libs/CMakeLists.txt,
# which also pins the files by SHA-256). Nothing else may use it. gg_check_asiosdk() walks every
# target the configure created and fails, naming the target and the property, when any of these
# mention "asiosdk" other than the allowed use:
#   SOURCES, INCLUDE_DIRECTORIES, LINK_LIBRARIES, COMPILE_OPTIONS and COMPILE_DEFINITIONS (and
#   their INTERFACE_ forms), and the directories' INCLUDE_DIRECTORIES.
# The allowed use is what libs/CMakeLists.txt records in the global properties
# GG_ASIO_TARGET, GG_ASIO_SOURCES and GG_ASIO_INCLUDE_DIRS (empty off Windows, so nothing is
# allowed there). The pins stay in libs/CMakeLists.txt; the guard only checks who uses the files.
#
#   gg_check_asiosdk([ROOT <dir>])
#
# ROOT is the source directory to start the walk from (default CMAKE_SOURCE_DIR); the scratch
# projects in tests/cmake/asio-guard use it through their own top-level project.

function (gg_asio_guard_walk dir out)
    set(found ${dir})
    get_property(subdirs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
    foreach (sub IN LISTS subdirs)
        gg_asio_guard_walk(${sub} sub_found)
        list(APPEND found ${sub_found})
    endforeach ()
    set(${out} ${found} PARENT_SCOPE)
endfunction ()

function (gg_check_asiosdk)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "ROOT" "")
    if (NOT arg_ROOT)
        set(arg_ROOT ${CMAKE_SOURCE_DIR})
    endif ()
    get_property(allowed_target GLOBAL PROPERTY GG_ASIO_TARGET)
    get_property(allowed_sources GLOBAL PROPERTY GG_ASIO_SOURCES)
    get_property(allowed_includes GLOBAL PROPERTY GG_ASIO_INCLUDE_DIRS)

    gg_asio_guard_walk(${arg_ROOT} directories)
    set(problems)
    set(allowed_uses 0)

    foreach (directory IN LISTS directories)
        get_property(entries DIRECTORY ${directory} PROPERTY INCLUDE_DIRECTORIES)
        foreach (entry IN LISTS entries)
            string(TOLOWER "${entry}" lower)
            if (lower MATCHES "asiosdk")
                list(APPEND problems "directory ${directory}: INCLUDE_DIRECTORIES has ${entry}")
            endif ()
        endforeach ()
        get_property(targets DIRECTORY ${directory} PROPERTY BUILDSYSTEM_TARGETS)
        foreach (target IN LISTS targets)
            foreach (property IN ITEMS SOURCES INTERFACE_SOURCES INCLUDE_DIRECTORIES
                    INTERFACE_INCLUDE_DIRECTORIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
                    LINK_LIBRARIES INTERFACE_LINK_LIBRARIES LINK_OPTIONS INTERFACE_LINK_OPTIONS
                    COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS COMPILE_DEFINITIONS
                    INTERFACE_COMPILE_DEFINITIONS)
                get_target_property(entries ${target} ${property})
                if (NOT entries)
                    continue()
                endif ()
                foreach (entry IN LISTS entries)
                    string(TOLOWER "${entry}" lower)
                    if (NOT lower MATCHES "asiosdk")
                        continue()
                    endif ()
                    if ("${target}" STREQUAL "${allowed_target}"
                            AND ((property STREQUAL "SOURCES" AND entry IN_LIST allowed_sources)
                            OR (property STREQUAL "INCLUDE_DIRECTORIES"
                            AND entry IN_LIST allowed_includes)))
                        math(EXPR allowed_uses "${allowed_uses} + 1")
                    else ()
                        list(APPEND problems "target ${target}: ${property} has ${entry}")
                    endif ()
                endforeach ()
            endforeach ()
        endforeach ()
    endforeach ()

    if (problems)
        list(JOIN problems "\n  " text)
        message(FATAL_ERROR "libs/asiosdk (the ASIO SDK, used under GPLv3 for RtAudio's ASIO only: "
                "INTENT §15, §23 #56) is used outside that:\n  ${text}")
    endif ()
    message(STATUS "ASIO SDK guard: ${allowed_uses} allowed use(s), no other")
endfunction ()
