# M3 sub-project 8: grain shapes (reverse grains and the window's skew)

| | |
|---|---|
| **Status** | Draft, 2026-11-07, written without the director at his request ("keep building all of the sub-projects as parallel as possible and merging them as able to. progress as much as you can without input"). Every forced choice is **Claude's provisional call** (G1 to G16, §3), each with the alternatives declined; the builder check's answers are in §3a (GB1 to GB17). The director reviews the calls later; **G2 (where a reversed grain starts), G7 (the window stays in output time) and G9 (Skew's law and its 1% floor) are the ones most likely to be overruled** |
| **Program** | `docs/superpowers/specs/2026-11-07-m3-program-design.md` (§3's goals, §8, §9); this sub-project is new (G0) |
| **Builds on** | sub-project 1, `docs/superpowers/specs/2026-11-07-m3-voice-design.md` (V1 to V7, B1 to B10) and its plan, on `m3-voice` (draft PR #37) |
| **Branch** | `m3-sp8-grainshape` (from `m3-voice`), worktree `~/src/gg-m3-sp8-grainshape` |
| **Runs beside** | sub-projects 2 (expression), 3 (pitch-sync), 3b (playhead), 4 (tuning), 5 (views and presets), 7 (tempo sync), 9 (voice filter), 10 (scenes) and 11 (color), in parallel (§7 lists the seams) |
| **Why** | The comparison with the hardware granular instruments (2026-11-07, the program's research notes `competitor-gaps.md`, gaps 7 and 10) found that they play grains backward with a set probability, and shape a grain's window from an early, percussive peak to a late swell. Ours read every grain forward, and Texture moves the peak only later (to 70%), never earlier. The director moved every gap the comparison found into M3 (2026-11-07: "no i think this all needs to happen in m3") |

## 1. Intent

Two per-layer controls over a grain's shape, both latched at each grain's onset:

- **Reverse**, a probability from 0 (every grain forward, today) to 100% (every grain backward). A
  reversed grain reads the same span of material as its forward twin, from the far end back.
- **Skew**, the window's peak moved from early (−100%: a fast attack and a long fall, a percussive
  grain) through symmetric (0, today's window exactly) to late (+100%: a slow swell and a fast
  cut), on top of whatever Texture has made of the window. The window's mean and energy don't
  change, so a skewed stream doesn't jump in level.

Both are route targets and per-voice targets: Velocity → Reverse makes the hard notes backward,
Velocity → Skew the hard notes percussive.

The capabilities are generic to granular instruments (a direction probability; an asymmetric
window). No product's names, labels or layout are copied.

At rest nothing moves: with Reverse 0 and Skew 0, the defaults, every layer draws and renders
exactly as today, and every golden (the 108 M2-era cases and sub-project 1's eight voice cases)
keeps its WAV SHA-256 and its event log byte for byte.

## 2. Scope

**In:**
- two layer parameters, Reverse and Skew (§4.1);
- the reverse draw in the scheduler, a new appended `rng::Field` (§4.2);
- reversed grains on every source kind: a Sample (the mirror, the start clamp, the edge at frame
  0), a Live or Resample ring (the reversed admission clamp: never crossing the head), and the
  Spectral smear's ring (§4.3);
- reversed grains in every kernel: the scalar references, the 4-wide and 8-wide SIMD interiors of
  the sample and live paths at every tier (Eco, Normal, HQ) and mip level, bit for bit (§4.4);
- the window's Skew in `gg/grain/Envelope.h`, with its gain argument (§4.5);
- both as route targets and per-voice targets (sub-project 1's `isVoiceTarget`), and in Mutate's
  default locks (§4.6);
- the event log's sign and its `"skew"` field (§4.7); the canvas's pitch for a reversed grain
  (§4.8);
- the parameters' placement in the Edit view (the frame tests require every parameter reachable);
- `gg-render`'s keys, nine goldens, the unit, kernel, engine, core, metrics and plugin tests, and
  two report-only benchmark cases (§4.9 to §4.12);
- the records: INTENT §14, §17, §18, §20 and a §23 row; the program design's §8 row (§6).

**Out:**
- a curve on the window's slopes (an exponential or logarithmic fall, beside Skew's position of the
  peak): later, if ears ask; Texture and Skew together cover the shapes the comparison named;
- a choice of window family (Tukey, Gaussian, ...): the Hann family stays the one family (§14
  Texture);
- reversing the window with the read (G7);
- reversing the Spectral smear's analysis or its synthesis frames (G8);
- per-sample direction changes inside a grain (a grain reads one way for its life);
- new factory presets that reverse or skew (sub-project 5's, which these parameters enable);
- remote-controls pages for the new parameters (sub-project 5's; §7).

## 3. Forced choices (Claude's provisional calls; the director may overrule each)

- **G0. The director's, not a call: sub-project 8 is new, in M3.** The director, 2026-11-07: "no
  i think this all needs to happen in m3" (every gap of the comparison into M3). It amends the
  approved program design (§8 gains row 8; §20's M3 row names it) with this sub-project's §23 row.

- **G1. Reverse is a per-grain probability p in [0, 1], linear (p = the CLAP value), drawn from a
  new field.** A grain is reversed when `p > 0 && uniform01(hash(seed, layer, stream,
  Field::Reverse, k)) < p`: at p = 1 every grain (uniform01 is below 1), at p = 0 no draw at all,
  so every other draw is today's, bit for bit (the pattern of Bus Split, extremes spec §6.3).
  `Field::Reverse = 0x70` (G13). *Declined:* a Direction switch (Forward, Reverse; it is p = 0 and
  p = 1 of this, without the mixtures the comparison found); a bipolar control (−100% all
  reversed … +100% all forward: no rest point that reads as "off", and a route couldn't add
  reversal from 0).

- **G2. A reversed grain covers its forward twin's span, read from its far end.** With the forward
  grain reading from start s at rate r > 0 for D output samples (the span [s, s + r·D] in the
  source's frames), its reversed twin starts at `s + r·D` and reads at −r, so at Texture 0 and
  Skew 0 it is the forward grain played backward. Raising Reverse then changes how grains read,
  never what material is heard: Position, Spray, Smear's jitter and the playhead (sub-project 3b)
  mean what they mean today. The far end is clamped into the material (G4, G5). *Declined:*
  reading backward from the forward start (the span would move by r·D with each grain's direction
  draw, so Reverse would also shift the material heard; on a Sample at Position 0 every reversed
  grain would read before frame 0, silence); a span centered on the forward start (the same
  shift, halved).

- **G3. A reversed grain is a grain with a negative rate.** `GrainSpec::rate = −r` and
  `GrainSpec::startPos` the mirrored start; the kernels' arithmetic, `pos = (startPos + rate × e)
  × invDec`, is unchanged, so no per-sample branch and no second kernel family exist. Everywhere
  the rate's **magnitude** matters it is read as |rate| (G6). *Declined:* a direction flag with
  `startPos − rate × e` in every kernel (a new branch or template axis in every renderer, and the
  event log would need a new field to show what the sign shows).

- **G4. On a ring (Live, Resample, the smear's) a reversed grain never crosses the head.** The
  admission clamp keeps the read inside the window over the grain's whole life, as today, with the
  bounds re-derived for a read that moves away from the head (§4.3, GB3): its newest frame is read
  at the onset, so it must already be M frames behind the head; its oldest is read at the end, so
  it must still be inside the window after the head has advanced. At Position 0 a reversed grain
  therefore reads the newest D frames of the input backward (a reverse delay of one grain). A span
  that fits neither way drops, counted, as today.

- **G5. On a Sample the far end is clamped to the last frame, and a reversed grain that reaches
  frame 0 while its window is open ends there with the edge fade.** The mirrored start is
  `min(s + r·D, F − 1)` (F the level-0 frames), so a reversed grain never enters the material
  mid-window (its window is 0 at its start, so nothing is cut there). A grain longer than the
  material (r·D > F − 1) reaches frame 0 and is cut with carry-over row 300's raised-cosine edge
  fade, mirrored: `grainEdgeSample` finds the first sample past frame 0 for a negative rate as it
  finds the first past the last frame for a positive one (GB4).

- **G6. Every kernel renders reversed grains, the SIMD interiors included, bit for bit with the
  scalar references, in this sub-project.** What the code does today, read for a negative rate:
  - **the scalar references** (sample and live, Eco, Normal, HQ, every level): correct as written
    (the position formula and every reader are direction-agnostic; taps outside the material read
    0), except the **sinc's stretch**, `stretchOf(rate × invDec)`, which gives 1 for a negative
    rate (no anti-aliasing for a reversed grain pitched up): it becomes `stretchOf(|rate × invDec|)`;
  - **the mip level**, `mipLevelFor(rate, …)`, gives level 0 for a negative rate: it is computed
    from the magnitude;
  - **the 4-wide and 8-wide sample interiors**: their per-lane arithmetic is direction-agnostic and
    so already matches the reference; `sampleSimdRun` solves its bounds only for rate > 0 and steps
    sample by sample otherwise (correct, slow): it gains the inverted bounds for rate < 0;
  - **the live interiors** (`liveInterior`, `liveLevelInterior`) return empty for a rate that isn't
    positive, so a reversed ring grain renders through the reference (correct, several times the
    cost): `liveRun` and `liveLevelRun` gain the bound for rate < 0, and `RingTaps::locate` (4-wide)
    and `RingTaps8::locate` (8-wide), which require lanes in increasing order, gain the wrap below
    slot 0;
  - `LiveSource::slotOf`'s cursor serves only forward reads and falls back to a division backward:
    the same slot (a function of the frame), so it is a cost, which a backward branch removes.

  *Why now:* the budget is deterministic and never measures CPU (CLAUDE.md), so a grain's weight
  must be what it costs. A layer at Reverse 100% rendered through the reference would spend several
  times the CPU its weights claim. *Declined:* the reference alone for reversed grains (above);
  a reversed copy of the material (memory, and a ring has no fixed copy).

- **G7. The window stays in output time: Reverse flips the read, not the window.** Skew −100% with
  Reverse 100% is a percussive grain of backward material, not a swell. *Declined:* mirroring the
  window with the read (Skew's meaning would then depend on each grain's direction draw, and a
  percussive patch would turn to swells as Reverse rose).

- **G8. On a Spectral layer Reverse acts on the grains, not on the smear.** Spectral grains read
  the smear's ring through the ring path (`Engine::liveGrain`), so G4 applies to them as to a Live
  layer's. The smear's analysis reads its material forward at each hop, and its synthesis frames
  are written forward, as today. A backward read of the smear (random-phase frames, overlap-added)
  sounds much like a forward one: what changes is each grain's envelope against the frames' own.
  *Declined:* reversing the smear's analysis (that is a playhead running backward, sub-project
  3b's H9 at a negative Scan Speed); ignoring Reverse on Spectral layers (a kind-dependent rule
  with no reason).

- **G9. Skew σ in [−1, 1] moves the window's peak by trading attack for release, from Texture's
  window toward a floor.** With (a, r) today's attack and release fractions for the grain's
  Texture and duration (`Envelope.h`), and `sMin = min(0.5, max(0.01, 16 / d))` (each side at
  least 16 samples and 1% of the grain, never more than half):
  - σ = 0: today's shape, returned unchanged (a branch, so no rounding question);
  - σ < 0 (earlier): `a' = (1 − |σ|) × a + |σ| × min(a, sMin)`, `r' = (a + r) − a'`;
  - σ > 0 (later): `r' = (1 − σ) × r + σ × min(r, sMin)`, `a' = (a + r) − r'`;
  - the sustain `1 − a − r` is unchanged, and so is `a + r`, up to one rounding.

  At Texture 0, Skew −100% on a 100 ms grain rises over 1 ms (48 samples at 48 kHz) and falls over
  99 ms; +100% the mirror. The lerp form lands exactly on both anchors (Texture's ruling PU-T3a).
  `min(a, sMin)` means a side already shorter than the floor (a short grain at Texture 0.5) is never
  lengthened: Skew never moves the peak the wrong way. A gated window (Texture 1, a = r = aMin,
  5% for a long grain) still skews: its attack or its release goes down to the 1% floor, the
  other side taking the difference. A grain of 32 samples or fewer has sMin = 0.5, so Skew leaves
  it as it is. *Why 1%:* Texture's own floor, 5%, keeps a 1 s
  grain's attack at 50 ms, which is not percussive; 0 would be a step. Measured on the M2-era
  instrument (`m3-voice`'s `gg-render`, a 220 Hz sine, Density 8 Hz, Texture 1, so attacks at
  Texture's floor): a 16-sample attack scores clickScore **8.58**, a 37.5-sample one **5.80**, a
  240-sample one **0.00**, against the limit of 15; over a 2 kHz sine 1.4 to 3.3 whatever the
  attack. So the 16-sample floor that already binds short gated grains stays below the limit, and
  Skew's 1% (48 samples at 100 ms) sits between the measured points. *Declined:* a bipolar
  Texture (its low half to early peaks: Texture's range, Macro 5's route and every preset with
  Texture would change meaning); Texture's 5% floor (above); a floor of 0 (a click); a separate
  slope curve (out, §2).

- **G10. The gain normalization is unchanged, because Skew keeps the window's mean and energy.**
  The warped Hann's integral is `1 − (a + r)/2` and its energy `1 − 5(a + r)/8` (each warped half
  integrates to 0.5 and 0.375 of its length): both depend on a + r alone, which Skew keeps. So
  every skewed grain has today's mean and RMS, and on uncorrelated material a skewed stream's
  level is today's. On coherent material at a periodic overlap the sum ripples (the point of a
  percussive stream): computed for Skew −100% at Texture 0 over a constant, the overlap sum's RMS
  times today's `1 / sqrt(Density × Size)` moves **+0.37 dB** at an overlap of 2, **+0.09 dB** at
  4, **+0.02 dB** at 8, and **0 dB** at 1 (one window at a time); −50% moves +0.13 dB at 2. All
  under the goldens' 0.5 dB metric tolerance. *Declined:* re-deriving the normalization per Skew
  (it would couple gain to shape for a fraction of a dB, and the at-rest rule needs nothing).

- **G11. Skew doesn't move V1's latch point.** A voice grain's gain still latches Env 1's value
  at onset + D/2 (sub-project 1's V1), wherever the skewed window peaks. *Declined:* latching at
  the window's peak (onset + a'·D: it would move every voice grain with Texture ≠ 0 too, and the
  difference is one Env 1 slope over half a grain).

- **G12. Both are sound parameters, route targets and per-voice targets, locked out of Mutate's
  defaults.** Saved with the patch, set by a preset load, defaulted by INIT; no patch-version bump
  (a state or preset without them reads 0, today's behavior). Every route may target them, and
  `isVoiceTarget` gains both, so a per-voice source reaches each voice's grains (sub-project 1's
  §4.4). Mutate's default locks include both, so no press of a factory preset or of INIT moves
  (sub-project 3b's H11 for the same reason); a user unlocks them. *Declined:* unlocked by
  default (every `--mutate-preset` output and every factory preset's press would change at once).

- **G13. Indices.** Layer indices **0x70 (Reverse)** and **0x71 (Skew)** of the range 0x70 to 0x73
  agreed for this sub-project, in a new list `GG_LAYER_PARAMS_M3_GRAINSHAPE` after
  `GG_LAYER_PARAMS_M2_EXTREMES` (and after sub-project 3b's `GG_LAYER_PARAMS_M3_PLAYHEAD`, 0x68 to
  0x6A, where it has landed); 0x72 and 0x73 stay unused. **No global parameter.** The new field is
  `rng::Field::Reverse = 0x70`, the same number as the parameter's index, so no parallel branch can
  take it (each sub-project's CLAP range is its own; fields only need to be distinct, and
  `SignalNoise = 100` already leaves a gap). *Declined:* 13, the next value, which any parallel
  branch appending a field would also take, moving this sub-project's goldens at merge.

- **G14. The event log shows a reversed grain by its rate's sign, and a skewed grain by a new
  field written only when Skew isn't 0.** A reversed grain's line has `"rate":-<r>` and `"pos"` its
  mirrored start; a skewed grain's line adds `"skew":<σ>` after `"tex"`. Every line at rest is
  byte-identical to today's (as sub-project 1's `"v"` is written only on voice lines). *Declined:*
  a `"rev"` field (redundant with the sign); `"skew"` on every line (every golden log would move).

- **G15. The canvas draws a reversed grain at its pitch.** `pitchY` reads |rate| for a negative
  rate, amending views ruling VW-T2c ("a reverse grain draws centered"), which predates reverse
  grains: a backward grain an octave up is an octave up. The capsule's horizontal extent already
  spans the read (`grainCapsule` orders its ends). *Declined:* drawing reversed grains centered (it
  would misplace every reversed grain's pitch on a pitch axis); a direction mark (sub-project 5's,
  if wanted).

- **G16. At Clock Pitch, Reverse's draw is held with the others (sub-project 3's S4).** S4 keys
  every per-grain draw but the onset's by the last refresh grain's index, so a refresh's pulses
  repeat one grain shape; the Reverse draw is one of them, so every pulse of a refresh reads the
  same way and the pitch holds. This is S4's rule applied, not an exception. A held ring anchor
  (S4, its §4.6) is the forward start; a reversed held grain starts at `anchor + r·D`, under the
  reversed clamp.

## 3a. The builder check's rulings (Claude's, binding on the plan)

- **GB1. The parameters reach the scheduler as two `CloudParams` fields.** `double reverse{0.0}`
  (the probability, [0, 1]) and `double skew{0.0}` ([−1, 1]); `CloudScheduler::setParams`
  sanitizes each as the others are (non-finite to the default 0, then clamped). `toCloudParams`
  (`core/src/ParamMap.cpp`) sets `p.reverse = param(LayerParam::Reverse)` and `p.skew =
  param(LayerParam::Skew)`, beside `p.texture`. `sameCloud` (`core/src/Processor.cpp`), which
  compares every `CloudParams` field bit for bit for the Processor's and the voices' change
  detection, gains both, and its `static_assert(sizeof(CloudParams) == 120)` moves to the new size
  (136 unless padding moves; merged with 3b's `scanSpeed` and `scanEdge`, the sum), so a change of
  Reverse or Skew alone reaches the engine and an unchanged one never does.

- **GB2. The draw and what it carries.** `GrainDraw` gains `bool reversed{false}` and `double
  skew{0.0}` (its defaulted `operator==` covers them); `takeNext` sets `skew = params_.skew` as it
  sets `texture`, and `reversed` by G1's test, keyed `key(rng::Field::Reverse)`, with no draw at
  p = 0. Nothing else in `takeNext` changes, and no onset moves (Reverse never changes the
  schedule). `GrainSpec` gains `double skew{0.0}` (copied by `sampleGrain` and `liveGrain`), and
  `GrainPool::spawn` latches `envelopeShape(g.texture, g.skew, g.durationSamples)` into the four
  env rows; no new pool row (the env rows carry the shape, and only the event log, which reads the
  `GrainSpec`, needs σ itself).

- **GB3. The exact start and bounds.**
  - **Sample** (`Engine::sampleGrain`): with `start` and `rate` (> 0) computed as today, a
    reversed draw gives `startPos = std::min(start + rate × d.durationSamples, span)` and
    `rate = −rate`; the level is `mipLevelFor(magnitude)` from the positive rate, computed before
    the sign is applied (so a forward grain's code path, and its bits, are today's).
  - **Ring** (`Engine::liveGrain`): for a reversed draw, with ρ, `step`, `dur`, `rate` (> 0),
    `span` and the splice case's `stepOldest`, `spanOldest` as today:
    `hi = ρ − 3 − M` (the forward `max(0, dur × rate − span)` term is 0: the read moves away from
    the head), and `lo = ρ − W(t0) + 1 + M + span + rate × dur` (the forward `max(0, span × (1 −
    rate))` becomes `span + rate × dur`: the oldest frame advances by `span` while the read falls by
    `rate × dur`; with a bound splice start, `windowOldestAt(t0) + stepOldest + 1 + M + spanOldest
    + rate × dur`). The start is `clamp(ρ − positionDraw × sampleRate + rate × dur, lo, hi)`;
    `lo > hi` drops the draw, counted in `dropped_`, as today. The rate stored is `−rate`; the level
    is `mipLevelFor(rate, …)` of the positive rate. The forward path is untouched.
  - The derivation, for the record: the read at elapsed e is `s − rate × e` and the window's ends
    move by `adv(e) = min(e, span)`; staying M inside the end needs `s ≤ hi0 + min_e(adv(e) + rate ×
    e) = hi0`, and inside the oldest `s ≥ lo0 + max_e(adv(e) + rate × e) = lo0 + span + rate ×
    dur`. The forward bounds are the same extrema with `− rate × e`.

- **GB4. The edge at frame 0** (`grainEdgeSample`, `GrainPool.h`). For `rate < 0` the test is
  `startPos + rate × e < 0`, the estimate `n = floor(onsetFrac + (0 − startPos) / rate) + 1`, then
  the same clamping, the same stepping (at most four samples each way) and the same window check;
  `rate == 0` or NaN is `kNoEdge`. For `rate > 0` every line is today's. The edge fade itself
  (`sampleFaded`, ruling EF-6) is in output time and direction-agnostic. The function's comment's
  "the read only moves toward this edge" becomes "toward the edge in its direction: the last frame
  for a positive rate, frame 0 for a negative one".

- **GB5. The SIMD changes, exactly** (`GrainKernels.h`; the 8-wide file calls the shared run
  finders and needs only its own `RingTaps8::locate`):
  - `sampleSimdRun`, for `rateL < 0`: `nBegin = ceil(max(onsetFrac, onsetFrac + (hi − startL) /
    rateL)) − 2` and `nEnd = ceil(min(onsetFrac + duration, onsetFrac + (lo − startL) / rateL)) + 2`,
    then today's clamps and stepping (which make the run exact whatever the rounding). posL is
    non-increasing in t for a negative rate, by the comment's own argument, so the conjunction is
    still an interval. `rateL == 0` or NaN keeps today's fallback.
  - `liveRun` and `liveLevelRun`: the `pos ≥ b` bound, today `nFirst = max(onsetFrac, onsetFrac +
    (b − startL) / rate)`, becomes for `rate < 0` `nLast = min(nLast, onsetFrac + (b − startL) /
    rate)` (nFirst stays `onsetFrac`); the head and tail bounds already go through `bound(c, k)`,
    which handles either sign of `sE − rate` and `rate − sO`. The callers' guards (`liveInterior`,
    `liveLevelInterior`) become `rate != 0 && |rate| < 2^62`.
  - `RingTaps::locate` and `RingTaps8::locate`: after today's `slot -= slot >= capacity ? capacity
    : 0`, add `slot += slot < 0 ? capacity : 0`. The doc's "in increasing lane order" becomes
    "monotone in lane order"; the one-wrap argument holds as written (every tap of a step is in one
    span of N + 3 frames, below C).
  - `paramsOf`: `stretchOf(std::abs(rows.rate[i] * invDecimation(decimation)))`.
  - If the builder finds a case where an interior can't match the reference exactly, the fallback
    is the reference for that case (correct), recorded as a ruling with its `gg-bench` cost.

- **GB6. `LiveSource::slotOf` gains a backward branch** (`first < cursor.index && cursor.index −
  first < capacity_`: `slot −= difference`, wrapped up once), so a reversed reference read finds
  its slot without a division. Its value is the modulo's, so no bit moves (a test pins it).

- **GB7. The Processor.** `kVoiceTargets` gains `LayerParam::Reverse` and `LayerParam::Skew` (12 →
  14 on this branch); `kVoiceKeySlots` follows from its size; `isVoiceTarget` (`ModMatrix.h`) gains
  both cases. A voice's `CloudParams` carries its own reverse and skew, so each voice's grains draw
  with its own probability and shape. Nothing else in `updateVoices` changes.

- **GB8. Spectral and Voice mode** need nothing beyond GB3's ring path: Spectral grains go through
  `liveGrain` with the smear's ring and its window. The smear's analysis (`SpectralSource::process`)
  is untouched (G8).

- **GB9. The overload ladder** weighs a reversed grain as a forward one at its tier
  (`grainWeight(tier, stereo)`), steals it the same way (the steal fade is in output time), and a
  lowered tier keeps the reversal (the rate's sign is the grain's, not the tier's). Reverse never
  changes the number of grains.

- **GB10. Real-time safety.** One compare and, at p > 0, one hash per grain in `takeNext`; a
  branch in `sampleGrain` and `liveGrain`; a few double operations in `envelopeShape` at spawn; a
  compare per lane in `locate`. Nothing allocates, locks or loops beyond today's bounds. RTSan and
  the tripwire tests cover a Reverse 50% / Skew −100% layer on a moving ring and on a sample, in
  both products, with 16 voices.

- **GB11. Cost.** At rest the X headline case (`engine-sample`) gains one predicted branch per grain
  (`reversed`) and the 2-argument envelope's skew check: `gg-bench --cases reference,engine-sample
  --quick` before and after, the ratio within its noise. Two new report-only cases measure the
  reversed path against its forward twin in the same rounds: **`engine-sample-reverse`**
  (`engine-sample` with every grain reversed) and **`engine-live-reverse`** (`engine-live` the
  same). The builder's bar: each within 10% of its twin, or the gap recorded as a ruling with the
  reason. No baseline entry yet (report-only, as the other new cases).

- **GB12. The rate's magnitude is never 0.** A grain's rate is the pitch ratio, exp2Det of a pitch
  clamped to ±48 semitones, times the ratio of two finite, positive sample rates, so `−rate` is
  finite and negative; the `rate == 0` branches above are defensive, as today's are.

- **GB13. The playhead (sub-project 3b) and the mirror, in order.** The forward start is computed
  first, with the playhead's point where 3b supplies it (Sample: folded in `takeNext`, so it
  arrives in `positionDraw`; ring: 3b's `scanFold(scanPoint, …) + positionDraw` in `liveGrain`);
  then the mirror adds `rate × D`; then the clamp for the grain's direction. Reverse 100% with a
  Scan Speed of −1× plays a sample backward at real time; with +1× it scans forward through
  backward grains. Whichever of 3b and 8 lands second writes the merged `liveGrain` and adds a test
  of both together (a reversed, scanning ring grain stays inside the reversed bounds).

- **GB14. Display and text.** Reverse is shown as a percentage, `0%` to `100%` (sst's
  `asPercent()`, as Texture); Skew as a signed percentage, `-100%` to `100%`, 0 shown `0%` (as
  sub-project 2's Env Curve is shown; `asPercentBipolar()` or the equivalent). Text entry parses
  both with or without `%`. Both are `CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE`, as
  Texture.

- **GB15. `envelopeShape` keeps its two-argument form.** The new three-argument
  `envelopeShape(texture, skew, durationSamples)` computes today's (a, r) through one shared helper
  and applies G9; `envelopeShape(texture, durationSamples)` returns exactly what it does today
  (the helper's (a, r) as `{a, 1 − r, 0.5 / a, 0.5 / r}`), so every existing caller and test is
  untouched, and σ = 0 in the new form returns it. A skew outside [−1, 1] or NaN reads as 0, as an
  out-of-range Texture reads as 0; a NaN duration reads as a long grain's floor (0.01), as today's
  `aMin` does.

- **GB16. Windows on a frozen or spliced ring.** The reversed bounds read the same window
  functions at the same sample (`windowFramesAt(t0)`, `anchorRunAfter`, `oldestRunAfter`) as the
  forward ones, so Freeze, Loop and a selected splice need nothing new; a test covers each.

- **GB17. The builder check's remaining answers.**
  - *A Sample of one frame or none:* `span` is 0, the mirrored start is 0, and a reversed grain
    reads before frame 0 from its second sample, so `grainEdgeSample` cuts it at once (frames 0 is
    `kNoEdge`, nothing being read), as a forward grain past a one-frame file is cut.
  - *A source change under a sounding reversed grain:* it keeps its latched slot and reads to its
    end, as a forward one does (`retirePrevious` reads `endSample`, which Reverse doesn't change).
  - *Feedback and Resample:* a reversed grain's output is output like any other; nothing changes
    in the taps.
  - *The UI's published grains* (`GrainVis`, the canvas queue) carry the rate's sign as they carry
    the rate; no new field.
  - *Steal lines* in the event log are unchanged (they name the stolen grain's index alone).
  - *Velocity → Skew in `inst-voice-grainshape`:* unipolar at depth −1, so velocity 127 gives
    Skew −1 and velocity 20 about −0.16; Velocity → Reverse at depth 1 gives p = velocity / 127.
  - *Sample mode's `gg-render` stream* sets no quota, as today, so its reversed goldens see no
    ladder.

## 4. Design

### 4.1 Parameters (append-only IDs; the indices agreed across the parallel sub-projects)

| Slot | CLAP ID (A … D) | Display name | Range, default | Kind |
|---|---|---|---|---|
| `Reverse`, `B_Reverse` … | `0x010070` … `0x040070` | Reverse (A Reverse …) | 0 to 1, 0; shown 0% to 100% | layer, automatable, modulatable; a route and voice target |
| `Skew`, `B_Skew` … | `0x010071` … `0x040071` | Skew (A Skew …) | −1 to 1, 0; shown −100% (early) to 100% (late) | layer, automatable, modulatable; a route and voice target |

- A new list `GG_LAYER_PARAMS_M3_GRAINSHAPE(X)` (`X(Reverse, 0x70, "Reverse") X(Skew, 0x71,
  "Skew")`) **after** `GG_LAYER_PARAMS_M2_EXTREMES(X)` (and after `GG_LAYER_PARAMS_M3_PLAYHEAD(X)`
  if it has landed) in both `GG_LAYER_PARAMS` and `GG_LAYER_PARAMS_SINCE_M1`, so the index column
  stays ascending (`validIndexColumn`). The other M3 sub-projects' lists below 0x40 sit between
  `GG_LAYER_PARAMS_M3` and `GG_LAYER_PARAMS_M2_SOURCES`, so the lists never touch at a merge.
- `detail::isReservedIndexInUse` gains `scope >= 1 && index >= 0x70 && index <= 0x71` (the
  reserved-block static_assert requires it). `kNextFreeLayerIndex` and `kNextFreeGlobalIndex` are
  untouched (they count indices below 0x40).
- Counts on this branch: `kLayerParamCount` 63 → 65, `kParamCount` 290 → 298; merged with the
  others, the sums. Every hard-coded count follows, as sub-project 1 listed (`tests/support/
  ParamTable.h`, `patch_tests`, `identity_tests`, `params_tests`, `rt_tests`, `EditLayout` and its
  tests, the sweep floors with their reasons), and the comments in `ParamIds.h` that state counts.
- Metadata (`core/src/ParamIds.cpp`): Reverse as Texture (`fresh().asPercent().withDefault(0.0f)`);
  Skew a bipolar percentage, range −1 to 1, default 0 (GB14).
- No patch-version bump (G12); `PatchState` reads absent values as defaults (the builder confirms,
  as sub-project 1 did, and records it).
- Mutate: both join `defaultMutateLocks` (G12).
- The display names "Reverse" and "Skew" are unique among the parameters (checked: no "Skew" or
  "Reverse" exists in `core/`, `engine/` or `plugin/`).

### 4.2 The reverse draw (`CloudScheduler`, `Rng.h`)

`rng::Field` appends `Reverse = 0x70` with the comment "whether a grain reads backward (M3
grain-shapes spec G1)". In `takeNext`, beside the bus draw:

```cpp
const bool reversed =
    params_.reverse > 0.0 && rng::uniform01(key(rng::Field::Reverse)) < params_.reverse;
```

carried into `GrainDraw::reversed`, with `GrainDraw::skew = params_.skew`. At p = 0 nothing is
drawn and `reversed` is false; every other field of the draw is today's, bit for bit, at any p
(each field has its own key).

### 4.3 What a reversed grain reads on each source

- **Sample:** the forward span [s, s + r·D] of the file, read from `min(s + r·D, F − 1)` down at
  −r (GB3). A grain that reaches frame 0 with its window open ends there with the edge fade (GB4).
  Spray in seconds and Smear's jitter shift s as today, before the mirror.
- **Live and Resample rings:** the forward span, mirrored, under the reversed clamp (GB3): the
  newest frame read at the onset at least M behind the head, the oldest still inside the window at
  the end. On a frozen ring (the head held) both bounds stand still and the reversed grain reads
  the frozen material backward; on a moving ring at Position 0 it reads the last D frames backward.
- **Spectral:** the smear's ring, as a Live ring (G8).
- **Oscillator** (not built; falls back to the product's default kind, `ParamMap.h`): whatever kind
  it falls back to.

### 4.4 The kernels

G6 and GB5 give every change. The per-sample arithmetic of every renderer is unchanged, so the
claim to test is the one the kernel tests already make for forward grains: every SIMD interior
(4-wide and 8-wide; sample planar, interleaved and stereo-pair; live at blocks with and without a
kink; levels 0 to 3; Eco, Normal, HQ; one and two outputs) matches its scalar reference with
`std::memcmp`, now at negative rates too, and every run finder returns exactly the samples a
brute-force scan of its condition admits. The frozen `reference` kernel in `tools/gg-bench` is
untouched (it never sees a reversed grain).

### 4.5 The window's Skew (`engine/include/gg/grain/Envelope.h`)

G9 and GB15. In one line: Texture gives (a, r); Skew trades attack for release toward
`min(side, sMin)`, `sMin = min(0.5, max(0.01, 16 / d))`, keeping a + r; the window at phase φ is
`hann(warp(φ))` with the same `warpPhase`, so no kernel changes for Skew, and `isIdentity` still
holds only at a = r = 0.5 (a skewed grain takes the warp's path, as a textured one does). The
gain normalization is unchanged (G10). Texture's §14 paragraph gains the Skew law.

### 4.6 Modulation and the voice path

GB7 and G12. A route's depth is in the target's CLAP domain, as every route's is: Velocity →
Reverse at depth 1 (unipolar) makes a note at full velocity reverse every grain; Velocity → Skew at
depth −1 (unipolar) makes it percussive. On a Voice layer the per-voice pass adds the offset per
voice (sub-project 1's B2); on a Cloud layer the route applies layer-wide. Sub-project 2's per-note
parameter-modulation slots (its E7, 23 per layer) gain Reverse and Skew, whichever of 2 and 8 lands
second (§7).

### 4.7 The event log (`tools/gg-render/Render.cpp`, `formatEventLine`)

A reversed grain: `"rate"` negative, `"pos"` its mirrored start (G14). A grain with
`GrainSpec::skew != 0`: `"skew":<σ>` after `"tex"`, in fmt's shortest round-trip form, on Cloud and
voice lines alike. Every other line is today's byte for byte. `formatEventLine`'s comment documents
both.

### 4.8 The canvas (`ui/src/LaneModel.cpp`)

`pitchY` reads `|rate|` for a finite negative rate (G15); rate 0, NaN and infinities still draw
centered. Its comment and the views spec's VW-T2c entry are amended; the LaneModel test that pins a
reverse grain centered is rewritten to pin it at its pitch.

### 4.9 Determinism and real-time safety

- One new draw, keyed by its own field; no other key changes; no draw at Reverse 0.
- Every value is latched at the grain's onset from values the Processor's grid applies at the same
  sample at any block size; the mirror, the bounds and the shape are closed-form functions of the
  latched values. Tests at blocks 1, 7, 32, 64, 256 and 333 (§4.12) pin it with Reverse 50% and an
  LFO on Skew, so both change every cell.
- 64-bit throughout: positions, the mirror and the bounds in double (CLAUDE.md); the arithmetic in
  `Envelope.h` under `GG_FP_CONTRACT_OFF`, as today's.
- RTSan- and tripwire-clean (GB10).

### 4.10 The at-rest rule

With Reverse 0 and Skew 0 on every layer (the defaults), `takeNext` draws nothing new, `reversed`
is false, `sampleGrain` and `liveGrain` take today's path, `envelopeShape` returns today's shape,
the kernels see positive rates (their new branches unreached; `|x|` of a positive x is x), and the
event log writes no `"skew"`. **Every existing golden's WAV SHA-256 is unchanged** (the 108 and
sub-project 1's eight voice cases), and their event logs byte for byte: the at-rest check
(`check-wav-hashes.sh`, last line `baseline cases: 108, changed: 0, gone: 0`) after every DSP task,
and the voice goldens compared, in the final report.

### 4.11 `gg-render`

- `params` takes `"A Reverse"` … `"D Reverse"` (0 to 1) and `"A Skew"` … `"D Skew"` (−1 to 1),
  from the macro tables (`RenderSpec.cpp` expands the lists); automation and routes take them as any
  other. Bare `"Reverse"` and `"Skew"` are unknown keys (only layer A's eleven M1 names are bare).
- **Sample mode's `stream`** gains optional `"reverse"` (0 to 1, default 0) and `"skew"` (−1 to 1,
  default 0), passed to the engine's `CloudParams`; out of range is bad input (exit 2). Absent, the
  stream is today's. `RenderSpec.h` documents both.
- A preset source (`PresetSource.h`) takes them in `params` like any layer parameter.

### 4.12 Goldens (new; each 4 to 6 s, seed 1; each catches the 16 injected steps or joins the click-injection table with its reason)

- `sweep-stretch-reverse` (sample mode): M0's `sweep-stretch` with `"reverse": 1`: every grain
  backward over the sweep, at the engine alone; the log's every `rate` negative.
- `inst-reverse`: one held note over a built-in 4 s sweep sample (100 Hz to 2 kHz), Position 0.3,
  Size 150 ms, Density 16 Hz, Reverse 0, then 0.5 at 2 s, then 1 at 4 s.
- `inst-reverse-edge`: a 0.25 s 660 Hz sine sample at Position 0.5, Size 1 s (longer than the
  sample), Reverse 1, notes at rates 0.5, 1 and 2 (keys 48, 60, 72): every reversed grain starts at
  the last frame and ends at frame 0 with the edge fade (the mirror of `inst-sample-end`).
- `inst-reverse-hq`: layer A at HQ over the 20 kHz to 50 Hz sweep sample `fx-hq-pitch-ladder` uses,
  Reverse 1, Pitch 0, +12 and +24 at 1.5 s intervals: the sinc's stretch and the mip level from
  |rate| (levels 0 to 2 in the log's `lvl`).
- `fx-reverse-ring`: the FX, layer A Live over a stereo sweep, Size 200 ms, Density 16 Hz,
  Reverse 1 at Position 0 (the reverse delay), Position 0.5 s from 2 s, Freeze from 3.5 s, then
  Reverse 0.5 with Pitch +12 from 4.5 s (the head bound at rate 2).
- `inst-skew`: one held note over a 220 Hz sine, Size 100 ms, Density 8 Hz (Distribution 0), Skew
  −1, −0.5, 0, 0.5, 1 at 1 s intervals, then Texture 0.75 with Skew −1 at 5 s.
- `fx-skew-percussive`: the FX over a stereo 330 Hz sine, layer A Live, Size 60 ms, Density 6 Hz
  periodic, Skew −1; layer B Live at Pitch −12, Skew +1; Reverse 1 on layer A from 3 s (backward
  material in a percussive window, G7).
- `inst-voice-grainshape`: layer A at Mode Voice over the sweep sample, routes Velocity → A
  Reverse (unipolar, depth 1) and Velocity → A Skew (unipolar, depth −1); notes at velocities 20,
  64 and 127, staggered 1 s, each held 1.5 s: each voice's share of negative rates and its `"skew"`
  in the log follow its velocity.
- `fx-spectral-reverse`: layer A Spectral over `loop-48k.wav` as its sample, Reverse 1, Skew −0.5:
  reversed grains on the smear's ring (G8).

Each new case's spectrogram is looked at before commit. A case that hides injected steps (the
sweeps' top octaves, the smear's noise bed) joins the click-injection table with its exact count and
reason, in the same commit.

### 4.13 Tests

- **Envelope** (`tests/engine/window_tests.cpp`, `[engine][window][grainshape]`): σ = 0 returns the
  two-argument shape bit for bit over a grid of Texture (0, 0.25, 0.5, 0.75, 1) and durations (16,
  17, 100, 320, 4800, 96000 samples); the anchors at σ = ±1 exact (`a' == min(a, sMin)`,
  `r' == min(r, sMin)`); `a' + r'` within one ulp of `a + r`; the peak `a'` monotone in σ; never the
  wrong way for a side below the floor; NaN and out-of-range σ read as 0; the window's numerical
  integral and energy (`hann(warp(φ))` over 10⁵ points) within 10⁻⁶ relative of σ = 0's at every
  grid point (G10).
- **Reverse in the engine** (`tests/engine/reverse_tests.cpp`, `[engine][grainshape]`): no draw
  at p = 0 (every `GrainDraw` field equal to today's over a stream); at p = 1 every grain reversed,
  at p = 0.5 about half (a 10,000-grain count within 3σ) and the same grains at every block size;
  the Sample mirror (`startPos − rate × D` equals the forward start, or the clamp's case) and the
  last-frame clamp; `grainEdgeSample` for negative rates against a brute-force scan; the ring's
  reversed bounds: over every sample of a reversed grain's life the reference's read succeeds and
  `Reader::fade == 1`, for Eco, Normal and HQ at levels 0 to 3, on a moving ring, a frozen one, with
  Loop on, and with a selected splice (GB16); Spectral grains reversed on the smear's ring; a
  reversed grain at Texture 0, Skew 0, Linear tier, rate 1 and integer start equals the forward
  grain's output reversed in time, within 10⁻⁶; block-size invariance of a Reverse 50% / Skew −1
  engine (Sample, Live frozen and moving, Spectral) at blocks 1, 7, 32, 64, 256 and 333, output and
  grain events (`[determinism]` too).
- **Kernels** (`tests/engine/kernel_tests.cpp` and its siblings, `[kernel]`): every existing
  memcmp case gains negative-rate rows (rates −0.5, −1, −1.5, −4 and an HQ −8 at levels 1 to 3), in
  both tables (4-wide and 8-wide; the AVX2-against-SSE case too); `sampleSimdRun`, `liveRun` and
  `liveLevelRun` for negative rates against brute force; `RingTaps::locate` and
  `RingTaps8::locate` with decreasing lanes across the storage's wrap; `stretchOf` of a negative
  residual equals that of its magnitude; `mipLevelFor` from the magnitude; `slotOf`'s backward
  branch equals the modulo.
- **Core** (`tests/core/grainshape_tests.cpp`, `[core][grainshape]`): the parameters' ranges,
  defaults and flags; `toCloudParams` maps both; a per-voice route (Velocity → Reverse, → Skew)
  gives two voices two values, and a Cloud layer's route applies layer-wide; Mutate's default locks
  hold both (a default press leaves them); INIT and a preset load set them; an LFO → Skew route
  renders the same at the six block sizes in both products (`[determinism]`).
- **Metrics** (`tests/metrics/grainshape_click_tests.cpp`, `[golden][metrics][grainshape]`): the
  click score stays below the limit (15, metricsVersion 2) for Skew ∈ {−1, −0.5, 0.5, 1} × Size ∈
  {5 ms, 20 ms, 100 ms, 1 s} × Texture ∈ {0, 0.5, 1} over 220 Hz and 2 kHz sines at Density 8 Hz,
  and for Reverse 1 at the same corners; the level of `inst-skew`'s steps (its RMS envelope) within
  0.5 dB of Skew 0's (G10); `inst-reverse-hq`'s energy above 16 kHz within 1 dB of its forward
  twin's (the stretch from |rate|: a sign bug aliases).
- **Sweep** (`tests/sweep/`): Reverse and Skew join the parameter sweeps and the extremes' corners
  (Reverse 1 with Skew ±1 at Size 5 ms and 2 s, every source kind), each with finiteness, the peak
  bound and the click score; floors updated with their reasons.
- **Plugin:** the counts; the two parameters' flags and display text round trip; the tripwire
  (`[plugin][rt]`) with 16 voices at Reverse 50% / Skew −1 on a ring; a preset load sets them and
  INIT clears them; Mutate's default press leaves them.
- **UI:** `pitchY` of a negative rate (G15); `EditLayout` places each slot exactly once (its test);
  the frame tests' reachability and elision checks.
- **Render:** `gg-render` parses the eight names and the sample-mode keys, refuses bare `"Reverse"`
  and `"Skew"`, and refuses out-of-range sample-mode values (exit 2).
- **Bench:** `engine-sample-reverse` and `engine-live-reverse` run, and a `[bench]` case checks
  every grain they admit is reversed (GB11).
- `scripts/check.sh` (dev, asan; rtsan in CI), the validators, and the X ratio (GB11).

### 4.14 The Edit view

Skew pairs with Texture in the Grain panel (`edit(Texture, Grain, 4, Skew)`, then `edit(Skew,
Grain, 5)`, Smear and Quality moving down one), and Reverse joins the Pitch panel after Pitch
Spread (it is about how a grain reads). Short labels if the elision check needs them: "Rev",
"Skew". Sub-project 5 may move them.

## 5. Label

A new Catch2 tag `[grainshape]` (the CTest label `grainshape`), listed in CLAUDE.md's Tests section.

## 6. INTENT changes (Part II, each with a §23 row; made by the build's records task)

- **§14** (the engine): Texture's paragraph gains the Skew law, its floor and the gain argument
  (G9, G10); a Reverse paragraph (the draw, the mirror, the ring's reversed clamp, the edge at frame
  0, the kernels' |rate|, the window in output time, Spectral, the held draw at Clock Pitch); the
  `rng::Field` list gains `Reverse = 0x70`; the layer-parameter count. The parameter model's
  "envelope: shape, skew, sustain fraction" now has its skew built.
- **§17** (UI): the Edit view's placement of the two; the canvas draws a reversed grain at its pitch
  (VW-T2c amended).
- **§18** (state): the two parameters, saved with the patch and set by a preset load; no
  patch-version bump; Mutate's default locks.
- **§20** (the roadmap): M3's row names sub-project 8.
- **§22:** the window-family question (Texture's spread, the distribution selector) is unchanged;
  a slope curve is noted as a later option.
- **§23:** one row for this sub-project (the next free number at merge; #97 on this branch), with
  G1 to G16 marked provisional and G0 the director's.
- The program design's §8 gains row 8: "**Grain shapes** | Reverse (a per-grain probability; the
  mirror; the ring's reversed clamp; every kernel at negative rates) and Skew (the window's peak
  early to late, level-neutral) | At rest every golden unchanged; nine goldens; reversed kernels
  bit for bit with the references; click scores under the limit".
- CLAUDE.md: the parameter and golden counts, the `grainshape` label, the new `gg-render` names
  and sample-mode keys, the two benchmark cases.

## 7. Seams with the parallel sub-projects

- **Sub-project 2 (expression):** both edit `isVoiceTarget` and `kVoiceTargets` (its Pan, Bus
  Split and the envelope controls; these two): the merge takes the union and `kVoiceKeySlots`
  follows. Its per-note slots (E7, 23 per layer) gain Reverse and Skew, whichever lands second,
  with the CLAP per-note flags. Its layer lists (0x32 to 0x37) sit below 0x40; this one's at 0x70.
- **Sub-project 3 (pitch-sync):** G16. Both edit `takeNext` (its refresh keying; this one's draw)
  and `liveGrain` (its held anchor; this one's mirror and bounds). The merge keys Field::Reverse by
  the refresh index at Clock Pitch, as S4 keys every per-grain draw but the onset's, and a reversed
  held grain starts at `anchor + rate × D` under the reversed bounds. Whichever lands second adds a
  test: at Clock Pitch with Reverse 50%, every pulse between two refreshes reads the same way.
- **Sub-project 3b (playhead):** GB13. Both edit `liveGrain`'s start and `takeNext`; 3b's H8 ("each
  grain reads forward at its own rate") becomes "in its own direction". Its indices end at 0x6A,
  this one's begin at 0x70, so its list sits before this one.
- **Sub-project 4 (tuning):** nothing shared but the counts.
- **Sub-project 5 (views and presets):** may move the Edit view's placement, add the two to a
  remote-controls page (the Grain page beside Texture), and write factory presets that use them (an
  obvious set: a reversed cloud, a percussive stream, a reverse delay in the FX).
- **Sub-projects 7 (tempo sync), 9 (voice filter), 10 (scenes) and 11 (color):** nothing shared but
  the counts and the ParamIds lists (their own index ranges). A scene recalls Reverse and Skew as
  the ordinary sound parameters they are. A new `rng::Field` there takes its own value, never 0x70.
- **All:** `kParamCount` and the tables merge by addition; §23 rows renumber to the next free number
  at merge; CLAUDE.md's counts are settled at merge.

## 8. Left to the builder

- The exact Edit view slots and short labels, within §4.14, as the frame tests allow.
- Each golden's exact notes, Position and input, within §4.12's description, so each catches the
  16 injected steps or joins the table with its reason.
- Whether `GrainDraw` carries the skew (as specified) or the scheduler's `params_` is read at
  `sampleGrain`; the bits are the same either way.
- If a SIMD interior can't match its reference exactly at a negative rate, the fallback of GB5's
  last item, recorded as a ruling with its `gg-bench` cost.
