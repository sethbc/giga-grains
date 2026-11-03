# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# snap_cli_usage: gg-snap's bad usage exits 2 with the usage on stderr and writes nothing.
#   cmake -DTOOL=<gg-snap> -DOUT=<scratch dir> -P check-snap-usage.cmake

# A -P script sets no policies of its own: the project's minimum, so CMake 3.x reads it as 4.x
# does (CMP0012's TRUE in a condition among them).
cmake_minimum_required(VERSION 3.22)

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

function(expect_usage name)
    execute_process(COMMAND "${TOOL}" ${ARGN} RESULT_VARIABLE status
            OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if (NOT status EQUAL 2)
        message(FATAL_ERROR "${name}: gg-snap exited with ${status}, not 2:\n${out}${err}")
    endif ()
    if (NOT err MATCHES "usage: gg-snap")
        message(FATAL_ERROR "${name}: no usage on stderr:\n${err}")
    endif ()
endfunction()

expect_usage("an unknown option" "${OUT}/unknown" --bogus)
expect_usage("an unknown option after the folder" "${OUT}/after" --bogus)
expect_usage("no folder")
expect_usage("a second folder" "${OUT}/a" "${OUT}/b")
expect_usage("--sizes without a value" "${OUT}/s" --sizes)
expect_usage("a malformed size" "${OUT}/m" --sizes 960)

file(GLOB made "${OUT}/*")
if (made)
    message(FATAL_ERROR "bad usage wrote ${made}")
endif ()
