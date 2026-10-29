# Carry-over burn-down inventory (2026-10-28)

Input: `docs/m1-carry-over.md` at `m1-space` HEAD `c1de2e1`. Row ids are that file's line
numbers (the numbering the reviewers cited), plus the row's first words and its section. Every
FIXABLE-NOW row was checked against HEAD and is still real; current `file:line` is given where
the row's own line numbers are stale. No row turned out to be already done.

Sections: E = Engine, T = Tools, P = Plugin and UI, B = Build and CI, D = Docs, S1/S2/S3/S4 =
"From M1 sub-project 1/2/3/4".

> **Status (2026-10-28):** Space's fix wave (PR #7) has since closed the 11 CLOSING-IN-FIX-WAVE rows (row 219 on CI run 40814679184). The director decided two BLOCKED-DIRECTOR rows the same day: **133** (enable Windows ASIO in the standalone under the ASIO SDK's GPLv3 option) and **193** (a newer MEDI version loads the patch, keeps the unknown manifest bytes for re-save, and shows a status). Both are now FIXABLE-NOW (burn-down Tasks 10 and 6). Row 218 (Decay's label) stays for the Space listening check. Row numbers are labels from the inventory's reading; match rows by content.

## 1. Counts (71 open rows)

| Status | Rows | Count |
|---|---|---|
| FIXABLE-NOW | 58 62 63 66 68 83 94 96 103 116 130 131 132 144 145 146 148 150 153 154 155 156 157 158 159 160 161 162 180 181 185 187 188 190 191 192 194 197 201 204 205 | 41 |
| CLOSING-IN-FIX-WAVE | 125 129 151 152 182 186 189 195 196 203, and 219 (closes with the `m1-space` CI run) | 11 |
| BLOCKED-UPSTREAM | 84 86 87 217 | 4 |
| BLOCKED-LATER-MILESTONE | 50 117 118 183 216 | 5 |
| BLOCKED-DIRECTOR | 133 193 218 | 3 |
| SUB-PROJECT-5 | 115 123 198 199 | 4 |
| SUB-PROJECT-6 | 143 147 184 | 3 |
| STANDING-NOTE | none | 0 |
| ALREADY-DONE | none | 0 |

Rows whose "When" says M1 or sub-project 6 but don't need X or the blocking gate, so they are
FIXABLE-NOW: 63, 68/103, 94, 96 (M1); 116, 144, 145, 146, 148, 150 (sub-project 6). Rows parked
on a trigger that hasn't fired but cost little are FIXABLE-NOW too: 58 (M2's overload), 83 (the
real canvas), 205 (turned into a test that fires when the trigger does).

## 2. Every open row

Effort: S < 1 h, M 1–3 h, L > 3 h. "Task" is the grouping in §3.

### Not fixable now

| Row | Status | Notes |
|---|---|---|
| 50 "Overload-ladder hazards" (E) | BLOCKED-LATER-MILESTONE | The budget manager and grain stealing are M2 (INTENT §20). The pool's capacity admission already counts by end > onset (the min-heap, `8864401`; test `pool admission matches a brute-force count`), so only the budget and stealing halves remain. |
| 84 "clap_juce_shim.h lacks `<string>`" (P) | BLOCKED-UPSTREAM | Still absent at the pin (`libs/sst/sst-clap-helpers/include/sst/clap_juce_shim/clap_juce_shim.h:15-20`); workaround at `plugin/src/editor/EditorPlugin.h:9-10`. |
| 86 "auval Preset name is not retained" (P) | BLOCKED-UPSTREAM | Pinned by `gg-au-state-tests`; open until a clap-wrapper bump. |
| 87 "AUv2 SetParameter on an idle unit" (P) | BLOCKED-UPSTREAM | clap-wrapper `WrapAsAUV2::SetParameter`. |
| 115 "A seed loaded while active" (S1) | SUB-PROJECT-5 | Per-instance seeds. |
| 117 "`[rt]` live test's setSourceMode is a no-op" (S1) | BLOCKED-LATER-MILESTONE | M2: no product switches source mode until the resample source and layers. |
| 118 "A source-mode change hard-cuts" (S1) | BLOCKED-LATER-MILESTONE | M2, with 117. |
| 123 "The value-mirror race (M1-x)" (S1) | SUB-PROJECT-5 | UI value mirror. |
| 125 "huge-frame test comment" (S1) | CLOSING-IN-FIX-WAVE | |
| 129 "randomSplit's comment" (S1) | CLOSING-IN-FIX-WAVE | |
| 133 "sst-plugininfra's libs/asiosdk… Windows ASIO" (S1) | BLOCKED-DIRECTOR | Whether to re-enable Windows ASIO from ASIO SDK 2.3.4 (GPLv3 option); INTENT §22 keeps it open. |
| 143 "Make the CPU gate blocking, with the published X" (S2) | SUB-PROJECT-6 | Needs X on the M5 Max and baselines for unseen models; the M1 VM leg's status is part of it. |
| 147 "CI's warning branch trusts exit 3 alone" (S2) | SUB-PROJECT-6 | The warning branch (`.github/workflows/ci.yml:175-178`) is exactly what the blocking gate replaces; fix it in the same change. |
| 151 "gg-bench options silently ignored" (S2) | CLOSING-IN-FIX-WAVE | |
| 152 "gg-bench JSON escaping" (S2) | CLOSING-IN-FIX-WAVE | |
| 182 "gg-render exit 3 untested" (S3) | CLOSING-IN-FIX-WAVE | |
| 183 "Two notes on one (channel, key)" (S3) | BLOCKED-LATER-MILESTONE | M3 Voice scheduler. `held_` is a count array now (`core/include/gg/core/Processor.h:346`, `std::array<uint16_t,128>`), but per-note identity is still M3's. |
| 184 "Re-run Task 4's ratio check on an idle machine" (S3) | SUB-PROJECT-6 | Needs the idle reference machine, the same session as X's measurement. |
| 186 "State minors (MEDI version parse…)" (S3) | CLOSING-IN-FIX-WAVE | |
| 189 "r8brain minors" (S3) | CLOSING-IN-FIX-WAVE | Its Windows `-Werror` remark overlaps row 94 (task T4). |
| 193 "A newer MEDI version fails the whole state" (S3) | BLOCKED-DIRECTOR | Whether a newer manifest should load with no sample and a status, accepting or handling the save-over risk, before the state format freezes (INTENT §18, §23 #49). |
| 195 "Note minors (isNoteEvent)" (S3) | CLOSING-IN-FIX-WAVE | |
| 196 "The dev preset skips two tests" (S3) | CLOSING-IN-FIX-WAVE | |
| 198 "Loader strip gaps" (S3) | SUB-PROJECT-5 | Loader strip, replaced by sub-project 5. |
| 199 "Loader strip test gaps" (S3) | SUB-PROJECT-5 | |
| 203 "process_tests.cpp's stderr comments" (S3) | CLOSING-IN-FIX-WAVE | |
| 216 "Moving Reverb Size clicks" (S4) | BLOCKED-LATER-MILESTONE | M2's Space FX view (or an upstream Reverb 2 that smooths Size). Accepted as S8. |
| 217 "AU hosts never see the tail" (S4) | BLOCKED-UPSTREAM | `SupportsTail()` still at `libs/clap/clap-wrapper/src/detail/auv2/auv2_base_classes.h:391`. |
| 218 "Decay's label against the measured T60" (S4) | BLOCKED-DIRECTOR | A voicing call at the Space listening check (spec §9): recalibrate Decay (moves the Space goldens) or keep Reverb 2's formula. |
| 219 "The sst headers aren't SYSTEM" (S4) | CLOSING-IN-FIX-WAVE | Closes with the `m1-space` CI run in progress. |

### FIXABLE-NOW

| Row | Area | Files (HEAD) | Effort | Done means | Dep. | Task | Notes |
|---|---|---|---|---|---|---|---|
| 58 "clickScore can't see a click in dense material" (T) | metrics | `tools/audiometrics/src/Metrics.cpp:171` (clickScore), `tools/audiometrics/include/gg/metrics/Metrics.h`, `tests/metrics/metrics_tests.cpp`, `tests/golden/expected/*.metrics.json` (19 cases, rewritten), `tests/CMakeLists.txt` | L | `metricsVersion` 2; a test injects a −20 dBFS step into each golden render and requires it detected; the 0 dBFS spike in dense noise scores above the limit; `ctest -L golden` passes on the new expectations | after the fix wave (its goldens) | T2 | Parked on M2's overload, but needs nothing from it. Only task that rewrites golden expectations. |
| 62 "Detect non-finite band power" (T) | metrics | `tools/audiometrics/src/Metrics.cpp:107-123` | S | a test with a huge finite input reports non-finite band power and fails compare, not inf | 58 (same version bump) | T2 | |
| 63 "A spectrogram test of orientation and grayscale" (T) | metrics | `tests/metrics/metrics_tests.cpp` (beside `:320`) | S | test reads the PNG back (`libs/stb/stb_image.h`): 1 channel, a low tone lights bottom rows, a high tone top rows | – | T5 | |
| 66 "Type-check the spec keys a source kind doesn't use" (T) | tools-render | `tools/gg-render/RenderSpec.cpp:148-200` (`readSource`), `tests/tools/render_spec_tests.cpp` | S | a sine spec with `startHz: "x"` (or reject unused keys outright) exits 2; every case in `tests/golden/cases/` and `docs/listening/*/specs/` still parses | – | T5 | "In render-spec v2" is a parking spot; there is no v2 plan. |
| 68 "No same-platform audio-bits golden" (T) | docs | `INTENT.md` §19 (`:1119`, "Bit-exact comparison applies on the same platform") and a §23 entry | S (amend) / L (digest) | §19 states what is bit-exact (block-size invariance, SIMD vs scalar, event logs cross-platform) and what is metric-compared; §23 entry | – | T5 | Same item as 103. Recommend the amendment: a per-platform digest needs four platforms' digests refreshed by CI on every DSP change. Part II, so Claude may amend with a §23 entry; flag it in the PR. |
| 83 "Clamp px1/py1" (P) | ui | `ui/src/SplatRenderer.cpp:79-82` | S | integer clamp to `width_ - 1`/`height_ - 1`; a `[ui]` test at a width past 2^24 (one row) splats at the right edge, ASan-clean | – | T1 | Trigger is sub-project 5's canvas, but it's a 15-minute fix in a file SP5 reuses. |
| 94 "Add -Wall -Wextra, and -Werror on ClangCL" (B) | build-ci | `cmake/compiler-options.cmake:76-80`, `:83-89`, `:114-128` (`:121`); warning fixes in any first-party file | L | all three CI legs and asan/rtsan green with `-Wall -Wextra`, `-Werror` on ClangCL; `scripts/check.sh` PASS | fix wave (189) | T4 | Touches files every task touches: land last. Also closes 189's Windows `-Werror` remark. |
| 96 "A CPM source cache in CI" (B) | build-ci | `.github/workflows/ci.yml` (`CPM_SOURCE_CACHE` + `actions/cache`) | M | a second CI run shows a cache hit and no VST3 SDK/AudioUnitSDK/wil download | – | T4 | |
| 103 "Either add the digest or amend §19" (D) | docs | as 68 | – | as 68 | 68 | T5 | Duplicate of 68; close together. |
| 116 "Nothing automated keeps libs/asiosdk out" (S1) | build-ci | `libs/CMakeLists.txt:111-120` or a new `cmake/` guard called from `CMakeLists.txt` | S | configure walks every target's `SOURCES` and fails naming any under `asiosdk`; proven by a scratch-project CTest or a deliberate local failure recorded in the PR | – | T4 | "Sub-project 6, or sooner": nothing needs SP6. |
| 130 "check.sh stale format.log; worktree-setup preset names; …" (S1) | scripts | `scripts/check.sh:250-252`, `scripts/worktree-setup.sh:151-157` | M | `--no-format` removes the stale log; unknown preset names fail before cloning; one recorded run under Windows Git Bash (the `windows-vm` skill) and one with CTest 3.x, or a CLAUDE.md line saying they are unsupported | – | T5 | |
| 131 "check.sh's install guard accepts lower-case notfound" (S1) | scripts | `scripts/check.sh:160-163` | S | `notfound`, `x-notfound` are refused; `NOTFOUND`, `X-NOTFOUND`, `off` accepted (a small shell test, or recorded runs) | – | T5 | |
| 132 "check.sh aborts if binaryDir isn't build/<preset>; status comment omits U" (S1) | scripts | `scripts/check.sh:80-86`, `scripts/worktree-setup.sh:17-19` | S | binaryDir read from `CMakePresets.json` (or a summary line instead of an abort); comment lists `U` | – | T5 | The `-L` regex part is already documented in CLAUDE.md. |
| 144 "compare() must account for every baseline entry" (S2) | tools-bench | `tools/gg-bench/Bench.cpp:844` (`compare`), `tests/tools/bench_tests.cpp` | S | tests: a baseline entry the run lacks is a failure naming it; a verdict over 0 ratios is an error | fix wave (151, 152 touch the same files) | T3 | Needs no X. |
| 145 "Adding a model to a baseline needs the old runs" (S2) | tools-bench | `tools/gg-bench/Bench.cpp:886` (`makeBaseline`), `tools/gg-bench/main.cpp:553`, `tools/gg-bench/baselines/README.md`, CLAUDE.md ("Refreshing a baseline") | M | a merge mode (`--make-baseline … --merge old.json`) keeps other models' entries and replaces the runs' models; tested | 146 | T3 | |
| 146 "makeBaseline doesn't validate what it writes" (S2) | tools-bench | `tools/gg-bench/Bench.cpp:886` | S | a NaN ratio or tolerance makes it fail (exit 1, no file); tested | – | T3 | |
| 148 "The reference kernel isn't self-contained" (S2) | tools-bench | `tools/gg-bench/Kernels.h:36-37` (includes `gg/core/Interpolate.h`, `gg/core/Window.h`) | M | `Kernels.h` includes no engine header, or a `[bench]` test pins `renderHermiteSimd`'s output hash; CI ratios still pass their baselines | – | T3 | Copy verbatim so the baselines stay valid. |
| 150 "No test proves engine-sample-eco runs at Eco" (S2) | tools-bench | `tools/gg-bench/Bench.cpp:292-296`, `tests/tools/bench_tests.cpp:162-180` | M | a test reads the case's latched tier (from the pool rows or a case-params accessor) and requires `Tier::Eco` | – | T3 | Avoid an engine stats change so T1 keeps its files. |
| 153 "Tier{} value-initializes to Eco" (S2) | engine | `engine/include/gg/grain/Tier.h:16-20`; `engine/src/Engine.cpp:431`, `:450` (were `:300`, `:315`) | S | `Tier.h` documents both; goldens unchanged | – | T1 | |
| 154 "LiveSource::readLinear repeats read()" (S2) | engine | `engine/src/LiveSource.cpp:148`, `:193` | M | one shared template; `ctest -L 'kernel|live|engine|golden|determinism'` unchanged bit for bit; `gg-bench --cases reference,engine-live --quick` no slower | – | T1 | |
| 155 "rng::bipolar has no GG_FP_CONTRACT_OFF" (S2) | engine | `engine/include/gg/core/Rng.h:61` | S | guard or comment; `-L rng` passes | – | T1 | |
| 156 "sampleSimdRun's run-end stepping" (S2) | engine | `engine/include/gg/grain/GrainKernels.h:311-318`, `:347` | S | comment says the engine never makes a rate ≤ 0 or non-finite row (or an early bail-out with a kernel test) | – | T1 | The comment already says the stepping is bounded; the "never made" half is missing. |
| 157 "The kernels load gainR with one output" (S2) | engine | `engine/include/gg/grain/GrainKernels.h:275`, `:468`, `:586` | S | `gainR` loaded only with two outputs; kernel tests bit-exact | – | T1 | |
| 158 "Kernel-test gaps" (S2) | engine | `tests/engine/kernel_tests.cpp:838`, `:895-918` | M | a case with sources of 1–4 frames; `requireExactRuns` inside the 200-call loop (`:910-917`) | – | T1 | |
| 159 "The live-interior property test should pin the slack" (S2) | engine | `tests/engine/kernel_tests.cpp:1432-1517` | S | the test asserts margins ≥ M + 1 − 2^−10 at every interior sample | – | T1 | |
| 160 "Live-kernel comments" (S2) | engine | `engine/include/gg/grain/GrainKernels.h:740` (RingTaps), `:642` (liveRun) | S | both comments updated | – | T1 | |
| 161 "pool_tests' comment" (S2) | engine | `tests/engine/pool_tests.cpp:207` | S | reworded | – | T1 | |
| 162 "spike-doc paragraphs not re-wrapped" (S2) | docs | `docs/spikes/m1-cpu-levers.md` | S | no prose line over 100 columns (outside tables and code) | – | T5 | |
| 180 "clickScore is blind on the noise-burst medium" (S3) | metrics | `tests/golden/expected/fx-file*.metrics.json`, `fx-note-freeze*`; `tools/gg-render/Signals.cpp` stays as is | S | after 58, `loop-48k`, `fx-file` and `fx-note-freeze` score well under the limit and a click injected at a freeze transition is detected | 58 | T2 | The row offers the director two options, one being "wait for B-I1"; doing 58 takes it. If the bursts still score near the limit, return it to the director. |
| 181 "The spectrogram PNG writer opens its path narrow" (S3) | metrics | `tools/audiometrics/src/Spectrogram.cpp:65` | S | write via `stbi_write_png_to_func` into a stream opened from a `std::filesystem::path`; a test with a non-ASCII `--spectrogram` path passes on the Windows leg | – | T5 | |
| 185 "Deeply nested XML… pre-scan can be bypassed" (S3) | core-state | `core/src/XmlText.h:50-90` (`xmlNestingWithin`); tests in `tests/core/patch_tests.cpp`, `session_state_tests.cpp`, `settings_tests.cpp` | M | scan follows tinyxml's `Identify` (a `<` not followed by a name start is unknown, to the first `>`, quotes ignored) and refuses `<?` after the first node; both crash shapes are tests in PTCH, MEDI and settings, ASan-clean | fix wave (196 touches `session_state_tests.cpp`) | T6 | A crash; "before the first public release" but nothing blocks it. |
| 187 "Decoder minors" (S3) | media | `media/src/Decode.cpp` (`:176` growth, `:246` path conversion, `:571`), `engine/include/gg/source/SampleBuffer.h:20-31` (an adopting constructor) | L | FLAC/MP3/ADPCM peak ≈ decoded size (a test counting bytes allocated); ADPCM truncation without `fact` detected; RIFF data size 0 reads as truncated; one open per decode | 201 (path helper) | T7 | M2's memory cap bounding decode stays M2's (note it in the PR). |
| 188 "Media test gaps" (S3) | media | `tests/media/decode_tests.cpp`, `tests/media/search_tests.cpp`, `tests/core/settings_tests.cpp`, `tests/media/sample_tests.cpp` | M | RAII cleanup of the locked folder; R10's tolerance tested on AIFF; scan tests for `EPERM`, `EIO` (through a test hook) and a folder removed mid-scan; folders renamed | – | T8 | |
| 190 "Search and settings minors" (S3) | media | `media/src/Search.cpp:141-190`, `core/src/MachineSettings.cpp` | L | `findSample` returns the Changed path normalized like Found; the settings XML refuses declarations after the root (falls out of 185); NFC/NFD names match (tests with both spellings) | 185 (XML part) | T8 (XML part in T6) | NFC/NFD needs Unicode normalization: CFStringNormalize/NormalizeString per platform, or utf8proc (MIT; a new pinned dependency and an INTENT §15 row). |
| 191 "Settings save gaps" (S3) | media | `media/src/SettingsFile.cpp:140-165` | M | fsync before rename; stale `*.tmp` from crashed saves removed on the next save; Windows retries or uses `ReplaceFileW` while another instance holds the file (tested on the Windows leg) | – | T8 | |
| 192 "Repeated scans for one missing sample" (S3) | plugin | `plugin/src/SampleLoader.cpp` (`restore`) | M | a per-process negative cache keyed by fingerprint and folder, cleared on a load or a settings change; a test restoring N instances counts one scan | – | T9 | Not sub-project 5's code: the loader backend, not the strip. Keep the cache in the plugin layer so `Search.cpp` stays T8's. |
| 194 "Loader minors" (S3) | plugin | `plugin/src/SampleLoader.cpp` (deactivate, destructor, `convertNow` `:282-305`), `core/src/SourceExchange.cpp:77` | L | deactivate/destructor return promptly mid-decode (a cancel flag between chunks; tested with a long file); present-but-unreadable is Error; Missing/Changed rechecked on activate; `reserve()` grows geometrically | – | T9 | |
| 197 "Note-test minors" (S3) | core-state | `tests/core/note_tests.cpp:437` (was `:388`) | S | comment added; Freeze's base value checked at every note event | – | T6 | |
| 201 "UTF-8 path helpers are duplicated" (S3) | media | `media/src/Utf8Path.h:49` → a public header; `plugin/src/SampleLoader.cpp:47`, `plugin/src/editor/EditorPlugin.cpp:25`, `tools/gg-render/RenderSpec.cpp:553` + `RenderSpec.h:269`, `media/src/Decode.cpp:246`; test helpers in `tests/media/sample_tests.cpp`, `tests/plugin/factory_tests.cpp` → `tests/support/` | M | one helper; `grep -rn 'u8string(text.begin()'` finds only it; all tests pass on the three legs | – | T7 | |
| 204 "GrainSpec's operator== compares sourceSlot" (S3) | engine | `engine/include/gg/grain/GrainPool.h:37` | S | a comment saying so (the plan asked for it) | – | T1 | |
| 205 "toStateXml builds a Patch(Mode::Fx)" (S3) | core-state | `core/src/PatchState.cpp:11-22`, `tests/core/patch_tests.cpp` | S | a test that both modes' `Patch` writes byte-identical XML for random values, so the row's trigger becomes a failing test; then the row closes | – | T6 | Tests already include `core/src` (`tests/CMakeLists.txt:85`). |

## 3. Proposed tasks (9, for parallel worktrees)

All tasks branch from the Space fix wave's merged tip: the fix wave edits `tools/gg-bench/main.cpp`,
`tests/tools/bench_tests.cpp`, `plugin/src/GigaGrainsPlugin.cpp`, `tests/plugin/params_tests.cpp`,
`process_tests.cpp`, `rt_tests.cpp`, `tests/core/session_state_tests.cpp`, `libs/CMakeLists.txt`
and the goldens.

| Task | Rows | Files (main) | Effort | Deliverable |
|---|---|---|---|---|
| **T1 Engine and canvas tidy** | 83, 153, 154, 155, 156, 157, 158, 159, 160, 161, 204 | `engine/include/gg/grain/{Tier,GrainKernels,GrainPool}.h`, `engine/include/gg/core/Rng.h`, `engine/src/LiveSource.cpp`, `tests/engine/{kernel,pool}_tests.cpp`, `ui/src/SplatRenderer.cpp`, `tests/ui/` | ~5 h | engine output bit-identical (goldens and determinism unchanged with no regeneration), new kernel and splat tests, a `gg-bench --quick` ratio check for 154/157 |
| **T2 Local clickScore (metricsVersion 2)** | 58, 62, 180 | `tools/audiometrics/src/Metrics.cpp`, `Metrics.h`, `tests/metrics/metrics_tests.cpp`, `tests/golden/expected/*.metrics.json`, `tests/CMakeLists.txt` | ~5–6 h | the click-injection test over every golden; goldens regenerated with spectrograms checked; 180 closed or handed back with numbers |
| **T3 gg-bench baseline robustness** | 144, 145, 146, 148, 150 | `tools/gg-bench/{Bench.cpp,Bench.h,main.cpp,Kernels.h}`, `tools/gg-bench/baselines/README.md`, `tests/tools/bench_tests.cpp`, CLAUDE.md (Benchmarks) | ~6 h | `[bench]` tests for each; a CI run whose verdicts still pass (Kernels.h unchanged in output) |
| **T4 Warnings and CI hygiene** | 94, 96, 116 | `cmake/compiler-options.cmake`, `.github/workflows/ci.yml`, `libs/CMakeLists.txt` or a new `cmake/` guard, warning fixes anywhere | ~6 h (CI round trips) | three green legs with `-Wall -Wextra` (+ ClangCL `-Werror`), a cache hit on the second run, the asiosdk guard |
| **T5 Tools, scripts and docs** | 63, 66, 68+103, 130, 131, 132, 162, 181 | `tools/audiometrics/src/Spectrogram.cpp`, `tests/metrics/metrics_tests.cpp` (new case only), `tools/gg-render/RenderSpec.cpp` (`readSource`), `tests/tools/render_spec_tests.cpp`, `scripts/{check,worktree-setup}.sh`, `INTENT.md` §19/§23, `docs/spikes/m1-cpu-levers.md` | ~5–6 h | the spectrogram and render-spec tests; script behaviour shown by recorded runs (or a small shell test); §19 amended with a §23 entry |
| **T6 State XML hardening** | 185, 190 (settings-XML part), 197, 205 | `core/src/XmlText.h`, `core/src/MachineSettings.cpp` (if needed), `tests/core/{patch,session_state,settings,note}_tests.cpp` | ~4 h | both crash shapes refused in PTCH, MEDI and settings under ASan; the mode-identity test; note-test fixes |
| **T7 Decoder memory and one UTF-8 path helper** | 187, 201 | `media/src/Decode.cpp`, `media/src/Utf8Path.h` → `media/include/gg/media/`, `engine/include/gg/source/SampleBuffer.h`, `plugin/src/SampleLoader.cpp` (`pathOf` only), `plugin/src/editor/EditorPlugin.cpp`, `tools/gg-render/RenderSpec.{h,cpp}` (`pathFromUtf8` only), `tests/support/`, `tests/media/sample_tests.cpp`, `tests/plugin/factory_tests.cpp` | ~5–6 h | an allocation-peak test per codec, truncation tests, one helper, Windows leg green |
| **T8 Search and settings file** | 188, 190 (rest), 191 | `media/src/Search.cpp`, `media/src/SettingsFile.cpp`, `tests/media/{decode,search}_tests.cpp`, `tests/core/settings_tests.cpp` (folder names), maybe `libs/` + INTENT §15 (utf8proc) | ~6 h | NFC/NFD and Changed-path tests, the save-gap tests (Windows leg), the media test gaps closed |
| **T9 Sample loader** | 192, 194 | `plugin/src/SampleLoader.{h,cpp}` (`restore`, `deactivate`, `convertNow`), `core/src/SourceExchange.cpp`, `tests/plugin/` (loader tests) | ~5 h | a prompt deactivate mid-decode, the one-scan test, the Error status and recheck tests |

**Overlaps that can't be avoided:**
- `docs/m1-carry-over.md`: every task closes rows there. Have tasks leave it alone and list their
  closed rows in the PR; the integrator marks them **Closed** at merge.
- `tests/CMakeLists.txt`: T2 (click injection), T4 (asiosdk check, if a CTest), T5 (a script
  test, if added). Each adds lines only.
- `tests/metrics/metrics_tests.cpp`: T2 and T5 each add test cases only.
- `tools/gg-render/RenderSpec.cpp`: T5 (`readSource`) and T7 (`pathFromUtf8`), different
  functions.
- `plugin/src/SampleLoader.cpp`: T7 (`pathOf`) and T9 (`restore`, `deactivate`, `convertNow`).
  Merge T7 first, or let T9 rebase.
- `tests/core/settings_tests.cpp`: T6 (XML shapes) and T8 (folder names).
- `media/src/Search.cpp`: T8 only, if T9 keeps its negative cache in `SampleLoader`.
- CLAUDE.md: T3 (baseline merge) and T4 (warning flags). INTENT.md: T5 (§19) and T8 (§15, only
  if utf8proc is added).
- T4's warning fixes can touch any file: merge it last and rebase it onto the others.
- Goldens: only T2 rewrites expectations. T1 and T7 must leave every golden and determinism test
  unchanged without regeneration; that is their proof of no behaviour change.

After all nine land, the open rows are the 30 that aren't FIXABLE-NOW minus the 11 the fix wave
closes: 19 (4 upstream, 5 later milestones, 3 director, 4 sub-project 5, 3 sub-project 6).
