# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# cmake -DBENCH=<gg-bench> -DRUN_JSON=<a gg-bench --json report> -DBASELINE=<baseline to write>
#       -DRATIO=<r> [-DMACHINE=<CPU model>] -DEXPECTED_CODE=<n> -DEXPECTED_VERDICT=<line>
#       -P check-bench-baseline.cmake
#
# Baselines are per CPU model, keyed by gg-bench's machine string, so this script writes a
# baseline (schema 2) with one machine entry for MACHINE. MACHINE defaults to the machine of
# RUN_JSON, a run of the same gg-bench on this host. The entry gives engine-sample a ratio of
# RATIO, with tolerance 0.10, at blocks 32, 64 and 256. The script then runs a tiny gg-bench of
# reference and engine-sample against that baseline. It fails unless the exit code is exactly
# EXPECTED_CODE and stdout has the line EXPECTED_VERDICT, in which <machine> stands for the run's
# machine.
#
# CI's bench step relies on the exit code. 3 (a ratio regressed) becomes a warning; 0 is a pass,
# or no verdict for a CPU model the baseline doesn't have; anything else fails the job.

# First, so that `cmake -P` runs under the project's policies. Without it CMake 3.x leaves
# CMP0053 unset, and its old rules expand an @VAR@-shaped string as a variable.
cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS BENCH RUN_JSON BASELINE RATIO EXPECTED_CODE EXPECTED_VERDICT)
    if (NOT DEFINED ${var})
        message(FATAL_ERROR "check-bench-baseline.cmake: ${var} is not set")
    endif ()
endforeach ()

file(READ ${RUN_JSON} run)
string(JSON run_machine GET "${run}" machine)
if (NOT DEFINED MACHINE)
    set(MACHINE "${run_machine}")
endif ()

# MACHINE as a JSON string.
string(REPLACE "\\" "\\\\" machine_json "${MACHINE}")
string(REPLACE "\"" "\\\"" machine_json "${machine_json}")
set(entries "")
foreach (block IN ITEMS 32 64 256)
    list(APPEND entries
            "{\"case\": \"engine-sample\", \"blockSize\": ${block}, \"ratio\": ${RATIO}, \"tolerance\": 0.10}")
endforeach ()
list(JOIN entries ", " entries)
file(WRITE ${BASELINE} "{\"schemaVersion\": 2, \"leg\": \"test\", \"machines\": [{\"machine\": \"${machine_json}\", \"commit\": \"test\", \"runs\": 1, \"ratios\": [${entries}]}]}\n")

execute_process(
        COMMAND ${BENCH} --seconds 0.002 --rounds 1 --grains 50 --cases reference,engine-sample
        --baseline ${BASELINE}
        RESULT_VARIABLE code
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)

if (NOT code STREQUAL "${EXPECTED_CODE}")
    message(FATAL_ERROR "gg-bench exited with ${code}, not ${EXPECTED_CODE}\n"
            "stdout:\n${out}\nstderr:\n${err}")
endif ()
string(REPLACE "<machine>" "${run_machine}" verdict "${EXPECTED_VERDICT}")
string(FIND "${out}" "\n${verdict}\n" at)
if (at EQUAL -1)
    message(FATAL_ERROR "gg-bench printed no line \"${verdict}\"\nstdout:\n${out}")
endif ()
message(STATUS "gg-bench exited with ${code} and printed \"${verdict}\"")
