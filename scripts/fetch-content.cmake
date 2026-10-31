# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Fetches the content release that a pin names (content/pin.txt: the samples the factory presets
# load, CC0, from https://github.com/sethbc/giga-grains-content; spec §6.5), or checks a folder
# against it:
#
#   cmake -DDEST=<dir> -DPIN=<pin.txt> -P scripts/fetch-content.cmake
#   cmake -DCHECK=<content dir> -DPIN=<pin.txt> -P scripts/fetch-content.cmake
#
# The pin is three lines: `tag <tag>`, `url <the release asset's URL>` and `sha256 <64 hex>`.
#
# Fetching downloads the asset to <dir>/giga-grains-content-<tag>.zip, checks its SHA-256,
# extracts it into <dir>/content/ and writes <dir>/content/.gg-pin (`<tag> <sha256>`) last; the
# zip is then removed. A <dir> whose .gg-pin already matches costs no network and prints
# `content: <tag> already at <dir>/content`. A download whose hash differs is deleted and the
# script fails naming both hashes, so no bad zip stays behind. (file(DOWNLOAD)'s EXPECTED_HASH
# would also fail, but it leaves the bad file in place, so the hash is checked here instead.) The
# download gives up after 600 s in all, or 60 s with nothing received. A failed fetch leaves any older <dir>/content without a matching .gg-pin, so the next run fetches
# again.
#
# Checking (CMake's gg-content target, when GG_CONTENT_DIR names a folder fetched elsewhere, as
# CI's cache is) fails unless <content dir>/.gg-pin matches the pin, naming both.
#
# scripts/fetch-content.sh wraps the fetch for CI; cmake/content.cmake runs both forms.

cmake_minimum_required(VERSION 3.22)

if (NOT PIN)
    message(FATAL_ERROR "usage: cmake -DDEST=<dir> -DPIN=<pin.txt> -P fetch-content.cmake\n"
            "   or: cmake -DCHECK=<content dir> -DPIN=<pin.txt> -P fetch-content.cmake")
endif ()
if (NOT EXISTS "${PIN}")
    message(FATAL_ERROR "fetch-content: no pin file ${PIN}")
endif ()

# The pin's three lines; anything else in it is an error.
file(STRINGS "${PIN}" pin_lines)
foreach (line IN LISTS pin_lines)
    if (line MATCHES "^tag ([A-Za-z0-9._-]+)$")
        set(pin_tag "${CMAKE_MATCH_1}")
    elseif (line MATCHES "^url ([^ ]+)$")
        set(pin_url "${CMAKE_MATCH_1}")
    elseif (line MATCHES "^sha256 ([0-9a-f]+)$")
        set(pin_sha256 "${CMAKE_MATCH_1}")
    else ()
        message(FATAL_ERROR "fetch-content: ${PIN}: unexpected line \"${line}\"")
    endif ()
endforeach ()
foreach (key IN ITEMS tag url sha256)
    if (NOT DEFINED pin_${key})
        message(FATAL_ERROR "fetch-content: ${PIN} has no ${key} line")
    endif ()
endforeach ()
string(LENGTH "${pin_sha256}" sha256_length)
if (NOT sha256_length EQUAL 64)
    message(FATAL_ERROR "fetch-content: ${PIN}: the sha256 must be 64 lowercase hex digits")
endif ()
set(want "${pin_tag} ${pin_sha256}")

# Checking a folder fetched elsewhere.
if (CHECK)
    set(have "(no .gg-pin)")
    if (EXISTS "${CHECK}/.gg-pin")
        file(READ "${CHECK}/.gg-pin" have)
        string(STRIP "${have}" have)
    endif ()
    if (NOT have STREQUAL want)
        message(FATAL_ERROR "fetch-content: ${CHECK} holds content ${have}, but ${PIN} pins "
                "${want}; fetch it again (scripts/fetch-content.sh) or clear GG_CONTENT_DIR")
    endif ()
    message(STATUS "content: ${pin_tag} checked at ${CHECK}")
    return()
endif ()

if (NOT DEST)
    message(FATAL_ERROR "fetch-content: give -DDEST=<dir> to fetch, or -DCHECK=<content dir>")
endif ()
get_filename_component(dest "${DEST}" ABSOLUTE)
set(content "${dest}/content")
set(stamp "${content}/.gg-pin")
if (EXISTS "${stamp}")
    file(READ "${stamp}" have)
    string(STRIP "${have}" have)
    if (have STREQUAL want)
        message(STATUS "content: ${pin_tag} already at ${content}")
        return()
    endif ()
endif ()

set(zip "${dest}/giga-grains-content-${pin_tag}.zip")
file(REMOVE "${zip}")
file(MAKE_DIRECTORY "${dest}")
message(STATUS "content: fetching ${pin_tag} from ${pin_url}")
# Bounded (T3): ten minutes in all, and a minute without a byte, so a stalled server fails the
# fetch rather than hanging the build.
file(DOWNLOAD "${pin_url}" "${zip}" STATUS download_status TLS_VERIFY ON TIMEOUT 600
        INACTIVITY_TIMEOUT 60)
list(GET download_status 0 download_code)
if (NOT download_code EQUAL 0)
    file(REMOVE "${zip}")
    list(GET download_status 1 download_message)
    message(FATAL_ERROR "fetch-content: downloading ${pin_url} failed: ${download_message}")
endif ()
file(SHA256 "${zip}" actual)
if (NOT actual STREQUAL pin_sha256)
    file(REMOVE "${zip}")
    message(FATAL_ERROR "fetch-content: ${pin_url} has SHA-256 ${actual}, but ${PIN} pins "
            "${pin_sha256}; the download was deleted")
endif ()

# Extract beside the old folder, then swap: an interrupted run leaves no half folder with a
# matching .gg-pin.
set(partial "${dest}/content.partial")
file(REMOVE_RECURSE "${partial}")
file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${partial}")
foreach (entry IN ITEMS LICENSE README.md PROVENANCE.md Samples)
    if (NOT EXISTS "${partial}/${entry}")
        file(REMOVE_RECURSE "${partial}")
        file(REMOVE "${zip}")
        message(FATAL_ERROR "fetch-content: ${pin_url} has no ${entry} at its top level")
    endif ()
endforeach ()
file(REMOVE_RECURSE "${content}")
file(RENAME "${partial}" "${content}")
file(REMOVE "${zip}")
file(WRITE "${stamp}" "${want}\n")
message(STATUS "content: ${pin_tag} fetched into ${content}")
