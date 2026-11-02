# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# cmake -DPROGRAM=<gg-render> -DPRESET=<in.ggpreset> -DSPEC=<an FX render spec> -DWORK=<dir>
#       -P check-mutate-preset.cmake
#
# gg-render --mutate-preset (the listening pack's Mutate ladder, spec §11): mutates PRESET twice
# with the same amount and counter and fails unless both runs exit 0 and write the same bytes,
# which differ from PRESET's; a third run at another counter must write other bytes; and SPEC
# must render through the mutated preset (--preset), so the file it writes loads.

# First, so that `cmake -P` runs under the project's policies.
cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS PROGRAM PRESET SPEC WORK)
    if (NOT DEFINED ${var})
        message(FATAL_ERROR "check-mutate-preset.cmake: ${var} is not set")
    endif ()
endforeach ()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(mutate out counter)
    execute_process(
            COMMAND ${PROGRAM} --mutate-preset ${PRESET} -o ${out} --amount 0.1 --counter ${counter}
            RESULT_VARIABLE code
            OUTPUT_VARIABLE stdout
            ERROR_VARIABLE stderr)
    if (NOT code EQUAL 0)
        message(FATAL_ERROR "--mutate-preset exited ${code}\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif ()
endfunction()

mutate(${WORK}/a.ggpreset 1)
mutate(${WORK}/b.ggpreset 1)
mutate(${WORK}/c.ggpreset 2)
file(SHA256 ${PRESET} in_hash)
file(SHA256 ${WORK}/a.ggpreset a_hash)
file(SHA256 ${WORK}/b.ggpreset b_hash)
file(SHA256 ${WORK}/c.ggpreset c_hash)
if (NOT a_hash STREQUAL b_hash)
    message(FATAL_ERROR "the same amount and counter wrote different bytes: ${a_hash}, ${b_hash}")
endif ()
if (a_hash STREQUAL in_hash)
    message(FATAL_ERROR "--amount 0.1 left the preset's bytes unchanged")
endif ()
if (a_hash STREQUAL c_hash)
    message(FATAL_ERROR "counters 1 and 2 wrote the same bytes")
endif ()

execute_process(
        COMMAND ${PROGRAM} ${SPEC} --preset ${WORK}/a.ggpreset -o ${WORK}/render.wav
        RESULT_VARIABLE code
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
if (NOT code EQUAL 0)
    message(FATAL_ERROR "rendering through the mutated preset exited ${code}\n${stdout}\n${stderr}")
endif ()
message(STATUS "--mutate-preset is deterministic (${a_hash}) and its file renders")
