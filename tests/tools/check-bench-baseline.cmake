# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# cmake -DBENCH=<gg-bench> -DBASELINE=<baseline.json> -DEXPECTED_CODE=<n> -DEXPECTED_VERDICT=<line>
#       -P check-bench-baseline.cmake
#
# Runs a tiny gg-bench of reference and engine-sample against BASELINE, then fails unless the
# exit code is exactly EXPECTED_CODE and stdout contains the line EXPECTED_VERDICT. CI's bench step
# relies on the exit code: 3 (a ratio regressed) becomes a warning, and any other non-zero code
# fails the job.

foreach (var IN ITEMS BENCH BASELINE EXPECTED_CODE EXPECTED_VERDICT)
    if (NOT DEFINED ${var})
        message(FATAL_ERROR "check-bench-baseline.cmake: ${var} is not set")
    endif ()
endforeach ()

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
string(FIND "${out}" "\n${EXPECTED_VERDICT}\n" at)
if (at EQUAL -1)
    message(FATAL_ERROR "gg-bench printed no line \"${EXPECTED_VERDICT}\"\nstdout:\n${out}")
endif ()
message(STATUS "gg-bench exited with ${code} and printed \"${EXPECTED_VERDICT}\"")
