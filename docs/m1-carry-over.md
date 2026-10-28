# M1 carry-over: what M0 leaves for milestone M1

*Written at the end of M0 (2026-10-26) from the final whole-branch review: four area reviews
(A engine, B tools, C plugin/UI, D build/CI/docs) and the ledger of deferred minor findings
(ruling F3). The review workspace is deleted when M0 closes, so this file is the durable record.
INTENT §20's M1 row links here.*

Each item says what to do, where (a file and line at M0's close, or a ruling or finding ID), and,
where a reviewer gave one, when. IDs in brackets, like [144], are lines of the M0 deferred-findings
ledger; A-, B-, C- and D- IDs are the area reviews' findings.

Items closed since are kept in place, marked **Closed** in the "When" column with the commits that
closed them: on `m1-fx-slice` for M1 sub-project 1 (the FX vertical slice), which closed the first
batch, and on `m1-cloud-engine` for sub-project 2 (the Cloud engine).

At its end, sub-project 1's final whole-branch review added what the slice itself leaves open: the
section "From M1 sub-project 1" below. Its IDs name the slice's ledger: T*n* (line) is a task's
deferred finding at that ledger line, M*n* a final-review finding, and M1-*x* a slice ruling. Its
"When" column names the M1 sub-project (INTENT §20, §23 #35) that owns the item.

## First, before parameters land

| What | Source | When |
|---|---|---|
| **Run the plugin audio path under the sanitizers.** The `asan` and `rtsan` presets set `GG_BUILD_PLUGINS=OFF`, because the plugins need JUCE, so `process`, `params.flush`, the clap-helpers dispatch and the FX dry/wet sum never run under ASan or RTSan. Split a JUCE-free plugin core from the editor glue (or add `GG_BUILD_EDITOR=OFF`) so both presets build and test the plugin core. The `[plugin][rt]` tripwire tests are the M0 stopgap. This split also fixes [161]/[292] and [210] below. | C-I1, D-I1; `CMakePresets.json` (asan, rtsan); `tests/plugin/rt_tests.cpp` | **Closed** by M1 sub-project 1: `ec2a054`, `e23adfb` (`gg-plugin-core`; `asan` and `rtsan` build it with `GG_BUILD_EDITOR=OFF`) |

## Engine

| What | Source | When |
|---|---|---|
| `exp2Det`, `logDet`, `hermite4` and `linear` are header-inline, so whether they contract into FMAs depends on each includer's flags. Add `#pragma clang fp contract(off)` (and the gcc equivalent), or move them out of line. | [70]; `engine/include/gg/core/MathDet.h:51` (`logDet`: `:114`, added in M1), `engine/include/gg/core/Interpolate.h:25`, `:43` | **Closed** by M1 sub-project 2, Task 2: `9fe9b61`, `1e6e637` (`GG_FP_CONTRACT_OFF` in the inline math and the Cloud scheduler's inline arithmetic; a test proves it holds under an includer's contracting flags) |
| `SampleBuffer` move semantics: make it non-copyable, and leave a moved-from buffer at 0 × 0 (today it keeps its channel and frame counts). | [73]; `engine/include/gg/source/SampleBuffer.h:16` | With M1's file loader |
| A saturated pool costs O(capacity) per admitted grain (a stable compaction per mid-block admission). Compact once per block, keep ruling T3-b's semantics, and add a full-pool `gg-bench` case. | [103]; `engine/src/Engine.cpp:128`, `engine/src/GrainPool.cpp:23` | **Closed** by M1 sub-project 2, Task 3: `8864401`, `e7931a0` (the SoA pool with 2 × capacity slots, a min-heap of end samples for admission, compaction once per block); the full-pool case is `gg-bench`'s `engine-full-pool` (`07b4838`) |
| `restartLattice_` stays set after a rate change A → B → A; it should compare against the anchored rate. | [104]; `engine/src/PeriodicStream.cpp:48` | **Closed** by M1 sub-project 1: `e8d1c1c` (`CloudScheduler` replaces `PeriodicStream`; `restartLattice_` is gone) |
| Ruling T3-a re-anchors the lattice at the start of the block where a rate change lands. M1 must apply parameters on the absolute control-rate grid (INTENT §14) instead. | A-m1; `engine/include/gg/grain/PeriodicStream.h:74` | **Closed** by M1 sub-project 1: `e8d1c1c`, `89e244f`, `b54a77d`, `d2a1ded` (the onset recurrence, the pending-onset rescale (ruling M1-h), the Processor's 32-sample grid on absolute samples) |
| The position-jitter RNG key hard-codes layer 0 and stream 0. Pass the real layer and stream IDs. | A-m2; `engine/include/gg/grain/PeriodicStream.h:88` | **Closed** by M1 sub-project 1: `e8d1c1c` (every draw has its own `rng::Field`, keyed with the real layer and stream) |
| Overload-ladder hazards: stealing a grain with a fade needs a mutable end time that keeps `earliestEnd_` correct, and budget admission must count only the grains whose end is after the onset, never `pool_.size()`. | A-m6; `engine/src/Engine.cpp:128`–`:143`, `engine/include/gg/grain/GrainPool.h` | With the M1 overload ladder |
| Hold one `ScopedDenormalsOff` over the whole plugin `process` (nesting is safe; the `[denormal]` nested-guard test proves it), and document on `Engine::process` that it restores the caller's FP mode. See the plugin item below. | [281]/[298]; `engine/src/Engine.cpp:80` | **Closed** by M1 sub-project 1: `7be308e` (`Engine::process` documents it), `b54a77d` (one guard over `Processor::process`) |
| Under `GG_SANITIZE`, compile the tripwire's replacement `operator new`/`delete` out (ruling R7's fallback), so ASan regains its alloc-dealloc-mismatch and new-delete-type-mismatch checks. | [144]; `tests/support/AllocTripwire.cpp` | **Before M1's sample loader lands** |

## Tools: metrics, goldens and bench

| What | Source | When |
|---|---|---|
| clickScore can't see a click in dense material (dense-noise R = 0.180: a 0 dBFS spike scores 10.9 against the limit of 30). Normalize d by a local level, for example the RMS of d over ±5–10 ms excluding n; bump `metricsVersion`; add a CTest that injects a −20 dBFS step into each golden render and checks it is detected. | B-I1; `tools/audiometrics/src/Metrics.cpp:171`–`:194`, `Metrics.h` (`maxClickScore`) | **Before overload and grain stealing are gated on it** |
| Make `gg-bench` gate-grade: interleave repetitions round-robin across kernels and block sizes ([133]); emit every run plus min and IQR, the run length and the commit SHA; gate on ratios measured in the same process, or on thread CPU time; set thresholds from the spread across several CI runs. | B-I2, [133]; `tools/gg-bench/main.cpp:74`, `:225`; `docs/spikes/m0-kernel-benchmark.md` ("Before M1 gates on this") | **Closed** by M1 sub-project 2, Task 8: `e044ad6`, `5fd3545` (interleaved rounds, rotated and reversed on odd rounds; every run, with median, min and IQR, the run length and the commit). The in-process ratios against a frozen reference are Task 9's (`07b4838`); the thresholds come from three separate CI runs, in the baselines under `tools/gg-bench/baselines/` |
| A JSON-schema test of `gg-bench`'s output. | [132] | **Closed** by M1 sub-project 2, Task 8: `e044ad6` (the `[bench]` test that the JSON matches schema 2) |
| Move the dr_libs and stb `*_IMPLEMENTATION` translation units into a target without `gg::compiler-options`, so gcc-12 middle-end warnings in third-party code can't break `-Werror`. | [119]; `tools/audiometrics/src/WavIO.cpp:16`, `Spectrogram.cpp:14` | M1 |
| Detect non-finite band power explicitly (a huge finite input overflows pffft to inf). | [120]; `tools/audiometrics/src/Metrics.cpp` | At the next `metricsVersion` bump |
| A spectrogram test of orientation and grayscale, not just dimensions. | [122]; `tests/metrics/metrics_tests.cpp` | M1 |
| Reject a render whose `durationSeconds` rounds to 0 frames. | [181]; `tools/gg-render/Render.cpp:129` | M1 |
| An internal-error exit code distinct from "bad input" (2), and complete throw docs in `Render.h`. | [182]; `tools/gg-render/main.cpp:241` | M1 |
| Type-check the spec keys a source kind doesn't use. | [183]; `tools/gg-render/RenderSpec.cpp` | In render-spec v2 |
| Wrap `main`'s `computeMetrics` and writers against `bad_alloc` on extreme specs. | [184]; `tools/gg-render/main.cpp:245` | M1 |
| No same-platform audio-bits golden: INTENT §19 says bit-exact comparison applies on the same platform, yet a 5% gain bug passes every golden. Add a per-platform output digest, and gain/pan to the event line, or amend §19. | B-m7; `tests/golden/`, `tools/gg-render/Render.h` | M1. **Closed in part** by M1 sub-project 1: `e8d1c1c`, `012f0fa` (the event line carries `gain` and `pan`, so the cross-platform goldens pin level and stereo placement). The per-platform output digest, or the §19 amendment, is still open |
| The goldens never use a stereo source; add a stereo case. | B-m8; `tests/golden/cases/` | **Closed** by M1 sub-project 1: `fd9b87f` (`fx-noise-stereo`) |
| `gg-render` opens files with narrow paths on Windows; use dr_libs' `_w` variants. | B-m9; `tools/audiometrics/src/WavIO.cpp:58` | When M1 feeds it user WAVs |

## Plugin and UI

| What | Source | When |
|---|---|---|
| Put `ScopedDenormalsOff` at the top of `processNonblocking`, so the FX's dry + wet sum runs flushed too. Add a test that feeds subnormal input and checks for no subnormal output and a restored FP mode. Document that the FX's pass-through is bit-exact only for normal floats. clap-validator's `process-audio-denormals` warning is a timing ratio, noise on arm64, not a gate. | [281]/[298]/C-m2; `plugin/src/GigaGrainsPlugin.cpp:168`, `:227` | **Closed** by M1 sub-project 1: `b54a77d` (the guard covers `Processor::process`, dry/wet sum included; test `processor flushes denormals`; `Processor.h` documents the normal-float pass-through), `a8202e2` (the plugin's `process` runs through the `Processor`) |
| Port-value tests (main port IDs 0 and 1 and the in-place pairs), the FX's no-input branch, and `reset()` tests (the rest of C-m4). | [160], C-m4; `plugin/src/GigaGrainsPlugin.cpp` | **Now due:** M1 has added state. **Closed in part** by M1 sub-project 1: `5ac840b` (`reset()` runs under the allocation tripwire, test `plugin reset does not allocate and a restored Freeze plays live`). The port-value tests and the FX's no-input branch are still open |
| The `ld` "ignoring duplicate libraries" warning (`libgg-engine.a`, `libgg-ui-core.a`). | [161]/[292]; `tests/CMakeLists.txt` | **Closed** by M1 sub-project 1: `ec2a054` |
| Move `createEditor()` into its own translation unit, so the plugin's audio TU stops including JUCE GUI headers. | [210]; `plugin/src/GigaGrainsPlugin.cpp:85` | **Closed** by M1 sub-project 1: `ec2a054` (`plugin/src/editor/EditorPlugin.cpp`) |
| The GUI test should assert only each platform's native window API, not the shim's cocoa/win32/x11 over-claim on every platform. | [211]; `tests/plugin/factory_tests.cpp:160`–`:162` | **Closed** by M1 sub-project 1: `ec2a054` |
| Set `JUCE_DISABLE_JUCE_VERSION_PRINTING=1` (the test log prints "JUCE v9.0.4"). | [212]; `ui/CMakeLists.txt` | **Closed** by M1 sub-project 1: `ec2a054` |
| Consolidate the `JUCE_*` definitions (`ui/CMakeLists.txt:38`–`:51`, including `JUCE_USE_WEBP=0`) next to `add_clap_juce_shim` in `libs/CMakeLists.txt`. | [213]/[344] | **Closed** by M1 sub-project 1: `ec2a054` (every `JUCE_*` definition is in `libs/CMakeLists.txt`) |
| Clamp `px1`/`py1` to width − 1 / height − 1 instead of relying on width ≤ 2^24. | [214]; `ui/src/SplatRenderer.cpp:80`–`:82` | When the real canvas reuses `SplatRenderer` |
| Upstream the missing `<string>` include to sst-clap-helpers' `clap_juce_shim.h`, then drop our workaround. | [297]; `plugin/src/GigaGrainsPlugin.h:11`–`:12` | At the next sst-clap-helpers bump |
| Pin the AU state format with a test. | C (declined, noted) | **Now due:** M1 sub-project 1 added state |
| auval reports "Preset name is not retained" three times. It is upstream clap-wrapper: `wrapasauv2.cpp:2283`–`:2289` writes `_current_program_name`, which only `RestoreState` sets. Also, its `SaveState` ignores the plugin's `save()` result, and `RestoreState` returns `noErr` when `load()` fails. Pin these together with the AU state format, and report them upstream. | T7 (223); clap-wrapper `src/detail/auv2/wrapasauv2.cpp` | With the AU state-format test above (now due) |
| clap-wrapper's VST3 `process` takes a spin lock. Third-party; keep in mind when profiling. | C (declined, noted) | — |

## Build and CI

| What | Source | When |
|---|---|---|
| Add `-Wall -Wextra`, and `-Werror` on ClangCL (today `-Werror` without either, and none on Windows). | [63]; `cmake/compiler-options.cmake:77`, `:84`, `:121` | M1 |
| At the Minimal checking level, clap-helpers still reports real host misbehaviour (a wrong thread, process while inactive) through stdio. `CheckingLevel::None` would remove it; decide with the plugin RT coverage above. | [236]; `plugin/src/HelpersConfig.h`, clap-helpers `plugin.hxx` | **Closed** by M1 sub-project 1: `ec2a054` (decided: `Minimal` stays in release-type builds; `HelpersConfig.h` says why) |
| A CPM source cache in CI, so each leg doesn't re-download the VST3 SDK, AudioUnitSDK and wil. | [199] | M1 |
| Required status checks and branch protection on `main`. A GitHub repository setting, so it's the director's call (ruling F6). | D-I2 | At the end of M0, before merging |

## Docs

| What | Source | When |
|---|---|---|
| Either add the same-platform output digest (B-m7 above) or amend INTENT §19's "bit-exact comparison applies on the same platform". | B-m7; INTENT §19 | M1 |
| Remove clap-validator's `--exclude '^param-conversions$'` from `ci.yml` and CLAUDE.md once the plugins have parameters. Already an M1 exit criterion. | Ruling T8-c; INTENT §20 | **Closed** by M1 sub-project 1: `a8202e2` (removed from `ci.yml` and CLAUDE.md; clap-validator runs in full) |

## From M1 sub-project 1 (the FX vertical slice)

Written at the slice's end (2026-10-27) from its final whole-branch review and the triage of its
ledger. Items the review fixed before the merge aren't listed.

| What | Source | When |
|---|---|---|
| **Ruling M1-aa.** Spec §7's WAV-file input for FX-mode `gg-render` is deferred: FX mode accepts only the built-in `sine`, `sweep` and `noise` inputs, and rejects `"file"`. Add the file input with the file loader. | M1-aa; final review M4; `tools/gg-render/RenderSpec.cpp` (`input`), slice spec §7 | Sub-project 3 |
| **Ruling M1-ac: Freeze arming after a reset.** The behaviour is kept (director, 2026-10-27): `reset()` disarms Freeze, and it re-arms only when Freeze is seen off at a call's start, a grid boundary or a `setBase` (params.flush or a state load), so an emptied ring is never frozen. The reset itself doesn't arm. So after a VST3 suspend, a Freeze turned on before audio flows stays live until it's toggled; an activation with Freeze off does arm. Freeze modulation that takes the effective value off and back on also counts as a toggle. INTENT §14's Freeze bullet and §23 #40 name only grid boundaries and a block's first sample: state the full rule there (`core/include/gg/core/Processor.h:69`–`:79` has it), and add the host-checklist lines: a project reopened with Freeze on plays live; toggling engages; stop and start in a VST3 host; Freeze turned on while suspended. | M1-ac; final fix re-review; `core/src/Processor.cpp:119`–`:122`, `:179`–`:196` | Sub-project 3, with the note-triggered freeze/capture work |
| A seed loaded while active takes effect only if the host honours `request_restart`; if it doesn't, the engine keeps the old seed while `stateSave` writes the new one, so a reload sounds different from what was heard. Latent while every instance uses seed 0. Hand the seed to the audio thread with the snapshot and reseed in `Processor::reset`, or ask for a restart and keep `seed_` unchanged until `activate`. | Final review M2; `plugin/src/GigaGrainsPlugin.cpp:486`–`:498` | Sub-project 5, which decides per-instance seeds |
| Nothing automated keeps `libs/asiosdk` out of the build (spec §5 allows a review check, and the final review made one). Add a configure-time guard over every target's `SOURCES`, or a CI grep of `build.ninja` for `asiosdk`. | Final review M5; slice spec §5 | Sub-project 6, or sooner if `libs/CMakeLists.txt` changes how sst-plugininfra is added |
| The `[rt]` live test's `setSourceMode(Live)` is a no-op (the engine is already in Live mode), so the source-mode switch never runs under the tripwire. It is non-allocating on inspection. Cover the switch, both ways, once a product switches modes. | T4 (127), final review M6; `tests/engine/rt_tests.cpp:198` | Sub-project 3 |
| A source-mode change hard-cuts the sounding grains (the pool is cleared). The FX can't reach it; any live-input instrument must fade them instead. | T4 (128); `engine/src/Engine.cpp:110`–`:117` | Sub-project 3 |
| `toCloudParams` maps Position and Spray to seconds in every mode. The instrument's Sample mode needs its own mapping, or its Position will be wrong. | T6 (152); `core/src/ParamMap.cpp:29` | Sub-project 3, before the instrument's Sample source |
| `renderLiveGrain` computes the fade and reads taps where the Hann window is 0 (the onset sample when `onsetFrac` > 0, and up to 2 samples past the duration). Wasted work, harmless. | T4 (126); `engine/src/Engine.cpp:349` | **Closed** by M1 sub-project 2, Tasks 4 and 7: `d226c6d` (the scalar reference reads nothing where the window is 0), `1bd948a` (the SIMD live kernel runs only over the fade's interior) |
| The bad-state test's snapshot doesn't cover `migratedFromVersion` or `dirty`. The values-untouched guarantee is tested. | T5 (105); `tests/core/patch_tests.cpp:398` | With the next state-format change (sub-project 3's media manifest) |
| `stateLoad`'s read loop is unbounded. Real host streams are finite; cap it (1 MiB, say) and fail cleanly. | T7 (218); `plugin/src/GigaGrainsPlugin.cpp:461` | Sub-project 3, with the media-manifest state |
| The value-mirror race (ruling M1-x): after the audio thread applies a loaded state, set a flag and `requestCallback`, then `paramsRescan(CLAP_PARAM_RESCAN_VALUES)` on the main thread. Benign and self-healing today. | T7 (217), M1-x; `plugin/src/GigaGrainsPlugin.cpp:285` | Sub-project 5, when the UI starts reading values |
| "Missing input" is tested at plugin level only as an absent port. Add `data32 == nullptr` and null-channel-pointer cases; the Processor-level tests cover null pointers. | T7 (219); `tests/plugin/params_tests.cpp:666` | With C-m4's port-value tests (now due) |
| The huge-frame test fails without the cap only through the accessor on macOS, after a ~44 s, 34 GB allocation. Say so in its comment. | T7 (220); `tests/plugin/process_tests.cpp:365` | When the test is next touched |
| The mono FX path has no independent check against a direct `Processor` run (only `channels` 2 has one). The `fx-sine` golden pins it. | T8 (207); `tests/tools/render_spec_tests.cpp` (`fx render matches the processor`) | Sub-project 3, or the next `gg-render` FX change |
| `readAutomation`'s error text reads "…before the render's end, durationSeconds", and a point in the last 31 samples is accepted but never applies (its grid boundary is past the end). Document both in `RenderSpec.h`. | T8 (208); `tools/gg-render/RenderSpec.cpp:268` | The next `RenderSpec` change |
| `tools/gg-render/CMakeLists.txt:21`'s "for the same reason" is unclear, and `Signals.h`'s doc comment for `inputFadeFrames` sits on `kInputFadeSeconds`. Cosmetic. | T8 (209); `tools/gg-render/Signals.h:36`–`:39` | The next `gg-render` change |
| `randomSplit`'s comment overstates how often calls end on the grid (1–3 of about 70 do, and no random split reaches the carry at a call's end). Reword it, or snap the multiples of 32 onto the grid. The dedicated test `events for one boundary apply as one group across a call's end` covers the case. | T6 (179); `tests/core/processor_tests.cpp:390` | When the test is next touched |
| `check.sh` keeps a stale `build/check/format.log` under `--no-format`; `worktree-setup.sh` doesn't validate preset names; CTest 3.x's real output and Windows Git Bash are unverified (CI exercises the real paths); the fetch path for an unadvertised pin is untested. | X1 (175); `scripts/check.sh`, `scripts/worktree-setup.sh` | When the scripts are next touched |
| `check.sh`'s install guard accepts lower- and mixed-case `notfound` and `x-notfound`, which CMake counts as true (its NOTFOUND match is case-sensitive). Match `NOTFOUND` and `*-NOTFOUND` on the raw value. | X1 (191); `scripts/check.sh:162`–`:163` | When the scripts are next touched |
| `check.sh` aborts without a summary if a preset's `binaryDir` isn't `build/<preset>`; `worktree-setup.sh`'s status comment omits `U`; `-L` is a regular expression, so `-L rt` also matches `rtsan` (CLAUDE.md documents it). | X1 (193); `scripts/check.sh:160`, `scripts/worktree-setup.sh:18` | When the scripts are next touched |
| sst-plugininfra's `libs/asiosdk` is the ASIO SDK 2.3.4, dual-licensed (the Steinberg ASIO License or GPLv3). Nothing builds it. Re-enabling Windows ASIO is the director's later decision. | T5 (87, 107); INTENT §15, §22 | When the director decides on Windows ASIO |

## From M1 sub-project 2 (the Cloud engine)

Written at the sub-project's end (2026-10-28). Its final whole-branch review adds the rest.

| What | Source | When |
|---|---|---|
| **Make the CPU gate blocking, with the published X.** Through M1, CI's `gg-bench` step reports each ratio's verdict against the leg's baseline and turns a regression (exit 3) into a warning (ruling 3, INTENT §23 #44). Measure X on the M5 Max, publish it, and make a regression fail the job. First deal with the CI runners' CPU mix: ratios for unchanged code drifted up to ~20% between runs on Windows that landed on different CPU models (§23 #47; `docs/spikes/m1-cpu-levers.md`, "CI variance"), so the tolerance must cover every model the pool serves, or each model needs its own baseline. | Ruling 3; INTENT §16, §19, §23 #44, #47; `.github/workflows/ci.yml` (Benchmark step) | Sub-project 6 |

## Resolved in M0's final fix wave (not carried)

- **[199] (bench/golden part), D-I5:** the macOS CI leg now runs the mac-universal x86_64 slice's
  unit tests and golden checks under Rosetta.
- **[353], C-I2:** `identity_auv2` checks the AU's three AudioComponents in the built bundle.
- **C-I2, the VST3 class IDs:** `identity_vst3` loads the built VST3 on macOS and pins both class
  IDs to INTENT §18's values. It runs on CI's macOS leg, so a clap-wrapper bump that changed them
  fails CI. Windows and Linux factories aren't checked; clap-wrapper derives the same IDs there.
