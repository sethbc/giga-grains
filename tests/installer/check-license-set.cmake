# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks the license set that gg-products writes beside the products (cmake/products.cmake), which
# every installer carries (spec §6.3). Run as a CTest:
#
#   cmake -DDIR=<licenses dir> -DPLATFORM=<Darwin|Windows|Linux> -P <this file>
#
# DIR must hold exactly the platform's files, each larger than 1,000 bytes (no stub or truncated
# text): the notices, the AGPL, the GPLv3, the two MIT texts and the CC0 1.0 legal code no checkout
# holds, plus the AudioUnitSDK's Apache 2.0 text on macOS and the ASIO SDK's license on Windows.

cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS DIR PLATFORM)
    if (NOT DEFINED ${var})
        message(FATAL_ERROR "check-license-set: ${var} is not set")
    endif ()
endforeach ()

set(expected THIRD_PARTY_NOTICES.md LICENSE GPL-3.0.txt CMakeRC-LICENSE.txt
        flowbite-icons-LICENSE.txt CC0-1.0.txt)
if (PLATFORM STREQUAL "Darwin")
    list(APPEND expected AudioUnitSDK-LICENSE.txt)
elseif (PLATFORM STREQUAL "Windows")
    list(APPEND expected ASIO-SDK-LICENSE.txt)
elseif (NOT PLATFORM STREQUAL "Linux")
    message(FATAL_ERROR "check-license-set: PLATFORM '${PLATFORM}' isn't Darwin, Windows or Linux")
endif ()

# An absolute path only: a relative (or empty) one would be read from wherever CTest runs.
if (NOT IS_ABSOLUTE "${DIR}")
    message(FATAL_ERROR "check-license-set: DIR '${DIR}' isn't an absolute path")
endif ()
if (NOT IS_DIRECTORY "${DIR}")
    message(FATAL_ERROR "FAIL: ${DIR} doesn't exist")
endif ()

set(failures "")
foreach (name IN LISTS expected)
    set(file "${DIR}/${name}")
    if (NOT EXISTS "${file}")
        string(APPEND failures "\n  ${name} is missing")
        continue()
    endif ()
    file(SIZE "${file}" size)
    if (size LESS_EQUAL 1000)
        string(APPEND failures "\n  ${name} is ${size} bytes, not more than 1,000")
    endif ()
endforeach ()

file(GLOB present RELATIVE "${DIR}" "${DIR}/*" "${DIR}/.*")
foreach (name IN LISTS present)
    if (NOT name IN_LIST expected)
        string(APPEND failures "\n  ${name} shouldn't be there")
    endif ()
endforeach ()

if (failures)
    message(FATAL_ERROR "FAIL: ${DIR} isn't the ${PLATFORM} license set:${failures}")
endif ()
list(LENGTH expected count)
message(STATUS "PASS: ${DIR} holds the ${count} files of the ${PLATFORM} license set")
