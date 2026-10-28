# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-tests: the Catch2 v3 suite. Each Catch2 tag becomes a CTest label, so
# `ctest --preset dev -L <tag>` runs one area.

add_executable(gg-tests
        smoke_tests.cpp
        core/parammap_tests.cpp
        core/patch_tests.cpp
        core/processor_tests.cpp
        engine/cloud_tests.cpp
        engine/denormal_tests.cpp
        engine/determinism_tests.cpp
        engine/engine_tests.cpp
        engine/fp_contract_fast.cpp
        engine/fp_contract_tests.cpp
        engine/interpolate_tests.cpp
        engine/kernel_tests.cpp
        engine/live_tests.cpp
        engine/mathdet_tests.cpp
        engine/pool_tests.cpp
        engine/rng_tests.cpp
        engine/rt_tests.cpp
        engine/sample_buffer_tests.cpp
        engine/window_tests.cpp
        metrics/metrics_tests.cpp
        tools/bench_kernel_tests.cpp
        tools/bench_tests.cpp
        tools/render_spec_tests.cpp
        tools/signals_tests.cpp
        ui/splat_tests.cpp
        support/AllocTripwire.cpp
        )
target_include_directories(gg-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# fp_contract_fast.cpp includes the engine's inline math with the flags of an includer that lets
# Clang contract a*b + c into an FMA: Clang's default -ffp-contract=on, placed after the project's
# -ffp-contract=off so it wins. (Under -ffp-contract=fast Clang disregards the fp contract pragma,
# so no header can guard against that.) -O2 inlines the header functions all the way down, so the
# file runs its own code and leaves no out-of-line copy for the linker to pick in place of the
# project-flag one. GCC's guard is empty and relies on the project-wide flag, so under GCC the
# file keeps the project flags.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT MATCHES "MSVC")
        set(gg_fp_contract_includer /clang:-O2 /clang:-ffp-contract=on)
    else ()
        set(gg_fp_contract_includer -O2 -ffp-contract=on)
    endif ()
    set_source_files_properties(engine/fp_contract_fast.cpp PROPERTIES
            COMPILE_OPTIONS "${gg_fp_contract_includer}")
endif ()
# Each library is listed before the libraries it links itself. Listing one after a library it
# links would make CMake repeat it on the link line, which Apple's linker warns about ("ignoring
# duplicate libraries"). So the plugin layers, when they're built, come first: they link the
# engine and, with the editor, gg::ui-core. The editor layer is an object library, whose own
# libraries CMake would put after everything listed here, so they're listed too: the plugin
# core and gg::ui. Then gg::render, gg::ui-core, gg::bench and gg::core, which link the engine
# (and gg::render the metrics). gg::bench-kernels follows gg::bench, which links it: left out,
# CMake would repeat the engine after it.
set(gg_tests_libraries gg::render gg::ui-core gg::bench gg::bench-kernels gg::core gg::engine
        gg::audiometrics)
if (GG_BUILD_PLUGINS AND GG_BUILD_EDITOR)
    list(PREPEND gg_tests_libraries gg::plugin-impl gg-plugin-core gg::ui)
elseif (GG_BUILD_PLUGINS)
    list(PREPEND gg_tests_libraries gg::plugin-impl)
endif ()
target_link_libraries(gg-tests PRIVATE ${gg_tests_libraries} gg::compiler-options
        Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
# Scratch files (WAV/PNG round trips) go under the build tree, never a shared temp dir.
target_compile_definitions(gg-tests PRIVATE GG_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output"
        GG_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
# 0 or 1, so tests can use #if GG_USE_RTSAN for checks that only hold in the RTSan build.
target_compile_definitions(gg-tests PRIVATE GG_USE_RTSAN=$<BOOL:${GG_USE_RTSAN}>)
# 0 or 1: the plugin tests that need the JUCE editor, and the one that checks there is none.
target_compile_definitions(gg-tests PRIVATE GG_BUILD_EDITOR=$<BOOL:${GG_BUILD_EDITOR}>)

# The plugin tests drive gg::plugin-impl (the plugin core, or the core plus the editor layer)
# through its factory, not the built .clap. They run in every preset that builds the plugins,
# the sanitizer presets included. They also reach the libraries' private headers (plugin/src) to
# inspect a plugin instance. gg::plugin-impl is linked above, first.
if (GG_BUILD_PLUGINS)
    target_sources(gg-tests PRIVATE
            plugin/factory_tests.cpp
            plugin/identity_tests.cpp
            plugin/params_tests.cpp
            plugin/process_tests.cpp
            plugin/rt_tests.cpp
            )
    target_include_directories(gg-tests PRIVATE ${CMAKE_SOURCE_DIR}/plugin/src)
endif ()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/libs/catch2/extras)
include(Catch)
catch_discover_tests(gg-tests ADD_TAGS_AS_LABELS)

# gg-bench's command line, label `bench`. The bench tests check the timings' shape, never their
# values, so they run beside the others.
# - bench_cli: a tiny run of a kernel, the reference and an engine case must exit 0 and write its
#   JSON, ratios included;
# - bench_make_baseline: a baseline made from that JSON must be written, exit 0;
# - bench_baseline_pass, bench_baseline_regressed and bench_baseline_other_machine: a tiny run
#   checked against a baseline that check-bench-baseline.cmake writes when the test runs.
#   Baselines are per CPU model, so the script takes this host's machine string from bench_cli's
#   JSON. The pass and regressed baselines give this machine limits no ratio can miss (1e9) or
#   meet (1e-9); the third has an entry only for a CPU model that doesn't exist. The script
#   requires an exact exit code and verdict line:
#   - pass: exit 0, "verdict: 0 of 3 ratios regressed (...)";
#   - regressed: exit 3, "verdict: 3 of 3 ...";
#   - other machine: exit 0, "no baseline for <this machine>; no verdict".
#   CI's bench step relies on these codes: it reports exit 3 without failing (ruling 3).
set(bench_cases hermite-simd,reference,engine-sample)
add_test(NAME bench_cli
        COMMAND gg-bench --seconds 0.002 --rounds 2 --grains 50 --cases ${bench_cases}
        --json ${CMAKE_BINARY_DIR}/bench_cli.json)
add_test(NAME bench_make_baseline
        COMMAND gg-bench --make-baseline test ${CMAKE_BINARY_DIR}/bench_made_baseline.json
        ${CMAKE_BINARY_DIR}/bench_cli.json ${CMAKE_BINARY_DIR}/bench_cli.json)
set_tests_properties(bench_cli PROPERTIES LABELS bench FIXTURES_SETUP bench_json)
set_tests_properties(bench_make_baseline PROPERTIES LABELS bench FIXTURES_REQUIRED bench_json)
set(verdict_line "ratios regressed (limit: the baseline's ratio x (1 + its tolerance))")
set(bench_baseline_pass_args -DRATIO=1e9 -DEXPECTED_CODE=0
        "-DEXPECTED_VERDICT=verdict: 0 of 3 ${verdict_line}")
set(bench_baseline_regressed_args -DRATIO=1e-9 -DEXPECTED_CODE=3
        "-DEXPECTED_VERDICT=verdict: 3 of 3 ${verdict_line}")
set(bench_baseline_other_machine_args -DRATIO=1e-9 "-DMACHINE=no such CPU (test)"
        -DEXPECTED_CODE=0 "-DEXPECTED_VERDICT=no baseline for <machine>$<SEMICOLON> no verdict")
foreach (test IN ITEMS bench_baseline_pass bench_baseline_regressed bench_baseline_other_machine)
    add_test(NAME ${test}
            COMMAND ${CMAKE_COMMAND} -DBENCH=$<TARGET_FILE:gg-bench>
            -DRUN_JSON=${CMAKE_BINARY_DIR}/bench_cli.json
            -DBASELINE=${CMAKE_CURRENT_BINARY_DIR}/${test}.json ${${test}_args}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check-bench-baseline.cmake)
    set_tests_properties(${test} PROPERTIES LABELS bench FIXTURES_REQUIRED bench_json)
endforeach ()

# RTSan's negative control (rtsan build only), labels `rt` and `rtsan`: a program that allocates
# inside a nonblocking function must make RealtimeSanitizer report it. The test passes on the
# report itself, not on a failing exit status, which a crash or a missing binary would also give.
# abort_on_error=0 makes RTSan exit after its report instead of aborting (its default on macOS),
# because CTest fails a test that dies on a signal whatever its output.
if (GG_USE_RTSAN)
    add_executable(gg-rtsan-negative-control rtsan_negative_control.cpp)
    target_link_libraries(gg-rtsan-negative-control PRIVATE gg::compiler-options sst-cpputils)
    add_test(NAME rtsan_negative_control COMMAND gg-rtsan-negative-control)
    set_tests_properties(rtsan_negative_control PROPERTIES
            PASS_REGULAR_EXPRESSION "ERROR: RealtimeSanitizer"
            ENVIRONMENT "RTSAN_OPTIONS=abort_on_error=0"
            LABELS "rt;rtsan")
endif ()

# Golden renders (INTENT §19), label `golden`: gg-render renders each case and compares its
# metrics, within tolerances, and its grain-event log, exactly per line (CR and final newline
# ignored), with the committed files in golden/expected/. The event-log comparison is the
# cross-platform determinism gate. The WAV and PNG of each run go to <build>/golden/ for listening
# and inspection. Regenerate the expected files with scripts/update-goldens.sh.
#
# Every golden/cases/*.json is a case, the same glob scripts/update-goldens.sh renders, so a new
# case can't go ungated. CONFIGURE_DEPENDS re-runs CMake when the set of files changes.
file(GLOB GG_GOLDEN_CASE_SPECS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/golden/cases/*.json)
if (NOT GG_GOLDEN_CASE_SPECS)
    message(FATAL_ERROR "No golden cases in ${CMAKE_CURRENT_SOURCE_DIR}/golden/cases")
endif ()
foreach (spec IN LISTS GG_GOLDEN_CASE_SPECS)
    get_filename_component(case ${spec} NAME_WLE)
    add_test(NAME golden_${case}
            COMMAND gg-render ${spec}
            -o ${CMAKE_BINARY_DIR}/golden/${case}.wav
            --spectrogram ${CMAKE_BINARY_DIR}/golden/${case}.png
            --compare ${CMAKE_CURRENT_SOURCE_DIR}/golden/expected/${case}.metrics.json
            --compare-events ${CMAKE_CURRENT_SOURCE_DIR}/golden/expected/${case}.events.jsonl)
    set_tests_properties(golden_${case} PROPERTIES LABELS golden)
endforeach ()

# The golden gate must be able to fail, label `golden`. Each tamper test compares a fresh render of
# sparse-sine with a copy of its golden file that has one value changed, and passes only on the
# specific failure gg-render then prints:
# - golden_tamper_events: the last digit of grain 4's onset (line 5 of the event log) changed;
# - golden_tamper_metrics: the first RMS-envelope frame louder than -40 dB set to 0 dB.
# The copies are written here, at configure time, and CMake re-runs when the goldens change.
set(tamper_case sparse-sine)
set(tamper_dir ${CMAKE_CURRENT_BINARY_DIR}/golden-tamper)
set(golden_events ${CMAKE_CURRENT_SOURCE_DIR}/golden/expected/${tamper_case}.events.jsonl)
set(golden_metrics ${CMAKE_CURRENT_SOURCE_DIR}/golden/expected/${tamper_case}.metrics.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${golden_events} ${golden_metrics})

file(STRINGS ${golden_events} event_lines)
list(GET event_lines 4 event_line)
if (NOT event_line MATCHES "\"onset\":([0-9]*)([0-9]),")
    message(FATAL_ERROR "No onset in line 5 of ${golden_events}")
endif ()
set(onset_head "${CMAKE_MATCH_1}")
set(onset_last "${CMAKE_MATCH_2}")
math(EXPR tampered_last "${onset_last} ^ 1") # 0 <-> 1, 2 <-> 3, ...: exactly one digit changes
string(REPLACE "\"onset\":${onset_head}${onset_last},"
        "\"onset\":${onset_head}${tampered_last}," event_line "${event_line}")
list(REMOVE_AT event_lines 4)
list(INSERT event_lines 4 "${event_line}")
list(JOIN event_lines "\n" tampered_events)
file(WRITE ${tamper_dir}/${tamper_case}.events.jsonl "${tampered_events}\n")

file(READ ${golden_metrics} tampered_metrics)
string(JSON envelope_frames LENGTH "${tampered_metrics}" rmsEnvelopeDb)
math(EXPR last_frame "${envelope_frames} - 1")
set(tamper_frame -1)
foreach (frame RANGE ${last_frame})
    string(JSON level GET "${tampered_metrics}" rmsEnvelopeDb ${frame})
    if (level GREATER -40)
        set(tamper_frame ${frame})
        break()
    endif ()
endforeach ()
if (tamper_frame LESS 0)
    message(FATAL_ERROR "No RMS-envelope frame louder than -40 dB in ${golden_metrics}")
endif ()
string(JSON tampered_metrics SET "${tampered_metrics}" rmsEnvelopeDb ${tamper_frame} 0.0)
file(WRITE ${tamper_dir}/${tamper_case}.metrics.json "${tampered_metrics}\n")

add_test(NAME golden_tamper_events
        COMMAND gg-render ${CMAKE_CURRENT_SOURCE_DIR}/golden/cases/${tamper_case}.json
        --compare-events ${tamper_dir}/${tamper_case}.events.jsonl)
add_test(NAME golden_tamper_metrics
        COMMAND gg-render ${CMAKE_CURRENT_SOURCE_DIR}/golden/cases/${tamper_case}.json
        --compare ${tamper_dir}/${tamper_case}.metrics.json)
set_tests_properties(golden_tamper_events PROPERTIES LABELS golden
        PASS_REGULAR_EXPRESSION "FAIL: event log differs from .* at line 5 ")
set_tests_properties(golden_tamper_metrics PROPERTIES LABELS golden
        PASS_REGULAR_EXPRESSION
        "rmsEnvelope: 1 of [0-9]+ compared frames differ by more than 0.5 dB; worst is frame ${tamper_frame} ")

# Code signatures (macOS), label `plugin`: every product bundle in the build tree must pass
# codesign's strict verification, as Ableton Live's plugin scanner requires. A bundle whose only
# signature is the one the linker puts on its executable fails it ("code has no resources but
# signature indicates they must be present"), and Live then doesn't list the plugin.
# clients/clap-first signs each bundle after it is built. The bundles exist only with the editor
# and the formats (GG_BUILD_EDITOR), and so do these tests and the identity tests below.
if (APPLE AND GG_BUILD_PLUGINS AND GG_BUILD_EDITOR)
    foreach (format IN ITEMS clap vst3 auv2 standalone standalone_fx)
        add_test(NAME codesign_${format}
                COMMAND ${GG_CODESIGN} --verify --deep --strict --verbose=2
                $<TARGET_BUNDLE_DIR:gg_clapfirst_${format}>)
        set_tests_properties(codesign_${format} PROPERTIES LABELS plugin)
    endforeach ()

    # Identities (INTENT §18), label `plugin`: what the built bundles declare, read back from them.
    # - Each standalone app's bundle ID. clap-wrapper would give both the instrument's; the FX's
    #   is set in clients/clap-first (ruling T8-a). macOS keys app preferences and privacy
    #   permissions by it.
    # - The AU's AudioComponents: exactly the instrument, the FX and the FX's extra aumf identity.
    # - The VST3 class IDs, which clap-wrapper derives from the CLAP IDs. They must not change with
    #   a clap-wrapper update: run this before accepting any bump.
    find_program(GG_PLUTIL plutil REQUIRED)
    set(check_identity ${CMAKE_CURRENT_SOURCE_DIR}/plugin/check-bundle-identity.cmake)
    add_test(NAME identity_standalone
            COMMAND ${CMAKE_COMMAND} -DPLUTIL=${GG_PLUTIL} -DCHECK=bundle-id
            -DBUNDLE=$<TARGET_BUNDLE_DIR:gg_clapfirst_standalone>
            -DEXPECTED=org.sethbc.giga-grains.standalone -P ${check_identity})
    add_test(NAME identity_standalone_fx
            COMMAND ${CMAKE_COMMAND} -DPLUTIL=${GG_PLUTIL} -DCHECK=bundle-id
            -DBUNDLE=$<TARGET_BUNDLE_DIR:gg_clapfirst_standalone_fx>
            -DEXPECTED=org.sethbc.giga-grains-fx.standalone -P ${check_identity})
    add_test(NAME identity_auv2
            COMMAND ${CMAKE_COMMAND} -DPLUTIL=${GG_PLUTIL} -DCHECK=au-components
            -DBUNDLE=$<TARGET_BUNDLE_DIR:gg_clapfirst_auv2>
            -DEXPECTED=aumu/GGin/Sbcx,aufx/GGfx/Sbcx,aumf/GGfx/Sbcx -P ${check_identity})

    add_executable(gg-vst3-class-ids plugin/vst3_class_ids.cpp)
    target_link_libraries(gg-vst3-class-ids PRIVATE gg::compiler-options "-framework CoreFoundation")
    add_dependencies(gg-tests gg-vst3-class-ids)
    add_test(NAME identity_vst3
            COMMAND gg-vst3-class-ids $<TARGET_BUNDLE_DIR:gg_clapfirst_vst3>
            $<TARGET_FILE:gg_clapfirst_vst3>
            "Giga Grains=56E869BFEDD75019BD0C3105FA80B835"
            "Giga Grains FX=DBAF5C12EA095FEFB3A5CBBBB254442A")
    set_tests_properties(identity_standalone identity_standalone_fx identity_auv2 identity_vst3
            PROPERTIES LABELS plugin)
endif ()
