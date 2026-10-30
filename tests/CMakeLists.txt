# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# gg-tests: the Catch2 v3 suite. Each Catch2 tag becomes a CTest label, so
# `ctest --preset dev -L <tag>` runs one area.

add_executable(gg-tests
        smoke_tests.cpp
        core/diffuser_tests.cpp
        core/exchange_tests.cpp
        core/layers_tests.cpp
        core/mod_matrix_tests.cpp
        core/mod_processor_tests.cpp
        core/mod_sources_tests.cpp
        core/note_tests.cpp
        core/parammap_tests.cpp
        core/patch_tests.cpp
        core/processor_tests.cpp
        core/publish_tests.cpp
        core/session_state_tests.cpp
        core/settings_tests.cpp
        core/space_processor_tests.cpp
        core/space_reverb_tests.cpp
        core/threads_tests.cpp
        engine/cloud_tests.cpp
        engine/denormal_tests.cpp
        engine/determinism_tests.cpp
        engine/engine_reference_tests.cpp
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
        engine/task_runner_tests.cpp
        engine/window_tests.cpp
        media/convert_tests.cpp
        media/decode_memory_tests.cpp
        media/decode_tests.cpp
        media/sample_tests.cpp
        media/search_tests.cpp
        media/sha256_tests.cpp
        media/utf8_path_tests.cpp
        metrics/click_injection_tests.cpp
        metrics/metrics_tests.cpp
        metrics/spectrogram_tests.cpp
        tools/bench_kernel_tests.cpp
        tools/bench_tests.cpp
        tools/render_spec_tests.cpp
        tools/signals_tests.cpp
        ui/lane_model_tests.cpp
        ui/undo_stack_tests.cpp
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
# core and gg::ui, then sst-jucegui, which gg::ui links and factory_tests.cpp's knob test
# includes. Then gg::render, gg::ui-core, gg::bench, gg::media and gg::core, which link
# the engine (and gg::render the metrics). gg::bench-kernels (header-only, no
# engine dependency; the tests include its Kernels.h) follows gg::bench, which links it.
# gg::media links gg::core, so comes first.
set(gg_tests_libraries gg::render gg::ui-core gg::bench gg::bench-kernels gg::media gg::core
        gg::engine gg::audiometrics)
if (GG_BUILD_PLUGINS AND GG_BUILD_EDITOR)
    list(PREPEND gg_tests_libraries gg::plugin-impl gg-plugin-core gg::ui sst-jucegui)
elseif (GG_BUILD_PLUGINS)
    list(PREPEND gg_tests_libraries gg::plugin-impl)
endif ()
target_link_libraries(gg-tests PRIVATE ${gg_tests_libraries} gg::compiler-options
        Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
# The Space tests (core/space_reverb_tests.cpp) reach gg-core's private Space headers (core/src,
# as "space/..."), and through them sst-effects and sst-filters, which gg-core links PRIVATE.
target_include_directories(gg-tests PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
target_link_libraries(gg-tests PRIVATE sst-effects sst-filters)
# metrics/spectrogram_tests.cpp reads the spectrogram PNG back with stb_image, implemented in
# gg::stb_impl.
target_link_libraries(gg-tests PRIVATE gg::stb gg::stb_impl)
# The search and settings-file tests set gg-media's test hooks (media/src/TestHooks.h).
target_include_directories(gg-tests PRIVATE ${CMAKE_SOURCE_DIR}/media/src)
# Scratch files (WAV/PNG round trips) go under the build tree, never a shared temp dir.
target_compile_definitions(gg-tests PRIVATE GG_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output"
        GG_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
        GG_AUDIO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audio"
        GG_BENCH_BASELINE_DIR="${CMAKE_SOURCE_DIR}/tools/gg-bench/baselines")
# 0 or 1, so tests can use #if GG_USE_RTSAN for checks that only hold in the RTSan build.
target_compile_definitions(gg-tests PRIVATE GG_USE_RTSAN=$<BOOL:${GG_USE_RTSAN}>)
# 0 or 1: under ASan (1) the allocation tripwire's replacement operator new/delete are compiled
# out (support/AllocTripwire.cpp), and the tests skip their assertions on its count.
target_compile_definitions(gg-tests PRIVATE GG_SANITIZE=$<BOOL:${GG_SANITIZE}>)
# 0 or 1: the plugin tests that need the JUCE editor, and the one that checks there is none.
target_compile_definitions(gg-tests PRIVATE GG_BUILD_EDITOR=$<BOOL:${GG_BUILD_EDITOR}>)
# The editor's tests create it, and drive its loader strip through JUCE (factory_tests.cpp): JUCE's
# headers and definitions, without its sources, which the editor layer links already.
if (GG_BUILD_PLUGINS AND GG_BUILD_EDITOR)
    target_link_libraries(gg-tests PRIVATE gg::juce-headers)
endif ()

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
            plugin/sample_tests.cpp
            )
    target_include_directories(gg-tests PRIVATE ${CMAKE_SOURCE_DIR}/plugin/src)
endif ()
# The editor's tests reach its components' private headers too (ui/src: GrainCanvas, LoaderStrip).
if (GG_BUILD_PLUGINS AND GG_BUILD_EDITOR)
    target_include_directories(gg-tests PRIVATE ${CMAKE_SOURCE_DIR}/ui/src)
endif ()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/libs/catch2/extras)
include(Catch)
# A case tagged [serial] measures time (or memory) and must not run beside the others: it is
# discovered on its own, with RUN_SERIAL (CLAUDE.md, "Tests run in parallel").
catch_discover_tests(gg-tests TEST_SPEC "~[serial]" ADD_TAGS_AS_LABELS)
catch_discover_tests(gg-tests TEST_SPEC "[serial]" ADD_TAGS_AS_LABELS
        PROPERTIES RUN_SERIAL TRUE)

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
#   - other machine: exit 0, "no baseline for <this machine>; no verdict";
#   - missing (bench_baseline_missing): the entry also has engine-live, which the run lacks:
#     exit 3, "verdict: 0 of 3 ratios regressed, 3 baseline entries missing from this run (...)";
#   - no ratios (bench_baseline_no_ratios): a run of reference alone has no ratio to check
#     against the entry: exit 1 and the error on stderr, never "0 of 0".
#   CI's bench step relies on these codes: it reports exit 3 without failing (ruling 3).
# - bench_space_cli: gg-bench --space --quick, Space's report-only cost, must exit 0 and write
#   its JSON (the [bench] case "gg-bench --space --quick prints a per-sample cost" checks what
#   it prints and writes).
set(bench_cases hermite-simd,reference,engine-sample)
add_test(NAME bench_cli
        COMMAND gg-bench --seconds 0.002 --rounds 2 --grains 50 --cases ${bench_cases}
        --json ${CMAKE_BINARY_DIR}/bench_cli.json)
add_test(NAME bench_make_baseline
        COMMAND gg-bench --make-baseline test ${CMAKE_BINARY_DIR}/bench_made_baseline.json
        ${CMAKE_BINARY_DIR}/bench_cli.json ${CMAKE_BINARY_DIR}/bench_cli.json)
set_tests_properties(bench_cli PROPERTIES LABELS bench FIXTURES_SETUP bench_json)
set_tests_properties(bench_make_baseline PROPERTIES LABELS bench FIXTURES_REQUIRED bench_json)
add_test(NAME bench_space_cli
        COMMAND gg-bench --space --quick --json ${CMAKE_BINARY_DIR}/bench_space_cli.json)
set_tests_properties(bench_space_cli PROPERTIES LABELS bench)
# - bench_rejects_*: an option the mode doesn't take is bad usage (carry-over [151]), and so is a
#   value --space's validation rejects. Each passes only on gg-bench's error line, then its
#   usage (both on stderr, before anything is timed), with exit code 2 (check-exit-code.cmake).
set(bench_rejects_space_cases_args --space --cases reference)
set(bench_rejects_space_cases_error "--cases doesn't apply with --space")
set(bench_rejects_canvas_seconds_args --canvas --seconds 1)
set(bench_rejects_canvas_seconds_error "--seconds doesn't apply with --canvas")
set(bench_rejects_make_baseline_quick_args --quick --make-baseline test out.json a.json b.json)
set(bench_rejects_make_baseline_quick_error "--quick doesn't apply with --make-baseline")
set(bench_rejects_space_seconds_args --space --seconds 0)
set(bench_rejects_space_seconds_error "the run length must be more than 0")
foreach (test IN ITEMS bench_rejects_space_cases bench_rejects_canvas_seconds
        bench_rejects_make_baseline_quick bench_rejects_space_seconds)
    add_test(NAME ${test}
            COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:gg-bench>
            "-DARGS=${${test}_args}" -DEXPECTED_CODE=2
            "-DEXPECTED_STDERR=error: ${${test}_error}"
            "-DEXPECTED_STDERR2=usage: gg-bench"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check-exit-code.cmake)
    set_tests_properties(${test} PROPERTIES LABELS bench)
endforeach ()
# - bench_make_baseline_invalid: --make-baseline from a run whose ratio has a median of 0 (which
#   would make a tolerance of 0/0, NaN) must exit 1 with the error and write no file.
# - bench_make_baseline_merge: --make-baseline --merge, written over the file it merges into,
#   keeps another model's entry and replaces this machine's (check-bench-merge.cmake).
# - bench_make_baseline_bad_merge: --merge without its file and an input is bad usage (exit 2).
add_test(NAME bench_make_baseline_merge
        COMMAND ${CMAKE_COMMAND} -DBENCH=$<TARGET_FILE:gg-bench>
        -DRUN_JSON=${CMAKE_BINARY_DIR}/bench_cli.json
        -DBASELINE=${CMAKE_CURRENT_BINARY_DIR}/bench_merged_baseline.json
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check-bench-merge.cmake)
set_tests_properties(bench_make_baseline_merge PROPERTIES LABELS bench FIXTURES_REQUIRED bench_json)
set(bench_make_baseline_bad_merge_args --make-baseline test out.json --merge old.json)
add_test(NAME bench_make_baseline_bad_merge
        COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:gg-bench>
        "-DARGS=${bench_make_baseline_bad_merge_args}" -DEXPECTED_CODE=2
        "-DEXPECTED_STDERR=error: bad argument: --make-baseline"
        "-DEXPECTED_STDERR2=usage: gg-bench"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check-exit-code.cmake)
set_tests_properties(bench_make_baseline_bad_merge PROPERTIES LABELS bench)
set(bench_bad_run ${CMAKE_CURRENT_BINARY_DIR}/bench_bad_run.json)
file(WRITE ${bench_bad_run} "{\"schemaVersion\": 2, \"machine\": \"CPU (test)\", \"commit\": \"test\", \"ratios\": [{\"name\": \"engine-sample\", \"blockSize\": 32, \"median\": 0}]}\n")
set(bench_invalid_baseline ${CMAKE_CURRENT_BINARY_DIR}/bench_invalid_baseline.json)
set(bench_make_baseline_invalid_args --make-baseline test ${bench_invalid_baseline} ${bench_bad_run})
add_test(NAME bench_make_baseline_invalid
        COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:gg-bench>
        "-DARGS=${bench_make_baseline_invalid_args}" -DEXPECTED_CODE=1
        "-DEXPECTED_STDERR=error: run 1 has a ratio whose median isn't positive and finite: engine-sample at block 32"
        -DABSENT=${bench_invalid_baseline}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check-exit-code.cmake)
set_tests_properties(bench_make_baseline_invalid PROPERTIES LABELS bench)
set(verdict_line "ratios regressed (limit: the baseline's ratio x (1 + its tolerance))")
set(bench_baseline_pass_args -DRATIO=1e9 -DEXPECTED_CODE=0
        "-DEXPECTED_VERDICT=verdict: 0 of 3 ${verdict_line}")
set(bench_baseline_regressed_args -DRATIO=1e-9 -DEXPECTED_CODE=3
        "-DEXPECTED_VERDICT=verdict: 3 of 3 ${verdict_line}")
set(bench_baseline_other_machine_args -DRATIO=1e-9 "-DMACHINE=no such CPU (test)"
        -DEXPECTED_CODE=0 "-DEXPECTED_VERDICT=no baseline for <machine>$<SEMICOLON> no verdict")
set(bench_baseline_missing_args -DRATIO=1e9 -DEXTRA_CASE=engine-live -DEXPECTED_CODE=3
        "-DEXPECTED_VERDICT=verdict: 0 of 3 ratios regressed, 3 baseline entries missing from this run (limit: the baseline's ratio x (1 + its tolerance))")
set(bench_baseline_no_ratios_args -DRATIO=1e9 -DCASES=reference -DEXPECTED_CODE=1
        "-DEXPECTED_STDERR=the run has no ratios to check against the baseline's entry")
foreach (test IN ITEMS bench_baseline_pass bench_baseline_regressed bench_baseline_other_machine
        bench_baseline_missing bench_baseline_no_ratios)
    add_test(NAME ${test}
            COMMAND ${CMAKE_COMMAND} -DBENCH=$<TARGET_FILE:gg-bench>
            -DRUN_JSON=${CMAKE_BINARY_DIR}/bench_cli.json
            -DBASELINE=${CMAKE_CURRENT_BINARY_DIR}/${test}.json ${${test}_args}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check-bench-baseline.cmake)
    set_tests_properties(${test} PROPERTIES LABELS bench FIXTURES_REQUIRED bench_json)
endforeach ()

# The scripts (carry-over [130]-[132]), label `scripts`: scripts/check.sh against stub cmake and
# ctest (replaying CTest 3.31's and 4.4's recorded output) in a fake source tree, and
# scripts/worktree-setup.sh in scratch git repositories, all under the build tree; and the
# release pipeline's scripts (scripts/release/) against scratch files. Not on Windows, where the
# scripts' bash (Git Bash) is unverified (CLAUDE.md).
if (NOT WIN32)
    find_program(GG_BASH bash REQUIRED)
    add_test(NAME scripts_check_and_worktree_setup
            COMMAND ${GG_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/scripts_tests.sh
            ${CMAKE_CURRENT_BINARY_DIR}/scripts-tests ${CMAKE_COMMAND})
    set_tests_properties(scripts_check_and_worktree_setup PROPERTIES LABELS scripts)
    add_test(NAME scripts_release
            COMMAND ${GG_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/release_scripts_tests.sh
            ${CMAKE_CURRENT_BINARY_DIR}/release-scripts-tests)
    set_tests_properties(scripts_release PROPERTIES LABELS scripts)
endif ()

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
#
# The cases' media files are generated, never committed (spec §6.9): golden_media writes them
# into <build>/golden-media, a fixture every golden test requires, and the cases' relative paths
# resolve there (--media-dir). That applies to every relative path in every case, so a future
# case that reads a committed file must give its path some other way than relative to the spec.
set(GG_GOLDEN_MEDIA_DIR ${CMAKE_BINARY_DIR}/golden-media)
add_test(NAME golden_media COMMAND gg-render --write-media ${GG_GOLDEN_MEDIA_DIR})
set_tests_properties(golden_media PROPERTIES LABELS golden FIXTURES_SETUP golden_media)
# --write-media takes no other option: one given with it is refused (exit 2), never ignored.
add_test(NAME render_write_media_alone
        COMMAND gg-render --write-media ${CMAKE_CURRENT_BINARY_DIR}/write-media-alone
        --block-size 64 -o ${CMAKE_CURRENT_BINARY_DIR}/write-media-alone/x.wav)
set_tests_properties(render_write_media_alone PROPERTIES LABELS render
        PASS_REGULAR_EXPRESSION "excludes --write-media|--write-media excludes")
# A non-ASCII --spectrogram path (carry-over [181]): gg-render exits 0 only if the PNG was
# written, which on Windows needs the wide-path open, not stbi_write_png's narrow one.
add_test(NAME render_spectrogram_utf8_path
        COMMAND gg-render ${CMAKE_CURRENT_SOURCE_DIR}/golden/cases/sparse-sine.json
        --media-dir ${GG_GOLDEN_MEDIA_DIR}
        --spectrogram "${CMAKE_CURRENT_BINARY_DIR}/render-utf8/spëctrogram ✓ 音.png")
set_tests_properties(render_spectrogram_utf8_path PROPERTIES LABELS render
        FIXTURES_REQUIRED golden_media)
file(GLOB GG_GOLDEN_CASE_SPECS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/golden/cases/*.json)
if (NOT GG_GOLDEN_CASE_SPECS)
    message(FATAL_ERROR "No golden cases in ${CMAKE_CURRENT_SOURCE_DIR}/golden/cases")
endif ()
foreach (spec IN LISTS GG_GOLDEN_CASE_SPECS)
    get_filename_component(case ${spec} NAME_WLE)
    add_test(NAME golden_${case}
            COMMAND gg-render ${spec}
            --media-dir ${GG_GOLDEN_MEDIA_DIR}
            -o ${CMAKE_BINARY_DIR}/golden/${case}.wav
            --spectrogram ${CMAKE_BINARY_DIR}/golden/${case}.png
            --compare ${CMAKE_CURRENT_SOURCE_DIR}/golden/expected/${case}.metrics.json
            --compare-events ${CMAKE_CURRENT_SOURCE_DIR}/golden/expected/${case}.events.jsonl)
    set_tests_properties(golden_${case} PROPERTIES LABELS golden FIXTURES_REQUIRED golden_media)
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
        --media-dir ${GG_GOLDEN_MEDIA_DIR}
        --compare-events ${tamper_dir}/${tamper_case}.events.jsonl)
add_test(NAME golden_tamper_metrics
        COMMAND gg-render ${CMAKE_CURRENT_SOURCE_DIR}/golden/cases/${tamper_case}.json
        --media-dir ${GG_GOLDEN_MEDIA_DIR}
        --compare ${tamper_dir}/${tamper_case}.metrics.json)
set_tests_properties(golden_tamper_events PROPERTIES LABELS golden
        FIXTURES_REQUIRED golden_media
        PASS_REGULAR_EXPRESSION "FAIL: event log differs from .* at line 5 ")
set_tests_properties(golden_tamper_metrics PROPERTIES LABELS golden
        FIXTURES_REQUIRED golden_media
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

    # Notarization's signing requirements (spec §6.2), label `installer`: signed with a real
    # identity, every bundle carries the hardened runtime, and the two standalone apps the
    # audio-input entitlement (installer/macos/standalone.entitlements). An ad-hoc tree signs
    # with neither, so these exist only when GG_MACOS_CODESIGN_IDENTITY isn't "-".
    if (NOT GG_MACOS_CODESIGN_IDENTITY STREQUAL "-")
        foreach (format IN ITEMS clap vst3 auv2 standalone standalone_fx)
            set(entitlement "")
            if (format MATCHES "^standalone")
                set(entitlement com.apple.security.device.audio-input)
            endif ()
            add_test(NAME codesign_hardened_${format}
                    COMMAND ${CMAKE_COMMAND} -DCODESIGN=${GG_CODESIGN}
                    -DBUNDLE=$<TARGET_BUNDLE_DIR:gg_clapfirst_${format}>
                    -DEXPECT_ENTITLEMENT=${entitlement}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/installer/check-codesign-flags.cmake)
            set_tests_properties(codesign_hardened_${format} PROPERTIES LABELS installer)
        endforeach ()
    endif ()

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

    # Bundle versions (spec §6.1), label `installer`: each bundle's CFBundleShortVersionString and
    # CFBundleVersion are the version's numeric part alone, which Apple requires; the pre-release
    # suffix appears only in GG_VERSION_STRING.
    foreach (format IN ITEMS clap vst3 auv2 standalone standalone_fx)
        add_test(NAME version_bundle_${format}
                COMMAND ${CMAKE_COMMAND} -DPLUTIL=${GG_PLUTIL} -DCHECK=bundle-version
                -DBUNDLE=$<TARGET_BUNDLE_DIR:gg_clapfirst_${format}>
                -DEXPECTED=${GG_VERSION_NUMERIC} -P ${check_identity})
        set_tests_properties(version_bundle_${format} PROPERTIES LABELS installer)
    endforeach ()

    # The AU state format (labels `plugin` and `au`): gg-au-state-tests loads the built
    # .component into its own process, registers its factories there (AudioComponentRegister;
    # nothing is installed) and checks that ClassInfo carries the CLAP state bytes, GGST
    # included, and round-trips them. It also pins clap-wrapper's three known AU state faults
    # (T7 223), so a clap-wrapper update that changes one fails here. Its own executable: gg-tests
    # links the plugin's code itself, and the bundle brings a second copy.
    add_executable(gg-au-state-tests plugin/au_state_tests.cpp)
    target_include_directories(gg-au-state-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(gg-au-state-tests PRIVATE gg::media gg::core gg::compiler-options
            Catch2::Catch2WithMain "-framework AudioToolbox" "-framework CoreFoundation")
    target_compile_definitions(gg-au-state-tests PRIVATE
            GG_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output"
            GG_AU_BUNDLE="$<TARGET_BUNDLE_DIR:gg_clapfirst_auv2>"
            GG_CLAP_WRAPPER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/libs/clap/clap-wrapper")
    add_dependencies(gg-au-state-tests gg_clapfirst_auv2)
    add_dependencies(gg-tests gg-au-state-tests)
    catch_discover_tests(gg-au-state-tests ADD_TAGS_AS_LABELS)
endif ()

# The installers (spec §6), label `installer`. installer_build_products builds gg-products (not
# in `all`) into build/<preset>/gg-products/, as the fixture every later installer_* test
# requires, so `ctest -L installer` needs no build step of its own. It runs alone: a build can
# relink the bundles other tests read. The products exist only with the editor and the formats.
if (GG_BUILD_PLUGINS AND GG_BUILD_EDITOR)
    add_test(NAME installer_build_products
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --config $<CONFIG>
            --target gg-products)
    set_tests_properties(installer_build_products PROPERTIES LABELS installer
            FIXTURES_SETUP gg_products RUN_SERIAL TRUE)

    # The license set (spec §6.3, carry-over row 221): gg-products/licenses/ holds exactly the
    # platform's notices and license texts, none of them a stub.
    add_test(NAME installer_license_set
            COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_BINARY_DIR}/gg-products/licenses
            -DPLATFORM=${CMAKE_SYSTEM_NAME}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/installer/check-license-set.cmake)
    set_tests_properties(installer_license_set PROPERTIES LABELS installer
            FIXTURES_REQUIRED gg_products)

    # The macOS PKG and DMG (spec §6.4), unsigned: installer_macos_pkg checks what
    # installer/macos/make-installer.sh makes from the products; _missing_product and _partial_env
    # check that it refuses, with exit 2 and nothing written, an empty products directory and a
    # signing environment with only some of its five variables. Each writes its own folder.
    if (APPLE)
        foreach (case IN ITEMS pkg pkg_missing_product pkg_partial_env)
            set(switch "")
            if (case STREQUAL "pkg_missing_product")
                set(switch -DMISSING=1)
            elseif (case STREQUAL "pkg_partial_env")
                set(switch -DPARTIAL_ENV=1)
            endif ()
            add_test(NAME installer_macos_${case}
                    COMMAND ${CMAKE_COMMAND}
                    -DSCRIPT=${PROJECT_SOURCE_DIR}/installer/macos/make-installer.sh
                    -DPRODUCTS=${GG_PRODUCT_DIR} -DLICENSES=${GG_LICENSE_DIR}
                    -DRESOURCES=${PROJECT_SOURCE_DIR}/installer/macos
                    -DOUT=${CMAKE_CURRENT_BINARY_DIR}/installer-macos/${case}
                    -DNUMERIC=${GG_VERSION_NUMERIC} -DFULL=${GG_VERSION_STRING} ${switch}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/installer/check-macos-pkg.cmake)
            set_tests_properties(installer_macos_${case} PROPERTIES LABELS installer
                    FIXTURES_REQUIRED gg_products)
        endforeach ()
    endif ()

    # The Linux tarball (spec §6.5): installer_linux_tarball checks the entries, the executable
    # bits, the README and the license set of what installer/linux/make-tarball.sh makes from the
    # products (through tests/installer/check-archive.cmake, which the Windows zip shares);
    # _missing_product checks that it refuses an empty products directory with exit 2 and nothing
    # written. Each writes its own folder.
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        foreach (case IN ITEMS tarball tarball_missing_product)
            set(switch "")
            if (case STREQUAL "tarball_missing_product")
                set(switch -DMISSING=1)
            endif ()
            add_test(NAME installer_linux_${case}
                    COMMAND ${CMAKE_COMMAND}
                    -DSCRIPT=${PROJECT_SOURCE_DIR}/installer/linux/make-tarball.sh
                    -DPRODUCTS=${GG_PRODUCT_DIR}
                    -DREADME=${PROJECT_SOURCE_DIR}/installer/linux/README.md
                    -DOUT=${CMAKE_CURRENT_BINARY_DIR}/installer-linux/${case}
                    -DFULL=${GG_VERSION_STRING}
                    -DCHECK_ARCHIVE=${CMAKE_CURRENT_SOURCE_DIR}/installer/check-archive.cmake
                    ${switch}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/installer/check-linux-tarball.cmake)
            set_tests_properties(installer_linux_${case} PROPERTIES LABELS installer
                    FIXTURES_REQUIRED gg_products)
        endforeach ()
    endif ()

    # The Windows zip and installer (spec §6.5): installer_windows_zip checks the entries, the
    # readme and the license set of what installer/windows/make-zip.cmake makes from the products
    # (through check-archive.cmake, as the Linux tarball does); _missing_product checks that it
    # refuses an empty products directory, naming every product, with nothing written.
    # installer_windows_inno compiles installer/windows/giga-grains.iss, only where
    # cmake/products.cmake found Inno Setup's compiler (GG_ISCC). Each writes its own folder.
    if (WIN32)
        foreach (case IN ITEMS zip zip_missing_product)
            set(switch "")
            if (case STREQUAL "zip_missing_product")
                set(switch -DMISSING=1)
            endif ()
            add_test(NAME installer_windows_${case}
                    COMMAND ${CMAKE_COMMAND}
                    -DSCRIPT=${PROJECT_SOURCE_DIR}/installer/windows/make-zip.cmake
                    -DPRODUCTS=${GG_PRODUCT_DIR}
                    -DREADME=${PROJECT_SOURCE_DIR}/installer/windows/readme.txt
                    -DOUT=${CMAKE_CURRENT_BINARY_DIR}/installer-windows/${case}
                    -DFULL=${GG_VERSION_STRING}
                    -DCHECK_ARCHIVE=${CMAKE_CURRENT_SOURCE_DIR}/installer/check-archive.cmake
                    ${switch}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/installer/check-windows-zip.cmake)
            set_tests_properties(installer_windows_${case} PROPERTIES LABELS installer
                    FIXTURES_REQUIRED gg_products)
        endforeach ()
        if (GG_ISCC)
            add_test(NAME installer_windows_inno
                    COMMAND ${CMAKE_COMMAND} -DISCC=${GG_ISCC}
                    -DISS=${PROJECT_SOURCE_DIR}/installer/windows/giga-grains.iss
                    -DPRODUCTS=${GG_PRODUCT_DIR} -DLICENSES=${GG_LICENSE_DIR}
                    -DOUT=${CMAKE_CURRENT_BINARY_DIR}/installer-windows/inno
                    -DNUMERIC=${GG_VERSION_NUMERIC} -DFULL=${GG_VERSION_STRING}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/installer/check-windows-inno.cmake)
            set_tests_properties(installer_windows_inno PROPERTIES LABELS installer
                    FIXTURES_REQUIRED gg_products)
        endif ()
    endif ()
endif ()

# The ASIO SDK guard (cmake/AsioGuard.cmake, carry-over [116]), proven on a scratch project: the
# one allowed use configures, and every misuse fails the configure naming it. Each case builds
# in its own folder under the test output.
foreach (case IN ITEMS allowed other-target-source other-target-include other-target-link
        other-file-in-allowed-target other-dir-in-allowed-target
        allowed-source-in-other-target-of-a-subdirectory directory-include)
    add_test(NAME asio_guard_${case}
            COMMAND ${CMAKE_COMMAND} -DGG_GUARD_CASE=${case} -DGG_REPO=${PROJECT_SOURCE_DIR}
            -DGG_OUT=${CMAKE_CURRENT_BINARY_DIR}/asio-guard/${case}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/asio-guard/run.cmake)
    set_tests_properties(asio_guard_${case} PROPERTIES LABELS "build")
endforeach ()

# The version (cmake/version.cmake, spec §6.1): gg_compose_version joins the numeric part and the
# pre-release suffix, and refuses a suffix that isn't dot-separated [0-9A-Za-z-] identifiers.
# Each case runs cmake -P on its own and writes nothing.
foreach (case IN ITEMS dev release nightly bad-underscore bad-empty-identifier)
    add_test(NAME version_${case}
            COMMAND ${CMAKE_COMMAND} -DGG_CASE=${case} -DGG_REPO=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version/run.cmake)
    set_tests_properties(version_${case} PROPERTIES LABELS "build")
endforeach ()
