# M3 sub-project 7: tempo sync of rates

| | |
|---|---|
| **Status** | Reviewed, 2026-11-09. Written 2026-11-07 without the director, at the director's request ("keep building all of the sub-projects as parallel as possible and merging them as able to. progress as much as you can without input"), each forced choice as Claude's provisional call (R0 to R15, §3) with the alternatives declined; the builder check's rulings are in §3a (RB1 to RB20). **The director's brainstorming of 2026-11-09 confirmed every call but R3, which the director overruled:** one stepped Sync switch per modulator per layer, in place of the one Mod Sync ladder (R3, amended 2026-11-09, with R11, R12, R14 and every place the ladder appeared) |
| **Program** | `docs/superpowers/specs/2026-11-07-m3-program-design.md` (§3's "rhythmic" focus, §8, §9); this sub-project is new (R0) |
| **Why** | The director, 2026-11-07, on the competitor comparison (`competitor-gaps.md`, gap 6): "no i think this all needs to happen in m3". Every hardware granular instrument compared runs its modulators, its delay and its looper in note divisions; ours run in Hz and seconds alone |
| **Builds on** | sub-project 1, `docs/superpowers/specs/2026-11-07-m3-voice-design.md` (on `m3-voice`, draft PR #37); M2 sub-project 1's transport sources (`gg/core/ModSources.h`); M2 sub-project 2's ring (`docs/superpowers/specs/2026-10-30-m2-sources-design.md` §6.1, §6.4); M2 sub-project 4's delay (`gg/core/Delay.h`); sub-project 3b's playhead, `~/.local/share/giga-grains/worktrees/gg-m3-sp3b-playhead/docs/superpowers/specs/2026-11-07-m3-playhead-design.md` (H5, H13, HB1 to HB3), for Scan Sync alone |
| **Branch** | `m3-sp7-temposync` (from `m3-voice`), worktree `~/.local/share/giga-grains/worktrees/gg-m3-sp7-temposync` |
| **Indices** | layer **0x6B to 0x6F** (the five modulator Sync switches; the director's assignment of 2026-11-09, in sub-project 3b's block, which its H12 leaves unused) and **0x7D, 0x7E** of the block 0x7C to 0x7F (0x7C and 0x7F unused); global **0x48 to 0x4F** (uses 0x48, 0x49); `rng::Field` **0x48, 0x49** |
| **Runs beside** | sub-projects 2 (expression), 3 (pitch-sync), 3b (playhead), 4 (tuning), 5 (views and presets), 8 to 11, in parallel (§8 lists the seams) |

## 1. Intent

The layer's modulators, the global delay, the live ring's loop and the playhead's scan lock to the
host's tempo when the user asks: an LFO in eighth-note triplets, a random step every sixteenth, a
step sequencer on the sixteenths, a dotted-eighth delay, a two-bar loop that holds on the bar line,
a sample scanned once every two bars in step with the song. A synced value is a **closed-form
function of the host's beat position** at each control boundary, so a host loop plays the same
modulation each pass, a bounce from bar 17 sounds as playback from bar 17 does, and an offline
render pins every case at any block size.

The capabilities are generic to tempo-aware instruments (synced rates in straight, dotted and
triplet values; synced delay times; loop lengths in beats or bars; quantized punch-in and out). No
product's names, labels or layout are copied.

**At rest nothing moves:** with every sync switch Off (the defaults) every golden keeps its WAV
SHA-256 and its event log byte for byte (the 108 M2-era cases, sub-project 1's voice cases and
every sibling's that has merged).

## 2. Scope

**In:**
- **the sync clock** (§4.2): one closed-form beat line per instance, anchored from the host's
  transport, relocked only at a discontinuity (a loop or a jump), a tempo change or a play-state
  change, with the stop policy (R2); INTENT §15's "internal monotonic sample clock" that "detects
  loops and jumps from discontinuities";
- **the division table** and its display and parsing (§4.3);
- **the modulator Sync switches** (per layer, one per modulator: LFO 1 Sync, LFO 2 Sync, Random 1
  Sync, Random 2 Sync, Step Sync): each puts its modulator's rate in note divisions (§4.4);
- **Delay Sync** (global): the delay's Time in note divisions (§4.5);
- **Loop Sync** (per layer): Loop Length in beats or bars (§4.6);
- **Ring Quantize** (global): the ring's hold and resume (Freeze) and its splice marks on the next
  beat or bar line: the quantized punch-out and punch-in (§4.7);
- **Scan Sync** (per layer, with sub-project 3b): Scan Speed as one pass of the window per note
  division, phase-locked to the song on a Cloud stream (§4.8);
- Transport Bar's compound-meter fix (R13);
- `gg-render`'s transport changes (§4.10), seven goldens (§4.11), the tests (§4.12), the Edit
  view's placements, the records (§6).

**Out:**
- **M4's**, kept there (INTENT §20): the Sync scheduler (grain onsets on a tempo grid), swing,
  probability, patterns, the grain streams' own transport-stop and loop policies (§22's open
  question, for streams), Size in beats, Density in divisions;
- **the rest of the looper transport, to M4** (R9): record arm, an automatic punch-out after n
  bars, a count-in, and a quantized capture of a running ring;
- envelope times in beats; per-voice LFO phase or a synced LFO restarted per note (LFOs stay
  global, program §3's non-goal);
- MIDI clock, Ableton Link and a tempo control in the standalone (the host owns the tempo; the
  standalone runs on the internal clock at 120 BPM, R2);
- new factory presets that sync and remote-controls pages for the new parameters (sub-project 5's,
  §8).

## 3. Forced choices (Claude's provisional calls, confirmed by the director's review of 2026-11-09; R3 overruled and amended)

- **R0. Tempo sync of rates is a new M3 sub-project, 7, in parallel with the others.** The
  director's decision of 2026-11-07 moves gap 6 into M3. It amends the program design (§8 gains a
  row) and INTENT §20 (M3's row names it; M4's row keeps the grid scheduler and gains the looper
  transport's remainder, R9), with a §23 row.

- **R1. One sync clock, a closed-form line relocked only at a discontinuity.** The Processor keeps
  `SyncClock`: an anchor (a grid boundary `a`, the beat `βa` there), the tempo T and a bar
  geometry. The beat at any boundary b is `β(b) = βa + (b − a) × T / (60 × sr)`, evaluated in
  closed form, never accumulated per cell. Each host transport (one per `process` call, as the
  plugin already passes it) is an observation: at the call's first boundary the clock predicts the
  host's beat at the call's start and keeps its anchor when the host agrees within one cell's worth
  of beats at an unchanged tempo; it relocks at a discontinuity beyond that (a loop or a jump), at
  a tempo change (a continuous rebase: the line bends, the beat never jumps) and at a play-state
  change. So a steady host transport, sent every call, anchors the clock once and leaves every
  synced value bit-identical at any block size; `gg-render`'s transport pins every change (§4.10).
  M2's Transport Beat, Bar and Playing sources keep their own per-cell accumulator, untouched, so
  `fx-mod-transport` keeps its hash (RB20 records the later merge). *Declined:* taking the host's
  position at every call (its rounding and its block-start offset depend on the host's block size,
  which is Transport Beat's flaw today: RB20's row (a)); trusting host loop flags (§15
  forbids it); moving the Transport sources onto the clock now (moves `fx-mod-transport` by
  rounding, for no feature).

- **R2. Stopped, the clock runs on at the last tempo; play relocks.** While the host's transport
  is stopped the clock keeps running from where it stopped at the host's last tempo (the host's
  stopped position is ignored), so a synced patch still moves when auditioned with the transport
  stopped; at play start it relocks to the host's position (synced values jump to the song's). With
  no host timeline at all (`gg-render` without `transport`, a standalone without one) the clock
  runs from beat 0 at reset, at the host's tempo if it gives one, else 120 BPM. *Declined:* holding
  while stopped (a synced LFO patch sounds static whenever the transport is stopped, which is most
  of sound design); a user policy parameter now (§22 leaves the stop policy to M4 for the grain
  streams; M4 may add one Stopped setting that covers these sources too, and nothing here precludes
  it).

- **R3. The parameters: seven per layer, two global, all stepped switches** (the director's
  decision of 2026-11-09, overruling the provisional call). Per layer: one **Sync** switch per
  modulator, **LFO 1 Sync**, **LFO 2 Sync**, **Random 1 Sync**, **Random 2 Sync** and **Step
  Sync** (each Off, On), then **Loop Sync** (Off, Beats, Bars) and **Scan Sync** (Off, On). Global:
  **Delay Sync** (Off, On), **Ring Quantize** (Off, Beat, Bar). Each modulator syncs on its own, so
  one layer can run a synced LFO beside a free one, or a synced Step beside free Randoms, without
  borrowing another layer's modulators. The five switches take the layer indices 0x6B to 0x6F,
  which sub-project 3b's H12 leaves unused in its block (R14). *Declined:* **one Mod Sync ladder
  per layer** (Off, Step, Step + Random, All, from least to most locked: Claude's provisional call,
  one index, overruled by the director: a layer that wanted a free LFO beside a synced one had to
  read another layer's modulators); one switch per modulator shared by every layer (five globals,
  but a switch in layer B's Edit panel would change layer A's); combination enums ("LFO 1 +
  Random 2").

- **R4. The division sits on the rate knob, snapped at a 120 BPM reference.** Synced, a rate knob
  keeps its value and range; its value picks the note value nearest, in log, to the length its Hz
  would have at 120 BPM (`D_ref = 2^(1 − v)` beats for v in log2 Hz), from a table of straight,
  triplet and dotted values from 1/64T to 32/1 (§4.3). So every default lands on a straight value
  (LFO 1's 1 Hz on 1/2, LFO 2's 0.25 Hz on 2/1, Random 1's 2 Hz on 1/4, Random 2's 0.5 Hz on 1/1,
  Step's 4 Hz on 1/8; the delay's 375 ms on 1/8.), and at 120 BPM a synced default sounds like the
  free one. The knob shows the division ("1/8T", "1/4.", "2/1") while synced. *Declined:* a separate
  division-type switch per rate (an index each, and one type for every rate it covers); equal bands
  over the knob's travel (the defaults land on arbitrary values).

- **R5. Synced modulators are closed forms of the beat.** With division D beats and the cell's
  beat β:
  - **LFO:** phase `frac(β / D)`, the shape as today;
  - **Random:** target k is `uniform01(hash({seed, layer, 0, ModRandom1Synced or
    ModRandom2Synced, k}))` with `k = floor(β / D)` (two's complement into the key's uint64);
    Smooth s glides from target k − 1 to target k over the first s of the period along a
    smoothstep, `d(k−1) + (d(k) − d(k−1)) × g(min(1, φ / s))`, `g(x) = x²(3 − 2x)`, φ = `frac(β /
    D)`; s = 0 jumps. Since s ≤ 1 every glide ends inside its period, so the output is exactly
    continuous;
  - **Step:** `steps[k mod length]`, k as above, the mod Euclidean (a pre-roll's negative beats
    count down from the last step).
  So a host loop replays the same values at the same song position, random draws included. A
  division change jumps the phase (the song position's phase at the new division); a tempo change
  bends it without a jump; leaving sync continues the free accumulator from the synced state, so a
  switch to Off never jumps (RB7). *Declined:* phase continuity across a division change (the
  modulator would leave the song position until the next relock); the free mode's exponential
  glide (history-dependent, so a jump would change the output for a time constant after it, and a
  closed form of it is discontinuous at period ends).

- **R6. Delay Sync snaps Time to a note value up to 1/1; past the line's 2 s it halves.** The
  delay's synced time is `D × 60 / T` seconds; above `Delay::kMaxSeconds` (2 s) it is halved until
  it fits, so it stays on the grid (1/1 at 60 BPM plays as 1/2). A tempo change moves the target,
  and the read glides there at the delay's own rate (a tape bend), as a Time automation does.
  Stop, loop and jump change nothing. *Declined:* a longer line (more memory per instance and a
  prepared-size change under every delay golden; nothing at 120 BPM needs it); clamping at 2 s
  (off the grid).

- **R7. Loop Sync counts in beats or in bars of the host's meter.** Synced, Loop Length's value,
  read at the 120 BPM reference (`b_ref = 2 × 2^v` beats for v in log2 seconds), picks:
  **Beats**, 1/4 or 1/2 beat below one beat, else a whole number of beats; **Bars**, a whole number
  of bars (`round(b_ref / 4)`, at least 1), each the host's bar (§4.2's `barBeats`, 3 beats in 3/4
  and 6/8). The loop is that many beats at the current tempo, clamped as today to [50 ms, the
  ring's length]. A tempo or meter change is a window change at its boundary (M2's SP-T12aa: the
  grains in flight finish in their old span). Stop, loop and jump change nothing (the alignment
  comes from Ring Quantize, R8). Loop Sync is a ring parameter, like Loop Length (R12).
  *Declined:* the division table for loops (no 5-bar or 7-beat loop; triplet loops nobody asks
  for); counts of quarter-beats (the knob's large values land on 15.75 beats).

- **R8. Ring Quantize defers the hold, the resume and the splice mark to the next line.** While the
  host plays and the clock is locked to it, a change of the ring's freeze request (the layer's
  effective Freeze, once armed, M1-ac) waits for the next beat or bar line t_q:
  - **a hold (punch-out)** starts its 20 ms freeze ramp at the last boundary that lets the head stop
    at or just before the line: `b = 32 × floor((t_q − R) / 32)`, R the ramp's samples, for the
    first line with `t_q − R` at or after the request's boundary. The head stops in
    `[t_q − 31, t_q]`, so the held loop ends on the line and no downbeat after it leaks in;
  - **a resume (punch-in)** applies at `32 × ceil(t_q / 32)` for the first line at or after the
    request: recording restarts on the line (up to 31 samples after it);
  - **AddSplice** waits for `32 × ceil(t_q / 32)` and marks the frame written at t_q (its `value`
    plus the wait, in seconds behind the head);
  - a request reversed before its boundary cancels; a relock while one pends recomputes its line.

  Unquantized, always: the FX's note freeze (notes are already timed by the player), AddSpliceAt
  (the editor's click names a frame), ClearSplices, captures, Loop, Overdub and a state load. With
  the host stopped or no timeline, every action applies at once. *Declined:* quantizing every ring
  parameter (Loop and Overdub act on a running window with no seam to place); quantizing the note
  freeze (a player's timing would be moved).

- **R9. What of the looper transport comes here.** M2 sources deferred "a looper transport (record
  arm, punch in, quantized loop start)". The ring always records, so the useful looper gestures are
  "hold the last n bars, on the bar" and "resume on the bar": Loop Sync Bars plus Ring Quantize
  give both, and splices on the grid slice the held loop rhythmically. A capture of a held ring is
  already exact, so a quantized hold then a capture is a bar-exact capture. **To M4:** record arm,
  an automatic punch-out after n bars (the Freeze parameter would no longer say what the ring
  does), a count-in, and a quantized capture of a running ring (a capture ending at a past line
  needs a reader of an older span). *Declined:* building the automatic punch-out here with a hidden
  state beside the Freeze parameter.

- **R10. Scan Sync: one pass per note division, phase-locked on a Cloud stream (with 3b).** Synced,
  Scan Speed's magnitude picks the pass length: `D_ref = 4 / (8|v|³)` beats (so 1× is one pass of
  the window per whole note, 1/2× per 2/1, 2× per 1/2), snapped to straight and dotted values from
  1/8 to 4096/1 (§4.3); v's sign is the direction; v = 0 holds (at rest). The window is the
  playhead's own (3b HB2: the whole sample, or the ring's window).
  - **A Cloud stream's playhead is phase-locked to the song:** its offset is `x = g × U × β / D`
    (U the window's length in Position's units, g = +1 on a sample and −1 on a ring, where forward
    is toward the head), folded at the edges as 3b's (Wrap, Bounce, Stop). At song beat 0 the point
    is Position; a host loop or jump moves it with the song; 3b's Cloud restarts (H4) are ignored,
    since the song position is the restart. A two-bar loop sample at Scan 2/1 plays in step with
    the bars at any tempo, after any jump.
  - **A voice's playhead is rate-locked:** it restarts at its note-on from Position (3b H4) and
    moves one window per D beats at the current tempo; Scan Track (3b H7) multiplies on top (an
    octave up halves the pass, still on the grid). Stop, loop and jump don't touch it.
  - The Spectral smear's point (3b H9) follows the layer playhead, so it is phase-locked too.

  *Declined:* rate-locking the Cloud stream (a loop sample would drift off the bar after a host
  jump); a pass length relative to the material (the window's length at 120 BPM over the speed:
  a 2-bar loop recorded at 100 BPM, 4.8 s, snaps to a triplet value, and the Processor would need
  the engine's geometry).

- **R11. The sync switches are not route targets, and the synced values are not modulation
  targets of their own.** A modulator's Sync switch changes how that modulator steps, so a route
  to it would make a source's timing depend on a source (the matrix's one-pass rule, INTENT §14):
  the five join the modulators' controls. Loop Sync, Scan Sync, Delay Sync and Ring Quantize are
  switches of a time base, which nothing modulates usefully. All nine are automatable and stepped,
  not modulatable.
  The rate knobs keep their flags: host automation and CLAP modulation move them, and synced they
  snap.

- **R12. State and presets.** No patch-version bump: a state or preset without the new values
  reads their defaults (Off), today's behavior. The five modulator Sync switches, Scan Sync and
  Delay Sync are sound: saved with the patch, set by a preset load, Off at INIT, and in Mutate's
  default locks (a press that syncs a modulator changes what the preset is). **Loop Sync is a ring parameter** (`isRingParam`):
  a preset load keeps it, INIT resets it (INIT keeps Ring Length alone), Mutate locks it, and a
  preset source may not set it (`--pack-preset` exits 2, as for Loop Length). **Ring Quantize is an
  instance setting:** a preset load and INIT keep it, as they keep the MIDI settings, and Mutate
  locks it. The clock's running state is never saved: a reopened session takes the host's next
  transport.

- **R13. Transport Bar counts the host's bar, not tsig_num quarter notes.** Today `barPhase()`
  divides the quarter-note position by the time signature's numerator, so in 6/8 a "bar" is six
  quarter notes (two bars). It becomes `frac((β − barOffset) / barBeats)` with `barBeats = num × 4
  / denom` and `barOffset = mod(bar_start, barBeats)` from the host (0 without one): the same bar
  the clock uses, so Transport Bar and Bars agree. In 4/4 with no offset the expression is today's
  bit for bit (`β − 0.0` is β; `/ 4.0` as now), and no golden has another meter. *Declined:*
  leaving two definitions of a bar in one product.

- **R14. Indices** (amended with R3, 2026-11-09). Layer **0x6B LFO 1 Sync, 0x6C LFO 2 Sync, 0x6D
  Random 1 Sync, 0x6E Random 2 Sync, 0x6F Step Sync**, the director's assignment: the tail of
  sub-project 3b's block 0x68 to 0x6F, which 3b's H12 (0x68 to 0x6A) leaves unused. They form a
  list of their own, after `GG_LAYER_PARAMS_M2_EXTREMES` and 3b's list (0x68 to 0x6A) and before
  sub-project 8's (0x70). Layer **0x7D Loop Sync, 0x7E Scan Sync** of the block 0x7C to 0x7F agreed
  for this sub-project, in a second list after every other layer list (its indices are the scope's
  highest); **0x7C and 0x7F stay unused** (0x7C was the ladder's). Two lists, since the index
  column must stay ascending and the siblings' lists of 0x70 to 0x7B fall between. Global **0x48 Delay Sync, 0x49 Ring Quantize** of
  the block 0x48 to 0x4F; **0x4A to 0x4F stay unused**. New `rng::Field` values
  **`ModRandom1Synced = 0x48`, `ModRandom2Synced = 0x49`**: a synced draw's index is a song-position
  period, not a count from reset, so it gets its own field and never correlates with the free
  sequence.

- **R15. `gg-render` pins the transport's changes.** `transport` gains an optional
  `timeSignature`, `barStart` and a time-ordered `changes` list (a tempo, a play state, a locate or a
  meter at a time). `gg-render` keeps setting the transport once before the render (so
  `fx-mod-transport` keeps its hash) and sets it again only at each change's frame, where it cuts
  its calls, so every change lands on the same sample at any block size. *Declined:* setting the
  transport every call as hosts do (moves Transport Beat and Bar renders by their block-start
  offset; the clock's steady path is pinned in process instead, §4.12).

## 3a. The builder check's rulings (Claude's, binding on the plan)

- **RB1. Where the clock lives.** `gg::core::SyncClock` (`core/include/gg/core/SyncClock.h`,
  header-only, `noexcept`, no allocation), owned by the Processor beside `Transport transport_`.
  `Processor::setTransport` keeps feeding `TransportSource` exactly as today and also hands the
  observation to the clock (`observe(host, s)`, s = the call's first sample, absolute). The clock
  resolves a pending observation at the first boundary b ≥ s, inside `stepSourcesAt(b)` **before**
  the modulators step, so every synced source at b reads the resolved line. Latest observation
  wins: a call that ends before its first boundary leaves its observation pending, and the next
  call's replaces it (1-sample blocks resolve the last one at the boundary, extrapolated from its
  own s).
- **RB2. The arithmetic,** under `GG_FP_CONTRACT_OFF`, in double:
  - `k = T / (60.0 * sampleRate)` beats per sample, recomputed only when T changes;
  - `beatAt(t) = beatA + static_cast<double>(t - a) * k` for any int64 t (extrapolation backward is
    allowed: an observation's s may precede a pending anchor);
  - a relock at b from host beat h observed at s: `a = b`, `beatA = h + static_cast<double>(b - s)
    * kHost` (the host's tempo);
  - a tempo rebase at b: `beatA = beatAt(b)` with the old k, `a = b`, then T and k take the host's.
  The tempo is sanitized: non-finite or ≤ 0 keeps the previous; otherwise clamped to [1, 1000] BPM.
- **RB3. The steady test.** At resolution, with the host playing and the clock locked:
  `|h − beatAt(s)| ≤ 32 × k` (one cell of beats at the clock's tempo) **and** the host's tempo equal
  to T bit for bit → steady: nothing changes. A larger difference is a jump (relock, `epoch++`); an
  equal position with a different tempo is a tempo rebase (`epoch++`). CLAP's fixed-point beat
  (2⁻³¹ beats) and a host's own rounding sit far inside a cell; a host whose clock drifts relocks
  when the drift passes a cell (a step of under 0.7 ms at 48 kHz).
- **RB4. Play state and timeline.** Stopped → playing: relock at the boundary (always, whatever the
  positions). Playing → stopped: `a = b`, `beatA = beatAt(b)`, `internal = true`: the line runs on
  (R2). While stopped, observations update the tempo (a rebase) and the meter, never the position.
  The first observation after a reset takes the host's position whatever its play state (a
  stopped host's position seeds the internal line, so it runs on from the song's place). A pending
  quantized action meets a stop (or a loss of the timeline) at its resolution: it applies there,
  at once. `hasBeats` false: the clock stays internal, taking the host's tempo when given. Reset (and
  construction): `a = 0`, `beatA = 0`, T = 120, internal, `locked = false`, epoch 0, meter 4/4 with
  offset 0. `bool locked()` is "anchored to a playing host" (Ring Quantize's condition, R8).
- **RB5. The meter.** `barBeats = num × 4 / denom` (CLAP's `tsig_num`, `tsig_denom`; 4/4 without
  them), `barOffset = mod(bar_start, barBeats)` (0 without `bar_start`); bar lines are the beats
  `barOffset + m × barBeats`. A meter change at a resolution is `epoch++` (no position change). The
  plugin's `Transport` gains `int beatUnit{4}` and `double barStart{0.0}`, filled from
  `tsig_denom` and `bar_start / CLAP_BEATTIME_FACTOR` when their flags are set.
- **RB6. Which boundaries apply.** A boundary where the clock's epoch changed applies as a group
  (empty if no event pends) **only while some tempo-dependent feature is engaged** (any layer's Loop
  Sync or Scan Sync, or Delay Sync), so its tempo reaches the delay, the window and the playhead at
  that boundary; a quantized action's boundary (R8) always applies. With every switch Off no
  boundary is added, so no ramp is retargeted and the pieces are today's. (The modulator Sync
  switches need nothing: the modulators step every boundary already, and routes from them are
  time-varying.)
- **RB7. Leaving and entering sync.** Each synced step writes the free state from the synced one
  (LFO: `phase_ = φ`; Random: `phase_ = φ`, `target_` and `out_` the synced target and output;
  Step: `phase_ = φ`, `k_ = k mod length`, Euclidean), so the next free step continues without a
  jump; the free Random keeps its own k counter (keyed by its own field). Entering sync jumps to the
  song's value, by R5.
- **RB8. The division tables** (`core/include/gg/core/SyncDivisions.h`, constexpr literals):
  - **Note values** (the modulator Sync switches, Delay Sync): for n = −4 … 7, the beats `(2/3) × 2^n` (triplet),
    `2^n` (straight) and `1.5 × 2^n` (dotted), 36 entries sorted ascending (the triplet of 2^(n+1)
    falls between 2^n and its dotted). The Delay table is its prefix up to 4 beats.
  - **Pass lengths** (Scan Sync): `2^n` and `1.5 × 2^n` beats for n = −1 … 14, sorted: 1/8 (the
    pass at Scan Speed's top, 8×) to 4096/1.
  - **Snapping:** the nearest entry in log, decided without logs: entry i is chosen when
    `e(i−1) × e(i) ≤ D_ref² < e(i) × e(i+1)` (a tie goes to the longer), the products constexpr
    literals; `D_ref = exp2Det(…)` from the knob (§4.3); below the first or above the last entry,
    the end entry.
  - The modulator Sync switches: each is Off 0 or On 1, and On syncs its own modulator alone
    (LFO 1 Sync LFO 1, …, Step Sync Step); the five are independent (R3, amended 2026-11-09).
- **RB9. Display and parsing.** The rate knobs' text depends on another parameter, so the plugin's
  `value_to_text` and `text_to_value` (and the editor's, through the same formatter) consult the
  rate's own switch (a modulator rate its modulator's Sync switch; Loop Length the layer's Loop
  Sync; Scan Speed its Scan Sync; Delay Time the global Delay Sync) in its main-thread value
  store; `paramMeta` stays static. When one of those switches changes from any source, the plugin
  requests a main-thread callback and calls `clap_host_params::rescan(CLAP_PARAM_RESCAN_TEXT)`.
  Text is ASCII: `1/64` … `32/1`, a `T` suffix for a triplet, a `.` for a dotted value (`1/8T`,
  `1/4.`); Loop Sync `1/4 beat`, `1/2 beat`, `1 beat`, `3 beats`, `1 bar`, `2 bars`; Scan Sync the
  pass length with a `-` when backward (`2/1`, `-1/1.`). Parsing accepts those strings (and the
  free units while free) and sets the entry's reference value (`v = 1 − log2 D`, and the analogues),
  computed on the main thread.
- **RB10. The quantizer's state** per layer: `{bool pending; bool target; int64_t at;}`. It sits
  after Freeze's arming and before the FX's note freeze: `request = effectiveFreeze && armed`;
  `quantized = quantizer(request)`; `engineFrozen = quantized || (fx && anyKeyHeld)`. A request
  equal to the engine's quantized state clears a pending change. `reset()`, a state load and
  `setBase` of Freeze through a preset load clear it and apply at once. A ring save while a hold
  pends saves the ring as it is (the base says Freeze on; the restore holds it), recorded in the
  state tests.
- **RB11. The pending splice,** per layer, one AddSplice (the latest wins, its `value` kept); it
  applies only when the anchor is the moving head (no splice selected, the ring not frozen), else
  at once. ClearSplices cancels a pending one and applies at once. No queue grows: the fixed state
  is per layer.
- **RB12. Loop Sync's published window.** Where the main thread used the Loop and Loop Length base
  values for a capture's or a save's span (SP-T8b), a Loop-Synced layer uses the loop frames the
  audio thread publishes in `ringMeta(layer)` (the synced length at the last boundary): the main
  thread has no tempo. `toRingParams` takes the clock's tempo and bar geometry.
- **RB13. The Scan seam** (with 3b's `Playhead`): `CloudParams` gains `ScanSync scanSync{}`:
  `{bool on; int64_t anchorSample; double anchorBeat; double beatsPerSample; double passBeats
  (signed, D with v's sign); uint32_t epoch;}`, filled by the Processor from the clock and Scan
  Sync (on only when Scan Sync is On and v ≠ 0). `Playhead` gains `lock(int64_t t, double x0,
  double r, ScanEdge edge)`: sets the line exactly (`x0` reduced by 3b's canon for Wrap and Bounce,
  `tau0 = t`, `r`, the edge; a pending restart cleared). The Engine, at a piece start t, for a
  stream with `scanSync.on`, computes a **lock key** (the epoch, `passBeats` and the edge bit for
  bit, and a source counter bumped on a sample load or replacement, a source-mode change and a
  change of Loop, Loop Length, Loop Sync or Splice) and, when it differs from the stream's last or
  a restart is pending, latches U from the geometry (sample: 1; ring: `hi`, the window at t) and:
  - Cloud: `β = anchorBeat + (t − anchorSample) × beatsPerSample`; `lock(t, g × U × β / D,
    g × U × beatsPerSample / D, edge)`;
  - voice: on its restart `lock(t, 0, g × U × beatsPerSample / D × track, edge)` (track 3b's Scan
    Track ratio, 1 when Off); on a later key change `lock(t, x(t), new r, edge)` (continuous).

  Otherwise 3b's `sync` runs untouched, so with Scan Sync Off every byte of 3b's path is its own.
  A ring's window that grows sample by sample (3b HB1) is never a key: U refreshes at the next key
  change.
- **RB14. The order against 3b.** Scan Sync is this sub-project's last task and needs 3b merged. If
  3b hasn't merged when the rest is done, this branch merges without Scan Sync (0x7E reserved, its
  row absent) and the Scan task follows as its own pull request on the merged 3b.
- **RB15. Cost.** Per cell, per layer, five synced modulators cost a division, a floor and (Random)
  one hash: nothing measurable. The engine changes only under Scan Sync (one branch on `on`). No
  new bench case; `gg-bench --cases reference,engine-sample --quick` before and after the Scan task,
  the ratio within its noise.
- **RB16. Placements** (the frame tests require every parameter reachable; sub-project 5 may move
  them): each modulator's Sync switch in its own panel right after its Rate (LFO 1 Sync in
  `Panel::Lfo1`, LFO 2 Sync in `Panel::Lfo2`, Random 1 Sync in `Panel::Random1`, Random 2 Sync in
  `Panel::Random2`, Step Sync in `Panel::Step`); Loop Sync in the Ring panel after Loop Length; Scan Sync after 3b's Scan cells; Delay Sync in the FX view's Delay panel
  after Delay Time; Ring Quantize in the FX view's MIDI panel (its last panel of instance
  settings). Short labels if the elision check needs them ("Sync", "Quantize").
- **RB17. Real-time safety.** The clock, the tables, the quantizer and the lock are fixed-size
  values and a few double operations; no loop but the fixed layers and streams; nothing allocates,
  locks or calls libm beyond `std::floor`, `std::fabs` and `exp2Det`. `params.flush` is untouched
  (it carries no transport). RTSan and the tripwire tests cover every switch on, a relocking
  transport and a pending quantized hold in both products.
- **RB18. Sample-rate change.** A new activation constructs the Processor, so the clock resets
  (RB4); k follows the new rate.
- **RB19. Voices.** The synced modulators are the layer's (global to its voices, as the LFOs are);
  the per-voice matrix reads them as it reads the free ones. Ring Quantize ignores voices.
- **RB20. Carry-over rows** (new, numbered at merge in `docs/m3-carry-over.md`): (a) Transport Beat
  and Bar take the host's position at each call's start and step from there, so in a live host
  their phase depends on the block size by up to a cell (gg-render, setting it once, is exact);
  moving them onto the clock moves `fx-mod-transport` by rounding, so it waits for a commit that
  says so; (b) whether clap-wrapper's VST3 and AU paths fill `bar_start` and the time signature
  (the close's host matrix checks; without them bars count from beat 0 in 4/4). (The draft's row
  (c), per-modulator sync switches, is gone: the director's R3 builds them.)

## 4. Design

### 4.1 Parameters (append-only IDs; the indices agreed across the parallel sub-projects)

| Slot | CLAP ID | Display name | Values, default | Kind |
|---|---|---|---|---|
| `Lfo1Sync`, `B_Lfo1Sync` … | `0x01006B` … `0x04006B` | LFO 1 Sync (A LFO 1 Sync …) | 0 Off, 1 On; 0 | layer, automatable, stepped; a modulator control (no route targets it) |
| `Lfo2Sync` … | `0x01006C` … `0x04006C` | LFO 2 Sync | 0 Off, 1 On; 0 | as LFO 1 Sync |
| `Random1Sync` … | `0x01006D` … `0x04006D` | Random 1 Sync | 0 Off, 1 On; 0 | as LFO 1 Sync |
| `Random2Sync` … | `0x01006E` … `0x04006E` | Random 2 Sync | 0 Off, 1 On; 0 | as LFO 1 Sync |
| `StepSync` … | `0x01006F` … `0x04006F` | Step Sync | 0 Off, 1 On; 0 | as LFO 1 Sync |
| `LoopSync`, `B_LoopSync` … | `0x01007D` … `0x04007D` | Loop Sync | 0 Off, 1 Beats, 2 Bars; 0 | layer, automatable, stepped; a ring parameter |
| `ScanSync`, `B_ScanSync` … | `0x01007E` … `0x04007E` | Scan Sync | 0 Off, 1 On; 0 | layer, automatable, stepped (Scan task, RB14) |
| `DelaySync` | `0x000048` | Delay Sync | 0 Off, 1 On; 0 | global, automatable, stepped |
| `RingQuantize` | `0x000049` | Ring Quantize | 0 Off, 1 Beat, 2 Bar; 0 | global, automatable, stepped; an instance setting |

- Two new lists, so the index column stays ascending (`validIndexColumn`), in both
  `GG_LAYER_PARAMS` and `GG_LAYER_PARAMS_SINCE_M1`:
  - `GG_LAYER_PARAMS_M3_MODSYNC(X)` (`X(Lfo1Sync, 0x6B, "LFO 1 Sync") X(Lfo2Sync, 0x6C, "LFO 2
    Sync") X(Random1Sync, 0x6D, "Random 1 Sync") X(Random2Sync, 0x6E, "Random 2 Sync")
    X(StepSync, 0x6F, "Step Sync")`), after `GG_LAYER_PARAMS_M2_EXTREMES` (and, once 3b merges,
    after 3b's list of 0x68 to 0x6A) and before every sibling's list in 0x70 to 0x7B;
  - `GG_LAYER_PARAMS_M3_TEMPOSYNC(X)` (`X(LoopSync, 0x7D, "Loop Sync") X(ScanSync, 0x7E, "Scan
    Sync")`), **last** (after every sibling's list in 0x70 to 0x7B).
- The globals append to `ParamSlot` after the last global in effect at merge (`BendRange` on this
  branch), with `kParamIds` `0x000048` and `0x000049`.
- `detail::isReservedIndexInUse` gains `(scope == 0 && (index == 0x48 || index == 0x49)) ||
  (scope >= 1 && ((index >= 0x6B && index <= 0x6F) || (index >= 0x7D && index <= 0x7E)))`
  (scope-specific, since the static_assert checks that every in-use index is used in each scope it
  names; 0x7E with the Scan task). `kNextFreeLayerIndex` and `kNextFreeGlobalIndex` are untouched
  (they count indices below 0x40).
- Counts on this branch: `kLayerParamCount` 63 → 69 (the five modulator Sync switches, Loop Sync),
  `kParamCount` 290 → 316; with the Scan task 70 and 320; merged with the others, the sums. Every hard-coded count follows,
  as sub-project 1 listed (`tests/support/ParamTable.h`, `patch_tests`, `identity_tests`,
  `params_tests`, `rt_tests`, `EditLayout` and its tests, the sweep floors with their reasons), and
  the comments in `ParamIds.h` that state counts.
- `isModulatorControlSlot` (and `isModulatorControl`) admits the five modulator Sync switches
  beside the range check, and `isValidTarget` refuses Loop Sync, Scan Sync, Delay Sync and Ring
  Quantize (R11). `isRingParam` gains Loop Sync. Ring Quantize joins the instance settings a preset
  load and INIT keep (beside `isMidiSetting`; the builder may widen that predicate's name).
  `defaultMutateLocks` gains all nine.
- CLAP flags: `CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED` for all nine.
- No patch-version bump (R12); `PatchState` reads absent values as defaults (the builder confirms,
  as sub-project 1 did, with a test loading an M3-voice state).

### 4.2 The sync clock

RB1 to RB5 in one place. The Processor holds `SyncClock clock_`; its line is `β(t) = beatA + (t −
a) × k`. Each `setTransport` is an observation, resolved at the first boundary at or after the
call's start: steady (keep), jump (relock to the host's position extrapolated to the boundary),
tempo change (continuous rebase), stop (run on), play (relock), meter change. Each relock or rebase
increments `epoch`. At each boundary b the Processor reads `beat = clock_.beatAt(b)` once and every
synced feature at b uses it. Helpers for the features and for M4's scheduler and sub-project 10's
quantized recall: `nextLineSample(Line line, int64_t from)` (the first beat or bar line's sample at
or after `from`, `llround` of the line's exact sample, from the current line), `barBeats()`,
`tempo()`, `locked()`, `epoch()`.

**What each rate does** (R2 and the features' calls):

| | Playing, steady | Stopped | Loop or jump | Tempo change |
|---|---|---|---|---|
| Synced LFO, Random, Step | the song position's value | run on at the last tempo | the new position's value at once | the slope bends; no jump |
| Delay Sync | `D × 60 / T` | unchanged | unchanged | the read glides to the new time |
| Loop Sync | n beats or bars at T | unchanged | unchanged | the window changes at the boundary |
| Scan Sync, Cloud | phase-locked | runs on at the last tempo | the point follows the song | the slope bends |
| Scan Sync, voice | from its note-on, one pass per D | unchanged | unchanged | the slope bends |
| Ring Quantize | the next line | at once | a pending line recomputed | a pending line recomputed |

### 4.3 The division tables (RB8)

| Use | Knob value v | Reference length | Table |
|---|---|---|---|
| Modulator rates (their Sync switch On) | log2 Hz, −6.64 to 5.64 | `D_ref = exp2Det(1 − v)` beats (a cycle at 120 BPM) | note values 1/64T to 32/1 |
| Delay Sync | log2 s, −9.97 to 1 | `D_ref = 2 × exp2Det(v)` | note values 1/64T to 1/1 |
| Loop Sync | log2 s, log2 0.05 to log2 300 | `b_ref = 2 × exp2Det(v)` | Beats: 1/4, 1/2 (below 1 beat, by the geometric means 0.354 and 0.707), else `floor(b_ref + 0.5)`; Bars: `max(1, floor(b_ref / 4 + 0.5))` × barBeats |
| Scan Sync | −1 to 1 (3b's law, 8v³) | `D_ref = 1 / (2 × abs(v)³)` | pass lengths 1/8 to 4096/1, straight and dotted; sign from v |

The note value of D beats: `2^n` is "1/(4 / 2^n)" for n ≤ 2 (1/64 … 1/1) and "(2^n / 4)/1" above
(2/1 … 32/1); its triplet `(2/3)·2^n` and dotted `1.5·2^n` add `T` and `.`. At 120 BPM every M2
default rate and the delay's default land on an entry exactly (R4).

### 4.4 The modulator Sync switches (R3, R5)

`ParamMap.h`'s `lfoParams`, `randomParams` and `stepParams` gain `double divisionBeats` (0 when the
modulator's own Sync switch in that layer is Off). `Lfo::step`, `RandomSource::step` and
`StepSequencer::step` take the cell's beat; with `divisionBeats > 0` they compute R5's closed forms
(and write RB7's free state), else today's code path, unchanged. `Processor::stepSourcesAt` passes
`clock_.beatAt(c)`. The synced Random's key is `{seed, layer, 0, Field::ModRandom1Synced` or
`ModRandom2Synced, static_cast<uint64_t>(k)}`.

### 4.5 Delay Sync (R6)

`delayParams` takes the clock's tempo; with Delay Sync On, `timeSeconds = D × 60 / T`, halved while
above `Delay::kMaxSeconds`. The Processor's existing per-boundary `setTimeSamples` carries it; the
delay's glide does the rest. Off: today's `exp2Of(DelayTime)`.

### 4.6 Loop Sync (R7)

`toRingParams` takes the clock's tempo and `barBeats`; with Loop Sync on, `loopSeconds = beats × 60
/ T` from §4.3's count, then today's clamps. The audio thread publishes the resulting loop frames
for the main thread's captures and saves (RB12).

### 4.7 Ring Quantize (R8, RB10, RB11)

In `Processor::syncFreeze` and the command path. A quantized boundary is registered as one the
Processor must apply (RB6). `nextLineSample` gives t_q; Beat lines are integer beats, Bar lines
`barOffset + m × barBeats`. While `!clock_.locked()` the quantizer passes requests through at once.

### 4.8 Scan Sync (R10, RB13, RB14)

The Processor fills `CloudParams::scanSync` for the Cloud stream and, through `updateVoices`, each
voice (a per-voice route on Scan Speed changes that voice's D); the Engine's per-piece playhead step
takes the lock path of RB13 when `on`, else 3b's `sync`. Scan Sync, Scan Speed's sign and its pass
length join the voice cache key with 3b's slots.

### 4.9 Determinism and the at-rest rule

- **Block-size invariance.** Every synced value is a function of `β(b)` at a grid boundary, and the
  clock's line changes only at boundaries fixed by the transport's observations, which `gg-render`
  places at the same frames at any block size (§4.10). Quantized actions land on boundaries
  computed from the line. Tests at blocks 1, 7, 32, 64, 256 and 333 (§4.12).
- **No adaptive behavior:** the clock reacts to the host's transport only, never to time or CPU.
- **64-bit:** beats, anchors and lines in double and int64.
- **At rest** (every switch Off): the modulators, `toRingParams`, `delayParams`, `syncFreeze`, the
  commands and the playhead take today's paths byte for byte; the clock observes but adds no
  boundary (RB6); R13's Transport Bar expression is today's in 4/4 with no offset. So every existing
  golden's WAV SHA-256 is unchanged and its event log byte for byte: `scripts/check-at-rest.sh` after
  every DSP task, last line `baseline cases: 108, changed: 0, gone: 0`, and the voice and merged
  siblings' goldens compared, in the final report. No new event-log field.

### 4.10 `gg-render`

- `params` takes `"A LFO 1 Sync"`, `"A LFO 2 Sync"`, `"A Random 1 Sync"`, `"A Random 2 Sync"`,
  `"A Step Sync"` … `"D Step Sync"` (0, 1), `"A Loop Sync"` … (0 to 2),
  `"A Scan Sync"` … (0, 1; with the Scan task), `"Delay Sync"` (0, 1) and `"Ring Quantize"` (0 to
  2), from the macro tables; automation takes them as any stepped parameter (`"A Loop Sync"` too:
  only Ring Length is refused in automation).
- `transport` gains, all optional: `"timeSignature": [num, denom]` (num 1 to 32, denom 1, 2, 4, 8,
  16 or 32; default [4, 4]), `"barStart"` (beats, finite; default 0), and `"changes"`, a list
  ordered by strictly ascending `"time"` (seconds, ≥ 0 and < `durationSeconds`), each with at least
  one of `"beat"` (a locate: finite), `"tempoBpm"` (> 0, finite), `"playing"` (bool) and
  `"timeSignature"`. Unknown keys are errors (exit 2).
- **The host model:** the transport is set once before the render, as today. At each change's frame
  `f = llround(time × sampleRate)` `gg-render` ends a call (so a call starts at f) and calls
  `setTransport` with the host's state there: the given beat, or the previous state's beat plus
  `(f − f_prev) × tempo_prev / (60 × sr)` while it was playing (held while stopped); the given or
  previous tempo, play state and meter. A change of `playing` alone at f is a play or stop at f.
- No other key. `--save-state` writes no transport (R12).

### 4.11 Goldens (new; each 6 to 8 s at block 256, seed 1; each catches the 16 injected steps or joins the click-injection table with its reason)

- `fx-sync-mod`: the FX over a stereo 50 to 700 Hz sweep, 120 BPM playing from beat 0, **90 BPM
  from 4.0 s**. Layer A LFO 1 Sync and LFO 2 Sync On; LFO 1 Rate log2 6 (1/8T), Square, to Pitch
  ±12 (a triplet trill); LFO 2 at its default (2/1) to Position. The trill's period follows the tempo change with
  no phase jump.
- `inst-sync-step`: the instrument, one held note over a 330 Hz sine; Step Sync and Random 1 Sync
  On (the other three free); Step
  at its default (1/8), Step Length 6, to Pitch ±12 (a six-step figure across 4/4); Random 1 at
  1/16 (log2 8 Hz), Smooth 50%, to Stereo. 120 BPM, **a host loop at 3.0 s back to beat 2.0**: the
  figure and the random steps replay from beat 2.
- `fx-sync-delay`: the FX over `loop-48k.wav`, Delay Sync On, Amount 60%, Time at its default
  (1/8.), Ping-pong; 120 BPM, **150 BPM from 3.0 s** (the repeats glide from 375 to 300 ms), **stop
  at 5.0 s and play from beat 0 at 6.0 s** (no change: the delay depends on tempo alone).
- `fx-sync-loop`: the FX, layer A Live over a stereo sweep; Loop On, Loop Sync Bars at the default
  Loop Length (1 bar, 2 s at 120 BPM), Overdub 70%, Ring Quantize Bar; Freeze automated on at
  2.7 s (the head stops in [4.0 s − 31 samples, 4.0 s]) and off at 5.1 s (recording resumes at
  6.0 s); a `splice` operation at 1.3 s (marked at 2.0 s).
- `fx-sync-stop`: the FX over a 330 Hz sine, LFO 1 Sync On, LFO 1 at 1/4 (log2 2 Hz) Sine to Pitch
  ±2; playing from beat 0, **stopped at 2.0 s, playing from beat 0 at 3.5 s**: the vibrato runs on
  through the stop and snaps to the song's phase at 3.5 s.
- With the Scan task: `inst-scan-sync`: the instrument at Mode Cloud, one held note over a 4 s
  generated sweep sample, Scan Speed 0.375 (2/1), Scan Sync On, Wrap; **100 BPM, a locate to beat
  0 at 3.0 s**: the point returns to Position with the song. `inst-scan-sync-voice`: Mode Voice,
  the same sample, Scan 0.5 (1/1), three notes off the beat, **80 BPM from 3.0 s**: each voice's
  pass slows without a jump.

Each new case's spectrogram is looked at before commit. The timing claims are pinned by tests
(§4.12), not by the goldens alone.

### 4.12 Tests (tag `[temposync]`, the CTest label `temposync`)

- **The clock** (`tests/core/sync_clock_tests.cpp`, `[core][temposync]`): a steady host observed
  every call at random block sizes (positions computed as a host would, in double, then through
  CLAP's 2⁻³¹ fixed point) never relocks, and `beatAt` at every boundary equals the set-once
  clock's bit for bit; a jump beyond a cell relocks at the call's first boundary to the host's
  position extrapolated; a one-sample jitter doesn't; a tempo change rebases continuously
  (`beatAt(b)` the same just before and after); stop runs on at the last tempo and ignores the
  host's position; play relocks; no timeline runs from reset at 120 or the host's tempo; 1-sample
  blocks resolve the latest observation; reset; the meter (`barBeats`, `barOffset`, `nextLineSample`
  for Beat and Bar, negative beats); tempo sanitizing.
- **The tables** (`[core][temposync]`): sorted, the products monotone, every M2 default and the
  delay's default on its entry at 120 BPM, the ends, display and parsing round-trip for every entry,
  Loop Sync's Beats and Bars counts at their thresholds and in 3/4 and 6/8.
- **The modulators:** each synced form against its closed form over 10,000 cells; a looped
  transport gives the same values at the same beats bit for bit; the synced Random is continuous at
  every period boundary for Smooth in (0, 1]; Step's Euclidean mod over negative beats; leaving
  sync never jumps (RB7); with every Sync switch Off the values equal M2's bit for bit; each
  modulator's Sync switch syncs that modulator alone (a synced LFO 1 beside a free LFO 2 in one
  layer).
- **The delay:** the target `D × 60 / T` with the halving; a tempo change glides; Off is M2's.
- **The ring:** Loop Sync's frames at tempos and meters, clamped; a tempo change is a window change;
  the published loop frames for a capture. Ring Quantize: a hold's head stops in `[t_q − 31, t_q]`
  and a resume starts in `[t_q, t_q + 31]` (asserted on `LiveSource`'s head); a reversed request
  cancels; a relock recomputes; stopped and unlocked apply at once; the FX's note freeze,
  AddSpliceAt and ClearSplices unquantized; a pending AddSplice marks t_q's frame; a save while a
  hold pends (RB10); Quantize Off is M2's bit for bit.
- **Scan Sync** (with the Scan task, `[engine][temposync]` and `[core][temposync]`): the Cloud
  playhead's point against `g × U × β / D` folded, across a jump and a tempo change; a voice's from
  its note-on; restarts ignored on Cloud; Scan Sync Off is 3b's path bit for bit; v = 0 holds.
- **Determinism** (`[determinism]` too): each golden's spec renders the same at blocks 1, 7, 32, 64,
  256 and 333, output and per-layer event lines; in process, a host transport sent every call
  (steady, then a loop, then a stop) against `gg-render`'s set-at-changes form, for specs with no
  Transport-source route: the same bits.
- **Transport Bar** (R13): 6/8 and 3/4 with an offset; 4/4 with none equal to M2's bit for bit.
- **Sweep** (`tests/sweep/`, `[sweep][temposync]`): one case in each product with every switch on,
  the tempo stepped from 1 to 1000 BPM, a host loop every 100 ms and Freeze toggled every 70 ms
  under Ring Quantize Beat: finiteness, the peak bound, no runaway and the goldens' click score
  outside named exemptions, as the harness's other cases (`SweepDriver.h`).
- **Plugin:** the counts; the nine parameters' flags and texts; the rate knobs' synced text and
  parsing through `value_to_text` and `text_to_value`; the rescan request after a switch changes;
  `bar_start` and `tsig_denom` reach the clock; the tripwire (`[plugin][rt]`) with every switch on,
  a relocking transport and a pending hold; a preset load keeps Loop Sync and Ring Quantize and sets
  the other seven; INIT; Mutate's default press leaves all nine.
- **Render:** `gg-render` parses the new names and transport keys and refuses bad ones (exit 2);
  `--pack-preset` refuses Loop Sync and Ring Quantize in a source.
- **UI:** `EditLayout` places each new slot exactly once; the frame tests' reachability and elision
  checks.
- `scripts/check.sh` (dev, asan; rtsan in CI), the validators, and the X ratio after the Scan task
  (RB15).

## 5. Label

A new Catch2 tag `[temposync]` (the CTest label `temposync`), listed in CLAUDE.md's Tests section.
(`sync` alone would also match pitch-sync's cases under `-L`'s regex.)

## 6. INTENT changes (Part II, each with a §23 row; made by the build's records task)

- **§14** (the engine): a "Tempo sync" paragraph: the clock (R1, R2), the division tables (R4), the
  synced modulators in the sources table (R5), Delay Sync (R6), Loop Sync (R7), Ring Quantize
  (R8), Scan Sync (R10); Transport Bar's bar (R13); the layer- and global-parameter counts; the
  `rng::Field` values 0x48 and 0x49.
- **§15** (host robustness): the monotonic-clock bullet "as built": the steady test, the relock,
  the stop policy.
- **§17** (UI): the placements (RB16) and the rate knobs' synced display.
- **§18** (state): the nine parameters, which ones a preset load keeps (R12), no patch-version bump.
- **§19** (verification): `gg-render`'s transport keys (R15) and the seven goldens.
- **§20** (the roadmap): M3's row names sub-project 7; **M4's row** keeps the Sync scheduler, swing,
  probability, patterns and the streams' stop and loop policies, and gains the looper transport's
  remainder (record arm, automatic punch-out, count-in, quantized capture of a running ring; R9);
  sub-project 3b's H13 note (tempo-locked Scan Speed is M4's) is superseded by R10.
- **§22:** "Default behavior of rhythmic streams when the transport stops" stays M4's, with a note
  that synced modulators and the Cloud scan run on (R2).
- **§23:** one row for this sub-project (the next free number at merge), with R0 to R15 as
  Claude's provisional calls confirmed by the director's review of 2026-11-09, and R3 as the
  director overruled it (a Sync switch per modulator per layer; the ladder declined).
- The program design's §8 gains row 7 (below) and §10 its open items (none left).
- CLAUDE.md: the parameter and golden counts, the `temposync` label, the new `gg-render` names and
  transport keys.

Program §8's new row:

| # | Sub-project | Delivers | Exit |
|---|---|---|---|
| 7 | **Tempo sync of rates** | The sync clock (steady, jump, tempo, stop, play); a Sync switch per modulator, Loop Sync and Scan Sync per layer; Delay Sync and Ring Quantize; Transport Bar's meter; `gg-render`'s transport changes | Five goldens (seven with Scan Sync) render the same at six block sizes under tempo changes, loops, stops and locates; a steady host sent every call equals set-once bit for bit; a quantized hold stops the head within 31 samples before the line; every existing golden's hash unchanged; RTSan and the sanitizers clean |

## 7. What is built in what order (for the plan)

1. The clock and the tables (core, tests), `Transport`'s new fields, the plugin's transport fill,
   Transport Bar (R13).
2. The parameters (IDs, metadata, counts, targets, presets, Mutate, the display and rescan).
3. The modulator Sync switches (the modulators).
4. Delay Sync and Loop Sync (and RB12's published window).
5. Ring Quantize.
6. `gg-render`'s transport keys and the five goldens; the at-rest check.
7. The Edit view placements and the frame tests.
8. Scan Sync, after 3b merges (RB14), with its two goldens.
9. The records (§6).

Tasks 3 to 5 are independent after 2; 6 depends on 3 to 5.

## 8. Seams with the parallel sub-projects

- **3b (playhead):** RB13's `Playhead::lock` and `CloudParams::scanSync` are this sub-project's
  additions to 3b's code; 3b's `sync`, `scanFold`, geometry and restarts are used as built. 3b's
  §6 note that M4 gains tempo-locked Scan Speed is superseded (§6 here). Scan Sync's voice-cache
  slot joins 3b's (`kVoiceKeySlots` follows). The five modulator Sync switches take 0x6B to 0x6F,
  the unused tail of 3b's block (the director's assignment, R14): at 3b's merge its list (0x68 to
  0x6A) goes before this sub-project's `GG_LAYER_PARAMS_M3_MODSYNC`.
- **2 (expression):** both edit `isValidTarget` and the per-voice target lists; Scan Speed's
  per-voice route works under Scan Sync by RB13. No per-note slot for the switches. Its MIDI
  settings and this one's Ring Quantize both join the "kept by a preset load" set: the merge takes
  the union.
- **3 (pitch-sync):** its per-layer **Clock** (Free, Pitch) is a different thing from this clock;
  the names don't collide (`SyncClock` in code; "LFO 1 Sync" and the rest in the UI). M4's tempo
  grid may later be a third Clock value, reading `SyncClock`.
- **4 (tuning):** nothing shared.
- **5 (views and presets):** may move the placements, add a "Sync" remote-controls page, and write
  factory presets that sync (an obvious set: a step arpeggio, a dotted-delay FX, a bar looper).
- **10 (scenes):** a quantized scene recall should use `SyncClock::nextLineSample` (and may share
  Ring Quantize's Beat and Bar or take its own setting); the clock is the one source of beat time.
- **8, 9, 11:** nothing shared beyond the counts.
- **All:** `kParamCount` and the tables merge by addition; this list sits last; §23 rows and
  carry-over rows renumber at merge; CLAUDE.md's counts are settled at merge.

## 9. Left to the builder

- Each golden's exact automation times and depths within §4.11, so each catches the 16 injected
  steps or joins the click-injection table with its reason.
- The exact Edit view cells and short labels (RB16).
- Where the published loop frames sit in `ringMeta` (RB12), and whether the main thread's other
  readers of Loop Length (the editor's ring view) need them too.
- Whether clap-wrapper's VST3 and AU transports carry `bar_start` and the meter (RB20 (b)); a
  plugin test pins what the CLAP path gives.
