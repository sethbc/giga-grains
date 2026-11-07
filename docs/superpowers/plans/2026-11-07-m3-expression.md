# M3 Full Expression Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A voice hears everything a controller and a CLAP host can tell it: MPE zones announced by the MCM and RPN 0 (lower and upper, saved with the session), every CLAP note expression including channel-wide ones, a per-layer Pan, envelope and pan targets per voice, CLAP per-note modulation and `voice-info`. A layer's voices share its budget fairly under load. Nothing moves at rest.

**Architecture:** The core gains an RPN parser (`MpeZones`) and an `MpeLayout` that the note table and the bend read. The parser's writes join the group's pending base values and are queued for the plugin, which tells the host. The note table gains per-channel and per-note expression state and per-note modulation amounts. `Processor::updateVoices` reads them through one `VoiceInputs` struct. The engine's third rung picks its victim per E9 from per-slot weights and cursors in `GrainPool`. The plugin numbers its events in host order, converts the new MIDI and CLAP events, and answers `voice-info` and the per-note flags.

**Tech Stack:** C++23, CLAP 1.x (`voice-info`, `CLAP_PARAM_IS_MODULATABLE_PER_*`), clap-wrapper (`clap_plugin_as_vst3`), clap-helpers, Catch2, CMake presets, `gg-render` goldens, `gg-bench`.

**Spec:** `docs/superpowers/specs/2026-11-07-m3-expression-design.md` (E1 to E15, EB1 to EB13; parent `docs/superpowers/specs/2026-11-07-m3-program-design.md`). It builds on sub-project 1's spec and plan (`docs/superpowers/plans/2026-11-07-m3-voice.md`) as merged here at `bd4e022`: Task 7 (input paths, the `NOTE_END` queue, eviction `NOTE_END`) and Task 13 (V7's `1/sqrt(n)`) are both in.

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
- Branch hygiene: this branch merges `m3-voice` (sub-project 1) as it moves; parallel sub-projects 2, 3, 4 edit `core/include/gg/core/ParamIds.h` lists and `Processor::updateVoices`. Keep such edits small and self-contained to ease merges.
- **This sub-project's IDs:** globals `0x25` to `0x28` (reserved to `0x2F`), layer index `0x32` (reserved to `0x37`); `kNextFreeLayerIndex` 0x33, `kNextFreeGlobalIndex` 0x29. At a merge, keep the larger of each branch's values. 298 slots, `kLayerParamCount` 64. No patch-version bump, no new state chunk.
- **The voice goldens at rest:** "unchanged" means `build/dev/golden/wav-sha256.txt`'s 8 `*voice*` hashes equal those printed before the task, `ctest --preset dev -L golden -R voice` passes, and `git status --short tests/golden` lists no `*voice*` expected file. If the check rewrote them, `git checkout --` them.
- **Implementer models:** opus for Tasks 3, 6, 8 and 9; sonnet for the rest.
- **The tie order (E8)** is keyed by (offset, seq, rank), the ranks being parameter 0, note 1, per-note parameter modulation 2, control 3; commands stay last at a tie. `gg-render` and the tests leave `seq` at 0.

## Review Focus

1. **An MCM followed at once by RPN 0 on its channels, in one block** (a LinnStrument at power-up or on a mode change). The ranges should land on the settings of the zones the MCM just made, not on the zones before it (plan decision P1). Task 6's `mcm then rpn 0 in one group set the new zones ranges`.
2. **A 0-frame `process()` call (or the overflow path `applyBetweenBlocks`) that carries an MCM or ends a note.** The host should still be told at frame 0, `values_` should follow, and nothing should wait until the next block. Task 11's `plugin a zero frame call reports written settings and note ends at frame 0`.
3. **A restored session, or a host's parameter values, whose zones overlap** (Lower 10, Upper 10). The layout resolves lower first (Upper acts as 4) and the stored values stay as loaded. An edit in the editor writes both counts, so the UI never shows an overlap. Task 5's `overlapping zones resolve lower first` and Task 11's `plugin a zone edit writes both counts`.
4. **A held, dense chord under a spent budget with no weak voice and no orphan.** E10 should finish no voice. E9 should take the heaviest held voice's oldest grains past half their windows, never a grain under half, and the output should never click. Task 10's `budget steal with no weak voice finishes nothing`.
5. **A per-note modulation aimed at a Cloud layer, at an unknown note ID or at a parameter without the per-note flags, with a Voice layer beside it.** It should reach no voice, and it should never be applied as monophonic. Task 8's `per note modulation on a Cloud layer is ignored and never monophonic`.

## Plan decisions (Claude's, made without the director; each with its reason)

- **P1. The RPN parser reads the group's own view of the settings.** RPN 0's role and an MCM's shrink use the MIDI settings as the group's pending values leave them (`pendingBase_` where set, else `base_`). The bend, the pedal and the copying of pressure, CC 74 and CC 11 still use the layout in effect (EB3). *Why:* controllers send the MCM and then RPN 0 back to back. EB3's literal reading would route the RPN 0 on channel 16, sent right after an upper-zone MCM, to the lower member range.
- **P2. A voice's envelope parameters are computed when it starts and when it releases** (in `commitVoices`, with the function `updateVoices` uses) and again at every `updateVoices`. `stepVoices` and the voice's `AmpEnvelope` use the voice's cached copy. *Why:* a boundary runs commit, then step, then update. EB8's "first grains use the velocity-shaped attack", and an exact Release Velocity → Env 1 Release from the release's first cell, both need the values before the step.
- **P3. `VoiceInputs`.** Task 3 moves everything a voice's parameters depend on (its sources, `pitchIn`, volume, pan, its per-note amounts) into one struct, built by `Processor::voiceInputs`. Tasks 7 and 8 then fill fields of that struct instead of editing `updateVoices`' body. *Why:* merge risk with sub-projects 3 and 4.
- **P4. Per-note amounts are stored on the note, whatever its layers' modes.** Only voices read them, and a Cloud layer never has voices, so E7's "ignored" holds. A layer switched to Voice starts no voice for a note already held (sub-project 1).
- **P5. Absolute times.** `EndedNote` and `SettingChange` carry `int64_t at`, the boundary in samples (for a reset, the clock at the reset). The plugin reads `Processor::sampleClock()` before each call and stamps `clamp(at − start, 0, max(frames, 1) − 1)`.
- **P6. An `RpnCc` control's value is the raw data byte** (0 to 127, exact), exempt from the 0-to-1 clamp.
- **P7. The editor's zone edit.** `uiSetValue` on MPE Lower Zone or MPE Upper Zone also queues the other zone's count, as `otherZoneAfter` (Task 6) gives it, in the same gesture. It doesn't reset ranges or turn MPE on: only an MCM does that.
- **P8. E10's test uses the quotas as the last boundary left the layers' On.** A Layer On change in the same group may give the piece a different quota. That is accepted and recorded in the code comment.
- **P9. V7 and E9 meet only through the voice count.** E9's "heaviest" uses ledger weights (tier × channels), which V7's gain factor doesn't touch. A budget steal (E10) finishes a voice before the new one starts, so the boundary's `n` counts the new voice and not the finished one.

## Fit with the merged code (adjustments the spec didn't name)

- `EndedNote` has no boundary, and `sendEndedNotes` stamps every `NOTE_END` at the call's last frame. Task 11 adds `at` and replaces the stamping with P5's clamp. Task 7's fixed queue (`ended_`, `kMaxEndedNotes`) stays as it is.
- `NoteTable` reads MPE as a `bool` (`applyControl(c, mpe)`, `pedalDown(ch, mpe)`, `setMpe`), and `Processor::updateEffective` derives `mpe_`. Task 5 replaces all three with `setLayout(MpeLayout)`.
- `GrainPool` doesn't store a grain's voice, and `GrainStealSink` has no voice argument. Task 9 adds both.
- `Processor.cpp`'s private `kVoiceTargets` (12 rows) and the voice key's separate Bus Split row become ModMatrix.h's `kVoiceLayerTargets` (22 rows) in Task 3.
- The quota split lives inline in `Processor::render`. Task 10 moves it into `layerQuotaAt` for E10 to share.
- Every voice golden's event log holds no `"steal"` line (checked at `bd4e022`), so E9 and E10 can't move them. Task 9 re-checks this after its change.

---

### Task 1: The new parameters

**Files:**
- Modify: `core/include/gg/core/ParamIds.h`: add `GG_LAYER_PARAMS_M3_EXPRESSION(X) X(Pan, 0x32, "Pan")` after `GG_LAYER_PARAMS_M3` in both `GG_LAYER_PARAMS` and `GG_LAYER_PARAMS_SINCE_M1`; append `MpeLowerZone`, `MpeUpperZone`, `MpeUpperBendRange` and `UpperBendRange` after `BendRange`; set `kLayerParamCount` 64 and the next-free indices; add `kMidiSettingSlots` and derive `isMidiSetting` from it.
- Modify: `core/src/ParamIds.cpp` (metadata), `core/src/Mutate.cpp` (`buildStructuralLocks`: each layer's Pan), `ui/src/EditLayout.cpp` (placements).
- Test: `tests/support/ParamTable.h`, the count assertions the build turns up (`patch_tests`, `identity_tests`, `params_tests`, `rt_tests`, `edit_layout_tests`, `sweep_params_tests`), `tests/core/preset_tests.cpp`, `tests/core/mutate_tests.cpp`, `tests/core/mod_matrix_tests.cpp`, `tests/tools/` (the pack refusal).

**Interfaces:**
- Produces:
  - `LayerParam::Pan`: −1 to 1, default 0, shown −100% to +100% (`"%"`, like Stereo's display ×100), automatable and modulatable.
  - Four globals, stepped integers, neither automatable nor modulatable (as `Mpe`):
    - `ParamSlot::MpeLowerZone`: 0x25, "MPE Lower Zone", 0 to 15, default 15;
    - `MpeUpperZone`: 0x26, "MPE Upper Zone", 0 to 15, default 0;
    - `MpeUpperBendRange`: 0x27, "MPE Upper Bend Range", 1 to 96, default 48;
    - `UpperBendRange`: 0x28, "Upper Bend Range", 0 to 24, default 2.
  - `inline constexpr std::array<ParamSlot, 7> kMidiSettingSlots{Mpe, MpeBendRange, BendRange, MpeLowerZone, MpeUpperZone, MpeUpperBendRange, UpperBendRange};`.
  - `kParamCount` 298. The gg-render names `"A Pan"` … `"D Pan"` and the four display names come from the macros.

- [ ] **Step 1:** Update `ParamTable.h` with the four Pan rows (`0x00010032` … `0x00040032`) and the four globals as above. Add the tests:
  - `preset load keeps the MPE zones and upper ranges`: an instance at Lower 7, Upper 7, MPE Upper Bend Range 12 and Upper Bend Range 5 loads a factory preset and keeps all four.
  - `no route targets the zone settings`: `isValidTarget` is false for the four, and true for `ParamSlot::Pan`.
  - `a default mutate press leaves Pan`: `defaultMutateLocks` has every layer's Pan.
  - `pack preset refuses MPE Lower Zone`: exit 2, naming it.
- [ ] **Step 2:** Run `ctest --preset dev -L 'core|plugin|ui|render' -LE slow`. Expected: the updated cases FAIL.
- [ ] **Step 3:** Add the rows, metadata and locks. Place each layer's Pan in `Panel::Space`, beside Stereo. Place the four globals in `Panel::Midi`, rows 3 to 6 after Bend Range. Give a short label only if the elision check (`editor_labels_dejavu`) needs one; the host keeps the full names.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS, the frame tests included ("every parameter is reachable from the UI", the elision checks). Adjust a sweep floor only with its reason in a comment. Run the at-rest check. Expected: `baseline cases: 108, changed: 0, gone: 0`, and the voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(params): Pan per layer (0x32); MPE Lower and Upper Zone, the upper bend ranges (0x25-0x28)`.

### Task 2: Pan's sound (EB9)

**Files:**
- Modify: `engine/include/gg/grain/CloudScheduler.h` (`CloudParams::pan`), `engine/src/CloudScheduler.cpp` (sanitize; the draw's pan at `:411`), `core/src/ParamMap.cpp` (`toCloudParams` reads `layerSlot(LayerParam::Pan, L)`)
- Test: `tests/engine/cloud_tests.cpp`, `tests/core/layers_tests.cpp`

**Interfaces:**
- Produces: `double CloudParams::pan{0.0}` (−1 to 1; the sanitizer applies `finiteOr` then a clamp). A draw's pan is `p = stereo × bipolar(u)` as today. When `pan` is not ±0, `p = clamp(pan + p, −1, 1)`. The flush-to-+0 of a subnormal stays last.

- [ ] **Step 1:** Tests:
  - `pan 0 leaves every draw as it was`: 2,000 draws at Stereo 0.7, with pan +0 and with −0, have pans bit-equal (`std::bit_cast<uint32_t>`) to `float(0.7 × rng::bipolar(key(Field::Pan)))`, recomputed in the test.
  - `pan adds to the stereo draw and clamps`: at pan 0.75 and Stereo 0.5, each draw's pan equals `float(std::clamp(0.75 + 0.5 × bipolar(u), −1.0, 1.0))`.
  - `pan alone places every grain`: at Stereo 0 and pan −0.4, every pan is `-0.4f`.
  - `A Pan reaches layer A's grains only`: A Pan 0.5, B on at Pan 0. Every layer 0 event has `pan == 0.5f` and every layer 1 event has `pan == 0.0f`.
- [ ] **Step 2:** Run `ctest --preset dev -L 'cloud|layers'`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run `ctest --preset dev -L 'engine|cloud|layers|determinism'`. Expected: PASS. Run the at-rest check. Expected: 108/0/0 and the voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(engine): CloudParams::pan, the layer's Pan added to each grain's stereo draw`.

### Task 3: The voice targets: Pan, Bus Split and the envelope controls per voice (E6, EB8, P2, P3)

**Files:**
- Modify: `core/include/gg/core/ModMatrix.h` (`kVoiceLayerTargets`, `kPerNoteSlots`, `isVoiceTarget`, `isVoiceOnlyTarget`, `isValidTarget`, `actsPerVoice`), `core/src/ModMatrix.cpp` (`applyRoutes` skips the envelope targets on every layer), `core/include/gg/core/Processor.h` and `core/src/Processor.cpp` (`VoiceInputs`, `voiceInputs`, `updateVoices`, `commitVoices`, `stepVoices`; the local `kVoiceTargets` and `kVoiceKeySlots` go), `core/include/gg/core/VoiceAllocator.h` (`Voice::env` params cache)
- Test: `tests/core/mod_matrix_tests.cpp`, `tests/core/voice_processor_tests.cpp`, `tests/core/patch_tests.cpp`

**Interfaces:**
- Produces:
  - `inline constexpr std::array<LayerParam, 22> kVoiceLayerTargets{Density, Size, Position, Pitch, Spray, Stereo, Distribution, Texture, Smear, Quality, PitchSpread, SizeSpread, Pan, BusSplit, Env1Attack, Env1Decay, Env1Sustain, Env1Release, Env2Attack, Env2Decay, Env2Sustain, Env2Release};`
  - `inline constexpr size_t kPerNoteSlots = 23;` (the 22 rows, then index 22, the layer's Level). Task 8's per-note storage indexes by this order.
  - `isVoiceTarget(t)`: Level, or layer A's slot of a row of `kVoiceLayerTargets`.
  - `constexpr bool isVoiceOnlyTarget(ParamSlot t) noexcept`: layer A's slot of the eight envelope controls.
  - `isValidTarget` admits those eight; every other modulator control is still refused.
  - `actsPerVoice(r, L)` also requires, for a voice-only target, `r.source != Env1 && r.source != Env2`.
  - `applyRoutes` skips every route to a voice-only target, on every layer.
  - `struct VoiceInputs { ModSourceValues sources; double pitchIn; float volume; float pan; std::array<float, kPerNoteSlots> perNote; };` (Processor.h, private).
  - `VoiceInputs Processor::voiceInputs(uint8_t L, const VoiceAllocator::Voice &, const NoteTable::Note &) const noexcept`: today's sources, `pitchIn` and volume; `pan` 0 and `perNote` all 0 until Tasks 7 and 8.
  - `VoiceAllocator::Voice` gains `AdsrParams envParams[2]`.

- [ ] **Step 1:** Tests:
  - `envelope controls are voice only targets`: `isValidTarget` and `isVoiceOnlyTarget` are true for the eight; `isValidTarget(layerSlot(Lfo1Rate, 0))` stays false. `applyRoutes` with Velocity → Env 1 Attack leaves the offsets all +0 on every layer, Voice or not.
  - `an envelope target route round-trips through the patch`.
  - `velocity to Env 1 Attack equals the layer's attack moved`: one Voice note at velocity 1 with Velocity → Env 1 Attack (Unipolar, depth d) gives event lines equal to a render with no route and Env 1 Attack's base raised by d.
  - `release velocity to Env 1 Release equals the layer's release moved`: the same equality from the release on, release velocity 1.
  - `Env 1 to Env 1 Release does nothing`: events equal to a render with no route.
  - `an envelope target never moves a Cloud layer`: a Cloud layer with Velocity → Env 1 Attack has `effective(Env1Attack) == base` and events equal to no route.
  - `Key to Pan pans each voice`: Stereo 0 and Key → Pan (Bipolar, depth 1), notes 36 and 96. Each voice's grains have `pan == float(clamp(bipolarCurve(key / 127)))`, computed in the test with the matrix's curve function.
  - `Bus Split per voice`: Velocity → Bus Split (Unipolar, depth 1). At velocity 0 every grain has bus 0; at velocity 1 every grain has bus 1.
  - `the voice cache recomputes once per change`: Task 6 of sub-project 1's count holds with the larger key.
- [ ] **Step 2:** Run `ctest --preset dev -L 'mod|voice'`. Expected: FAIL.
- [ ] **Step 3: Implement.**
  - `updateVoices` keys each voice on `offsets` (its 23 route offsets), `pitchIn`, `volume`, `pan` and `perNote`.
  - A voice's slot sum is `base + mod + layerOffset + routeOffset + perNote` for each of the 22 rows, re-clamped only where the extra terms aren't both 0.
  - Its `envParams` come from `envParams(voiceScratch_, L, k)` while its slots are set. Pan and Bus Split reach `toCloudParams` through `voiceScratch_`.
  - Factor one voice's computation into `void Processor::computeVoice(uint8_t L, size_t slot) noexcept`. `commitVoices` calls it for a voice that starts or releases in the group (P2); `updateVoices` calls it for every spawning voice.
  - `stepVoices` and `params.amp` use `v.envParams`.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS. Run the at-rest check. Expected: 108/0/0 and the voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(core): Pan, Bus Split and the envelope controls as voice targets; VoiceInputs`.

### Task 4: Events at one sample keep the host's order (E8; row 417)

**Files:**
- Modify: `core/include/gg/core/NoteEvents.h` (`NoteEvent::seq`, `ControlEvent::seq`), `core/include/gg/core/Processor.h` (`ParamEvent::seq`), `core/src/Processor.cpp` (the merge at `:2215`), `plugin/src/GigaGrainsPlugin.cpp` (numbering in `process` and `applyBetweenBlocks`)
- Test: `tests/core/note_tests.cpp`, `tests/core/voice_processor_tests.cpp`, `tests/plugin/voice_plugin_tests.cpp`

**Interfaces:**
- Produces:
  - `uint32_t seq{0}` as the last field of all three event structs.
  - The merge takes the smallest `(offset, seq, rank)`, where rank is `eventRank(const ParamEvent &)` (0, and 2 from Task 8 for a per-note modulation), 1 for a note and 3 for a control. A command goes first only when its offset is strictly smaller, as today.
  - The plugin sets `seq = host index + 1` for every event it converts (in both loops). The editor's changes keep 0.

- [ ] **Step 1:** Tests:
  - `a pedal down numbered before a note off at one sample sustains the note`: CC 64 down with seq 1 and the note-off with seq 2, both at offset 5. The note becomes PedalHeld, and the Cloud held set keeps its key.
  - `with sequence numbers 0 a tie keeps the kinds order`: the same two events with seq 0 release the note, as before.
  - `plugin host order decides a same sample pedal and note off`: MIDI 0xB0 64 127, then 0x80, at time 10 in one block. The voice keeps spawning after the block.
- [ ] **Step 2:** Run `ctest --preset dev -L 'notes|voice'`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS. Run the at-rest check. Expected: 108/0/0 and the voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(core,plugin): events at one sample keep the host's order (row 417)`.

### Task 5: MPE zones: the layout, the per-zone bend and pedal (E2's layout, E3, EB3, EB11)

**Files:**
- Create: `core/include/gg/core/MpeZones.h` (the layout part), `core/src/MpeZones.cpp`, `tests/core/mpe_tests.cpp` (`[core][mpe]`; add to `tests/CMakeLists.txt`, `core/CMakeLists.txt`)
- Modify: `core/include/gg/core/NoteTable.h`, `core/src/NoteTable.cpp` (`setLayout` replaces `setMpe`; `applyControl(const ControlEvent &)`; `pedalDown(int ch)`; `channelValuesCopied` false for either master), `core/src/Processor.cpp` (`layout_` in place of `mpe_`; `bendSemitones`)

**Interfaces:**
- Produces:
  - `enum class ZoneRole : uint8_t { Conventional, LowerMaster, LowerMember, UpperMaster, UpperMember };`
  - `struct MpeLayout { bool on{false}; uint8_t lower{15}; uint8_t upper{0}; ZoneRole roleOf(int channel) const noexcept; };`. The lower zone has master 0 and members 1 to `lower`. The upper zone has master 15 and members `15 − upper` to 14. With `on` false, or both counts 0, every channel is Conventional.
  - `MpeLayout layoutFrom(const ParamValues &effective) noexcept`, lower first: `upper' = lower == 0 ? U : lower == 15 ? 0 : min(U, 14 − lower)`.
  - `NoteTable::setLayout(const MpeLayout &)` and `const MpeLayout &layout() const`.
  - `bendSemitones(n)`:
    - a member: `exactBend(own) × its zone's member range + exactBend(master's) × its master's range`;
    - a lower master or a conventional channel: `exactBend(own) × Bend Range`;
    - the upper master: `exactBend(own) × Upper Bend Range`.

- [ ] **Step 1:** Tests in `mpe_tests.cpp`:
  - `zoneOf maps channels by layout`: a table of (on, L, U, channel) → role covering both zones, MPE Off, L 0 with U 0, L 15, and L 7 with U 7.
  - `overlapping zones resolve lower first`: (10, 10) gives an upper of 4, (15, 3) gives 0, and (0, 9) gives 9. The base values stay as set.
  - `the default layout equals sub-project 1s mapping`: at MPE On, L 15 and U 0, for every channel 0 to 15 and 50 bends drawn with `gg::rng`, `bendSemitones` equals sub-project 1's formula (copied into the test).
  - `upper zone bends use the upper ranges exactly`: at L 7 and U 7, a note on channel 14 with 14-bit bend 16383 has `bendSemitones == 48.0`. Adding master channel 15's full bend makes it `48.0 + 2.0`.
  - `each master's pedal holds its own zone`: CC 64 on channel 15 holds the notes on channels 8 to 14, not on 1 to 7. Channel 0's holds 1 to 7.
  - `a masters pressure and CC 74 are not copied into notes`: for both masters.
  - `a layout change moves a sounding notes bend at its boundary`: Upper Zone set from 0 to 7 by a parameter event at offset 100. A note on channel 14 is a lower member before the boundary (×48 from MPE Bend Range) and an upper member after it (×48 from MPE Upper Bend Range, set to 24 here, so ×24).
- [ ] **Step 2:** Run `ctest --preset dev -L mpe`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS, every `notes` and `voice` test unchanged. Run the at-rest check. Expected: 108/0/0 and the voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(core): MPE zones, lower and upper, with each master acting on its own zone`.

### Task 6: The RPN parser, the MCM and the write-through queue (E1, E2, EB1, EB2, P1, P6)

**Files:**
- Create: `core/include/gg/core/MidiControl.h`, `core/src/MidiControl.cpp`
- Modify: `core/include/gg/core/MpeZones.h`, `core/src/MpeZones.cpp` (the parser), `core/include/gg/core/NoteEvents.h` (`ControlKind` appends `Expression`, `RpnCc`), `core/include/gg/core/Processor.h` and `core/src/Processor.cpp` (validation, the merge, `commitPending`, `reset`, the queue)
- Test: `tests/core/mpe_tests.cpp`

**Interfaces:**
- Produces:
  - `bool controlFromMidiCc(uint32_t offset, int16_t channel, uint8_t controller, uint8_t value, ControlEvent &out) noexcept`:
    - 1 → ModWheel, 64 → Sustain, 74 → Timbre and 11 → Expression, each `value / 127`;
    - 6, 38, 98, 99, 100 and 101 → `RpnCc` with `key = controller` and `value = float(value)` (P6);
    - anything else returns false (96 and 97 included).
  - `ControlKind::Expression` is applied by nothing until Task 7.
  - `struct MidiSettings { bool mpe; uint8_t lower, upper, mpeBendRange, bendRange, mpeUpperBendRange, upperBendRange; };` and `MidiSettings midiSettingsOf(const ParamValues &) noexcept`.
  - `uint8_t otherZoneAfter(uint8_t count, uint8_t other) noexcept`: 0 when count is 15, `min(other, 14 − count)` when count > 0, otherwise `other`. Task 11's editor edit uses it too.
  - `class MpeZones { public: struct Write { ParamSlot slot; float value; }; struct Result { std::array<Write, 5> writes; uint8_t count; }; Result onRpnCc(int channel, uint8_t controller, uint8_t value, const MidiSettings &current, const MpeLayout &pendingLayout) noexcept; void reset() noexcept; };`. It keeps the selection (MSB, LSB, or none) per channel:
    - 101 sets the MSB and 100 the LSB;
    - 99 or 98, or a selection of 127/127, deselects;
    - CC 6 with (0, 6) selected on channel 0 or 15 is an MCM; with (0, 0) selected it is RPN 0;
    - CC 38 does nothing.
  - An MCM writes MPE On (only when its count is > 0), its zone's count, the other zone's `otherZoneAfter`, its member range 48 and its master range 2.
  - RPN 0 writes one slot, the range of the channel's role in `pendingLayout` (P1), clamped to the slot's range.
  - The Processor applies the writes into `pendingBase_` in merge order. `commitPending` pushes `struct SettingChange { ParamSlot slot; float value; int64_t at; };` for each of `kMidiSettingSlots` that MIDI wrote in the group, in slot order, with the committed base value and the boundary. It uses a fixed queue: `static constexpr size_t kMaxSettingChanges = 32;`, `size_t takeSettingChanges(std::span<SettingChange> out) noexcept`, `uint64_t settingChangesDropped() const noexcept`. `reset()` clears the selection.

- [ ] **Step 1:** Read RP-053's MCM and RPN text. Record any difference from E2 (the resets, a count of 0, the shrink) as a ruling in the task report before coding it. Then write the tests:
  - `controlFromMidiCc maps each controller`: one table row per controller listed above, plus 96, 97 and 7 refused.
  - `an MCM on channel 1 sets the lower zone and resets its ranges`. Start from MPE Off, MPE Bend Range 24 and Bend Range 12, then send CC 101 0, CC 100 6 and CC 6 7 on channel 0. After the boundary, MPE is 1, MPE Lower Zone 7, MPE Bend Range 48 and Bend Range 2, and MPE Upper Zone is still 0.
  - `an MCM on channel 16 sets the upper zone and shrinks the lower`: L 15 then an upper MCM of 5 gives U 5, L 9, MPE Upper Bend Range 48 and Upper Bend Range 2.
  - `an MCM of 15 empties the other zone`. `an MCM of 0 leaves MPE as it is`: at MPE Off, a lower MCM of 0 leaves MPE 0 and sets L 0.
  - `an MCM on channel 2 is ignored`: no write, an empty queue.
  - `RPN 0 sets the range of the channels role`: a table covering a lower member, channel 0, an upper member, channel 15 and MPE Off. A value of 120 clamps to 96 on MPE Bend Range and to 24 on Bend Range; 0 clamps to 1 on MPE Bend Range.
  - `CC 38 is accepted and ignored`. `an NRPN or the null RPN deselects`: CC 99 then CC 6 writes nothing; 101 127 and 100 127 then CC 6 write nothing.
  - `mcm then rpn 0 in one group set the new zones ranges`: an upper MCM of 7 on channel 15, then RPN 0 = 12 on channel 15, at one offset, give Upper Bend Range 12 and leave Bend Range alone.
  - `written settings are queued at their boundary`: the queue holds the five writes of one MCM in slot order, `at` the boundary (`gridCeil`).
  - `the setting queue drops past 32 and counts`.
  - `a reset clears the RPN selection`.
  - `rpn messages are block size invariant`: at blocks 1, 32, 64 and 4096, the values and the queue entries are equal.
- [ ] **Step 2:** Run `ctest --preset dev -L mpe`. Expected: FAIL.
- [ ] **Step 3:** Implement. `validControl` admits `RpnCc` only with key in {6, 38, 98, 99, 100, 101} and a value of 0 to 127. Every new array is fixed (16 selections; a queue of 32).
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS. Run the at-rest check. Expected: 108/0/0 and the voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(core): the MPE Configuration Message and RPN 0 write the MIDI settings, queued for the host`.

### Task 7: Expressions: CC 11, pan, vibrato, expression, channel-wide expressions (E3, E4, E5, EB10)

**Files:**
- Modify: `core/include/gg/core/NoteEvents.h` (`ControlKind` appends `NotePan`, `NoteVibrato`, `NoteExpression`), `core/include/gg/core/NoteTable.h`, `core/src/NoteTable.cpp`, `core/include/gg/core/ModMatrix.h` and `core/src/ModMatrix.cpp` (`ModSource` appends `Vibrato`, "Vibrato", and `Expression`, "Expression", both in `isPerVoiceSource`), `core/src/Processor.cpp` (`voiceInputs`, `updateNoteSources`, `sourceValue`, a channel-wide PRESSURE into `pendingMidi_.aftertouch`)
- Test: `tests/core/note_table_tests.cpp`, `tests/core/voice_processor_tests.cpp`, `tests/core/patch_tests.cpp`

**Interfaces:**
- Consumes: Task 3's `VoiceInputs`, Task 5's layout and Task 6's `ControlKind::Expression`.
- Produces:
  - `NoteTable::Note` gains `float pan{0}, vibrato{0}, expression{0}; bool hadVibrato{false}, hadExpression{false};`.
  - Per-channel state for `channelTuning`, `channelVolume` (1), `channelPan`, `channelVibrato` and `channelExpression(int ch)`.
  - `NotePan`'s value is 0 to 1 and becomes `(v − 0.5) × 2`.
  - A Note kind with `noteId == −1` and `key == −1` is channel-wide (every channel for −1), per E5.
  - `voiceInputs` gives:
    - `pan = note.pan + channelPan(ch)`, added to the voice's CloudParams pan after `toCloudParams` with a clamp to ±1 (the add is skipped at ±0);
    - `volume = clamp(note.volume × channelVolume(ch), 0, 4)`;
    - `pitchIn = (key − 60) + bend + note.tuning + channelTuning(ch)` (EB11);
    - the sources Vibrato and Expression from the note's values.

- [ ] **Step 1:** Tests:
  - `note_table_cc11_sets_expression_per_channel`: a member's note follows CC 11; the lower master's CC 11 isn't copied.
  - `note_table_channel_wide_tuning_adds_to_each_note`: a later note on the channel inherits it.
  - `note_table_channel_wide_volume_multiplies_and_clamps`: own 3 × channel 2 gives a voice volume of 4.
  - `note_table_channel_wide_vibrato_follows_until_own`.
  - `note_table_channel_wide_on_every_channel`: channel −1.
  - `a channel wide pressure moves Aftertouch and Pressure`.
  - `a channel wide tuning bends voices but not the Pitch Bend source`: the rates are `exp2((key − 60 + 2) / 12)`, and `source(PitchBend)` is unchanged.
  - `CLAP PAN pans a voice`: at A Pan 0.5 and expression value 1.0, every grain of the voice has `pan == 1.0f`.
  - `Expression reaches a route per voice`: Expression → Size (Unipolar, depth d). CC 11 at 127 on one member channel and 0 on another give that voice events equal to a single-note render with Size's base raised by d, and the other's unmoved.
  - `Vibrato and Expression read the newest held note instance wide`, 0 with none held.
  - `the Vibrato and Expression sources save by name`.
- [ ] **Step 2:** Run `ctest --preset dev -L 'notes|voice|mod'`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS. Run the at-rest check. Expected: 108/0/0 and the voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(core): CC 11, note pan, vibrato and expression, channel-wide expressions (row 407)`.

### Task 8: Per-note parameter modulation in the core (E7, EB5, P4)

**Files:**
- Modify: `core/include/gg/core/Processor.h` (`ParamEvent` fields), `core/include/gg/core/ModMatrix.h` (`PerNoteRef`, `perNoteRefOf`), `core/include/gg/core/NoteTable.h` and `core/src/NoteTable.cpp` (storage, `applyPerNoteMod`), `core/src/Processor.cpp` (`eventRank`, the merge, `voiceInputs`' `perNote`)
- Test: `tests/core/voice_processor_tests.cpp` (with an `[rt]` case), `tests/core/note_table_tests.cpp`

**Interfaces:**
- Consumes: Task 3's `kVoiceLayerTargets` and `kPerNoteSlots`, Task 4's ranks.
- Produces:
  - `ParamEvent` gains `int32_t noteId{-1}; int16_t channel{-1}; int16_t key{-1};`. It is per-note when `isModulation` is set and any of the three is ≥ 0. A value event with them is applied as today.
  - `struct PerNoteRef { uint8_t layer; uint8_t index; bool valid; };` and `PerNoteRef perNoteRefOf(ParamSlot) noexcept`: a layer slot of a `kVoiceLayerTargets` row gives its index; Level and Level B to D give index 22; anything else is invalid.
  - `Note::perNote`: `std::array<float, kLayers * kPerNoteSlots>`, zeroed when an On takes the slot.
  - `void NoteTable::applyPerNoteMod(int32_t noteId, int16_t channel, int16_t key, PerNoteRef, float amount) noexcept`. An ID ≥ 0 reaches that non-Free note. Otherwise it reaches every non-Free note that matches, −1 matching any. The latest amount replaces the last.
  - The merge applies a per-note event at its rank-2 turn (never into `pendingMod_`), so it joins its group. An invalid ref or non-finite amount is ignored.
  - `voiceInputs` copies the voice's layer's 23 amounts. Level's is in dB, added to the voice's Level offset; Pitch's is in semitones, added to the voice's Pitch offset.

- [ ] **Step 1:** Tests:
  - `per note modulation by note id moves one voice`: A Size +0.2 to note ID 7. Its events equal a single-note render with Size's base +0.2; note 8's equal no modulation.
  - `per note modulation by key and by channel reaches matching notes`.
  - `per note modulation of an unknown note id is ignored`.
  - `per note modulation on a Cloud layer is ignored and never monophonic`: A Voice, B Cloud. A per-note B Size leaves `effective(B_Size)` at its base and B's events equal no modulation; a per-note A Size on the same event list still moves A's voice.
  - `per note modulation of a parameter without the flags is ignored`: Reverb Amount.
  - `per note modulation sent at the note on sample finds the note`: seq 0, the same offset as the On.
  - `a later per note modulation replaces the earlier`.
  - `per note modulation ends with its note`: a new note reusing ID 7 later starts at 0.
  - `per note modulation is block size invariant`: blocks 1, 32, 64 and 4096.
  - `per note modulation recomputes a voice once`: `voiceParamRecomputes` +1.
  - The `[rt]` tripwire over a process with per-note events.
- [ ] **Step 2:** Run `ctest --preset dev -L 'voice|notes'`. Expected: FAIL.
- [ ] **Step 3:** Implement. Compacting the storage is allowed only if every test above passes unchanged (EB5).
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS. Run the at-rest check. Expected: 108/0/0 and the voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(core): CLAP per-note parameter modulation reaches one voice (P5)`.

### Task 9: The steal order in Voice mode, and the steal line's `"v"` (E9, EB6, EB7, E14 part 2, row 412)

**Files:**
- Modify: `engine/include/gg/grain/GrainPool.h`, `engine/src/GrainPool.cpp` (rows `voice` and `voiceSlot`; per-slot weights and cursors), `engine/include/gg/engine/Engine.h` and `engine/src/Engine.cpp` (`VoiceParams::weak`; rung 3 in voice mode; `GrainStealSink`), `core/src/Processor.cpp` (`onSteal`'s forwarding; `params.weak = v.weak`), `tools/gg-render/Render.h` and `Render.cpp` (`formatStealLine`)
- Test: `tests/engine/voice_tests.cpp`, `tests/engine/determinism_tests.cpp`, `tests/engine/budget_tests.cpp` (the sink's new signature), `tests/tools/` (the steal line)

**Interfaces:**
- Produces:
  - In GrainPool.h, `inline constexpr uint8_t kStreamSlots = 17;` and `kCloudStreamSlot = 16`. Engine.h adds `static_assert(kMaxVoices + 1 == kStreamSlots)`.
  - `GrainSpec::voiceSlot` (`uint8_t`, default `kCloudStreamSlot`). `GrainRows` gains `const uint32_t *voice` and `const uint8_t *voiceSlot`.
  - `uint64_t GrainPool::slotWeight(uint8_t slot) const noexcept`: the ledger weight of the slot's grains not yet forgotten, fading ones included.
  - `bool GrainPool::oldestSoundingOf(uint8_t slot, int64_t onset, uint32_t &row) noexcept`: as `oldestSounding`, with a per-slot cursor restarted by `retireFinished`.
  - `bool VoiceParams::weak{false}`, from the allocator's flag at every boundary.
  - `using GrainStealSink = void (*)(void *ctx, uint32_t layer, uint64_t index, uint32_t voice, int64_t onset);` (voice is the stolen grain's ordinal or `kNoVoice`).
  - `std::string formatStealLine(uint32_t layer, uint64_t index, uint32_t voice, int64_t onset)` writes `{"i":k,"layer":L,"v":o,"steal":onset}` for a voice grain and today's bytes for `kNoVoice`.
- Rung 3 in voice mode tries the candidates in E9's order. Each is tested with D4 (half its window), then today's room test, and the first that passes is stolen:
  1. each orphan slot's oldest grain (slot 16, and every slot whose voice isn't spawning), oldest row first;
  2. each weak spawning voice's oldest grain, heaviest `slotWeight` first, a tie to the lower ordinal;
  3. each held voice's oldest grain, the same way.

  If none passes, the draw drops. Cloud mode keeps `oldestSounding`.

- [ ] **Step 1:** Tests:
  - `per slot weights sum to the ledger`: random voices, finishes and steals; at every admitted onset, `Σ slotWeight == soundingWeight(onset)`.
  - `an orphan slot's grain is stolen first`.
  - `the heaviest weak voice yields before a held one`.
  - `the heaviest held voice yields when none is weak`.
  - `the drawing voice may yield its own grain`.
  - `a candidate under half its window is passed over`: the next candidate is stolen.
  - `no candidate past half drops the draw`.
  - `one voice steals as the shared rule did`: one voice, no orphan; each stolen index is the smallest index sounding and unstolen at its onset, reconstructed from the event and steal lines.
  - `steals are block size invariant in voice mode` (determinism): blocks 1, 32, 64 and 4096.
  - `the steal line carries v for a voice grain`: `formatStealLine(0, 5, 3, 900)` gives `{"i":5,"layer":0,"v":3,"steal":900}`, and `formatStealLine(0, 5, kNoVoice, 900)` gives `{"i":5,"layer":0,"steal":900}`.
- [ ] **Step 2:** Run `ctest --preset dev -L 'voice|budget|determinism|render'`. Expected: FAIL.
- [ ] **Step 3:** Implement. The weights are updated wherever the heap adds or forgets an entry. At most 17 candidates per draw. A slot reused by a new voice keeps its old grains (EB6).
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS; `fx-overload` and `inst-overload` keep their logs. Run the at-rest check. Expected: 108/0/0 and the voice goldens unchanged (`grep -c steal tests/golden/expected/*voice*.events.jsonl` is still 0).
- [ ] **Step 5:** Commit `feat(engine): Voice mode's steal order (orphans, then weak, then held, heaviest first); the steal line's "v" (row 412)`.

### Task 10: The steal by budget (E10, EB12, P8, P9)

**Files:**
- Modify: `engine/include/gg/engine/Engine.h`, `engine/src/Engine.cpp` (`soundingWeightAt`, `nextGrainWeight`), `core/include/gg/core/VoiceAllocator.h`, `core/src/VoiceAllocator.cpp` (`oldestWeak`), `core/include/gg/core/Processor.h`, `core/src/Processor.cpp` (`layerQuotaAt`, shared with `render`; `commitVoices`; `budgetVoiceSteals`)
- Test: `tests/core/voice_processor_tests.cpp`

**Interfaces:**
- Consumes: Task 9's `VoiceParams::weak`.
- Produces:
  - `uint64_t Engine::soundingWeightAt(int64_t t) noexcept` (`soundingWeight`'s requirement holds).
  - `uint32_t Engine::nextGrainWeight(Tier) const noexcept`: the weight of a grain of that tier reading the engine's current source, stereo as the ladder decides it.
  - `uint64_t Processor::layerQuotaAt(int64_t t) const noexcept`.
  - `int VoiceAllocator::oldestWeak() const noexcept` (−1 for none).
  - `uint64_t Processor::budgetVoiceSteals() const noexcept`.
  - In `commitVoices`, before each `start` on a Voice layer: if `soundingWeightAt(b) + nextGrainWeight(the layer's Quality) > layerQuotaAt(b)` and `oldestWeak() ≥ 0`, that voice finishes as V4's steal finishes one. At most one per started voice.

- [ ] **Step 1:** Tests:
  - `a new note under a spent budget finishes the oldest weak voice`: Budget 256, A Voice at HQ and Density 400 Hz, a pedaled chord released, then a new note. `budgetVoiceSteals() == 1`; the oldest weak voice spawns nothing after b; its note's `NOTE_END` comes once its voices are done.
  - `budget steal with no weak voice finishes nothing`: the same with every note held. `budgetVoiceSteals() == 0`, and the render's click score (`gg::metrics`, as `processor_voice_steal_pedal` measures it) is under 15.
  - `the budget steal frees the slot the new voice takes`: 16 voices, one weak, the budget spent. After the 17th note, 16 voices spawn and none of the held ones was cap-stolen.
  - `a budget steal lowers the headroom count`: the other voices' later grains gain by `sqrt(n / (n − 1))`, exact to the engine's float math.
  - `budget steals are block size invariant`: blocks 1, 32, 64 and 4096.
  - `[rt]`: the tripwire on `process` over a render with Voice-mode grain steals (Task 9) and budget steals.
- [ ] **Step 2:** Run `ctest --preset dev -L voice`. Expected: FAIL.
- [ ] **Step 3:** Implement. Move the quota expression out of `render` into `layerQuotaAt`, unchanged for `render`.
- [ ] **Step 4:** Run `ctest --preset dev -LE slow`. Expected: PASS. Run the at-rest check. Expected: 108/0/0 and the voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(core): the steal by budget finishes the oldest weak voice for a new note`.

### Task 11: The plugin's new inputs and outputs (E1, E4, E13, E14 part 1, EB2, P5, P7; row 416)

**Files:**
- Modify: `core/include/gg/core/Processor.h`, `core/src/Processor.cpp` (`EndedNote::at`, `pushEnded(n, at)`, `sampleClock()`), `plugin/src/GigaGrainsPlugin.h`, `plugin/src/GigaGrainsPlugin.cpp`:
  - `isControlEvent` and `toControlEvent` (MIDI CCs through `controlFromMidiCc`; the seven note expressions);
  - `toNoteEvent` (0x90 at velocity 0);
  - `extension` (as_vst3 adds `AS_VST3_NOTE_EXPRESSION_VIBRATO | AS_VST3_NOTE_EXPRESSION_EXPRESSION`);
  - `sendOutputEvents` in place of `sendEndedNotes`;
  - `uiSetValue` (P7).
- Test: `tests/plugin/voice_plugin_tests.cpp`, `tests/plugin/mpe_plugin_tests.cpp` (new, `[plugin][mpe]`), `tests/plugin/rt_tests.cpp`

**Interfaces:**
- Consumes: Task 6's `takeSettingChanges` and `otherZoneAfter`, Task 7's kinds.
- Produces:
  - `EndedNote` gains `int64_t at`. `int64_t Processor::sampleClock() const noexcept` returns the next call's first sample.
  - `void GigaGrainsPlugin::sendOutputEvents(const clap_output_events *out, int64_t callStart, uint32_t frames) noexcept` pushes the waiting setting changes (`CLAP_EVENT_PARAM_VALUE`; each value is stored into `values_` when taken) and `NOTE_END`s, merged by stamped time, the setting changes first at a tie. A refused push leaves the rest waiting (fixed arrays: 32 changes, `kMaxEndedNotes` notes).
  - A MIDI note-on at velocity 0 is an Off with `releaseVelocity = 64.0f / 127.0f`.

- [ ] **Step 1:** Tests:
  - `plugin an MCM writes the MIDI settings and tells the host`: CC 101 0, 100 6, 6 7 on channel 0 at time 10 in a 512 block. The output holds `PARAM_VALUE`s for MPE, MPE Lower Zone, MPE Upper Zone, MPE Bend Range and Bend Range at time 32; `params.get_value` returns them; the saved state carries them; `presetEdited()` is false.
  - `plugin a preset load keeps the written zones`.
  - `plugin a zero frame call reports written settings and note ends at frame 0`.
  - `plugin a zone edit writes both counts`: `uiSetValue(MpeLowerZone, 10)` at Upper 10 queues Upper 4.
  - `plugin note end is stamped at its boundary`: a note whose voice ends at the cell 3 × 32 into a call gets `NOTE_END` at time 96, not 511 (row 416).
  - `plugin note expressions pan vibrato and expression reach the voice`.
  - `plugin a channel wide tuning bends every voice on its channel`: `note_id` −1, key −1 and TUNING 2.0 give rates `exp2((key − 60 + 2) / 12)` (the AU's dialect).
  - `plugin CC 11 reaches Expression`.
  - `plugin as vst3 lists vibrato and expression`: the mask equals TUNING | VOLUME | PAN | BRIGHTNESS | VIBRATO | EXPRESSION, and PRESSURE is absent.
  - `plugin note on velocity 0 releases at 64 of 127`: `source(plugin, ModSource::ReleaseVelocity) == 64.0f / 127.0f`.
  - `[rt]`: the tripwire on `process` with an MCM, RPN 0, CC 11, the new expressions and `NOTE_END`s.
- [ ] **Step 2:** Run `ctest --preset dev -L plugin -LE slow`. Expected: FAIL.
- [ ] **Step 3:** Implement. Each converted event counts toward `kMaxControlEvents`.
- [ ] **Step 4:** Run `ctest --preset dev -L plugin -LE slow`. Expected: PASS. Run the validators (`scripts/fetch-validators.sh tools-bin` first): clap-validator and pluginval at strictness 10. Expected: clean. Run the at-rest check. Expected: 108/0/0 and the voice goldens unchanged.
- [ ] **Step 5:** Commit `feat(plugin): MPE zones by MIDI, the written settings to the host, new expressions, NOTE_END at its boundary (row 416)`.

### Task 12: The plugin's per-note modulation, per-note flags and voice-info (E7, E11)

**Files:**
- Modify: `plugin/src/GigaGrainsPlugin.h`, `plugin/src/GigaGrainsPlugin.cpp` (`toParamEvent` keeps `note_id`, `channel` and `key` of `CLAP_EVENT_PARAM_MOD`; `paramsInfo` adds the three flags; `implementsVoiceInfo`, `voiceInfoGet`)
- Test: `tests/plugin/voice_plugin_tests.cpp`, `tests/plugin/params_tests.cpp`, `tests/plugin/rt_tests.cpp`

**Interfaces:**
- Consumes: Task 8's `ParamEvent` fields and `perNoteRefOf`.
- Produces:
  - `paramsInfo` sets `CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID | _PER_KEY | _PER_CHANNEL` exactly where `perNoteRefOf(slot).valid`, in both products.
  - `voiceInfoGet`: `voice_count` 16, `voice_capacity` 16, `flags = CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES`, in both products.

- [ ] **Step 1:** Tests:
  - `plugin per note flags are on exactly the 92 voice slots`.
  - `plugin voice info answers 16 16 overlapping`: both products.
  - `plugin a per note PARAM_MOD reaches one voice`: by note ID, then by key with ID −1.
  - `plugin a monophonic PARAM_MOD is unchanged`: all three −1, on any port.
  - `[rt]`: the tripwire with per-note modulation.
- [ ] **Step 2:** Run `ctest --preset dev -L plugin -LE slow`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run `ctest --preset dev -L plugin -LE slow` and clap-validator. Expected: PASS; it reads `voice-info` and the param flags without complaint. Run the at-rest check.
- [ ] **Step 5:** Commit `feat(plugin): CLAP per-note modulation, the per-note flags, voice-info`.

### Task 13: gg-render's keys (§4.8)

**Files:**
- Modify: `tools/gg-render/RenderSpec.h`, `tools/gg-render/RenderSpec.cpp`, `tools/gg-render/Render.cpp`
- Test: `tests/tools/render_spec_tests.cpp`

**Interfaces:**
- Consumes: Task 6's `controlFromMidiCc`, Task 7's kinds and Task 8's `ParamEvent` fields.
- Produces:
  - `ControlSpec` kind `"cc"` with `channel` (an integer, 0 to 15), `controller` and `value` (an integer, 0 to 127). It takes controllers 1, 6, 11, 38, 64, 74 and 98 to 101; any other exits 2, naming it.
  - Kind `"expression"` with `expression` (one of the seven names), `channel`, an optional `key` (default −1) and `value`, sent with note ID −1.
  - A note's `expressions` kinds gain `"pan"`, `"vibrato"` and `"expression"`.
  - A note gains `"paramMods": [{"time", "param", "value"}]`, which needs the note's `id`; `param` must be a per-note slot's display name, and `time` must be inside the note's span.
  - Every built event has seq 0.

- [ ] **Step 1:** Tests:
  - `render spec reads cc controls`.
  - `render spec refuses a cc it does not convert`: controller 7 exits 2 with `controller 7`.
  - `render spec reads channel wide expressions`.
  - `render spec reads pan vibrato and expression note expressions`.
  - `render spec reads paramMods`, and its refusals: no `id`, `"Reverb Amount"`, an unknown name, a time outside the span. Each exits 2 with its message.
  - `render spec takes the new names`: `"MPE Upper Zone"` and `"A Pan"` in `params`, the sources `"Vibrato"` and `"Expression"`, and the target `"Env 1 Attack"` in `mod`.
- [ ] **Step 2:** Run `ctest --preset dev -L render`. Expected: FAIL.
- [ ] **Step 3:** Implement, documenting each key in `RenderSpec.h`.
- [ ] **Step 4:** Run `ctest --preset dev -L render`. Expected: PASS.
- [ ] **Step 5:** Commit `feat(render): cc and expression controls, note pan, vibrato, expression and paramMods`.

### Task 14: The Mod view's Voice toggle and the never-acting marker (E12; row 415)

**Files:**
- Modify: `ui/include/gg/ui/RouteEdits.h`, `ui/src/RouteEdits.cpp` (`setVoiceOnly`, `routeCanAct`), `ui/src/views/RouteList.h`, `ui/src/views/RouteList.cpp` (the toggle `route.N.voice` labelled "Voice"; dimming and tooltip)
- Test: `tests/ui/route_edits_tests.cpp`, `tests/plugin/factory_tests.cpp`; the frame tests in `tests/plugin/editor_frame_tests.cpp` run unchanged

**Interfaces:**
- Produces:
  - `void setVoiceOnly(gg::core::ModRoutes &, uint8_t slot, bool on)`: a slot out of range changes nothing.
  - `bool routeCanAct(const gg::core::ModRoute &) noexcept`: valid, and either it acts layer-wide (not Voice only, not to a voice-only target) or it acts per voice (a per-voice source to a voice target, Env 1 and Env 2 excluded for a voice-only target).
  - A row whose route can't act draws at reduced alpha, with the tooltip `Acts on no layer: needs a per-voice source and a voice target`.

- [ ] **Step 1:** Tests:
  - `setVoiceOnly sets the flag and nothing else`.
  - `routeCanAct`: a table covering Voice-only LFO → Size (false), Voice-only Velocity → Size (true), LFO → Env 1 Attack (false), Env 1 → Env 1 Release (false), Velocity → Env 1 Attack (true) and plain LFO → Size (true).
  - `the voice toggle edits a routes flag through undo`: toggle, undo, redo, all through the gesture path.
  - `a route that can act nowhere is drawn dimmed with its tooltip`.
- [ ] **Step 2:** Run `ctest --preset dev -L 'ui|plugin' -LE slow`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run `ctest --preset dev -L 'ui|plugin' -LE slow`. Expected: PASS. The frame tests (keyboard reach, inside the view at 960 × 600 and 640 × 360 in both themes, no elided label) need no edit. Regenerate the Mod view PNGs in `docs/listening/m2-views/snapshots/` with `gg-snap` and look at each.
- [ ] **Step 5:** Commit `feat(ui): the Mod view's Voice toggle, and routes that act nowhere dimmed (row 415)`.

### Task 15: Six goldens and the twin test (§4.9)

**Files:**
- Create: `tests/golden/cases/{inst-mpe-zones,inst-mpe-zones-expr,inst-voice-pan,inst-voice-env-targets,inst-voice-budget,inst-voice-permod}.json` and their expected files
- Modify: `tests/metrics/click_injection_tests.cpp` (an entry with its reason for any case that hides steps), `tests/core/voice_processor_tests.cpp` (`mpe zones and note expressions render the same voices`)

- [ ] **Step 1:** Write the six specs as §4.9 describes them: seed 1, block 256, 6 to 8 s, 48 kHz, the gestures the builder's choice. The MIDI twin uses `cc` controls for the MCM and RPN 0. The CLAP twin keeps the MIDI settings at their defaults and sends each note's tuning as the semitones its MIDI twin's member and master bends make together.
- [ ] **Step 2:** Run `scripts/update-goldens.sh build/dev`. Expected: the six are new. Then restore every other expected file (`git checkout -- tests/golden`) and run the at-rest check. Expected: 108/0/0 and the voice goldens unchanged.
- [ ] **Step 3:** Add the twin test: the two cases' event lines are equal after dropping `i`, over the spans the test names. Look at each new spectrogram (`build/dev/golden/<case>.png`). Run `ctest --preset dev -L golden`. Expected: PASS, the click injection included; `inst-voice-budget`'s log holds steal lines with `"v"`.
- [ ] **Step 4:** Commit `test(golden): six expression cases`, listing their WAV hashes, any click-table entry and its reason, and stating that every earlier hash is unchanged. Stage only the six cases' files and the two test files.

### Task 16: The steal order's benchmark (EB6)

**Files:**
- Modify: `tools/gg-bench/Bench.h`, `tools/gg-bench/Bench.cpp`, `tools/gg-bench/main.cpp` (`engine-voices-overload`: 16 voices on one engine in voice mode, half of them weak, capacity 16,384, a quota of 2,000 Normal grains' worth and 1 s grains asked for at four times that rate; report-only, no baseline entry)
- Test: `tests/tools/bench_tests.cpp`

- [ ] **Step 1:** Write `engine-voices-overload steals in voice mode`: a `--quick` run reports the case with `stolen > 0` and `dropped > 0`.
- [ ] **Step 2:** Implement.
- [ ] **Step 3:** Run `build/release/tools/gg-bench/gg-bench --quick --cases reference,engine-voices,engine-voices-overload`.
- [ ] **Step 4:** Commit `bench: engine-voices-overload (report-only)`, quoting the ratios.

### Task 17: Records

**Files:**
- Modify:
  - `INTENT.md`:
    - §14: zones, the expressions' map, Pan, the voice and envelope targets, Vibrato and Expression, per-note modulation, the tie order, E9 marked as applying D4 per voice (#81), the budget steal, and the "per-voice quotas" bullet rewritten;
    - §15: `voice-info`, per-note modulation on CLAP only, as_vst3's two new expressions, EB4, and E5's ±2;
    - §17: the toggle and marker, the menus, the MIDI panel's four and Pan;
    - §18: the four kept by a preset load, the CLAP features list, no state format change;
    - §23: one row with E1 to E15, EB1 to EB13 and P1 to P9.
  - `docs/m3-carry-over.md`: close rows 407, 412, 415, 416 and 417, naming their commits. Add new rows for EB4 (RPN through VST3), per-note modulation absent from VST3 and AU, the AU's channel bend at ±2, and CC 7 and CC 10 declined.
  - `CLAUDE.md`:
    - the label `mpe`;
    - 298 parameters;
    - the new spec keys;
    - six goldens (122 in all);
    - `engine-voices-overload`.
- Create: `docs/listening/m3-expression/README.md` (the host-check list of §4.10, and the six goldens' WAVs as A/B files)

- [ ] **Step 1:** Write them.
- [ ] **Step 2:** Commit `docs(m3): full expression's records`.

### Task 18: The gate

- [ ] **Step 1:** Run `scripts/check.sh --presets dev,asan,rtsan`, one gate on the machine at a time; where the machine has no RTSan LLVM, rely on CI's (row 420). Expected: `PASS`.
- [ ] **Step 2:** Run the at-rest check. Expected: `baseline cases: 108, changed: 0, gone: 0`; the voice goldens unchanged; the six new goldens pass.
- [ ] **Step 3:** Run the validators (clap-validator, pluginval at 10, Steinberg's validator; auval on a Mac or in CI).
- [ ] **Step 4:** Merge `m3-voice` once more if it moved. Push and open the draft PR. Fix any GCC 13 diagnostic from CI. Paste the gate's summary, the hashes and `engine-voices-overload`'s figure into the final report.

**Task order and parallelism:**
- Tasks 1 to 4 are serial, the merge-sensitive edits first.
- After them, 5 → 6 → 7 run in a line, as do 9 → 10.
- Task 8 needs 3 and 4. Task 11 needs 6 and 7, Task 12 needs 8, and Task 13 needs 6 to 8. Task 14 needs 3 alone.
- Task 15 needs everything before it but 14 and 16.
