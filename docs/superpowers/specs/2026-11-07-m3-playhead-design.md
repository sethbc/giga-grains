# M3 sub-project 3b: the playhead

| | |
|---|---|
| **Status** | Built (Tasks 1 to 6, 2026-11-07 to 2026-11-09). Drafted 2026-11-07 without the director, at the director's request ("keep building all of the sub-projects as parallel as possible and merging them as able to. progress as much as you can without input"). Every forced choice was **Claude's provisional call** (H0 to H13, §3), each with the alternatives declined; the builder check's answers are in §3a (HB1 to HB12). **The director reviewed the calls on 2026-11-09 and confirmed H0 to H13, H13 as amended** (tempo-locked scanning is sub-project 7's, Scan Sync, in M3, not M4's). Ruling R9 (below, HB9) was added in the build |
| **Program** | `docs/superpowers/specs/2026-11-07-m3-program-design.md` (§3's "playable", §4, §8; this sub-project is new, H0) |
| **Builds on** | sub-project 1, `docs/superpowers/specs/2026-11-07-m3-voice-design.md` (V1 to V7, B1 to B10) and its plan, on `m3-voice` (draft PR #37) |
| **Branch** | `m3-sp3b-playhead` (from `m3-voice`), worktree `/home/sethbc/.local/share/giga-grains/worktrees/gg-m3-sp3b-playhead` |
| **Runs beside** | sub-projects 2 (expression), 3 (pitch-sync), 4 (tuning) and 5 (views and presets), in parallel (§7 lists the seams) |
| **Why** | A comparison with the hardware granular instruments (2026-11-07, the program's research notes, gap 1) found that every one of them moves the read position at a set speed, forward or backward, wrapping, bouncing or stopping at the edges and restarting per voice at note-on, while our layers read one static Position. Without it a Voice layer can't play through a sample, the most common way a granular instrument is played, and the reason INTENT §6 borrows Granulator III's Classic mode |

## 1. Intent

Each layer gains a **playhead**: Position becomes where reading **starts**, and **Scan Speed** moves
the read point from there at a set rate, in seconds of source per second of real time, forward or
backward. At the source's edges the point **wraps**, **bounces** or **stops** (**Scan Edge**). Each
voice has its own playhead, restarted at its note-on, so a chord of voices each plays through the
sample from Position at its own stretch; **Scan Track** makes a voice's speed follow its note, as
tape does. A Cloud layer has one playhead for the layer.

The capabilities this covers are generic to granular instruments (time-stretch by scanning, reverse
scanning, looping and ping-pong playback, one-shot holds, per-note restart, tape-style speed
tracking). No product's names, labels or layout are copied.

At rest nothing moves: with Scan Speed 0, the default, every layer reads Position exactly as today,
and every golden (the 108 M2-era cases and sub-project 1's eight voice cases) keeps its WAV SHA-256
and its event log byte for byte.

## 2. Scope

**In:**
- three layer parameters: Scan Speed, Scan Edge, Scan Track (§4.1);
- the playhead itself, a closed-form, exactly accumulated offset per stream in the engine (§4.2);
- the playhead on every source kind: a Sample (a fraction of the file), a Live or Resample ring
  (time behind the window's end, never crossing the head), and the Spectral smear's analysis point
  (§4.3);
- one playhead per Cloud stream and per voice, with their restart rules (§4.4);
- Scan Speed as a route target and a per-voice target (sub-project 1's `isVoiceTarget`), and Scan
  Track's per-voice ratio (§4.5);
- the parameters' placement in the Edit view (the frame tests require every parameter reachable);
- `gg-render`'s keys, seven goldens, the unit, engine, core and plugin tests (§4.8 to §4.10);
- the records: INTENT §14, §17, §18, §20 and a §23 row; the program design's §8 row (§6).

**Out:**
- tempo-locked speed (Scan Speed in beats) and a lock to the host's transport: **sub-project 7's
  (Scan Sync)**, in M3 (H13);
- scan loop points inside a sample (a start and a length narrowing the window): later, if ears ask;
  a Live ring's Loop and splices already narrow its window (§4.3);
- reversed grains (a grain reading backward): another feature. A grain reads forward at its own
  rate whichever way the playhead moves (H8);
- saving the playheads' running state in the session (H11);
- new factory presets that scan (sub-project 5's, which this sub-project's parameters enable);
- remote-controls pages for the new parameters (sub-project 5's; §7).

## 3. Forced choices (Claude's provisional calls; the director may overrule each)

- **H0. The playhead is a new M3 sub-project, 3b, run in parallel with 2 to 5.** It amends the
  approved program design (§8 gains a row, §20's M3 row names it) with a §23 row. It belongs to M3
  because it is how a Voice layer is played. *Declined:* folding it into sub-project 2 (already the
  largest; a separate branch merges sooner); deferring it to M4, tempo sync's home when this was written (M3's instrument
  would close unable to play through a sample).

- **H1. Scan Speed is an offset in source time added to Position, at s seconds of source per
  second.** Scan Speed 0 is today's static read in every mode. On a Sample or a frozen ring, s = 1
  plays the material forward at real time, s = −1 backward, s = 0.001 a 1000× stretch. On a moving
  ring (Live or Resample, not frozen) the ring already moves under a fixed Position, so Scan adds to
  that motion: Position is a lag behind the head, and the playhead moves the lag at −s seconds per
  second (forward is toward the head). There s = −1 holds a point of the material still while the
  head runs away from it, until it reaches the window's oldest edge; s = +1 reads the material at
  twice real time until it reaches the head's edge. The edges are §4.3's. *Declined:* (a) a speed
  relative to the material on a moving ring (s = 0 would hold a point, so Scan 0 would no longer be
  today's read: the at-rest rule breaks and Position's meaning changes between kinds); (b) an
  absolute playhead that replaces Position once moving (Position, and its routes, Timbre → Position
  among them, would stop doing anything while scanning).

- **H2. Scan Speed's law is cubic: its CLAP value v in [−1, 1], speed = 8 v³.** 1× sits at half
  travel (v = 0.5), 1/8× at a quarter, 1/64× at an eighth, 1/512× at a sixteenth, 8× at full
  travel; −v is the same speed backward. A linear knob would crowd every stretch from 10× to 1000×
  into the first 1.25% of its travel; the cubic keeps them at 5% to 23%. For a dyadic v the speed
  is exact in double (`8.0 * v * v * v`), so the goldens' speeds are exact. The display shows the
  speed in × real time (§4.1). *Declined:* linear ±8 (above); a signed exponential (no exact zero
  without a dead band, and 1× at no round value); separate Direction and Speed parameters (two
  controls for one gesture, and a route couldn't sweep through zero).

- **H3. Scan Edge: Wrap (the default), Bounce, Stop, over the source's window.** The window is the
  whole sample, or the ring's window: its length N, the loop's length with Loop on, a selected
  splice's span (§4.3). **Wrap** jumps to the other edge and goes on (a sample loops). **Bounce**
  reflects: the point is a triangle function of the unbounded offset, so the speed's sign is
  relative to the current leg (a ping-pong). **Stop** holds the point at the edge it reached; a
  later change of speed that points back into the window leaves the edge at once. The point jumping
  (Wrap) is never a click: every grain latches its start and is windowed. Wrap is the default
  because playing through a sample and round again is the common case. *Declined:* an edge that
  ends the voice (Env 1 ends a voice, V1; a short Env 1 Release after the note's end already does
  it); Bounce with an explicit direction flag flipped at each reflection (equivalent while the speed
  holds, and the triangle form needs no state).

- **H4. One playhead per stream, and when each restarts.** Each voice stream has its own, restarted
  at its note-on from its own Position (the layer's with the voice's own routes at that cell, so a
  LinnStrument's Y picks each voice's start). The Cloud stream has one for the layer, restarted
  **wherever the engine forces the Cloud stream's onset**: at reset (activation), when the layer is
  switched on, when the held set goes from empty to non-empty (the first key after none), and at a
  switch from Voice to Cloud. So in the instrument a Cloud patch plays through from Position at the
  first note of each phrase; in the FX a note that freezes the ring (D4) scans the frozen ring from
  Position; with no notes the FX's playhead runs free from activation. *Declined:* a free-running
  Cloud playhead in the instrument (where a phrase starts would depend on when it was played); a
  restart at every note-on in Cloud (a chord, or a trill, would restart it every note); a restart at
  the transport's start (sub-project 7's Scan Sync, H13).

- **H5. The playhead is exactly accumulated, piecewise linear in the sample clock.** Its offset at a
  grain's onset t (in output samples, with its fraction) is `x(t) = x0 + r × (t − τ0)`: r is the
  rate in Position's units per output sample, (x0, τ0) the last rebase. A rebase happens only at a
  piece start where the rate, the edge mode or the stream's restart changes it (bit for bit), and
  those change only at grid boundaries or at an event's sample, which are the same at any host block
  size; a piece start with nothing changed does nothing. So the point at any sample is a closed-form
  function of the sample clock and the events, and the output is bit-identical at any block size.
  While x(t) is exactly 0 (never moved, or at the restart's own sample) the point is Position
  itself, through today's code path, bit for bit. *Declined:* advancing the playhead by each
  block's length (block-size dependent rounding); advancing it per grain (the playhead would depend
  on Density).

- **H6. Scan Speed is a route target and a per-voice target; Scan Edge and Scan Track are layer
  targets only.** LFO → Scan Speed wobbles a tape; Velocity, Timbre or Pressure → Scan Speed on a
  Voice layer gives each voice its own speed (sub-project 1's per-voice pass: the route's offset in
  v adds before the clamp to [−1, 1], then the cubic law). Scan Edge and Scan Track are valid targets
  as every stepped layer parameter is, layer-wide, never per voice. Sub-project 2's per-note
  parameter modulation lists Scan Speed among its per-note slots (whichever lands second adds it,
  §7). *Declined:* Scan Edge per voice (no gesture asks for it).

- **H7. Scan Track (Off by default) makes a voice's speed follow its note, as tape varispeed.** On a
  Voice layer, a voice's speed is multiplied by `exp2Det(notePitch / 12)`, where notePitch is the
  note's own pitch in semitones from middle C: (key − 60) through the tuning, plus its bend, plus its
  tuning expression (sub-project 1's §4.5 terms; not the layer's Pitch or its routes). So C4 plays
  at Scan Speed, C5 at twice it, and an MPE slide speeds the scan with the pitch. The product is
  clamped to ±64×. A Cloud layer ignores it (its grains draw their keys at random; there is no one
  note). *Declined:* no tracking (a Key → Scan Speed route is linear in the cubic's domain, not a
  ratio, so octaves don't double the speed); tracking the voice's total pitch (the layer's Pitch,
  the formant or transposition, would then change the speed too).

- **H8. Spray and Smear act around the moving point; the grains read forward.** Wherever a grain's
  draw used Position, it uses the playhead's point: Sample mode `clamp(point + spray × bipolar(u),
  0, 1)` (or with the spray in seconds, the point with the spray carried as today), a ring's lag
  `point + spray × uniform01(u)`, Smear's jitter added after, all as today. "Never crossing the
  head" is the ring's admission clamp's, unchanged. A negative speed moves where grains start, not
  how they read: each grain reads forward at its own rate.

- **H9. On a Spectral layer the playhead moves the smear's analysis point.** Spectral grains read no
  Position (they draw a lag behind the smear's ring head), so a voice's own playhead changes nothing
  of theirs. The smear analyses the material at the layer playhead's point, evaluated at each hop's
  own sample. In Voice mode the layer playhead restarts at each voice's start (the newest note, the
  program's P7 rule) and runs at the layer-wide speed (no per-voice routes, no Scan Track): one
  smear, one point. *Declined:* one smear per voice (16 STFTs a layer).

- **H10. Pitch-sync's held anchor (sub-project 3's S4) takes the moving point at each refresh.** At
  Clock Pitch on a ring, a refresh grain's anchor is `ρ − positionDraw × sampleRate` with
  positionDraw now from the playhead's point, so the anchor moves at each refresh and holds between
  them; on a Sample, Position is read per grain (its SB5), so the point moves grain by grain. No
  code in this sub-project serves it beyond the draw; whichever of 3 and 3b merges second adds a
  test (§7).

- **H11. Saved state and presets.** The three parameters are sound, not instance settings: saved
  with the patch, set by a preset load, defaulted by INIT. No patch-version bump: a state or preset
  without them reads their defaults, today's behavior. The playheads' running offsets are not saved:
  a reopened session starts each playhead at Position. Mutate's default locks include all three (a
  press that sets a pad scanning changes what the preset is), so no Mutate press of a factory preset
  moves.

- **H12. Indices.** Layer indices **0x68 (Scan Speed), 0x69 (Scan Edge), 0x6A (Scan Track)** of the
  range 0x68 to 0x6F agreed for this sub-project (the unused tail of M2 sub-project 4's block), in a
  new list after `GG_LAYER_PARAMS_M2_EXTREMES`; 0x6B to 0x6F stay unused. **No global parameter**:
  the agreed global range 0x40 to 0x47 stays unused.

- **H13. Tempo-locked speed is sub-project 7's (Scan Sync), in M3.** Amended by the director's
  review of 2026-11-09: this call first gave it to M4, and the director has since moved tempo sync
  into M3. Sub-project 7 gives Scan Speed a synced form (a beat length per pass over the window)
  and the lock to the transport, beside the synced modulator and delay rates. Nothing here
  precludes either: the rate is one number per stream.

## 3a. The builder check's rulings (Claude's, binding on the plan)

- **HB1. Where the playhead lives.** A small value type, `gg::Playhead`
  (`engine/include/gg/grain/Playhead.h`, header-only, `noexcept`, no allocation), held by every
  `CloudScheduler` (so by the Cloud stream and each voice stream). It keeps `x0`, `tau0` (int64),
  the rate `r`, the edge mode and a `restartPending` flag. Every place that sets the scheduler's
  `onsetForced_` (`reset()`, `forceNextOnset()`, and `setHeldKeys`' first key after none) sets the
  restart flag too (H4: wherever the Cloud onset is forced; a voice's `startVoice` resets its
  scheduler), and `CloudScheduler::restartPlayhead()` sets it alone (HB5). The Engine calls
  `scheduler.syncPlayhead(t, geometry)` for the Cloud stream and every spawning voice at the start
  of every `processBlock` (whether or not the stream schedules), before any grain is drawn:
  - restart pending: `x0 = 0`, `tau0 = t`, `r = r'`, the edge mode taken, the flag cleared;
  - else if the new rate `r' = params.scanSpeed × geometry.unitsPerSecond / sampleRate` or the edge
    mode differs bit for bit from what the last rebase used: `x0 = canon(x0 + r × (t − tau0))`,
    `tau0 = t`, `r = r'`, the edge mode taken;
  - else nothing. **The window is never a rebase trigger:** a ring's window can grow sample by
    sample (the last splice, selected while the head advances), so a window trigger would rebase at
    every sub-block start, which depends on the host's blocks. Canon reads the window at the
    rebase's own sample, which is an event's.
  `canon` is: Wrap, `x − L × floor(x / L)` over the window length L; Bounce, the same over 2L; Stop,
  `clamp(P + x, lo, hi) − P` with P the stream's Position at t. (Wrap and Bounce reduce only to keep
  the offset small; the fold is periodic, so the point is the same up to rounding, which is
  deterministic. Stop's is what makes a reversed speed leave the edge at once, H3.)
- **HB2. The geometry** the Engine hands each stream at a piece start, from the source in effect:
  - **Sample** (the current slot holds a source of F frames at rate R): `unitsPerSecond = R / (F −
    1)` (a fraction of the source per source second, against `sampleGrain`'s span F − 1),
    `lo = 0`, `hi = 1`; no source or F < 2: 0 (the playhead holds). A new sample changes it, and
    the next piece rebases (HB8);
  - **Live or Resample ring:** `unitsPerSecond = −1` (seconds of lag per second; forward is toward
    the head), `lo = 0`, `hi = W / sampleRate` with W `LiveSource::windowFramesAt` at the piece
    start (read by Stop's canon at a rebase alone; the grains fold with the window at their onset,
    HB3);
  - **Spectral:** the material's (the current sample's, else the live ring's), for the layer
    playhead's smear point (HB5); a Spectral grain's own draw ignores the playhead.
- **HB3. Where the fold happens.** `gg::scanFold(y, lo, hi, edge)` (in `Playhead.h`): Wrap `lo +
  mod(y − lo, L)`, Bounce `lo + tri(y − lo, L)` with `tri(u) = m ≤ L ? m : 2L − m`, `m = mod(u, 2L)`,
  Stop `clamp(y, lo, hi)`; L = hi − lo, and L ≤ 0 gives lo. `mod(a, b) = a − b × floor(a / b)`,
  then a result of b (rounding) taken as 0. Under `GG_FP_CONTRACT_OFF`, as the scheduler is.
  - **Sample mode:** in `CloudScheduler::takeNext`, when `x(t) != 0`, `point = scanFold(position +
    x(t), 0, 1, edge)` replaces `params_.position` in the draw; otherwise the draw is today's,
    untouched.
  - **Spectral mode:** a Spectral grain's draw reads no Position (its lag is the spray's alone), so
    `takeNext` ignores the playhead there; the smear's point is HB5's.
  - **Ring modes (Live, Resample):** the window can grow within a block (the last splice, selected while the head
    advances: `LiveSource::windowFramesAt`'s note), so the fold uses the window **at the grain's
    onset**, which only the Engine knows. When `x(t) != 0`, `takeNext` sets `GrainDraw::scanning`,
    `GrainDraw::scanPoint = position + x(t)` (unfolded) and `GrainDraw::scanEdge`, and positionDraw
    carries the spread alone (the spray and Smear's jitter; Position replaced by 0). `liveGrain`
    then starts from `ρ − (scanFold(scanPoint, 0, W(onset) / sampleRate, scanEdge) + positionDraw) ×
    sampleRate`, under today's admission clamp. With `scanning` false every byte of the path is
    today's. `scanning` is set in ring modes only (Sample mode folds in the scheduler).
  - `GrainDraw` gains `bool scanning{false}`, `uint8_t scanEdge{0}`, `double scanPoint{0.0}`; its
    defaulted `operator==` covers them.
- **HB4. The time base.** `t` in `x(t)` is the grain's exact onset, `onsetSample + onsetFrac` as the
  scheduler holds it (`nextOnset_`, engine samples since reset), and `tau0` an engine sample. The
  difference is taken in double; at 48 kHz a day is 4.1 × 10⁹ samples, far inside double's exact
  integers.
- **HB5. The smear's point.** `SpectralSource::process` takes, in place of its `double position`,
  a small `ScanPosition` (P, x0, r, tau0, the edge, the geometry): each hop at sample t computes
  `x(t)`, and when it is not 0 the point `scanFold(P + x(t), 0, hi(t), edge)`, hi(t) the material's
  bound at the hop (1 for a sample, the live ring's `windowFramesAt(t) / sampleRate`); otherwise P,
  as today. The Engine fills it from the Cloud scheduler. In Voice mode the Engine restarts the Cloud
  playhead at every `startVoice` (H9) with `cloud_.restartPlayhead()`, which forces no onset.
- **HB6. The Processor.** `toCloudParams` sets `CloudParams::scanSpeed = 8 v³` (double, from the
  effective float v) and `CloudParams::scanEdge` from Scan Edge; for a voice, `updateVoices` folds
  in its Scan Speed route offsets (Scan Speed joins `kVoiceTargets` and `isVoiceTarget`) and, with
  Scan Track On on a Voice layer, multiplies by `exp2Det(notePitch / 12)`, clamped to ±64.
  `notePitch` comes from the same terms as the voice's pitch (sub-project 3's `Processor::notePitch`
  seam where it has landed; the key term sub-project 4's tuning owns). Scan Speed, Scan Edge and
  Scan Track join the voice cache key (`kVoiceKeySlots` = `kVoiceTargets.size() + 5`: the Source,
  Bus Split, Level, Scan Edge and Scan Track), so a voice's CloudParams follow them.
  `CloudScheduler::setParams` sanitizes `scanSpeed` (non-finite to 0, clamped to ±64) and the edge
  (out of range to Wrap).
- **HB7. A density or distribution rescale doesn't touch the playhead,** and the playhead never
  moves an onset: the schedule is today's at any speed. The event log's `pos` shows the point.
- **HB8. Sample replacement.** A new sample (`adoptSource`, `setSource`, a capture into the layer)
  changes the geometry's `unitsPerSecond`, so the next piece rebases: the point keeps its fraction
  and moves at the new length's rate.
- **HB9. A source-mode change** (Sample to Live and back) restarts every stream's playhead: an
  offset in a fraction of a file and one in seconds of lag don't correspond, so a switch starts
  reading at Position, as a switch does today. The restart lives in `Engine::process()`'s
  mode-change branch (`restartPlayheads()`, right after `applySourceMode()`), not in
  `applySourceMode`, which a ring's resize also calls and which must not restart a scan.
  **Ruling R9 (Claude's, the build, 2026-11-08):** a Spectral layer restarts the Cloud playhead
  when its **material kind** changes, a sample loaded or removed so the smear's material goes
  from a sample to the live ring or back (checked in `process()` against the previous block's
  `materialRing_`): the point is a file's fraction on one and seconds of lag on the other, the
  same reason. *Declined:* no restart (the old point would read as the wrong unit).
- **HB10. Restarts at rest.** In the FX the Cloud stream's onset is forced when the held set goes
  non-empty, as in the instrument, so the FX's restart (H4) needs nothing new. With Scan Speed 0 and
  the playhead never moved, a restart sets x0 = 0 again, so no golden sees it. With Scan Speed
  returned to 0 after moving, x0 holds the point where it stopped (Scan 0 holds), and the next
  restart returns it to Position.
- **HB11. Real-time safety.** `syncPlayhead` and the fold are a few double operations with no loop
  but the fixed 17 streams; nothing allocates or locks. RTSan and the tripwire tests cover 16
  scanning voices and a scanning Cloud stream in both products.
- **HB12. Cost.** One branch per grain at rest (`x(t) != 0`), the X headline case's only change:
  `gg-bench --cases reference,engine-sample --quick` before and after, the ratio within its noise.
  Scanning adds a multiply, a fold and an Engine fold per ring grain; no new benchmark case.

## 4. Design

### 4.1 Parameters (append-only IDs; the indices agreed across the parallel sub-projects)

| Slot | CLAP ID (A … D) | Display name | Range, default | Kind |
|---|---|---|---|---|
| `ScanSpeed`, `B_ScanSpeed` … | `0x010068` … `0x040068` | Scan Speed (A Scan Speed …) | v in −1 to 1, 0; speed 8v³, shown in × real time | layer, automatable, modulatable, a route and voice target |
| `ScanEdge`, `B_ScanEdge` … | `0x010069` … `0x040069` | Scan Edge | stepped 0 Wrap, 1 Bounce, 2 Stop; 0 | layer, automatable |
| `ScanTrack`, `B_ScanTrack` … | `0x01006A` … `0x04006A` | Scan Track | stepped 0 Off, 1 On; 0 | layer, automatable |

- A new list `GG_LAYER_PARAMS_M3_PLAYHEAD(X)` (`X(ScanSpeed, 0x68, "Scan Speed") X(ScanEdge, 0x69,
  "Scan Edge") X(ScanTrack, 0x6A, "Scan Track")`) **after** `GG_LAYER_PARAMS_M2_EXTREMES(X)` in both
  `GG_LAYER_PARAMS` and `GG_LAYER_PARAMS_SINCE_M1`, so the index column stays ascending. The other
  M3 sub-projects' lists sit between `GG_LAYER_PARAMS_M3` and `GG_LAYER_PARAMS_M2_SOURCES` (their
  indices are below 0x40), so the lists never touch at a merge.
- `detail::isReservedIndexInUse` gains `scope >= 1 && index >= 0x68 && index <= 0x6A` (the
  reserved-block static_assert requires it). `kNextFreeLayerIndex` and `kNextFreeGlobalIndex` are
  untouched (they count indices below 0x40).
- Counts on this branch: `kLayerParamCount` 63 → 66, `kParamCount` 290 → 302; merged with the
  others, the sums. Every hard-coded count follows, as sub-project 1 listed (`tests/support/
  ParamTable.h`, `patch_tests`, `identity_tests`, `params_tests`, `rt_tests`, `EditLayout` and its
  tests, the sweep floors with their reasons), and the comments in `ParamIds.h` that state counts.
- **Scan Speed's display:** the speed `8v³` in × real time, three significant digits, a sign for
  negative values and `0.00x` at 0 (e.g. `1.00x`, `-0.125x`, `0.00195x`); text entry parses a speed
  (with or without the `x`) back to `v = cbrt(speed / 8)`, clamped. ASCII `x`, as the other
  displays use ASCII. Scan Edge shows "Wrap", "Bounce", "Stop"; Scan Track "Off", "On".
- No patch-version bump (H11); `PatchState` reads absent values as defaults (the builder confirms,
  as sub-project 1 did, and records it).
- Mutate: the three join `defaultMutateLocks` (H11).
- The plugin's CLAP flags: Scan Speed `CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE`, as
  Position; the two stepped ones `CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED`, as Source.

### 4.2 The playhead (`engine/include/gg/grain/Playhead.h`, `CloudScheduler`)

`CloudParams` gains `double scanSpeed{0.0}` (× real time, after the law and any tracking) and
`ScanEdge scanEdge{ScanEdge::Wrap}` (`enum class ScanEdge : uint8_t { Wrap, Bounce, Stop }`, in
`Playhead.h`). HB1 to HB4 give the state, the rebase, the geometry and the fold. In one line: the
point at a grain's onset t is `Position` while `x(t) = x0 + r (t − τ0)` is exactly 0, and
`scanFold(Position + x(t), lo, hi, edge)` otherwise.

### 4.3 What the point means on each source

- **Sample:** a fraction of the file, 0 to 1, the window the whole file. 1× moves it by one second
  of the file's own audio per second (at the file's rate, whatever the output rate). A grain that
  starts near the end and reads past it ends with the sample-edge fade (carry-over row 300's rule);
  Wrap's next grains start near 0.
- **Live and Resample rings:** seconds behind the window's end (the head, or a selected splice's
  later mark), 0 to W, W the window (N, the loop's length, a selected splice's span) at the grain's
  onset. On a frozen ring 1× plays the frozen material forward; on a moving ring Scan adds to the
  ring's motion (H1). The admission clamp keeps every grain M frames behind the head and inside the
  oldest frame, as today, so the point at lag 0 (Wrap's or Stop's edge at the head) reads the newest
  admissible material and never crosses the head.
- **Spectral:** the smear's analysis point, in the material's units (H9, HB5).
- **Oscillator** (not built; falls back to the product's default kind, `ParamMap.h`): whatever kind
  it falls back to.

### 4.4 Streams and restarts

H4 and H9. The voice's restart is `startVoice`'s scheduler reset; the Cloud's is every
`forceNextOnset` and `reset`; in Voice mode on a Spectral layer the Engine also restarts the Cloud
playhead at each `startVoice`. A stolen or finished voice's playhead stops with its stream (no grain
reads it). B10 (a layer switched to Voice starts no voices for notes already held) is unchanged.

### 4.5 Modulation and the voice path

HB6 and H6, H7. Scan Speed's per-voice offset adds to its v before the clamp, then the law, then the
track ratio. A route's depth is in v (as every route's is in its target's CLAP domain), so a depth
of 0.5 from Velocity takes a voice from 0 to 1× at full velocity. The layer-wide pass leaves
per-voice routes on Scan Speed out on a Voice layer (B2), as for every voice target.

### 4.6 Determinism and real-time safety

- No randomness: the playhead draws nothing, so no `rng::Key` changes.
- Block-size invariance (H5): rebases only on a changed value at a piece start, which the
  Processor's grid places at the same sample at any block size; every evaluation is closed form at a
  block-size-invariant onset. Tests at blocks 1, 7, 32, 64, 256 and 333 (§4.10) pin it with a
  time-varying route on Scan Speed (an LFO), so the rate rebases every cell.
- 64-bit throughout: positions and offsets in double, times in int64 and double (CLAUDE.md).
- RTSan- and tripwire-clean (HB11).

### 4.7 The at-rest rule

With Scan Speed 0 on every layer (the default), every `x(t)` is exactly 0: `takeNext` takes
today's path, `GrainDraw::scanning` is false, `liveGrain` is today's, the smear reads Position.
**Every existing golden's WAV SHA-256 is unchanged** (the 108 and sub-project 1's eight voice
cases), and their event logs byte for byte (no new event-log field): the at-rest check
(`check-wav-hashes.sh`, last line `baseline cases: 108, changed: 0, gone: 0`) after every DSP task,
and the voice goldens compared, in the final report.

### 4.8 `gg-render`

`params` takes `"A Scan Speed"` … `"D Scan Speed"` (the CLAP value v, so 0.5 is 1×),
`"A Scan Edge"` … (0 Wrap, 1 Bounce, 2 Stop) and `"A Scan Track"` … (0, 1), from the macro tables
(`RenderSpec.cpp` expands the lists); automation and routes take them as any other. No other key.

### 4.9 Goldens (new; each 4 to 6 s at block 256, seed 1; each catches the 16 injected steps or joins the click-injection table with its reason)

- `inst-scan-voice`: layer A Voice over the generated sweep sample, Position 0, Scan Speed 0.5 (1×),
  Wrap; three notes staggered by 0.75 s, each held 2.5 s: each voice plays through from the start
  on its own (the log's `pos` rises per `v`).
- `inst-scan-stretch`: one held note over `loop-48k.wav`, Scan Speed 0.0625 (1/512×), then −0.25
  (−1/8×) from 3 s.
- `inst-scan-edges`: one note over a 2 s built-in sweep sample, Scan Speed 0.75 (3.375×), Scan Edge Wrap, then
  Bounce at 2 s, then Stop at 4 s.
- `inst-scan-track`: Scan Track On, Scan Speed 0.5, notes C3, C4, C5 staggered (0.5×, 1×, 2×).
- `inst-scan-cloud`: the instrument at Mode Cloud, Scan Speed 0.5, two phrases with a gap of
  silence: the playhead restarts at the second phrase's first note (H4).
- `fx-scan-ring`: the FX, layer A Live over a stereo sweep: Freeze at 1 s, Scan Speed −0.5 (−1×,
  backward through the frozen ring) Wrap, Bounce from 3 s; unfrozen from 4.5 s with Scan Speed −0.25
  and Stop (the lag drifts back to the oldest edge and holds).
- `fx-scan-spectral`: layer A Spectral over `loop-48k.wav` as its sample, Scan Speed 0.25 (1/8×):
  the smear's analysis point moves (the WAV and metrics show it; the log shows Spectral grains
  only).

Each new case's spectrogram is looked at before commit; the scanning is checked by its event log
(the `pos` column against the closed form, a test, not just the golden) and its metrics.

### 4.10 Tests

- **Engine** (`tests/engine/playhead_tests.cpp`, `[engine][playhead]`): `scanFold` at each edge,
  including the window's ends exactly, L = 0 and negative offsets; `x(t)` closed form; a sync with
  nothing changed changes nothing (the same bits); a rebase at a speed change continues from the
  same point; Stop holds at an edge and leaves it on a reversal; Bounce's triangle; restart rules
  (reset, `forceNextOnset`, `startVoice`); at Scan 0 the draws equal today's bit for bit
  (`GrainDraw` compared over a stream with and without the new fields set at rest); the ring's lag
  never below 0 or above W at the onset; block-size invariance of a scanning Engine (Sample, Live
  frozen and moving, Spectral) at blocks 1, 7, 32, 64, 256 and 333, output and grain events.
- **Core** (`tests/core/playhead_tests.cpp`, `[core][playhead]`): the law `8v³` into
  `CloudParams::scanSpeed`; a per-voice route on Scan Speed (Velocity) gives two voices two speeds;
  Scan Track's ratio from notePitch (bend included); Cloud's restart at the first key after none in
  the instrument and in the FX; each voice's restart at its note-on; an LFO → Scan Speed route
  renders the same at the six block sizes in both products (`[determinism]` too); the event log's
  `pos` per voice against the closed form for `inst-scan-voice`'s spec.
- **Metrics:** `tests/metrics/comb_tests.cpp` gains a case: Scan Speed 0.05 (1000×) over noise,
  Smear 0 and 1, Smear 1's comb score well below Smear 0's (Smear decorrelates a slow scan as it
  does a slow LFO).
- **Plugin:** the counts; the three parameters' flags and display text round trip; the tripwire
  (`[plugin][rt]`) with 16 scanning voices; a preset load sets them and INIT clears them; Mutate's
  default press leaves them.
- **UI:** `EditLayout` places each slot exactly once (its test); the frame tests' reachability and
  elision checks (short labels if the elision check needs them: "Scan", "Edge", "Track").
- **Render:** `gg-render` parses the twelve names and refuses `"Scan Speed"` bare (only layer A's
  eleven M1 names are bare).
- `scripts/check.sh` (dev, asan; rtsan in CI), the validators, and the X ratio (HB12).

## 5. Label

A new Catch2 tag `[playhead]` (the CTest label `playhead`), listed in CLAUDE.md's Tests section.

## 6. INTENT changes (Part II, each with a §23 row; made by the build's records task)

- **§14** (the engine): the playhead, its law, its edges, its streams and restarts, and what the
  point means on each source; Position as the start point; the layer-parameter count.
- **§17** (UI): the Edit view's placement of the three.
- **§18** (state): the three parameters, saved with the patch and set by a preset load; the running
  offsets not saved; no patch-version bump.
- **§20** (the roadmap): M3's row names sub-project 3b, and says tempo-locked Scan Speed and the
  lock to the transport are sub-project 7's Scan Sync (H13); M4's row doesn't change.
- **§23:** one row for this sub-project (the next free number at merge; #97 on this branch), with
  H0 to H13 recorded as Claude's calls, confirmed by the director's review of 2026-11-09 (H13 as
  amended there).
- The program design's §8 gains row 3b and §10 its open items (none left: this spec settles them).
- CLAUDE.md: the parameter and golden counts, the `playhead` label, the new `gg-render` names.

## 7. Seams with the parallel sub-projects

- **Sub-project 2 (expression):** its per-note parameter-modulation slots (E7, §4.5's list of 23)
  and its extended per-voice targets edit the same `isVoiceTarget` switch and `kVoiceTargets` array
  as this one's Scan Speed: the merge takes the union; `kVoiceKeySlots` follows. Whichever lands
  second adds Scan Speed to the per-note slots and the CLAP per-note flags. Its layer lists (0x32 to
  0x37) sit before 0x40; this one's after 0x67.
- **Sub-project 3 (pitch-sync):** both edit `CloudScheduler::takeNext`'s position draw (its refresh
  keying, this one's point); the merge keeps both: the refresh keys the spray's random draw, the
  playhead supplies the point it spreads around. H10's test (the anchor moves between refreshes
  while scanning) is added by whichever lands second. Its layer index 0x38 is far from 0x68.
- **Sub-project 4 (tuning):** owns the key term of notePitch, which Scan Track reads; nothing else
  is shared.
- **Sub-project 5 (views and presets):** may move the Edit view's placement, add the three to a
  remote-controls page, and write factory presets that scan (an obvious set: a 1× player, a slow
  stretch, a tape-tracked keyboard).
- **All:** `kParamCount` and the tables merge by addition; §23 rows renumber to the next free number
  at merge; CLAUDE.md's counts are settled at merge.

## 8. Left to the builder

- The exact Edit view slot (beside Position, as the frame tests allow), and the short labels if the
  elision check needs them.
- Each golden's exact notes and Position, within §4.9's description, so each catches the 16 injected
  steps or joins the table with its reason.
- Whether `windowFramesAt` at the piece start is the right Stop bound in every window case (HB2); if
  a case shows otherwise, the builder records a ruling and the test that shows it.
