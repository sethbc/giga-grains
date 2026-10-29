# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks an archive's entries (spec §6.5): the Linux tarball and the Windows zip.
#
#   cmake -DARCHIVE=<file> -DKIND=tar|zip -DEXPECT=<;-list> [-DFORBID=<;-list>]
#         [-DPREFIX=<folder>] [-DEXECUTABLE=<;-list>] -P <this file>
#
#   ARCHIVE     the archive, listed with `cmake -E tar tf` (libarchive), so it runs on every
#               platform.
#   KIND        tar (a tar, gzip-compressed or not) or zip; the file's first bytes must say so.
#   EXPECT      entries the archive must hold, by their path inside it.
#   FORBID      entries it must not hold.
#   PREFIX      if set, every entry must sit under <PREFIX>/ (the folder itself may be an entry).
#   EXECUTABLE  entries whose mode, in `cmake -E tar tvf`'s listing, must have the owner's
#               execute bit (a zip records no Unix modes, so this is for KIND=tar).
#
# A directory entry is compared without its trailing slash. Fails naming every missing,
# forbidden, stray or non-executable entry, with the archive's listing.

cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS ARCHIVE KIND EXPECT)
    if (NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "check-archive: ${var} is not set")
    endif ()
endforeach ()
if (NOT KIND STREQUAL "tar" AND NOT KIND STREQUAL "zip")
    message(FATAL_ERROR "check-archive: KIND is '${KIND}', not tar or zip")
endif ()
if (NOT EXISTS "${ARCHIVE}" OR IS_DIRECTORY "${ARCHIVE}")
    message(FATAL_ERROR "FAIL: the archive ${ARCHIVE} doesn't exist")
endif ()

# The kind, from the magic bytes: gzip (1f 8b) or a plain tar's "ustar" at offset 257; a zip's
# local file header, "PK\3\4". Both are read as hex: a text read of these 5 bytes returns
# "ustar" and a newline (CMake 3.31 and 4.4), which never equals "ustar".
file(READ "${ARCHIVE}" head LIMIT 4 HEX)
file(READ "${ARCHIVE}" ustar OFFSET 257 LIMIT 5 HEX)
if (KIND STREQUAL "tar")
    if (NOT head MATCHES "^1f8b" AND NOT ustar STREQUAL "7573746172")
        message(FATAL_ERROR "FAIL: ${ARCHIVE} is neither a gzip file nor a tar (it starts ${head})")
    endif ()
elseif (NOT head STREQUAL "504b0304")
    message(FATAL_ERROR "FAIL: ${ARCHIVE} isn't a zip (it starts ${head})")
endif ()

# lines_of <output> <out var>: the output's lines as a list, each with any trailing slash (a
# directory) and CR removed. Entry names hold no semicolons.
function(lines_of text out)
    string(REPLACE "\r" "" text "${text}")
    string(REGEX REPLACE "\n+$" "" text "${text}")
    string(REPLACE "\n" ";" lines "${text}")
    set(result "")
    foreach (line IN LISTS lines)
        string(REGEX REPLACE "/$" "" line "${line}")
        list(APPEND result "${line}")
    endforeach ()
    set(${out} "${result}" PARENT_SCOPE)
endfunction()

execute_process(COMMAND ${CMAKE_COMMAND} -E tar tf "${ARCHIVE}"
        RESULT_VARIABLE list_result OUTPUT_VARIABLE listing ERROR_VARIABLE list_error)
if (NOT list_result EQUAL 0)
    message(FATAL_ERROR "FAIL: cmake -E tar tf ${ARCHIVE} failed: ${list_error}")
endif ()
lines_of("${listing}" entries)
if (NOT entries)
    message(FATAL_ERROR "FAIL: ${ARCHIVE} is empty")
endif ()

set(failures "")
foreach (entry IN LISTS EXPECT)
    if (NOT entry IN_LIST entries)
        string(APPEND failures "\n  it lacks ${entry}")
    endif ()
endforeach ()
foreach (entry IN LISTS FORBID)
    if (entry IN_LIST entries)
        string(APPEND failures "\n  it holds ${entry}, which it must not")
    endif ()
endforeach ()
if (DEFINED PREFIX AND NOT PREFIX STREQUAL "")
    foreach (entry IN LISTS entries)
        string(FIND "${entry}/" "${PREFIX}/" at)
        if (NOT at EQUAL 0)
            string(APPEND failures "\n  ${entry} isn't under ${PREFIX}/")
        endif ()
    endforeach ()
endif ()

# The modes: tvf lists the same entries in the same order as tf, each line starting with the
# mode (-rwxr-xr-x), so the two listings are paired by index rather than by parsing names.
set(verbose "")
if (EXECUTABLE)
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar tvf "${ARCHIVE}"
            RESULT_VARIABLE verbose_result OUTPUT_VARIABLE verbose ERROR_VARIABLE verbose_error)
    if (NOT verbose_result EQUAL 0)
        message(FATAL_ERROR "FAIL: cmake -E tar tvf ${ARCHIVE} failed: ${verbose_error}")
    endif ()
    lines_of("${verbose}" modes)
    list(LENGTH entries entry_count)
    list(LENGTH modes mode_count)
    if (NOT entry_count EQUAL mode_count)
        message(FATAL_ERROR "FAIL: cmake -E tar tvf lists ${mode_count} entries of ${ARCHIVE}, "
                "tf ${entry_count}\n--- tf\n${listing}--- tvf\n${verbose}")
    endif ()
    foreach (entry IN LISTS EXECUTABLE)
        list(FIND entries "${entry}" index)
        if (index EQUAL -1)
            string(APPEND failures "\n  it lacks ${entry}, which must be executable")
            continue()
        endif ()
        list(GET modes ${index} line)
        string(SUBSTRING "${line}" 0 10 mode)
        if (NOT mode MATCHES "^-..x")
            string(APPEND failures "\n  ${entry} is ${mode}, not an executable file")
        endif ()
    endforeach ()
endif ()

if (failures)
    if (verbose)
        set(listing "${verbose}")
    endif ()
    message(FATAL_ERROR "FAIL: ${ARCHIVE}:${failures}\n--- its entries\n${listing}")
endif ()
list(LENGTH entries count)
list(LENGTH EXPECT expect_count)
list(LENGTH FORBID forbid_count)
list(LENGTH EXECUTABLE exec_count)
message(STATUS "PASS: ${ARCHIVE} (${KIND}, ${count} entries) holds the ${expect_count} expected, "
        "none of the ${forbid_count} forbidden, and the ${exec_count} executables")
