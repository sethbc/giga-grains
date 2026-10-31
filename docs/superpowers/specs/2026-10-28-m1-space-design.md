# M1 sub-project 4: Space — design

| | |
|---|---|
| **Milestone** | M1 Cloud core (INTENT §20), sub-project 4 of 6 |
| **Status** | Approved by the director, 2026-10-28. Decisions S1–S8 made by the director, 2026-10-28, by forced choice. Amended during the build, in place and marked with each ruling's ID: §6.3 (P1, R-T2a, R-T2b, R-T5a, R-T5b, and the fork per the director's no-filing policy), §6.4 (`loopClip`, R-T8a, P2), §6.7 (R-T7a); INTENT §23 #50 records them |
| **Spec authority** | `INTENT.md`; this document records how this sub-project implements it |
| **Builds on** | The FX-slice spec (the `Processor` and its wet chain), the Sources spec (notes, freeze, the instrument's sound) |
| **Feeds** | Sub-project 5 (Perform UI): its Texture macro drives Diffusion in the top quarter of its travel (`2026-10-27-m1-perform-ui-design.md` §3, §4) |
| **Starts from** | `main` at `5eefffc` (sub-project 3 merged) and `docs/m1-carry-over.md` |

## 1. Why this sub-project

INTENT §23 #35 gives sub-project 4 "diffusion and reverb", the first stage of INTENT §14's global
effects chain (diffuse · delay · reverb · limiter). Today the wet chain is grains × Level → soft
limiter → dry/wet mix: the grains sound in a dry, empty space, and a sparse cloud reads as
separate clicks of sound rather than a texture.

Space gives the grains a place to live:

- a **diffuser** that blurs grains into each other, part of the granular sound itself (the Texture
  macro reaches it in sub-project 5);
- a **reverb** with its own controls that puts the whole sound, and optionally the FX's dry input,
  in a room, up to an endless hall that hangs forever.

**Who it's for.** The director, shaping textures in Ableton Live 12 and Bitwig Studio with both
products, and sub-project 5, which needs Diffusion's parameter ID.

## 2. Goals

1. A per-layer **diffuser** (layer A in M1), with one parameter, Diffusion.
2. A global **reverb** after the layers, with Amount, Decay, Size, Damping, Pre-delay and
   Modulation, built on Surge's Reverb 2 from sst-effects.
3. In the FX, a **Dry to Reverb** send that puts the unprocessed input in the same room.
4. **Decay's top setting is infinite:** the tail never fades while it is there.
5. **Off by default:** with every new parameter at its default, both products render
   bit-identically to today, and every existing golden stays bit-identical.
6. Everything stays **deterministic and real-time safe:** bit-identical at any host block size,
   no allocation on the audio thread, RTSan-clean.
7. The plugins report their **tail** to the host, so a reverb tail isn't cut off when the input
   stops.

## 3. Non-goals and anti-goals

**Non-goals (later):**
- The delay and the full global limiter of §14's chain (M2 or later; the soft limiter stays).
- Edit and FX views with Space panels (M2). In M1 the new parameters are reachable through the
  host's generic parameter list, automation and a remote-controls page (§6.6); the Perform view's
  only route to Space is the Texture macro's Diffusion.
- Factory presets with space (M2). Until then INIT is all defaults, so everything starts dry.
- Per-layer reverbs, reverb sends per layer, or more than one reverb algorithm.
- Tempo-synced pre-delay.
- A reverb freeze tied to the Freeze parameter (the director chose an infinite Decay instead, S5).

**Anti-goals (failure even if it technically works):**
- A Space at 0 that changes the sound: any bit of difference with every new parameter at its
  default.
- Output that depends on the host's block size, or on measured CPU.
- A tail that grows without bound, goes non-finite, or clips the output past the soft limiter.
- Zipper noise or clicks when Diffusion, Amount or Pre-delay is automated.

**Accepted (S8):** moving Size mid-sound clicks or glitches the pitch, as Surge's Reverb 2 does: Size
is "set, don't sweep". The listening check judges it, and the final review logs it as a carry-over
row.

## 4. Director decisions (2026-10-28)

| # | Decision | Choice |
|---|---|---|
| S1 | What Space is musically | **Both:** a diffuser that belongs to the grain sound, feeding a separate reverb with its own controls |
| S2 | What the FX's reverb hears | **The grains always, plus a separate Dry to Reverb send** for the unprocessed input |
| S3 | The reverb's controls | **Standard plus modulation:** Amount, Decay, Size, Damping, Pre-delay, Modulation |
| S4 | A fresh instance | **Dry engine, roomy presets:** every new parameter defaults to 0 (no space); presets choose their own |
| S5 | Decay's top | **Infinite:** the top of Decay's travel holds the tail forever, with the input still added |
| S6 | How the reverb is built | **Approach A:** Surge's Reverb 2 from sst-effects, plus an in-house diffuser |
| S7 | What ∞ holds (Reverb 2 damps inside its loop, so a loop gain of 1 alone still drains lows and highs) | **At ∞, the LF damping is switched off;** highs still fade by Damping, and at Damping 0 nothing fades. A small pinned fork, changing only the ∞ setting |
| S8 | Automating Size and Pre-delay (Reverb 2 jumps to new delay lengths) | **Smooth Pre-delay now** (it glides like tape), in the same fork; **accept Size's artefact** and log it |

S7 and S8 came from the builder check, which read the draft as the planner would.

## 5. Approaches

- **A. Surge's Reverb 2 (sst-effects) plus an in-house diffuser — chosen.** It already has every
  control in S3 (pre-delay, room size, decay, modulation, HF/LF damping, and its own diffusion and
  buildup), years of ears behind it, and INTENT §15 already plans sst-effects for the reverb. It
  processes fixed chunks, so it is aligned to the 32-sample grid (§6.3). Its coefficients use
  `powf`, deterministic on one platform; across platforms the goldens' metric tolerances cover the
  last bits, as they do for r8brain.
- **B. A port of Nimbus (the Clouds-style diffuser and reverb, in sst-effects).** The authentic
  granular-box character, dark and a little lo-fi, but designed for a fixed 32 kHz rate and with
  no size, pre-delay or modulation controls; adding them is close to a rewrite.
- **C. All in-house: a diffuser and a feedback-delay-network reverb.** Total control (an exact
  infinite, no `libm`, per-sample, RNG-keyed modulation), at the cost of designing and voicing a
  reverb from scratch, spending the director's listening on an unproven sound.

The diffuser is in-house in every approach: a short allpass chain per layer, cheap enough for M2's
four layers.

## 6. Design

### 6.1 The signal flow

Per output sample, with `w` the layer's grain sum (the engine's output today), `d` the FX's
sanitized dry input (ruling M1-q), `m` Mix, `g` Level's gain, `a` Reverb Amount and `s` Dry to
Reverb:

```
layer A grains ─► diffuser(Diffusion) ─► w'                          (per layer; §6.2)
wet  = softLimit(w' × g)                                              (today's wet, unchanged)
rIn  = m × wet + s × d          FX        rIn = wet        instrument
r    = Reverb(rIn)                                                    (global; §6.3)
out  = d × (1 − m) + wet × m + a × r   FX
out  = wet + a × r                    instrument
```

- **The dry path** is unchanged: `d × (1 − m)`, unclamped (ruling M1-q).
- **The reverb hears the wet signal as the listener does** (scaled by Mix in the FX), so Mix 0
  with Dry to Reverb up puts only the input in the room, and Mix 100% with the send at 0 only the
  grains.
- **The return is bounded:** `a × r` passes through a soft limiter of its own before the sum, or
  the sum's peak is otherwise held within today's bound. The exact form is the builder's (§12),
  under two rules: with `a` = 0 and `s` = 0 the output is **bit-identical to today's**, including
  the sign of zero (a test pins it, §8); and no setting can produce a non-finite sample or a
  sample whose magnitude exceeds 2 (the dry input's own peak aside).
- **The instrument** has no dry signal and hides Mix (ruling M1-w), so its reverb hears the wet
  signal; it hides Dry to Reverb as it hides Mix.

*My call (the director chose S1–S2; this placement is my proposal):* Level and the soft limiter
stay where they are, before the reverb, so a Space at 0 is exactly today's chain; the reverb's
return is added after.

### 6.2 The diffuser (per layer)

- **What it does:** a cascade of four allpass filters per channel, different lengths on left and
  right so it also widens, crossfaded with its input by Diffusion: at 0 it is bypassed bit for bit,
  at 100% the grains are fully smeared into each other (a few tens of milliseconds of blur, not a
  reverb).
- **Lengths** are fixed in milliseconds, mutually prime in samples at the running rate, computed
  in `activate()` (allocation happens there). The allpass coefficient is fixed (around 0.6), or
  scaled gently by Diffusion; the exact lengths, coefficient and curve are voiced by ear and fixed
  in the plan with tests.
- **Smoothing:** Diffusion's effective value ramps like Mix and Level (on the control grid, then
  per sample over the ramp), so automating it never zippers.
- **Where it lives:** in M1, in the `Processor`, on layer A's output. It is written as a per-layer
  unit (state in a struct the layer owns) so M2's layers each get one.
- **Determinism:** plain float arithmetic per sample, no `libm`, no RNG, no block quantum; the
  allpass state is cleared on `activate()` and `reset()`.

### 6.3 The reverb (global)

- **Algorithm:** sst-effects' `Reverb2<FXConfig>` (Surge's Reverb 2), with an `FXConfig` of our own
  whose `blockSize` is 32, the control grid's. Its mix is fixed at 100% wet and its width at 100%;
  we do the mixing (§6.1).
- **Fixed internals, voiced by ear:** Reverb 2's own diffusion, buildup and LF damping are set to
  fixed values chosen at the listening check and recorded in the plan. (The LF damping is switched
  off at ∞, §6.4.)
- **Pre-delay is smoothed (S8):** in the fork, the pre-delay read position glides to a new value
  instead of jumping: a slewed, interpolated read, so a moving Pre-delay bends pitch like tape and
  never clicks. The slew rate is fixed in the plan. The fork's change is per sample inside Reverb
  2's chunk, from values that change only at chunk boundaries, so it stays block-size invariant.

  > **Amended by ruling P1 (2026-10-28):** the glide is rate-limited, not a fixed time: the read's
  > delay moves toward each new value at no more than `kPredelayMaxRate` = 0.5 samples per sample,
  > interpolated linearly, so a glide bends the pitch at most an octave down or a fifth up. The
  > plan's first draft, a fixed 0.1 s glide, bent it fivefold over a 500 ms jump, which isn't
  > tape-like and failed the plan's own click test. Ruling R-T2a adds: while nothing has run since
  > construction or a clear, a new value applies at once (the buffer is silent, so nothing jumps).
- **Size is not smoothed (S8):** a Size change takes effect at the next chunk, as in Surge.
- **Grid alignment:** Reverb 2 processes whole 32-sample chunks. The `Processor` feeds it chunks
  aligned to absolute sample positions (multiples of 32 of the stream's sample count), buffering a
  partial chunk across host calls. Its return is therefore delayed by a constant 32 samples
  (0.67 ms at 48 kHz) at every block size; the delay is subtracted from Pre-delay, so Pre-delay's
  real minimum is 32 samples. No latency is reported to the host: only the reverb return is late,
  and a reverb return 32 samples late is inaudible.
- **Parameters on the grid:** the reverb's values change only at grid boundaries, which are chunk
  boundaries, and Reverb 2 interpolates them across its chunk (its `lipol`), so the output is a
  function of the parameter history on the grid alone.
- **Memory:** Reverb 2 holds fixed-size arrays (about 13 MB of delay lines and pre-delay per
  instance, to be measured). It is allocated once, off the audio thread, in `activate()` (or the
  `Processor`'s construction), never in `process()`. `reset()` clears it; clearing a 13 MB tail on
  the audio thread is acceptable only where the host already expects a reset to cost.

  > **Amended by rulings R-T2b, R-T5a and R-T5b (2026-10-28):** measured, the reverb is
  > 14,533,632 bytes, allocated with the `Processor`. A clear (the patch's `clear()`) zeroes only
  > what a later read can reach (R-T2b): each allpass up to its longest length since the last
  > clear, and of each delay line and the pre-delay only the oldest positions a fresh run reads
  > before writing them, within the reach of the largest Size, Modulation and Pre-delay at the
  > current rate; the smoothed pre-delay's range is clamped to 0.5 s (Space's) for it. That is
  > about 0.5 MB at 48 kHz, not the whole 14.5 MB. `clearStep(maxFloats)` does the same in steps.
  > The idle rule's clear is spread (R-T5a): the first idle chunk after an active one begins a
  > clear, each idle chunk zeroes at most 16,384 floats (64 KiB), and the first active chunk
  > finishes what is left before it runs, so raising Amount still starts from an empty room,
  > deterministically. `reset()` clears it all at once (R-T5b); construction is already clean, and
  > a new sample rate builds a new `Processor` and reverb.
- **Dependencies:** sst-effects and sst-filters (both GPL-3.0-or-later, AGPL-compatible) join
  `libs/sst/` as submodules pinned to exact SHAs, as INTENT §15 already lists.
- **The fork (S7, S8):** Reverb 2 is modified in two places only: the LF damping at ∞ (§6.4) and
  the smoothed pre-delay. Each change is selected by our `FXConfig` (or a flag it sets) so that,
  with both off, the code is Surge's. *My call on how it is carried:* a small patched copy of
  `Reverb2.h` in our tree (`libs/` or `core/`), under its GPL-3.0-or-later header with the change
  marked, rather than a fork of the whole sst-effects repository; sst-effects itself stays an
  unmodified pinned submodule. INTENT §15 records the patch. Offering the two changes upstream is
  left for later.

  > **Amended (2026-10-28; INTENT §23 #50):** the patch grew beyond two changes: ∞ is now the
  > decay multiplier at exactly 1, the LF dampers at a 0.5 Hz DC blocker's coefficient (ruling
  > R-T8a), the HF clamp at 0 and `loopClip`, reached by a 30 ms glide (ruling R-T8b; §6.4);
  > plus the smoothed pre-delay, and `clear()` and `clearStep()` (rulings R-T2b, R-T5a). They are
  > selected by calls (`setInfinite`, `setPredelaySamples`, `clear`, `clearStep`), not by the
  > `FXConfig`; with none used, the copy renders
  > bit-identically to Surge's. It sits in `core/src/space/Reverb2Patched.h`. **It isn't offered
  > upstream:** the director's policy (2026-10-28) is no external reports or upstream filings.
- **CPU when off:** while Amount and Dry to Reverb are both 0 and Amount has been 0 long enough,
  the reverb may skip processing, but only on a rule that depends on the parameter history alone
  (never on CPU, never on signal level measured in ways that differ by block size). Raising Amount
  then starts from an empty room. The rule is fixed in the plan.

### 6.4 Infinite Decay (S5)

- Decay's range runs from a short room to a long hall (§6.5), and **its top 2% is "∞"**: the
  reverb's loop gain is exactly 1, so the tail never fades. The host displays "∞" there.
- The input is still added at ∞, so a frozen grain cloud builds a pad that hangs. Coming down
  from ∞ lets it fade at the new Decay.
- **Bounded, always:** at ∞ with any input, for any length of render, every internal sample stays
  finite and the output stays within §6.1's bound. A lossless loop fed coherent input (DC, a
  steady tone) grows without limit, so the builder must bound it: for example a DC blocker on the
  reverb's input (INTENT §14 already lists one in the global chain), a loop gain at ∞ a hair below
  1 (a T60 of hours rather than truly infinite), an input gain that eases off near ∞, or a soft
  saturation inside the loop (a fork). The plan picks one, and a test renders an hour of a
  full-scale steady tone and a full-scale noise at ∞ to prove the bound.

  > **Amended (the plan's choice, 2026-10-28):** the bound is a soft saturation inside the loop,
  > `loopClip(x)` = `x` for |x| ≤ 1, else `sign(x) × (2 − 1/|x|)`: below 2 in magnitude, slope 1
  > at the knee, no libm. At ∞ it runs on each block's loop signal after the dampers. The hour
  > tests (a full-scale 110 Hz sine and full-scale noise, Damping 0, Size and Modulation 100%)
  > peak at 1.685 and 1.718 with the final code (R-T8a, R-T8b), every sample finite.
- **What ∞ holds (S7):** Reverb 2 damps inside its loop: four LF high-pass and four HF low-pass
  one-poles per trip, and the LF coefficient is clamped to at least 0.01, a high-pass near 77 Hz
  at 48 kHz. A loop gain of 1 alone would still drain the lows in seconds. **At ∞ the LF damping
  is switched off** (the fork, §6.3), so lows and mids hold. The HF damping stays and follows
  Damping, so highs still fade, as in Clouds' and Beads' frozen reverbs; at Damping 0 the loop is
  lossless and nothing fades. Every finite Decay keeps Surge's damping unchanged.

  > **Amended by rulings R-T8a and P2 (2026-10-28):** at ∞ the LF damping is replaced by a 0.5 Hz
  > DC blocker in the loop, a one-pole high-pass (`kInfiniteDcHz`), because a lossless loop
  > integrates the grains' DC (the `inst-infinite` golden saw it grow to −0.02). So ∞ is: the
  > decay multiplier exactly 1, the DC blocker, the HF damping per Damping with its lower clamp at
  > 0 (lossless at Damping 0 apart from DC), and `loopClip`. With Damping up, the HF one-pole
  > attenuates the mids on every pass too, so only the lows hold, and the mids fade more slowly
  > than the highs (P2); the listening check judges it.
  >
  > **Amended by ruling R-T8b (2026-10-28, the fix wave after the final review):** the switch
  > into and out of ∞ is continuous. The DC blocker is the LF dampers themselves at the 0.5 Hz
  > coefficient, keeping their state, not a separate filter that started at rest (which stepped
  > the loop's lows on entering ∞, about +26 dB above 3 kHz with a held pad). And the loop glides
  > between its finite and infinite settings over 30 ms, in whole 32-sample blocks: with g the
  > share of ∞, the decay multiplier is decay + g·(1 − decay), the LF coefficient ldc·(dc/ldc)^g,
  > the HF clamp's floor 0.01·(1 − g), and `loopClip` runs while g > 0 (leaving ∞ had dropped the
  > multiplier from 1 within one block). Before a reverb's first block, g takes its target at
  > once, so a reverb that starts at ∞ (the `inst-infinite` golden) is unchanged, and at g = 0
  > the loop is Surge's exactly. The DC blocker costs about 0.6 dB in the lowest band over 30 s
  > at ∞.
- Reverb 2's damping coefficients don't scale with the sample rate, so how fast the highs fade at
  a given Damping depends on the host's rate. That is Surge's behaviour, and it is kept.

### 6.5 Parameters

Eight new parameters, appended (§18's IDs are append-only). Sub-project 5's spec has already
reserved `0x000011`–`0x000018` (the macros), `0x010009` (Texture) and `0x01000A` (Feedback), so
Space takes the next free IDs in each scope.

| ID | Name | Scope | Range | Default | Notes |
|---|---|---|---|---|---|
| `0x01000B` | Diffusion | layer A | 0–100% | 0% | The Texture macro's top quarter writes it (sub-project 5) |
| `0x000003` | Reverb Amount | global | 0–100% | 0% | The return's gain `a` |
| `0x000004` | Reverb Decay | global | 0.25 s – 60 s, then ∞ (log) | 2 s | Top 2% of travel is ∞ (§6.4) |
| `0x000005` | Reverb Size | global | 0–100% | 50% | Reverb 2's room size |
| `0x000006` | Reverb Damping | global | 0–100% | 30% | Reverb 2's HF damping |
| `0x000007` | Reverb Pre-delay | global | 0–500 ms | 0 ms | Real minimum 32 samples (§6.3) |
| `0x000008` | Reverb Modulation | global | 0–100% | 25% | Reverb 2's modulation depth |
| `0x000009` | Dry to Reverb | global | 0–100% | 0% | FX only; the instrument hides it (`HIDDEN`, no effect) |

- **Amended (M2 sub-project 4, INTENT §23 #69; carry-over row 311):** Reverb Modulation glides.
  Reverb 2 sets its modulation depth once per 32-sample block, so a jump moved each line's
  modulated read by up to 240 samples within one block, a scrub that echoed as a click at Decay ∞
  and Damping 0. `SpaceReverb` now moves the depth it gives Reverb 2 toward Modulation's mapped
  value by at most one 30 ms glide's share per chunk (`kModulationGlideSeconds`, 0 to 1 in 30 ms,
  as long as the patch's glide into ∞), and an empty reverb (constructed, reset or cleared) takes
  the value at once, as the pre-delay and ∞ do. `Reverb2Patched.h` is unchanged. A full move bends
  a held tail by about 3 semitones for the 30 ms (a listening item).
- *My calls:* the IDs, names, ranges and the defaults other than Diffusion, Amount and Dry to
  Reverb (S4 fixes those at 0). Decay, Size, Damping, Pre-delay and Modulation affect nothing
  while Amount is 0, so their defaults are what a user hears when first turning Amount up; they
  are voiced at the listening check.
- Every one is `AUTOMATABLE | MODULATABLE`, values float end to end, with sst-basic-blocks
  `ParamMetaData` (log-domain for Decay; Decay's display shows "∞" at the top).
- **The patch:** the new values join the `PTCH` XML. A state saved before this sub-project loads
  with the new parameters at their defaults and renders bit-identically to before. Whether that
  takes a patch version bump with a migration or only defaults for missing values is the
  builder's (§12), with a test either way.
- **AUv2 parameter order:** the new parameters are appended after the ten, in ID order within each
  scope as the plan fixes it; the order is frozen at the first public release (INTENT §18).

### 6.6 Remote controls

- A second page, **"Space"**: Diffusion, Reverb Amount, Decay, Size, Damping, Pre-delay,
  Modulation, Dry to Reverb. In the instrument the Dry to Reverb slot is empty, as Mix's is on the
  Cloud page.
- *My call.* Sub-project 5 inserts its "Perform" page first; "Cloud" and "Space" follow it.

### 6.7 Tail

- Both plugins implement CLAP's `tail` extension (clap-wrapper maps it to VST3's `getTailSamples`
  and AU's tail time). The tail is the longest a sound can ring after its input or its last note
  stops:
  - **the grains' part:** in the instrument, Size (the release tail); in the FX, how far back the
    grains can still read the live ring plus one grain: Position + Spray + Size, at their maxima
    under the current values;
  - **plus the reverb's,** when Amount > 0: Pre-delay plus a multiple of Decay chosen in the plan;
  - **`CLAP_TAIL_INFINITE`** (INT32_MAX) when Decay is at ∞ with Amount > 0, and in the FX while
    Freeze is engaged or a note is held, since it keeps sounding with no input. Hosts that suspend
    silent plugins would otherwise cut off a frozen texture.
- *My call* (from the builder check). The tail is recomputed on the main thread when a parameter
  that enters it changes, and `tail.changed()` is called; Freeze and notes change on the audio
  thread, so the plugin requests a main-thread callback to report them. Sub-project 5's Feedback
  will lengthen the FX's tail and adds its own term.
- The plugin keeps processing through the tail; nothing returns `CLAP_PROCESS_SLEEP` while the
  reverb rings.

> **Amended by ruling R-T7a (2026-10-28):** the tail is a constant per product, and nothing calls
> `tail.changed`. This replaces the tail above that followed the values, and the "My call" on
> announcing it.
> - **The FX's is `CLAP_TAIL_INFINITE`:** a live-buffer effect can sound with no input at any time
>   (Freeze, a held note, a 16 s read, an infinite reverb).
> - **The instrument's is its longest finite one,** 122.5 s at the active rate (2 + 0.5 + 2 × 60
>   s: Size's maximum, Pre-delay's maximum and twice Decay's longest finite value).
> - **Why:** clap-wrapper turns `tail.changed` into VST3's `restartComponent(kLatencyChanged)`, on
>   which the host deactivates and reactivates the plugin, and into AU's
>   `PropertyChanged(TailTime)`. Following the values would have restarted the FX at every note,
>   Freeze toggle or Amount change, emptying its live ring and dropping its notes and Freeze; and
>   CLAP makes `tail.changed` an audio-thread call while both wrappers' targets are main-thread
>   ones. A finite instrument tail also spares hosts that render the reported tail an endless
>   bounce.
> - **The cost:** the FX never sleeps on silence in hosts that suspend silent plugins (idle CPU),
>   and an instrument at ∞ is suspended 122.5 s after its last note there.
> - clap-wrapper's AUv2 hard-codes `SupportsTail()` false, so AU hosts see neither tail; that waits
>   on upstream (`docs/m1-carry-over.md`).

### 6.8 `gg-render`

- No spec format change: FX and instrument modes already take `params` and `automation` by name,
  so the eight new parameters work there as soon as they exist.
- **New golden cases** (each with its spectrogram looked at before committing):
  - `fx-space` — a grain cloud over noise into the reverb at a medium Decay and Size;
  - `fx-dry-send` — Mix 0, Dry to Reverb up: the input alone in the room;
  - `inst-diffusion` — a sparse cloud with Diffusion stepped from 0 to 100% in 16 steps;
  - `inst-infinite` — a held note at Decay ∞, released, then the tail sustaining;
  - an automation case moving Size, Decay and Pre-delay mid-render (block-size invariance under
    automation).
- **Every existing golden stays bit-identical:** its WAV's SHA-256 is pinned before the change and
  checked after, since metric tolerances miss sub-1e-7 changes.

## 7. Determinism and real-time safety

- **Block-size invariance:** the diffuser is per sample; the reverb runs on chunks aligned to the
  absolute 32-sample grid, so its input, its parameter history and its output are the same at any
  host block size. The determinism tests gain Space renders at block sizes 1, 32, 64, 511 and 4096
  and under random splits, with every Space parameter automated.
- **No new RNG draws in the engine:** the event logs of every existing golden are unchanged.
  Reverb 2's modulation uses its own deterministic quadrature oscillator, not a clock-seeded RNG
  (INTENT §14's rule for reused sst code); the plan confirms Reverb 2 has no other randomness.
- **Floating point:** sst-effects and sst-filters compile with `-ffp-contract=off` like every
  first-party target. `powf` in Reverb 2's coefficients is deterministic on one platform; across
  platforms the metrics comparison covers it.
- **Real time:** no allocation, lock or I/O in `process()`; the `[rt]` tripwire tests and RTSan
  cover the diffuser and the reverb, including parameter changes and the transition into and out
  of ∞. `ScopedDenormalsOff` already covers the whole `process()`, which matters for decaying
  tails.

## 8. Verification

- **Unit tests:**
  - with every Space parameter at its default, `Processor` output is bit-identical to today's (FX
    and instrument, several inputs, Mix 0 and 100%), including the sign of zero;
  - the diffuser: bypass at 0 is exact; at 100% the impulse response spreads energy over its
    expected span; L and R differ; no zipper (bounded sample-to-sample step) when Diffusion is
    automated;
  - the reverb: an impulse produces a tail delayed by exactly Pre-delay (minimum 32 samples);
    Decay maps to a measured T60 within a tolerance across the range; at ∞ with Damping 0 a
    stopped tail holds its level in every band, and with Damping up its lows and mids hold while
    its highs fade (amended, §6.4: the DC blocker costs about 0.6 dB in the lowest band, and with
    Damping up only the lows hold, ruling P2); coming down from ∞ decays; with the fork's two
    changes off, the patched Reverb 2 renders bit-identically to Surge's;
  - Pre-delay automation: a swept Pre-delay produces no step larger than the signal's own (no
    click), at block sizes 1 and 4096 alike;
  - the hour-long boundedness renders at ∞ (§6.4), labelled `slow`;
  - Dry to Reverb: at Mix 0 and send > 0 the output is the dry input plus a reverb of it; the
    instrument ignores the send;
  - the parameter IDs and metadata pinned; an old state loads at the defaults and renders
    bit-identically;
  - the tail extension's values: a constant per product (ruling R-T7a, §6.7), the FX's
    `CLAP_TAIL_INFINITE` and the instrument's 122.5 s.
- **Plugin tests:** the new parameters through the CLAP factory, the "Space" remote-controls page
  in both products, the `[rt]` tripwire with Space automated, the AU state round-trip.
- **Goldens:** the new cases (§6.8); the existing ones' WAV SHA-256s unchanged.
- **Benchmarks:** `gg-bench` gains a report-only case timing the reverb and diffuser per sample,
  so sub-project 6 can publish Space's cost beside X. X itself (grains only) doesn't change.
- **Validators:** clap-validator, pluginval at strictness 10, Steinberg's validator and auval ×3
  pass with the new parameters and the tail.
- **The gate:** `scripts/check.sh` (dev, asan, rtsan, format) on every merged state.

## 9. Listening and host checklist (feeds listening gate #1, sub-project 6)

A listening pack under `docs/listening/m1-space/`, rendered with `gg-render`, with a short
checklist:

1. Diffusion swept 0 → 100% on a sparse cloud: grains blur into a texture without sounding like a
   reverb or a chorus.
2. Amount at a few Decay/Size settings: a small room to a long hall; the tail sounds smooth, not
   metallic or grainy.
3. Modulation 0 vs 25% vs 100% on a long frozen tail: does it stop ringing without seasick
   pitch wobble?
4. Decay at ∞ with a frozen FX note: the pad hangs and stays clean; bringing Decay down fades it.
5. Dry to Reverb with Mix 0: the input sits in the room.
6. Automating Pre-delay glides like tape; automating Size clicks or glitches (accepted, S8): judge
   how bad it is, for the carry-over row.
7. Defaults: the five non-zero defaults sound right when Amount is first turned up.
8. Loudness: Amount at 100% with a dense cloud into the soft limiter; and the instrument's default
   loudness carry-over (`docs/m1-carry-over.md`) with reverb added.

Host check: the "Space" page in Bitwig; automating Amount and Decay in Ableton Live; the tail
continuing after the clip stops in both.

## 10. Carry-over items

- No item in `docs/m1-carry-over.md` is owned by sub-project 4. Its final review adds a section
  "From M1 sub-project 4 (Space)" for whatever it leaves open.
- The instrument's default-loudness row (closed: the default Level stays) is rechecked by ear with
  reverb in §9 item 8; it is reopened only if the director hears a problem.

## 11. INTENT changes (Part II, with §23 entries in the same change)

- **§14:** the diffuser moves from the global chain to the layer (the diagram and "Mix and global
  FX"), matching §17's per-layer "Space" panel; the global chain becomes delay · reverb ·
  limiter. The signal flow of §6.1, the grid-aligned reverb with its 32-sample return delay, and
  infinite Decay's bound are recorded in the Cloud core rules.
- **§15:** sst-effects and sst-filters pinned, with their SHAs, and the patched Reverb 2 (S7, S8).
- **§18:** the eight parameters, the "Space" remote-controls page, the tail extension.
- **§23:** one entry for decisions S1–S8 and the rulings of this sub-project.

## 12. Left to the builder

- The return's limiting form (§6.1), under its two rules.
- The diffuser's lengths, coefficient and Diffusion curve (§6.2), voiced and then pinned by tests.
- Reverb 2's fixed internals (diffusion, buildup, LF damping), voiced at the listening check.
- How infinite Decay is bounded (§6.4), within the fork S7 already requires.
- Pre-delay's slew rate (§6.3).
- The skip rule when the reverb is off (§6.3), if any.
- Whether the patch version bumps (§6.5).
- The tail's multiple of Decay (§6.7).
- How the reverb's memory is allocated (in `activate()` or at construction), so long as
  `process()` never allocates.
