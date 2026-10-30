# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# The render_state_* CLI tests (M2 sub-project 2's spec §6.10): gg-render --save-state, then a spec
# with "state", and the exit statuses of a state that names a sample it can't find. Run as
#   cmake -DGG_RENDER=<gg-render> -DWORK=<scratch dir> -DMEDIA=<golden media dir> -P this-file
# MEDIA holds the fixtures `gg-render --write-media` writes.

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(run_render expected_status)
    execute_process(COMMAND "${GG_RENDER}" ${ARGN}
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if (NOT status EQUAL expected_status)
        message(FATAL_ERROR "gg-render ${ARGN}: exit ${status}, expected ${expected_status}\n${out}\n${err}")
    endif ()
    set(RENDER_OUT "${out}" PARENT_SCOPE)
    set(RENDER_ERR "${err}" PARENT_SCOPE)
endfunction()

# 1. A frozen ring saved at the end of a render.
file(WRITE "${WORK}/save.json" [[{
  "mode": "fx", "sampleRate": 48000, "durationSeconds": 2.0, "blockSize": 128, "seed": 1,
  "input": {"kind": "sine", "frequencyHz": 220, "seconds": 2.0, "amplitude": 0.5},
  "channels": 1,
  "params": {"A Ring Length": 3.0},
  "automation": [{"time": 1.0, "param": "A Freeze", "value": 1}],
  "operations": [{"time": 1.5, "layer": "A", "op": "splice", "behind": 0.25}]
}]])
run_render(0 "${WORK}/save.json" --save-state "${WORK}/session.ggst")
if (NOT EXISTS "${WORK}/session.ggst")
    message(FATAL_ERROR "--save-state wrote no file")
endif ()

# 2. A spec that restores it (the state's path is relative to the spec's directory).
file(WRITE "${WORK}/restore.json" [[{
  "mode": "fx", "sampleRate": 48000, "durationSeconds": 0.5, "blockSize": 128, "seed": 1,
  "input": {"kind": "noise", "seconds": 0.5, "amplitude": 0.0},
  "channels": 1,
  "state": "session.ggst",
  "params": {"Mix": 1.0}
}]])
run_render(0 "${WORK}/restore.json" -o "${WORK}/restore.wav")
if (NOT EXISTS "${WORK}/restore.wav")
    message(FATAL_ERROR "the restoring render wrote no audio")
endif ()

# 3. A state that names samples the render can't find is bad input (exit 2); --media-dir finds them.
file(MAKE_DIRECTORY "${WORK}/lost")
file(COPY "${MEDIA}/four-samples.ggst" DESTINATION "${WORK}/lost")
file(WRITE "${WORK}/lost/four.json" [[{
  "mode": "instrument", "sampleRate": 48000, "durationSeconds": 0.25, "blockSize": 128, "seed": 1,
  "state": "four-samples.ggst"
}]])
run_render(2 "${WORK}/lost/four.json")
if (NOT RENDER_ERR MATCHES "tone-44k1.wav")
    message(FATAL_ERROR "the error doesn't name the missing sample: ${RENDER_ERR}")
endif ()
run_render(0 "${WORK}/lost/four.json" --media-dir "${MEDIA}")
run_render(0 "${WORK}/lost/four.json" --content-dir "${MEDIA}")

# 4. --save-state of a sample-mode spec, and --write-media with --save-state, are refused.
file(WRITE "${WORK}/sample.json" [[{
  "sampleRate": 48000, "durationSeconds": 0.5, "blockSize": 128, "seed": 1,
  "source": {"kind": "sine", "frequencyHz": 220, "seconds": 1.0, "amplitude": 0.5},
  "stream": {"grainRateHz": 8, "grainDurationMs": 60, "position": 0.5, "positionJitter": 0.0,
             "pitchSemitones": 0, "gain": 0.5}
}]])
run_render(2 "${WORK}/sample.json" --save-state "${WORK}/nope.ggst")
run_render(2 --write-media "${WORK}/media" --save-state "${WORK}/nope.ggst")
