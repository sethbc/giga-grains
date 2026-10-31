# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# snap_cli: runs gg-snap into a scratch directory and checks its files: 2 products x 2 themes x
# 4 views x 2 sizes = 32 PNGs, each named <product>-<view>-<theme>-<w>x<h>.png and non-empty.
#   cmake -DTOOL=<gg-snap> -DOUT=<dir> -P check-snap.cmake

file(REMOVE_RECURSE "${OUT}")
execute_process(COMMAND "${TOOL}" "${OUT}" RESULT_VARIABLE status
        OUTPUT_VARIABLE out ERROR_VARIABLE err)
if (NOT status EQUAL 0)
    message(FATAL_ERROR "gg-snap exited with ${status}:\n${out}${err}")
endif ()

set(expected "")
foreach (product IN ITEMS fx inst)
    foreach (view IN ITEMS perform edit mod fx)
        foreach (theme IN ITEMS dark light)
            foreach (size IN ITEMS 960x600 640x360)
                list(APPEND expected "${product}-${view}-${theme}-${size}.png")
            endforeach ()
        endforeach ()
    endforeach ()
endforeach ()

file(GLOB found RELATIVE "${OUT}" "${OUT}/*")
list(LENGTH found count)
if (NOT count EQUAL 32)
    message(FATAL_ERROR "expected 32 files in ${OUT}, found ${count}: ${found}")
endif ()
foreach (name IN LISTS expected)
    if (NOT EXISTS "${OUT}/${name}")
        message(FATAL_ERROR "missing ${name}")
    endif ()
    file(SIZE "${OUT}/${name}" bytes)
    file(READ "${OUT}/${name}" magic LIMIT 4 HEX)
    if (bytes LESS 1000 OR NOT magic STREQUAL "89504e47")
        message(FATAL_ERROR "${name} is not a PNG of any size (${bytes} bytes)")
    endif ()
endforeach ()
message(STATUS "gg-snap wrote ${count} PNGs")
