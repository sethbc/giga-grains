# M3 Pitch-sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Voice layer gains a Clock, Free (sub-project 1's voices) or Pitch, where each voice spawns grains at its note's frequency, Pitch sets the formant, Size counts periods, Density sets the refresh and Distribution jitters the period; at Clock Free nothing moves.

**Architecture:** The voice's `CloudScheduler` gains a Pitch branch (onsets at the period, the jitter law, the length in periods, draws keyed by the last refresh grain's index, `GrainDraw::held`), reached only when `CloudParams::clock` is Pitch. The engine keeps a held read anchor per voice for the ring kinds. The `Processor`'s voice pass turns the note's tuned pitch (one helper, `notePitch`, the seam sub-project 4 fills) into f0, and Pitch, Density and Size into the formant, the refresh and the gain.

**Tech Stack:** C++23, CLAP 1.x, clap-wrapper, Catch2, CMake presets, `gg-render` goldens, `gg-bench`.

**Spec:** `docs/superpowers/specs/2026-11-07-m3-pitchsync-design.md` (S1 to S14, SB1 to SB12, §4; parent: `docs/superpowers/specs/2026-11-07-m3-program-design.md`). Builds on sub-project 1's plan, `docs/superpowers/plans/2026-11-07-m3-voice.md`. File references are at `3f4eb3f` (this branch after merging `m3-voice` with its Task 7).

**The merge check (m3-voice's Task 7, merged at `3f4eb3f`).** Task 7 touched the note table (NOTE_END for a replaced note), the plugin's input paths and the tail; none of the spec's seams moved. `updateVoices` still computes `pitchIn = (key − 60) + bend + tuning` inline (`core/src/Processor.cpp:1040`), and the voice API (`startVoice`, `setVoiceParams`, `scheduleVoices`) is as the spec read it. Four adjustments, decided here:
1. **The seam's name is `notePitch`, not `noteSemitones`.** Sub-project 4's spec (`~/src/gg-m3-sp4-tuning`, its §5.3 and §9) names the helper `double Processor::notePitch(const NoteTable::Note &) const noexcept` (key term + bend + tuning expression) and says pitch-sync computes f0 from it. This plan adds that helper with today's body, so sub-project 4 changes one line in it. The C4 constant: this branch adds `gg::core::kC4Hz` in `ParamMap.h`; sub-project 4 adds `TuningTable::kC4Hz`. Whichever merges second makes one alias the other.
2. **`Clock` lives in the engine.** `CloudParams` (engine) carries the clock, and the engine can't include gg-core. So `enum class Clock : uint8_t { Free, Pitch }` goes in `engine/include/gg/grain/CloudScheduler.h` (namespace `gg`), and `ParamMap.h` declares `gg::Clock clockOf(float)` beside `layerModeOf`. `kPeriodsPerSizeSecond` is `CloudScheduler::kPeriodsPerSizeSecond`, and `ParamMap.h` re-exports it.
3. **The sweep count is 33 on this branch, not 30** (sub-project 1 added three). The three new cases take it to 36.
4. **`engine-pitchsync` reports in gg-bench's own unit** (ns per rendered grain-sample, with its ratio to `reference`), as every engine case does. The commit also quotes ns per output sample, which is that unit × 32: 16 voices × N = 2 sounding grains each.

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
- **This sub-project's IDs:** layer index **0x38** only (`X(Clock, 0x38, "Clock")`, CLAP IDs 0x010038 to 0x040038). It reserves layer 0x38 to 0x3B and globals 0x30 to 0x37, and uses no global. 0x39 stays free for a later Formant Track (S2). `kNextFreeLayerIndex` 0x39, `kLayerParamCount` 64, `kParamCount` 294.
- **The voice goldens:** the eight `*voice*` cases keep their WAV SHA-256 through every task (no task here changes voices at Clock Free). The at-rest check prints them; compare them with the hashes in Task 9's commit of sub-project 1 (`git log -1 --format=%B <that commit>`, or `build/dev/golden/wav-sha256.txt` before your first change).
- **Floating point (SB11):** every new expression runs under `GG_FP_CONTRACT_OFF`. f0, the read rate and the refresh rate come from `exp2Det`, never libm. d³ is `d * d * d`. The interval is `period_ * (1.0 + 0.5 * d3 * b)`, evaluated in that order.
- **Clock Free takes today's path:** at Free, `setParams`, `beginRun` and `takeNext` run today's arithmetic. The only new instruction Free can reach is the test of `clock` (SB2). The Cloud stream is always given Free (`toCloudParams` sets neither new field).
- Models for this plan: opus implements Tasks 2, 3 and 4; sonnet implements the rest; reviewers sonnet at least.

## Review Focus

1. **Another sample rate (44.1, 96 or 192 kHz).** The f0 ceiling is sampleRate / 4 (11,025 Hz at 44.1 kHz, so key 127 clamps there). The refresh period and a grain's length in periods scale with the rate, and the Edit view's numbers stay the same. (Task 2's `pitchsync scheduler scales with the sample rate`.)
2. **Ring Length shrunk, or Freeze toggled, while a voice holds an anchor on the ring.** Held grains are clamped into the new window, read nothing outside it, and stay finite with no click. A hold that outlives the window reads from the oldest admissible frame. (Task 3's `a held anchor survives a ring shrink and a freeze toggle`.)
3. **A tuning expression at ±120 semitones on keys 0 and 127.** f0 clamps to [1 Hz, sampleRate / 4] at both ends, and grain lengths stay within [16 samples, 2 s]. The read rate (the formant) doesn't move. (Task 4's `pitchsync_f0_extremes_clamp`.)
4. **The 17th note at Clock Pitch.** The stolen voice's grains play out their windows. The new voice's first grain is a refresh with draws of its own and no anchor carried over from the slot's previous voice. (Task 4's `pitchsync_steal_starts_fresh`.)
5. **A Clock change and a note-on in the same cell** (a preset load or automation landing with a chord). Each voice gets exactly one forced onset at that boundary: one grain, not two. (Task 4's `pitchsync_clock_and_note_same_cell`.)

---

### Task 1: The Clock parameter

**Files:**
- Modify: `core/include/gg/core/ParamIds.h` (new list `GG_LAYER_PARAMS_M3_PITCHSYNC(X) X(Clock, 0x38, "Clock")`, after `GG_LAYER_PARAMS_M3` and before `GG_LAYER_PARAMS_M2_SOURCES` in both `GG_LAYER_PARAMS` and `GG_LAYER_PARAMS_SINCE_M1`; `kLayerParamCount` 64; `kNextFreeLayerIndex` 0x39; the comment naming the reserved ranges), `core/src/ParamIds.cpp` (metadata as Mode's: `asInt`, 0..1, default 0, `{{0, "Free"}, {1, "Pitch"}}`, `kAutomatableModulatable | CLAP_PARAM_IS_STEPPED`), `core/include/gg/core/ModMatrix.h` (`isValidTarget` refuses Clock), `core/src/Mutate.cpp` and `core/include/gg/core/Mutate.h` (lock each layer's Clock in `structuralLocks`, beside Mode), `core/include/gg/core/ParamMap.h` and `core/src/ParamMap.cpp`, `ui/src/EditLayout.cpp` (`edit(ParamSlot::Clock, Panel::Scheduler, 3)`)
- Test: `tests/support/ParamTable.h`, `tests/core/patch_tests.cpp`, `tests/core/parammap_tests.cpp`, `tests/core/mod_matrix_tests.cpp`, `tests/core/mutate_tests.cpp`, `tests/core/preset_tests.cpp`, `tests/plugin/identity_tests.cpp`, `tests/plugin/params_tests.cpp`, `tests/plugin/rt_tests.cpp`, `tests/ui/edit_layout_tests.cpp`, `tests/tools/render_spec_tests.cpp`, `tests/tools/preset_source_tests.cpp`, and any sweep floor that moves (as sub-project 1's Task 2 moved `bursts: 16 x 1024 events, instrument`)

**Interfaces:**
- Consumes: nothing new. This task can run beside Tasks 2 and 5.
- Produces:
  - `LayerParam::Clock`; `ParamSlot::Clock`, `B_Clock`, `C_Clock`, `D_Clock`; gg-render's `"A Clock"` to `"D Clock"`, automatic from the macros.
  - In `ParamMap.h` (namespace `gg::core`): `gg::Clock clockOf(float value) noexcept` (≥ 0.5 is Pitch; below it, or NaN, is Free), `inline constexpr double kC4Hz = 261.6255653005986;`, `double noteFrequencyHz(double notePitch) noexcept` (= `kC4Hz * exp2Det(notePitch / 12.0)`, unclamped), `inline constexpr double kPeriodsPerSizeSecond = gg::CloudScheduler::kPeriodsPerSizeSecond;` and `double sizePeriods(float sizeLog2Seconds) noexcept` (= `exp2Det(sizeLog2Seconds) * kPeriodsPerSizeSecond`).
  - This task adds `enum class Clock : uint8_t { Free, Pitch };` and `static constexpr double kPeriodsPerSizeSecond = 40.0 / 3.0;` (public, in `CloudScheduler`) to `engine/include/gg/grain/CloudScheduler.h`, with no behaviour. Task 2 uses them. Whichever of Tasks 1 and 2 lands second keeps the other's lines.

- [ ] **Step 1: Write the failing tests.**
  - Update `ParamTable.h` and every count to 294 parameters and 64 a layer. Add the rows `0x00010038` to `0x00040038`: "Clock", stepped 0..1, default 0, texts "Free" and "Pitch".
  - `patch_tests.cpp`: update the pinned state bytes (four more `<p>` elements). Add `a state without Clock reads Clock Free` (S13): strip the four `<p>` elements from a saved state; it restores with every Clock at 0.
  - `parammap_tests.cpp`:
    - `clockOf reads 0.5 and above as Pitch`: 0, 0.49 and NaN are Free; 0.5 and 1 are Pitch.
    - `noteFrequencyHz gives 440 at A4`: `noteFrequencyHz(9.0)` within 1e-12 relative of 440.0, and `noteFrequencyHz(0.0) == kC4Hz` exactly.
    - `sizePeriods counts periods`: `sizePeriods(log2f(0.15f))` within 1e-6 of 2.0. log2 0.005 (Size's bottom) gives 1/15 within 1e-6, and 1.0 gives 80/3 within 1e-9.
  - `mod_matrix_tests.cpp`: `Clock is no route target`: `isValidTarget(ParamSlot::Clock)` is false.
  - `mutate_tests.cpp`: `Mutate locks every layer's Clock`: `structuralLocks` holds the four slots in both products.
  - `preset_tests.cpp`: `a preset carries Clock` (S12): a preset encoded with A Clock 1 reads back with A Clock 1, and loading it sets the instance's A Clock (unlike the ring parameters).
  - `preset_source_tests.cpp`: a source with `"A Clock": 1` packs it.
  - `edit_layout_tests.cpp`: `slotsIn(Panel::Scheduler, 1)` is `{B_Density, B_Distribution, B_Freeze, B_Clock}`.
  - `render_spec_tests.cpp`: the parameter list in the unknown-key message reads `"A Step 16, A Mode, A Clock, A Ring Length"`.
- [ ] **Step 2:** Run `ctest --preset dev -L 'core|plugin|ui|render' -LE slow`. Expected: the updated cases FAIL.
- [ ] **Step 3:** Add the list, the metadata, the target refusal, the locks, the placement and the `ParamMap` helpers. Update every comment that lists the layer lists or the counts.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS, including the editor frame tests: "every parameter is reachable from the UI", no label elided at 960 × 600 and 640 × 360, `editor_labels_dejavu`. Run `ctest --preset dev -L sweep`. If a floor moves, re-pin it and give the reason, as sub-project 1 did. Run the at-rest check. Expected: `baseline cases: 108, changed: 0, gone: 0`, and the eight voice WAVs unchanged.
- [ ] **Step 5:** Commit `feat(params): Clock per layer (0x38), Free or Pitch; not a target, locked by Mutate`.

### Task 2: The Pitch clock in the scheduler

**Files:**
- Modify: `engine/include/gg/grain/CloudScheduler.h`, `engine/src/CloudScheduler.cpp`, `core/src/Processor.cpp:178-199` (`sameCloud` and its `sizeof` assert take the new fields)
- Create: `tests/engine/pitchsync_tests.cpp` (tags `[engine][pitchsync]`; add it to `tests/CMakeLists.txt`), `tests/engine/VoiceFixture.h` (move `voice_tests.cpp`'s `config`, `unityAmp`, `voiceAt`, `sineSource`, `GrainLog`, `Audio`, `renderSpan`, `silentAudio` and `sameBits` here, in `namespace gg::test`, and include it from both files)

**Interfaces:**
- Consumes: Task 1's `Clock` and `kPeriodsPerSizeSecond` (add them here if Task 1 hasn't landed).
- Produces:
  - `CloudParams` gains two fields at its end: `Clock clock{Clock::Free};` and `double refreshHz{0.0};`. At Pitch, `densityHz` is f0 (≤ 0 means off, as at Free; a positive value is clamped to [1, sampleRate / 4]). `grainDurationSec` is Size's seconds. `refreshHz` is sanitized: non-finite means 16, then it is clamped to [0.01, sampleRate]. Free ignores `refreshHz`.
  - `GrainDraw` gains `bool held{false};` at its end: true for a non-refresh grain at Pitch, always false at Free.
  - `void CloudScheduler::restartRefresh() noexcept`: the next grain is a refresh (Task 3 calls it when it drops an anchor). `reset()` does the same.
  - At Pitch, with P = `period_` = sampleRate / f0:
    - **onsets** follow Δ(k) = `period_ * (1.0 + 0.5 * d3 * b)`, with d3 = d·d·d and b = `rng::bipolar(key(Onset))` keyed by the grain's own k. `lastE_` holds b, so M1-h's rescale uses the Pitch law (SB3). There is no `kMinIntervalSamples` floor.
    - **length** is `std::clamp(S * kPeriodsPerSizeSecond * period_, 16.0, 2.0 * sampleRate_)`, with S = `grainDurationSec`. With a size spread, S is first `exp2Det(clamp(log2 S + spread × b', log2 0.005, 1))`, as today, where b' is the SizeSpread draw.
    - **refresh:** grain k is a refresh when no refresh is valid, or when `t - refreshOnset_ >= sampleRate_ / refreshHz` (the rate in effect at its emission). A refresh sets `refreshK_ = k` and `refreshOnset_ = t`. Every draw but Onset is keyed by `refreshK_`; `GrainDraw::index` stays k.
    - **a Clock change** in `setParams` forces the next onset (`onsetForced_`), drops any pending rescale and invalidates the refresh (S9).

- [ ] **Step 1: Pin today first.** Before changing any engine line, add `free voices and the Cloud stream are unchanged by the clock`. It renders two engines for 2 s at block 256: Sample mode over `sineSource()` with four voices of `kParams` (ordinals 0 to 3), and Live mode over a 220 Hz sine input with the Cloud stream at `kParams`. It folds the output bytes and every logged `GrainSpec`'s `index, onsetSample, onsetFrac, startPos, rate, durationSamples, gain, pan, bus, voice` into an FNV-1a 64 hash. Run the test on the unchanged code, write the two printed hashes into it as constants, and commit them on their own (`test(engine): pin Free voices and the Cloud stream before the Pitch clock`).
- [ ] **Step 2: Write the failing tests** in `pitchsync_tests.cpp`. Each uses one voice at Clock Pitch over `sineSource()` at 48 kHz, with f0 = 100 Hz (P = 480 exactly) unless it says otherwise.
  - `pitch onsets fall at exactly k times P at distribution 0`: onsets 0, 480, 960, …, each with `onsetFrac == 0.0`, for 200 grains. At f0 = 110 Hz the first 200 onsets equal `k * (48000.0 / 110.0)` summed one interval at a time (`t += P`).
  - `pitch grains are N periods long`: Size 0.15 s gives `durationSamples == 0.15 * kPeriodsPerSizeSecond * 480.0`. Size 0.005 s at f0 = 12000 Hz gives 16.0. Size 2 s at f0 = 1 Hz gives 96000.0.
  - `pitch f0 clamps to one hertz and a quarter of the sample rate`: f0 0.25 Hz has onsets 48000 apart; f0 20000 Hz has onsets 4 apart; f0 0 spawns nothing.
  - `the period jitter stays within half a period at distribution 1`: 2,000 intervals, each in [0.5 P, 1.5 P], with a mean within 1% of P. At d = 0.25 each is within ±0.0078125 P.
  - `held draws repeat within a refresh`: at refreshHz 16 (3,000 samples), refreshes are at k = 0, 7, 14, 21 (`held` false there, true elsewhere). Within each group, `positionDraw`, `sprayOffsetSec`, `rate`, `durationSamples`, `pan` and `bus` equal the refresh's. Run it with spray 0.3, stereo 0.8, busSplit 0.5, a pitch spread of 2, a size spread of 0.5 and the Smear jitter and detune on, so every field is drawn. The next group's values differ.
  - `the refresh follows the rate in effect`: refreshHz changes from 16 to 400 at a boundary, and every grain after it is a refresh.
  - `a clock change forces an onset and a refresh`: a voice at Free with densityHz 0.25 (pending onset 4 s away) is switched to Pitch at sample 3200; the next grain is at 3200 and is a refresh. Switching back to Free at 6400 puts the next grain at 6400.
  - `a pitch slide rescales the pending onset`: f0 changes from 100 to 200 Hz at boundary 1000. The next onset is `max(480 * 2 + 240, 1000)` = 1200, with the same draw.
  - `pitchsync scheduler scales with the sample rate` (Review Focus 1): at 44.1, 96 and 192 kHz, f0 = 20000 clamps to sampleRate / 4, and refreshHz 16 refreshes at the first grain at or past sampleRate / 16 samples.
  - `restartRefresh makes the next grain a refresh`.
  - In `cloud_tests.cpp`: `the cloud stream ignores refreshHz at Free`: draws with `refreshHz` 0 and 400 are equal (`operator==`) over 500 grains.
- [ ] **Step 3:** Run `ctest --preset dev -L 'pitchsync|voice|cloud'`. Expected: the new cases FAIL to compile, then fail.
- [ ] **Step 4: Implement.** `updateDerived` sets `period_` from the clamped f0 at Pitch, and `refreshPeriod_` (a new member: sampleRate / refreshHz). Its Free branch stays as written. New members: `uint64_t refreshK_`, `double refreshOnset_`, `double refreshPeriod_`, `bool refreshValid_`. `interval()` branches on the clock. In `takeNext`, the `key` lambda indexes by `params_.clock == Clock::Pitch && field != rng::Field::Onset ? refreshK_ : k`. The Pitch branch computes the duration in place of `durationSamples_`. `sameCloud` compares `clock` and `refreshHz`.
- [ ] **Step 5:** Run `ctest --preset dev -L 'engine|voice|cloud|determinism|kernel'`. Expected: PASS, with Step 1's pins unchanged. Run the at-rest check. Expected: 108 unchanged, and the eight voice WAVs unchanged.
- [ ] **Step 6:** Commit `feat(engine): the Pitch clock in the scheduler (onsets at the period, length in periods, held draws per refresh)`.

### Task 3: The held anchor on the ring

**Files:**
- Modify: `engine/include/gg/engine/Engine.h` (`Voice` gains `VoiceAnchor anchor`; `liveGrain`'s new parameter; `const VoiceParams &voiceParams(uint32_t slot) const noexcept`, a test accessor returning the slot's last `startVoice` or `setVoiceParams` params; the class comment's **Voices** paragraph), `engine/src/Engine.cpp` (`scheduleVoices`, `spawnAs`, `liveGrain`, `startVoice`, `clearVoices`, `reset`, the mode change in `process`)
- Test: `tests/engine/pitchsync_tests.cpp`, `tests/engine/determinism_tests.cpp`

**Interfaces:**
- Consumes: Task 2's `GrainDraw::held`, `CloudParams::clock` and `restartRefresh()`.
- Produces:
  - A private `struct VoiceAnchor { double start{0.0}; uint8_t slot{kNoAnchor}; };`, with `static constexpr uint8_t kNoAnchor = 0xFF;`.
  - `bool liveGrain(const GrainDraw &, GrainSpec &, const LiveSource &, uint8_t slot, VoiceAnchor *anchor) const noexcept`. A null anchor is today's code. The Cloud stream, Free voices and sample slots always pass null.
  - With an anchor: a `held` draw whose `anchor->slot == slot` starts at `std::clamp(anchor->start, lo, hi)`. Any other draw starts as today and stores the unclamped `rho - d.positionDraw * config_.sampleRate` and `slot` in the anchor. The store happens before the `lo > hi` drop, so a dropped refresh's held grains aim at the place it asked for.
  - `scheduleVoices` calls `spawn(const GrainDraw &, uint32_t ordinal, uint32_t slot)`, and the Cloud path passes `kMaxVoices` as the slot. `spawnAs` passes `&voices_[slot].anchor` only when `slot < kMaxVoices` and that voice's scheduler is at `Clock::Pitch`.
  - Dropping a voice's anchor (`startVoice`, `clearVoices`, `reset`, and an applied source-mode change in `process`, where `requestedMode_ != mode_`) sets `slot = kNoAnchor` and calls that voice's `scheduler.restartRefresh()` (SB4). `updateLiveBound`'s `applySourceMode` call (a ring resize) drops nothing.

- [ ] **Step 1: Write the failing tests** in `pitchsync_tests.cpp`, over a Live engine (a 1 kHz stereo sine input; `setSourceMode(Live)`) with one voice at Pitch, f0 100 Hz, refreshHz 4, spray 0.5 s and Position 0.3 s, started after 1 s of input so the clamp's lower bound doesn't bind:
  - `held grains keep the refresh's ring start`: within a refresh group, every `GrainSpec::startPos` equals the refresh's (the anchor stays inside [lo, hi] while the head moves on). With spray 0, refreshes fall every 25 grains (12,000 samples), and each refresh's `startPos` is exactly 12,000 frames after the previous one's.
  - `a spectral voice holds its anchor on the smear's ring`: the same over `SourceMode::Spectral` with `kSpectralSlot` grains.
  - `sample voices and free voices keep no anchor`: a Sample-mode Pitch voice's held grains differ in `startPos` when Position moves between them (SB5). A Free voice over the ring renders as Task 2's Step 1 pin.
  - `a source mode change drops the anchors`: Live to Spectral mid-group makes the next grain a refresh (`held` false) on the new slot.
  - `startVoice and reset drop the anchor`: a voice restarted in the same slot has a refresh first, whose start comes from its own draw.
  - `a held anchor survives a ring shrink and a freeze toggle` (Review Focus 2): with refreshHz 0.25, `setRingLimit` shrinks the ring to 1 s mid-hold, then Freeze goes on and off. Every held grain's `startPos` is in its [lo, hi], or the grain is dropped and counted. The output is finite, and its largest sample-to-sample step is under 0.05 for a 0.5-amplitude input.
  - `top-note pitch voices fill the pool without overflow`: 16 voices at f0 = 12000 Hz (P = 4), Size 0.005 s (16-sample grains, four deep), Sample mode, 1 s. Admitted grains, counted in the sink (not stored), are 192,000 ± 16. `droppedGrains()` is 0, at most 64 grains sound at once, and onsets never decrease.
  - In `determinism_tests.cpp`: `pitch voices with anchors are block-size invariant`: 4 Live voices at Pitch (two refreshes each, a Clock change at 9600 and an f0 slide over 10 boundaries), blocks 1, 7, 32, 256 and 4096. Bytes and grain lists are equal.
- [ ] **Step 2:** Run `ctest --preset dev -L pitchsync`. Expected: FAIL.
- [ ] **Step 3:** Implement as the Interfaces block says. Update the class comment: Live mode's `startPos` for a held voice grain, and the drop rules.
- [ ] **Step 4:** Run `ctest --preset dev -L 'engine|live|spectral|voice|determinism|rt'`. Expected: PASS, with Task 2's Step 1 pins unchanged. Run the at-rest check. Expected: 108 unchanged, and the eight voice WAVs unchanged.
- [ ] **Step 5:** Commit `feat(engine): a pitch-sync voice holds its read anchor on the ring between refreshes`.

### Task 4: The Processor's mapping

**Files:**
- Modify: `core/include/gg/core/Processor.h` (`double notePitch(const NoteTable::Note &) const noexcept`, private; `Layer::voiceKey`'s size follows `kVoiceKeySlots`), `core/src/Processor.cpp:217-226` (`kVoiceKeySlots` = `kVoiceTargets.size() + 4`, the fourth extra slot being the layer's Clock), `:1040` (`pitchIn` from `notePitch`), `:1073-1094` (the Pitch branch)
- Create: `tests/core/pitchsync_tests.cpp` (tags `[core][pitchsync]`; add it to `tests/CMakeLists.txt`)
- Test: `tests/plugin/rt_tests.cpp`

**Interfaces:**
- Consumes: Task 1's `clockOf`, `noteFrequencyHz` and `kPeriodsPerSizeSecond`; Task 2's `CloudParams::clock` and `refreshHz`; Task 3's `Engine::voiceParams`.
- Produces:
  - `notePitch(note)` = `(static_cast<double>(note.key) - 60.0) + bendSemitones(note) + static_cast<double>(note.tuning)`, today's `pitchIn` expression moved into the helper unchanged. Sub-project 4 changes its key term; the Note Pitch source keeps its own expression here.
  - On a Voice layer whose `clockOf(effective_[Clock slot])` is Pitch, in the recompute branch, after `toCloudParams` (§4.4):
    - `cloud.clock = Clock::Pitch`;
    - `cloud.densityHz = noteFrequencyHz(pitchIn)`;
    - `cloud.pitchSemitones = clamp(effective Pitch + pitchOffset, ±48)`, with no `pitchIn`;
    - `cloud.refreshHz = exp2Det(value(voiceScratch_, densitySlot))`, read before the scratch is restored, so the voice's Density offset is in it and Smear's term isn't;
    - `cloud.gain = static_cast<float>(1.0 / std::sqrt(std::max(1.0, cloud.grainDurationSec * kPeriodsPerSizeSecond)))`.
  - A Cloud layer, and a Voice layer at Free, run today's lines.

- [ ] **Step 1: Write the failing tests** in `tests/core/pitchsync_tests.cpp`. They use an instrument `Processor` at 48 kHz with layer A at Mode Voice and Clock Pitch over a 2 s sine sample, and read each voice's params through `processor.engine(0).voiceParams(slot)` and the event sink.
  - `pitchsync_f0_from_the_note`: key 69 gives `densityHz` within 1e-12 relative of 440; key 57 gives 220.
  - `pitchsync_bend_and_tuning_move_f0_not_the_rate`: at key 69, a bend of +2 semitones (Bend Range 2, full bend) and a tuning expression of +0.5 move `densityHz` to `noteFrequencyHz(9 + 2 + 0.5)`, and `pitchSemitones` stays the layer's Pitch.
  - `pitchsync_pitch_moves_the_rate_not_f0`: Pitch +12 makes `pitchSemitones` 12 with `densityHz` unchanged. A Pitch route offset of +5 from LFO 1 adds 5. Pitch +40 plus a +20 route clamps to 48.
  - `pitchsync_f0_extremes_clamp` (Review Focus 3): key 0 with a tuning expression of −120, and key 127 with +120. The grains' onsets are 48,000 and 4 samples apart, every `durationSamples` is in [16, 96000], and the output is finite.
  - `pitchsync_density_is_the_refresh`: Density 4 (16 Hz) and Smear 1 give `refreshHz == 16.0` exactly and `densityHz` = f0. Factory route 44 (Pressure → Density) at pressure 1 raises `refreshHz` to `exp2Det(4 + 2)`.
  - `pitchsync_gain_is_one_over_root_n`: Size log2 0.15 gives `gain == static_cast<float>(1.0 / std::sqrt(std::max(1.0, 0.15 * kPeriodsPerSizeSecond)))`, computed from the same `exp2Det` seconds. Size log2 0.005 gives 1.0.
  - `pitchsync_clock_ignored_on_cloud_layers`: in both products, a Cloud layer at Clock Pitch renders byte-identically, with the same event lines, to Clock Free over 2 s with a chord.
  - `pitchsync_free_voice_unchanged`: a Voice layer at Clock Free renders byte-identically to the same render with the Clock slot never written. The eight voice goldens check the rest.
  - `pitchsync_clock_change_recomputes_once`: with 3 voices held, flipping Clock raises `voiceParamRecomputes()` by exactly 3. A second block with no change raises it by 0.
  - `pitchsync_clock_and_note_same_cell` (Review Focus 5): a Clock change and a note-on at the same sample give that voice exactly one grain at the boundary.
  - `pitchsync_steal_starts_fresh` (Review Focus 4): 17 notes at Clock Pitch over the live ring (FX). The stolen slot's new voice's first grain has `held` false (its event `pos` comes from its own draw, not the previous voice's), and the render's click score is under 15.
  - `pitchsync_block_size_invariant` (`[determinism][pitchsync]`): the FX with a Live Voice layer at Pitch, an MPE slide of +12 over 1 s, Density automated 1 → 7, Clock flipped twice and a chord. Blocks 1, 7, 32, 256 and 4096 give identical audio and identical per-layer event lines.
  - `pitchsync_no_allocation` (`[rt][pitchsync]`): the tripwire over 16 voices at keys 112 to 127 at Clock Pitch, Size log2 0.005, for 1 s at block 64.
  - In `tests/plugin/rt_tests.cpp`: `process allocates nothing with 16 pitch-sync voices at a top note` (`[plugin][rt]`): the same through the CLAP factory, with `params.flush` beside it.
- [ ] **Step 2:** Run `ctest --preset dev -L pitchsync`. Expected: FAIL.
- [ ] **Step 3:** Implement. Keep the `updateVoices` edit to the Clock key slot, the `notePitch` call and one guarded block after the `cloud.pitchSemitones` line, so sub-project 2's edits there merge cleanly.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS. Run the at-rest check. Expected: 108 unchanged, and the eight voice WAVs unchanged.
- [ ] **Step 5:** Commit `feat(core): Clock Pitch on Voice layers: f0 from the note (notePitch, the tuning seam), Pitch the formant, Density the refresh, 1/sqrt(N)`.

### Task 5: The f0 estimator (test support)

**Files:**
- Create: `tests/support/PitchEstimate.h` (header-only, `namespace gg::test`), `tests/metrics/pitch_estimate_tests.cpp` (tags `[metrics][pitchsync]`; add it to `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: nothing. This task can run beside Tasks 1 to 4.
- Produces: `double estimateF0(std::span<const float> x, double sampleRate, double minHz, double maxHz) noexcept`, which returns 0 when no lag's CMND falls under 0.1. The algorithm is YIN: the difference function, then the cumulative mean normalized difference (CMND), then the first lag τ0 in [sr/maxHz, sr/minHz] whose CMND is under 0.1, followed down to its local minimum. Then a refinement, decided here (the spec says parabolic interpolation, which alone misses ±0.1 cent at 4 kHz, where a period is 12 samples): take the largest m with m·τ0 ≤ min(4096, x.size() / 2), find the CMND's local minimum within ±2 samples of m·τ0, interpolate it parabolically to τm, and return sr·m / τm. Also `double centsBetween(double f, double ref)` = 1200·log2(f / ref).

- [ ] **Step 1: Write the failing tests:**
  - `estimateF0 finds band-limited sawtooths to a tenth of a cent`: additive sawtooths (harmonics up to 20 kHz, amplitude 1/h) at 55, 110, 220, 440, 1000, 2093 and 4000 Hz, 0.5 s at 48 kHz. `|centsBetween(estimateF0(...), f)| < 0.1` for each.
  - `estimateF0 returns 0 for white noise` (a seeded `gg::rng` noise, 0.5 s).
  - `estimateF0 ignores a phase offset and a DC offset`.
- [ ] **Step 2:** Run `ctest --preset dev -R estimateF0`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run the same command. Expected: PASS. If the 4 kHz case can't reach 0.1 cent with a 4,096-sample lag window, lengthen the window, not the tolerance.
- [ ] **Step 5:** Commit `test(support): a YIN f0 estimator with multi-period refinement`.

### Task 6: The seven goldens

**Files:**
- Create: `tests/golden/cases/{inst-pitchsync,inst-pitchsync-formant,inst-pitchsync-periods,inst-pitchsync-jitter,inst-pitchsync-slide,fx-pitchsync,fx-pitchsync-sweep}.json` and their expected files (written by `scripts/update-goldens.sh`; commit only these seven cases' files)
- Modify: `tests/metrics/click_injection_tests.cpp` (an entry, with its reason, for each case that hides steps)

**Interfaces:**
- Consumes: Tasks 1 to 4. gg-render's `"A Clock"`, `"A Mode"`, `"MPE"`, `"MPE Bend Range"` and the note `expressions` sub-project 1 added.

- [ ] **Step 1:** Write the seven specs as spec §4.10 describes, each 4 to 6 s at block 256, seed 1, 48 kHz, with layer A at `"A Mode": 1, "A Clock": 1`. Use A2 = key 45 (110 Hz), A3 = 57, C♯3 = 49 and E3 = 52, and `"Stereo": 0`. Every case but `inst-pitchsync-jitter` sets `"Distribution": 0`.
  - `inst-pitchsync`: `noise-2s.wav`, `"A Env 1 Release"` log2 0.3 s; A2 0.25–1.25 s, A3 1.5–2.5 s, the chord 3.0–4.5 s.
  - `inst-pitchsync-formant`: a sine sample (`"frequencyHz": 1000`, 2 s), `"Quality": 2`, Pitch −12, 0, +12, +24 stepped at 1 s intervals (automation at 1.0, 2.0, 3.0) under one A2 from 0.1 to 4.4 s.
  - `inst-pitchsync-periods`: noise, Size log2 of 0.005, 0.0375, 0.15, 0.6 and 2.0 at 0.9 s intervals under one A2.
  - `inst-pitchsync-jitter`: noise, Distribution 0, 0.25, 0.5 and 1 at 1 s intervals under one A2.
  - `inst-pitchsync-slide`: `"MPE": 1`, one A2 on channel 2 bent to +12 and back over 2 s in 50 ms steps (`pitchBend` values 0.5 to 0.625 at MPE Bend Range 48), then a second A2 with `id` 7 slid by `tuning` expressions 0 → +7 → 0 over 1.5 s in 50 ms steps.
  - `fx-pitchsync`: stereo noise input, Mix 1, layer A Live; A2, C♯3 and E3 in turn (1 s each), then the chord. Density automated to 1 at 0 s, 4 at 2 s and 7 at 4 s (refresh 2, 16 and 128 Hz).
  - `fx-pitchsync-sweep`: input a sweep (`"startHz": 100, "endHz": 2000`, 5 s), Mix 1, one held A2 from 0.2 to 4.8 s, Density 4.
  - Each case's event log must stay under about 1 MB (`wc -c`); shorten the case if it doesn't.
- [ ] **Step 2:** Run `scripts/update-goldens.sh build/dev`. Expected: the seven are new, and the changed list names nothing else. Then restore the 116 earlier cases' expected files: `git checkout -- $(git ls-files tests/golden/expected tests/golden/presets/expected)`.
- [ ] **Step 3:** Look at every new spectrogram (`build/dev/golden/<case>.png`). The pitch-sync cases show harmonic lines at multiples of f0. `inst-pitchsync-formant`'s energy peaks around 1 kHz × 2^(Pitch/12). `fx-pitchsync-sweep` shows harmonics brightening as the sweep passes them. Check `inst-pitchsync-formant`'s event log: its `lvl` values are 0, 0, 1 and 2 across the four steps (S8).
- [ ] **Step 4:** Run `ctest --preset dev -L golden`. Expected: PASS. For each case that catches fewer than 16 injected steps, add its exact count and the reason to the click-injection table (noise beds are expected in `inst-pitchsync`, `-periods`, `-jitter` and `fx-pitchsync`). Run the at-rest check. Expected: 108 unchanged, and the eight voice WAVs unchanged.
- [ ] **Step 5:** Commit `test(golden): seven pitch-sync cases; every earlier WAV hash unchanged`, listing the seven hashes and the click-table entries with their reasons. Stage only the seven cases' files and the table (`git diff --cached --stat -- tests/golden` lists only them).

### Task 7: The pitch, formant and aliasing tests

**Files:**
- Create: `tests/metrics/pitchsync_tests.cpp` (tags `[metrics][pitchsync]`, rendering cases in process as `comb_tests.cpp` does: `gg::render::render(loadCase(name, media), nullptr, optionsFor(media))`; add it to `tests/CMakeLists.txt` with the `golden_media` fixture)

**Interfaces:**
- Consumes: Task 5's `estimateF0` and `centsBetween`; Task 6's cases.

- [ ] **Step 1: Write the tests:**
  - `pitchsync renders the note's pitch to two cents`: over each steady span of `inst-pitchsync` (A2, A3 and the chord's root region, skipping 100 ms after each change), on 0.25 s windows hopped by 0.125 s. Each estimate is within ±2 cents of 110, 220 or 110 Hz (the chord's lowest note). The same over each Pitch step of `inst-pitchsync-formant` (always 110 Hz: Pitch moves the formant, never f0), and over `fx-pitchsync`'s spans at Density 1 and 4. The span at 7 is left out: refreshes at 128 Hz read new material each period (S4).
  - `pitchsync jitter at a quarter keeps the mean pitch to ten cents`: the median estimate over `inst-pitchsync-jitter`'s d = 0.25 span is within ±10 cents of 110 Hz.
  - `pitchsync formant follows Pitch`: in `inst-pitchsync-formant`, at each step, the harmonic h·110 Hz with the most energy (a 0.5 s Hann FFT, summed within ±5 Hz of each harmonic) is within one harmonic of round(1000 × 2^(Pitch/12) / 110).
  - `pitchsync adds no aliasing`: in-process specs, at HQ and at Normal, of a 1 kHz sine sample, Pitch 0, Distribution 0, Stereo 0, one held key 107 (3,951 Hz), 3 s. Over 1 to 3 s, a 65,536-point Hann FFT: the energy more than 20 Hz from every harmonic of f0, over the total, is under each tier's floor. Pin the floor from the measured value plus 6 dB, at most −60 dB, and write the measured value in a comment. If a tier measures above −60 dB, stop and report it: that is a spec finding, not a floor to raise.
- [ ] **Step 2:** Run `ctest --preset dev -L pitchsync`. Expected: PASS once the floors are pinned. A failing pitch check is a bug in Tasks 2 to 4, not in the tolerance (S14).
- [ ] **Step 3:** Commit `test(metrics): pitch-sync pitch to two cents, the formant, and no aliasing beyond the tiers'`, quoting the measured floors.

### Task 8: The sweep cases and the benchmark

**Files:**
- Create: `tests/sweep/sweep_pitchsync_tests.cpp` (tags `[sweep][pitchsync]`; add it to `tests/CMakeLists.txt`)
- Modify: `tools/gg-bench/Bench.cpp` (`EngineSetup::pitchSync`, a flag: the voices take `params` as given, at Clock Pitch, with no 1/voices split of the density), `tools/gg-bench/Bench.h` and `tools/gg-bench/main.cpp` (the case lists in their comments), `tests/tools/bench_tests.cpp` (the report-only list at :180, and the new case)

**Interfaces:**
- Consumes: Tasks 1 to 4; `SweepDriver` (`tests/sweep/SweepDriver.h`).
- Produces: the bench case `engine-pitchsync`: `engineSample`'s config and noise sample, 16 voices at Clock Pitch, `densityHz = 261.6255653005986 * 8.0` (C7), `grainDurationSec = 0.15` (N = 2), Normal, `refreshHz = 16`, at blocks 32, 64 and 256. It is report-only, with no baseline entry.

- [ ] **Step 1: Write the sweep cases.** Each checks what every sweep case checks (finite, the peak bound, no runaway, the click score outside exemptions) and pins a scored-share floor from its first run, with the reason in a comment.
  - `pitchsync: sixteen top voices over four HQ layers at Budget 256` (instrument): four layers at Voice and Clock Pitch, `Quality` 2, Size log2 2, Budget 256; 16 notes at keys 100 to 115 held for 3 s. It also requires `loweredGrains() + droppedGrains() > 0` summed over the layers (the ladder engages).
  - `pitchsync: a sixteen-voice chord slides four octaves and back` (instrument): MPE On, MPE Bend Range 48, 16 notes on member channels 2 to 16 (channel 2 carries two notes, which share its bend), each channel bent linearly to +48 and back over 2 s in 10 ms steps.
  - `pitchsync: Clock flips every 100 ms under a held chord` (FX, Live layer A at Voice): a four-note chord held for 3 s while `A Clock` toggles every 100 ms.
- [ ] **Step 2: Write the bench test** `engine-pitchsync runs and reports` (`[bench]`): a 0.002 s run, 2 rounds; one result per block size, each named `engine-pitchsync`, with a positive finite ratio. `admittedTiers("engine-pitchsync", 2000, 256)` admits only Normal.
- [ ] **Step 3:** Run `ctest --preset dev -L 'sweep|bench'`. Expected: the new cases FAIL until the bench case exists. Implement the bench case, then run again. Expected: PASS.
- [ ] **Step 4:** On an idle machine: `cmake --build --preset release --target gg-bench`, then `build/release/tools/gg-bench/gg-bench --cases reference,engine-sample,engine-voices,engine-pitchsync --commit $(git rev-parse --short HEAD) --json $SCRATCH/pitchsync-bench.json`. Expected: exit 0. `engine-sample`'s ratio is within its baseline's tolerance of the committed baseline for this machine's model, or within 5% of a run made at the merge base.
- [ ] **Step 5:** Commit `test(sweep): three pitch-sync cases (36 in all); bench: engine-pitchsync (report-only)`, quoting the three block sizes' ratios and ns per output sample (the per-grain-sample figure × 32).

### Task 9: Records

**Files:**
- Modify:
  - `INTENT.md`: §14 (Scheduler: Voice's Clock, Free or Pitch, with the line spec §5 gives; the levels: the read rate's at either clock, S8; the budget: about N grains a voice at any note, the pool stays 4,096 a layer, S10); §17 (Clock in the Scheduler panel); §18 (Clock is the sound's, carried by a preset, no patch-version bump); §23 a dated row (2026-11-07) for S1 to S14 marked as Claude's provisional calls, numbered at merge.
  - `CLAUDE.md`: the label `pitchsync`; the spec key `"A Clock"` to `"D Clock"` (0 Free, 1 Pitch) and the parameter totals (294); the seven goldens in the case list, and the golden count, which rises by seven (with the voice cases: 123 = 116 + 7); the sweep count 33 → 36; `engine-pitchsync` in the bench case list; "Where we are".
  - `docs/m3-carry-over.md`: the rows left to others. For sub-project 5: Size shown in periods at Clock Pitch (`sizePeriods`), the Clock control's look and hiding it at Mode Cloud, the canvas at high spawn rates (SB10), the pitch-sync presets. A later sub-project: Formant Track at layer 0x39 (S2). The director: S1 to S14 to review (S3 and S4 first). Whichever of sub-projects 3 and 4 merges second: unify `kC4Hz`.
- [ ] **Step 1:** Write them.
- [ ] **Step 2:** Commit `docs(m3): pitch-sync's records; INTENT §14, §17, §18, §23`.

### Task 10: The gate

- [ ] **Step 1:** Merge `m3-voice` again if it has moved. Rerun the merge check at the top of this plan and record any adjustment in the final report.
- [ ] **Step 2:** Run `scripts/check.sh --presets dev,asan,rtsan` (one gate on the machine at a time). Expected: `PASS`. Also build `gg-tests` in the CI-equivalent LLVM ASan tree (CLAUDE.md's `asan` row).
- [ ] **Step 3:** Run the at-rest check. Expected: `baseline cases: 108, changed: 0, gone: 0`, and the eight voice WAVs equal to sub-project 1's.
- [ ] **Step 4:** Run the validators on `gg-products`: `clap-validator validate` on both CLAPs and pluginval at strictness 10 on both VST3s (`scripts/fetch-validators.sh tools-bin` first). Expected: both pass.
- [ ] **Step 5:** Push the branch and open a draft PR against `m3-voice` (against `main` if it has merged). Fix any GCC 13 diagnostics CI's Linux leg reports.
- [ ] **Step 6:** Write the listening pack `docs/listening/m3-pitchsync/`: the seven goldens' WAVs and spectrograms, plus a README with what to listen for. The README covers the pitch holding as the formant moves, the periods ladder from buzz to resonance, the jitter from clean to rough, the slide, the FX's resonance of the input, and S3 and S4 as the calls to judge by ear.
