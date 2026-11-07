# M3 Voice filter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On a layer at Mode Voice, each voice's grains pass through that voice's own state-variable filter (Voice Filter, Voice Cutoff, Voice Resonance, Voice Key Track) before the layer's two bus filters, and with Voice Filter Off (the default) every render keeps sub-project 1's bits.

**Architecture:** Each layer's `Engine` owns a `VoiceFilterBank`: 32 lanes, each two four-wide `CytomicSVF`s and a 4 × 32 scratch, and one type controller with `BusFilter`'s law. While the bank is *engaged* (a busy lane and a type in force or fading), `Engine::process` splits its blocks at grid samples, `renderPool` routes each voice grain into its lane's scratch, and the lane pass filters the busy lanes in owner-ordinal order and adds them to `out` and `bus2`. While it isn't engaged, the engine runs sub-project 1's direct render untouched. The `Processor` computes each voice's cell (cutoff Hz, resonance) in `updateVoices` and passes the layer's type at every boundary.

**Tech Stack:** C++23, sst-filters' `CytomicSVF` (pinned, header-only), Catch2, CMake presets, `gg-render` goldens, `gg-bench`.

**Spec:** `docs/superpowers/specs/2026-11-07-m3-voicefilter-design.md` (F1–F14, FB1–FB17; builds on `docs/superpowers/specs/2026-11-07-m3-voice-design.md`). File:line references are at `c455aea`.

## Global Constraints

- Audio thread: no allocation, locks, I/O, syscalls or unbounded loops; RTSan-clean; 0- and 1-sample blocks; `params.flush` trivial.
- Determinism: randomness only from `gg::rng` (seed, layer, stream, field, index); no `std::` distributions; output bit-identical at any host block size; layers summed A to D.
- At rest nothing moves: with the new feature at rest every existing golden keeps its WAV SHA-256. The at-rest check: `/home/sethbc/src/giga-grains-m3-voice/.superpowers/sdd/2026-11-07-m3-voice/check-wav-hashes.sh <worktree>` compares the 108 M2-era cases (last line `baseline cases: 108, changed: 0, gone: 0`); also confirm the 8 voice goldens (tests/golden/cases/*voice*) are unchanged unless the task changes voices on purpose.
- Stage files by name; never `git add -A` or `git commit -a` (the at-rest check rewrites expected files while it runs). Never commit regenerated expected files of existing cases (this Linux machine's metrics differ in the last digit).
- CLAP IDs append-only; your sub-project's index ranges as the spec says.
- `-Wall -Wextra -Werror`; GCC 13's diagnostics (CLAUDE.md). Test names ASCII only; tests parallel-safe.
- Format: `CLANG_FORMAT=$HOME/.local/share/giga-grains/format-venv/bin/clang-format scripts/format.sh`.
- New build trees: `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Models: implementers opus for design-heavy tasks, sonnet otherwise; reviewers sonnet at least; never Fable.
- Branch hygiene: this branch merges `m3-voice` (sub-project 1) as it moves; parallel sub-projects 2, 3, 4 edit `core/include/gg/core/ParamIds.h` lists and `Processor::updateVoices` — keep such edits small and self-contained to ease merges.

This sub-project's own:
- **Indices:** layer indices **0x74 to 0x77** in each layer scope (`0x010074` … `0x040077`), **no global**. `kNextFreeLayerIndex` and `kNextFreeGlobalIndex` unchanged. `kLayerParamCount` 63 → 67, `kParamCount` 290 → 306 on this branch.
- **No patch-version bump**; a state or preset without the four reads their defaults. A preset saves them (sound, not instance).
- **`core/src/fx/BusFilter.*` is not touched or moved.** `gg-engine` links `sst-filters` PRIVATE; no public engine header includes `sst/filters/*`.
- **The event log is unchanged** (no `"lane"` field). The voice filter's goldens are checked across platforms by their metrics alone.
- **No new factory routes** (the 48 slots stay); the filter is **not counted against the budget** (no ledger weight).
- **The stronger at-rest check:** every step that says "at rest" runs `.superpowers/sdd/2026-11-07-m3-voicefilter/check-at-rest.sh <worktree>` (Task 1), which runs the 108-case script above, then the 8 voice goldens and the engine schedule hashes. Expected last line: `at rest: 108 + 8 goldens, 0 changed; schedules 0 changed`.
- Implementers: **opus for Tasks 3, 4, 5, 6 and 7**, sonnet otherwise.

## Review Focus

1. **Host blocks that fall off the grid while the bank is engaged** (a call starting at sample 40, odd sizes cycling): no free, type or cell decision lands off a grid sample, and the render equals the 32-frame one. (Task 5's `voice filter renders are invariant under random host blocks`.)
2. **Non-finite or huge cells** (a NaN base, two large route depths summing to ±∞, a cutoff far above Nyquist): the output stays finite and the cutoff clamps to `[20, min(20000, 0.45 × sr)]`. (Task 5's `a non-finite cell renders finite`, Task 7's `voice filter cells from extreme routes stay finite`.)
3. **`reset()` (deactivate/activate) while engaged, mid-fade:** the next render is a new engine's, bit for bit, with no stale lane state or half-done fade. (Task 6's `reset while engaged renders as a new engine`.)
4. **A host passing null `out` while engaged:** nothing is rendered, but the lanes step on silence, so frees, merges and fades land on the same samples as with buffers. (Task 5's `a null out while engaged keeps the lane timeline`.)
5. **Sample rates far from 48 kHz** (22.05 kHz, 192 kHz): the cutoff clamp, the 10 ms ramp and the 4 s cap follow the rate. (Task 3's `the bank follows the sample rate`.)

## Plan decisions (Claude's, binding on the tasks; the spec leaves them open)

- **P1. Off's engine bits are pinned on this machine before any engine change** (Task 1): an engine-level schedule harness writes FNV-1a hashes of sub-project 1's voice renders to a file, and the at-rest script compares them with the branch-start file. Hard-coded hash constants were declined: engine output differs across platforms in the last bits (CLAUDE.md: across platforms only the event log and metrics compare).
- **P2. The order of the tasks is the crux's order.** Task 4 adds every new engine field, the lane column and the lanes' bookkeeping with **no engaged path at all** (`voiceFilterEngaged()` is `false`, `renderPool` unchanged), and lands F4's `memcmp` test. Only Task 5 makes a lane render reachable, with that test and the schedule hashes still green.
- **P3. The bank is private to `gg-engine`.** `engine/include/gg/grain/VoiceFilter.h` (public) holds `VoiceFilterType`, `LaneState` and the constants; `engine/src/VoiceFilterBank.h` and `engine/src/VoiceFilter.cpp` hold the class, which includes `CytomicSVF`. `Engine` holds `std::unique_ptr<VoiceFilterBank> lanes_`, made in the constructor (as `spectral_` is), with `~Engine()` defined out of line. `gg-tests` and `gg-bench` add `${CMAKE_SOURCE_DIR}/engine/src` to their private includes (both already link `sst-filters`). This keeps FB1's PRIVATE link while `Engine.h` stays free of sst-filters.
- **P4. The free rule is written so it can run at any block start** and still mean F9's "first grid sample": a Draining lane is *due* when `gridCeil(lastEnd) <= t` and *capped* when `gridCeil(lastEnd + capSamples) <= t`. Not engaged, due frees it; engaged, it needs a grid `t`, no fade and `stateMagnitude < 2^-20`; capped always frees. It runs at every block start of `process()` and in `startVoice`. Between two runs the engagement is constant (it changes only at grid block starts, which are runs), so this equals a check at every grid sample, and no host block boundary moves a free. A lane's `lastEnd` starts at its assignment clock, so a voice that never admits a grain still frees.
- **P5. At a grid block start the engagement is recomputed twice:** after the type is applied (FB3 step 2, which the free rule reads), and again after the free rule, so a block whose last lane just freed renders direct. The output is the same either way (a freed lane has no sounding grain); the second recompute only stops needless grid splits.
- **P6. The lane pass is lane-major:** `filter(lane, n)` runs one lane over the block's `n ≤ 32` frames at the controller's fade position, handling a fade that ends inside the block, and `advance(n)` moves the controller once after every lane. Bit-equal to `BusFilter`'s per-sample loop (Task 3 pins it).
- **P7. Voice Cutoff and Voice Resonance join `isVoiceTarget` in Task 7**, with the `Processor` code that reads and zeroes their `voiceOffsets_`, not in Task 2, so no task leaves a voice offset accumulating unread.
- **P8. Voices start on grid samples in the `Processor`** (its boundaries are `gridCeil` of the event, Processor.cpp:2196), so a lane's first frame is always a grid block start there. A direct engine caller starting a voice off the grid engages at the next grid sample, its first frames rendering direct; the engine tests start voices on the grid.
- **P9. The observers** the tests and `gg-bench` read are public const `Engine` methods (Task 4's Interfaces), documented as observers; none changes state.
- **P10. `gg-snap`:** Task 12 regenerates `docs/listening/m2-views/snapshots/` if `gg-snap` runs headless on this Linux machine; otherwise it adds a carry-over row for the Mac and says so.

## Seams with the parallel sub-projects (for whoever merges second)

- **Sub-project 2 (expression, `m3-sp2-expression`):** its plan replaces the `Processor`'s local `kVoiceTargets`/`kVoiceKeySlots` with `ModMatrix.h`'s `kVoiceLayerTargets` and `kPerNoteSlots = 23`, and pins "plugin per note flags are on exactly the 92 voice slots". At merge: `isVoiceTarget` is the union of both lists; Voice Cutoff and Voice Resonance become per-note slots (**25 a layer, 100 in all**, the test updated) and get the three CLAP per-note flags; but they stay **out of whatever list keys the voice cache and builds `CloudParams`** (FB9), so a Voice Cutoff route never moves `voiceParamRecomputes()` (Task 7's test pins it). Its EB6 per-row slot and this branch's `GrainRows::lane` are two columns (FB12). `gg-render`'s note `paramMods` then accept `"A Voice Cutoff"` and `"A Voice Resonance"` (a render-spec test added at merge).
- **Sub-project 3b (playhead, `m3-sp3b-playhead`):** its `GG_LAYER_PARAMS_M3_PLAYHEAD` (0x68–0x6A) goes before this branch's `GG_LAYER_PARAMS_M3_VOICEFILTER` (0x74–0x77) in both macros, both after `GG_LAYER_PARAMS_M2_EXTREMES`; `isReservedIndexInUse` ORs both conditions; the counts add (63 + 3 + 4 = 70 a layer).
- **Sub-projects 3, 4, 8:** Key Track reads `pitchIn` as built (sub-project 4 owns its key term); a `GrainRows` column from sub-project 8 sits beside `lane`.
- **Sub-project 1:** if its remaining tasks change `scheduleVoices`, `startVoice` or `renderPool`, re-apply Task 4's and Task 5's edits to them.

---

### Task 1: The at-rest baseline and the schedule harness

**Files:**
- Create: `tests/engine/VoiceSchedules.h`, `tests/engine/voice_schedule_tests.cpp` (tags `[engine][voicefilter]`; add to `tests/CMakeLists.txt`); outside git, `.superpowers/sdd/2026-11-07-m3-voicefilter/{check-at-rest.sh,voice-baseline-wav-sha256.txt,schedules-baseline.txt}`

**Interfaces:**
- Produces (namespace `gg::test`, header-only):
  - `enum class ScheduleForm : uint8_t { TwoBus, FiveArgument, Mono };`
  - `struct ScheduleEvent { int64_t at; enum class Kind : uint8_t { Start, Params, Finish, Clear } kind; uint32_t slot; uint32_t ordinal; double pitchSemitones; };` (`at` a grid sample)
  - `struct VoiceSchedule { std::string name; ScheduleForm form; double busSplit; int64_t frames; std::vector<ScheduleEvent> events; };`
  - `std::vector<VoiceSchedule> voiceSchedules();` — `chord` (3 voices), `steal` (17 starts over 16 slots, reusing slot 0), `dup` (two voices at one pitch), `bus-split` (Bus Split 0.5, two-bus form), `mono`, `five-argument`; each 2 s at 48 kHz over a stereo noise sample, `kParams`-like stream, seed 11.
  - `struct ScheduleRender { std::vector<float> out[2]; std::vector<float> bus2[2]; std::vector<std::string> events; };` (`events`: one `formatEventLine`-like text per sink call: index, voice, onset, frac, pos, rate, dur, gain, pan, bus)
  - `using BeforeCall = std::function<void(gg::Engine &, int64_t clock)>;`
  - `ScheduleRender renderSchedule(const VoiceSchedule &, uint32_t blockSize, const BeforeCall &before = {});` — cuts its calls at event times, applies events between calls, calls `before` before every `process()`.
  - `uint64_t fnv1a(const ScheduleRender &);` (FNV-1a 64 over every buffer's bytes, then every event line)

- [ ] **Step 1: Set up the worktree.** Run `scripts/worktree-setup.sh`, then `cmake --preset dev -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache` (and `asan`; `rtsan` with `GG_LLVM` if an upstream LLVM ≥ 20 is installed, else CI's RTSan covers it), `cmake --build --preset dev`. Run sub-project 1's `check-wav-hashes.sh`. Expected: `baseline cases: 108, changed: 0, gone: 0`.
- [ ] **Step 2: Record the voice baseline.** From `build/dev/golden/wav-sha256.txt`, copy the 8 `*voice*` lines into `voice-baseline-wav-sha256.txt`. Write `check-at-rest.sh <worktree>`: run sub-project 1's script; restore the 8 cases' committed expected files (only paths `git ls-files` lists); compare their hashes; run `build/dev/tests/gg-tests "voice schedules write their render hashes"` and compare `build/dev/tests/test-output/voice-schedules.txt` with `schedules-baseline.txt`; print `at rest: 108 + 8 goldens, N changed; schedules M changed`; exit non-zero on any change.
- [ ] **Step 3: Write the harness test** `voice schedules write their render hashes`: for every schedule, `renderSchedule` at block sizes 1, 32, 64, 255 and 4096; REQUIRE all five `ScheduleRender`s equal (`memcmp` of each buffer, equal event lists), REQUIRE every sample finite and the `steal` schedule's events to include a reused slot; write `<name> <fnv1a hex>` lines to `GG_TEST_OUTPUT_DIR/voice-schedules.txt`.
- [ ] **Step 4:** Run `ctest --preset dev -L voicefilter`. Expected: PASS. Copy the hash file to `schedules-baseline.txt`. Run `check-at-rest.sh`. Expected: `at rest: 108 + 8 goldens, 0 changed; schedules 0 changed`.
- [ ] **Step 5:** Commit `test(engine): the voice schedule harness, Off's bits pinned before the voice filter (M3 sub-project 9)` (stage the two test files and `tests/CMakeLists.txt` by name).

### Task 2: The four parameters

**Files:**
- Modify: `core/include/gg/core/ParamIds.h` (a list `GG_LAYER_PARAMS_M3_VOICEFILTER(X)` after `GG_LAYER_PARAMS_M2_EXTREMES(X)` in `GG_LAYER_PARAMS` and `GG_LAYER_PARAMS_SINCE_M1`; `isReservedIndexInUse` gains `(scope >= 1 && index >= 0x74u && index <= 0x77u)`; the 63/290 comments and tallies), `core/src/ParamIds.cpp` (metadata), `core/src/Mutate.cpp` (`buildStructuralLocks`: the four in every layer), `ui/src/EditLayout.cpp` (F13), `plugin/src/GigaGrainsPlugin.cpp` (only if its param info lists rows by hand)
- Test: `tests/support/ParamTable.h`, `tests/core/patch_tests.cpp`, `tests/core/mutate_tests.cpp`, `tests/plugin/identity_tests.cpp`, `tests/plugin/params_tests.cpp`, `tests/plugin/rt_tests.cpp`, `tests/ui/edit_layout_tests.cpp`, `tests/sweep/*` floors

**Interfaces:**
- Produces: `LayerParam::VoiceFilter` (0x74, "Voice Filter", stepped 0–4, choices `busFilterType()`'s, default Off), `LayerParam::VoiceCutoff` (0x75, "Voice Cutoff", `busCutoff()`: log2 Hz 20 Hz–20 kHz, default 1 kHz), `LayerParam::VoiceResonance` (0x76, "Voice Resonance", 0–1 percent, default 0), `LayerParam::VoiceKeyTrack` (0x77, "Voice Key Track", 0–1 percent, default 0); all automatable and modulatable. `kLayerParamCount` 67, `kParamCount` 306. `gg-render` names `"A Voice Filter"` … `"D Voice Key Track"` and the route targets by layer A's names follow from the macros. `isValidTarget` admits all four unchanged (no exclusion added); `isVoiceTarget` is unchanged here (P7).
- Edit placement (F13), Filter panel: `editLine(VoiceFilter, 0)`, `edit(VoiceCutoff, 1)`, `edit(VoiceResonance, 2)`, `edit(VoiceKeyTrack, 3)`, then `editLine(BusSplit, 4)`, `editLine(Bus1Filter, 5)`, 6, 7, `editLine(Bus2Filter, 8)`, 9, 10.

- [ ] **Step 1: Write the failing tests.** Update `ParamTable.h` and every count to 306/67 with the 16 IDs `0x00010074` … `0x00040077` and the ranges and defaults above. Add:
  - `the voice filter's indices are reserved in every layer scope` (patch_tests): `isReservedIndexInUse(s, i)` true for s 1–4, i 0x74–0x77; false for scope 0 and for 0x73 and 0x78.
  - `a patch without the voice filter's values loads their defaults` (patch_tests): a version-2 state with the four rows removed loads Off, 1 kHz, 0, 0 in every layer; the patch version is unchanged.
  - `mutate's default locks hold the voice filter's four` (mutate_tests): `defaultMutateLocks` has the 16 slots; a press at amount 1 leaves them; every other slot's moved value equals the pre-change value for the same seed and counter (the CLAP-ID keying).
  - `the voice filter's four are route targets but not voice targets` (mod_matrix_tests): `isValidTarget` true for layer A's four, false for B's; `isVoiceTarget` false for all four (Task 7 flips two).
- [ ] **Step 2:** Run `ctest --preset dev -L 'core|plugin|ui|mod' -LE slow`. Expected: the new and updated cases FAIL.
- [ ] **Step 3: Implement** the list, metadata, reserved block, locks and placement. If the 640 × 360 frame tests fail on elision or reachability, append `Panel::VoiceFilter` beside Filter for the four, give short labels where needed (full names to the host), and record the ruling in the commit message.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow` (the frame tests "every parameter is reachable from the UI", "no control's label is elided…", the keyboard and both themes included) and `-L sweep`; move a sweep floor only with its reason in a comment. Run `check-at-rest.sh`. Expected: PASS, 0 changed.
- [ ] **Step 5:** Commit `feat(params): Voice Filter, Voice Cutoff, Voice Resonance, Voice Key Track per layer (0x74-0x77)`.

### Task 3: VoiceFilterBank, the law and the lanes

**Files:**
- Create: `engine/include/gg/grain/VoiceFilter.h`, `engine/src/VoiceFilterBank.h`, `engine/src/VoiceFilter.cpp`, `tests/engine/voice_filter_bank_tests.cpp` (tags `[engine][voicefilter]`)
- Modify: `engine/CMakeLists.txt` (the source; `sst-filters` PRIVATE), `engine/include/gg/grain/GrainPool.h` (`kNoLane` beside `kNoVoice`), `tests/CMakeLists.txt` (the file; `engine/src` include)

**Interfaces:**
- Produces, public (`VoiceFilter.h`): `enum class VoiceFilterType : uint8_t { Off, LowPass, HighPass, BandPass, Notch };` `enum class LaneState : uint8_t { Free, Owned, Draining };` `kVoiceLanes = 32`, `kLaneFrames = 32`, `kLaneFreeLevel = 0x1.0p-20f`, `kLaneCapSeconds = 4.0`; in `GrainPool.h`, `inline constexpr uint8_t kNoLane = 0xFF;`.
- Produces, private (`class gg::VoiceFilterBank`, every method `noexcept` and real-time safe but `prepare`):
  - `void prepare(double sampleRate, uint32_t rampSamples);` then `reset()`; `capSamples = llround(4 × sr)`, `cutoffMax = min(20000, 0.45 × sr)`.
  - `void reset();` every lane Free, filters `init()` with `firstBlock`, Off in force, request Off, no fade.
  - The controller (FB2, FB15): `void request(VoiceFilterType)`, `VoiceFilterType requested() const`, `inForce() const` (`type_`), `target() const` (`nextType_` while fading, else `type_`), `bool fading() const`, `void applyRequest()` — with a busy lane exactly `BusFilter::setCell`'s type logic (start from silence on the spare, turn back with `fadePos_ = min(R − p, R − 1)`, a third type waits); with none, a jump (no fade, `type_ = requested`).
  - Lanes (F2, FB6): `uint8_t assign(uint32_t ordinal, int64_t clock, float cutoffHz, float resonance)` — lowest Free (init the active filter, and the spare too during a fade, `firstBlock`), else the Draining lane of lowest owner ordinal, merged (state kept; owner, Owned, cell replaced); `lastEnd = max(lastEnd, clock)` (a new lane's starts at `clock`). `void setCell(uint8_t, float cutoffHz, float resonance)` (BusFilter's clamp and NaN rule, `res = 0.95 × clamp`); `void drain(uint8_t)`; `void raiseLastEnd(uint8_t, int64_t)`; `void endAllAt(int64_t t)` (every busy lane's `lastEnd = t`); `void freeAll()` (every lane Free and reset; the controller jumps to the request); `void freeFinished(int64_t t, bool grid, bool engaged)` (P4; the controller jumps when the last busy lane frees during a fade).
  - Observers: `bool anyBusy() const`, `LaneState state(uint8_t) const`, `uint32_t owner(uint8_t) const`, `int64_t lastEnd(uint8_t) const`, `float stateMagnitude(uint8_t) const` (FB11: max |·| of the active filter's `ic1eq`, `ic2eq` over the four channels).
  - The render (FB5, P6): `void beginCell()` (`setCoeffForBlock<32>` on every busy lane's active filter, and the spare while fading, for non-Off types), `uint32_t busyByOwner(std::span<uint8_t, kVoiceLanes> order) const` (ascending owner ordinal), `float *scratch(uint8_t lane, uint32_t channel)` (`kLaneFrames` floats), `void zero(uint8_t lane, uint32_t n)`, `void filter(uint8_t lane, uint32_t n)` (in place, four channels, `processBlockStep(A, B, C, D)`), `void advance(uint32_t n)`.

- [ ] **Step 1: Write the failing tests** in `voice_filter_bank_tests.cpp` (it includes `fx/BusFilter.h` from `core/src` too):
  - `the bank's channels 0 and 1 are BusFilter's bit for bit`: 200 cells of seeded random (type 0–4, cutoff 10 Hz–30 kHz, NaN once, resonance −0.1–1.1) over seeded stereo noise; BusFilter: `setCell` then 32 `process`; bank (one lane assigned at clock 0 with Off in force): `request`, `applyRequest`, `setCell`, `beginCell`, channels 2–3 other noise, `filter(lane, 32)`, `advance(32)`. REQUIRE `memcmp` equal for channels 0 and 1 over all 6,400 samples. The sequence includes a turn-back (A, B, A within a fade) and a third type during a fade.
  - `a lane assigned during a fade joins it`: two lanes, the second assigned mid-fade with both filters from silence; its output equals a BusFilter that was reset at that cell and given the same fade position (both from silence, so its first cell is a weighted mix of two silent-start filters: compare against an explicit two-`CytomicSVF` reference in the test).
  - `lanes are assigned lowest free first and merge with the oldest draining`: assign 32 ordinals 0–31, drain 16–31 (state Draining), drain 0–15 except 3; assign ordinal 40 → the lane owned by ordinal 0 (lowest Draining owner), state Owned, `owner == 40`, its `lastEnd` kept.
  - `a draining lane frees by the rule`: `freeFinished` table: not engaged, `t = gridCeil(lastEnd)` frees and `t − 32` doesn't; engaged, grid, no fade, magnitude below `kLaneFreeLevel` frees; magnitude above, or `grid == false`, or fading, doesn't; `gridCeil(lastEnd + capSamples)` frees a lane still fed a 1 kHz sine at resonance 1.
  - `the last lane freed mid-fade ends the fade`: `fading()` false and `inForce() == requested()` after it.
  - `a request with no busy lane jumps` (FB15): no fade, `inForce()` is the request at once.
  - `the bank follows the sample rate` (Review Focus 5): at 22,050 Hz a 20 kHz cell filters as `0.45 × 22050` Hz (equal to a bank given 9,922.5 Hz exactly); at 192,000 Hz `capSamples == 768000`; both outputs finite under resonance 1.
- [ ] **Step 2:** Run `ctest --preset dev -L voicefilter`. Expected: FAIL (compile errors first).
- [ ] **Step 3: Implement** the bank in `VoiceFilter.cpp`, copying BusFilter's arithmetic (clamp, `0.95f ×`, `modeOf`, the fade's `float(fadePos)/float(R)` after the increment and the swap at `fadePos >= R`). No allocation after `prepare`.
- [ ] **Step 4:** Run `ctest --preset dev -L 'voicefilter|buses'` and `check-at-rest.sh`. Expected: PASS, 0 changed (nothing calls the bank yet).
- [ ] **Step 5:** Commit `feat(engine): VoiceFilterBank, BusFilter's law on four channels and 32 lanes (FB1, FB2, F2, F9)`.

### Task 4: The engine's lanes, bookkeeping only (Off never reroutes)

**Files:**
- Modify: `engine/include/gg/engine/Engine.h` (`EngineConfig::filterRampSamples`, `VoiceParams` fields, the API and observers below, `Voice::lane`, `std::unique_ptr<VoiceFilterBank> lanes_`, `requestedFilterType_`, `engaged_`, `~Engine()`), `engine/src/Engine.cpp` (constructor; `startVoice`, `setVoiceParams`, `finishVoice`, `clearVoices`, `clearGrains`, `reset`, the gate in `process`, the block loop's free rule, `spawnAs` and `scheduleVoices` passing the lane, `admit` raising `lastEnd`), `engine/include/gg/grain/GrainPool.h` and `engine/src/GrainPool.cpp` (`GrainSpec::lane`, the `GrainRows::lane` column, stored at spawn)
- Test: `tests/engine/voice_filter_tests.cpp` (new, `[engine][voicefilter]`), any test that aggregate-initializes `GrainRows` (`-Wmissing-field-initializers`)

**Interfaces:**
- Consumes: Task 3's bank; Task 1's `renderSchedule` and `BeforeCall`.
- Produces:
  - `EngineConfig::filterRampSamples` (`uint32_t`, 0 means `max(1, llround(0.01 × sampleRate))`).
  - `VoiceParams::filterCutoffHz{20000.0f}`, `VoiceParams::filterResonance{0.0f}`.
  - `GrainSpec::lane` (`uint8_t`, `kNoLane` default; a voice grain takes its voice's lane at admission); `GrainRows::lane` (`const uint8_t *`, appended last).
  - `void Engine::setVoiceFilterType(VoiceFilterType) noexcept` (the layer's request; `setCloudParams`' threading rule).
  - Observers (P9): `bool voiceFilterEngaged() const`, `VoiceFilterType voiceFilterInForce() const`, `bool voiceFilterFading() const`, `uint8_t voiceLane(uint32_t slot) const` (`kNoLane` if none), `LaneState laneState(uint8_t) const`, `uint32_t laneOwner(uint8_t) const`, `float laneStateMagnitude(uint8_t) const`, `VoiceParams voiceParams(uint32_t slot) const` (a default for a slot out of range).
- Rules (FB7, FB15, P4): `startVoice` runs `freeFinished(clock_, clock_ % 32 == 0, engaged_)`, applies a pending request if no lane is busy, drains the slot's lane if still Owned, then `assign`s with `params`' cell. `finishVoice`/`clearVoices` drain Owned lanes. `setVoiceParams` updates the owned lane's cell. `clearGrains` calls `endAllAt(clock_)`; the gate turning off and `reset()` call `freeAll()` and clear `engaged_`. The block loop runs the free rule at every block start; at the call's first frame a request with no busy lane applies. **`engaged_` is never set true in this task, and `renderPool` and `processBlock` are not touched beyond the lane argument.**

- [ ] **Step 1: Write the failing tests** in `voice_filter_tests.cpp`:
  - `voice filter off never reroutes`: for every Task 1 schedule at block sizes 32 and 4096, `renderSchedule` plain vs with a `BeforeCall` that calls `setVoiceFilterType(Off)` and gives every spawning slot a seeded random cell (`setVoiceParams` with `filterCutoffHz` 20–20,000 and `filterResonance` 0–1) at every grid call. REQUIRE `memcmp`-equal buffers and equal event lines.
  - `a cloud layer with its type set renders cloud's bits`: voice mode off, `setVoiceFilterType(LowPass)` before every call vs never; `memcmp` equal; `voiceLane` is `kNoLane` for every slot and every event's `lane` is `kNoLane`.
  - `a voice grain carries its voice's lane`: three voices → lanes 0, 1, 2; every sink `GrainSpec`'s `lane == voiceLane(slot of its voice)`.
  - `a slot reused by a cap steal gets a different lane`: `startVoice(0, 0)` then `startVoice(0, 1)` → lane 1 Owned by 1, lane 0 Draining.
  - `finish, clearVoices and a mode switch drain the lanes`: states Draining, owners kept, cells frozen (`voiceParams` unchanged by a later `setVoiceParams` to a finished slot).
  - `a lane frees at the first grid sample past its last grain`: with 1-sample calls, `laneState` turns Free exactly at `gridCeil(lastEnd)` (lastEnd the latest `onset + dur` of the voice's sink grains; the schedule's grains meet no sample edge), and at block sizes 37 and 4096 the probe at that sample (calls cut there) agrees.
  - `clearGrains, the gate and reset end the lanes`: a source removal sets each busy lane's last end to the clear's frame (Free at the next grid sample); `setEnabled(false)` frees every lane at that call's first frame; `reset()` frees every lane and `voiceFilterInForce()` is the request.
- [ ] **Step 2:** Run `ctest --preset dev -L voicefilter`. Expected: FAIL.
- [ ] **Step 3: Implement** the fields, the column, the bookkeeping and the observers. Nothing reads `rows.lane` yet.
- [ ] **Step 4:** Run `ctest --preset dev -L 'engine|voice|voicefilter|determinism|cloud|kernel|pool'` and `check-at-rest.sh`. Expected: PASS; `at rest: 108 + 8 goldens, 0 changed; schedules 0 changed`.
- [ ] **Step 5:** Commit `feat(engine): voice lanes' bookkeeping and the lane column, the render untouched (F4, FB4, FB6, FB7)`.

### Task 5: Engagement and the lane render

**Files:**
- Modify: `engine/src/Engine.cpp` (`process`'s block loop: grid splitting and the grid work; `processBlock`: the engaged path; `renderPool`: the partition), `engine/include/gg/engine/Engine.h` (`BlockOutputs` gains the lanes' flag; doc comments)
- Test: `tests/engine/voice_filter_tests.cpp`, `tests/engine/EngineReference.h` (new: the event-to-reference helpers moved out of `engine_reference_tests.cpp`, which then includes it unchanged in behavior), `tests/engine/determinism_tests.cpp`

**Interfaces:**
- Consumes: Task 4's API; Task 3's `beginCell`, `busyByOwner`, `scratch`, `zero`, `filter`, `advance`.
- Rules:
  - **Splitting (FB3):** a block is capped at the next grid sample while `engaged_ || (anyBusy() && requested() != target())`, beside the read limit's cap (`readLimitCheckAt_`), the smaller cap winning.
  - **At a grid block start:** `applyRequest()`; `engaged_ = anyBusy() && (inForce() != Off || fading())`; `freeFinished(clock_, true, engaged_)`; recompute `engaged_` (P5); if `engaged_`, `beginCell()`. Off the grid: `freeFinished(clock_, false, engaged_)` only.
  - **`processBlock` engaged:** `zero` every busy lane for `numFrames`; spawn as today; `renderPool` routes row i to `lane[i]`'s output when `rows.lane[i] != kNoLane` (FB16: bus 0 `{scratch(l,0), numChannels > 1 ? scratch(l,1) : nullptr}`, bus 1 `{scratch(l,2), scratch(l,3)}` or bus 0's when `bus2` is null), else to `out`/`bus2` as today, in one pass in spawn order; then for each lane of `busyByOwner`: `filter(l, numFrames)`, `out[c][t] += y_c` and `bus2[c][t] += y_{2+c}` (only `y0`, `y1` with `bus2` null; only `out[0]` with one channel; nothing with `out` null, the lanes still filtering their zeroed scratch); then `advance(numFrames)`; `clock_ = blockEnd`; retire as today.
  - **Not engaged:** today's code exactly.

- [ ] **Step 1: Write the failing tests:**
  - `the partition is exact`, for each of TwoBus (Bus Split 0.5), FiveArgument and Mono: three voices, Low-pass, seeded cells per voice at every grid call, Resonance 0.7, no steal. Reference (`EngineReference.h`): each sink grain rendered by the scalar reference kernels into its voice's own 4-channel buffer, each buffer filtered by a fresh `VoiceFilterBank` lane given the same type and cells, then the direct grains summed in spawn order and the lanes added in ordinal order. REQUIRE `memcmp` equal to the engine.
  - `off to a type engages at a grid sample and fades from dry`: four voices, `setVoiceFilterType(LowPass)` before the call at 1,000; `voiceFilterEngaged()` false at 1,000 and true from 1,024; `voiceFilterFading()` until 1,024 + R; `clickScore` (gg::metrics) of the render below 15.
  - `type to type, a turn back and a third type`: Low-pass → High-pass, back to Low-pass mid-fade, then Notch mid-fade (waits; `voiceFilterInForce()` and fading traced at grid samples equal a hand-written expected trace); `clickScore` below 15.
  - `type to off returns to the direct render`: Low-pass engaged, then Off; engaged until the first grid sample at or after the fade's end, then false; from that sample on, the output is `memcmp`-equal to a run that never engaged; between the fade's end and that sample every |difference| is at most `4 × FLT_EPSILON × peak`.
  - `a non-finite cell renders finite` (Review Focus 2): NaN cutoff, ∞ cutoff and NaN resonance cells, Resonance 1; every sample finite, peak below 64.
  - `a null out while engaged keeps the lane timeline` (Review Focus 4): one run passes `out == nullptr` for 0.5 s mid-render; its `laneState`, `voiceFilterEngaged` and `voiceFilterFading` traces at every grid sample equal the run with buffers.
  - In `determinism_tests.cpp`: `voice filter renders are block-size invariant` (block sizes 1, 32, 64, 255, 256, 4096; engagement, fades both ways, Bus Split 0.5, mono and the five-argument form, frees) and `voice filter renders are invariant under random host blocks` (Review Focus 1: call sizes drawn from `gg::rng` in [1, 300], three seeds, equal to block size 32).
- [ ] **Step 2:** Run `ctest --preset dev -L 'voicefilter|determinism'`. Expected: the new cases FAIL.
- [ ] **Step 3: Implement** the splitting, the grid work, the engaged path and the partition. `engine_reference_tests.cpp` still passes on the moved helpers.
- [ ] **Step 4:** Run `ctest --preset dev -L 'engine|voice|voicefilter|determinism|kernel|budget'` and `check-at-rest.sh`. Expected: PASS, `0 changed` (Task 4's `voice filter off never reroutes` still green).
- [ ] **Step 5:** Commit `feat(engine): the voice filter's lane render, engaged at grid samples (F3, F4, FB3-FB5, FB14, FB16)`.

### Task 6: The lanes' life under load

**Files:**
- Modify: `engine/src/Engine.cpp`, `engine/src/VoiceFilter.cpp` (fixes the tests find)
- Test: `tests/engine/voice_filter_tests.cpp`, `tests/engine/rt_tests.cpp`, `tests/engine/determinism_tests.cpp`

- [ ] **Step 1: Write the failing tests:**
  - `a draining lane frees once its ring-out is below 2^-20`: Low-pass, 20 Hz, Resonance 1, one short voice; the lane frees at a grid sample after `lastEnd` where `laneStateMagnitude` is below `kLaneFreeLevel`, and at the grid sample before it the magnitude was at or above it; the free comes before `lastEnd + 4 s`.
  - `a merge glides without a step`: 40 voices started 60 ms apart with 1.5 s grains, finishing each after 50 ms (more than 16 draining), Resonance 0.9, alternating cells 200 Hz / 6.4 kHz; at least one merge happens (`laneOwner` of some lane changes while Draining); `clickScore` below 15; the partition reference (Task 5's helper, lanes by owner from each merge on) is `memcmp` equal.
  - `reset while engaged renders as a new engine` (Review Focus 3): engine A engaged mid-fade, then `reset()`; engine B new with the same config, source and type; the same next 1 s of calls; `memcmp` equal, events equal.
  - `the gate off mid fade then on starts clean`: after the gate turns back on, the first voice's lane jumps to the request (`voiceFilterFading()` false) and `clickScore` is below 15.
  - `a source removal while engaged rings the lanes out`: lanes Draining with `lastEnd` the clear's frame; output after it decays with `clickScore` below 15; every lane Free within 4 s.
  - In `determinism_tests.cpp`: `voice filter merges and frees are block-size invariant` (the merge schedule at 1, 32, 255, 4096).
  - In `rt_tests.cpp` (`[engine][rt]`): `an engaged voice filter does not allocate`: the tripwire over `process` with fades, merges, frees, a turn-back, a gate cycle and `reset()`.
- [ ] **Step 2:** Run `ctest --preset dev -L 'voicefilter|rt|determinism'`. Expected: any case the earlier tasks don't already satisfy FAILS; record which.
- [ ] **Step 3: Fix** what fails, in the bank or the engine, keeping Tasks 4 and 5's tests green.
- [ ] **Step 4:** Run `ctest --preset dev -L 'engine|voicefilter|determinism|rt'`, `ctest --preset asan -L 'voicefilter|rt'` and `check-at-rest.sh`. Expected: PASS, 0 changed.
- [ ] **Step 5:** Commit `test(engine): the voice lanes' frees, merges, resets and real-time safety (F2, F9, FB7, FB11, FB17)`.

### Task 7: The Processor's cells and type

**Files:**
- Modify: `core/include/gg/core/ParamMap.h`, `core/src/ParamMap.cpp` (`voiceFilterParams`), `core/include/gg/core/ModMatrix.h` (`isVoiceTarget` gains `VoiceCutoff`, `VoiceResonance`), `core/src/Processor.cpp` (`engineConfig`: `filterRampSamples = rampLength(sr)`; `updateEffective`'s per-layer loop: `setVoiceFilterType` beside `sendCloudParams(L)`; `updateVoices`: the cell), `tests/core/mod_matrix_tests.cpp` (Task 2's case flips for the two)
- Create: `tests/core/voice_filter_processor_tests.cpp` (`[core][voicefilter]`)

**Interfaces:**
- Consumes: Task 4's `VoiceParams` fields, `setVoiceFilterType`, `voiceParams(slot)`, `voiceFilterInForce()`.
- Produces: `struct VoiceFilterParams { BusFilterType type; float keyTrack; };` and `VoiceFilterParams voiceFilterParams(const ParamValues &effective, uint8_t layer) noexcept` (the type rounded as `busParams` rounds a bus type, 0–4; Key Track as it is). `static_assert` in `ParamMap.cpp` that each `gg::VoiceFilterType` value equals `BusFilterType`'s.
- The cell (FB9), per spawning voice at every applied cell, **outside** the `same`/cache branch: `cutoffSlot = clampToRange(VoiceCutoff's slot, sum)` with `sum = base + mod + offsets_ + voice offset`, NaN falling back to `base + mod`; `resonance` the same for Voice Resonance; `hz = (float) exp2Det((double) cutoffSlot + (double) keyTrack × pitchIn / 12.0)`. The two voice offsets are read and zeroed from `voiceOffsets_` as Level's is; they join neither `kVoiceTargets` nor the voice key.

- [ ] **Step 1: Write the failing tests:**
  - `velocity sets each voice's cutoff`: Velocity → Voice Cutoff Unipolar +5 (octaves); notes at velocities 20, 64, 127 together; each slot's `voiceParams(slot).filterCutoffHz == (float) exp2Det(log2(200) + 5 × v/127)` with base 200 Hz, exactly.
  - `key track moves the cutoff by key track times pitch in over twelve`: Key Track 1 and 0.5, keys 36, 60, 84 and a +12 bend on the 60; `filterCutoffHz == (float) exp2Det(log2(1000) + kt × pitchIn / 12)` exactly; Layer Pitch +7 leaves it.
  - `env 2, pressure and timbre move one voice's cell`: per-voice routes; only the expressed note's cell moves.
  - `a per-voice source isn't counted twice on the voice cutoff` (B2): a non-Voice-only Velocity → Voice Cutoff route moves each voice's cell once and the layer's effective Voice Cutoff not at all.
  - `an LFO on voice cutoff moves every voice`: LFO 1 → Voice Cutoff; every voice's cell equals the layer's effective value.
  - `a voice cutoff route never moves the voice recomputes`: Env 2 → Voice Cutoff, 2 s held chord; `voiceParamRecomputes()` equals the same render without the route.
  - `voice filter cells from extreme routes stay finite` (Review Focus 2): two depth-1e30 routes summing to ∞ − ∞ and a NaN-producing pair; cells finite, output finite.
  - `the type reaches the engine at its boundary`: Voice Filter automated to Low-pass at 0.5 s; `engine(0).voiceFilterInForce()` Off before the boundary and Low-pass (or fading to it) from it; a Cloud layer with Voice Filter Low-pass renders bit-identically to Off.
  - `a short tap's voice gets its cell at its boundary` (FB10) and `a switch to cloud drains the lanes through the filter` (FB10, F11: lanes Draining, Cloud grains' `lane` `kNoLane`).
  - `voice filter processor renders are block-size invariant` (1, 32, 64, 255, 256, 4096) and `voice filter renders the same bytes with pools of 0, 1, 2 and 8` (`[threads]`, FB13).
  - `[rt]`: `the voice filter in the processor does not allocate`.
- [ ] **Step 2:** Run `ctest --preset dev -L 'voicefilter|mod'`. Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** Run `ctest --preset dev -LE slow` and `check-at-rest.sh`. Expected: PASS, 0 changed.
- [ ] **Step 5:** Commit `feat(core): each voice's filter cell, Key Track, and the layer's type (F6, F7, FB8, FB9)`.

### Task 8: The plugin and gg-render's keys

**Files:**
- Test: `tests/plugin/params_tests.cpp`, `tests/plugin/rt_tests.cpp`, `tests/tools/render_spec_tests.cpp`, `tests/tools/preset_source_tests.cpp`
- Modify: `tools/gg-render/RenderSpec.h` (document the four keys and route targets), anything the tests find

- [ ] **Step 1: Write the failing tests:**
  - `plugin lists the voice filter's parameters`: for each layer, the four infos' IDs, names (`"A Voice Filter"` …), ranges, defaults; Voice Filter stepped; all automatable and modulatable.
  - `plugin process with the voice filter engaged does not allocate` (`[plugin][rt]`): notes, Voice Filter automated through every type, Voice Cutoff modulated, through `process`.
  - `plugin params flush with the voice filter does not allocate` (`[plugin][rt]`).
  - `render spec reads the voice filter's parameters`: `"A Voice Filter": 1`, `"B Voice Cutoff": 8.0`, `"C Voice Resonance"`, `"D Voice Key Track"` in `params` and in `automation`; `mod` routes to `"Voice Cutoff"` with `targetLayer` `"B"` and `"all"`.
  - `render spec refuses an unknown voice filter name`: `"A Voice Filtr"` → the unknown-key error (exit 2 through the CLI test).
  - `a preset source takes the voice filter's four` (preset_source_tests): packs and reads back their values.
- [ ] **Step 2:** Run `ctest --preset dev -L 'plugin|render' -LE slow`. Expected: the cases that need code FAIL.
- [ ] **Step 3: Implement** what fails; document the keys.
- [ ] **Step 4:** Run the same, then the validators (`scripts/fetch-validators.sh tools-bin`; `clap-validator validate` and `pluginval --strictness-level 10` on the release products). Expected: PASS.
- [ ] **Step 5:** Commit `feat(plugin,render): the voice filter's parameters in the plugin and in specs`.

### Task 9: The sweep case

**Files:**
- Modify: `tests/sweep/sweep_extremes_tests.cpp`, `tests/sweep/SweepDriver.h` and `.cpp` (a `setRoutes(std::span<const gg::core::ModRoute>)` if it has none)

- [ ] **Step 1:** Add `voice-filter-extremes: 16 voices at Resonance 100, instrument` (`[sweep]`): layer A Voice, 16 held notes over keys 0–127, Voice Resonance 1, Key Track 1, Env 2 and LFO 1 (rate at the cell rate's order) → Voice Cutoff ±10 octaves, Voice Filter stepped 0→4 every 100 ms; `requireClean` with finiteness, the peak bound and the click score; its scored-share floor pinned a little under the first measure, with the reason in a comment.
- [ ] **Step 2:** Run `ctest --preset dev -L sweep` and `ctest --preset asan -L sweep -R voice-filter`. Expected: PASS; any other floor that moved is updated with its reason.
- [ ] **Step 3:** Commit `test(sweep): voice-filter-extremes`.

### Task 10: The seven goldens

**Files:**
- Create: `tests/golden/cases/{inst-voice-filter-velocity,inst-voice-filter-env,inst-voice-filter-mpe,inst-voice-filter-keytrack,inst-voice-filter-switch,inst-voice-filter-steal,fx-voice-filter}.json` and their expected files
- Modify: `tests/metrics/click_injection_tests.cpp` (entries with reasons for any case that hides steps)
- Create: `tests/metrics/voice_filter_golden_tests.cpp` (`[golden][voicefilter]`)

- [ ] **Step 1:** Write the seven specs as spec §4.9 describes them (6 to 8 s, block 256, seed 1).
- [ ] **Step 2:** Write `the voice filter never touches a grain`: for each of the seven, render in process (the click-injection tests' `loadCase` and render helpers) as written and with every `"<L> Voice Filter"` param set to 0 and its automation points removed; REQUIRE the event lines equal.
- [ ] **Step 3:** Run `scripts/update-goldens.sh build/dev`; then restore every older case's expected files (`git checkout --` the tracked ones) and run `check-at-rest.sh`. Expected: the seven are new; `0 changed`. Look at every new spectrogram.
- [ ] **Step 4:** Run `ctest --preset dev -L golden`. Expected: PASS, click injection included (each new case catches 16 or has a table entry with its reason).
- [ ] **Step 5:** Commit `test(golden): seven voice filter cases; every earlier WAV hash unchanged` (the new cases' files and the test by name; `git diff --cached --stat -- tests/golden` lists only them), with the hashes in the message.

### Task 11: The benchmarks

**Files:**
- Modify: `tools/gg-bench/Bench.h`, `tools/gg-bench/Bench.cpp` (`engine-voices-filter` after `engine-voices`; `ChainStats::voiceLane` and its JSON key `"voice-lane"`), `tools/gg-bench/CMakeLists.txt` (`engine/src` include, P3)
- Test: `tests/tools/bench_tests.cpp`

- [ ] **Step 1: Write the failing tests:** `engine-voices-filter runs engaged` (its setup's engine reports `voiceFilterEngaged()` after the warm-up and every voice owns a lane) and `the chain bench reports a voice-lane stage` (its JSON has `"voice-lane"` with a positive `nsPerSample`).
- [ ] **Step 2:** Implement: `engine-voices-filter` is `engine-voices` with Low-pass, 1 kHz, Resonance 0.5 on every voice (report-only, no baseline entry); `voice-lane` times one lane's four-wide step with `beginCell` every 32 samples over stereo noise, in ns per lane-sample.
- [ ] **Step 3:** Run `cmake --build --preset release --target gg-bench`, then `build/release/tools/gg-bench/gg-bench --quick --cases reference,engine-sample,engine-voices,engine-voices-filter` and `--chain --quick`. Expected: both run; note the ratios and the added cost (`engine-voices-filter` minus `engine-voices`).
- [ ] **Step 4:** Commit `bench: engine-voices-filter and the --chain voice-lane stage (report-only)`, quoting the figures.

### Task 12: Records

**Files:**
- Modify: `INTENT.md` (spec §6: §14 Voice mode and Grain buses, §15 the PRIVATE link, §16 the two report-only figures, §17 the Filter panel, §18 the parameters, no version change, presets, Mutate locks; §22 the cross-reference; §23 one row, number settled at merge, with F1–F14, FB1–FB17 and P1–P10 as Claude's provisional calls), `CLAUDE.md` (label `voicefilter`; the spec keys; the seven goldens and the new totals; 306 parameters; `engine-voices-filter` and the `voice-lane` stage; the run's 66 configurations), `docs/m3-carry-over.md` (F10's locks to revisit with sub-project 5's Voice presets; a remote-controls page; the `gg-snap` regeneration if P10 defers it)
- Modify (P10): `docs/listening/m2-views/snapshots/` if `gg-snap` runs here

- [ ] **Step 1:** Write them; regenerate and look at the snapshots, or record the row.
- [ ] **Step 2:** Commit `docs(m3): the voice filter's records (INTENT, CLAUDE.md, carry-over)`.

### Task 13: The gate

- [ ] **Step 1:** Run `scripts/check.sh --presets dev,asan` (and `rtsan` where `GG_LLVM` is available; one gate on the machine at a time). Expected: `PASS`.
- [ ] **Step 2:** Run `check-at-rest.sh`. Expected: `at rest: 108 + 8 goldens, 0 changed; schedules 0 changed`.
- [ ] **Step 3:** Push and open the draft PR (base `m3-voice` while its PR #37 is open, else `main`); fix CI's GCC 13 diagnostics and the RTSan job if they report anything.
- [ ] **Step 4:** Render the listening files (`docs/listening/m3-voicefilter/`: the seven cases' WAVs, each beside its filter-Off twin, and a README of what to listen for: one note brightening while its neighbours hold, the type switch's declick, the merges in `inst-voice-filter-steal`).
- [ ] **Step 5:** The final report: the at-rest line, the bench figures, the sweep floor, any click-injection entries, the rulings (F13's placement), and the carry-over rows.
