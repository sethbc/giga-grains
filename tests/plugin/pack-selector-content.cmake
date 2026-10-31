# Packs the two test preset sources into a scratch content root's Presets folder (the VST3
# preset selector test's content; tests/CMakeLists.txt). Inputs: OUT, GG_RENDER, SOURCES.
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
foreach (name IN ITEMS fx-test inst-test)
    execute_process(COMMAND "${GG_RENDER}" --pack-preset "${SOURCES}/${name}.json"
            -o "${OUT}/${name}.ggpreset" RESULT_VARIABLE code ERROR_VARIABLE error)
    if (NOT code EQUAL 0)
        message(FATAL_ERROR "packing ${name} failed (${code}): ${error}")
    endif ()
endforeach ()
