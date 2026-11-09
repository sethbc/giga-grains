# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# render_tuning_save_state (M3 tuning spec §5.5): gg-render --save-state of a spec with an inline
# tuning writes a state with a TUNE chunk whose names are empty, and a render that restores the
# state (spec "state") takes the tuning from it. Run as
#   cmake -DGG_RENDER=<gg-render> -DFIXTURES=<tests/fixtures/render> -DWORK=<scratch dir>
#         -P this-file

cmake_minimum_required(VERSION 3.22)

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(run_render expected_status)
    execute_process(COMMAND "${GG_RENDER}" ${ARGN}
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if (NOT status EQUAL expected_status)
        message(FATAL_ERROR "gg-render ${ARGN}: exit ${status}, expected ${expected_status}\n${out}\n${err}")
    endif ()
    set(RENDER_ERR "${err}" PARENT_SCOPE)
endfunction()

# The state carries the TUNE chunk, with the scale text and an empty name (TUNE's payload:
# version 1, then each of scl, kbm, sclName and kbmName as a length and its bytes).
run_render(0 "${FIXTURES}/render-tuning-save-state.json" -o "${WORK}/a.wav"
        --save-state "${WORK}/a.ggst")
file(READ "${WORK}/a.ggst" hex HEX)
string(HEX "TUNE" tune_tag)
string(FIND "${hex}" "${tune_tag}" tune_at)
if (tune_at EQUAL -1)
    message(FATAL_ERROR "the saved state has no TUNE chunk")
endif ()
string(HEX "19-EDO" description)
string(FIND "${hex}" "${description}" description_at)
if (description_at EQUAL -1)
    message(FATAL_ERROR "the TUNE chunk lacks the scale's text")
endif ()
# The chunk is tag(4), size(8, little-endian), payload; the payload ends with kbm's length, the
# scl name's and the kbm name's, three zero words, when the kbm and both names are empty.
string(SUBSTRING "${hex}" ${tune_at} -1 from_tune)
string(SUBSTRING "${from_tune}" 8 16 size_hex)
string(REGEX MATCHALL ".." size_bytes "${size_hex}")
list(GET size_bytes 0 b0)
list(GET size_bytes 1 b1)
math(EXPR payload_size "0x${b1} * 256 + 0x${b0}")
math(EXPR names_at "24 + ${payload_size} * 2 - 24")
string(SUBSTRING "${from_tune}" ${names_at} 24 empty_names)
if (NOT empty_names STREQUAL "000000000000000000000000")
    message(FATAL_ERROR "TUNE's kbm and the two names should be empty, got ${empty_names}")
endif ()

# A render that restores that state has the same tuning, so the same audio as the one that made it.
file(WRITE "${WORK}/restore.json" [[{
  "mode": "instrument", "sampleRate": 48000, "durationSeconds": 0.25, "blockSize": 128, "seed": 1,
  "sample": {"kind": "sine", "frequencyHz": 220, "seconds": 0.5, "amplitude": 0.5},
  "state": "a.ggst",
  "notes": [{"time": 0.02, "key": 62, "duration": 0.15}]
}]])
run_render(0 "${WORK}/restore.json" -o "${WORK}/b.wav")
file(SHA256 "${WORK}/a.wav" sha_a)
file(SHA256 "${WORK}/b.wav" sha_b)
if (NOT sha_a STREQUAL sha_b)
    message(FATAL_ERROR "the restored tuning renders differently: ${sha_a} vs ${sha_b}")
endif ()
