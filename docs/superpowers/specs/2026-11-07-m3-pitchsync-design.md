# M3 sub-project 3: pitch-sync and formant grains

| | |
|---|---|
| **Status** | Draft, 2026-11-07, written without the director at his request ("keep building all of the sub-projects as parallel as possible and merging them as able to. progress as much as you can without input"). Every forced choice is **Claude's provisional call** (S1 to S14, §3), each with the alternatives declined; the builder check's answers are in §3a. The director reviews the calls later; **S3 and S4 are the ones most likely to be overruled** |
| **Program** | `docs/superpowers/specs/2026-11-07-m3-program-design.md` (§6, §8 row 3, §10's open items for 3; its §2a P1 to P8) |
| **Builds on** | sub-project 1, `docs/superpowers/specs/2026-11-07-m3-voice-design.md` (V1 to V7, B1 to B10) and its plan, on `m3-voice` (draft PR #37) |
| **Branch** | `m3-sp3-pitchsync` (from `m3-voice`), worktree `~/src/gg-m3-sp3-pitchsync` |
| **Runs beside** | sub-projects 2 (expression), 4 (tuning) and 5 (views and presets), in parallel (§7 lists the seams) |

## 1. Intent

A layer at **Mode Voice** gains a **Clock**: **Free**, sub-project 1's voices, or **Pitch**. At Pitch
each voice spawns grains at its note's frequency, so the grain rate is the note heard (pulsar and
formant synthesis: Roads' pulsar trains, Rodet's FOF, PSOLA's pitch-synchronous overlap-add). The
layer's Pitch then sets the read rate inside each grain, the **formant**, which stays put as the
note moves; Size sets a grain's length in periods; Distribution roughens the pulse. Over a sample
it gives vocal, formant-like tones. In the FX, over the live input, a played note becomes a pitched
resonance of what comes in: the MIDI pitch-sync §23 #25 and #32 promised the FX.

The sound is pitched and clean: at Distribution 0 the output is periodic at the note's frequency to
within the measurement's resolution (§4.10), and no aliasing beyond what the HQ and Normal tiers
already accept. At rest (Clock Free, the default) nothing moves: every existing golden keeps its
WAV SHA-256, sub-project 1's eight Voice goldens included.

## 2. Scope

**In:** the Clock parameter (per layer); the Pitch clock in the scheduler (onsets at the note's
period, the jitter law, the length in periods, the refresh); the held read anchor on the ring; the
Processor's mapping (the note's frequency through the tuning seam, Pitch as the formant, Density as
the refresh rate); the Edit view's placement of Clock (the frame tests require one); `gg-render`'s
`"A Clock"` to `"D Clock"`; seven goldens; the pitch, formant and aliasing tests; three sweep cases
for the budget at high notes; a report-only `gg-bench` case; INTENT §14's text on pitch-sync.

**Out:** Clock on Cloud layers (S1); a Formant Track amount (S2, later); the Size control's period
display in the editor and the Clock control's final look (sub-project 5, the views); pitch-sync
factory presets (sub-project 5); the tuning itself (sub-project 4: this sub-project reads the
voice's note semitones through one seam, §4.3); per-voice quotas (sub-project 2); synthetic pulsar
and FOF grains over oscillators (M5's synthetic grains, INTENT §14).

## 3. Forced choices (Claude's provisional calls; the director may overrule each)

- **S1. Clock is a per-layer parameter read only at Mode Voice.** Stepped, 0 **Free** (default), 1
  **Pitch**, in each layer scope at index **0x38**. A layer at Mode Cloud ignores it: its stream
  keeps today's Density clock bit for bit, whatever Clock holds. *Declined:* (a) a Cloud layer at
  Clock Pitch following the newest held note's frequency (a mono MIDI pitch-sync): Voice already
  gives the FX polyphonic pitch-sync (V2: notes don't freeze an FX Voice layer, so voices read the
  live input), and a second, mono path would add a branch to Cloud's hot path and to the at-rest
  rule for nothing Voice can't do; (b) Pitch-sync as a third Mode value (INTENT §14 lists it as a
  scheduler mode): the program design's §6, approved, makes it a clock inside Voice, and M4 appends
  Sync to Mode, not to Clock. Clock's values are append-only, like Mode's.
- **S2. The note sets the grain rate; Pitch sets the read rate alone.** A voice's grain rate is its
  note's frequency, `f0 = 261.6255653005986 × exp2Det(noteSemitones / 12)` Hz, where
  `noteSemitones` is the voice's note in semitones above key 60's 12-TET pitch, its bend and its
  CLAP tuning expression included (today `(key − 60) + bend + tuning`; sub-project 4 makes the key
  term the tuning's, §4.3), clamped to **[1 Hz, sampleRate / 4]**. The grains' read rate is
  `exp2Det(clamp(layer Pitch + the voice's Pitch route offsets, −48, 48) / 12)`: the key, the bend
  and the tuning never enter it, so the formant holds as the note moves, as a voice's formants do.
  Pitch Spread and Smear's detune scatter the read rate, as today. *Declined:* a **Formant Track**
  amount per layer (the formant following the key by 0 to 100%, as vocal synthesizers offer): a
  route can't reach full tracking (the Key source spans 0 to 1 over 127 keys, against Pitch's
  ±48), so it is a real gap, but not one §6 asked for; a later sub-project can add it at layer
  index 0x39, which this one leaves free. The ceiling sr/4 keeps at least four samples a period and
  bounds the spawn rate (§4.8); key 127 (12.5 kHz) reaches it at 44.1 and 48 kHz.
- **S3. Size is read in periods: N = Size × 40/3 Hz, so 1/15 to 26⅔ periods, 2 by default.** At
  Clock Pitch a grain is N periods of the f0 at its onset: `durationSamples = N × sampleRate / f0`,
  clamped to [16 samples, 2 s] (the window's minimum and Size's own top). The law is linear in
  Size's seconds, so an octave of Size (a route, a macro, Size Spread) is an octave of N in both
  clocks, and Size's default, 150 ms, is 2 periods: PSOLA's overlap, whose Hann windows sum flat.
  Below 1 the grains leave gaps (pulsar trains, bright and buzzy, the narrowest 1/15 of a period);
  above 2 the harmonics sharpen into resonances (at 26⅔, the Hann's main lobe makes each about f0/7 wide). **The host
  shows Size in ms** (a parameter's text is its own value: hosts cache it, and the wrappers'
  support for a text rescan is unverified); the Edit view shows periods at Clock Pitch, sub-project
  5's, from a helper this sub-project adds (`sizePeriods`, §4.4). *Declined:* (a) a new Periods
  parameter (a clean display and its own default, but Macro Size, Velocity → Size (factory route
  48) and every Size route would do nothing at Clock Pitch, and §6 says Size); (b) mapping Size's
  whole travel onto a rounder range such as 1/4 to 16 periods (its default then lands at 2.65, and
  an octave of Size stops being an octave of N); (c) host text that follows Clock.
- **S4. At Clock Pitch, Density is the refresh rate, and a refresh holds every random draw.** A
  voice's grains between two refreshes repeat one grain shape: every per-grain random draw but the
  onset's (the position's spray, Smear's jitter and detune, the pitch and size spreads, the pan and
  the bus) is keyed by the last refresh grain's index, not the grain's own, and on the ring
  (Live, Resample, Spectral) the read anchor, the ring time the refresh grain started at, is held
  too (§4.6). A grain is a refresh when it is the voice's first since its start, since Clock turned
  to Pitch or since its anchor was dropped, or when its onset is at least `sampleRate / exp2Det(Density)`
  samples after the last refresh's onset; Density's routes apply (Pressure → Density, factory route
  44, speeds the refresh as the player presses in). So Density keeps its Hz and its meaning, how
  often new material enters: 0.25 Hz is near frozen, the default 16 Hz refreshes every 62.5 ms
  (about 14 identical pulses at A3), and 400 Hz refreshes every 2.5 ms, which over a ring is
  nearly the input itself, amplitude-modulated at f0. *Why:* identical successive grains are what
  make a pulse train pitched (its lag-T0 autocorrelation is the source's correlation between
  successive grains' start positions, so it is full only when they read the same material). With
  per-grain draws, INIT's Spray (50 ms) and Stereo (50%) would scatter every period's grain and
  blur the pitch, and over a live ring, where Position is a lag behind a moving head, successive
  grains would read material one period apart: at the input's own rate the overlapping grains then
  read the same frame and sum to the input, unpitched. *Declined:* (a) per-grain draws (above); (b)
  a new Refresh parameter (Density would be dead at Clock Pitch, and the factory route 44 with it);
  (c) holding the anchor on a Sample layer too: a sample's Position is a place, so a Sample layer's
  grains follow Position per grain (Timbre → Position, factory route 46, scans smoothly) and hold
  only the random part.
- **S5. Distribution jitters the period: Δ(k) = P × (1 + 0.5 × d³ × bipolar(u(k))).** P =
  sampleRate / f0, d the Distribution, u(k) the onset draw Cloud uses (field Onset, keyed by the
  grain's own index). The mean interval stays P at every d, so the pitch's centre holds; the
  spread is uniform within ±50% d³: at d = 0.25 about ±0.8% (a voice's natural jitter), at 0.5
  ±6.25% (hoarse), at 1 ±50% (rough, the pitch nearly gone). The cube keeps the lower half of the
  travel clean. INIT's Distribution is 0.5, so INIT switched to Clock Pitch sounds hoarse until
  Distribution comes down; sub-project 5's pitch-sync presets set it low. *Declined:* Cloud's law
  (periodic mixed with Poisson), which at d = 0.5 already loses the pitch; a linear or squared law
  (hoarse from a quarter of the travel).
- **S6. Smear at Clock Pitch:** its density term (raising Density toward the overlap target) is
  skipped, since Density is the refresh rate there; its position jitter and detune apply, held per
  refresh (S4). *Declined:* Smear's density term on the refresh rate (it would tie the refresh to
  Size, which means nothing here).
- **S7. The gain normalization is `1 / sqrt(max(1, N))`,** the nominal overlap, latched at the onset
  like Cloud's `1 / sqrt(max(1, Density × Size))`, so 2 periods sit near the Free clock's level at
  its defaults (2.4 overlapping grains). The clamps (16 samples at the top notes, 2 s at the lowest)
  change the real overlap a little and the gain not at all: at f0 = 12 kHz with N = 1/15 a grain
  is 16 samples, an overlap of 4, about 6 dB louder than the nominal; accepted and documented.
  Coherent material (a periodic sample read in phase) can sum up to 3 dB above the power law at
  N = 2; V7's headroom and the limiter are there for it.
- **S8. The mip level stays the read rate's (§10's open item).** A grain latches its level from its
  read rate (`mipLevelFor`, HQ only), and at Clock Pitch that rate is the formant's, so **a note's
  frequency never selects a level**: a high note shortens the grains and packs them closer, it
  doesn't raise their rate. Pitch +12 reads level 1 at HQ at every note, as today; Normal and Eco
  read level 0, as today. The pulse train adds no aliasing of its own: onsets keep their fraction
  (`onsetFrac`), so each grain is the sampled, smooth (Hann) windowing of band-limited material,
  and its spectrum above Nyquist is the window's sidelobes times the tier's. The aliasing test
  (§4.10) pins it. *Declined:* choosing the level from f0 or from the grain's length (a short
  window spreads the spectrum by about sampleRate / length either way, which no level removes).
- **S9. A Clock change on a sounding voice forces an onset at its boundary and a refresh.** The
  pending onset was drawn under the other clock (at Density 0.25 Hz it could be 4 s away), so the
  voice's next grain sounds at the change's 32-sample boundary, as a note-on's does, and nothing
  is rescaled from before it. Automatable and stepped, like Mode.
- **S10. No new budget rule; the pool stays 4,096 a layer (§10's other item).** At Clock Pitch a
  voice sounds about N grains at any note (the length is N periods, the spacing one), at most
  max(N, 16 × f0 / sampleRate), so 26⅔ at the longest Size and 4 at the top note: 16 voices sound
  at most 427 grains a layer, well under the pool. What a high note raises is the spawn rate
  (f0 × voices: 192,000 grains a second a layer at 12 kHz), an admission cost the weights don't
  count and the benchmark measures (§4.8). The ladder governs the rest unchanged: 16 HQ voices at
  26⅔ periods weigh 11,520 units against the 4,096 a layer gets when four layers render at the
  default Budget, so it lowers them to Normal,
  deterministically, without a click.
- **S11. The grain-event log's format doesn't change.** A pitch-sync grain's line is a voice line
  (`"v"`, sub-project 1): its onsets show f0, its `rate` the formant, its `lvl` the level, and a
  held anchor shows as repeated `pos`. A refresh flag in the log was declined: nothing compares it
  that the positions don't already show.
- **S12. Clock is the sound's, not the instance's:** a preset carries it, a preset load sets it,
  Mutate locks it (structure, as Mode and Source), and no route targets it (`isValidTarget`, as
  Mode).
- **S13. No patch-version bump.** A state or preset without Clock reads Free, its default, by CLAP
  ID, as sub-project 1's Mode did.
- **S14. "Pitched" is measured to ±2 cents.** At Distribution 0 the pulse lattice is exact by
  construction, so the tolerance is the estimator's (§4.10), not the sound's; at Distribution 0.25
  the mean f0 is within ±10 cents.

## 3a. The builder check (questions a builder would ask, answered; binding on the plan)

- **SB1. Which layers does Clock Pitch reach?** Voice layers alone (S1); a layer switched to Cloud
  ignores it at once, and a Voice layer at Clock Free is sub-project 1's, bit for bit (a test pins
  it against a voice with no Clock code path).
- **SB2. Where does the scheduler learn the clock?** `CloudParams` gains `clock` (`Clock::Free`, the
  default, or `Clock::Pitch`) and `refreshHz` (0 at Free). At Pitch, `densityHz` carries f0,
  `grainDurationSec` stays Size's seconds (the scheduler multiplies by 40/3 and the period), and
  `pitchSemitones` is the formant's. The Cloud stream is always given Free (the Processor's
  `toCloudParams` sets nothing new), and `setParams` at Free takes today's path exactly, with no new
  branch inside `takeNext` that Free can reach but the test of `clock`.
- **SB3. A slide.** f0 changes at each cell the bend or tuning moves; the period change raises
  ruling M1-h's rescale, so the pending onset moves to `max(t(k−1) + Δ_new(k−1), g)` with the same
  draw: the pulse follows the slide cell by cell (32 samples), and each grain's length is N periods
  of the f0 at its onset (V3's per-grain latch). The rescale's draw is the Pitch law's (S5), kept as
  `lastE_` holds Cloud's today.
- **SB4. The refresh's state** lives in the voice's scheduler: the last refresh's index and onset,
  and whether one is valid (`reset`, a Clock change, `startVoice` and a dropped anchor invalidate
  it). Its decision uses the refresh rate in effect at the grain's emission. The onset draw stays
  the grain's own (so the jitter is per pulse); every other field takes the refresh's index.
- **SB5. Spray on a Sample layer** at Clock Pitch is drawn per refresh, around Position per grain
  (`sprayInSeconds`, as today), so a refresh picks a new snippet near Position and the pulses
  between repeat it.
- **SB6. Envelopes and the release:** unchanged. Env 1 is the voice's amplitude (V1), sampled at
  each grain's peak; the voice spawns pulses through its release (row 404), so a released note
  decays as a pitched tone. V7's `1 / sqrt(n)` headroom applies.
- **SB7. The FX:** an FX Voice layer isn't frozen by notes (V2), so voices read the live ring; with
  the layer's Freeze on, the ring stops and every anchor sits in the frozen material, which gives a
  steady tone from it. Mix stays the instance's.
- **SB8. A Spectral layer** at Clock Pitch reads the smear's ring like a Live layer (the anchor
  held, §4.6); the smear's own analysis follows the layer's base Position, as sub-project 1
  documented.
- **SB9. Per-voice change detection:** Clock joins the slots `updateVoices`' generation key reads
  (`kVoiceKeySlots` + 1), so a change recomputes each voice's `CloudParams` once.
- **SB10. The UI's grain queue** is lossy by design: at high notes it drops grains and counts them
  (`dropped()`), so the canvas draws a subset. Sub-project 5 decides whether the canvas decimates
  pitch-sync voices; nothing here changes the queue.
- **SB11. Floating point:** every new expression runs under `GG_FP_CONTRACT_OFF`; f0 and the read
  rate come from `exp2Det`, never libm, since both reach the event log; d³ is `d * d * d`, and
  `P * (1.0 + 0.5 * d3 * b)` is evaluated in that order.
- **SB12. The tail:** unchanged; a pitch-sync grain is at most 2 s, inside the voice tail sub-project
  1 sized.

## 4. Design

### 4.1 Parameters (append-only IDs)

- **Clock**, per layer: `X(Clock, 0x38, "Clock")` in a new list `GG_LAYER_PARAMS_M3_PITCHSYNC`,
  placed after `GG_LAYER_PARAMS_M3` (and after sub-project 2's list for 0x32 to 0x37, whichever
  merges first) and before `GG_LAYER_PARAMS_M2_SOURCES`, in both `GG_LAYER_PARAMS` and
  `GG_LAYER_PARAMS_SINCE_M1`, so the index column stays strictly ascending. CLAP IDs **0x010038,
  0x020038, 0x030038, 0x040038**. Stepped 0..1, default 0, choices "Free" and "Pitch"; flags as
  Mode's (`kAutomatableModulatable | CLAP_PARAM_IS_STEPPED`); not a route target.
- **The index allocation across the parallel sub-projects** (the program manager's): sub-project 2
  takes globals from 0x25 and layer indices 0x32 to 0x37; **this one takes layer indices 0x38 to
  0x3B and globals 0x30 to 0x37, and uses only layer index 0x38** (no global; 0x39 is left for a
  Formant Track, S2); sub-project 4 takes globals from 0x38 and, if it needs any, layer indices from
  0x3C. Unused indices stay unused.
- `kLayerParamCount` 63 → 64 and `kParamCount` 290 → 294 on this branch alone (each parallel
  branch adds its own; the merge sums them). `kNextFreeLayerIndex` becomes one past the largest
  index below 0x40 (0x39 here; the patch test pins `max + 1`, so a merge takes the larger), and
  `kNextFreeGlobalIndex` is untouched. Every hard-coded count follows, as sub-project 1 listed
  (`tests/support/ParamTable.h`, `patch_tests`, `identity_tests`, `params_tests`, `rt_tests`,
  `EditLayout` and its tests). Mutate is keyed by CLAP ID since sub-project 1's Task 1, so no press
  moves; Clock is locked (S12).
- `enum class Clock : uint8_t { Free, Pitch }` and `Clock clockOf(float)` (≥ 0.5 is Pitch) in
  `ParamMap.h`, beside `LayerMode`.

### 4.2 The Pitch clock in the scheduler (`engine/include/gg/grain/CloudScheduler.h`)

At `clock == Pitch`, with P = sampleRate / f0 (f0 clamped to [1, sampleRate / 4] in
`updateDerived`):

- **Onsets:** `Δ(k) = P × (1 + 0.5 d³ × bipolar(u(k)))`, u keyed (seed, layer, stream, Onset, k)
  (S5). At least 0.5 P, so `kMinIntervalSamples` never binds. The forced onset at the voice's start
  and at a Clock change (S9) works as today's.
- **Length:** `durationSamples = clamp(S × 40/3 × P, 16, 2 × sampleRate)`, S = Size's seconds; with
  a size spread, S × 2^(spread × bipolar(u)) clamped to Size's range [5 ms, 2 s] first, as today,
  then scaled (S3).
- **Draws:** every field but Onset is keyed by `kRefresh`, the last refresh grain's index (S4);
  `GrainDraw` gains `bool held` (true for a non-refresh grain at Clock Pitch, false otherwise and
  always at Free), which the engine reads for the ring's anchor (§4.6).
- **Gain:** `GrainDraw::gain` is the params' gain, which the Processor sets to `1 / sqrt(max(1, N))`
  (S7).
- At `clock == Free` every line of today's `setParams`, `beginRun` and `takeNext` runs unchanged.

### 4.3 The note's frequency: the tuning seam

The voice's `pitchIn` (`Processor::updateVoices`, today `(key − 60) + bend + tuning`) is the one
value both clocks read: Free adds it to the read rate's semitones, Pitch turns it into f0. This
sub-project names it `noteSemitones(const NoteTable::Note &)` (a private Processor function) if
sub-project 4 hasn't already; sub-project 4 changes its body (the key term from the tuning: MTS-ESP,
then Scala, then 12-TET, as semitones above key 60's 12-TET pitch, 261.6255653005986 Hz), and both
clocks follow without a line of this sub-project's code changing. In 12-TET, key 69 gives
`261.6255653005986 × exp2Det(9 / 12)` = 440 Hz to within a few ulps (a test pins 1e-12 relative).

### 4.4 The Processor (`core/src/Processor.cpp`, `core/src/ParamMap.cpp`)

Per voice on a Voice layer at Clock Pitch, after `toCloudParams` and the voice's route offsets:
- `cloud.clock = Pitch`; `cloud.densityHz = f0` (S2); `cloud.pitchSemitones = clamp(layer Pitch +
  pitch offset, −48, 48)` (pitchIn left out);
- `cloud.refreshHz = exp2Det(Density + the voice's Density offset)`, Smear's density term left out
  (S6), clamped to Density's range;
- `cloud.gain = 1 / sqrt(max(1, S × 40/3))` (S7).
`ParamMap.h` adds `double sizePeriods(float sizeLog2Seconds)` (S × 40/3) for the views and the
tests, and `constexpr double kPeriodsPerSizeSecond = 40.0 / 3.0`.

### 4.5 Determinism and real-time safety

- Voices keep sub-project 1's key (stream 1 + ordinal); no new `rng::Field`. A held draw is keyed by
  the refresh grain's index, so it is the same at any block size and on any platform.
- The refresh and the jitter live in the onset domain inside the scheduler; f0 and Clock change
  only at cell boundaries (the 32-sample grid), so pulses, refreshes and anchors land on the same
  samples at any block size.
- New state: two doubles, a `uint64_t` and a flag per voice scheduler; a double and a slot byte per
  engine voice (§4.6). Nothing allocates; the tripwire and RTSan tests cover 16 pitch-sync voices.

### 4.6 The held anchor on the ring (`engine/src/Engine.cpp`)

Each engine voice slot keeps `anchor` (a ring-time start, double, as `GrainSpec::startPos` is) and
the slot it belongs to (kLiveSlot or kSpectralSlot). `liveGrain` for a voice grain:
- not `held` (a refresh): `startPos` as today, `clamp(ρ − positionDraw × sampleRate, lo, hi)`, and
  the unclamped `ρ − positionDraw × sampleRate` is stored as the anchor;
- `held`, with an anchor for this ring: `startPos = clamp(anchor, lo, hi)` at the grain's own onset.
  The anchor stays fixed in ring time while the head moves on, so the clamp's upper bound only
  recedes; its lower bound binds only when the hold outlives the ring's window (a 4 s refresh at
  Density 0.25 Hz against a 1 s ring), and then the grain reads from the oldest admissible frame,
  windowed, never a click.
- A held grain whose voice has no anchor for this ring (the source mode changed under it) is treated
  as a refresh. `startVoice`, `clearVoices`, `reset` and a source-mode change drop the anchors.
- Sample slots need no anchor: a held grain's draws already repeat (S4), and Position is read per
  grain (SB5). Cloud grains and Free voices never read or write an anchor.

### 4.7 The at-rest rule

With every layer at Clock Free, nothing reaches the new code: the Cloud stream is always Free, and a
Free voice takes sub-project 1's path. **Every existing golden's WAV SHA-256 is unchanged** (the 108
and sub-project 1's eight voice cases): `scripts/update-goldens.sh` before and after, compared, in
the final report. Their event logs are byte-identical too (S11).

### 4.8 The budget at high notes, and the benchmark

- **Sweep cases** (`tests/sweep/sweep_pitchsync_tests.cpp`, `[sweep][pitchsync]`, both products
  where they apply): (1) 16 voices at keys 100 to 115, Size 2 s (26⅔ periods), HQ, four layers at
  Voice and Clock Pitch, Budget 256: the ladder engages (lowered or dropped grains counted),
  finite, the peak bound, no runaway, the click score under the limit; (2) a 16-voice chord sliding
  four octaves up and back in 2 s (MPE bend at ±48); (3) Clock flipped every 100 ms under a held
  chord. The sweep's case count goes from 30 to 33; each pins its scored-share floor as the others
  do.
- **Engine tests at the top note:** f0 at sampleRate / 4 with N = 1/15 gives 16-sample grains four
  deep; 16 such voices admit 192,000 grains a second a layer with no pool overflow and the merge's
  order intact (non-decreasing onsets).
- **`gg-bench`'s `engine-pitchsync`** (report-only, no baseline entry, at blocks 32, 64 and 256):
  16 voices at Clock Pitch at C7 (2,093 Hz), N = 2, Normal, over `engine-sample`'s stereo noise
  sample: ns per output sample, beside X. It adds three configurations to the full run (about 30 s
  of timing). The builder reports it with the idle machine's figure.

### 4.9 `gg-render`

- `params` takes `"A Clock"` to `"D Clock"` (0 Free, 1 Pitch), from the macro tables; nothing else
  changes. A spec at Clock Pitch with the layer at Mode Cloud is valid (Clock is ignored there).

### 4.10 Goldens and the measuring tests

**Goldens** (in `tests/golden/cases/`; each 4 to 6 s at block 256, seed 1, notes low enough that the
event log stays under about 1 MB; each catches the 16 injected steps or joins the click-injection
table with its reason):
- `inst-pitchsync`: layer A Voice, Clock Pitch, over the generated `noise-2s.wav`, Distribution 0,
  Stereo 0: A2, A3, then the chord A2-C♯3-E3, Env 1 Release 0.3 s.
- `inst-pitchsync-formant`: one A2 over a 1 kHz sine sample at HQ, Pitch stepped −12, 0, +12, +24 at
  1 s intervals (levels 0, 0, 1, 2 in the log: S8 visible).
- `inst-pitchsync-periods`: one A2 over noise, Size stepped 18.75 ms, 37.5 ms, 150 ms, 600 ms, 2 s
  (¼, ½, 2, 8 and 26⅔ periods). The first step is not 5 ms (1/15 of a period, S3's floor): there
  the click metric scores the windowed bursts themselves (clean score 27.9, against the limit of
  15), so `pitchsync_smallest_size_is_windowed_bursts` covers that Size instead.
- `inst-pitchsync-jitter`: one A2, Distribution 0, 0.25, 0.5, 1.
- `inst-pitchsync-slide`: MPE On, a note on channel 2 bent +12 and back over 2 s, then a second note
  slid by a CLAP tuning expression (the rescale, SB3).
- `fx-pitchsync`: the FX, layer A Live at Voice and Clock Pitch, Mix 1, over stereo noise: three
  notes in turn, then a chord, with Density automated 1, 4 and 7 (refresh 2, 16 and 128 Hz; S4).
- `fx-pitchsync-sweep`: the FX over a 100 Hz to 2 kHz sweep, one held A2: the input's spectrum
  imprinted on the pulse, the resonance §23 #25 promised.

**Tests:**
- **Pitch** (`tests/metrics/pitchsync_tests.cpp`, `[metrics][pitchsync]`): an f0 estimator in the
  test support (YIN's cumulative mean normalized difference, its first minimum under 0.1, refined by
  parabolic interpolation; unit-tested on band-limited sawtooths to ±0.1 cent from 55 Hz to 4 kHz),
  over each steady span of `inst-pitchsync`, `inst-pitchsync-formant` and `fx-pitchsync` rendered in
  process: the note's 12-TET frequency to ±2 cents at Distribution 0 (S14), and at every formant
  step of `inst-pitchsync-formant` (Pitch moves the formant, never f0); `inst-pitchsync-jitter`'s
  d = 0.25 span to ±10 cents.
- **Formant:** in `inst-pitchsync-formant`, the strongest harmonic of f0 at each step is the one
  nearest 1 kHz × 2^(Pitch / 12), within one harmonic.
- **Aliasing** (S8): HQ and Normal, a 1 kHz sine sample, Pitch 0, f0 at key 107 (3,951 Hz, so the
  sample rate isn't a multiple of f0 and folded harmonics land between the real ones): the energy
  more than 20 Hz from every harmonic of f0, against the total, is below a floor the builder pins
  from the measured value with a margin (as `comb_tests.cpp` pins Smear's), and at most −60 dB.
- **Engine** (`tests/engine/pitchsync_tests.cpp`, `[engine][pitchsync]`): onsets at exactly kP at
  d = 0 when P is a whole number of samples; lengths N × P and both clamps; the jitter's bounds at
  d = 1; held draws equal within a refresh and new at the next; the refresh timing; the ring anchor
  held (equal `startPos` within a refresh) and dropped as §4.6 says; a Clock change's forced onset;
  a Free voice and the Cloud stream byte-identical to sub-project 1's.
- **Core** (`tests/core/pitchsync_tests.cpp`, `[core][pitchsync]`): f0 from the note (440 Hz at key
  69), the clamps, bend and tuning moving f0 and not the rate; Pitch moving the rate and not f0;
  Clock ignored on Cloud layers; Density as the refresh with no Smear term; the gain; the
  generation key's recompute on a Clock change.
- **Determinism** (`[determinism][pitchsync]`): pitch-sync voices with slides, refreshes, Clock
  changes and the ring anchor, at blocks 1, 7, 32, 256 and 4,096: identical audio and per-layer
  event lines.
- **rt:** the tripwire on `process` with 16 pitch-sync voices at a top note; RTSan in the `rtsan`
  build.
- `scripts/check.sh` (dev, asan, rtsan), the validators, and `gg-bench`'s headline ratios unchanged.

The new Catch2 tag `pitchsync` becomes a CTest label; CLAUDE.md's label list and its sweep count
(30 to 33) are updated with the build.

### 4.11 The Edit view

Clock is placed in each layer's Scheduler panel after Freeze (`edit(ParamSlot::Clock,
Panel::Scheduler, 3)`), so "every parameter has exactly one placement" and the frame tests hold at
960 × 600 and 640 × 360 in both themes. Its final look, the Size control's periods at Clock Pitch
(`sizePeriods`) and any hiding of Clock at Mode Cloud are sub-project 5's.

## 5. INTENT changes (Part II, each with a §23 row; the row numbers are taken at merge, in merge order, since four branches add rows in parallel)

- **§14, Scheduler:** Pitch-sync is a **Clock** of the Voice mode, not a fourth mode: "Modes: Cloud,
  Sync (M4), Voice; Voice's Clock is Free (Density) or Pitch (pulsar and formant: the note's
  frequency the grain rate, Pitch the formant, Size in periods, Density the refresh, Distribution
  the period's jitter)". The clocks line keeps "MIDI pitch-sync".
- **§14, the levels:** the mip level is the read rate's at either clock; a note's frequency never
  selects one (S8).
- **§14, the budget:** a pitch-sync voice sounds about N grains at any note; the pool stays 4,096 a
  layer (S10).
- **§17:** Clock in the Edit view's Scheduler panel (S1, §4.11).
- **§18:** Clock is the sound's, carried by a preset; no patch-version bump (S12, S13).
- **The §23 row:** S1 to S14, marked as Claude's provisional calls, dated 2026-11-07.

## 6. What's left to the builder

- The plan's task split (scheduler, engine anchor, Processor mapping and parameter, goldens and
  measuring tests, sweep and bench, records), as sub-project 1's plan split its own.
- The goldens' exact parameters within §4.10's descriptions, chosen so each catches the 16 injected
  steps or is pinned with a reason; every spectrogram looked at before committing.
- The aliasing floor's value (§4.10), from the measured render.
- `engine-pitchsync`'s idle figure, and the sweep cases' scored-share floors.

## 7. Seams with the parallel sub-projects

- **Sub-project 2 (expression):** takes layer indices 0x32 to 0x37 and globals from 0x25; both
  branches add a list after `GG_LAYER_PARAMS_M3` and edit `kLayerParamCount`, `kParamCount` and
  `kNextFreeLayerIndex`: the merge keeps both lists in index order and the larger next-free index.
  Both touch `Processor::updateVoices` (its per-voice targets; this one's clock fields and the
  generation key's Clock slot). Its per-voice quotas divide a layer's quota among voices: a
  pitch-sync voice weighs about N grains whatever its note (S10).
- **Sub-project 4 (tuning):** owns `noteSemitones`' body (§4.3); this sub-project reads it and adds
  nothing to the tuning. Its renders in a non-12 scale apply to Clock Pitch with no further code:
  an f0 test in its scale is a one-line addition there, once both have merged.
- **Sub-project 5 (views and presets):** the Clock control's look, Size in periods at Clock Pitch,
  whether Clock hides at Mode Cloud, the canvas at high spawn rates (SB10), and the pitch-sync
  factory presets (low Distribution, S5).
- **All:** §23 rows are numbered at merge (§5).
