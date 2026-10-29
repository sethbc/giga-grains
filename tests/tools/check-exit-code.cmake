# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# cmake -DPROGRAM=<program> -DARGS=<its arguments, a list> -DEXPECTED_CODE=<n>
#       -DEXPECTED_STDERR=<text> [-DEXPECTED_STDERR2=<text>] -P check-exit-code.cmake
#
# Runs PROGRAM with ARGS and fails unless it exits with exactly EXPECTED_CODE and its stderr
# contains EXPECTED_STDERR (and EXPECTED_STDERR2, when given, after it), as plain text. A crash
# or a missing binary gives neither, so it can't pass by accident, as WILL_FAIL would.

# First, so that `cmake -P` runs under the project's policies.
cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS PROGRAM EXPECTED_CODE EXPECTED_STDERR)
    if (NOT DEFINED ${var})
        message(FATAL_ERROR "check-exit-code.cmake: ${var} is not set")
    endif ()
endforeach ()

execute_process(
        COMMAND ${PROGRAM} ${ARGS}
        RESULT_VARIABLE code
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)

if (NOT code STREQUAL "${EXPECTED_CODE}")
    message(FATAL_ERROR "expected exit code ${EXPECTED_CODE}, got ${code}\nstdout:\n${out}\nstderr:\n${err}")
endif ()
string(FIND "${err}" "${EXPECTED_STDERR}" at)
if (at EQUAL -1)
    message(FATAL_ERROR "stderr lacks \"${EXPECTED_STDERR}\":\n${err}")
endif ()
if (DEFINED EXPECTED_STDERR2)
    string(SUBSTRING "${err}" ${at} -1 rest)
    string(FIND "${rest}" "${EXPECTED_STDERR2}" at2)
    if (at2 EQUAL -1)
        message(FATAL_ERROR "stderr lacks \"${EXPECTED_STDERR2}\" after \"${EXPECTED_STDERR}\":\n${err}")
    endif ()
endif ()
message(STATUS "exit ${code}, stderr as expected")
