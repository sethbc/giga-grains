# M3 sub-project 2: full expression

| | |
|---|---|
| **Status** | Draft, 2026-11-07, written without the director at his request ("keep building all of the sub-projects as parallel as possible and merging them as able to. progress as much as you can without input"). Every forced choice is **Claude's provisional call** (E1 to E15, §3), each with the alternatives declined; the builder check's answers are in §3a. The director reviews the calls later; **E9 (the steal order in Voice mode, which applies his D4 per voice) and E1 (the MPE Configuration Message writes the parameters) are the ones most likely to be overruled** |
| **Program** | `docs/superpowers/specs/2026-11-07-m3-program-design.md` (§4.4, §4.5, §5, §8 row 2, §10's items for 2; its §2a P2, P5 and P7) |
| **Builds on** | sub-project 1, `docs/superpowers/specs/2026-11-07-m3-voice-design.md` (V1 to V7, B1 to B10) and its plan, on `m3-voice` (draft PR #37), including Task 7 (the plugin's input paths, `NOTE_END`) and Task 13 (V7's headroom) as they land |
| **Branch** | `m3-sp2-expression` (from `m3-voice` at `0885636`), worktree `~/src/gg-m3-sp2-expression` |
| **Runs beside** | sub-projects 3 (pitch-sync), 4 (tuning) and 5 (views and presets), in parallel (§7 lists the seams) |

## 1. Intent

Sub-project 1 made a note a voice. This one finishes what a voice can be told. A LinnStrument (or a
Seaboard, or a split controller) that announces its zones with the **MPE Configuration Message**
and its bend ranges with **RPN 0** is understood without a trip to the settings, the upper zone
included, and what it announced is saved with the session. Every CLAP note expression reaches the
voice (pan, vibrato and expression beside sub-project 1's four), a channel-wide one too, so the AU's
channel bend finally bends. The matrix reaches everything a voice carries: its pan, its bus split
and its own envelopes, so velocity can shape an attack and release velocity a release. Bitwig's
polyphonic modulators reach one voice through **CLAP per-note parameter modulation**, and the host
knows the instrument's polyphony through **`voice-info`**. Under load, a layer's voices share its
budget fairly: release tails and the heaviest voice yield first, so a pressed note never starves its
neighbors and a new note speaks. The Mod view edits a route's **Voice only** flag.

At rest (no new message, expression, route or per-note modulation; every new parameter at its
default) nothing moves: every existing golden keeps its WAV SHA-256, sub-project 1's Voice goldens
included.

## 2. Scope

**In:** the MPE Configuration Message (MCM) and RPN 0 bend ranges, per zone, lower and upper,
written through to the MIDI settings (four new globals); the per-zone pedal and bend; CLAP note
expressions PAN, VIBRATO and EXPRESSION, MIDI CC 11, and channel-wide note expressions (carry-over
row 407); a per-layer **Pan** parameter; the matrix's per-voice targets extended to Pan, Bus Split
and the eight envelope controls; two new per-voice sources, Vibrato and Expression; release velocity
completed (the envelope targets; a MIDI note-on at velocity 0); CLAP per-note parameter modulation
(P5; the CLAP path only); `voice-info`; the steal order and the steal by budget in Voice mode; the
event order at a tie (row 417); `NOTE_END` at its boundary's frame (row 416); the steal line's
`"v"` (row 412); the Mod view's Voice-only toggle and its never-acting marker (row 415); `gg-render`
keys and six goldens; the Edit view's placement of the new parameters (the frame tests require it).

**Out:** pitch-sync (3); the tuning's key term (4); the views' Voice controls beyond placement, voice
tints and presets (5); per-note automation (`CLAP_PARAM_IS_AUTOMATABLE_PER_*`, which no host we
target sends); a per-sample glide or volume ramp inside a grain (row 414's kernel path); per-voice
LFO phase (row 414); MIDI CC 7 and CC 10 per channel (E4); AU MPE (row 406, upstream); a VST3
PRESSURE note expression (row 409, upstream); per-note modulation through VST3 or AU (the wrappers
force note ID −1; a carry-over row).

## 3. Forced choices (Claude's provisional calls; the director may overrule each)

- **E1. The MCM and RPN 0 write the MIDI settings.** A completed MCM or RPN 0 sets the matching
  global MIDI setting's base value at its boundary, as a parameter event would; the plugin stores
  the value, tells the host (`CLAP_EVENT_PARAM_VALUE` out) and the session saves it, so a
  LinnStrument that announced ±24 once is still ±24 after the project reopens (it announces only at
  power-up and on a mode change). To hold an upper zone and the zones' sizes the MIDI settings gain
  four globals (§4.1). *Declined:* a runtime overlay on the parameters (lost on reload while the UI
  shows stale values); an `<mpe>` element in the patch (a second store of facts the parameters
  already hold, which a preset load would need its own rule to keep).
- **E2. The zones' rules** (MPE, RP-053, as the builder checks against the specification's text):
  - an MCM is RPN 6 on channel 1 (the lower zone) or 16 (the upper), data entry MSB the member
    count 0 to 15; on any other channel it is ignored. It sets MPE On (a count of 0 leaves MPE as it
    is), sets its zone's count, resets its zone's member range to 48 and its master's to 2, and
    shrinks the other zone to fit (`other = 0` when its count is 15, else `min(other, 14 − count)`
    when count > 0);
  - RPN 0 sets the range of the channel's role: a lower member's to **MPE Bend Range**, the lower
    master's to **Bend Range**, an upper member's to **MPE Upper Bend Range**, the upper master's to
    **Upper Bend Range**, and a conventional channel's (MPE Off, or outside both zones) to **Bend
    Range**; its data entry MSB is semitones, clamped to the setting's range; CC 38 (cents) is
    accepted and ignored (the ranges are whole semitones);
  - the RPN selection is per channel (CC 101, CC 100); CC 99 or CC 98 (an NRPN) or RPN 127/127
    deselects it; increment and decrement (CC 96, 97) are ignored; activation and reset clear it;
  - loaded or set values that overlap resolve **lower first**: in effect the upper zone holds
    `U' = U` when L is 0, else `min(U, 14 − L)` (0 when L is 15). The editor writes both counts as
    an MCM would (the edited one wins), so the UI never shows an overlap.
  *Declined:* honoring CC 38's cents (no controller we target sends any); a per-channel Bend Range
  with MPE Off (one keyboard plays one channel; a global setting is what the UI can show).
- **E3. Each zone's master acts on its own zone.** The lower master's (channel 1) pedal and bend
  reach the lower zone's channels, the upper master's (16) the upper's, a conventional channel's its
  own. A master channel's pressure, CC 74 and CC 11 aren't copied into notes (sub-project 1's rule
  for channel 1, now for both masters). With the defaults (MPE On, lower 15, upper 0) every channel
  2 to 16 is a lower member: sub-project 1's mapping exactly.
- **E4. The expressions' map** (program §10's item for 2):

  | Input | Reaches | Notes |
  |---|---|---|
  | CLAP TUNING | the voice's pitch, semitones | as built |
  | CLAP PRESSURE, MIDI poly and channel pressure | Pressure | as built |
  | CLAP BRIGHTNESS, MIDI CC 74 | Timbre | as built |
  | CLAP VOLUME | the voice's gain, linear 0 to 4 | as built; latched per grain like Env 1 (V1) |
  | CLAP PAN | the voice's pan, `(v − 0.5) × 2` added to its layer's Pan | new |
  | CLAP VIBRATO | **Vibrato**, a new per-voice source, 0 to 1 | new; unrouted |
  | CLAP EXPRESSION, MIDI CC 11 | **Expression**, a new per-voice source, 0 to 1 | new; unrouted |

  A grain latches its voice's pan and gain at its onset, so a pan or volume gesture moves grain by
  grain, as V3 moves pitch. *Declined:* CC 10 and CC 7 per channel (DAWs and controllers use them
  for track pan and volume, so routing them into voices would surprise; no controller we target
  sends them per note); Vibrato as a built-in pitch LFO (the matrix has no route-on-route depth, and
  a fixed vibrato would be a sound the user didn't choose).
- **E5. A channel-wide note expression is the channel's** (note ID −1 and key −1; closes row 407).
  It acts on every note the table holds on its channel (every channel for −1) and is that channel's
  starting value for notes that start on it later, until changed or reset: TUNING adds semitones to
  each note's own tuning (beside the bend), VOLUME multiplies its own volume (the product clamped
  to 0 to 4), PAN adds to its own pan, PRESSURE and BRIGHTNESS set the channel's pressure and CC 74
  state (which notes without their own follow, as built), VIBRATO and EXPRESSION set the channel's
  value (a note's own once it has had one, as pressure). A channel-wide PRESSURE is exactly a
  channel pressure (0xDn): it moves the Aftertouch source too; a channel-wide TUNING doesn't move the
  Pitch Bend source. So under the AU's CLAP dialect the
  channel's bend arrives as clap-wrapper's ±2 semitone tuning and bends the voices by ±2 whatever
  Bend Range says (a new carry-over row), and its channel pressure reaches Pressure. *Declined:*
  reading the AU's tuning as a normalized bend × Bend Range (it would misread a CLAP host that sends
  real semitones, and the wrapper's ±2 is not a contract).
- **E6. Pan, a per-layer parameter, and the voice's new targets.** A new **Pan** per layer (−100% to
  +100%, default 0): each grain's pan is `clamp(pan + stereo × bipolar(u), −1, 1)`, the add skipped
  when the pan is exactly 0, so every existing render keeps its bits. It is a route target on every
  layer and a voice target on Voice layers, so Key → Pan or Timbre → Pan works from MIDI-MPE too, not
  only from Bitwig's PAN. The voice targets (`isVoiceTarget`) gain **Pan**, **Bus Split** (a
  per-grain draw already) and the **eight envelope controls** (Env 1 and Env 2 Attack, Decay,
  Sustain, Release). The envelope controls are targets **per voice only**: no layer-wide pass ever
  moves them (`applyRoutes` skips them on every layer, so §14's "no route targets the modulators"
  holds for the layer stepping), and per voice only a per-voice source other than Env 1 and Env 2
  moves them (no envelope modulates its own segments), as does a per-note modulation (E7). So
  Velocity → Env 1 Attack and Release Velocity → Env 1 Release work; a voice's Env 1 is its
  amplitude (V1), so its own params drive the closed-form amp envelope and B8's sustain test. Pan is
  locked out of Mutate's defaults, so a default press moves what it moved before. *Declined:* pan
  from CLAP PAN alone (no route could reach it); envelope targets layer-wide (a cycle through the
  modulators' stepping, and nothing a Cloud layer needs).
- **E7. CLAP per-note parameter modulation (P5).** Every per-voice target of every layer (§4.5's
  list: the twelve engine parameters, Pan, Bus Split, the eight envelope controls and the layer's
  Level) gets `CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID`, `_PER_KEY` and `_PER_CHANNEL` (not `_PER_PORT`:
  one note port). A `CLAP_EVENT_PARAM_MOD` whose note ID is ≥ 0 reaches the note with that ID; one
  with note ID −1 and a channel or key given reaches every note the table holds that matches (−1 a
  wildcard); one with all three −1 is the monophonic modulation it is today (any port). The amount
  is kept per note and per slot, the latest replacing, and adds to that voice's slot before its
  clamp, beside its route offsets. It lives as long as the note (until it leaves the table); a note
  that starts later inherits nothing. It reaches only voices: a per-note modulation of a Cloud
  layer's parameter, of an unknown note, or of a parameter without the flags is ignored (never
  applied as monophonic). *Declined:* per-voice storage (a modulation sent at the note-on's sample
  arrives before the voice exists); applying a Cloud layer's per-note modulation instance-wide (one
  note's gesture would move every note).
- **E8. Events at one sample keep the host's order** (closes row 417). `ParamEvent`, `NoteEvent` and
  `ControlEvent` gain a sequence number; the plugin numbers them in the host's order, and the
  Processor's merge breaks a tie by it before the kind. With sequence numbers equal (0, as
  `gg-render` and the tests leave them), the tie falls back to the kinds: parameters, notes,
  **per-note parameter modulations**, controls; commands stay last as today. So a pedal-down
  written before a note-off on one tick sustains the note, a per-note modulation sent with its
  note-on finds the note, and every existing render, which leaves the numbers 0, merges as before.
  *Declined:* a fixed rule "controls before notes" (it would break the opposite order, which hosts
  also write).
- **E9. Voices share the layer's quota; the steal order chooses fairly** (program §4.5; INTENT §14's
  "per-voice quotas"). A layer's quota is not divided: any voice may use the room left, as today.
  When the ladder reaches its third rung on a Voice layer (no room at any tier), its victim is the
  first of these candidates that has sounded half its window (D4's test), examined in order:
  1. the oldest unstolen grain of each **orphan slot** (a slot whose voice has finished, or the
     Cloud stream's grains from before a switch), oldest first;
  2. the oldest unstolen grain of each **weak** spawning voice (released or pedal-held, as V4
     ranks them), heaviest voice first (the largest summed weight of its sounding grains; a tie goes
     to the lower ordinal);
  3. the same for each held voice, the drawing voice included, heaviest first.
  The first that has sounded half its window is stolen with the raised-cosine fade; if none has, the
  draw is dropped. With one voice and no orphan the only candidate is the layer's oldest grain, so
  D4 holds exactly; Cloud mode is unchanged. So a pressed, dense note yields its own grains before
  its neighbors', and release tails yield before held notes. **This applies the director's D4
  (§23 #81) per voice:** D4 says "the oldest sounding grain"; in Voice mode it becomes the first
  candidate in this order. *Declined:* a static share Q / n per voice (budget wasted while voices
  differ, and either an overshoot of the Budget or a silent strike when n grows); the shared
  oldest-grain rule as built (a pressed note thins its neighbors, whose gain normalization was
  latched for a denser cloud).
- **E10. The steal by budget.** At a boundary where a voice starts on a layer whose ledger leaves no
  room for its first grain (`W(b) + w(the layer's Quality, its channels) > Q`, Q the quota that
  layer renders the piece from b with), the layer's oldest weak spawning voice (if any) finishes
  first, as V4's steal finishes one: it stops spawning and its grains play out as orphans, first in
  E9's order. At most one per voice started, and before the start, so the new voice takes the slot
  it freed and V4's cap steal (the 17th note, otherwise unchanged) doesn't also fire.
  *Declined:* finishing the heaviest voice (a held note would end while tails ring); no budget steal
  (released voices go on spawning grains that are stolen as fast as they start).
- **E11. `voice-info` is static: 16 voices, 16 allocated, overlapping notes.** Both products answer
  `voice_count` 16, `voice_capacity` 16 and `CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES` (the
  counted identity of sub-project 1). *Declined:* `voice_count` 1 while every layer is Cloud (true for
  modulation, but a host may then manage notes as mono; it would need `changed()` plumbing for a
  value that matters little).
- **E12. The Mod view's Voice toggle marks, never refuses** (row 415). Each route row gains a toggle
  labelled "Voice", the route's Voice-only flag, edited through the undo stack like a depth. A route
  that can act nowhere (Voice only with a source or target that isn't per voice; an envelope target
  from a source other than a per-voice one, Env 1 and Env 2 excluded) is drawn dimmed with the
  tooltip "Acts on no layer: needs a per-voice source and a voice target". *Declined:* refusing the
  edit (a half-built route is a step of building a good one, as row 330's draft rows are).
- **E13. A MIDI note-on at velocity 0 releases at 64/127.** MIDI 1.0's value for a release velocity
  that wasn't sensed, so a keyboard that sends note-on 0 lands Release Velocity mid-range rather than
  at its floor. A note-off (0x8n) keeps its own byte; a CLAP note-off its value; a choke and CC 123
  release at 0. *Declined:* 0 (sub-project 1's), which makes a routed Release Velocity jump between
  keyboards that differ only in how they spell a note-off.
- **E14. Two records made exact** (rows 416, 412). `NOTE_END` is stamped at the frame of the boundary
  its note ended at (`EndedNote` carries the boundary; the plugin stamps `clamp(at − call start, 0,
  frames − 1)`), and a stolen grain's event line carries `"v":ordinal` after `"layer"` when the
  stolen grain was a voice's (`{"i":k,"layer":L,"v":o,"steal":onset}`); a Cloud steal line is
  byte-identical, so `fx-overload` and `inst-overload` keep their logs.
- **E15. The new parameters' homes.** The four MIDI settings join `isMidiSetting` (kept by a preset
  load, refused by `--pack-preset`, locked out of Mutate, no route targets them, not automatable nor
  modulatable) and the Edit view's MIDI group; Pan joins each layer's panel beside Stereo.

## 3a. The builder check's answers (Claude's, binding on the plan)

- **EB1. Where the RPN parser lives:** in the core (`core/include/gg/core/MpeZones.h`, a class the
  Processor owns), fed by a new control kind in the merge, so `gg-render` and the plugin share it.
  The plugin and `gg-render` turn a MIDI CC into a `ControlEvent` with one shared function
  (`controlFromMidiCc`, in a new `core/include/gg/core/MidiControl.h` and
  `core/src/MidiControl.cpp`), so a spec's `cc` means what the plugin's MIDI means.
- **EB2. How a written setting reaches the host:** the MCM or RPN's values join the group's pending
  base values in merge order (E8), like a parameter event for those slots, and apply at the boundary;
  the Processor then queues `{slot, value, boundary}` in a fixed queue (32, drops counted), which the
  plugin takes after each `process()` call: it stores each value (`values_`), pushes
  `CLAP_EVENT_PARAM_VALUE` at the boundary's frame, merged in time order with `NOTE_END`, and the
  editor follows `values_` as it follows host automation. A written MIDI setting never sets the
  preset's edited mark (the MIDI settings are the instance's).
- **EB3. When a new layout applies:** at its boundary, as a parameter does; controls later in the
  same group are routed by the layout before it. A note's zone is read where it is used (the bend,
  the pedal), so a layout change moves a sounding note's bend range at that boundary.
- **EB4. VST3 and RPN:** clap-wrapper's VST3 maps each MIDI controller to a hidden parameter and
  hands over each block's last point, ordered by parameter, so an RPN sequence inside one block can
  arrive out of order or partly lost; the parser takes what arrives. A new carry-over row; the MCM is
  dependable through CLAP's MIDI dialect, the AU's raw MIDI and the standalone.
- **EB5. Per-note modulation storage:** the note table's entry gains one float per per-voice slot of
  every layer (four layers × 23 slots, about 47 KB for 128 notes, preallocated); the builder may
  compact it if a test pins the same answers. A voice's parameter cache key includes its note's 23
  values for its layer, so a change recomputes it once (`voiceParamRecomputes`).
- **EB6. The steal order's cost:** the pool keeps per slot (16 voice slots and one for the Cloud
  stream) the summed weight of its sounding grains, updated where the ledger adds and forgets a
  grain, and a cursor per slot that advances, as `oldestSounding`'s does, only past rows that ended,
  were stolen or belong to another slot (a row's slot never changes), restarting at each retirement.
  A row records its slot; a slot reused by a new voice keeps its old voice's grains, which then count
  with the new voice (the steal takes the oldest first, so the old ones go first). At most 17
  candidates per draw. A row stores its voice's ordinal too (`GrainSpec::voice`), for E14's steal
  line. A slot's weight, for "heaviest", is its ledger weight including grains already fading.
  Only Voice mode's third rung uses any of it. `gg-bench` measures it
  (`engine-voices-overload`, report-only).
- **EB7. "Weak" reaches the engine** in `VoiceParams` (`bool weak`), from the allocator's flag, every
  applied boundary (B1).
- **EB8. The envelope targets' timing:** a voice's envelope parameters are its own from its first
  cell: computed in `updateVoices` at its start (so its first grains' amp envelope uses its
  velocity-shaped attack) and cached for the stepping at later boundaries, as the layer's are.
- **EB9. Pan's scheduler term:** `CloudParams` gains `double pan` (−1 to 1); the scheduler adds it to
  the draw's `stereo × bipolar(u)` and clamps, skipping both when it is exactly ±0. The PAN
  expression's offset joins the voice's pan before `toCloudParams`' clamp.
- **EB10. Instance-wide values (B4) of the new sources:** Vibrato and Expression read the newest held
  note's, as Pressure and Timbre do; 0 until a note has a value (V6).
- **EB11. The note's semitones:** `pitchIn = (key − 60) + bend + note tuning + channel tuning`, and
  Note Pitch takes the same terms (B9). This sub-project owns the bend term (`bendSemitones`, now per
  zone) and the channel tuning; sub-project 4 owns the key term (§7).
- **EB12. A budget-stolen voice's note** ends as any finished voice's does: `NOTE_END` when no
  spawning voice plays it. The Processor counts budget steals (`budgetVoiceSteals()`) for tests; the
  event log has no line for them (a voice steal is not a grain).
- **EB13. Release velocity "as a full source":** it exists (sub-project 1); what this sub-project
  adds is the envelope targets (E6), the note-on-0 value (E13) and a golden; the Mod view already
  lists it.

## 4. Design

### 4.1 Parameters (append-only IDs; the indices agreed across the parallel sub-projects)

Sub-project 2 takes globals from **0x25** (to 0x2F at most) and layer indices from **0x32** (to 0x37
at most); sub-project 3 takes globals from 0x30 and layer 0x38; sub-project 4 globals from 0x38.

| Slot | CLAP ID | Display name | Range, default | Kind |
|---|---|---|---|---|
| `MpeLowerZone` | `0x000025` | MPE Lower Zone | 0 to 15 member channels, 15 | MIDI setting (E15) |
| `MpeUpperZone` | `0x000026` | MPE Upper Zone | 0 to 15, 0 | MIDI setting |
| `MpeUpperBendRange` | `0x000027` | MPE Upper Bend Range | 1 to 96 semitones, 48 | MIDI setting |
| `UpperBendRange` | `0x000028` | Upper Bend Range | 0 to 24 semitones, 2 | MIDI setting |
| `Pan`, `B_Pan` … `D_Pan` | `0x010032` … `0x040032` | Pan (A Pan … D Pan) | −1 to 1 (shown −100% to +100%), 0 | layer, automatable, modulatable, a route and voice target |

- Pan is `X(Pan, 0x32, "Pan")` in a new list `GG_LAYER_PARAMS_M3_EXPRESSION`, after
  `GG_LAYER_PARAMS_M3` in both `GG_LAYER_PARAMS` and `GG_LAYER_PARAMS_SINCE_M1` (sub-project 3's list
  for 0x38 follows it). `kNextFreeLayerIndex` becomes 0x33 and `kNextFreeGlobalIndex` 0x29 on this
  branch; a merge keeps the larger of each branch's (its test pins `max + 1`). 0x33 to 0x37 and 0x29
  to 0x2F stay unused. The four globals are appended after `BendRange`.
- 298 slots (290 + 4 globals + 4 Pans; `kLayerParamCount` 64), before the parallel sub-projects'.
  Every hard-coded count follows (`tests/support/ParamTable.h`, `patch_tests`, `identity_tests`,
  `params_tests`, `rt_tests`, `EditLayout` and its tests, the sweep floors with their reasons).
- **No patch-version bump:** a state without the new values reads their defaults, and the defaults
  are sub-project 1's behavior. No new state chunk; per-note modulation and the RPN selection are
  never saved.
- Mutate keys by CLAP ID (sub-project 1's Task 1), so no press moves; Pan joins the default locks.
- No remote-controls page changes (sub-project 5's, if any); the factory routes are unchanged.
- The plugin numbers every parameter, note and control event it hands the Processor (E8): 1, 2, 3 …
  in the host's order within a call (the 0-frame calls of `applyBetweenBlocks` too); the editor's
  queued changes keep 0, ahead of the host's at a tie. Per-note modulations count toward
  `kMaxParamEvents` as monophonic ones do.

### 4.2 MPE zones (`core/include/gg/core/MpeZones.h`, the note table, the Processor)

- **`MpeLayout`** `{bool on; uint8_t lower, upper;}`, from the effective MIDI settings with E2's
  lower-first resolution, and **`zoneOf(channel)`**: Conventional, LowerMaster, LowerMember,
  UpperMaster or UpperMember (lower: master 0, members 1 to L; upper: master 15, members 15 − U to
  14). With MPE Off, or both counts 0, every channel is Conventional.
- **The note table** takes the layout in place of sub-project 1's `bool mpe` (`setLayout`): the
  pedal (`pedalDown(ch)`: the channel's own, or its zone's master's), and which channels' pressure,
  CC 74 and CC 11 are copied into notes (not a master's).
- **The bend** (`bendSemitones`): a member's `exactBend(own) × its zone's member range +
  exactBend(its master's) × its master's range`; a master's or a conventional channel's `exactBend(own)
  × its range` (a master's: Bend Range or Upper Bend Range; conventional: Bend Range). B7's exact
  center holds. With the defaults this is sub-project 1's formula exactly.
- **The parser:** per channel the selected RPN (MSB, LSB; null at start). A `ControlKind::RpnCc`
  event (key = the controller: 6, 38, 98, 99, 100 or 101; value = the data byte, 0 to 127) moves the
  selection or, on CC 6 with RPN 0 or 6 selected, completes a message, whose settings (E2) join the
  group's pending base values.

### 4.3 Expressions (the note table, the Processor, the plugin)

- `ControlKind` appends `Expression` (CC 11 / 127, its channel's), `RpnCc` (§4.2), `NotePan`,
  `NoteVibrato` and `NoteExpression` (0 to 1, clamped). The Note kinds with note ID −1 and key −1 are
  now channel-wide (E5) rather than ignored; channel −1 means every channel.
- A note gains `pan` (−1 to 1), `vibrato`, `expression` and the `had*` flags E5's "own once it has
  had one" needs; a channel gains `tuning`, `volume`, `pan`, `vibrato`, `expression`.
- A voice's values: pan `= own pan + channel pan` (into its CloudParams' pan with its layer's Pan
  and its routes), gain `× clamp(own volume × channel volume, 0, 4)`, tuning `= own + channel`,
  Vibrato and Expression its own or its channel's.
- The plugin: `isControlEvent` admits CC 6, 11, 38 and 98 to 101 and every CLAP note expression;
  `toControlEvent` maps them (through `controlFromMidiCc`); `clap_plugin_as_vst3`'s
  `supportedNoteExpressions` gains VIBRATO and EXPRESSION (PRESSURE stays out, row 409). Each counts
  toward `kMaxControlEvents`. A MIDI note-on at velocity 0 carries release velocity 64/127 (E13).

### 4.4 The matrix per voice (`ModMatrix.h`, `Processor::updateVoices`, `stepVoices`)

- `ModSource` appends **`Vibrato`** ("Vibrato") and **`Expression`** ("Expression"), both per voice
  (`isPerVoiceSource`), neither time-varying.
- `isVoiceTarget` adds `Pan`, `BusSplit` and the eight envelope controls. A new
  `isVoiceOnlyTarget(target)` is true for the eight; `isValidTarget` admits them (the other modulator
  controls stay refused); `applyRoutes` skips a route to one on every layer; `actsPerVoice` admits a
  route to one only from a per-voice source other than Env 1 and Env 2.
- Per voice: the voice's slots are `clamp(base + mod + layer offsets + its route offsets + its note's
  per-note modulation)` for every voice target; its Env 1 and Env 2 parameters come from its own
  envelope slots (`envParams` of the voice's values), used for its stepping, its `AmpEnvelope` and
  B8's sustain test. The voice cache's key gains the envelope slots, Pan and Bus Split and the note's
  per-note values (EB5).
- Every other target keeps B4's instance-wide rule.

### 4.5 Per-note modulation (`Processor.h`'s `ParamEvent`, the note table, the plugin)

- `ParamEvent` gains `int32_t noteId{-1}; int16_t channel{-1}, key{-1}; uint32_t seq{0};`. A
  modulation with any of the three given is per note (E7); a value event with them is applied as
  today (no host sends one: no `_AUTOMATABLE_PER_*` flag is set).
- The per-note slots, per layer (23): Density, Size, Position, Pitch, Spray, Stereo, Distribution,
  Texture, Smear, Quality, Pitch Spread, Size Spread, Pan, Bus Split, Env 1 and Env 2 Attack, Decay,
  Sustain and Release, and the layer's Level (Level, Level B to D). These get the three flags; nothing
  else does.
- The plugin passes `CLAP_EVENT_PARAM_MOD`'s note ID, channel and key into the event (today it drops
  them and applies every modulation as monophonic).

### 4.6 The ladder in Voice mode, and the steal by budget (`GrainPool`, `Engine`, the Processor)

- E9's order at the third rung, EB6's per-slot weights and cursors, EB7's weak flag. The room test
  for a candidate is today's (`(W − W_fading) − w(old) + w(t) ≤ Q` for some `t ≤ q`).
- `Engine::soundingWeightAt(b)` (the ledger at b; real-time safe, with `soundingWeight`'s monotonic
  requirement, called between pieces) and the quota the next piece gets let the Processor test E10 at
  `commitVoices`.
- `GrainStealSink` gains the stolen grain's voice ordinal (`kNoVoice` for a Cloud grain) for E14's
  line.
- V7's `1 / sqrt(n)` is unchanged; a budget steal lowers n at its boundary like any finish.

### 4.7 Determinism, real-time safety, the at-rest rule

- Every new input joins the control grid; the MCM's settings, the per-note modulation, the budget
  steal and the layout apply at boundaries; the steal order depends on the schedule alone (the
  ledgers, the weak flags set at boundaries, the ordinals), never on block size. A block-size test
  covers each (1, 32, 64, 4096).
- Fixed arrays only: the RPN state (16 channels), the setting-change queue (32), the per-note values
  (EB5), the per-slot weights and cursors (17 per pool). No new loop is unbounded; the tripwire
  (`rt`) covers `process` and `params.flush` with RPN, per-note modulation, channel-wide expressions
  and Voice-mode steals.
- **At rest:** with no RPN, CC 11, new expression, per-note modulation, route to a new target or
  source, Pan 0 and the new MIDI settings at their defaults, every existing golden keeps its WAV
  SHA-256, sub-project 1's Voice goldens included: their event logs hold no steal line and no lowered
  tier (the builder confirms), so neither E9 nor E10 runs in them; E8's fallback order is today's.
  The at-rest check (sub-project 1's `check-wav-hashes.sh`, against this branch's starting
  baseline) runs after every task.

### 4.8 `gg-render`

- `controls` gains `{"time", "kind": "cc", "channel", "controller", "value"}` (channel 0 to 15,
  value 0 to 127, both integers), mapped by `controlFromMidiCc`. It takes the controllers the plugin
  converts, 1, 6, 11, 38, 64, 74 and 98 to 101; any other is refused (exit 2, naming it). CC 1, 64
  and 74 keep their own kinds too. And `{"time", "kind": "expression",
  "expression", "channel", "key", "value"}`: a note expression with note ID −1 (`key` optional,
  −1 by default: channel-wide).
- A note's `expressions` kinds gain `pan`, `vibrato` and `expression`; a note gains optional
  `"paramMods": [{"time", "param", "value"}]`, sent as per-note modulation to the note's ID (its
  `id`, required for it: exit 2 without one), `param` a per-note slot's name ("A Size", "Level B"),
  inside the note's span. Every other parameter is refused (exit 2).
- `params` takes the four new globals and "A Pan" to "D Pan"; `mod` takes the sources "Vibrato" and
  "Expression" and the new targets by name ("Env 1 Attack", "Pan", …).
- Every event it builds has sequence number 0 (E8's fallback).

### 4.9 Goldens (new; each catches the 16 injected steps or joins the table with its reason)

- `inst-mpe-zones`: the MIDI path: an MCM for a lower zone of 7 and an upper zone of 7, RPN 0
  setting the lower members to ±24 and the upper to ±12, three notes in each zone with member
  bends of whole semitones, each master's bend, CC 74 and pressure on the members, CC 11 through an
  Expression → Size route.
- `inst-mpe-zones-expr`: the same gestures as CLAP note expressions (tuning in semitones, pressure,
  brightness, expression) with the MIDI settings at their defaults; a test,
  `mpe zones and note expressions render the same voices`, requires its event lines equal to
  `inst-mpe-zones`' over the spans where both mean the same gesture (B7's exact bends).
- The CLAP twin leaves the MIDI settings at their defaults (note expressions don't read them) and
  sends each note's tuning as the semitones the MIDI twin's member and master bends make together.
- `inst-voice-pan`: two voices panned apart by CLAP PAN, Stereo 0, and a Key → Pan route on a third.
- `inst-voice-env-targets`: Velocity → Env 1 Attack and Release Velocity → Env 1 Release, four notes
  at different velocities and release velocities.
- `inst-voice-budget`: Budget 256, layer A Voice at HQ and Density high, a pedaled chord, then new
  notes: grain steals in E9's order (steal lines with `"v"`) and E10's budget steals.
- `inst-voice-permod`: three notes, per-note modulation of A Size and A Pitch on two of them.

### 4.10 Tests

- Core, `tests/core/mpe_tests.cpp` (`[core][mpe]`): each MCM and RPN rule of E2 (both zones, the
  shrink, the resets, the master and conventional ranges, MPE Off, the NRPN and null deselection,
  CC 38, an MCM on channel 2 ignored), the write-through queue, the layout at its boundary, lower
  first on load, the per-zone pedal and bend (exact), masters' values not copied, the defaults equal
  sub-project 1's mapping on its MPE sequences.
- Core voices (`tests/core/voice_processor_tests.cpp`): pan (expression, Pan, routes, the clamp),
  Vibrato and Expression (CC 11), each channel-wide kind and a later note inheriting it, the envelope
  targets (attack timing, release length, Env 1 → Env 1 Release doing nothing, a Cloud layer
  unmoved), Bus Split per voice, per-note modulation (by ID, key, channel; unknown ID; a Cloud layer
  ignored; sent at the note-on's sample; replaced; gone with its note), the tie order (a pedal-down
  numbered before a note-off at one sample sustains; with 0s, today's order), the budget steal.
- Engine (`tests/engine/voice_tests.cpp`): the per-slot weights sum to the ledger; E9's order (an
  orphan first, the heaviest weak, the heaviest held, the drawer itself, D4 per candidate); one
  voice's steals equal today's rule's; block-size invariance with steals; the steal line's `"v"`.
- Plugin (`tests/plugin/voice_plugin_tests.cpp`): `voice-info`; the per-note flags in param info
  (exactly the 92 slots); a per-note `PARAM_MOD` reaching one voice; channel-wide expressions; the
  as_vst3 bits; `NOTE_END`'s frame; the MIDI settings written through (out events, `values_`, the
  saved state, a preset load keeping them, the edited mark untouched); note-on 0's release velocity;
  the tripwire with all of it.
- UI: `RouteEdits`' `setVoiceOnly` and undo, `routeCanAct` (true when the route is valid and either
  acts layer-wide (not Voice only, not to an envelope target) or can act per voice (a per-voice
  source to a voice target, Env 1 and Env 2 excluded for an envelope target)), the toggle reachable by keyboard and
  inside its view at 960 × 600 and 640 × 360 in both themes, no label elided (DejaVu, as row 405
  taught), the dimmed marker.
- `gg-render`: each key and refusal of §4.8. Click injection for the six goldens. `gg-bench`'s
  `engine-voices-overload` runs and reports. `scripts/check.sh` (dev, asan, rtsan; CI's RTSan where
  the machine has none, row 420), the validators (clap-validator reads `voice-info`; pluginval;
  auval), and the at-rest hashes in the final report.
- **The host checks** (the exit; the director's, informal): Bitwig CLAP (per-note pan and
  modulation, `voice-info`), Bitwig VST3, Live VST3 (MPE with an upper zone), Live AU (the channel
  bend through E5); a checklist in `docs/listening/m3-expression/README.md` with the six goldens'
  WAVs as A/B files.

## 5. INTENT changes (Part II, each with a §23 row numbered at merge)

- **§14:** the MPE zones (MCM, RPN 0, the write-through, the per-zone master); the expressions' map
  and channel-wide expressions; Pan; the voice targets and the envelope targets per voice; Vibrato and
  Expression; per-note modulation; the tie order; Voice mode's steal order (E9, marked as applying
  D4 per voice, #81) and the steal by budget; the "per-voice quotas" bullet rewritten as E9 states.
- **§15:** `voice-info`; per-note modulation (CLAP only, the wrappers' note ID −1); as_vst3's two new
  expressions; RPN through the VST3 wrapper's parameter mapping (EB4); the AU's channel-wide bend
  as ±2 (E5).
- **§17:** the Mod view's Voice toggle and marker; the new sources and targets in its menus; the MIDI
  group's four new settings and each layer's Pan.
- **§18:** the four MIDI settings kept by a preset load; the CLAP features list (`voice-info` and the
  per-note flags shipped); no state format change.
- **§23:** one row for this sub-project with E1 to E15 and EB1 to EB13, Claude's provisional calls.
- **Carry-over** (`docs/m3-carry-over.md`): rows 407, 412, 415, 416 and 417 closed by their commits;
  new rows for EB4 (RPN through VST3), per-note modulation absent from VST3 and AU (P5's record), the
  AU's channel bend fixed at ±2 (E5), and CC 7 and CC 10 declined (E4, a record).

## 6. Left to the builder

- The exact MPE specification text for E2's resets and shrink (the builder reads RP-053 and records
  any difference as a ruling before coding it).
- The goldens' exact gestures within §4.9, chosen so each catches the 16 injected steps or is pinned
  with a reason; every spectrogram looked at before committing; the MIDI and expression twins' equal
  spans named in the test.
- The Edit view's placement of the four settings and the Pans, and short labels if the frame tests'
  elision check needs them (as the strip's "Osc"); the host always sees the full names.
- `engine-voices-overload`'s idle figure, and the sweep floors' adjustments with their reasons.

## 7. Seams with the parallel sub-projects

- **Sub-project 3 (pitch-sync):** layer 0x38 and globals 0x30 up; both branches add a list after
  `GG_LAYER_PARAMS_M3` (this one's 0x32 first) and edit the counts and next-free indices: the merge
  keeps both lists in index order and the larger next-free index. Both touch `updateVoices` (this
  one the per-voice slots, Pan and the envelopes; that one the clock fields): the voice cache key
  takes both. E9 changes which grain a Voice-mode steal takes, so a sub-project 3 golden that steals
  grains in Voice mode (its budget-at-high-notes case) moves at whichever merge comes second, which
  regenerates it with that reason; its S10 ("no new budget rule") reads E9 as the rule.
- **Sub-project 4 (tuning):** owns the key term of `noteSemitones` (sub-project 3's seam); this one
  owns the bend term (per zone) and the channel tuning (EB11). Both edit Note Pitch's formula's
  inputs, not its shape.
- **Sub-project 5 (views and presets):** the Mod view's toggle and marker are this one's; the Edit
  view's placements of the new parameters are this one's, which sub-project 5 may move; its MPE
  presets can rely on the zones being announced or set, and on Vibrato and Expression as sources.
- **All:** `ModSource` appends (saved by name: the merge appends after whichever landed first);
  `kParamCount` and the tables merge by addition; §23 rows and CLAUDE.md's counts (labels `mpe`, the
  six goldens) are settled at merge.
