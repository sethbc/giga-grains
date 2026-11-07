# M3 sub-project 1: the Voice slice

| | |
|---|---|
| **Status** | Built on `m3-voice` (draft PR #37), 2026-11-07, written overnight at the director's request. **The director approved** (2026-11-07, "3) approved") the calls put to him by name: V1, V2, V3, V4 and B10; V5, V6 and the rulings B1 to B9 stand as Claude's. Gain staging (voices sum without headroom) waits for sub-project 2 ("4) sounds fine"; carry-over) |
| **Program** | `docs/superpowers/specs/2026-11-07-m3-program-design.md` (§4, §5, §8 row 1; its §2a P1 to P8) |
| **Branch** | `m3-voice` (from `m3-program`) |
| **Research** | the note path (`Processor`, `Engine`, `CloudScheduler`, `GrainPool`, `ModMatrix`, the plugin's events), clap-wrapper's MPE paths, sst-voicemanager (not used, P6); the notes are summarized where used |

## 1. Intent

The first playable M3: a layer at **Mode Voice** turns every note into a voice with its own grain
stream, pitch, envelopes and expression, and keeps spawning through the release. A LinnStrument
plays it per note in Bitwig (CLAP and VST3) and, if Live sends per-note data to a VST3, in Live; a
keyboard plays it with the sustain pedal in every format. At Mode Cloud, the default, nothing moves:
all 108 goldens keep their WAV SHA-256.

## 2. Scope

**In:** the Mode parameter; the voice table and note identity (rows 184, 404); voices in the
engine; per-voice amplitude (Env 1), Env 2 and the per-voice mod-matrix sources on engine and Level
targets; pitch from key, bend and note expressions; the sustain pedal; MPE, MPE Bend Range and Bend
Range; the three input paths (CLAP note expressions, MIDI-MPE, `clap_plugin_as_vst3`); the
Voice-only route flag and factory routes 44 to 48; `NOTE_END`; the tail; `gg-render` voice specs and
goldens; the Edit view's placement of the new parameters (the frame tests require it).

**Out (later sub-projects):** the MPE Configuration Message and RPN 0 (2); upper zones (2);
per-voice pan and the volume expression beyond a gain (2); CLAP per-note parameter modulation (2);
per-voice budget quotas (2); the Mod view's Voice-only toggle (2); pitch-sync (3); tuning (4);
voice tints and new presets (5); per-sample pitch glides inside a grain (later, if ears ask).

## 3. Forced choices (Claude's provisional calls; the director may overrule each)

- **V1. Env 1 is the voice's amplitude.** On a Voice layer each voice runs Env 1 (the existing
  global Env 1 parameters) as its own ADSR, and its value multiplies the gain of every grain the
  voice spawns, latched at the grain's onset as **the value Env 1 reaches at the grain's peak**
  (onset + half its duration), computed in closed form from the linear ADSR with the note's gate
  as it stands at the onset. So the strike's forced grain speaks at the attack's level rather than
  at 0 (the "light touch speaks" anti-goal), and a release thins grain by grain. A note-off that
  falls between a grain's onset and its peak doesn't change that grain. The voice stops spawning when Env 1's release reaches
  0; its grains play out their windows. Env 1 is also still a mod source. *Declined:* four new
  per-layer amp parameters (a cleaner model, but 16 new parameters and a second envelope that
  overlaps Env 1's purpose); no implied amplitude (a voice would never end on its own).
- **V2. Notes don't freeze an FX Voice layer.** D4's notes-freeze applies to Cloud layers as
  today; on a Voice layer the layer's Freeze parameter alone freezes its ring, so voices play the
  live input pitched (a harmonizer-like voice). *Declined:* notes freezing Voice layers too (the
  M1 hold-and-play), which turns every played chord into a frozen snapshot.
- **V3. A voice's pitch is latched per grain** (each grain reads at the voice's pitch at its
  onset), so a slide moves the pitch grain by grain. At Density 64 Hz and Size 100 ms, six
  overlapping grains span a slide, heard as a smooth, slightly chorused glide. A per-sample glide
  inside each grain is a new kernel path; deferred.
- **V4. A stolen voice stops spawning; its grains play out their windows.** That is the fade: a
  grain's window never clicks, so no steal fade is needed. The cap counts spawning voices: a 17th
  note-on steals the oldest released voice, else the oldest held one. The overload ladder still
  governs grains.
- **V5. Factory routes 44 to 48**, not 13 to 17: the Mod view adds a user's first route at the first
  free slot from 13, so the Voice-only routes stay out of the way at the end of the 48. (This
  amends the program design's P4, which named 13 to 17.)
- **V6. Pressure, Timbre and Note Pitch are 0 until a voice has a value.** A keyboard sends no
  CC 74, so Timbre → Position does nothing for it; a LinnStrument's first Y lands before its
  note-on, so its voice starts with it.

- **V7. Voice layers get automatic headroom** (the director, 2026-11-07: "yes voice lyaers should
  get automatic headroom"; the form is Claude's call). Each voice grain's gain is multiplied by
  `1 / sqrt(n)`, where n is the number of the layer's voices spawning in the `process()` call that
  admits the grain (its own voice included; releasing and pedal-held voices count until they
  finish). It is latched at the grain's onset like the existing `1 / sqrt(max(1, Density × Size))`,
  so a chord's summed power stays near one voice's, a new note thins the others grain by grain
  without a step, one note is unchanged (n = 1), and Cloud layers are untouched. Voices start and
  finish only at control boundaries, where the `Processor` cuts its pieces, so n is block-size
  invariant.

## 3a. The builder check's rulings (2026-11-07; Claude's, binding on the plan)

- **B1. Every cell is applied while a Voice layer has a sounding voice,** as with time-varying
  routes, so a voice's per-voice Env 1 and Env 2 step, and its end reaches the engine, at the cell
  it happens, whatever events arrive. V1's peak gain is computed from the Env 1 state handed over
  at the grain's cell.
- **B2. No double counting:** on a Voice layer, a route whose source is per voice is left out of
  the layer-wide pass for the per-voice targets (the engine's and Level) and applied per voice
  only; for every other target it reads the instance-wide value (§4.4).
- **B3. At Mode Voice the layer's Cloud stream doesn't schedule;** the tie rule "Cloud first"
  matters only at a mode-switch boundary.
- **B4. P7 as amended:** per-voice sources on targets that aren't per voice read the
  instance-wide values of §4.4 (Velocity and Key the last note-on, Env 1 and Env 2 gated by the
  held set, as today; Pressure, Timbre and Note Pitch the newest held note's; Release Velocity the
  last note-off's). This amends the program design's P7 ("the newest sounding voice") and its
  §4.4 ("the new ones 0 on Cloud"), so no Cloud render moves.
- **B5. A short tap keeps its gate for one cell on Voice layers:** a note that starts and ends in
  one group starts its voice with the gate on and applies the Off at the next cell (b + 32), so
  its Env 1 attacks and the voice plays its release. Cloud's held set is unchanged.
- **B6. This spec amends two parent rules,** recorded in §23 with V1–V6: grains are summed in spawn
  order across a layer's voices (not per voice by ordinal), and a stolen voice gets no steal fade
  of its own (V4: its grains' windows are the fade).
- **B7. The direct bend's center is exact:** the voice's bend is `(v − 8192) / 8192` from the
  14-bit value, kept as its own field (the Pitch Bend mod source keeps today's
  `(msb << 7 | lsb) / 16383`). The MPE-against-expression goldens use gestures exactly
  representable on both paths, and their equality test compares only those spans.
- **B9 (Task 6's review). Note Pitch includes the tuning expression:** `(key + bend + tuning) / 127`
  clamped, so a CLAP TUNING gesture and the same MIDI-MPE bend give the same source value.
- **B10 (Task 6; for the director). A layer switched from Cloud to Voice, or turned on, starts no
  voices for notes already held;** the next note-on starts one. Voice → Cloud resumes Cloud from
  the held set at once.
- **B8. A held voice whose Env 1 sits at 0 in Sustain stops spawning,** as if finished (it frees
  its slot), so a Sustain-0 patch doesn't hold silent voices.

## 4. Design

### 4.1 Parameters (append-only IDs)

- **Mode**, per layer, stepped: 0 Cloud (default), 1 Voice. A new layer row (the next free layer
  index, `kNextFreeLayerIndex`), so a CLAP ID each for A to D; automatable, but a change applies at
  a grid boundary (a layer switched from Voice to Cloud stops its voices spawning, as a steal).
- **MPE** (global, stepped, Off default), **MPE Bend Range** (1 to 96 semitones, default 48) and
  **Bend Range** (0 to 24 semitones, default 2): not automatable, and a preset load keeps the
  instance's values, as it keeps the ring parameters (program P2).
- No patch-version bump: a state without the new values reads their defaults. The builder checks
  `PatchState` and records the result.
- Every hard-coded count follows (`tests/support/ParamTable.h`, `patch_tests`, `identity_tests`,
  `params_tests`, `rt_tests`, `EditLayout` and its tests). **Mutate** is keyed by
  `counter × kParamCount + slot`, so any new parameter moves every Mutate press: first, as its own
  commit, re-key Mutate by CLAP ID so later appends never move it again; that commit moves the
  `--mutate-preset` outputs once and says so (no audio golden moves). The new parameters are
  locked out of Mutate (Mode, like Source, is structural; the bend settings are the instance's).

### 4.2 Notes, identity and the pedal

- `NoteEvent` gains the CLAP note ID (−1 when none) and the release velocity.
- **The note table** (the `Processor`'s, fixed size, say 128 entries; the builder sizes it with a
  test at the cap): each entry is a note: ID, channel, key, velocity, state (held, pedal-held,
  released), its per-note expression (bend, pressure, timbre, tuning, volume) and the start
  ordinal.
- **Identity:** a note-off addressed by ID releases that note; one by (channel, key) releases the
  **oldest** held note on that pair (so two notes on one pair are two notes; row 184 closes for
  every layer); wildcard and CC 123 rules stay as today (R14, R15).
- **The held set Cloud layers read** is the set of keys with any held or pedal-held note, ascending
  and distinct as today. Applied per group exactly as `held_` is today: a note that starts and ends
  inside one group leaves the held set unchanged. A test pins that the held keys at every boundary
  equal today's for every M2 note sequence (no duplicates, no pedal).
- **The sustain pedal:** CC 64 ≥ 64 is down, < 64 up, per channel; with MPE On, the master
  channel's (channel 1) pedal acts on every channel. While down, a note-off moves the note to
  pedal-held; pedal-up releases the pedal-held notes. Cloud's held set and the FX's notes-freeze
  count pedal-held notes as held (program P8).
- **Voice-mode grouping:** a note that starts and ends inside one group *does* start a voice and
  release it at once (a short tap plays its release), unlike Cloud's held set.

### 4.3 Voices in the engine

- Each layer's `Engine` gains **up to 16 voice streams** beside its Cloud stream. A voice stream is
  a `CloudScheduler` with no held keys, its pitch folded into its stream parameters, and a forced
  onset at the voice's start (as a note-on forces one today). At Mode Cloud no voice stream exists
  and the Cloud path runs exactly as today (`cloud_.schedule`, EC:579, untouched).
- **The RNG key:** a voice stream's draws use `stream = 1 + ordinal`, the ordinal counting the
  layer's voices since activation (a `uint32_t`; Cloud's stream 0 untouched). `rng::Key` and its
  hash don't change.
- **Admission:** every stream's draws for a cell are merged by onset, ties broken by stream
  (Cloud first, then ordinal), before `admit` (the pool's ledger and steal cursor require
  non-decreasing onsets, GP:252-292). Grains are summed in spawn order, as today; with one stream
  that order is today's exactly.
- **The pool and budget:** voices share the layer's pool (4,096) and the layer's quota; the overload
  ladder treats their grains as any others. Per-voice quotas are sub-project 2's.
- **Per-voice values at a grain's onset:** each voice carries its own `CloudParams` (the layer's
  effective values plus its per-voice route offsets, §4.4), its pitch (§4.5) and its amplitude, the
  product of Env 1's value, the Level offset of its routes (dB to gain) and its volume expression,
  latched into the grain's gain at onset like the existing gain normalization.
- **A voice's life:** started at its note's boundary; releasing from its note-off (or pedal-up)
  boundary, still spawning, Env 1 releasing; **finished** at the boundary Env 1 reaches 0 (no more
  spawning; its slot is free; its grains play out). A steal or a switch to Cloud finishes it at
  once. The enable gate, `Engine::reset` and a layer's ramp to off clear voices with the pool, at
  the same samples they clear Cloud grains.
- **Spectral layers in Voice mode:** voices read the layer's smear like any grain; the smear's own
  analysis follows the layer's base Position (one per engine, EC:542). Documented, not changed.
- **The event log:** a voice grain's line adds `"v":ordinal` after `"layer"`; Cloud lines are
  byte-identical to today. `i` is the stream's grain index.

### 4.4 Per-voice modulation

- **Sources per voice** on Voice layers: Env 1 and Env 2 (each voice's own ADSR, gated by its note
  and the pedal), Velocity, Key, and the new **Pressure**, **Timbre**, **Note Pitch** (key plus bend,
  `(key + bend) / 127` clamped to [0, 1]) and **Release Velocity** (0 until the note's off), appended
  to `ModSource` (saved by name).
- **Evaluation:** the layer's effective values are computed as today from the global sources; then,
  per voice, the routes whose source is per voice add their offsets on top, and only the touched
  slots are clamped. A route marked **Voice only** is skipped on Cloud layers; on a Voice layer it
  applies per voice. A per-voice route on a Voice layer applies per voice whether or not it is
  Voice only.
- **Targets a voice can carry:** the engine's (Density, Size, Position, Pitch, Spray, Stereo,
  Distribution, Texture, Smear, Quality's tier and the spreads), latched at each grain's onset, and
  **Level**, as the voice's gain (§4.3). Other layer targets (Diffusion, Feedback, the buses, Mix)
  and global targets read the instance-wide values (P7): Velocity and Key the last note-on as today;
  Env 1 and Env 2 gated by the held set as today; Pressure, Timbre and Note Pitch the newest held
  note's; Release Velocity the last note-off's. So Cloud and global behavior are today's.
- **Change detection:** a voice's `CloudParams` is recomputed when its sources or the layer's
  values change, compared bit for bit as `updateEffective` does, so ruling M1-h's onset rescale
  doesn't fire every cell.
- **Cost:** about 72 KB of per-voice scratch, preallocated; the per-voice pass runs only for routes
  with per-voice sources, on Voice layers. The builder measures it with `gg-bench` (a new
  report-only `engine-voices` case: 16 voices over `engine-sample`'s workload).

### 4.5 Pitch and input

- **A voice's pitch:** the layer's Pitch (with its non-per-voice routes) + its per-voice Pitch
  offsets + (key − 60) + bend + tuning expression, clamped to [−48, 48] as today.
- **Bend:** with MPE Off, a channel's pitch bend × Bend Range bends that channel's voices. With MPE
  On, a member channel's (2 to 16) bend × MPE Bend Range bends that channel's voices, and the master
  channel's (1) bend × Bend Range bends every voice. Cloud layers take no direct bend (pitch bend
  stays a matrix source there).
- **Pressure:** a note's poly aftertouch when it has had one, else its channel's pressure. With MPE
  On, the master channel's pressure isn't a note's (it stays the global Aftertouch source).
- **Timbre:** its channel's CC 74 / 127 (MPE On: member channels; MPE Off: the note's channel).
- **CLAP note expressions** (Bitwig CLAP; VST3 through `clap_plugin_as_vst3`): matched by note ID,
  else by (channel, key); TUNING adds semitones; PRESSURE sets Pressure; BRIGHTNESS sets Timbre;
  VOLUME multiplies the voice's gain (CLAP's 0 to 4); PAN, VIBRATO and EXPRESSION are accepted and
  ignored until sub-project 2.
- **`clap_plugin_as_vst3`:** 16 MIDI channels, and the supported note expressions TUNING, VOLUME,
  PAN and BRIGHTNESS. The VST3 note IDs reach us as CLAP note IDs.
- **The plugin** recognizes CC 64, CC 74, poly aftertouch (0xAn) and per-channel pitch bend and
  pressure as before, now with their channel kept; the `Processor` reads them per channel.
- **`NOTE_END`:** when a note is released and no Voice layer still has a spawning voice for it, the
  plugin emits `CLAP_EVENT_NOTE_END` for it (its ID, port, channel, key) from a fixed queue the
  `Processor` fills; with no Voice layer, at the release's boundary.

### 4.6 The tail

The instrument's tail grows from `kInstrumentTailSeconds` to cover a voice's release: plus Env 1's
longest release (10 s). The FX's is unchanged unless the builder finds a Voice layer can outlast it.

### 4.7 Factory routes 44 to 48 (Voice only)

| Slot | Source | Target | Curve | Depth (start; set by ear) |
|---|---|---|---|---|
| 44 | Pressure | Density | Unipolar | +2 (log2 Hz: two octaves denser at full pressure) |
| 45 | Pressure | Level | Unipolar | +6 dB |
| 46 | Timbre | Position | Unipolar | +0.5 (half the sample or ring span) |
| 47 | Velocity | Level | Bipolar | +6 dB (±6 around velocity 64) |
| 48 | Velocity | Size | Unipolar | +0.5 (log2: up to √2 longer) |

Every layer, like the macro routes. The builder confirms the depths by rendering and listening
metrics, and records them in INTENT §14's factory-route table.

### 4.8 `gg-render`

- A note takes optional `"id"`, `"releaseVelocity"` and `"expressions"`: a time-ordered list of
  `{"time", "kind", "value"}`, kinds `tuning` (semitones), `pressure`, `brightness`, `volume`,
  sent as CLAP note expressions on that note.
- `controls` gains kinds `sustain` (0 or 1), `timbre` (CC 74, 0 to 1), `polyPressure` (with
  `"key"`), and `pitchBend` and `aftertouch` keep their channel, so MPE-as-MIDI renders work.
- `params` takes `"A Mode"` to `"D Mode"`, `"MPE"`, `"MPE Bend Range"` and `"Bend Range"`.
- `mod` routes take an optional `"voiceOnly"`.

### 4.9 Goldens (new; each catches the 16 injected steps or joins the table with its reason)

- `inst-voice-chord`: a pedaled C-major chord at Mode Voice, released with the pedal up at 3 s;
  the release keeps spawning (row 404 audible).
- `inst-voice-release`: one note, Env 1 Release 2 s, Density 32 Hz: the stream through the release.
- `inst-voice-mpe`: three MPE notes on channels 2 to 4 with bend slides, pressure swells and CC 74
  moves (MIDI path).
- `inst-voice-expr`: the same gestures as CLAP note expressions; its event log matches
  `inst-voice-mpe`'s line for line where the gestures are equal (a test, not just two goldens).
- `inst-voice-steal`: 20 notes held at once on one layer: four steals, no click.
- `inst-voice-dup`: two notes on one (channel, key), released one at a time (row 184).
- `fx-voice`: the FX, layer A Voice over a live 330 Hz sine, a three-note chord.
- `inst-voice-layers`: layers A Voice and B Cloud together, one phrase.

## 5. Tests

- Engine (`tests/engine/voice_tests.cpp`): a voice stream's key; the merge order (non-decreasing
  onsets, ties by stream); one voice equals a Cloud stream with the same params and a forced onset
  except for its key; the steal; the enable gate and reset clear voices; block-size invariance with
  voices.
- Core (`tests/core/voice_tests.cpp`): the note table, identity by ID and by pair (oldest first),
  the pedal, the held set equal to today's on M2 sequences, Voice-mode grouping, bend with MPE Off
  and On, pressure and timbre routing, per-voice Env 1 and Env 2, Voice-only routes skipped on
  Cloud, `NOTE_END`'s queue, the tail.
- Plugin: CLAP note expressions, MIDI-MPE, CC 64, `clap_plugin_as_vst3`'s answers, `NOTE_END`, the
  tripwire (`rt`) with voices.
- **Every existing golden's WAV SHA-256 unchanged:** `scripts/update-goldens.sh` before and after,
  compared, in the final report.
- `scripts/check.sh` (dev, asan, rtsan), the validators, and `gg-bench`'s headline ratios.

## 6. INTENT changes (Part II, with §23 rows)

§14: Mode, voices, the note table, the pedal, per-voice modulation, the factory routes 44 to 48;
§15: the input paths and the wrapper's limits; §17: the Edit view's Mode; §18: the new parameters
(a preset load keeps MPE and the ranges). Carry-over rows for the wrapper limits (AU MPE; the VST3
channel-state parameters' last point per block; no VST3 PRESSURE expression; the AU bend as a ±2
tuning expression under the CLAP dialect; Live VST3's delivery unconfirmed until a host test).
