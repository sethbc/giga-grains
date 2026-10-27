# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-tests: the Catch2 v3 suite. Each Catch2 tag becomes a CTest label, so
# `ctest --preset dev -L <tag>` runs one area.

add_executable(gg-tests
        smoke_tests.cpp
        engine/denormal_tests.cpp
        engine/determinism_tests.cpp
        engine/engine_tests.cpp
        engine/interpolate_tests.cpp
        engine/mathdet_tests.cpp
        engine/pool_tests.cpp
        engine/rng_tests.cpp
        engine/rt_tests.cpp
        engine/sample_buffer_tests.cpp
        engine/stream_tests.cpp
        engine/window_tests.cpp
        metrics/metrics_tests.cpp
        tools/bench_kernel_tests.cpp
        tools/render_spec_tests.cpp
        tools/signals_tests.cpp
        support/AllocTripwire.cpp
        )
target_include_directories(gg-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# gg::render comes first: it links the engine and the metrics itself, and listing them before it
# would make CMake repeat them on the link line, which Apple's linker warns about.
target_link_libraries(gg-tests PRIVATE gg::render gg::bench-kernels gg::engine gg::audiometrics
        gg::compiler-options Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
# Scratch files (WAV/PNG round trips) go under the build tree, never a shared temp dir.
target_compile_definitions(gg-tests PRIVATE GG_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output"
        GG_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
# 0 or 1, so tests can use #if GG_USE_RTSAN for checks that only hold in the RTSan build.
target_compile_definitions(gg-tests PRIVATE GG_USE_RTSAN=$<BOOL:${GG_USE_RTSAN}>)

# The plugin tests drive gg-plugin-impl through its factory, not the built .clap. They also
# reach the library's private headers (plugin/src) to inspect a plugin instance.
if (GG_BUILD_PLUGINS)
    target_sources(gg-tests PRIVATE
            plugin/factory_tests.cpp
            plugin/process_tests.cpp
            )
    target_include_directories(gg-tests PRIVATE ${CMAKE_SOURCE_DIR}/plugin/src)
    target_link_libraries(gg-tests PRIVATE gg::plugin-impl)
endif ()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/libs/catch2/extras)
include(Catch)
catch_discover_tests(gg-tests ADD_TAGS_AS_LABELS)

# Golden renders (INTENT §19), label `golden`: gg-render renders each case and compares its
# metrics, within tolerances, and its grain-event log, byte for byte, with the committed files in
# golden/expected/. The event-log comparison is the cross-platform determinism gate. The WAV and
# PNG of each run go to <build>/golden/ for listening and inspection. Regenerate the expected files
# with scripts/update-goldens.sh.
set(GG_GOLDEN_CASES sparse-sine sparse-sine-44k1 dense-noise sweep-stretch)
foreach (case IN LISTS GG_GOLDEN_CASES)
    add_test(NAME golden_${case}
            COMMAND gg-render ${CMAKE_CURRENT_SOURCE_DIR}/golden/cases/${case}.json
            -o ${CMAKE_BINARY_DIR}/golden/${case}.wav
            --spectrogram ${CMAKE_BINARY_DIR}/golden/${case}.png
            --compare ${CMAKE_CURRENT_SOURCE_DIR}/golden/expected/${case}.metrics.json
            --compare-events ${CMAKE_CURRENT_SOURCE_DIR}/golden/expected/${case}.events.jsonl)
    set_tests_properties(golden_${case} PROPERTIES LABELS golden)
endforeach ()
