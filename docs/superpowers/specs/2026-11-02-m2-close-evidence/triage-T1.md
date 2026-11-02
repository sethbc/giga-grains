# T1 triage: carry-over rows 85, 87, 88, 144, 148, 184, 218, 232, 233, 234, 302–310, 330, 331, 333

Worktree `m2-close` at `0189dc9` (= `main`), 2026-11-02. Read-only: nothing built, edited or run in the
repository. Upstream checked with `gh api` on 2026-11-02 at about 23:00 UTC.

**Counts:** FIX 11 (232, 234, 302, 303, 304, 305, 306, 308, 309, 330, 331), KEEP-BLOCKED 9 (85, 87,
88, 144, 148, 184, 218, 233, 307), CLOSE-OBSOLETE 1 (310), DIRECTOR 1 (333).

**Four findings the controller should know before planning:**
1. **Row 310 failed again after its fix.** Run 41174234568 (head `1d62946`, which contains `5f1f9b5`)
   failed "deactivate and destruction cancel a load under way at once" natively on mac-universal
   (deactivate 155.2 ms against a limit of 139.0 ms, the whole job 555.9 ms). The retry version
   `a9e82d3` (three tries) came later and has been green since. Details under 310.
2. **Row 234's debug-only assert can't fire where the faults happen.** All six faults are in
   mac-universal's x86_64 slice, which is a **Release** build (`CMakePresets.json:56` inherits
   `release`, so `-DNDEBUG`), and `assert` compiles out there. Details and an always-on alternative
   under 234.
3. **Row 331's file is `ui/src/Editor.cpp`** (753 lines), not `plugin/src/editor/Editor.cpp`.
4. **Baseline runs right now:** dispatch 41543261855 (on `0189dc9`) is still running at 23:27 UTC.
   Its Windows build **failed** at "Build gg-all and gg-products" (23:17 UTC; the log isn't
   readable until the run ends), so its Windows benchmark was skipped. The Linux benchmark started
   at 23:17 and is still running; its CPU model will show once it finishes. One earlier full-length
   Linux run uses the same engine code: 41525070644 (push at `57ac152`, EPYC 9V45; `git diff
   57ac152 0189dc9 -- engine core media tools/gg-bench` is empty). The Windows and mac benchmark
   steps of that run hit the old 30-minute limit.

| Row | Class | One-line reason / plan | Size | Group |
|---|---|---|---|---|
| 85 | KEEP-BLOCKED | Our pin `f17567a` *is* sst-clap-helpers' HEAD, and its `clap_juce_shim.h` still lacks `<string>`. The workaround is already ours. | – | – |
| 87 | KEEP-BLOCKED | clap-wrapper `next` (15 commits past our pin) and v0.17.0 don't touch `_current_program_name`, `SaveState` or `RestoreState`. Pinned by the au tests. No workaround on our side. | – | – |
| 88 | KEEP-BLOCKED | `SetParameter` queues to the process adapter; `SaveState` (`wrapasauv2.cpp:2230`) still doesn't drain it. Upstream has no fix. The value never reaches our code, so we can't work around it. | – | – |
| 144 | KEEP-BLOCKED | INTENT §23 #60(1) and #61 schedule it for 1.0.0, after M5. | – | – |
| 148 | KEEP-BLOCKED | Same as 144 (#60). It's tiny: the controller could pull it forward, but that needs a §23 entry. | (S) | with 144 |
| 184 | KEEP-BLOCKED | `held_` is still one bit per (channel, key) (`Processor.h:1118`, `std::array<uint16_t,128>`). M3's Voice scheduler (INTENT §20 M3, line 1159). | – | – |
| 218 | KEEP-BLOCKED | `SupportsTail()` is still `return false` in clap-wrapper `next`. We can't override the wrapper's class. | – | – |
| 232 | FIX | Local workaround: when the shim kept an unshown editor, `EditorPlugin::guiDestroy` replaces the shim (keeping its scale), which destroys the kept editor. | S | shim (alone) |
| 233 | KEEP-BLOCKED | clap-validator's latest release is still 0.4.1. At HEAD, `src/plugin/preset_discovery/indexer.rs:178` still requires a leading `/`. Any workaround would hand hosts unopenable paths. | – | – |
| 234 | FIX | Add the agreement and grid asserts in `renderLayerPiece`, plus an always-on fault counter (assert alone is compiled out on mac-universal). Record run 41499519821. Make the exit-case test compare counts before samples. The row then stays open as the flake's record. | M | rosetta (alone) |
| 302 | FIX | Still live: the reverb input isn't faded, and `kReverbOnset` is still in `SweepDriver.h:748-752`; sub-project 4 didn't touch it. Remedy: Processor fades the reverb's input over 10 ms after an idle-to-active transition at t > 0. No golden can move. | M | sweep (302, 303, 309) |
| 303 | FIX | `LiveSource::kGuardFrames` is still private (`LiveSource.h:518`). There are now three copies of 15 (RingStorage, LiveSource, SweepDriver) and two of `unit`. Use `RingStorage::kGuardFrames`, one `unit`, and delete the copies. No golden can move. | S | sweep |
| 304 | FIX | Delete the stale SSE-era EPYC 9V74 Windows entry (`8eb0613`, 12 ratios). Re-adding it falls under 144's "unseen models" at 1.0, or under a remake if three runs land on that model. | S | bench (304, 305) |
| 305 | FIX | The close's baseline pass: three full-length runs per x86 leg on HEAD's engine code. Windows is blocked first by the dispatch's build failure. Models drawn fewer than 3 times keep the 0.10 floor and fold into 144. | M (wall-clock) | bench |
| 306 | FIX | No finiteness scrub in `media/src/` (only `isfinite` checks on rates). Zero any non-finite decoded sample in `decodeAudioFile` and in the ring audio reader. No golden can move. | S | media (alone) |
| 307 | KEEP-BLOCKED | INTENT #66 ES-T8b: "provisional until M5's calibration"; M5's exit is "Budget weights calibrated" (INTENT §20). The close's idle run reports the HQ/Normal ratio without moving the weights. | – (report: S) | bench |
| 308 | FIX | The README items are still open (`docs/listening/m2-engine/README.md:56` and others). Fix the README. The question for the director goes into the M2 close's listening checklist; the row closes after the sitting. | S | listening docs |
| 309 | FIX | Still live (`SweepDriver.h` is now 963 lines in 5 TUs; `sizeBase_` at `:627`; `reinterpret_cast` plant at `sweep_params_tests.cpp:451`). The driver changes with 302, which meets the row's own trigger. | M | sweep |
| 310 | CLOSE-OBSOLETE | Confirmed by CI with `a9e82d3` (three tries), not by `5f1f9b5` alone. Green on mac-universal since (for example run 41499519821: Passed 2.70 s). | – | – |
| 330 | FIX | (a) a loaded out-of-range depth is clamped on first touch: widen the slider range to the loaded depth. (b) drag-and-drop is in fact tested headlessly now. (c) stock JUCE widgets is a look choice: a director line. | S | ui (330, 331, 333) |
| 331 | FIX | Split `ui/src/Editor.cpp`'s header into `ui/src/views/Header.{h,cpp}`, keeping every `header.*` component ID. | M | ui |
| 333 | DIRECTOR | A behavior choice. Option 1 (recommended): auto-select only when exactly one route appeared (an undo, redo or drop), not on a load. | S | ui |

---

## 85: sst-clap-helpers' missing `#include <string>`: KEEP-BLOCKED

- **Our workaround:** `plugin/src/editor/EditorPlugin.h:9-10`: "Before clap_juce_shim.h: sst-clap-helpers'
  clap_juce_shim.h uses std::string without <string>." followed by `#include <string>`.
- **Upstream:** `gh api repos/surge-synthesizer/sst-clap-helpers/commits`. The newest commit is `f17567a`
  (2026-07-19, "Reapply remembered GUI scale…"), which is our pin. So nothing upstream is newer.
- **Upstream header at HEAD:** `include/sst/clap_juce_shim/clap_juce_shim.h` includes only
  `<functional> <memory> <cstdint>` and the clap headers.
- **Unblocks:** an sst-clap-helpers commit that adds `<string>`. No bump is possible today. The
  workaround already lives in our code and costs nothing, so there is nothing to fix on our side.

## 87: AU "Preset name is not retained", SaveState ignores save(), RestoreState returns noErr: KEEP-BLOCKED

- **Our pin:** `4e7afc3` is on clap-wrapper's `next` branch, 15 commits ahead of `main` and of v0.17.0
  (`gh api compare`).
- **`next` past our pin:** 15 commits (ARA, AUv3, standalone URL scheme, VST3 null context, an AUv2
  values-only param rescan). Its `wrapasauv2.cpp` diff touches `param_rescan`, `GetPropertyInfo`
  and `GetProperty` (ARA). It touches neither `_current_program_name` (still only set in
  `RestoreState`, `wrapasauv2.cpp:2283-2289`, `:2347`) nor `SaveState`/`RestoreState`.
- **Pins:** still in `tests/plugin/au_state_tests.cpp:401` and `:467`.
- **Cost of a bump:** it buys nothing here. It would also cost the `identity_*` checks, a
  `gg-au-state-tests` run and the AU pins being re-read, plus checking the AUv2 rescan change
  against our parameters.
- **Workaround:** none in our code. The program name lives in the wrapper.
- **Unblocks:** a clap-wrapper commit that changes these. The pins then fail and are updated.

## 88: AUv2 `SetParameter` on an idle initialized unit is lost by a save before render: KEEP-BLOCKED

- **At our pin:** `WrapAsAUV2::SetParameter` (`wrapasauv2.cpp:787-836`) queues the event on
  `_processAdapter`, or on the flush adapter when deactivated, and sets `_requestedFlush` for
  `onIdle()`.
- **The gap:** `SaveState` (`:2230-2245`) calls `_ext._state->save` without draining the queue,
  so the gap stands. Upstream `next` has nothing past our pin for `SetParameter` or `SaveState`.
- **Workaround:** none. The value never reaches the plugin before `save()`, so our state can't
  see it.
- **Unblocks:** a clap-wrapper change to `SetParameter`, or a `SaveState` that flushes first.

## 144: blocking CPU gate, published X, unseen-model baselines: KEEP-BLOCKED

- **Schedule:** INTENT §23 #60 (1): "The CPU gate stays report-only through M1 and turns blocking at
  1.0 … carry-over rows 144 and 148 move to 'At 1.0'". #61 moves 1.0 to after M5.
- **Unblocks:** 1.0.0. The close's baseline pass (304, 305) feeds it but doesn't close it.

## 148: CI's warning branch trusts exit 3 alone: KEEP-BLOCKED

- **Code:** `.github/workflows/ci.yml:755-758`: `if [[ $status == 3 ]]; then echo "::warning …"; status=0`.
  The row's old line 171 has moved.
- **Schedule:** 1.0.0, with 144 (#60).
- **If pulled forward:** it's S, needs a §23 note, and moves no golden. Inside the `status == 3`
  branch, require `grep -q '^verdict: '` on `$out.txt` (CR-stripped, as the summary grep is), and
  fail with a `::error` otherwise.

## 184: two notes on one (channel, key) share one held bit: KEEP-BLOCKED

- **Code:** `core/include/gg/core/Processor.h:1118` `std::array<uint16_t, 128> held_{}`, one bit per
  channel per key.
- **Schedule:** INTENT line 1159 ("note-off releases both; M3's Voice scheduler revisits this") and
  the M3 row (line 3125).
- **Unblocks:** M3.

## 218: AU hosts never see the tail: KEEP-BLOCKED

- **At our pin:** `libs/clap/clap-wrapper/src/detail/auv2/auv2_base_classes.h:391-394`:
  `virtual bool SupportsTail() override { return false; }`. Unchanged on `next` (`gh api contents
  …?ref=next`, line 391).
- **Workaround:** none in our code: the wrapper's class owns the override.
- **Unblocks:** a clap-wrapper release with AUv2 tail support.

## 232: the shim keeps an editor a host never showed: FIX

- **Upstream:** our pin = HEAD (see 85). `ClapJuceShim::guiDestroy` → `Implementor::destroy()`
  (`clap_juce_shim_impl.cpp:132-143`) frees the editor only `if (guiParentAttached && implDesktop &&
  editor)`. The next `guiCreate` → `setContents` hits `jassert(!editor)` (`:121`).
- **What we have:** our `EditorPlugin::guiDestroy` (`plugin/src/editor/EditorPlugin.cpp:622-635`)
  already turns publishing off, and the test "publishing follows gui destroy without a parent"
  (`tests/plugin/factory_tests.cpp:1335`) says: "A second create would replace it, which the shim
  asserts against in a debug build."
- **Plan:**
  - In `EditorPlugin::guiDestroy`, read `const bool kept = shimEditor_ != nullptr &&
    !clapJuceShim->isEditorAttached();` before `clapJuceShim->guiDestroy()`.
  - After it, if `kept`, replace the shim (main thread):
    `const double scale = clapJuceShim->getGuiScale(); clapJuceShim =
    std::make_unique<sst::clap_juce_shim::ClapJuceShim>(this); clapJuceShim->setResizable(true);
    if (scale != 1.0) clapJuceShim->guiSetScale(scale);`.
  - `guiSetScale` with no editor is safe: `applyGuiScale` checks `edHolder() && desktop()`
    (`:367-377`).
  - Destroying the old shim destroys `Implementor`. Its `editor` is declared last, so it goes first,
    before the holders and the `ScopedJuceInitialiser_GUI`, exactly as at plugin destruction today.
  - The editor's own `publishing(false)` is a no-op, because the counted flag is already false.
  - Factor the construction at `EditorPlugin.cpp:146-147` into a helper so the two sites agree.
- **Test:** `[plugin]` (editor builds only) "a gui created and destroyed without a parent, then
  created again, holds one editor".
  - Create, destroy, create, destroy.
  - Assert the first editor's destructor ran before the second `createEditor` (a test-only counter
    of live editors in `EditorPlugin`, or a weak flag captured in `createEditor`).
  - Keep the existing test.
- **Risk:** no audio thread, no golden ("no golden can move because the change is in the editor's
  lifecycle on the main thread").
- **Size:** S. Group: alone. Run the GUI tests and `snap_cli` on macOS.

## 233: clap-validator rejects Windows preset locations: KEEP-BLOCKED

- **Releases:** `gh api repos/free-audio/clap-validator/releases` lists 0.4.1 (2026-07-22) as the
  newest. `scripts/fetch-validators.sh:21-22` pins 0.4.1 / `0.4.1-127-g152b982`; master has 2
  fuzzer commits past it (`98fca59`, `b2f1d9b`).
- **At HEAD:** `src/plugin/preset_discovery/indexer.rs:178-179`: `if !path_str.starts_with('/') {
  anyhow::bail!("'{path_str}' should be an absolute path, i.e. '/{path_str}'.")`. Still unfixed.
- **Workaround:** declaring `/C:\…` would satisfy the validator and break every host, which isn't
  sane. CI's exclusion (`ci.yml`, the clap-validator step) stays.
- **Unblocks:** a clap-validator release that accepts `C:\…`. Then pin it and drop the Windows
  exclusion.

## 234: Rosetta transient faults: FIX (the diagnostic), row then kept as the flake's record

### The new fault to record in the row

- **Run:** 41499519821, attempt 1 (PR #27, head `bc83fb8`, `ci-bench-quick-on-main`, 2026-11-02),
  job `build (mac-universal)`, the x86_64 slice under Rosetta.
- **The failure:** `tests/engine/engine_tests.cpp:2153: FAILED: REQUIRE( sameBits(r.audio.left,
  ref.audio.left) )` in "the exit case never clicks or drops out" (`:2193`), with messages "window
  RMS floor -36.3796 dBFS, click score 1.67172, stolen 3072, dropped 28672", "blocks of 1". Catch2
  summary: 1379 cases, 1 failed.
- **The re-run:** attempt 2 passed (the run's conclusion is success).
- **Context:** at the time the PR runs still included `[slow]` under Rosetta. §23 #75 later moved
  `[slow]` to the nightly, a dispatch and a tag, so this test now meets Rosetta less often.

### `renderLayerPiece` and where the assert goes

- **The flip:** `core/src/Processor.cpp:1821-1845`. `renders = rendering_[L]` (set at `:2049-2052`
  as `on.to != 0.0f || on.at(from, rampSamples_) != 0.0f`, before any task runs). The flip is the
  block `if (renders != l.enabledGiven) { l.engine.setEnabled(renders); l.enabledGiven = renders; … }`
  at `:1826`.
- **The fault matches a flip:** in the first fault (run 41122974832) layer C, off throughout
  `fx-sine`, spawned one grain at onset 26625 = 832·32 + 1, one frame off the grid. An off layer
  spawns only if its engine was enabled, and enabling forces the pending onset to the piece's
  start (the comment at `:1828-1829`). So that was a flip **on**, at an off-grid `from`, with
  `on.to == 0` and `on.at(from) == 0`.
- **When a flip may be off the grid:**
  - When a ramp off ends: `zero = on.start + rampSamples_` (the piece is cut there,
    `:2018-2030`). `rampSamples_` = round(0.01 × rate) is 480 at 48 kHz (on the grid) but 441 at
    44.1 kHz (off it).
  - After a `jump` (setBase or setBases between calls, or the constructor or reset), which sets
    `start = 0` (`Ramp::jump`, `:208-213`). setBase is legal between `process()` calls at any
    clock (`Processor.h:575-577`).
  - Retargets happen at grid boundaries.
- **The asserts:** place them at `:1826`, inside the flip branch, before `l.engine.setEnabled(renders)`:

  ```cpp
  // Row 234: a flip on needs a live on-ramp (the expression rendering_[L] was computed from,
  // re-evaluated from state no task writes), and a flip lands off the control grid only where a
  // ramp off reaches 0 (on.start + R) or after a jump (setBase, setBases, reset: start 0).
  assert(renders == (l.on.to != 0.0f || l.on.at(from, rampSamples_) != 0.0f));
  assert(from % static_cast<int64_t>(kControlGrid) == 0 || l.on.start == 0 ||
         (!renders && from == l.on.start + rampSamples_));
  ```

  Core already uses plain `<cassert>` `assert` (`Processor.cpp:10` includes it; `:639`, `:646`,
  `:1580`, `:1608`; `LivePeaks.cpp`, `SpaceReverb.cpp`). There's no project assert macro, so use
  `assert`.
- **Reading a failure:**
  - If the first assert fails, the same expression gave two answers from unchanged state. With
    `SerialRunner` (gg-render) that is a machine/VM fault. With a pool it could also be a missing
    happens-before in `TaskRunner`, so name the runner in the message.
  - If the first holds but the second fails, the on-ramp itself moved off the grid: a code fault.
- **Validating the asserts:** the builder must run the whole `dev` suite. Block-size invariance,
  the sweep, every golden, and 44.1 kHz cases such as `sparse-sine-44k1` are sample mode, so use
  any FX or instrument case at 44.1 kHz in a unit test. No legitimate site may fire. If one
  does, name it in the condition, don't loosen the assert.

### How a debug-only assert behaves, and why it's not enough here

- **Where it runs:** `assert` is active only without `NDEBUG`, which means `dev` and `asan` (both
  `CMAKE_BUILD_TYPE` Debug, `CMakePresets.json:20`, `:36`). A failure calls `abort()`, so the ctest
  case or Catch2 shard dies with SIGABRT and the failing expression on stderr.
- **Where it doesn't:** `release`, `mac-universal` (inherits `release`, `:56-57`), `linux-gcc13`,
  `windows-clangcl` (`:69`) and `rtsan` (RelWithDebInfo, `:47`, which CMake also builds with
  `-DNDEBUG`) all compile it out.
- **The consequence:** every Rosetta fault so far is in mac-universal's x86_64 slice, a Release
  build, so the assert as specified can never fire where the faults occur. It only proves that the
  dev and asan suites never reach the condition.
- **Recommendation (part of this FIX, S on top of the assert):** an always-on, RT-safe counter
  beside the asserts.
  - Add `uint32_t enableFaults_` (relaxed, audio thread only), incremented when either condition
    is false (two counters, or a bitfield, so the kind survives), with an accessor
    `Processor::enableFaults()`.
  - It costs one branch on the rare flip path only.
  - `gg::render::render` returns it in its result.
  - The flaking tests REQUIRE it is 0 with a message naming the kind:
    - "render block size does not change output" (`tests/tools/render_spec_tests.cpp:427`);
    - "renders are identical with no pool and pools of 1, 2 and 8 workers"
      (`tests/core/threads_tests.cpp:144`);
    - the core block-size tests.
  - A recurrence on Rosetta then says code or VM in Release.
  - **Decision needed:** this goes past the brief's "debug-only", so the controller decides whether
    to add it.

### Could `engine_tests.cpp:2153` share a cause?

- **Not this mechanism:** `renderExit` (`tests/engine/engine_tests.cpp:2112-2126`) drives a bare
  `gg::Engine`, with no `Processor`, no layers, no `setEnabled` and no on-ramp. So
  `renderLayerPiece` and both asserts are outside its path.
- **The same pattern, though:** x86_64 under Rosetta, blocks of 1, one occurrence, gone on re-run.
  That pattern sits below the Processor, in the engine's scheduler, ladder or kernels at block size
  1, or in the VM. It also matches the three Sinc-kernel faults, which are engine-level too.
- **What it suggests:** the cause is engine-level or the VM, not the layer enable.
- **A cheap diagnostic for it:**
  - In `checkExitCase` (`:2146-2157`), compare `dropped`, `lowered` and `stolen` (CHECK, not
    REQUIRE) **before** the audio, and INFO the first differing sample index.
  - Counts that differ mean the ladder's decisions diverged (scheduling); equal counts with
    differing samples mean arithmetic diverged. The run here reported only the reference's counts,
    because the audio REQUIRE fired first.
- **Size:** M (asserts, counter, test plumbing, row text). No golden can move: the asserts and the
  counter read state and write nothing the render uses. Run `update-goldens.sh` before and after
  to show every hash unchanged. Touches the audio thread (the counter increment), so run RTSan.
  Group: alone.

## 302: Reverb Amount from exactly 0 clicks: FIX

### Is it still live at HEAD?

- **Yes, by code:**
  - The reverb's send is `rIn[c] = fx ? m * wet[c] + s * d[c] : wet[c]` (`core/src/Processor.cpp:2176`),
    which Amount doesn't scale; only the return is scaled, `softLimit(a * r[c])` (`:2208-2209`).
  - `SpaceReverb` documents that the first active chunk after idle "starts from an empty room"
    (`core/include/gg/core/SpaceReverb.h:62-64`).
- **Sub-project 4 didn't touch it:** since the sweep landed (`1c7b393`), the only change to
  `SpaceReverb` or `core/src/space/` is `2b6f321` (Modulation glide, row 311), which doesn't touch
  the onset.
- **The exemption is still in force:**
  - The comment at `tests/sweep/SweepDriver.h:26-29`, `Exemption::ReverbOnset` (`:175`) and
    `kReverbOnsetExempt = 28800` (`:200`).
  - It's applied at `:748-752` (`if (active && !reverbWasActive_) exempt_.push_back({callStart - 64,
    clock_ + predelay + kReverbOnsetExempt, Exemption::ReverbOnset})`).
  - Its share is reported in `tests/sweep/sweep_extremes_tests.cpp:63`.
- **Confirm the red step first:** I couldn't run it. The builder deletes the exemption first and
  runs `ctest -L sweep`, expecting the 15–20 scores.

### Remedy (recommended)

- **What:** in the `Processor`, fade the reverb's **input** in after an idle-to-active transition.
- **How:**
  - At each sample with `t % kControlGrid == 0`, track whether the chunk now starting is active
    (`active`, `:2152`) against the last chunk's (a new member, reset by `reset()`).
  - When it goes from idle to active **and t > 0** (that is, not the first chunk after
    construction or reset), latch `reverbOnsetAt_ = t`.
  - Multiply `rIn[c]` by a raised cosine H((t − onset) / F), 1 from F on, with F = 10 ms
    (`kRampSeconds`, the FX inputs' M1-v shape).
  - Read H from a table or a deterministic expression, with no libm on the audio thread. The Hann
    rising-half table the engine uses for its fades fits.
- **Why it's deterministic:** it depends on the chunk sequence alone, as the idle rule does, so it
  is block-size invariant. `SpaceReverb`'s own law ("from there the output is exactly a fresh
  instance's for the same input") is untouched at its level, and its tests stay.
- **If 10 ms doesn't get the sweep under 15:** lengthen F (for example 30 ms, the Modulation
  glide's), measured.

### Alternatives

- **(b) Keep the reverb running at Amount 0:** drop the idle rule while keeping the
  `chunkActive()`-guarded add so Amount-0 output stays bit-exact. Costs: Space's ~0.14% of a core
  always, and a room that holds the past at Amount 0 (a stale infinite-Decay room returns when
  Amount rises). Recorded laws R-T5a and R-T5b change.
- **(c) Scale the send by Amount:** this moves every golden with Amount > 0, and changes the sound.
  Rejected.

### Goldens

- **No golden can move under the recommended remedy.** No case raises Reverb Amount from exactly 0
  after t = 0:
  - Amount's default is 0 (`core/src/ParamIds.cpp:372`).
  - No case or preset automates it (no `"param": "Reverb Amount"` in `tests/golden/`).
  - It has no factory route (no `ReverbAmount` in core but `Mutate.cpp`'s lock, `ParamMap.cpp`
    and `Processor.cpp:414`).
  - The only routes to it (FX presets "08 Still Water" and "10 Harmonic Hold", Env 2) sit on a
    base of 0.3, so they're active from t = 0.
  - Every case with Amount > 0 is active from its first chunk, which the fade leaves alone.
- **Prove it:** with `update-goldens.sh` before and after. All 108 WAV hashes are expected
  unchanged.

### Plan

- **Code:** `core/src/Processor.cpp` (the reverb send in the per-sample loop) and
  `core/include/gg/core/Processor.h` (the member and its doc).
- **Delete:** `Exemption::ReverbOnset`, `kReverbOnsetExempt`, `reverbWasActive_` and the comment
  from `SweepDriver.h`, plus `sweep_extremes_tests.cpp:63`'s share. Raise any floor it lowered.
- **New test:** `[core][space]` "raising Reverb Amount from 0 under a sounding input doesn't click"
  (an FX `Processor` over a 220 Hz sine and over noise, Amount 0 → 1 at 1 s, pre-delay 0 and
  0.3 s, click score < 15, at blocks 1, 32 and 511 bit-identical), plus the sweep family green with
  no exemption.
- **Records:** the Space spec and INTENT §23 get an entry (Part II: the send's onset fade).
- **Checks:** this touches the audio thread, so run RTSan and the tripwire.
- **Size:** M. Group: sweep (302 + 303 + 309, all in `SweepDriver.h`).

## 303: `LiveSource::kGuardFrames` private; duplicate `unit`: FIX

- **At HEAD:** `engine/include/gg/source/LiveSource.h:518` `static constexpr int64_t kGuardFrames = 15;`
  sits under `private:` (`:514`), and `:539` holds a private `static double unit(double)`.
- **The copies:**
  - `engine/include/gg/grain/GrainKernels.h:157` `inline double unit(double x)` in `detail` ("LiveSource::fade's
    clamp"; used by `tests/engine/kernel_tests.cpp:908-909`).
  - `tests/sweep/SweepDriver.h:191` `kLiveGuardFrames = 15` (used at `:480` and
    `sweep_params_tests.cpp:434`).
  - New since the row: `engine/include/gg/source/RingStorage.h:76-78` already has a **public**
    `RingStorage::kGuardFrames = 15` ("LiveSource's kGuardFrames"), so 15 lives in three places.
- **Plan:**
  - In `LiveSource.h`, make it `static constexpr int64_t kGuardFrames = RingStorage::kGuardFrames;`
    (it includes `RingStorage.h`) and public.
  - Move `unit` to `gg::detail` in `LiveSource.h` (GrainKernels includes LiveSource), have
    LiveSource call it, and delete the GrainKernels copy.
  - Replace `SweepDriver::kLiveGuardFrames` with `gg::RingStorage::kGuardFrames`.
- **Test:** the existing kernel and sweep tests (the ring-guard plant still catches a drift), plus
  `static_assert(LiveSource::kGuardFrames == RingStorage::kGuardFrames)`.
- **Goldens:** no golden can move because the constants and the function body are identical (a
  refactor). `update-goldens.sh` before and after shows every hash unchanged.
- **Size:** S. Group: sweep.

## 304: the stale EPYC 9V74 Windows entry: FIX

- **The stale entry:** `tools/gg-bench/baselines/windows-clangcl.json` has `AMD EPYC 9V74` from
  `8eb0613`, the SSE-era code, with 12 ratios (the others have 30) and tolerance 0.10.
- **Waiting for three runs on that model is outside our control.** A stale entry gives false
  verdicts, while a missing one prints "no verdict", which is honest.
- **Plan:**
  - Delete the 9V74 entry in the close's baseline pass, unless three full-length Windows runs land
    on that model, in which case remake it with `--merge`.
  - Note in `tools/gg-bench/baselines/README.md` and the commit that 9V74 now gets "no verdict"
    until runs land. Its re-entry is row 144's "unseen models" (1.0).
- **Test:** `bench_baseline_*` is unaffected. Check by reading the file with gg-bench's own
  `--baseline` on a local JSON.
- **Size:** S. Group: bench (304 + 305; with the Linux remake the brief names).

## 305: every Windows entry is single-run: FIX (the close's baseline pass)

- **Every x86 entry is stale against HEAD, not only Windows'.** All are from `630d740`, and the
  engine has changed since (sub-projects 2 and 4). The one full-length Linux run at HEAD-equal engine
  code (41525070644, EPYC 9V45) gives "verdict: 3 of 42 ratios regressed" (`engine-layers-full`
  32 at 1.5340 against limit 1.4806; 64 at 1.8451 against 1.3257; `engine-sample-sse` 64 at 1.2565
  against 1.2556). The 12 newer report-only cases have no entries.
- **Linux (feasible this close):**
  - Run 1 is 41525070644 (EPYC 9V45, `57ac152`, engine code identical to `0189dc9`).
  - Run 2 is 41543261855 (dispatch, running; model unknown until it ends).
  - Run 3 needs one more `gh workflow run ci.yml --ref main`, or the nightly.
  - Then `--make-baseline linux-gcc13 … --merge …`.
  - Models drawn fewer than 3 times keep the 0.10 floor. Explain each `(replaced)` entry's moved
    ratios (the three regressions above are the sub-projects' added work, to be confirmed against
    the `--chain`/engine changes).
- **Windows (blocked first by the build):**
  - 41525070644's Windows benchmark timed out at the old 30-minute limit (21:28 to 21:58 UTC).
  - 41543261855's Windows **build** failed (23:17 UTC). That has to be diagnosed and fixed first,
    likely the Ninja move (#29, merged as `0189dc9`), and it isn't a carry-over row of mine.
  - Then three dispatches under the 60-minute limit.
- **Size:** M in wall-clock (hours of CI), S in effort. No golden can move (baselines only).
  Group: bench. Whatever stays single-run after the pass is recorded and folds into 144.

## 306: non-finite samples from a decoded float file reach the output: FIX

- **At HEAD:** `git grep isfinite -- media/` finds only rate checks (`Convert.cpp:32`,
  `RingAudio.cpp:124,168`, `Sample.cpp:120`): no sample scrub. The live ring sanitizes its input
  (`LiveSource.h:48-50`); files don't. `Mipmap.h:94` states the finite-input precondition.
- **Plan:**
  - In `media/src/Decode.cpp`'s `decodeAudioFile` (`:774`) success path, after the f32 read
    (`drwav_read_pcm_frames_f32` `:622`, `drflac…` `:708`, `drmp3…` `:739`), make one pass over the
    decoded buffer replacing any non-finite sample with 0. Integer formats can't produce one, so the
    pass could be gated on the float formats, but one pass costs little next to the decode.
  - Do the same in the ring audio reader (`media/src/RingAudio.cpp`): a tampered float32 ring WAV
    could carry a NaN.
  - Leave finite values unclamped, so nothing else changes. Clamping to `kMaxInputMagnitude` is
    optional, and would follow the live ring's law.
  - The SHA-256 fingerprint is of the file's bytes, so it's unchanged.
- **Tests:** `[media]` "a decoded float file's non-finite samples are read as 0": a float32 WAV and
  AIFF written at run time by `tests/media/AudioFixtures.h` with NaN, +inf and −inf frames; check
  zeros there and the others bit-identical. Plus a ring-audio twin.
- **Goldens:** no golden can move because every golden's audio is finite (generated media and the
  content's PCM files), and replacing only non-finite values is the identity on them. Prove it with
  `update-goldens.sh` before and after.
- **Thread:** loader thread, not the audio thread.
- **Size:** S. Group: media (alone).

## 307: HQ's weights are provisional: KEEP-BLOCKED (to M5), with a report-only measurement now

- **Schedule:**
  - INTENT §23 #66, ES-T8b: "HQ is {20 mono, 27 stereo}, the mono column proportional and
    provisional until M5's calibration".
  - §14 (line 675): "The weights never change at run time", and line 1730: "until M5's calibration
    (§14)".
  - The M5 row (line 3127): exit "Budget weights calibrated."
- **Feasible in the close:**
  - From the close's idle run on the reference core, report `engine-sample-hq` over `engine-sample`
    per block (the idle run at `ab0bbe5` gave 6.8, 7.4 and 7.8 against the table's basis of 6.79).
  - From the x86 full-length JSONs (41525070644, plus 305's runs), report the same ratio per leg
    and model.
- **Moving the weights now** changes the budget's arithmetic and moves `fx-overload`,
  `inst-overload` (WAVs and steal onsets in their event logs) and the overload sweeps' floors. M5
  re-measures across the legs anyway, so don't move them in the close.
- **Size of the report:** S. No golden moves. **Unblocks:** M5's calibration.

## 308: the listening pack's open items: FIX (README) + the sitting

- **Still open in `docs/listening/m2-engine/README.md`:**
  - `:56`: "a faint whistle at −67 dB on the spectrogram; with HQ it is down at −108 dB", read off
    the image.
  - `:55`: `02`'s RMS spans aren't stated.
  - `:62-75`: the three questions, without the HQ broadband floor at stretch 0.67.
- **Plan:**
  - State the method (or "read off the spectrogram, not reproducible from the pack's commands")
    for the −67/−108 dB and the image/fold-back figures.
  - Give `02`'s RMS spans (the seconds each figure covers).
  - Add HQ's broadband floor at stretch 0.67 (about −99.5 dB in `fx-quality-switch`) to the
    questions.
  - Put the residual-rate question (`floor(log2 r)`, ES-T1a; `02`'s +19 step) on the M2 close's
    listening checklist for the director.
- **Closing:** the row closes when the director has answered at the sitting. Docs only, no golden.
- **Size:** S. Group: listening docs (with the close's listening pack).

## 309: latent gaps in the sweep driver: FIX (with 302)

- **The row's trigger:** "Fix when a case reaches one, or the driver next changes". 302 and 303
  change the driver.
- **At HEAD:**
  - `tests/sweep/SweepDriver.h` is now 963 lines (was 797), included by 5 TUs
    (`sweep_{bursts,extremes,keying,overload,params}_tests.cpp`).
  - `sizeBase_` is updated only through `noteExempt` (`:617-627`), so a direct
    `processor().setBase(ReverbSize, …)` is missed.
  - The peak-strip plant does `*reinterpret_cast<std::atomic<float> *const *>(&peaks)`
    (`sweep_params_tests.cpp:451`).
- **Plan:**
  1. Have `noteExempt` compare against `processor_.base(ParamSlot::ReverbSize)` and
     `effective(...)` read at each call, instead of a tracked copy, so every route into the base
     is seen.
  2. In the extremes and bursts, run the runaway check over the last held window **before** the
     restore of defaults, as well as after it, so both "no runaway under the setting" and
     "recovery" are proven.
  3. Give `LivePeaks` a test seam (a `friend struct LivePeaksTestAccess` or a
     `plantForTest(frame, value)` compiled for tests) in place of the `reinterpret_cast`.
  4. Move the driver's non-template bodies into `tests/sweep/SweepDriver.cpp`.
- **Tests:** `-L sweep` green, floors re-pinned where (2) adds a window.
- **Goldens:** no golden can move (tests only).
- **Size:** M. Group: sweep (one task with 302 and 303).

## 310: the cancel fix confirmed only by CI: CLOSE-OBSOLETE

- **History:**
  - `5f1f9b5` (2026-10-30, "the loader passes its cancel flag to PreparedSource::make").
  - It failed **again after the fix**: run 41174234568, head `1d62946` (`git merge-base
    --is-ancestor 5f1f9b5 1d62946` is true), mac-universal native, `tests/plugin/sample_tests.cpp:683:
    FAILED: CHECK( deactivating < whole / 4.0 )`, 155.159 ms against 138.96 ms, "the whole job:
    555.854 ms".
  - The same stall pattern hit `489d85e` (255.8 ms against 154.6 ms), as `a9e82d3`'s message
    records.
  - `a9e82d3` (2026-10-31, "the cancel test gives deactivate and the destructor three tries") came
    after `1d62946` (not its ancestor). Its message: with the cancel disabled, every try of both
    took 165–170 ms against a 226 ms job and all six failed, so the test still discriminates.
- **At HEAD:** the test has `kTries = 3` (`tests/plugin/sample_tests.cpp:664-679`).
- **Green since `a9e82d3`:** run 41499519821 attempt 1's native ctest: "Test #1375: deactivate and
  destruction cancel a load under way at once … Passed 2.70 sec". Run 41539299073 (push, `8be9e43`)
  and 41297307655 (nightly, `3f2f748`) succeeded. Every failed main run since 41331231402 failed only
  at the benchmark step.
- **Close it citing `5f1f9b5` and `a9e82d3`,** and note the post-fix failure at `1d62946` was a VM
  stall: with the flag the cancel takes under 1 ms locally. It is not a cancel-path fault.

## 330 (its open remainder): FIX (a); (b) obsolete; (c) to the director

- **(a) A loaded out-of-range depth is clamped on first touch.**
  - The cause: `ui/src/views/RouteList.cpp:529-531` sets `row.depth.setRange(-range, range)` from
    `depthRange` (`:72-81`, max − min of the target), then `setValue(r.depth)`. JUCE clamps to the
    range, so the first drag sends the clamped value. Core accepts any finite depth
    (`core/src/Patch.cpp:202-208`, `:378`).
  - Fix: `const double range = std::max(depthRange(r.target, context.mode),
    std::abs(static_cast<double>(r.depth)));`, so the slider shows and keeps the loaded depth until
    the user moves it.
  - Test: `[plugin]` (with `routes_tests.cpp` / `mod_view_tests.cpp`) "a loaded depth past the
    target's range is kept until moved": load routes with depth 3× range, refresh, CHECK the
    slider's value equals it, simulate a small drag, CHECK the sent depth is near it, not the clamp.
  - Size S. No golden can move (editor only).
- **(b) Drag-and-drop untested headlessly: obsolete.** Both halves are tested:
  - the chip's description: `tests/plugin/mod_view_tests.cpp:468`
    `CHECK(chip->description() == "modsource:" + i)`;
  - the drop: `tests/plugin/EditorTestUtil.h:316-325` `dropChip` → `isInterestedInDragSource` and
    `itemDropped`, used in `editor_frame_tests.cpp:638-641` and the Mod view tests (since
    `7df6ca0`).
  - Only JUCE's own `DragAndDropContainer::startDragging` (`ui/src/views/SourceStrip.cpp:134`) is
    unexercised, and that is JUCE's.
- **(c) Stock JUCE widgets: a look choice, a director line.**
  - Option 1 (recommended): keep the JUCE ComboBox and Slider under `BoxLookAndFeel`, themed by the
    palette (cost: none; they already follow both themes).
  - Option 2: move the route list to sst-jucegui widgets (cost: L, plus the GUI tests' label and
    collision checks redone).
- **Closing:** the row closes with (a) built and (c) answered. Group: ui (330 + 331 + 333).

## 331: split the editor's header into its own component: FIX

- **The file:** `ui/src/Editor.cpp` (753 lines; the brief's `plugin/src/editor/Editor.cpp` doesn't
  exist).
- **What's in the header now:**
  - built in `Editor`'s constructor (`:226-279`): `product_`, the preset strip `preset_` (in
    "header.preset", unblocked now that presets group 2 has merged), `viewButtons_` (kViewIds
    `header.perform|edit|mod|fx`, `:175`), `undo_`, `redo_`, `theme_`, `frameMs_`, `notice_`;
  - laid out in `resized()` (`:401-430`), painted at `:398`;
  - `kHeaderHeight = 40` (`:554`), `layoutNotice` (`:574-590`).
- **Plan:**
  - New `ui/src/views/Header.{h,cpp}`, `class Header final : juce::Component, StyleConsumer`,
    owning those widgets and their layout and paint.
  - Callbacks out: `onAbout`, `onSelectView(View)`, `onUndoRedo(bool)`, `onTheme()`, the preset
    strip's `onName`, `onPrev` and `onNext` (the browser and Mutate overlays stay the Editor's,
    since they cover the area under the header).
  - Inputs: `setView(View)`, `setFrameMs(double)`, `showNotice(text)` and the undo/redo enablement.
  - The notice's narrow fallback is a band **under** the header, over the view (`:583-584`). Keep
    `notice_` the Editor's child, with Header exposing `middle()`, or let Header place it in its
    parent's coordinates. The first is simpler.
  - Keep every component ID unchanged. Tests find them recursively (`EditorTestUtil.h:74-87`
    `stripChild`, `:158-170` `findDeep`), and 48 `header.*` references in `tests/plugin/` keep
    working. The keyboard shortcuts stay in `Editor::keyPressed`.
  - Add `Header.cpp` to `ui/CMakeLists.txt`'s editor sources.
- **Tests:**
  - `editor_frame_tests.cpp` unchanged and green: every view in both themes and sizes, no label
    elided or colliding (the check's coordinate space must be the editor's), keyboard reach, the
    theme switch.
  - `factory_tests.cpp`'s header cases.
  - `snap_cli` and a look at the 16 committed PNGs (`docs/listening/m2-views/snapshots/`), which
    should be pixel-identical. Remake them only if not.
- **Goldens:** no golden can move (editor only).
- **Size:** M. Group: ui.

## 333: the route list auto-selects a route added by an undo or a load: DIRECTOR

- **The code:** `ui/src/views/RouteList.cpp:480-493` and `:540-543`. On every refresh after the
  first, the newest slot whose source went from None to set is selected and scrolled to, "a drop, a
  redo, the host's patch". A preset or state load that brings in routes selects the last of them.
- **Option 1 (recommended): select only when exactly one route appeared since the last look**
  (an undo, a redo, a drop, a single host edit). A load, which brings several, leaves the selection
  and scroll alone.
  - Cost: S. Count the new routes in that loop and select when the count is 1, plus a `[plugin]`
    test with two cases (one route added: selected; a load of five: not).
  - A load that brings exactly one route still selects it, a corner case the director may accept.
- **Option 2:** keep it as it is (cost: none). The comment says it's meant, and a load scrolling to
  its last route is harmless.
- **Option 3:** select only on the user's own drop, never for undo, redo, a load or the host
  (cost: S). This loses the "where did my undone route go" cue that the redo case gives.
