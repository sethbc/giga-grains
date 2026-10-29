# M1 sub-project 5: the Perform UI (macros, Texture, Feedback and the canvas)

| | |
|---|---|
| **Status** | Approved in brainstorming by the director, 2026-10-27. Built 2026-10-29 (INTENT §23 #58, #59), and amended in place, each marked **Amended**: §1 (decision 3's slot), §3.1 (ruling PU-T2a), §4 (the fixed family, rulings PU-T3a and PU-T13a), §5 (the Processor's pieces, the per-sample guard, ruling PU-T4a), §6.2 (ruling PU-T8a), §6.4 (one peak level, rulings PU-T9a, PU-T9b, PU-T10a), §6.5 (MIT), §7 (the seed hand-off, ruling PU-T5a), §9 (ruling PU-T12a, `inst-texture`) |
| **Milestone** | M1 (Cloud core), sub-project 5 of 6 |
| **Depends on** | Sub-project 1 (FX slice: Processor, parameter model, live ring); 3 (Sources: the instrument's sample lane); 4 (Space: Diffusion) |
| **Authority** | INTENT.md. This spec realizes INTENT §17 for one layer, and adds §14 parameters |

## 1. Purpose

Sub-project 5 turns M1's working engine into an instrument you play from its own window. Its four parts:

- **Eight macros,** the Perform view's controls and the hosts' remote-controls page. Each drives a few parameters along curves.
- **Two new sound controls,** Texture (the grain envelope) and Feedback (regeneration into the live ring).
- **A single-lane canvas** that draws the grains the engine is actually playing (§8.7), with direct manipulation of the scan
  window.
- **Fresh-instance seeds,** a decision the FX-slice spec deferred to this sub-project.

**Success:**
- On a track in Ableton Live 12 or Bitwig Studio, the director shapes a texture from the eight macros alone, and every macro sounds
  good across its whole travel (§8.1).
- The canvas shows where the grains read, how long they are, how loud they are and where they pan, at the engine's real timing.
- Everything stays deterministic and real-time safe. The output is bit-identical at any host block size, with macros, Texture
  and Feedback automated.

**Director decisions** (2026-10-27, in brainstorming):

1. **Macros are relative.** Each macro is a modulation source that offsets its targets from the base values the preset sets
   (INTENT §14's diagram already lists macros among the modulation sources). One writer per parameter.
2. **Texture** sweeps the grain envelope from smooth to gated over the lower three quarters of its travel. In the top quarter it
   adds Space's diffusion.
3. **Feedback** regenerates the wet output into the live ring in the FX. The instrument has no writable buffer until M2's
   resample source, so its eighth macro slot is Stereo until then.

   > **Amended (2026-10-29):** its **seventh** macro slot (Macro 7, Feedback in the FX) is Stereo; the eighth is Level, as
   > §3.2's table has it (INTENT §23 #58).
4. **The eight recipes** in §3, with every depth tuned by ear in the listening check.
5. **The canvas:**
   - the FX lane has a fixed write head at the right edge, with history scrolling left;
   - the canvas sits on top, with one macro row below in remote-control order;
   - a canvas drag edits **base values**, and ghost windows show where modulation moves the grains.
6. **The rest:**
   - the vertical axis is pan;
   - sst-jucegui supplies the controls on the existing JUCE 9 shim;
   - the audio-to-UI path is INTENT §17's;
   - each new instance draws its own seed;
   - Edit, Mod, Mutate, the preset browser and lanes B–D stay in M2.

## 2. Non-goals

- The Edit and Mod views, Mutate, the preset browser, undo/redo UI, tags and favorites (M2).
- Layer lanes B–D, the layer strip and the XY morph pad (M2).
- User-editable macro assignments. They arrive with M2's mod matrix. The state format is versioned, so the assignments can be
  added then.
- Themes beyond one dark theme, and the GPU canvas backend (M2 spike, §23 #26).
- Feedback in the instrument (M2's resample source).

## 3. Macros

### 3.1 The model

- **Each macro is a CLAP parameter.** It is global (scope 0), `AUTOMATABLE | MODULATABLE`, with the value domain 0..1.
- **The effective value** of each target parameter `p` is

  `effective(p) = clamp(base(p) + mod(p) + Σ_macros curve_{m,p}(macro_m))`

  where `macro_m` is the macro's own effective value (base + modulation, clamped to 0..1). This extends the Processor's
  existing `clamp(base + mod)`.
- **Macro events behave like any parameter event.** They take effect at the next 32-sample grid boundary.

  > **Amended by ruling PU-T2a (2026-10-29):** a `setBase` of a macro (params.flush or a state load) applies at once, as a
  > `setBase` of any parameter does, and also jumps the ramps of every target whose effective value it moved (Macro 5's
  > Diffusion, the FX's Macro 7 Feedback and Macro 8 Mix, the instrument's Macro 8 Level), so a loaded state never ramps in
  > from stale values; a `setBase` of Macro 6 is seen as one of Freeze for arming (INTENT §14). The effective value is
  > `clamp(base + mod + Σ)` in one clamp, never two; a −0 base plus a −0 modulation gives +0 once the resting +0 offset is
  > added.
- **Every curve is libm-free:** polynomials, `exp2Det` and `logDet` only (INTENT §14).
- **Curves work in each target's CLAP value domain:**
  - Size, Density and Position are already log2-tapered, so "±N octaves" is linear in their domain;
  - Spray is 0..1 on its exp taper;
  - Pitch is in semitones;
  - Level is in dB.
- **Polarity:**
  - A **bipolar** macro rests at 0.5, and its curve passes through 0 there.
  - A **unipolar** macro rests at 0, and its curve is 0 there.
  - So with every macro at rest, a preset sounds exactly as its base values define it.
- **The recipes are fixed in M1:** a factory recipe per macro and per product, in code. The macro's value is saved in the state
  like any other parameter.

### 3.2 Recipes

`b` = the macro's bipolar position, `2·macro − 1` ∈ [−1, 1]. `u` = the macro's position for a unipolar macro, ∈ [0, 1].
`b⁺ = max(b, 0)`. Every depth is a starting value, tuned in the listening check (§9).

| # | Macro | Polarity | Targets and curves |
|---|---|---|---|
| 1 | Size | bipolar | Size `+3·b` octaves; Density `−1.5·b` octaves, to keep overlap roughly steady |
| 2 | Density | bipolar | Density `+4·b` octaves; Distribution `+0.25·b⁺`: sparse sounds pulse, dense sounds cloud |
| 3 | Pitch | bipolar | Pitch `+24·b` semitones, continuous |
| 4 | Spray | bipolar | Spray `+0.5·b` of its 0..1 domain; Stereo `+0.3·b⁺` |
| 5 | Texture | unipolar | Texture `+min(u / 0.75, 1)`, sweeping the envelope over the lower three quarters of the travel; Diffusion `+smoothstep(0.75, 1, u)` × its full range in the top quarter (Space owns Diffusion) |
| 6 | Freeze | unipolar, stepped | Freeze `+u`, so effective Freeze is on when base + u > 0.5 |
| 7 | Feedback (FX) | unipolar | Feedback `+0.9·u²`, with finer control at the low end |
| 7 | Stereo (instrument) | bipolar | Stereo `+0.5·b` |
| 8 | Mix (FX) | bipolar | Mix `+0.5·b` |
| 8 | Level (instrument) | bipolar | Level `+12·b` dB. The instrument's Mix is hidden. |

- **Naming:** the host names a macro "Macro Size", "Macro Density" and so on, per product. For the instrument, slot 7 is "Macro
  Stereo" and slot 8 is "Macro Level".
- **Slot 7 changes meaning in M2.** When M2 gives the instrument a resample source, slot 7 becomes Feedback there too. If that
  happens after the first public release, the state format's migration maps the old Stereo value. Before release, nothing is
  needed.
- **Remote controls:**
  - Page 1, "Perform", holds the eight macros in table order.
  - The FX slice's "Cloud" page (Position, Size, Density, Pitch, Spray, Stereo, Freeze, Mix) becomes page 2.

### 3.3 Parameter IDs (append-only, below `0xB00000`)

| ID | Name |
|---|---|
| `0x000011`–`0x000018` | Macros 1–8 (scope 0, global) |
| `0x010009` | Texture (layer A) |
| `0x01000A` | Feedback (layer A; the instrument hides it until M2) |

Diffusion's ID belongs to sub-project 4 (Space).

## 4. Texture

- **A new layer parameter,** Texture: domain 0..1, default `0.0` (Hann, the M0 window). It is `AUTOMATABLE | MODULATABLE`.
- **It drives INTENT §14's envelope fields** (shape, skew, sustain fraction) along one curve. These are the anchor points:

  | Texture | Envelope |
  |---|---|
  | 0.0 | Hann: symmetric raised cosine, no sustain. M0's window, so default output is unchanged |
  | 0.5 | Skewed swell: peak at 70% of the grain, gentle release, no sustain |
  | 1.0 | Gated: raised-cosine attack and release of `max(16 samples, 5% of the grain)` each, flat sustain between |

  Between anchors the envelope parameters interpolate linearly. The exact family (a skewed Tukey) is fixed in the
  implementation plan, with tests.

  > **Amended by the plan and ruling PU-T3a (2026-10-29):** the family as built (`engine/include/gg/grain/Envelope.h`).
  > With τ the grain's Texture, d its duration in samples and `aMin = min(0.5, max(0.05, 16 / d))`: for τ ≤ 0.5, attack
  > `a = 0.5 + 0.4τ` and release `r = 0.5 − 0.4τ`, no sustain; for τ > 0.5, with `s = 2τ − 1`,
  > `a = (1 − s)·0.7 + s·aMin` and `r = (1 − s)·0.3 + s·aMin`, a flat sustain `1 − a − r` between. The τ > 0.5 branch is
  > the plan's `a = 0.7 + (aMin − 0.7)s` written as a lerp (ruling PU-T3a): the same curve, exact at both anchors (the
  > plan's form gives 0.050000000000000044 for 0.05). The window at phase φ is `hann(warp(φ))`, with
  > `warp(φ) = φ < a ? φ·(0.5/a) : (φ > 1 − r ? 1 − (1 − φ)·(0.5/r) : 0.5)`; at τ = 0 both scales are exactly 1 and
  > `warp(φ) == φ` bit for bit (Sterbenz), so Texture 0 renders as the plain Hann. The pool latches `a`, `1 − r`, `0.5/a`
  > and `0.5/r` per grain; one `warpPhase` serves both scalar references and the SIMD interior. The gated anchor's
  > attack and release are `max(16 samples, 5%)` capped at half the grain; for τ ≤ 0.5 the attack and release don't
  > depend on the duration, so a 16-sample grain is a plain Hann only at Texture 0 and 1 (`aMin` is clamped to 0.5
  > there). The grain-event line carries `"tex"`.
- **Every envelope is continuous at both ends,** and its attack and release are never shorter than the 16-sample minimum grain.
  The click detector in the goldens guards this.

  > **Amended by ruling PU-T13a (2026-10-29):** the 16-sample floor applies to the gated anchor only,
  > `aMin = min(0.5, max(0.05, 16 / d))`. For τ ≤ 0.5 the shape is Hann-like and its attack and release scale with the
  > grain, as the default Hann always has: an 8-sample attack on a 16-sample grain at Texture 0 is M0's behaviour and stays
  > bit-identical. Every envelope is still continuous at both ends.
- **The window is evaluated per grain,** at spawn, from the grain's latched Texture value, the same way gain is latched today.
  It is deterministic and needs no libm: polynomial and `exp2Det` forms only.
- **Gain normalization:** `1/sqrt(max(1, density × duration))` stays. Texture changes only the envelope.
- **Macro 5's diffusion** in its top quarter writes Space's Diffusion parameter through the macro curve (§3.2). If Space is
  bypassed, that part of the travel has no audible effect.

## 5. Feedback (FX)

- **A new layer parameter,** Feedback: domain 0..0.95, default `0.0`. It is hidden in the instrument until M2.
- **The ring write becomes** `ring[t] = sanitize(in[t]) + fb(t) · wetPreMix[t − D]`, where:
  - `wetPreMix` is the wet signal after Level and the soft limiter, before the dry/wet mix;
  - `fb(t)` is Feedback's effective value, ramped like Mix and Level (10 ms linear);
  - `D` is a fixed internal delay, at least the engine's internal render chunk.
- **D must equal M,** the ring's 256-sample head margin. The admission clamp already keeps grains at least M samples behind
  the write head, so a fixed D = M makes feedback causal for every chunk of up to M samples. Engine renders longer than M are
  split into chunks of at most M internally.

  The output is therefore **bit-identical at any host block size and any maxBlockSize**. A determinism test pins this, with
  Feedback automated.

  > **Amended (2026-10-29):** the chunking is the **Processor's**, not the engine's: the Processor already owns the render
  > pieces, and in the FX each is at most `kFeedbackDelay` = 256 samples (static-asserted equal to
  > `LiveSource::kHeadMargin`); the instrument keeps its piece size and builds no ring input. The last 256 limited samples
  > are kept by absolute time (`t mod 256`). A **per-sample guard**: a sample where `fb(t)` is 0 writes the input alone, and
  > a piece whose ramp is 0 throughout hands the engine its input pointers, so where a piece starts never changes the ring,
  > the sign of zero included. The ring input is `finite(in[t]) + fb(t)·limited[t − 256]`, which the ring stores as it
  > stores any input (non-finite as 0, clamped to ±2^20).
- **Freeze wins.** While frozen, the ring's write gain is 0 (Task 4's 20 ms ramp), so neither input nor feedback is written,
  and the frozen buffer holds.

  > **Amended by ruling PU-T4a (2026-10-29):** the test checks the frozen ring bit for bit from 1 s to 3 s under Feedback
  > 0.9, and the output's level within 0.5 dB with Feedback raised at the freeze, instead of a 0.5 dB comparison against a
  > render with Feedback 0: the regeneration before the freeze legitimately sits in the frozen buffer.
- **Stability:** the soft limiter bounds `wetPreMix` to ±1, and `fb ≤ 0.95`, so the regenerated level stays bounded. A
  property test drives Feedback 0.95 with dense, loud settings for 60 s. It checks that the output is finite, that the wet
  signal never exceeds ±1 (the limiter's bound), and that the wet RMS over the last 10 s is no higher than over the first 10 s
  after the ring fills, within 1 dB, so there is no runaway.

## 6. The canvas and the Perform view

### 6.1 Layout

- **A header row:** the product name, the preset name and Perform. Edit is greyed out until M2.
- **The canvas** spans the full width: one lane in M1.
- **The macro row** below it holds the eight macros in remote-control order. Freeze is a large toggle, and the rest are knobs.
- The window is resizable with HiDPI support. Each macro takes keyboard focus, arrows adjust it, and Shift adjusts it finely.

### 6.2 The lane

- **FX:** the 16 s live ring, drawn with the write head fixed at the right edge as "now". History scrolls left, and x is
  seconds behind the head, from −16 s to 0.
- **Instrument:** the loaded sample's waveform, static, with x as the position across the file. This needs sub-project 3.
- **Each grain is drawn as:**
  - x = its read position;
  - width = its length, extrapolated from the atomic engine clock;
  - brightness = its gain;
  - vertical offset = its pan.

  The vertical axis is pan because each grain has a single pitch until M2's spread arrives.
- **The scan window:** a solid window at Position's base value, with edges at ±Spray's base.

  > **Amended by ruling PU-T8a (2026-10-29):** in the FX the window is one-sided, `[Position, Position + Spray]` seconds
  > behind the head, as the engine draws a live grain (`position + spray × u`, u in [0, 1)), so its newer edge is Position
  > itself and drags as the body, and its older edge drags Spray. The instrument's stays `Position ± Spray`, as its sample
  > spray is bipolar. Truthfulness is the success criterion (§1).
- **Ghost windows:** where macros or modulation move the effective value, a ghost window shows where grains actually read.
- **Freeze:** the scroll stops and the lane takes a tint. Feedback above 0 shows a regeneration marker on the write head.

### 6.3 Direct manipulation (base values)

- Dragging the window body sets Position's base value. Dragging an edge sets Spray's base value.
- Each drag is a host-visible parameter gesture: begin/end gesture events plus value events, so hosts record it as automation.
- No other canvas gestures exist in M1. Vertical drags, dropping a file and record arrive with M2's Edit work, or with
  sub-project 3 for the file drop if that sub-project specifies it.

### 6.4 Audio to UI (INTENT §17)

- One compact event per grain spawn, through a queue that drops entries when full.
- An atomic engine clock, from which the UI extrapolates each grain's progress.
- Triple-buffered per-block snapshots, which carry the effective Position and Spray for the ghost windows.
- A peak pyramid of the live ring.
- The engine publishes only while the editor is open. Visual thinning never touches the audio RNG.
- All of it is real-time safe: no allocation or locks on the audio thread. The `[rt]` tripwire covers the publish path.

  > **Amended by rulings PU-T9a, PU-T9b and PU-T10a (2026-10-29):** the peak pyramid is **one level** in M1, `LivePeaks`:
  > max(|L|, |R|) per 64-frame bucket of what the ring takes, **indexed by ring time**, the frames the write head advanced
  > over, not the sample clock, which a freeze makes diverge (PU-T9a). The FX writes it on **every** render piece, publishing
  > or not, so an editor that opens late finds the ring's recent audio, not an older pass's peaks (PU-T9b). "Publishes only
  > while the editor is open" holds for the grain queue (4096 entries, dropping the newest and counting them) and the
  > per-block snapshot (a triple buffer), and for the atomic clock stored after each snapshot. The plugin owns one editor
  > context for its life and points its publish fields at each activation's Processor, null while inactive (PU-T10a). INTENT
  > §17 records the path as built.

### 6.5 Technology

- **Editor:** the existing JUCE 9 shim (clap-juce-shim).
- **Controls and theming:** knobs, the toggle, focus and one dark theme come from **sst-jucegui**. It is GPL-3.0-or-later,
  AGPL-compatible, and a new submodule pinned to an exact SHA. Its adoption also closes carry-over [213]/[344] (consolidating
  the `JUCE_*` definitions).

  > **Amended (2026-10-29):** sst-jucegui is **MIT** (its `LICENSE.md`; INTENT §15 already said so), not GPL-3.0-or-later,
  > pinned at `22a89ca4e54179cbda0b8164e54e06dae5f30490`; the md4c and CMakeRC it vendors and the flowbite-icons glyphs in
  > its resources are MIT too (`THIRD_PARTY_NOTICES.md`). Carry-over [213]/[344] was already closed, by sub-project 1
  > (`ec2a054`). Its JUCE requirements are re-pointed at the headers-only `gg-juce-headers` plus
  > `$<LINK_ONLY:clap_juce_shim>`, so JUCE compiles once, in the shim (ruling PU-T7a).
- **Canvas:** M0's CPU `SplatRenderer`, behind the `GrainCanvas` renderer interface. It held the §16 UI budget: under 3 ms at
  8,192 grains in Live and Bitwig. Carry-over [214] (clamping `px1`/`py1`) is due when it is reused, so it is in scope.

## 7. Seeds

- **A new instance draws its own seed** at creation, on the main thread, from the OS's entropy source. It is saved in the state
  like today's seed. Two instances on two tracks then make different clouds, and a reloaded session renders identically.
- **The audio thread is unaffected:** its randomness still comes only from `gg::rng`, keyed by the saved seed.
- **gg-render, the tests and the goldens** keep explicit seeds. Loading a state uses the state's seed.

  > **Amended by ruling PU-T5a (2026-10-29):** the seed comes from two 32-bit draws of `std::random_device`, or a splitmix64
  > of the steady clock if that throws. A state loaded while active hands its seed to the audio thread in the plugin's
  > loaded-state snapshot, in both products, with no `request_restart` (carry-over row 115); `Processor::setSeed` keys the
  > grains scheduled from that `process()` call on. In the instrument the values travel with the sample, so the seed and
  > the values may land in different blocks (PU-T5a: a live load's timing is inexact anyway, and the saved state is
  > consistent). Once the values are applied, the host is asked to rescan them (row 123).

## 8. Error handling and limits

- Out-of-range macro values clamp to 0..1, and target sums clamp to each target's range, as today.
- **A non-finite parameter or macro event is dropped,** following Task 6's existing rule.
- **Feedback's input is the limited wet signal** (§5). If the input is non-finite, the ring stores 0, as Task 4 already does.
- **With the editor closed,** no UI data is published, and the audio cost is unchanged.

## 9. Verification

- **Unit tests:**
  - every recipe at macro 0, rest and 1, plus the interior points, against §3.2's formulas;
  - polarity, so that at rest every target equals its base exactly;
  - the Texture anchors, plus envelope continuity and minimum attack across 0..1;
  - Feedback's ring write with fixed D;
  - the IDs, flags, names and both remote pages.
- **Determinism:**
  - block-size invariance with every macro, Texture and Feedback automated, at block sizes 1, 7, 64, 511 and 4096 plus random
    splits, and with maxBlockSize 64 vs 4096;
  - the Feedback 60 s property test.
- **Real-time safety:** `[rt]` tripwire tests on `process`, `flush` and the UI publish path, and RTSan-clean.
- **Goldens:**
  - an `fx-macros` case, sweeping each macro across its travel;
  - an `fx-feedback` case, with Feedback at 0.5 and 0.9, frozen and unfrozen.

  > **Amended by ruling PU-T12a (2026-10-29):** `fx-macros` runs over a stereo 330 Hz sine, not noise, which hid all
  > sixteen injected clicks, a blind spot the director hadn't accepted; it catches all sixteen. `fx-feedback`'s sweep lasts
  > 4 s, so Feedback 0.9 at 3 s has material; it catches 7 of the 16, the rest hidden by the sweep's top octaves, their
  > regeneration and the frozen ring (pinned in the click-injection table). A third case, `inst-texture`, steps Texture from
  > 0 to 1 over a held instrument note, and catches all sixteen.
  - Each spectrogram is reviewed before commit. The existing goldens stay byte-identical, because Texture's default is Hann
    and Feedback's is 0.
- **UI tests (`ui` label):**
  - the lane's coordinate mapping (head-fixed x, pan to y);
  - ghost-window placement from a snapshot;
  - the gesture event sequence of a drag;
  - `SplatRenderer` clamping ([214]).
- **The canvas budget,** re-measured with the real Perform view at 8,192 grains in Live and Bitwig: at most 3 ms.
- **The validators:** clap-validator, pluginval at strictness 10, auval ×3 and Steinberg's validator, all with the new
  parameters.
- **The director's host and listening check:**
  - both products in Ableton Live 12 and Bitwig;
  - each macro swept across its travel;
  - depths tuned by ear, with the tuned values committed and recorded in the spec's decision notes;
  - Feedback and Freeze in a performance;
  - the canvas judged for truthfulness against what is heard.

## 10. INTENT changes (at this sub-project's close)

- **§14:** the macro model (relative, fixed recipes, curves in the value domain), Texture's envelope family and Feedback's ring
  write with D = M.
- **§15:** sst-jucegui.
- **§17:**
  - the M1 Perform view as built: the fixed-head FX lane, the macro row, base-value drags with ghosts;
  - the instrument's slot 7 and slot 8 substitutions;
  - the remote pages.
- **§18:** the new parameter IDs.
- **§23:** a dated row for the director decisions in §1, and one for the fresh-instance seed.

## 11. Risks

| Risk | Handling |
|---|---|
| Relative macros hit clamps when a preset's base value sits near a range end, so part of a macro's travel does nothing | The listening check sweeps every macro from the factory presets' base values. Factory bases are chosen inside each recipe's working window, and Mutate (M2) respects the windows |
| Feedback at high settings smears into a static drone or a level build-up | fb ≤ 0.95 after the limiter, the 60 s property test, the curve's fine low end and the listening check |
| sst-jucegui's API churns (no releases) | Pinned SHA. Only basic controls are used |
| The canvas misrepresents timing under host jitter | The atomic engine clock plus extrapolation (INTENT §17). The UI tests pin the mapping, and the director judges truthfulness |
