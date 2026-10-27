# M1 carry-over: what M0 leaves for milestone M1

*Written at the end of M0 (2026-10-26) from the final whole-branch review: four area reviews
(A engine, B tools, C plugin/UI, D build/CI/docs) and the ledger of deferred minor findings
(ruling F3). The review workspace is deleted when M0 closes, so this file is the durable record.
INTENT §20's M1 row links here.*

Each item says what to do, where (a file and line at M0's close, or a ruling or finding ID), and,
where a reviewer gave one, when. IDs in brackets, like [144], are lines of the M0 deferred-findings
ledger; A-, B-, C- and D- IDs are the area reviews' findings.

Items closed since are kept in place, marked **Closed** in the "When" column with the commits that
closed them on `m1-fx-slice`. M1 sub-project 1 (the FX vertical slice) closed the first batch.

## First, before parameters land

| What | Source | When |
|---|---|---|
| **Run the plugin audio path under the sanitizers.** The `asan` and `rtsan` presets set `GG_BUILD_PLUGINS=OFF`, because the plugins need JUCE, so `process`, `params.flush`, the clap-helpers dispatch and the FX dry/wet sum never run under ASan or RTSan. Split a JUCE-free plugin core from the editor glue (or add `GG_BUILD_EDITOR=OFF`) so both presets build and test the plugin core. The `[plugin][rt]` tripwire tests are the M0 stopgap. This split also fixes [161]/[292] and [210] below. | C-I1, D-I1; `CMakePresets.json` (asan, rtsan); `tests/plugin/rt_tests.cpp` | **Closed** by M1 sub-project 1: `ec2a054`, `e23adfb` (`gg-plugin-core`; `asan` and `rtsan` build it with `GG_BUILD_EDITOR=OFF`) |

## Engine

| What | Source | When |
|---|---|---|
| `exp2Det`, `logDet`, `hermite4` and `linear` are header-inline, so whether they contract into FMAs depends on each includer's flags. Add `#pragma clang fp contract(off)` (and the gcc equivalent), or move them out of line. | [70]; `engine/include/gg/core/MathDet.h:51` (`logDet`: `:114`, added in M1), `engine/include/gg/core/Interpolate.h:25`, `:43` | When the M1 kernels add targets |
| `SampleBuffer` move semantics: make it non-copyable, and leave a moved-from buffer at 0 × 0 (today it keeps its channel and frame counts). | [73]; `engine/include/gg/source/SampleBuffer.h:16` | With M1's file loader |
| A saturated pool costs O(capacity) per admitted grain (a stable compaction per mid-block admission). Compact once per block, keep ruling T3-b's semantics, and add a full-pool `gg-bench` case. | [103]; `engine/src/Engine.cpp:128`, `engine/src/GrainPool.cpp:23` | M1 renderer |
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
| Make `gg-bench` gate-grade: interleave repetitions round-robin across kernels and block sizes ([133]); emit every run plus min and IQR, the run length and the commit SHA; gate on ratios measured in the same process, or on thread CPU time; set thresholds from the spread across several CI runs. | B-I2, [133]; `tools/gg-bench/main.cpp:74`, `:225`; `docs/spikes/m0-kernel-benchmark.md` ("Before M1 gates on this") | **Before M1's CPU regression gate** |
| A JSON-schema test of `gg-bench`'s output. | [132] | With the CPU gate |
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
| Port-value tests (main port IDs 0 and 1 and the in-place pairs), the FX's no-input branch, and `reset()` tests (the rest of C-m4). | [160], C-m4; `plugin/src/GigaGrainsPlugin.cpp` | When M1 adds the sidechain and state |
| The `ld` "ignoring duplicate libraries" warning (`libgg-engine.a`, `libgg-ui-core.a`). | [161]/[292]; `tests/CMakeLists.txt` | **Closed** by M1 sub-project 1: `ec2a054` |
| Move `createEditor()` into its own translation unit, so the plugin's audio TU stops including JUCE GUI headers. | [210]; `plugin/src/GigaGrainsPlugin.cpp:85` | **Closed** by M1 sub-project 1: `ec2a054` (`plugin/src/editor/EditorPlugin.cpp`) |
| The GUI test should assert only each platform's native window API, not the shim's cocoa/win32/x11 over-claim on every platform. | [211]; `tests/plugin/factory_tests.cpp:160`–`:162` | **Closed** by M1 sub-project 1: `ec2a054` |
| Set `JUCE_DISABLE_JUCE_VERSION_PRINTING=1` (the test log prints "JUCE v9.0.4"). | [212]; `ui/CMakeLists.txt` | **Closed** by M1 sub-project 1: `ec2a054` |
| Consolidate the `JUCE_*` definitions (`ui/CMakeLists.txt:38`–`:51`, including `JUCE_USE_WEBP=0`) next to `add_clap_juce_shim` in `libs/CMakeLists.txt`. | [213]/[344] | **Closed** by M1 sub-project 1: `ec2a054` (every `JUCE_*` definition is in `libs/CMakeLists.txt`) |
| Clamp `px1`/`py1` to width − 1 / height − 1 instead of relying on width ≤ 2^24. | [214]; `ui/src/SplatRenderer.cpp:80`–`:82` | When the real canvas reuses `SplatRenderer` |
| Upstream the missing `<string>` include to sst-clap-helpers' `clap_juce_shim.h`, then drop our workaround. | [297]; `plugin/src/GigaGrainsPlugin.h:11`–`:12` | At the next sst-clap-helpers bump |
| Pin the AU state format with a test. | C (declined, noted) | When M1 adds state |
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

## Resolved in M0's final fix wave (not carried)

- **[199] (bench/golden part), D-I5:** the macOS CI leg now runs the mac-universal x86_64 slice's
  unit tests and golden checks under Rosetta.
- **[353], C-I2:** `identity_auv2` checks the AU's three AudioComponents in the built bundle.
- **C-I2, the VST3 class IDs:** `identity_vst3` loads the built VST3 on macOS and pins both class
  IDs to INTENT §18's values. It runs on CI's macOS leg, so a clap-wrapper bump that changed them
  fails CI. Windows and Linux factories aren't checked; clap-wrapper derives the same IDs there.
