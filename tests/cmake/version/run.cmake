# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Checks gg_compose_version (cmake/version.cmake) for one case. Run as a CTest:
#
#   cmake -DGG_CASE=<case> -DGG_REPO=<root> -P run.cmake
#
# dev, release and nightly must compose the expected version string. bad-underscore and
# bad-empty-identifier must fail, naming GG_VERSION_PRERELEASE: this script runs itself again in
# a nested cmake -P (GG_NESTED set), which makes the call, and checks that it failed. Nothing is
# written, so the cases run in parallel.

cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS GG_CASE GG_REPO)
    if (NOT DEFINED ${var})
        message(FATAL_ERROR "version test: ${var} is not set")
    endif ()
endforeach ()

set(numeric "0.2.0")
if (GG_CASE STREQUAL "dev")
    set(prerelease "dev")
    set(expected "0.2.0-dev")
elseif (GG_CASE STREQUAL "release")
    set(prerelease "")
    set(expected "0.2.0")
elseif (GG_CASE STREQUAL "nightly")
    set(prerelease "dev.20261029.95bee95")
    set(expected "0.2.0-dev.20261029.95bee95")
elseif (GG_CASE STREQUAL "bad-underscore")
    set(prerelease "beta_1")
elseif (GG_CASE STREQUAL "bad-empty-identifier")
    set(prerelease "beta..1")
else ()
    message(FATAL_ERROR "version test: unknown case '${GG_CASE}'")
endif ()

if (DEFINED expected)
    include(${GG_REPO}/cmake/version.cmake)
    gg_compose_version("${numeric}" "${prerelease}" version)
    if (NOT version STREQUAL expected)
        message(FATAL_ERROR "FAIL: ${numeric} with '${prerelease}' composed '${version}', "
                "expected '${expected}'")
    endif ()
    message(STATUS "${numeric} with '${prerelease}' composes ${version}")
elseif (GG_NESTED)
    include(${GG_REPO}/cmake/version.cmake)
    gg_compose_version("${numeric}" "${prerelease}" version)
    message(STATUS "composed '${version}'")
else ()
    execute_process(
            COMMAND ${CMAKE_COMMAND} -DGG_CASE=${GG_CASE} -DGG_REPO=${GG_REPO} -DGG_NESTED=ON
            -P ${CMAKE_CURRENT_LIST_FILE}
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if (status EQUAL 0)
        message(FATAL_ERROR "FAIL: the suffix '${prerelease}' was accepted:\n${out}")
    endif ()
    if (NOT err MATCHES "GG_VERSION_PRERELEASE")
        message(FATAL_ERROR "FAIL: the suffix '${prerelease}' failed, but not naming "
                "GG_VERSION_PRERELEASE:\n${out}\n${err}")
    endif ()
    message(STATUS "the suffix '${prerelease}' is refused")
endif ()
