# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# cmake -DBENCH=<gg-bench> -DRUN_JSON=<a gg-bench --json report> -DBASELINE=<baseline to write>
#       -P check-bench-merge.cmake
#
# gg-bench --make-baseline --merge: writes BASELINE, a baseline for leg "test" with two entries,
# one for a CPU model that doesn't exist and one, stale, for RUN_JSON's machine, then makes the
# baseline again from RUN_JSON, merged into BASELINE and written over it. It fails unless gg-bench
# exits 0, the other model's entry is kept as it was, in first place, and this machine's entry
# is replaced by one made from RUN_JSON alone (commit test-run, 1 run).

# First, so that `cmake -P` runs under the project's policies.
cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS BENCH RUN_JSON BASELINE)
    if (NOT DEFINED ${var})
        message(FATAL_ERROR "check-bench-merge.cmake: ${var} is not set")
    endif ()
endforeach ()

file(READ ${RUN_JSON} run)
string(JSON machine GET "${run}" machine)
string(REPLACE "\\" "\\\\" machine_json "${machine}")
string(REPLACE "\"" "\\\"" machine_json "${machine_json}")
# RUN_JSON with its commit changed, so the replaced entry is recognisable.
string(JSON run SET "${run}" commit "\"test-run\"")
get_filename_component(dir ${BASELINE} DIRECTORY)
set(run_copy ${dir}/bench_merge_run.json)
file(WRITE ${run_copy} "${run}")

set(other "{\"machine\": \"no such CPU (test)\", \"commit\": \"old\", \"runs\": 3, \"ratios\": [{\"case\": \"engine-live\", \"blockSize\": 64, \"ratio\": 1.5, \"tolerance\": 0.2}]}")
set(stale "{\"machine\": \"${machine_json}\", \"commit\": \"old\", \"runs\": 2, \"ratios\": [{\"case\": \"engine-live\", \"blockSize\": 64, \"ratio\": 9.0, \"tolerance\": 0.1}]}")
file(WRITE ${BASELINE} "{\"schemaVersion\": 2, \"leg\": \"test\", \"machines\": [${other}, ${stale}]}\n")

execute_process(
        COMMAND ${BENCH} --make-baseline test ${BASELINE} --merge ${BASELINE} ${run_copy}
        RESULT_VARIABLE code
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
if (NOT code STREQUAL "0")
    message(FATAL_ERROR "gg-bench exited with ${code}\nstdout:\n${out}\nstderr:\n${err}")
endif ()

file(READ ${BASELINE} merged)
string(JSON count LENGTH "${merged}" machines)
if (NOT count EQUAL 2)
    message(FATAL_ERROR "the merged baseline has ${count} machines, not 2:\n${merged}")
endif ()
string(JSON kept GET "${merged}" machines 0)
string(JSON kept_equal EQUAL "${kept}" "${other}")
if (NOT kept_equal)
    message(FATAL_ERROR "the other model's entry wasn't kept as it was:\n${merged}")
endif ()
string(JSON replaced_machine GET "${merged}" machines 1 machine)
string(JSON replaced_commit GET "${merged}" machines 1 commit)
string(JSON replaced_runs GET "${merged}" machines 1 runs)
if (NOT replaced_machine STREQUAL "${machine}" OR NOT replaced_commit STREQUAL "test-run" OR
        NOT replaced_runs EQUAL 1)
    message(FATAL_ERROR "this machine's entry wasn't replaced by the run's:\n${merged}")
endif ()
foreach (line IN ITEMS "  no such CPU (test): 1 ratios from 3 runs (kept)"
        "  ${machine}: ")
    string(FIND "${out}" "${line}" at)
    if (at EQUAL -1)
        message(FATAL_ERROR "gg-bench printed no \"${line}\"\nstdout:\n${out}")
    endif ()
endforeach ()
string(FIND "${out}" "run (replaced)" at)
if (at EQUAL -1)
    message(FATAL_ERROR "gg-bench didn't report the replaced entry\nstdout:\n${out}")
endif ()
message(STATUS "merged: ${merged}")
