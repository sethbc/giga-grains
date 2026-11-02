# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# cmake -DBUILD=<build tree> -DCONFIG=<config> -DPRESETS=<build>/content/Presets
#       -DOUT=<golden-media>/presets -P presets-built.cmake
#
# The fixture presets_built (tests/CMakeLists.txt; M2 sub-project 5's spec §10): builds gg-presets,
# which packs the 48 factory sources of content/presets/ into PRESETS/<Product>/NN Name.ggpreset,
# then copies both product folders into OUT, replacing what an earlier run left there, so a preset
# golden's "preset": "presets/<Product>/NN Name.ggpreset" resolves through --media-dir
# <golden-media>. It fails if the build fails or a product folder holds no preset.

# First, so that `cmake -P` runs under the project's policies.
cmake_minimum_required(VERSION 3.22)

foreach (var IN ITEMS BUILD CONFIG PRESETS OUT)
    if (NOT DEFINED ${var})
        message(FATAL_ERROR "presets-built.cmake: ${var} is not set")
    endif ()
endforeach ()

execute_process(
        COMMAND ${CMAKE_COMMAND} --build ${BUILD} --config ${CONFIG} --target gg-presets
        RESULT_VARIABLE code)
if (NOT code EQUAL 0)
    message(FATAL_ERROR "building gg-presets failed (exit ${code})")
endif ()

file(REMOVE_RECURSE "${OUT}")
foreach (product IN ITEMS "Giga Grains" "Giga Grains FX")
    file(GLOB presets "${PRESETS}/${product}/*.ggpreset")
    if (NOT presets)
        message(FATAL_ERROR "gg-presets packed no preset into ${PRESETS}/${product}")
    endif ()
    file(MAKE_DIRECTORY "${OUT}/${product}")
    file(COPY ${presets} DESTINATION "${OUT}/${product}")
    list(LENGTH presets count)
    message(STATUS "${count} presets of ${product} in ${OUT}/${product}")
endforeach ()
